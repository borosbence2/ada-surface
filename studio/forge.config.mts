import fs from 'node:fs';
import path from 'node:path';

import type { ForgeConfig } from '@electron-forge/shared-types';
import { MakerSquirrel } from '@electron-forge/maker-squirrel';
import { MakerZIP } from '@electron-forge/maker-zip';
import { VitePlugin } from '@electron-forge/plugin-vite';
import { FusesPlugin } from '@electron-forge/plugin-fuses';
import { FuseV1Options, FuseVersion } from '@electron/fuses';

// The kernel ships next to the app as an extra resource (resourcesPath), where
// src/main/kernel-path.ts looks for it in a packaged build. Forge runs from
// studio/, so the CMake build tree is one level up. Like the dev-time lookup,
// the most recently built preset wins, so a stale build is never packaged by
// accident. Without a built kernel the app still packages, and reports at
// runtime that ada-mcp is missing.
const kernelExe = process.platform === 'win32' ? 'ada-mcp.exe' : 'ada-mcp';
const kernel = ['release', 'dev', 'ci']
  .map((preset) => path.resolve(process.cwd(), '..', 'build', preset, 'bin', kernelExe))
  .filter((candidate) => fs.existsSync(candidate))
  .sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs)[0];
if (!kernel) {
  console.warn('[ada] no ada-mcp build found; packaging Studio without its kernel');
}

const config: ForgeConfig = {
  packagerConfig: {
    name: 'ADA Studio',
    executableName: 'ada-studio',
    asar: true,
    extraResource: kernel ? [kernel] : [],
  },
  rebuildConfig: {},
  // Installers are an M9 task; until then `package` (no maker) is what CI runs.
  makers: [new MakerSquirrel({}), new MakerZIP({}, ['darwin'])],
  plugins: [
    new VitePlugin({
      build: [
        {
          entry: 'src/main/main.ts',
          config: 'vite.main.config.mts',
          target: 'main',
        },
        {
          entry: 'src/preload/preload.ts',
          config: 'vite.preload.config.mts',
          target: 'preload',
        },
      ],
      renderer: [
        {
          name: 'main_window',
          config: 'vite.renderer.config.mts',
        },
      ],
    }),
    // Fuses switch off Electron features the app never needs, at package time.
    new FusesPlugin({
      version: FuseVersion.V1,
      [FuseV1Options.RunAsNode]: false,
      [FuseV1Options.EnableCookieEncryption]: true,
      [FuseV1Options.EnableNodeOptionsEnvironmentVariable]: false,
      [FuseV1Options.EnableNodeCliInspectArguments]: false,
      [FuseV1Options.EnableEmbeddedAsarIntegrityValidation]: true,
      [FuseV1Options.OnlyLoadAppFromAsar]: true,
    }),
  ],
};

export default config;
