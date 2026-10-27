import { app, BrowserWindow, dialog, ipcMain } from 'electron';
import fs from 'node:fs';
import path from 'node:path';
import started from 'electron-squirrel-startup';

import { Channels } from '../preload/api';
import type {
  CvHull,
  ExportOutcome,
  KernelStatus,
  LoadResult,
  PatchCombs,
  QualityReportJson,
  Selection,
  SolveOutcome,
  Startup,
} from '../shared/types';
import { kViewModes, resolveViewMode } from '../shared/views';
import { decodeDeviations, decodeTessellation } from './decode';
import { Kernel } from './kernel';
import { kernelCandidates, pickKernel } from './kernel-path';
import { SceneWatcher } from './scene-watcher';

// Squirrel.Windows launches the app with install/uninstall flags to manage
// shortcuts; there is nothing else to do in that case.
if (started) {
  app.quit();
}

/** Display resolution: smooth enough for degree-7 patches, small enough to reload instantly. */
const kTessellationGrid = 49;
const kCombSamples = 33;

let window: BrowserWindow | null = null;
let kernel: Kernel | null = null;
let kernelStatus: KernelStatus = { state: 'starting' };
let scenePath: string | null = null;

/** Loads are serialised: a file save during a load queues behind it instead of interleaving tool calls. */
let loading: Promise<unknown> = Promise.resolve();

const watcher = new SceneWatcher(() => window?.webContents.send(Channels.sceneChanged));

function statePath(): string {
  return path.join(app.getPath('userData'), 'studio-state.json');
}

function rememberScene(file: string): void {
  try {
    fs.writeFileSync(statePath(), JSON.stringify({ lastScene: file }));
  } catch {
    // Remembering the last scene is a convenience; failing to is not an error.
  }
}

function flag(name: string): string | undefined {
  const prefix = `--${name}=`;
  return process.argv.find((a) => a.startsWith(prefix))?.slice(prefix.length);
}

/** --scene=<path>, then ADA_SCENE, then the scene open last time. */
function startupScene(): string | null {
  const candidates = [flag('scene'), process.env.ADA_SCENE];
  try {
    candidates.push(
      (JSON.parse(fs.readFileSync(statePath(), 'utf8')) as { lastScene?: string }).lastScene,
    );
  } catch {
    // no state yet
  }
  const found = candidates.find((c): c is string => typeof c === 'string' && fs.existsSync(c));
  return found ? path.resolve(found) : null;
}

/**
 * --screenshot=<file.png>: capture the window once the renderer reports that
 * a scene is on screen, then quit. Lets a script (or an agent) look at what
 * Studio renders. Waiting for the renderer's signal rather than a timer
 * matters in development, where Vite may reload the page while dependencies
 * are re-optimised.
 */
const screenshotPath = flag('screenshot');

function captureAndQuit(): void {
  void window?.webContents.capturePage().then((image) => {
    fs.writeFileSync(path.resolve(screenshotPath!), image.toPNG());
    app.quit();
  });
}

function setKernelStatus(status: KernelStatus): void {
  kernelStatus = status;
  window?.webContents.send(Channels.kernelStatus, status);
}

async function startKernel(): Promise<void> {
  const candidates = kernelCandidates({
    override: process.env.ADA_MCP_PATH,
    packaged: app.isPackaged,
    resourcesPath: process.resourcesPath,
    appPath: app.getAppPath(),
    platform: process.platform,
  });
  const binary = pickKernel(candidates, (file) => {
    try {
      return fs.statSync(file).mtimeMs;
    } catch {
      return null;
    }
  });
  if (!binary) {
    setKernelStatus({
      state: 'stopped',
      message: `ada-mcp not found; build the kernel first (looked in ${candidates.join(', ')})`,
    });
    return;
  }
  kernel = new Kernel(binary, app.getVersion(), setKernelStatus);
  try {
    await kernel.start();
  } catch {
    // start() has already reported the failure through the status.
  }
}

async function load(file?: string): Promise<LoadResult> {
  if (!kernel || kernelStatus.state !== 'ready') {
    return { ok: false, error: 'the kernel is not running' };
  }
  const k = kernel;
  const target = file ?? scenePath;
  if (!target) {
    return { ok: false, error: 'no scene is open' };
  }
  try {
    await k.call('scene_open', { path: target });
    if (target !== scenePath) {
      scenePath = target;
      watcher.watch(target);
      rememberScene(target);
    }
    const [report, tessellation, hulls, combs, deviations] = await Promise.all([
      k.call('quality_report'),
      k.call('tessellate', { grid: kTessellationGrid }),
      k.call('cv_hulls'),
      k.call('boundary_combs', { samples: kCombSamples }),
      k.call('fit_deviations'),
    ]);
    return {
      ok: true,
      bundle: {
        path: target,
        report: report as unknown as QualityReportJson,
        tessellation: decodeTessellation(tessellation),
        hulls: (hulls as { patches: CvHull[] }).patches,
        combs: (combs as { patches: PatchCombs[] }).patches,
        deviations: decodeDeviations(deviations),
      },
    };
  } catch (error) {
    return { ok: false, error: error instanceof Error ? error.message : String(error) };
  }
}

/** Curve adjustments smaller than this are rounding; not worth showing. */
const kReportedCurveDeviationMm = 1e-6;

async function solveScene(): Promise<SolveOutcome> {
  if (!kernel || kernelStatus.state !== 'ready') {
    return { ok: false, error: 'the kernel is not running' };
  }
  if (!scenePath) {
    return { ok: false, error: 'no scene is open' };
  }
  try {
    await kernel.call('scene_open', { path: scenePath });
    const result = (await kernel.call('solve')) as {
      milliseconds: number;
      curves: { curve: string; max_mm: number }[];
      report: { pass: boolean };
    };
    const moved = result.curves
      .filter((c) => c.max_mm > kReportedCurveDeviationMm)
      .sort((a, b) => b.max_mm - a.max_mm);
    return { ok: true, pass: result.report.pass, milliseconds: result.milliseconds, moved };
  } catch (error) {
    return { ok: false, error: error instanceof Error ? error.message : String(error) };
  }
}

async function exportIges(): Promise<ExportOutcome | null> {
  if (!kernel || kernelStatus.state !== 'ready' || !scenePath) {
    return { ok: false, error: 'open a scene with the kernel running first' };
  }
  const options = {
    title: 'Export IGES',
    defaultPath: scenePath.replace(/\.ada\.json$|\.json$/, '') + '.igs',
    filters: [{ name: 'IGES', extensions: ['igs', 'iges'] }],
  };
  const choice = window
    ? await dialog.showSaveDialog(window, options)
    : await dialog.showSaveDialog(options);
  if (choice.canceled || !choice.filePath) {
    return null;
  }
  try {
    await kernel.call('scene_open', { path: scenePath });
    const result = (await kernel.call('export_iges', { path: choice.filePath })) as {
      path: string;
      patches: number;
    };
    return { ok: true, path: result.path, patches: result.patches };
  } catch (error) {
    return { ok: false, error: error instanceof Error ? error.message : String(error) };
  }
}

function registerIpc(): void {
  ipcMain.handle(Channels.chooseScene, async () => {
    const options = {
      title: 'Open scene',
      filters: [{ name: 'ADA scene', extensions: ['json'] }],
      properties: ['openFile' as const],
    };
    const result = window
      ? await dialog.showOpenDialog(window, options)
      : await dialog.showOpenDialog(options);
    return result.canceled ? null : (result.filePaths[0] ?? null);
  });

  ipcMain.handle(Channels.loadScene, (_event, file?: string) => {
    const next = loading.then(() => load(file));
    loading = next.catch(() => undefined);
    return next;
  });

  ipcMain.handle(Channels.exportIges, () => {
    const next = loading.then(() => exportIges());
    loading = next.catch(() => undefined);
    return next;
  });

  ipcMain.handle(Channels.solveScene, () => {
    const next = loading.then(() => solveScene());
    loading = next.catch(() => undefined);
    return next;
  });

  ipcMain.on(Channels.sceneRendered, () => {
    if (screenshotPath) {
      captureAndQuit();
    }
  });

  ipcMain.handle(Channels.writeSelection, (_event, selection: Selection) => {
    if (!scenePath) {
      return;
    }
    const target = path.join(path.dirname(scenePath), 'selection.json');
    fs.writeFileSync(
      target,
      `${JSON.stringify({ ...selection, time: new Date().toISOString() }, null, 2)}\n`,
    );
  });

  ipcMain.handle(Channels.restartKernel, async () => {
    await startKernel();
  });
}

function createWindow(): void {
  window = new BrowserWindow({
    width: 1440,
    height: 900,
    title: 'ADA Studio',
    backgroundColor: '#2b2d31',
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      // Security baseline from CLAUDE.md. These are Electron's defaults; they
      // are spelled out so that loosening one has to be a visible change.
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
    },
  });

  if (MAIN_WINDOW_VITE_DEV_SERVER_URL) {
    void window.loadURL(MAIN_WINDOW_VITE_DEV_SERVER_URL);
  } else {
    void window.loadFile(path.join(__dirname, `../renderer/${MAIN_WINDOW_VITE_NAME}/index.html`));
  }
  window.on('closed', () => {
    window = null;
  });
}

void app.whenReady().then(async () => {
  const viewFlag = flag('view');
  const view = viewFlag === undefined ? null : resolveViewMode(viewFlag);
  if (viewFlag !== undefined && view === null) {
    console.error(
      `ADA Studio: unknown --view '${viewFlag}'; use one of ${kViewModes.map((m) => m.mode).join(', ')}`,
    );
    app.exit(2);
    return;
  }
  const startup: Startup = { scene: startupScene(), view };
  ipcMain.handle(Channels.startup, () => startup);
  ipcMain.handle(Channels.kernelStatusNow, () => kernelStatus);
  registerIpc();
  createWindow();
  await startKernel();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  watcher.close();
  void kernel?.stop();
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
