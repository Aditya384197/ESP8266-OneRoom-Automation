#include "RelayManager.h"

const uint8_t RelayManager::PINS[4] = {5, 4, 14, 12}; // D1,D2,D5,D6

void RelayManager::begin(ConfigManager& cfg) {
  _cfg = &cfg;
  const bool activeLow = _cfg->config().relayActiveLow != 0;
  const uint8_t offLevel = activeLow ? HIGH : LOW;

  // Preload the OFF level before switching each GPIO to OUTPUT.
  // This prevents an Active-High module from receiving an ON pulse.
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(PINS[i], offLevel);
    pinMode(PINS[i], OUTPUT);
  }
  restore();
}

void RelayManager::writePin(uint8_t index, bool on) {
  if (index >= 4 || !_cfg) return;
  const bool activeLow = _cfg->config().relayActiveLow != 0;
  const uint8_t level = (on ^ activeLow) ? HIGH : LOW;
  digitalWrite(PINS[index], level);
}

void RelayManager::restore() {
  for (uint8_t i = 0; i < 4; i++) {
    writePin(i, _cfg->state().relay[i] != 0);
    yield();
  }
}

bool RelayManager::set(uint8_t index, bool on, bool persist) {
  if (!_cfg || index >= 4) return false;
  const uint8_t old = _cfg->state().relay[index];
  if (old == (on ? 1 : 0)) {
    writePin(index, on);
    return true;
  }

  _cfg->state().relay[index] = on ? 1 : 0;
  if (persist && !_cfg->saveState()) {
    _cfg->state().relay[index] = old;
    writePin(index, old != 0);
    return false;
  }
  writePin(index, on);
  return true;
}

bool RelayManager::toggle(uint8_t index) {
  if (index >= 4) return false;
  return set(index, !get(index));
}

bool RelayManager::get(uint8_t index) const {
  return (_cfg && index < 4) ? (_cfg->state().relay[index] != 0) : false;
}

void RelayManager::allOff() {
  for (uint8_t i = 0; i < 4; i++) set(i, false, false);
  if (_cfg) _cfg->saveState();
}
