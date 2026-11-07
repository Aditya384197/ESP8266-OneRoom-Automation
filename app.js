'use strict';
const $ = (id) => document.getElementById(id);
let busy = false;
let lastRelayNames = [];

function toast(message, ok = true) {
  const node = $('message');
  node.textContent = message;
  node.className = `toast ${ok ? 'ok' : 'error'}`;
  if (message) window.setTimeout(() => { if (node.textContent === message) node.textContent = ''; }, 4200);
}

async function api(path, options = {}) {
  const response = await fetch(path, { cache: 'no-store', ...options });
  let data = {};
  try { data = await response.json(); } catch (_) { /* handled below */ }
  if (!response.ok) throw new Error(data.error || `Request failed (${response.status})`);
  return data;
}

function formatDuration(seconds) {
  const value = Math.max(0, Number(seconds) || 0);
  const d = Math.floor(value / 86400);
  const h = Math.floor((value % 86400) / 3600);
  const m = Math.floor((value % 3600) / 60);
  const s = value % 60;
  return d ? `${d}d ${h}h ${m}m` : `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

function formatTime(minute) {
  const h = Math.floor(minute / 60);
  const m = minute % 60;
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}`;
}

function renderState(data) {
  const online = !!data.wifiConnected;
  $('connection').className = `status ${online ? 'online' : 'offline'}`;
  $('connection').replaceChildren(document.createElement('i'), document.createTextNode(online ? 'Home Wi-Fi online' : 'Local AP ready'));
  $('version').textContent = `Firmware ${data.version || '—'}`;
  $('ip').textContent = data.ip || '—';
  $('apIp').textContent = data.apIp || '—';
  $('rssi').textContent = online ? `${data.rssi} dBm` : 'Offline mode';
  $('heap').textContent = Number(data.heap) ? `${data.heap.toLocaleString()} B` : '—';
  $('uptime').textContent = formatDuration(data.uptimeSec);
  $('apClients').textContent = String(data.apClients ?? '—');
  lastRelayNames = Array.isArray(data.relayNames) ? data.relayNames : lastRelayNames;
  document.querySelectorAll('[data-timer]').forEach((card) => {
    const relay = Number(card.dataset.timer);
    const heading = card.querySelector('.timer-top strong');
    if (heading) heading.textContent = lastRelayNames[relay] || `Relay ${relay + 1}`;
  });
  const scheduleRelay = $('scheduleRelay');
  if (scheduleRelay) [...scheduleRelay.options].forEach((option) => {
    const relay = Number(option.value);
    option.textContent = lastRelayNames[relay] || `Relay ${relay + 1}`;
  });

  (data.relays || []).forEach((isOn, index) => {
    const card = document.querySelector(`[data-index="${index}"]`);
    if (!card) return;
    const button = card.querySelector('[data-toggle]');
    const state = card.querySelector('.relay-state');
    const name = card.querySelector('[data-relay-name]');
    const label = lastRelayNames[index] || `Relay ${index + 1}`;
    if (name) name.textContent = label;
    if (button) {
      button.classList.toggle('on', !!isOn);
      button.setAttribute('aria-pressed', String(!!isOn));
      button.disabled = false;
    }
    if (state) state.textContent = isOn ? 'ON' : 'OFF';
    card.classList.toggle('active', !!isOn);
  });
}

async function refreshState() {
  try { renderState(await api('/api/state')); }
  catch (error) {
    $('connection').className = 'status offline';
    $('connection').textContent = 'Device unavailable';
  }
}

async function refreshAutomation() {
  try {
    const data = await api('/api/automation');
    $('clockStatus').textContent = data.timeSynced ? `Clock synced · ${data.localTime || 'IST'}` : 'Clock not synced · schedules paused';
    $('clockStatus').classList.toggle('warn', !data.timeSynced);
    (data.timers || []).forEach((timer) => {
      const card = document.querySelector(`[data-timer="${timer.relay}"]`);
      if (!card) return;
      const status = card.querySelector('.timer-remaining');
      status.textContent = timer.enabled ? `${formatDuration(timer.remainingSec)} · ${timer.action ? 'then ON' : 'then OFF'}` : 'No timer';
      status.classList.toggle('running', !!timer.enabled);
    });
    renderSchedules(data.schedules || []);
  } catch (error) { $('clockStatus').textContent = 'Automation status unavailable'; }
}

function renderSchedules(items) {
  const box = $('scheduleItems');
  box.replaceChildren();
  if (!items.length) {
    const note = document.createElement('p'); note.className = 'empty-note'; note.textContent = 'No schedules configured.'; box.appendChild(note); return;
  }
  const dayLabels = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  items.forEach((item) => {
    const row = document.createElement('div'); row.className = 'schedule-item';
    const info = document.createElement('div');
    const title = document.createElement('strong');
    title.textContent = `${lastRelayNames[item.relay] || `Relay ${item.relay + 1}`} · ${item.action ? 'ON' : 'OFF'}`;
    const meta = document.createElement('small');
    const days = dayLabels.filter((_, day) => item.daysMask & (1 << day)).join(' ');
    meta.textContent = `${formatTime(item.minuteOfDay)} · ${days}`;
    info.append(title, meta);
    const remove = document.createElement('button'); remove.className = 'remove-rule'; remove.type = 'button'; remove.textContent = 'Remove';
    remove.addEventListener('click', async () => {
      try { await api('/api/schedule', { method: 'DELETE', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ slot: item.slot }) }); await refreshAutomation(); toast('Schedule removed.'); }
      catch (error) { toast(error.message, false); }
    });
    row.append(info, remove); box.appendChild(row);
  });
}

async function relayCommand(button) {
  if (busy) return;
  busy = true; button.disabled = true;
  const index = Number(button.dataset.toggle);
  const current = button.getAttribute('aria-pressed') === 'true';
  try {
    renderState(await api('/api/relay', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ index, on: !current }) }));
    toast(`${lastRelayNames[index] || `Relay ${index + 1}`} ${!current ? 'turned ON' : 'turned OFF'}.`);
  } catch (error) { toast(error.message, false); }
  finally { busy = false; button.disabled = false; }
}

document.querySelectorAll('[data-toggle]').forEach((button) => button.addEventListener('click', () => relayCommand(button)));

document.querySelectorAll('[data-timer]').forEach((card) => {
  const relay = Number(card.dataset.timer);
  card.querySelector('.timer-start').addEventListener('click', async () => {
    const seconds = Number(card.querySelector('.timer-seconds').value);
    const action = card.querySelector('.timer-action').value === 'true';
    if (!Number.isInteger(seconds) || seconds < 1 || seconds > 2592000) { toast('Enter a duration from 1 second to 30 days.', false); return; }
    try {
      await api('/api/timer', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ relay, seconds, action }) });
      await refreshAutomation(); toast(`Timer set for ${lastRelayNames[relay] || `Relay ${relay + 1}`}.`);
    } catch (error) { toast(error.message, false); }
  });
  card.querySelector('.timer-cancel').addEventListener('click', async () => {
    try { await api('/api/timer', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ relay, seconds: 0, action: false }) }); await refreshAutomation(); toast('Timer cancelled.'); }
    catch (error) { toast(error.message, false); }
  });
});

$('scheduleForm').addEventListener('submit', async (event) => {
  event.preventDefault();
  const [hour, minute] = $('scheduleTime').value.split(':').map(Number);
  const daysMask = [...document.querySelectorAll('.days input:checked')].reduce((mask, input) => mask | (1 << Number(input.value)), 0);
  if (!Number.isInteger(hour) || !Number.isInteger(minute) || daysMask === 0) { toast('Choose a valid time and at least one day.', false); return; }
  try {
    const existing = await api('/api/automation');
    const used = new Set((existing.schedules || []).map((rule) => rule.slot));
    let slot = -1; for (let i = 0; i < 8; i += 1) { if (!used.has(i)) { slot = i; break; } }
    if (slot < 0) throw new Error('All 8 schedule slots are in use. Remove one first.');
    await api('/api/schedule', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ slot, relay: Number($('scheduleRelay').value), action: $('scheduleAction').value === 'true', daysMask, minuteOfDay: hour * 60 + minute, enabled: true }) });
    await refreshAutomation(); toast('Weekly schedule saved.');
  } catch (error) { toast(error.message, false); }
});

$('rebootBtn').addEventListener('click', async () => {
  if (!window.confirm('Reboot the controller now?')) return;
  try { await api('/api/reboot', { method: 'POST' }); toast('Controller is rebooting…'); }
  catch (error) { toast(error.message, false); }
});

refreshState(); refreshAutomation();
window.setInterval(refreshState, 2500);
window.setInterval(refreshAutomation, 5000);
