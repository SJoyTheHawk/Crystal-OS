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
  async function viewport(width) {
    await cdp('Emulation.setDeviceMetricsOverride', { width, height: 900, deviceScaleFactor: 1, mobile: false });
    await waitFor("innerWidth===" + width + " && document.getElementById('panel-tabs').hidden===" + (width > 800) + " && document.getElementById('panel-apps').getAttribute('role')===" + JSON.stringify(width <= 800 ? 'tabpanel' : 'region'), 'responsive layout at ' + width + 'px');
  }
  async function assertDesktopPanels() {
    assert.equal(await evaluate("document.getElementById('panel-tabs').hidden"), true, 'desktop has no tab navigation');
    assert.equal(await evaluate("['apps','install','device'].every(name=>{const p=document.getElementById('panel-'+name);return !p.hidden && p.getAttribute('role')==='region' && p.getAttribute('aria-labelledby')==='heading-'+name && !p.hasAttribute('tabindex')})"), true, 'desktop shows all three named sections');
    assert.equal(await evaluate("document.getElementById('panel-apps').getBoundingClientRect().right <= document.getElementById('panel-install').getBoundingClientRect().left"), true, 'desktop places installation beside the app list');
  }
  async function assertCompactAppRows(width) {
    assert.equal(await evaluate("document.querySelectorAll('#apps details, #apps summary').length"), 0, 'app actions are visible without More options');
    assert.equal(await evaluate("[...document.querySelectorAll('.app')].every(row=>{const h=row.querySelector('.app-header').getBoundingClientRect(),o=row.querySelector('.app-order').getBoundingClientRect(),r=row.getBoundingClientRect();return o.left>=h.right && o.right<=r.right && r.right-o.right<=16 && o.top< h.bottom && r.height<=" + (width <= 360 ? 190 : 150) + "})"), true, 'ordering sits on the right of compact rows at ' + width + 'px');
    assert.equal(await evaluate("[...document.querySelectorAll('.app-order')].every(order=>{const b=[...order.querySelectorAll('button')];return b.length===2 && b[0].textContent==='↑' && b[1].textContent==='↓' && b.every(button=>{const r=button.getBoundingClientRect();return r.width>=44 && r.height>=44 && button.getAttribute('aria-label').startsWith('Move ')})})"), true, 'symbol buttons retain accessible labels and touch-sized targets');
    assert.equal(await evaluate("document.querySelector('.app:first-child .app-order button[title=\"Move up\"]').disabled && document.querySelector('.app:last-child .app-order button[title=\"Move down\"]').disabled"), true, 'ordering disables arrows at the list boundaries');
  }
  async function assertSessionControls() {
    assert.equal(await evaluate("!!document.querySelector('.panel-toolbar #sign-out') && document.getElementById('sign-out').textContent==='Sign out' && document.getElementById('sign-out').getBoundingClientRect().width>=44"), true, 'Sign out stays in the top toolbar');
    assert.equal(await evaluate("!!document.querySelector('#panel-device .reboot-card .card-heading #reboot.secondary') && !document.getElementById('disconnect')"), true, 'Reboot uses the shared Device card and action style');
  }
  async function screenshot(name) {
    if (!preview) return;
    await mkdir(preview, { recursive: true });
    const result = await cdp('Page.captureScreenshot', { format: 'png', captureBeyondViewport: true });
    await writeFile(join(preview, name + '.png'), Buffer.from(result.data, 'base64'));
  }
  await cdp('Runtime.enable'); await cdp('Page.enable');
  await cdp('Emulation.setDeviceMetricsOverride', { width: 1440, height: 1100, deviceScaleFactor: 1, mobile: false });
  await cdp('Page.addScriptToEvaluateOnNewDocument', { source: '(' + function () {
    window.fixture = { calls: [], failSystem: 0, failReboot: 0, uploadStatus: 202, uploadCount: 0, apps: [
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
      if (path === '/api/v1/system/reboot') return f.failReboot ? json({ error: 'Could not schedule reboot' }, f.failReboot) : json({ rebooting: true }, 202);
      if (path === '/api/v1/apps' && !options.method) return json({ apps: f.apps });
      if (path.endsWith('/enabled')) { f.apps.find(app => path.includes(encodeURIComponent(app.id))).enabled = body.enabled; return json({ saved: true, restart_required: true }, 202); }
      if (path === '/api/v1/apps/order') { for (const item of body) f.apps.find(app => app.id === item.id).slot = item.slot; return json({ saved: true, restart_required: true }, 202); }
      if (String(path).startsWith('/api/v1/logs')) {
        const chunks = String(path).includes('since=2') ? [] : ['id: 1\nda', 'ta: First message\n\nid: 2\ndata: Weather ready\n\n'];
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
  assert.deepEqual(await evaluate("[...document.querySelectorAll('link[rel=icon],link[rel=apple-touch-icon]')].map(link=>[link.rel,link.getAttribute('href'),link.getAttribute('sizes')])"),
    [['icon', 'favicon.png', '48x48'], ['apple-touch-icon', 'apple-icon.png', '180x180']], 'desktop and mobile icons use optimized local assets');
  assert.deepEqual(await evaluate("Promise.all([...document.querySelectorAll('link[rel=icon],link[rel=apple-touch-icon]')].map(link=>new Promise((resolve,reject)=>{const icon=new Image();icon.onload=()=>resolve([icon.naturalWidth,icon.naturalHeight]);icon.onerror=()=>reject(Error('Icon failed to load'));icon.src=link.href})))"),
    [[48, 48], [180, 180]], 'both icons decode at their declared dimensions');
  await waitFor("document.querySelector('img.brand-mark').complete && document.querySelector('img.brand-mark').naturalWidth > 0", 'header logo loaded');
  assert.deepEqual(await evaluate("{const logo=document.querySelector('img.brand-mark');[logo.naturalWidth,logo.naturalHeight]}"), [144, 121], 'header uses the optimized logo');
  assert.equal(await evaluate("{const logo=document.querySelector('img.brand-mark');Math.abs(logo.width / logo.height - 144 / 121) < 0.04 && logo.width === 44}"), true, 'desktop logo fits without stretching');
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
  await assertDesktopPanels();
  await assertCompactAppRows(1440);
  await assertSessionControls();
  assert.equal(await evaluate("document.activeElement.id"), 'refresh', 'desktop pairing focuses a visible control');
  await screenshot('dashboard-desktop');
  async function appButton(text) { await evaluate("[...document.querySelectorAll('.app:last-child button')].find(b=>b.textContent===" + JSON.stringify(text) + ').click()'); }
  await appButton('Disable'); await waitFor("fixture.apps[2].enabled===false && !document.getElementById('refresh').disabled", 'disable app');
  assert.equal(await evaluate("document.querySelector('.app:last-child button').disabled"), true, 'disabled app cannot launch');
  await appButton('Enable'); await waitFor("fixture.apps[2].enabled===true && !document.getElementById('refresh').disabled", 'enable app');
  await click('.app:first-child button[title="Move down"]');
  await waitFor("fixture.apps[0].slot===1 && !document.getElementById('refresh').disabled", 'reorder apps');
  assert.deepEqual(await evaluate("fixture.calls.find(c=>c.path==='/api/v1/apps/order').body.map(a=>a.slot)"), [0, 1, 2], 'compacts sparse slots into consecutive ranks');
  await click('#refresh'); await waitFor("!document.getElementById('refresh').disabled", 'refresh reordered apps');
  assert.deepEqual(await evaluate("currentApps.map(app=>[app.id,app.slot])"), [['weather', 0], ['clock', 1], ['org.crystal.declarative-dashboard', 2]], 'refresh preserves dense order');
  await click('.app:nth-child(2) button[title="Move up"]');
  await waitFor("fixture.apps[0].slot===0 && !document.getElementById('refresh').disabled", 'move app back up');
  await click('#refresh'); await waitFor("!document.getElementById('refresh').disabled", 'refresh restored order');
  assert.deepEqual(await evaluate("currentApps.map(app=>[app.id,app.slot])"), [['clock', 0], ['weather', 1], ['org.crystal.declarative-dashboard', 2]], 'moving back keeps consecutive ranks');
  await appButton('Uninstall'); assert.equal(await evaluate("document.getElementById('confirm-dialog').open"), true);
  assert.equal(await evaluate("document.getElementById('confirm-accept').classList.contains('danger')"), true, 'uninstall keeps a destructive action color');
  await click('#confirm-cancel'); await waitFor('!confirmationPending', 'cancel completes');
  assert.equal(await evaluate("fixture.calls.some(c=>c.method==='DELETE')"), false, 'cancel cannot delete');
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
  await waitFor("fixture.calls.some(c=>c.path==='/api/v1/logs?since=2')", 'cursor reconnect');
  assert.equal(await evaluate("document.getElementById('log-output').textContent"), 'First message\nWeather ready\n', 'cursor reconnect does not replay displayed logs');
  await click('#refresh'); await waitFor("!document.getElementById('refresh').disabled", 'refresh while following logs');
  assert.equal(await evaluate("document.getElementById('logs').getAttribute('aria-pressed')"), 'true', 'refresh keeps log following active');
  await click('#logs');
  const callsBeforeResize = await evaluate('fixture.calls.length');
  for (const width of [390, 320, 768, 800, 801, 1440]) {
    await viewport(width);
    await assertSessionControls();
    assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true, 'dashboard fits ' + width + 'px viewport');
    if (width <= 800) {
      await click('#tab-apps');
      await assertCompactAppRows(width);
      assert.equal(await evaluate("document.querySelectorAll('.panel-sections > section:not([hidden])').length"), 1, 'narrow layout shows exactly one panel');
      await click('#tab-install');
      assert.equal(await evaluate("!document.getElementById('panel-install').hidden && document.getElementById('tab-install').getAttribute('aria-selected')==='true'"), true, 'Install tab selects its panel');
      assert.equal(await evaluate("document.getElementById('package').files[0].name"), 'dashboard.capp', 'selected package survives responsive navigation');
      if (width === 390) await screenshot('install-mobile');
      await evaluate("document.getElementById('tab-install').focus();document.getElementById('tab-install').dispatchEvent(new KeyboardEvent('keydown',{key:'End',bubbles:true}));");
      assert.equal(await evaluate("!document.getElementById('panel-device').hidden && document.activeElement.id==='tab-device'"), true, 'keyboard End selects and focuses Device');
      if (width === 390) await screenshot('device-mobile');
      await evaluate("document.getElementById('tab-device').dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowLeft',bubbles:true}));");
      assert.equal(await evaluate("!document.getElementById('panel-install').hidden && document.activeElement.id==='tab-install'"), true, 'keyboard arrow selects and focuses Install');
      await evaluate("document.getElementById('tab-install').dispatchEvent(new KeyboardEvent('keydown',{key:'Home',bubbles:true}));");
      assert.equal(await evaluate("!document.getElementById('panel-apps').hidden && document.activeElement.id==='tab-apps'"), true, 'keyboard Home selects and focuses Apps');
      assert.equal(await evaluate("[...document.querySelectorAll('.tabs button')].every(b=>b.getBoundingClientRect().height>=44)"), true, 'mobile tabs have touch-sized targets');
      if (width === 390) await screenshot('dashboard-mobile');
    } else {
      await assertDesktopPanels();
      await assertCompactAppRows(width);
      assert.equal(await evaluate("document.activeElement.id"), 'refresh', 'resizing to desktop moves focus out of hidden tabs');
    }
  }
  assert.equal(await evaluate('fixture.calls.length'), callsBeforeResize, 'resizing and switching tabs do not re-pair or refetch data');
  await evaluate("document.getElementById('package').focus()");
  await viewport(390);
  assert.equal(await evaluate("!document.getElementById('panel-install').hidden && document.activeElement.id==='package'"), true, 'resizing preserves the focused installation section');
  await click('#tab-device');
  await viewport(1440);
  await viewport(390);
  assert.equal(await evaluate("!document.getElementById('panel-device').hidden"), true, 'mobile selection survives a desktop round trip');
  await click('#tab-apps');
  const callsBeforeSignOut = await evaluate('fixture.calls.length');
  await click('#sign-out');
  assert.equal(await evaluate("!document.getElementById('pairing').classList.contains('hidden') && document.getElementById('device').classList.contains('hidden') && document.querySelectorAll('.app').length===0 && document.querySelectorAll('#system .metric').length===0 && document.getElementById('package').files.length===0"), true, 'Sign out clears the session and displayed device data');
  assert.deepEqual(await evaluate('fixture.calls.slice(' + callsBeforeSignOut + ').map(c=>[c.path,c.method])'), [['/api/v1/session', 'DELETE']], 'Sign out revokes the device session');
  assert.match(await evaluate("document.getElementById('pair-status').textContent"), /signed out/);
  await pair();
  await click('#tab-device');
  await click('#reboot');
  assert.equal(await evaluate("document.getElementById('confirm-dialog').open && document.activeElement.id==='confirm-cancel'"), true, 'reboot requires confirmation with Cancel focused');
  assert.equal(await evaluate("fixture.calls.filter(c=>c.path==='/api/v1/system/reboot').length"), 0, 'opening confirmation does not reboot');
  assert.match(await evaluate("document.getElementById('confirm-copy').textContent"), /enable Local Control Panel again/);
  assert.equal(await evaluate("document.getElementById('confirm-cancel').classList.contains('secondary') && document.getElementById('confirm-accept').classList.contains('danger')"), true, 'reboot restores the gray Cancel and red confirmation buttons');
  for (const width of [1440, 390, 320]) {
    await viewport(width);
    assert.equal(await evaluate("{const d=document.getElementById('confirm-dialog'),r=d.getBoundingClientRect(),s=getComputedStyle(d),c=document.getElementById('confirm-cancel').getBoundingClientRect(),a=document.getElementById('confirm-accept').getBoundingClientRect();r.left>=16 && r.right<=innerWidth-16 && r.top>=0 && r.bottom<=innerHeight && s.borderRadius==='8px' && s.borderTopWidth==='1px' && d.scrollWidth<=d.clientWidth && Math.abs(c.left-r.left-25)<1 && Math.abs(r.right-a.right-25)<1 && c.right<a.left && Math.abs(c.top-a.top)<1 && c.height>=44 && a.height>=44}"), true, 'bordered dialog fits with Cancel left and confirmation right at ' + width + 'px');
    if (width === 1440) await screenshot('reboot-confirm-desktop');
    if (width === 390) await screenshot('reboot-confirm-mobile');
  }
  await viewport(390);
  await click('#confirm-cancel');
  await waitFor("!document.getElementById('confirm-dialog').open && !confirmationPending", 'reboot confirmation cancelled');
  assert.equal(await evaluate("fixture.calls.filter(c=>c.path==='/api/v1/system/reboot').length"), 0, 'Cancel sends no reboot request');
  await evaluate('fixture.failReboot=500');
  await click('#reboot'); await click('#confirm-accept');
  await waitFor("document.getElementById('device-status').classList.contains('error') && !document.getElementById('reboot').disabled", 'reboot failure');
  assert.equal(await evaluate("document.getElementById('pairing').classList.contains('hidden')"), true, 'reboot errors preserve the paired session');
  await evaluate('fixture.failReboot=0');
  const callsBeforeReboot = await evaluate('fixture.calls.length');
  await click('#reboot'); await click('#confirm-accept');
  await waitFor("!document.getElementById('pairing').classList.contains('hidden')", 'reboot accepted');
  assert.deepEqual(await evaluate('fixture.calls.slice(' + callsBeforeReboot + ').map(c=>[c.path,c.method,c.headers.Authorization])'), [['/api/v1/system/reboot', 'POST', 'Bearer test-session-token']], 'confirmed reboot posts once with authentication and does not refresh during restart');
  assert.match(await evaluate("document.getElementById('pair-status').textContent"), /Reboot requested/);
  await pair();
  await click('#tab-device');
  await click('#reboot');
  await evaluate('fixture.failSystem=401'); await click('#refresh');
  await waitFor("!document.getElementById('pairing').classList.contains('hidden')", 'session expiry closes pending confirmation');
  assert.equal(await evaluate("document.getElementById('confirm-dialog').open"), false, 'session expiry cancels pending reboot confirmation');
  assert.equal(await evaluate("fixture.calls.filter(c=>c.path==='/api/v1/system/reboot').length"), 2, 'session expiry does not authorize a pending reboot');
  await evaluate('fixture.failSystem=0'); await pair();
  await evaluate('fixture.failSystem=401'); await click('#refresh');
  await waitFor("!document.getElementById('pairing').classList.contains('hidden')", 'session expiry');
  assert.equal(await evaluate("document.getElementById('device').classList.contains('hidden')"), true);
  assert.equal(await evaluate("document.querySelectorAll('.app').length"), 0, 'expired session clears app data');
  for (const width of [390, 320, 768]) {
    await viewport(width);
    assert.equal(await evaluate('document.documentElement.scrollWidth <= innerWidth'), true, 'pairing fits ' + width + 'px viewport');
    assert.equal(await evaluate("{const logo=document.querySelector('img.brand-mark');const box=logo.getBoundingClientRect();box.width >= 32 && box.width <= 44 && Math.abs(box.width / box.height - 144 / 121) < 0.04}"), true, 'logo stays proportionate on ' + width + 'px viewport');
    if (width === 390) await screenshot('pairing-mobile');
  }
  assert.deepEqual(exceptions, [], 'no uncaught browser exceptions');
  console.log('PASS: pairing, rejected PIN, session expiry, sign out, confirmed reboot, apps, enable/disable, ordering, confirmation, upload, errors, logs, desktop sections, mobile tabs, keyboard navigation, resize state, and responsive layouts.');
  if (preview) console.log('Previews: ' + preview);
} finally {
  if (socket) socket.close();
  chrome.kill();
  await new Promise(resolve => { if (chrome.exitCode !== null) resolve(); else { chrome.once('exit', resolve); setTimeout(resolve, 2000); } });
  await rm(profile, { recursive: true, force: true, maxRetries: 3 });
}
