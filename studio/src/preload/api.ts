/** Shape of `window.ada`, the only API the preload script exposes to the renderer. */
export interface AdaApi {
  versions: {
    electron: string;
    chrome: string;
    node: string;
  };
}
