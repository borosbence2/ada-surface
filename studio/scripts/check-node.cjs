// Runs before `npm start` and `npm run package`. On an older Node, Electron
// Forge dies deep inside its own code with an unhelpful SyntaxError
// ("node:util does not provide an export named 'styleText'"); say what is
// actually wrong instead. Kept as plain CommonJS so that old Node versions can
// still parse it.
const fs = require('fs');
const path = require('path');

const required = Number(
  fs.readFileSync(path.join(__dirname, '..', '.node-version'), 'utf8').trim(),
);
const actual = Number(process.versions.node.split('.')[0]);

if (actual < required) {
  console.error(
    `ADA Studio needs Node ${required} or newer, but this shell runs ${process.version} (${process.execPath}).`,
  );
  console.error(
    'If Node was upgraded recently: terminals inherit PATH from the program that opened them, ' +
      'so restart the editor (all windows), not just the terminal.',
  );
  process.exit(1);
}
