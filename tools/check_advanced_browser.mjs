// Run the actual Advanced page script with mocked DOM and HTTP.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const script = readFileSync(new URL('../main/web/advanced.js', import.meta.url), 'utf8');
const flush = () => new Promise(resolve => setImmediate(resolve));

function harness(responses) {
  const elements = new Map(['capture', 'capture-status', 'retry-status'].map(id => [id, {
    disabled: id === 'capture', hidden: id === 'retry-status', textContent: '',
    addEventListener(type, callback) { this[type] = callback; },
    set innerHTML(_) { throw new Error('Status must be treated as text'); },
  }]));
  const calls = [], timers = new Map();
  let nextTimer = 0;
  vm.runInNewContext(script, {
    document: {getElementById: id => elements.get(id)}, AbortSignal,
    setTimeout(fn) { timers.set(++nextTimer, fn); return nextTimer; },
    clearTimeout(id) { timers.delete(id); },
    async fetch(url, options) {
      calls.push({url, ...options});
      const reply = responses.shift();
      if (reply instanceof Error) throw reply;
      assert.ok(reply, 'Unexpected HTTP request');
      return {ok: (reply.code ?? 200) < 400, status: reply.code ?? 200, json: async () => reply.state};
    },
  });
  return {elements, calls, timers, async poll() {
    const [id, fn] = timers.entries().next().value;
    timers.delete(id); fn(); await flush();
  }};
}

const ready = {state: {busy: false, success: false, message: 'Ready'}};
const busy = {state: {busy: true, success: false, message: 'Saving'}};
const saved = {state: {busy: false, success: true, message: 'Saved /sdcard/screens/12345678.bmp'}};

{
  const h = harness([ready, busy, busy, saved]);
  await flush();
  assert.equal(h.elements.get('capture').disabled, false);
  h.elements.get('capture').click();
  assert.equal(h.elements.get('capture').disabled, true);
  await flush();
  await h.poll(); await h.poll();
  assert.deepEqual(h.calls.map(c => c.method), ['GET', 'POST', 'GET', 'GET']);
  assert.ok(h.calls.every(c => c.url === '/api/screenshot' && c.cache === 'no-store'));
  assert.equal(h.elements.get('capture-status').textContent, saved.state.message);
  assert.equal(h.elements.get('capture').disabled, false);
  assert.equal(h.timers.size, 0);
}
{
  // Refresh during an existing job only polls; a second client gets 409 and waits.
  const h = harness([busy, ready, {...busy, code: 409}, saved]);
  await flush(); await h.poll();
  h.elements.get('capture').click(); await flush();
  assert.equal(h.elements.get('capture').disabled, true);
  await h.poll();
  assert.deepEqual(h.calls.map(c => c.method), ['GET', 'GET', 'POST', 'GET']);
}
for (const failure of [new Error('offline'), {code: 500}, {state: {message: 'missing busy'}}]) {
  const h = harness([ready, failure, saved]);
  await flush(); h.elements.get('capture').click(); await flush();
  assert.equal(h.elements.get('capture').disabled, true);
  assert.equal(h.elements.get('retry-status').hidden, false);
  assert.equal(h.timers.size, 0);
  h.elements.get('retry-status').click(); await flush();
  assert.deepEqual(h.calls.map(c => c.method), ['GET', 'POST', 'GET']);
  assert.equal(h.elements.get('capture').disabled, false);
}
{
  const message = '<img src=x onerror=alert(1)>';
  const h = harness([ready, {code: 503, state: {busy: false, success: false, message}}]);
  await flush(); h.elements.get('capture').click(); await flush();
  assert.equal(h.elements.get('capture-status').textContent, message);
  assert.equal(h.elements.get('capture').disabled, false);
}
console.log('Advanced screenshot workflow checks passed (mock DOM and HTTP).');
