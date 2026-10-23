import fs from 'node:fs';
import path from 'node:path';

/**
 * Editors save in different ways (write in place, or write a temp file and
 * rename it over the original), and a burst of change events follows either.
 * Watching the directory and filtering by name catches both kinds of save,
 * and the debounce turns each burst into one reload.
 */
const kDebounceMs = 150;

export class SceneWatcher {
  private watcher: fs.FSWatcher | null = null;
  private timer: NodeJS.Timeout | null = null;

  constructor(private readonly onChange: () => void) {}

  watch(file: string): void {
    this.close();
    const name = path.basename(file);
    this.watcher = fs.watch(path.dirname(file), (_event, changed) => {
      if (changed !== null && changed.toString() !== name) {
        return;
      }
      if (this.timer) {
        clearTimeout(this.timer);
      }
      this.timer = setTimeout(() => {
        this.timer = null;
        this.onChange();
      }, kDebounceMs);
    });
  }

  close(): void {
    this.watcher?.close();
    this.watcher = null;
    if (this.timer) {
      clearTimeout(this.timer);
      this.timer = null;
    }
  }
}
