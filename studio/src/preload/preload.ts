import { contextBridge } from 'electron';

import type { AdaApi } from './api';

// The renderer's only way out of its sandbox. Keep it narrow: every entry here
// is reachable by any script the renderer ever runs.
const api: AdaApi = {
  versions: {
    electron: process.versions.electron,
    chrome: process.versions.chrome,
    node: process.versions.node,
  },
};

contextBridge.exposeInMainWorld('ada', api);
