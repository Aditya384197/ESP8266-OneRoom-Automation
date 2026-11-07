'use strict';
const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
let state = [false, false, false, false];
const pending = new Set();

function showMessage(text, error = false) {
  const node = $('#dashboardMessage');
  node.textContent = text || '';
  node.classList.toggle('error', !!error);
  if (text) window.setTimeout(() => { if (node.textContent === text) node.textContent = ''; }, 2600);
}

async function api(path, options = {}) {
  const response = await fetch(path, { cache: 'no-store', ...options });
  let data = {};
  try { data = await response.json(); } catch (_) {}
  if (!response.ok) throw new Error(data.error || `Request failed (${response.status})`);
  return data;
}

function setConnection(online) {
  const badge = $('#connection');
  badge.classList.toggle('online', online);
  badge.classList.toggle('offline', !online);
  $('span', badge).textContent = online ? 'Online' : 'Offline';
}

function renderState(data) {
  setConnection(!!data.wifiConnected);
  if (typeof data.logoText === 'string' && data.logoText.trim()) $('#logoText').textContent = data.logoText;
  const names = Array.isArray(data.relayNames) ? data.relayNames : [];
  if (Array.isArray(data.relays)) state = data.relays.slice(0, 4).map(Boolean);
  $$('.relay-card').forEach((card) => {
    const index = Number(card.dataset.index);
    const isOn = !!state[index];
    const name = $('[data-relay-name]', card);
    const label = names[index] || `Relay ${index + 1}`;
    if (name && name.textContent !== label) name.textContent = label;
    card.classList.toggle('active', isOn);
    card.setAttribute('aria-pressed', String(isOn));
    $('.relay-state', card).textContent = isOn ? 'ON' : 'OFF';
    card.classList.toggle('pending', pending.has(index));
    card.disabled = pending.has(index);
  });
}

async function refreshState() {
  try { renderState(await api('/api/state')); }
  catch (_) { setConnection(false); }
}

async function toggleRelay(card) {
  const index = Number(card.dataset.index);
  if (pending.has(index)) return;
  pending.add(index);
  card.classList.add('pending');
  card.disabled = true;
  try {
    const data = await api('/api/relay', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ index, on: !state[index] })
    });
    renderState(data);
  } catch (error) {
    showMessage(error.message, true);
    await refreshState();
  } finally {
    pending.delete(index);
    card.classList.remove('pending');
    card.disabled = false;
  }
}

$$('.relay-card').forEach((card) => card.addEventListener('click', () => toggleRelay(card)));

// Prevent long-press selection/copy on dashboard labels and cards. Form
// fields on the settings page are explicitly exempted in its own stylesheet.
const isEditable = (target) => !!target.closest('input,textarea,select,[contenteditable="true"]');
document.addEventListener('contextmenu', (event) => { if (!isEditable(event.target)) event.preventDefault(); });
document.addEventListener('selectstart', (event) => { if (!isEditable(event.target)) event.preventDefault(); });
document.addEventListener('copy', (event) => { if (!isEditable(event.target)) event.preventDefault(); });

refreshState();
window.setInterval(refreshState, 2500);
