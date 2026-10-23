import { app, BrowserWindow } from 'electron';
import path from 'node:path';
import started from 'electron-squirrel-startup';

// Squirrel.Windows launches the app with install/uninstall flags to manage
// shortcuts; there is nothing else to do in that case.
if (started) {
  app.quit();
}

const createWindow = () => {
  const window = new BrowserWindow({
    width: 1280,
    height: 800,
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
};

void app.whenReady().then(() => {
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});
