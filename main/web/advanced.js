'use strict';
(() => {
  const button = document.getElementById('capture');
  const message = document.getElementById('capture-status');
  const retry = document.getElementById('retry-status');
  let timer;
  async function update(method = 'GET') {
    clearTimeout(timer);
    button.disabled = true;
    retry.hidden = true;
    if (method === 'POST') message.textContent = 'Starting screenshot...';
    try {
      const response = await fetch('/api/screenshot', {method, cache: 'no-store', signal: AbortSignal.timeout(10000)});
      if (!response.ok && response.status !== 409 && response.status !== 503) throw new Error('Request failed');
      const state = await response.json();
      if (typeof state.busy !== 'boolean' || typeof state.message !== 'string') throw new Error('Invalid status');
      message.textContent = state.message;
      button.disabled = state.busy;
      if (state.busy) timer = setTimeout(() => update(), 1000);
    } catch {
      message.textContent = 'Cannot read screenshot status. Check the connection, then check status again before removing the card.';
      retry.hidden = false;
    }
  }
  button.addEventListener('click', () => update('POST'));
  retry.addEventListener('click', () => update());
  update();
})();
