import { contextBridge, ipcRenderer, type IpcRendererEvent } from 'electron';

import type { KernelStatus, Selection } from '../shared/types';
import { Channels, type AdaApi } from './api';

// The renderer's only way out of its sandbox. Keep it narrow: every entry here
// is reachable by any script the renderer ever runs. Each method maps to one
// fixed IPC channel; the renderer never names channels itself.
const api: AdaApi = {
  versions: {
    electron: process.versions.electron,
    chrome: process.versions.chrome,
    node: process.versions.node,
  },
  startup: () => ipcRenderer.invoke(Channels.startup),
  kernelStatus: () => ipcRenderer.invoke(Channels.kernelStatusNow),
  chooseScene: () => ipcRenderer.invoke(Channels.chooseScene),
  loadScene: (path?: string) => ipcRenderer.invoke(Channels.loadScene, path),
  writeSelection: (selection: Selection) => ipcRenderer.invoke(Channels.writeSelection, selection),
  restartKernel: () => ipcRenderer.invoke(Channels.restartKernel),
  onKernelStatus: (listener) => {
    const handler = (_event: IpcRendererEvent, status: KernelStatus) => listener(status);
    ipcRenderer.on(Channels.kernelStatus, handler);
    return () => ipcRenderer.removeListener(Channels.kernelStatus, handler);
  },
  onSceneChanged: (listener) => {
    const handler = () => listener();
    ipcRenderer.on(Channels.sceneChanged, handler);
    return () => ipcRenderer.removeListener(Channels.sceneChanged, handler);
  },
};

contextBridge.exposeInMainWorld('ada', api);
