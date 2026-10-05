'use strict';

const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const {renderSources} = require('./localize_web_ui.js');

const root = path.resolve(__dirname, '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const routes = {
  '/compare': ['text/html; charset=utf-8', 'scripts/web-preview/compare.html'],
  '/app.css': ['text/css', 'src/web/app.css'],
  '/preview.css': ['text/css', 'scripts/web-preview/header.css'],
  '/preview.js': ['text/javascript', 'scripts/web-preview/header.js'],
  '/mobile-menu.css': ['text/css', 'scripts/web-preview/mobile-menu.css'],
  '/mobile-menu.js': ['text/javascript', 'scripts/web-preview/mobile-menu.js'],
  '/toolbar-buttons': ['text/html; charset=utf-8', 'scripts/web-preview/toolbar-buttons.html'],
  '/toolbar-buttons.js': ['text/javascript', 'scripts/web-preview/toolbar-buttons.js'],
  '/preset-cards': ['text/html; charset=utf-8', 'scripts/web-preview/preset-cards.html'],
  '/preset-cards.js': ['text/javascript', 'scripts/web-preview/preset-cards.js'],
  '/button-toggles': ['text/html; charset=utf-8', 'scripts/web-preview/button-toggles.html'],
  '/button-toggles.js': ['text/javascript', 'scripts/web-preview/button-toggles.js'],
  '/master-switch': ['text/html; charset=utf-8', 'scripts/web-preview/master-switch.html'],
  '/master-switch.js': ['text/javascript', 'scripts/web-preview/master-switch.js'],
  '/home-redesigns': ['text/html; charset=utf-8', 'scripts/web-preview/home-redesigns.html'],
  '/home-redesigns.js': ['text/javascript', 'scripts/web-preview/home-redesigns.js'],
  '/theme-toggle': ['text/html; charset=utf-8', 'scripts/web-preview/theme-toggle.html'],
  '/theme-toggle.js': ['text/javascript', 'scripts/web-preview/theme-toggle.js'],
  '/status-panels': ['text/html; charset=utf-8', 'scripts/web-preview/status-panels.html'],
  '/status-panels.js': ['text/javascript', 'scripts/web-preview/status-panels.js'],
  '/guard-ranges': ['text/html; charset=utf-8', 'scripts/web-preview/guard-ranges.html'],
  '/guard-ranges.js': ['text/javascript', 'scripts/web-preview/guard-ranges.js'],
  '/traffic-light': ['text/html; charset=utf-8', 'scripts/web-preview/traffic-light.html'],
  '/traffic-light.js': ['text/javascript', 'scripts/web-preview/traffic-light.js'],
  '/guard-profiles': ['text/html; charset=utf-8', 'scripts/web-preview/guard-profiles.html'],
  '/guard-profiles.js': ['text/javascript', 'scripts/web-preview/guard-profiles.js'],
};

function renderHome() {
  const [shell, home] = renderSources(['shell', 'home'].map(name => ({
    file: `src/web/html/${name}.html`, type: 'html',
    content: read(`src/web/html/${name}.html`),
  })), {language: 'en', allowUnused: true, machineType: 'paddle'}).sources;
  return shell.content
      .replace('<section id="view-home" class="view" data-view="home"></section>',
          `<section id="view-home" class="view" data-view="home">${home.content}</section>`)
      .replace('</head>', '<link rel="stylesheet" href="/preview.css"></head>')
      .replace('<script type="module" src="/app.js?v=__FW_VERSION__"></script>',
          '<script src="/preview.js" defer></script>')
      .replace(/<link rel="(?:manifest|icon|apple-touch-icon)"[^>]*>/g, '')
      .replace(/__FW_VERSION__/g, 'design-preview');
}

function renderSignals() {
  const runtime = read('src/web/js/runtime.js');
  const content = runtime.slice(runtime.indexOf('function updateHeaderSignals('),
      runtime.indexOf('\nlet homeBootDone=')).replace(/^export /gm, '');
  return renderSources([{file: 'src/web/js/runtime.js', type: 'js', content}],
      {language: 'en', allowUnused: true}).sources[0].content;
}

function renderMobileMenu() {
  const views = ['stats', 'history', 'settings', 'diagnostic', 'admin'];
  const {sources} = renderSources(views.map(name => ({
    file: `src/web/html/${name}.html`, type: 'html',
    content: read(`src/web/html/${name}.html`),
  })), {language: 'en', allowUnused: true, machineType: 'paddle'});
  let html = renderHome();
  views.forEach((name, index) => {
    html = html.replace(`<section id="view-${name}" class="view" data-view="${name}"></section>`,
        `<section id="view-${name}" class="view" data-view="${name}" hidden>${sources[index].content}</section>`);
  });
  return html.replace('width=device-width,initial-scale=1', 'width=device-width,initial-scale=1,viewport-fit=cover')
      .replace('</head>', '<link rel="stylesheet" href="/mobile-menu.css"></head>')
      .replace('</body>', '<script src="/mobile-menu.js" defer></script></body>');
}

function renderMicra() {
  return renderMobileMenu()
      .replace('<link rel="stylesheet" href="/preview.css">', '')
      .replace('<link rel="stylesheet" href="/mobile-menu.css">', '')
      .replace('<script src="/preview.js" defer></script>', '')
      .replace('<script src="/mobile-menu.js" defer></script>', '<script src="/micra-preview.js" defer></script>');
}

function renderMicraScript() {
  const runtime = read('src/web/js/runtime.js'), diagnostic = read('src/web/js/diagnostic.js');
  const content = runtime.slice(runtime.indexOf('const MICRA_SWITCHES='), runtime.indexOf('function renderLineaMicraDiagnostic(')) +
      diagnostic.slice(0, diagnostic.indexOf('function formatScaleDisconnect('));
  return renderSources([{file: 'src/web/js/runtime.js', type: 'js', content}],
      {language: 'en', allowUnused: true}).sources[0].content + '\n' + read('scripts/web-preview/micra.js');
}

function createServer() {
  return http.createServer((req, res) => {
    const pathname = new URL(req.url, 'http://localhost').pathname;
    if (req.method !== 'GET' && req.method !== 'HEAD') {
      res.writeHead(405, {Allow: 'GET, HEAD'}).end();
      return;
    }
    try {
      const asset = routes[pathname];
      const mobileMenu = /^\/mobile-menu(?:\/(?:stats|history|settings|diagnostic|admin))?\/?$/.test(pathname);
      const micra = pathname === '/micra' || pathname === '/micra-preview.js';
      if (pathname !== '/' && !mobileMenu && !micra && !asset) {
        res.writeHead(404).end('Preview resource not found');
        return;
      }
      const body = pathname === '/micra' ? renderMicra() : pathname === '/micra-preview.js' ? renderMicraScript() : mobileMenu ? renderMobileMenu() : pathname === '/preview.js' ? renderSignals() + '\n' + read(asset[1]) : asset ? read(asset[1]) : renderHome();
      res.writeHead(200, {'Content-Type': pathname === '/micra-preview.js' ? 'text/javascript' : asset ? asset[0] : 'text/html; charset=utf-8',
        'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff',
        'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; connect-src 'none'; frame-ancestors 'self'; form-action 'none'"});
      res.end(req.method === 'HEAD' ? undefined : body);
    } catch (error) {
      console.error(error.message);
      res.writeHead(500).end('Unable to render the design preview');
    }
  });
}

if (require.main === module) {
  const port = Number(process.env.PORT || 4173);
  if (!Number.isInteger(port) || port < 1 || port > 65535) {
    throw new Error('PORT must be an integer from 1 to 65535');
  }
  const server = createServer();
  server.on('error', error => { console.error(error.message); process.exitCode = 1; });
  server.listen(port, '127.0.0.1', () => {
    console.log(`Signal design preview: http://127.0.0.1:${port}/`);
  });
}

module.exports = {createServer, renderHome};
