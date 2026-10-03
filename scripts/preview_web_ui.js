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
      if (pathname !== '/' && !mobileMenu && !asset) {
        res.writeHead(404).end('Preview resource not found');
        return;
      }
      const body = mobileMenu ? renderMobileMenu() : pathname === '/preview.js' ? renderSignals() + '\n' + read(asset[1]) : asset ? read(asset[1]) : renderHome();
      res.writeHead(200, {'Content-Type': asset ? asset[0] : 'text/html; charset=utf-8',
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
