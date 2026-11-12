#!/usr/bin/env node
// Browser regression tests with simulated device responses; no hardware is contacted.
// Node 22+ and Chrome required. CHROME_BIN overrides the browser executable.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtemp, readFile, mkdir, writeFile, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const chromePath = process.env.CHROME_BIN || (process.platform === 'darwin' ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' : 'google-chrome');
const profile = await mkdtemp(join(tmpdir(), 'crystal-panel-test-'));
const preview = process.env.CRYSTAL_PANEL_PREVIEW_DIR;
const chrome = spawn(chromePath, ['--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check', '--remote-debugging-port=0', '--user-data-dir=' + profile, 'about:blank'], { stdio: 'ignore' });
let launchError, socket;
chrome.on('error', error => { launchError = error; });
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(fn, label) {
  for (let count = 0; count < 200; ++count) { if (await fn()) return; await delay(50); }
  throw Error('Timed out: ' + label);
}
try {
  let port;
  await until(async () => { if (launchError) throw launchError; try { port = (await readFile(join(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]; return port; } catch { return false; } }, 'Chrome startup');
  const targets = await (await fetch('http://127.0.0.1:' + port + '/json/list')).json();
  socket = new WebSocket(targets.find(target => target.type === 'page').webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
  let sequence = 0; const pending = new Map(), exceptions = [];
  socket.onmessage = event => {
    const message = JSON.parse(event.data);
    if (message.id) { const item = pending.get(message.id); pending.delete(message.id); if (message.error) item.reject(Error(message.error.message)); else item.resolve(message.result); }
    if (message.method === 'Runtime.exceptionThrown') exceptions.push(message.params.exceptionDetails);
  };
  function cdp(method, params = {}) { return new Promise((resolve, reject) => { const id = ++sequence; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); }); }
  async function evaluate(expression) {
    const result = await cdp('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
    if (result.exceptionDetails) throw Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  }
  async function waitFor(expression, label) { await until(() => evaluate(expression), label); }
  async function click(selector) { await evaluate('document.querySelector(' + JSON.stringify(selector) + ').click()'); }
  async function screenshot(name) {
    if (!preview) return;
    await mkdir(preview, { recursive: true });
    const result = await cdp('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true });
    await writeFile(join(preview, name + '.png'), Buffer.from(result.data, 'base64'));
  }
  await cdp('Runtime.enable'); await cdp('Page.enable');
  await cdp('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1100, deviceScaleFactor: 1, mobile: false });
  await cdp('Page.addScriptToEvaluateOnNewDocument', { source: '(' + function () {
    window.fixture = { calls: [], failSystem: 0, uploadStatus: 202, uploadCount: 0, apps: [
      { id: 'clock', name: 'Clock', origin: 'builtin', enabled: true, slot: 2 },
      { id: 'weather', name: 'Weather', origin: 'builtin', enabled: true, slot: 3 },
      { id: 'org.crystal.declarative-dashboard', name: 'Status Dashboard', origin: 'package', enabled: true, slot: 16, version: '1.0.0' }
    ] };
    const realFetch = window.fetch;
    window.fetch = async (path, options = {}) => {
      if (!String(path).startsWith('/api/')) return realFetch(path, options);
      const f = window.fixture, body = options.body ? JSON.parse(options.body) : null;
      f.calls.push({ path, method: options.method || 'GET', body, headers: options.headers });
      const json = (data, status = 200) => new Response(JSON.stringify(data), { status, headers: { 'Content-Type': 'application/json' } });
      if (path === '/api/v1/pair') return body.pin === '123456' ? json({ token: 'test-session-token', expires_in: 86400 }) : json({ error: 'pairing failed' }, 401);
      if (options.headers.Authorization !== 'Bearer test-session-token') return json({ error: 'unauthorized' }, 401);
      if (path === '/api/v1/system') return f.failSystem ? json({ error: 'Temporary device error' }, f.failSystem) : json({ os_version: '0.12.14', free_psram_kb: 4385, free_storage_kb: 1840, serial: 'CRYSTAL-7A21' });
      if (path === '/api/v1/apps' && !options.method) return json({ apps: f.apps });
      if (path.endsWith('/enabled')) { f.apps.find(app => path.includes(encodeURIComponent(app.id))).enabled = body.enabled; return json({ saved: true, restart_required: true }, 202); }
      if (path === '/api/v1/apps/order') { for (const item of body) f.apps.find(app => app.id === item.id).slot = item.slot; return json({ saved: true, restart_required: true }, 202); }
      if (path === '/api/v1/logs') {
        const chunks = ['id: 1\nda', 'ta: First message\n\ndata: Weather ready\n\n'];
        return new Response(new ReadableStream({ start(controller) { for (const chunk of chunks) controller.enqueue(new TextEncoder().encode(chunk)); controller.close(); } }), { headers: { 'Content-Type': 'text/event-stream' } });
      }
      return json({ queued: true, restart_required: false }, 202);
    };
    window.XMLHttpRequest = class {
      constructor() { this.upload = {}; }
      open(method, path) { this.method = method; this.path = path; }
      setRequestHeader(name, value) { this.authorization = value; }
      send(file) {
        ++window.fixture.uploadCount;
        if (this.authorization !== 'Bearer test-session-token') throw Error('Missing upload token');
        this.upload.onprogress({ lengthComputable: true, loaded: file.size, total: file.size });
        this.status = window.fixture.uploadStatus;
        this.responseText = JSON.stringify(this.status === 202 ? { installed: true, restart_required: true } : { error: 'Package signature is not trusted' });
        setTimeout(() => this.onload(), 0);
      }
      abort() { if (this.onabort) this.onabort(); }
    };
  }.toString() + ')();' });
  await cdp('Page.navigate', { url: pathToFileURL(join(root, 'assets/control-panel/index.html')).href });
  await waitFor("!!document.getElementById('pair-form')", 'page loaded');
  await screenshot('pairing-desktop');
  await click('#pair');
  assert.equal(await evaluate('fixture.calls.length'), 0, 'invalid short PIN never contacts device');
  assert.match(await evaluate("document.getElementById('pair-status').textContent"), /six-digit/);
  await evaluate("document.getElementById('pin').value='111111'"); await click('#pair');
  await waitFor("document.getElementById('pair-status').classList.contains('error') && !document.getElementById('pair').disabled", 'rejected PIN');
  assert.match(await evaluate("document.getElementById('pair-status').textContent"), /new code/);
  await screenshot('pairing-error');
  async function pair() { await evaluate("document.getElementById('pin').value='123456'"); await click('#pair'); await waitFor("document.querySelectorAll('.app').length===3 && !document.getElementById('refresh').disabled", 'paired dashboard'); }
  await pair();
  assert.equal(await evaluate("document.getElementById('pairing').classList.contains('hidden')"), true);
  assert.equal(await evaluate("document.querySelectorAll('#system .metric').length"), 4);
  await screenshot('dashboard-desktop');
  async function appButton(text) { await evaluate("[...document.querySelectorAll('.app:last-child button')].find(b=>b.textContent===" + JSON.stringify(text) + ').click()'); }
  await appButton('Disable'); await waitFor("fixture.apps[2].enabled===false && !document.getElementById('refresh').disabled", 'disable app');
  assert.equal(await evaluate("document.querySelector('.app:last-child button').disabled"), true, 'disabled app cannot launch');
  await appButton('Enable'); await waitFor("fixture.apps[2].enabled===true && !document.getElementById('refresh').disabled", 'enable app');
  await click('.app:first-child button[title="Move down"]');
  await waitFor("fixture.apps[0].slot===3 && !document.getElementById('refresh').disabled", 'reorder apps');
  assert.deepEqual(await evaluate("fixture.calls.find(c=>c.path==='/api/v1/apps/order').body.map(a=>a.slot)"), [2, 3, 16], 'preserves existing slot values');
  await appButton('Uninstall'); assert.equal(await evaluate("document.getElementById('confirm-dialog').open"), true);
  await click('#confirm-cancel'); assert.equal(await evaluate("fixture.calls.some(c=>c.method==='DELETE')"), false, 'cancel cannot delete');
  await appButton('Clear data'); await click('#confirm-accept');
  await waitFor("fixture.calls.some(c=>c.method==='DELETE' && c.path.endsWith('/data')) && !document.getElementById('refresh').disabled", 'clear data confirmation');
  assert.match(await evaluate("document.getElementById('device-status').textContent"), /queued/, 'queued action never claims completion');
  await evaluate("fixture.failSystem=500"); await click('#refresh');
  await waitFor("document.getElementById('device-status').classList.contains('error') && !document.getElementById('refresh').disabled", 'server error');
  assert.equal(await evaluate("document.getElementById('device').classList.contains('hidden')"), false, 'transient errors preserve session');
  await evaluate("fixture.failSystem=0"); await click('#refresh'); await waitFor("!document.getElementById('refresh').disabled", 'recovery');
  await evaluate("{const transfer=new DataTransfer();transfer.items.add(new File(['test package'],'dashboard.capp'));document.getElementById('package').files=transfer.files;document.getElementById('package').dispatchEvent(new Event('change'));}");
  await click('#install'); await waitFor("document.getElementById('install-status').classList.contains('success') && !document.getElementById('refresh').disabled", 'install success');
  assert.match(await evaluate("document.getElementById('install-status').textContent"), /Restart/);
  await evaluate('fixture.uploadStatus=400'); await click('#install'); await waitFor("document.getElementById('install-status').classList.contains('error') && !document.getElementById('refresh').disabled", 'signature rejection');
  assert.match(await evaluate("document.getElementById('install-status').textContent"), /not trusted/);
  await click('#logs'); await waitFor("document.getElementById('log-output').textContent.includes('Weather ready')", 'log stream');
  assert.equal(await evaluate("document.getElementById('log-output').textContent"), 'First message\nWeather ready\n', 'split SSE frames decode correctly');
  for (const width of [390, 320, 768]) {
    await cdp('Emulation.setDeviceMetricsOverride', { width, height: 900, deviceScaleFactor: 1, mobile: false });
    assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true, 'dashboard fits ' + width + 'px viewport');
    if (width === 390) await screenshot('dashboard-mobile');
  }
  await evaluate('fixture.failSystem=401'); await click('#refresh');
  await waitFor("!document.getElementById('pairing').classList.contains('hidden')", 'session expiry');
  assert.equal(await evaluate("document.getElementById('device').classList.contains('hidden')"), true);
  assert.equal(await evaluate("document.querySelectorAll('.app').length"), 0, 'expired session clears app data');
  for (const width of [390, 320, 768]) {
    await cdp('Emulation.setDeviceMetricsOverride', { width, height: 900, deviceScaleFactor: 1, mobile: false });
    assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true, 'pairing fits ' + width + 'px viewport');
    if (width === 390) await screenshot('pairing-mobile');
  }
  assert.deepEqual(exceptions, [], 'no uncaught browser exceptions');
  console.log('PASS: pairing, rejected PIN, session expiry, apps, enable/disable, ordering, confirmation, upload, errors, logs, and responsive layouts.');
  if (preview) console.log('Previews: ' + preview);
} finally {
  if (socket) socket.close();
  chrome.kill();
  await new Promise(resolve => { if (chrome.exitCode !== null) resolve(); else { chrome.once('exit', resolve); setTimeout(resolve, 2000); } });
  await rm(profile, { recursive: true, force: true, maxRetries: 3 });
}
