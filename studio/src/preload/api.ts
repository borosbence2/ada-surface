import type { KernelStatus, LoadResult, Selection, Startup } from '../shared/types';

/** Shape of `window.ada`, the only API the preload script exposes to the renderer. */
export interface AdaApi {
  versions: {
    electron: string;
    chrome: string;
    node: string;
  };
  /** Scene and view to start with (command line, environment, or the scene open last time). */
  startup(): Promise<Startup>;
  /** The kernel's state right now; `onKernelStatus` reports changes after that. */
  kernelStatus(): Promise<KernelStatus>;
  /** Native file dialog; resolves to the chosen scene path, or null. */
  chooseScene(): Promise<string | null>;
  /** Opens `path` in the kernel (or reloads the current scene) and fetches everything to draw. */
  loadScene(path?: string): Promise<LoadResult>;
  /** Writes selection.json next to the scene for the agent to read. */
  writeSelection(selection: Selection): Promise<void>;
  restartKernel(): Promise<void>;
  /** Each subscription returns its unsubscribe function. */
  onKernelStatus(listener: (status: KernelStatus) => void): () => void;
  onSceneChanged(listener: () => void): () => void;
}

/** IPC channel names, shared by main and preload so they cannot drift apart. */
export const Channels = {
  startup: 'ada:startup',
  kernelStatusNow: 'ada:kernel-status-now',
  chooseScene: 'ada:choose-scene',
  loadScene: 'ada:load-scene',
  writeSelection: 'ada:write-selection',
  restartKernel: 'ada:restart-kernel',
  kernelStatus: 'ada:kernel-status',
  sceneChanged: 'ada:scene-changed',
} as const;
