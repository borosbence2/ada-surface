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
 * Where to look for ada-mcp, in order. A packaged app ships it as an extra
 * resource; during development it comes from the CMake build tree next to
 * studio/, preferring an optimised build because the report runs much faster
 * there.
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
