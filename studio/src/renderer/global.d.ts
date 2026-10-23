import type { AdaApi } from '../preload/api';

declare global {
  interface Window {
    ada: AdaApi;
  }
}

export {};
