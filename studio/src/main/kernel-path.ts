import path from 'node:path';

export interface KernelSearch {
  /** ADA_MCP_PATH, if set: always wins. */
  override?: string;
  packaged: boolean;
  /** process.resourcesPath: where a packaged build ships the sidecar. */
  resourcesPath: string;
  /** app.getAppPath(): the studio/ directory during development. */
  appPath: string;
  platform: NodeJS.Platform;
}

/**
 * Where to look for ada-mcp. A packaged app ships it as an extra resource;
 * during development it comes from the CMake build tree next to studio/, in
 * any of the presets (see `pickKernel` for which one wins).
 */
export function kernelCandidates(search: KernelSearch): string[] {
  const exe = search.platform === 'win32' ? 'ada-mcp.exe' : 'ada-mcp';
  if (search.override) {
    return [search.override];
  }
  if (search.packaged) {
    return [path.join(search.resourcesPath, exe)];
  }
  const repo = path.resolve(search.appPath, '..');
  return ['release', 'dev', 'ci'].map((preset) => path.join(repo, 'build', preset, 'bin', exe));
}

/**
 * The candidate to run: the most recently built one that exists. Preferring a
 * fixed preset would silently run a stale kernel after a rebuild of another
 * preset. `modified` returns a file's modification time, or null if it does
 * not exist.
 */
export function pickKernel(
  candidates: string[],
  modified: (file: string) => number | null,
): string | null {
  let best: string | null = null;
  let bestTime = -Infinity;
  for (const candidate of candidates) {
    const time = modified(candidate);
    if (time !== null && time > bestTime) {
      best = candidate;
      bestTime = time;
    }
  }
  return best;
}
