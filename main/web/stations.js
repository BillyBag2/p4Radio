'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const state = {catalog: null, catalogVersion: 0, servers: [], serverPromise: null, rows: [],
    offset: 0, more: false, loading: false, version: 0, searchAbort: null, timer: null,
    saving: new Set(), preview: 0, previewUrl: '', hls: null, hlsPromise: null};
  const PAGE = 200;
  const uuidValid = value => /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(value || '');
  function safeUrl(value) {
    try { const url = new URL(value); return ['http:', 'https:'].includes(url.protocol) && !url.username && !url.password ? url.href : ''; }
    catch { return ''; }
  }
  const streamUrl = station => safeUrl(station.url_resolved || station.url || '');
  const compatible = station => { const codec = String(station.codec || '').toUpperCase(); return codec.includes('AAC') || (Number(station.hls) === 0 && codec === 'MP3'); };
  const bytes = value => new TextEncoder().encode(value).length;
  function notice(message, error = false) { $('notice').textContent = message; $('notice').classList.toggle('error', error); }
  function element(tag, text, cls) { const node = document.createElement(tag); if (text) node.textContent = text; if (cls) node.className = cls; return node; }
  function button(text, handler, cls = '') { const node = element('button', text, cls); node.type = 'button'; node.addEventListener('click', handler); return node; }
  const empty = (parent, text) => parent.append(element('p', text, 'empty'));
  function listenButton(station) {
    const url = streamUrl(station), active = url && url === state.previewUrl;
    const control = button(active ? 'Stop preview' : 'Listen here', () => active ? stopPreview() : preview(station), 'secondary');
    control.disabled = !url;
    return control;
  }

  async function localApi(path, command) {
    const response = await fetch(path, command ? {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(command), cache: 'no-store'} : {cache: 'no-store'});
    let data;
    try { data = await response.json(); } catch { throw new Error('The radio did not return a valid response. Please retry.'); }
    if (!response.ok || data.ok === false) throw new Error(data.message || 'The radio could not complete the request.');
    return data;
  }

  async function loadSaved(initial = false) {
    const version = ++state.catalogVersion;
    const catalog = await localApi('/api/stations');
    if (version !== state.catalogVersion) return;
    state.catalog = catalog;
    if (initial) $('bitrate').value = '0'; // Search filtering is independent of the playback preference.
    renderSaved(); renderResults();
  }

  function renderSaved() {
    const parent = $('saved-list'); parent.replaceChildren();
    if (!state.catalog) return empty(parent, 'Saved stations are not available yet.');
    const all = state.catalog.stations || [];
    const enabled = all.filter(station => station.enabled).length;
    $('saved-summary').textContent = `${all.length} of ${state.catalog.capacity} slots used · ${enabled} enabled` +
      (enabled ? '' : ' · Enable a station to resume the radio.');
    const text = $('saved-search').value.trim().toLocaleLowerCase(), filter = $('saved-state').value;
    const rows = all.filter(station => station.name.toLocaleLowerCase().includes(text) &&
      (filter === 'all' || (filter === 'enabled' ? station.enabled : !station.enabled)));
    for (const station of rows) {
      const card = element('article', '', 'station'), info = element('div'), actions = element('div', '', 'actions');
      info.append(element('h3', station.name));
      const badges = element('p', '', 'meta');
      badges.append(element('span', station.enabled ? 'Enabled' : 'Muted', 'badge ' + (station.enabled ? 'enabled' : 'muted')));
      if (station.builtin) badges.append(element('span', 'Preset', 'badge'));
      if (station.slot === state.catalog.selected) badges.append(element('span', 'Selected', 'badge'));
      info.append(badges);
      const listen = listenButton({...station, hls: station.hls === false ? 0 : 1});
      const toggle = button(station.enabled ? 'Mute' : 'Enable', () => change({action: station.enabled ? 'mute' : 'unmute', slot: station.slot, id: station.id}, station.name));
      toggle.disabled = state.saving.has(String(station.id));
      actions.append(listen, toggle);
      if (!station.builtin) {
        const remove = button('Remove', () => change({action: 'remove', slot: station.slot, id: station.id}, station.name), 'secondary danger');
        remove.disabled = toggle.disabled; actions.append(remove);
      }
      card.append(info, actions); parent.append(card);
    }
    if (!rows.length) empty(parent, 'No saved stations match this filter.');
  }

  async function change(command, name) {
    const key = String(command.id || command.uuid);
    if (state.saving.has(key)) return;
    state.saving.add(key); renderSaved(); renderResults();
    try {
      const reply = await localApi('/api/stations', command);
      await loadSaved(); notice(`${name}: ${reply.message}.`);
    } catch (error) {
      notice(error.message, true);
      try { await loadSaved(); } catch { /* Keep the last known list visible. */ }
    } finally { state.saving.delete(key); renderSaved(); renderResults(); }
  }

  function savedMatch(station) {
    return state.catalog?.stations?.find(saved => saved.uuid === station.stationuuid || saved.url === streamUrl(station));
  }

  function renderResults() {
    const parent = $('results'); parent.replaceChildren();
    const rows = state.rows.filter(station => !$('compatible').checked || compatible(station));
    for (const station of rows) {
      const card = element('article', '', 'station'), info = element('div'), actions = element('div', '', 'actions');
      info.append(element('h3', station.name || 'Unnamed station'));
      const meta = [station.country, station.language, station.codec,
        Number(station.bitrate) > 0 ? `${station.bitrate} kbit/s` : 'Bitrate unknown', Number(station.hls) === 1 ? 'HLS' : 'Direct stream'];
      info.append(element('p', meta.filter(Boolean).join(' · '), 'meta'));
      if (station.tags) info.append(element('p', String(station.tags).slice(0, 160), 'meta'));
      const url = streamUrl(station), saved = savedMatch(station);
      const listen = listenButton(station);
      const add = button(saved ? (saved.enabled ? 'Added' : 'Saved - muted') : 'Add to radio', () => {
        change({action: 'add', name: station.name, url, uuid: station.stationuuid,
          bitrate: Number(station.bitrate) || 0, hls: Number(station.hls) === 1, codec: String(station.codec || '').toUpperCase()}, station.name);
      });
      let reason = '';
      if (!compatible(station)) reason = 'This radio supports AAC HLS, direct AAC and MP3 streams.';
      else if (!url || bytes(url) > 1024 || !uuidValid(station.stationuuid) || !station.name || bytes(station.name) > 96)
        reason = 'This station has missing details or a link/name too long to save.';
      else if (state.catalog && state.catalog.stations.length >= state.catalog.capacity) reason = 'Remove an added station to make room.';
      add.disabled = !!saved || !!reason || !state.catalog || state.saving.has(String(station.stationuuid));
      if (reason) { add.title = reason; info.append(element('p', reason, 'meta')); }
      const cap = state.catalog?.max_bitrate || 0;
      if (cap && (!Number(station.bitrate) || Number(station.bitrate) > cap))
        info.append(element('p', `The radio prefers ${cap} kbit/s or less. If no lower rate is available, it will use the lowest available rate.`, 'meta'));
      actions.append(listen, add); card.append(info, actions); parent.append(card);
    }
    if (!state.loading && !rows.length) empty(parent, state.rows.length ?
      'No radio-compatible stations on this page. Try Next results or turn off the compatibility filter.' : 'No stations found. Try another name or adjust the filters.');
    $('previous').disabled = state.loading || state.offset === 0;
    $('next').disabled = state.loading || !state.more;
    $('page-number').textContent = `Page ${Math.floor(state.offset / PAGE) + 1}`;
  }

  async function loadServers(force = false) {
    if (!force && state.servers.length) return state.servers;
    if (state.serverPromise) return state.serverPromise;
    state.serverPromise = (async () => {
      for (let attempt = 0; attempt < 22; ++attempt) {
        const response = await localApi('/api/directory' + (force && attempt === 0 ? '?refresh=1' : ''));
        if (!response.connected) throw new Error('Connect the radio to Wi-Fi to discover directory servers. You can still manage saved stations.');
        if (!response.busy) {
          const servers = (response.servers || []).filter(value => { try { const url = new URL(value); return url.protocol === 'https:' && !url.username && !url.password; } catch { return false; } });
          if (!servers.length) throw new Error('Directory server discovery failed. Check the radio connection and use Retry directory.');
          state.servers = servers.slice();
          for (let i = state.servers.length - 1; i > 0; --i) {
            const j = Math.floor(Math.random() * (i + 1));
            [state.servers[i], state.servers[j]] = [state.servers[j], state.servers[i]];
          }
          return state.servers;
        }
        await new Promise(resolve => setTimeout(resolve, 800));
      }
      throw new Error('Directory discovery is taking too long. Please retry.');
    })();
    try { return await state.serverPromise; } finally { state.serverPromise = null; }
  }

  async function directory(path, signal) {
    const servers = await loadServers();
    for (const server of servers) {
      if (signal?.aborted) throw new DOMException('Cancelled', 'AbortError');
      const request = new AbortController(), cancel = () => request.abort();
      signal?.addEventListener('abort', cancel, {once: true});
      const timer = setTimeout(cancel, 8000);
      try {
        const response = await fetch(server + path, {signal: request.signal});
        if (!response.ok) throw new Error('Directory request failed');
        const data = await response.json();
        if (!Array.isArray(data)) throw new Error('Unexpected directory response');
        state.servers = [server, ...state.servers.filter(value => value !== server)];
        return data;
      } catch (error) { if (signal?.aborted) throw new DOMException('Cancelled', 'AbortError'); }
      finally { clearTimeout(timer); signal?.removeEventListener('abort', cancel); }
    }
    throw new Error('The directory could not be reached from this browser. Check internet access or use Retry directory.');
  }

  async function search(offset = 0) {
    state.searchAbort?.abort();
    const controller = new AbortController(); state.searchAbort = controller;
    const version = ++state.version;
    state.offset = offset; state.loading = true; renderResults(); $('results').setAttribute('aria-busy', 'true');
    $('search-status').textContent = 'Searching...';
    const params = new URLSearchParams({name: $('search').value.trim(), nameExact: 'false', limit: String(PAGE + 1), offset: String(offset),
      order: $('sort').value, reverse: String($('sort').value !== 'name'), hidebroken: String($('working').checked)});
    for (const [id, key] of [['country', 'countrycode'], ['language', 'language'], ['tag', 'tag'], ['codec', 'codec']])
      if ($(id).value.trim()) params.set(key, $(id).value.trim());
    if (Number($('bitrate').value)) params.set('bitrateMax', $('bitrate').value);
    if ($('https').checked) params.set('is_https', 'true');
    try {
      const rows = await directory('/json/stations/search?' + params, controller.signal);
      if (version !== state.version) return;
      state.more = rows.length > PAGE; state.rows = rows.slice(0, PAGE);
      const count = state.rows.filter(station => !$('compatible').checked || compatible(station)).length;
      $('search-status').textContent = `${count} matches on this page` + (state.more ? ' · More results available' : '');
    } catch (error) {
      if (version !== state.version || error.name === 'AbortError') return;
      state.rows = []; state.more = false; $('search-status').textContent = error.message;
    } finally {
      if (version === state.version) { state.loading = false; $('results').setAttribute('aria-busy', 'false'); renderResults(); }
    }
  }

  function scheduleSearch() {
    clearTimeout(state.timer); state.searchAbort?.abort(); ++state.version;
    state.loading = true; renderResults();
    state.timer = setTimeout(() => search(0), 350);
  }

  function stopPreview() {
    ++state.preview;
    if (state.hls) { state.hls.destroy(); state.hls = null; }
    $('preview').pause(); $('preview').removeAttribute('src'); $('preview').load();
    $('stop-preview').disabled = true; $('stream-link').hidden = true;
    state.previewUrl = '';
    $('preview-name').textContent = 'Preview stopped.';
    $('preview-status').textContent = 'Preview plays in this browser, without changing the radio.';
    renderSaved(); renderResults();
  }

  async function hlsLibrary() {
    if (window.Hls) return window.Hls;
    if (!state.hlsPromise) state.hlsPromise = new Promise((resolve, reject) => {
      const script = document.createElement('script');
      script.src = 'https://cdn.jsdelivr.net/npm/hls.js@1.7.3/dist/hls.min.js';
      script.onload = () => resolve(window.Hls);
      script.onerror = () => { state.hlsPromise = null; script.remove(); reject(new Error('Could not load the HLS preview player. Try Open stream instead.')); };
      document.head.append(script);
    });
    return state.hlsPromise;
  }

  async function preview(station) {
    stopPreview();
    const version = state.preview, audio = $('preview'), url = streamUrl(station);
    if (!url) return;
    $('preview-name').textContent = station.name || 'Station preview';
    state.previewUrl = url;
    $('preview-status').textContent = 'Connecting to preview...';
    $('stop-preview').disabled = false; $('stream-link').href = url; $('stream-link').hidden = false;
    renderSaved(); renderResults();
    $('preview-heading').scrollIntoView({behavior: 'smooth', block: 'start'});
    const uuid = station.stationuuid || station.uuid;
    if (uuidValid(uuid) && state.servers.length)
      fetch(state.servers[0] + '/json/url/' + uuid).catch(() => {});
    try {
      if (Number(station.hls) === 1 && !audio.canPlayType('application/vnd.apple.mpegurl')) {
        const Hls = await hlsLibrary();
        if (version !== state.preview) return;
        if (!Hls?.isSupported()) throw new Error('This browser cannot preview HLS. Try Open stream instead.');
        const hls = new Hls({maxBufferLength: 10, maxMaxBufferLength: 20}); state.hls = hls;
        hls.on(Hls.Events.ERROR, (_, data) => {
          if (data.fatal && version === state.preview) {
            $('preview-status').textContent = 'Preview failed. The stream may be unavailable, blocked by CORS, or unsupported by this browser.';
            hls.destroy(); if (state.hls === hls) state.hls = null;
          }
        });
        hls.loadSource(url); hls.attachMedia(audio);
      } else audio.src = url;
      await audio.play();
      if (version === state.preview) $('preview-status').textContent = 'Playing in this browser.';
    } catch (error) {
      if (version === state.preview) $('preview-status').textContent = error.name === 'NotAllowedError' ? 'Press Play in the audio controls to begin the preview.' : error.message;
    }
  }

  $('filters').addEventListener('input', scheduleSearch);
  $('filters').addEventListener('submit', event => { event.preventDefault(); clearTimeout(state.timer); search(0); });
  $('saved-search').addEventListener('input', renderSaved); $('saved-state').addEventListener('change', renderSaved);
  $('reload-saved').addEventListener('click', () => loadSaved().catch(error => notice(error.message, true)));
  $('previous').addEventListener('click', () => search(Math.max(0, state.offset - PAGE)));
  $('next').addEventListener('click', () => search(state.offset + PAGE));
  $('retry-directory').addEventListener('click', async () => {
    try { state.servers = []; await loadServers(true); search(0); } catch (error) { $('search-status').textContent = error.message; }
  });
  $('stop-preview').addEventListener('click', stopPreview);
  $('preview').addEventListener('error', () => { if (!$('stop-preview').disabled) $('preview-status').textContent = 'This stream could not be previewed. Check its availability or try Open stream.'; });
  window.addEventListener('pagehide', stopPreview);
  (async () => {
    try { await loadSaved(true); } catch (error) { notice(error.message, true); }
    search(0);
    try {
      const countries = await directory('/json/countries?order=name&hidebroken=true');
      const selected = $('country').value, options = [element('option', 'All countries')]; options[0].value = '';
      for (const country of countries) {
        if (!/^[A-Z]{2}$/.test(country.iso_3166_1 || '')) continue;
        const option = element('option', country.name); option.value = country.iso_3166_1; options.push(option);
      }
      if (options.length > 1) { $('country').replaceChildren(...options); $('country').value = selected; }
    } catch { /* Common country choices remain usable if metadata is unavailable. */ }
  })();
})();
