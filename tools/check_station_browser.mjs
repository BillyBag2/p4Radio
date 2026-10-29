// Runs the actual page script against a small DOM/network/audio harness.
// This checks client workflows, not real browser rendering, CORS or audio codecs.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

class Element {
  constructor(tag = 'div') {
    this.tagName = tag.toLowerCase(); this.children = []; this.listeners = {}; this.attributes = {};
    this.value = ''; this.checked = false; this.disabled = false; this.className = ''; this._text = '';
    this.classList = {toggle: (name, enabled) => {
      const classes = new Set(this.className.split(' ').filter(Boolean));
      enabled ? classes.add(name) : classes.delete(name); this.className = [...classes].join(' ');
    }};
  }
  set textContent(value) { this._text = String(value); this.children = []; }
  get textContent() { return this._text + this.children.map(child => child.textContent).join(' '); }
  set innerHTML(_) { throw new Error('Directory content must not be inserted as HTML'); }
  append(...nodes) { this.children.push(...nodes); }
  replaceChildren(...nodes) { this.children = nodes; this._text = ''; }
  addEventListener(type, handler) { (this.listeners[type] ||= []).push(handler); }
  async dispatch(type) { for (const handler of this.listeners[type] || []) await handler({target: this, preventDefault() {}}); }
  setAttribute(key, value) { this.attributes[key] = value; }
  removeAttribute(key) { delete this.attributes[key]; if (key === 'src') this.src = ''; }
  remove() {}
  scrollIntoView() {}
}
const html = fs.readFileSync('main/web/stations.html', 'utf8');
const nodes = new Map();
for (const match of html.matchAll(/<(\w+)\b[^>]*\bid="([^"]+)"[^>]*>/g)) {
  const [, tag, id] = match;
  assert(!nodes.has(id), `Duplicate HTML id ${id}`);
  const node = new Element(tag); node.id = id; node.disabled = match[0].includes(' disabled');
  node.checked = match[0].includes(' checked');
  if (tag === 'select') {
    const options = html.slice(match.index + match[0].length).split('</select>')[0];
    node.value = options.match(/<option[^>]*\bvalue="([^"]*)"/)?.[1] || '';
  }
  nodes.set(id, node);
}
const get = id => { assert(nodes.has(id), `Missing HTML element ${id}`); return nodes.get(id); };
const audio = get('preview');
audio.pause = () => { audio.paused = true; };
audio.load = () => {};
audio.play = async () => { audio.paused = false; };
audio.canPlayType = () => '';
let hlsLoads = 0, hlsDestroyed = 0, scriptsLoaded = 0;
class Hls {
  static isSupported() { return true; }
  static Events = {ERROR: 'error'};
  on() {}
  loadSource(url) { this.url = url; ++hlsLoads; }
  attachMedia(media) { media.src = 'blob:preview'; }
  destroy() { ++hlsDestroyed; }
}
const window = new Element('window');
const document = {getElementById: get, createElement: tag => new Element(tag), head: {append(script) {
  assert.equal(script.src, 'https://cdn.jsdelivr.net/npm/hls.js@1.7.3/dist/hls.min.js');
  ++scriptsLoaded; window.Hls = Hls; queueMicrotask(() => script.onload());
}}};
const uuid = suffix => `12345678-abcd-1234-abcd-${String(suffix).padStart(12, '0')}`;
let catalog = {capacity: 24, selected: 0, max_bitrate: 96, stations: [
  {slot: 0, id: 1, name: 'BBC preset', url: 'https://stream.example/bbc.m3u8', uuid: '', bitrate: 96, builtin: true, enabled: true},
  {slot: 5, id: 6, name: 'Old custom', url: 'https://stream.example/old.m3u8', uuid: uuid(6), bitrate: 96, builtin: false, enabled: true}
]};
const station = (name, id = 7) => ({name, stationuuid: uuid(id), url_resolved: `https://stream.example/${id}.m3u8`,
  country: 'United Kingdom', language: 'english', tags: 'jazz', codec: 'AAC', bitrate: 96, hls: 1});
const testName = 'Test <img src=x onerror=bad()>';
const rows = [station(testName), {...station('MP3 preview', 8), codec: 'MP3', hls: 0}];
const posts = [], searches = [];
let oldResolve, oldSignal, failNextSave = false;
const response = data => ({ok: true, json: async () => structuredClone(data)});
async function fetchMock(path, options = {}) {
  if (path === '/api/stations' && options.method === 'POST') {
    assert.equal(options.headers['Content-Type'], 'application/json');
    const command = JSON.parse(options.body); posts.push(command);
    if (failNextSave) { failNextSave = false; return {ok: false, json: async () => ({ok: false, message: 'Station storage is full'})}; }
    if (command.action === 'add') catalog.stations.push({slot: 6, id: 7, name: command.name, url: command.url, uuid: command.uuid, bitrate: command.bitrate, enabled: true, builtin: false});
    else if (command.action === 'remove') catalog.stations = catalog.stations.filter(value => value.id !== command.id);
    else catalog.stations.find(value => value.id === command.id).enabled = command.action === 'unmute';
    return response({ok: true, message: 'Stations saved'});
  }
  if (path === '/api/stations') return response(catalog);
  if (path.startsWith('/api/directory')) return response({connected: true, busy: false, servers: ['https://directory.example']});
  const url = new URL(path);
  if (url.pathname === '/json/countries') return response([{name: 'United Kingdom', iso_3166_1: 'GB'}]);
  if (url.pathname.startsWith('/json/url/')) return response({ok: true});
  assert.equal(url.pathname, '/json/stations/search'); searches.push(url);
  if (url.searchParams.get('name') === 'old') {
    oldSignal = options.signal;
    return new Promise(resolve => { oldResolve = () => resolve(response([station('Stale result')])); });
  }
  return response(url.searchParams.get('name') === 'New' ? [station('New result')] : rows);
}
const context = vm.createContext({document, window, fetch: fetchMock, URL, URLSearchParams, TextEncoder,
  AbortController, DOMException, setTimeout, clearTimeout, console, Promise, Math});
vm.runInContext(fs.readFileSync('main/web/stations.js', 'utf8'), context);
const flush = () => new Promise(resolve => setImmediate(resolve));
async function until(predicate) { for (let i = 0; i < 100; ++i) { if (predicate()) return; await flush(); } assert(predicate(), 'Workflow did not settle'); }
const walk = node => [node, ...node.children.flatMap(walk)];
const findButton = (node, name) => walk(node).find(value => value.tagName === 'button' && value.textContent === name);
const savedCard = name => get('saved-list').children.find(value => value.children[0]?.children[0]?.textContent === name);

await until(() => get('search-status').textContent.includes('matches'));
assert.equal(get('bitrate').value, '96');
assert.equal(get('results').children.length, 1, 'Incompatible streams hidden by default');
assert(get('results').textContent.includes(testName), 'Names are rendered literally');
await findButton(get('results'), 'Listen here').dispatch('click');
assert.equal(hlsLoads, 1); assert.equal(scriptsLoaded, 1); assert.equal(posts.length, 0, 'Preview must not change radio settings');
await get('stop-preview').dispatch('click'); assert(audio.paused); assert.equal(hlsDestroyed, 1);

await findButton(get('results'), 'Add to radio').dispatch('click');
await until(() => !!findButton(get('results'), 'Added'));
assert.equal(posts[0].uuid, uuid(7)); assert.equal(posts[0].hls, true); assert.equal(posts[0].codec, 'AAC');
assert(savedCard(testName));
await findButton(savedCard('BBC preset'), 'Mute').dispatch('click');
await until(() => !!findButton(savedCard('BBC preset'), 'Enable'));
assert(!findButton(savedCard('BBC preset'), 'Remove'), 'Presets are muted, never removed');
await findButton(savedCard('BBC preset'), 'Enable').dispatch('click');
await until(() => !!findButton(savedCard('BBC preset'), 'Mute'));
await findButton(savedCard(testName), 'Remove').dispatch('click');
await until(() => !savedCard(testName));
assert(findButton(get('results'), 'Add to radio'), 'Removed station can be added again');

get('compatible').checked = false;
await get('filters').dispatch('submit');
await until(() => get('results').children.length === 2);
const mp3 = get('results').children.find(value => value.textContent.includes('MP3 preview'));
assert(findButton(mp3, 'Add to radio').disabled); assert(!findButton(mp3, 'Listen here').disabled);
const postCount = posts.length;
await findButton(mp3, 'Listen here').dispatch('click'); assert.equal(posts.length, postCount);
assert(audio.src.includes('/8.m3u8')); // Native audio path for a non-HLS entry.

const catalogBeforeFailure = JSON.stringify(catalog);
failNextSave = true;
await findButton(get('results'), 'Add to radio').dispatch('click');
await until(() => get('notice').textContent.includes('storage is full') && !findButton(get('results'), 'Add to radio').disabled);
assert.equal(JSON.stringify(catalog), catalogBeforeFailure, 'A failed save must leave the displayed catalog unchanged');
assert(!savedCard(testName));
for (const name of ['BBC preset', 'Old custom']) {
  await findButton(savedCard(name), 'Mute').dispatch('click');
  await until(() => !!findButton(savedCard(name), 'Enable') && !findButton(savedCard(name), 'Enable').disabled);
}
assert(get('saved-summary').textContent.includes('0 enabled'));
assert(savedCard('BBC preset') && savedCard('Old custom'), 'All muted entries stay in settings');
await findButton(savedCard('BBC preset'), 'Enable').dispatch('click');
await until(() => !!findButton(savedCard('BBC preset'), 'Mute') && !findButton(savedCard('BBC preset'), 'Mute').disabled);

get('country').value = 'GB'; get('language').value = 'English'; get('tag').value = 'jazz';
get('codec').value = 'AAC'; get('https').checked = true; get('search').value = 'New';
await get('filters').dispatch('submit');
await until(() => get('results').textContent.includes('New result'));
const query = searches.at(-1).searchParams;
for (const [key, value] of [['countrycode', 'GB'], ['language', 'English'], ['tag', 'jazz'], ['codec', 'AAC'], ['bitrateMax', '96'], ['is_https', 'true']]) assert.equal(query.get(key), value);

get('search').value = 'old'; await get('filters').dispatch('submit'); await until(() => !!oldResolve);
get('search').value = 'New'; await get('filters').dispatch('submit');
await until(() => get('results').textContent.includes('New result'));
assert(oldSignal.aborted); oldResolve(); await flush(); await flush();
assert(!get('results').textContent.includes('Stale result'), 'Older responses must not replace live search');

const before = searches.length;
for (const value of ['b', 'bu', 'burst']) { get('search').value = value; await get('filters').dispatch('input'); }
await new Promise(resolve => setTimeout(resolve, 450));
assert.equal(searches.length, before + 1, 'Typing is debounced'); assert.equal(searches.at(-1).searchParams.get('name'), 'burst');
await window.dispatch('pagehide'); assert(audio.paused);
console.log('Station browser workflow checks passed (mock DOM, network and audio).');
