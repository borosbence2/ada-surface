import { useCallback, useEffect, useRef, useState } from 'react';

import type { KernelStatus, SceneBundle, Selection } from '../shared/types';
import { kDefaultDisplay, kViewModes, type DisplayOptions } from './display';
import { Legend } from './Legend';
import { ReportPanel } from './ReportPanel';
import { Toolbar } from './Toolbar';
import { Viewport } from './Viewport';

function kernelLabel(status: KernelStatus): string {
  switch (status.state) {
    case 'starting':
      return 'kernel: starting…';
    case 'ready':
      return `kernel ${status.version}`;
    case 'stopped':
      return 'kernel: stopped';
  }
}

function fileName(path: string): string {
  return path.split(/[\\/]/).pop() ?? path;
}

export function App() {
  const [kernel, setKernel] = useState<KernelStatus>({ state: 'starting' });
  const [bundle, setBundle] = useState<SceneBundle | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [options, setOptions] = useState<DisplayOptions>(kDefaultDisplay);
  const [selection, setSelection] = useState<Selection | null>(null);
  /** The scene to show: set by Open, by the startup scene, and kept across kernel restarts. */
  const scenePath = useRef<string | null>(null);

  const load = useCallback(async (path?: string) => {
    const target = path ?? scenePath.current;
    if (!target) return;
    setLoading(true);
    const result = await window.ada.loadScene(target);
    setLoading(false);
    if (result.ok) {
      if (result.bundle.path !== scenePath.current) {
        setSelection(null);
      }
      scenePath.current = result.bundle.path;
      setBundle(result.bundle);
      setError(null);
    } else {
      // Keep showing the last good geometry: a half-saved file should not
      // blank the viewport.
      setError(result.error);
    }
  }, []);

  useEffect(() => {
    let disposed = false;
    const onStatus = (status: KernelStatus) => {
      if (disposed) return;
      setKernel(status);
      if (status.state === 'ready') {
        // Startup, or after a restart: (re)open whatever scene is current.
        void load();
      }
    };
    const unsubscribe = window.ada.onKernelStatus(onStatus);
    // Ask for the startup scene first, then for the kernel's current state,
    // so that a kernel that became ready before we subscribed is not missed.
    void window.ada.startup().then(async (startup) => {
      if (disposed) return;
      scenePath.current ??= startup.scene;
      const view = kViewModes.find((m) => m.mode === startup.view);
      if (view) setOptions((o) => ({ ...o, mode: view.mode }));
      onStatus(await window.ada.kernelStatus());
    });
    return () => {
      disposed = true;
      unsubscribe();
    };
  }, [load]);

  useEffect(() => window.ada.onSceneChanged(() => void load()), [load]);

  const open = async () => {
    const path = await window.ada.chooseScene();
    if (path) await load(path);
  };

  const pick = (next: Selection | null) => {
    setSelection(next);
    if (next) void window.ada.writeSelection(next);
  };

  return (
    <div className="app">
      <Toolbar
        options={options}
        onChange={setOptions}
        onOpen={() => void open()}
        onReload={() => void load()}
        canReload={bundle !== null && kernel.state === 'ready'}
      />
      <main className="workspace">
        <div className="stage">
          <Viewport bundle={bundle} options={options} selection={selection} onPick={pick} />
          <Legend options={options} onChange={setOptions} />
          {!bundle && kernel.state === 'ready' && !loading && (
            <div className="empty">
              <p>No scene open.</p>
              <button onClick={() => void open()}>Open a scene…</button>
              <p className="empty__hint">Try cases/examples/hood.ada.json</p>
            </div>
          )}
          {error && <div className="banner banner--error">{error}</div>}
          {kernel.state === 'stopped' && (
            <div className="banner banner--error">
              {kernel.message}
              <button onClick={() => void window.ada.restartKernel()}>Restart kernel</button>
            </div>
          )}
        </div>
        <ReportPanel report={bundle?.report ?? null} selection={selection} />
      </main>
      <footer className="status-bar">
        <span>ADA Studio</span>
        <span className="status-bar__scene" title={bundle?.path}>
          {bundle ? fileName(bundle.path) : 'no scene'}
          {loading ? ' · loading…' : ''}
        </span>
        <span className="status-bar__right">
          {kernelLabel(kernel)} · Electron {window.ada.versions.electron}
        </span>
      </footer>
    </div>
  );
}
