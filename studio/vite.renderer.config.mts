import react from '@vitejs/plugin-react';
import { defineConfig, type Plugin } from 'vite';

/**
 * Content-Security-Policy for packaged builds only. The renderer loads nothing
 * but its own bundle; inline styles stay allowed because React's style props
 * produce them. In development Vite injects an inline React-refresh script,
 * which this policy would block, so `apply: 'build'` leaves dev alone (and
 * Electron's dev-time CSP warning is expected there).
 */
const kContentSecurityPolicy = [
  "default-src 'self'",
  "script-src 'self'",
  "style-src 'self' 'unsafe-inline'",
  "img-src 'self' data:",
  "connect-src 'self'",
].join('; ');

const csp: Plugin = {
  name: 'ada-content-security-policy',
  apply: 'build',
  transformIndexHtml(html) {
    return html.replace(
      '<head>',
      `<head>
    <meta http-equiv="Content-Security-Policy" content="${kContentSecurityPolicy}" />`,
    );
  },
};

// https://vitejs.dev/config
export default defineConfig({
  plugins: [react(), csp],
});
