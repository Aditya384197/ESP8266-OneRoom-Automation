#include "RelayManager.h"

const uint8_t RelayManager::PINS[4] = {5, 4, 14, 12}; // D1,D2,D5,D6
static constexpr uint8_t PHYSICAL_SWITCH_GPIO = 16; // NodeMCU D0

void RelayManager::begin(ConfigManager& cfg) {
  _cfg = &cfg;
  // Preload each relay's own OFF level before switching GPIO to OUTPUT.
  // Mixed Active-High and Active-Low modules are supported independently.
  for (uint8_t i = 0; i < 4; i++) {
    const bool activeLow = _cfg->automation().relayActiveLow[i] != 0;
    digitalWrite(PINS[i], activeLow ? HIGH : LOW);
    pinMode(PINS[i], OUTPUT);
  }
  restore();
}

void RelayManager::writePin(uint8_t index, bool on) {
  if (index >= 4 || !_cfg) return;
  const bool activeLow = _cfg->automation().relayActiveLow[index] != 0;
  const uint8_t level = (on ^ activeLow) ? HIGH : LOW;
  digitalWrite(PINS[index], level);
}

void RelayManager::restore() {
  for (uint8_t i = 0; i < 4; i++) {
    // Do not briefly restore a stale saved state on a relay owned by the
    // maintained wall switch. AutomationManager samples D0 immediately after
    // this call and applies the physical contact's state instead.
    if (_cfg->automation().switchPin[i] == PHYSICAL_SWITCH_GPIO) {
      yield();
      continue;
    }
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
