import { Viewport } from './Viewport';

export function App() {
  const { electron } = window.ada.versions;

  return (
    <div className="app">
      <Viewport />
      <footer className="status-bar">
        <span>ADA Studio</span>
        <span>kernel: not connected</span>
        <span className="status-bar__right">Electron {electron}</span>
      </footer>
    </div>
  );
}
