#include "AutomationManager.h"
#include <time.h>
#include <Wire.h>

static constexpr unsigned long SWITCH_DEBOUNCE_MS = 45UL;
static constexpr unsigned long SWITCH_SAMPLE_INTERVAL_MS = 10UL;
static constexpr unsigned long TIMER_SAVE_INTERVAL_MS = 300000UL; // 5-minute checkpoint to limit flash wear
static constexpr uint32_t MAX_TIMER_SECONDS = 2592000UL;
static constexpr uint8_t SWITCH_EXPANDER_ADDRESS = 0x20; // PCF8574 A0-A2 = GND
static constexpr uint8_t SWITCH_I2C_SDA_GPIO = 0;         // NodeMCU D3
static constexpr uint8_t SWITCH_I2C_SCL_GPIO = 2;         // NodeMCU D4

void AutomationManager::begin(ConfigManager& cfg, RelayManager& relays) {
  _cfg = &cfg;
  _relays = &relays;
  beginSwitches();
  const unsigned long now = millis();
  for (uint8_t i = 0; i < 4; ++i) {
    const RelayTimerConfig& timer = _cfg->automation().timers[i];
    // Keep a monotonic fallback even for epoch-based timers. If the device
    // reboots while the router/Internet is offline, the saved remaining time
    // can continue instead of freezing until NTP returns.
    if (timer.enabled && timer.remainingSec > 0) {
      _timerEndMs[i] = now + (unsigned long)timer.remainingSec * 1000UL;
    }
  }
  _lastTimerSaveAt = now;
}

const AutomationConfig& AutomationManager::config() const {
  return _cfg->automation();
}

void AutomationManager::beginSwitches() {
  if (!_cfg || !_relays) return;
  const AutomationConfig& a = _cfg->automation();
  bool needsExpander = false;
  for (uint8_t i = 0; i < 4; ++i) {
    if (a.switchPin[i] >= 32 && a.switchPin[i] <= 35) needsExpander = true;
  }
  _needsExpander = needsExpander;
  if (!needsExpander) {
    _expanderAvailable = false;
    return;
  }

  _lastExpanderRetryAt = millis();
  // The PCF8574 is powered at 3.3 V. I2C pull-ups must hold SDA/SCL HIGH
  // during reset because GPIO0/D3 and GPIO2/D4 are ESP8266 boot-strap pins.
  // External switches connect only to PCF8574 P0-P3 and GND.
  Wire.begin(SWITCH_I2C_SDA_GPIO, SWITCH_I2C_SCL_GPIO);
  Wire.setClock(100000);
  Wire.beginTransmission(SWITCH_EXPANDER_ADDRESS);
  Wire.write(0xFF); // Release all quasi-bidirectional port pins for input.
  if (Wire.endTransmission() != 0) {
    _expanderAvailable = false;
    Serial.println(F("PCF8574 not detected at 0x20; physical switch inputs are waiting."));
    return;
  }

  _expanderAvailable = true;
  if (!readExpander(_expanderBits)) {
    _expanderAvailable = false;
    Serial.println(F("PCF8574 read failed; physical switch inputs are waiting."));
    return;
  }

  const unsigned long now = millis();
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t pin = a.switchPin[i];
    if (pin < 32 || pin > 35) continue;
    _switches[i].rawPressed = readSwitchPressed(pin, true, _expanderBits);
    _switches[i].stablePressed = _switches[i].rawPressed;
    _switches[i].changedAt = now;
    // Align the relay to the physical contact once at startup in both modes.
    // Mode 1 keeps enforcing the input level; mode 0 permits phone overrides
    // until the next physical contact transition.
    _relays->set(i, _switches[i].stablePressed, false);
  }
  Serial.println(F("PCF8574 physical switch inputs ready (P0-P3)."));
}

bool AutomationManager::readExpander(uint8_t& bits) {
  if (!_expanderAvailable) return false;
  const uint8_t received = Wire.requestFrom(SWITCH_EXPANDER_ADDRESS, (uint8_t)1);
  if (received != 1 || !Wire.available()) {
    _expanderAvailable = false;
    return false;
  }
  bits = (uint8_t)Wire.read();
  return true;
}

bool AutomationManager::readSwitchPressed(uint8_t virtualPin, bool activeLow,
                                          uint8_t expanderBits) const {
  if (virtualPin < 32 || virtualPin > 35) return false;
  const bool high = (expanderBits & (1U << (virtualPin - 32U))) != 0;
  return activeLow ? !high : high;
}

void AutomationManager::scanSwitches() {
  if (!_cfg || !_relays || !_needsExpander) return;
  const unsigned long now = millis();
  if ((unsigned long)(now - _lastSwitchScanAt) < SWITCH_SAMPLE_INTERVAL_MS) return;
  _lastSwitchScanAt = now;
  if (!_expanderAvailable) {
    if ((unsigned long)(now - _lastExpanderRetryAt) >= 3000UL) beginSwitches();
    return;
  }
  if (!readExpander(_expanderBits)) return;

  const AutomationConfig& a = _cfg->automation();
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t pin = a.switchPin[i];
    if (pin < 32 || pin > 35) continue;
    const bool pressed = readSwitchPressed(pin, true, _expanderBits);
    if (pressed != _switches[i].rawPressed) {
      _switches[i].rawPressed = pressed;
      _switches[i].changedAt = now;
    }
    if ((unsigned long)(now - _switches[i].changedAt) >= SWITCH_DEBOUNCE_MS &&
        _switches[i].stablePressed != _switches[i].rawPressed) {
      _switches[i].stablePressed = _switches[i].rawPressed;
      // Transition-follow mode: closed edge commands ON, open edge commands
      // OFF. A phone command remains in effect until the physical switch moves.
      if (a.switchMode[i] == 0) _relays->set(i, _switches[i].stablePressed, true);
    }
    if (a.switchMode[i] == 1 && _relays->get(i) != _switches[i].stablePressed) {
      _relays->set(i, _switches[i].stablePressed, true);
    }
  }
}

void AutomationManager::loop(bool timeSynced) {
  if (!_cfg || !_relays) return;
  scanSwitches();
  processTimers(timeSynced);
  processSchedules(timeSynced);
}

bool AutomationManager::setTimer(uint8_t relay, bool turnOn,
                                 uint32_t seconds, bool timeSynced) {
  if (!_cfg || relay >= 4 || seconds > MAX_TIMER_SECONDS) return false;

  RelayTimerConfig& timer = _cfg->automation().timers[relay];
  const RelayTimerConfig previous = timer;
  const unsigned long previousEnd = _timerEndMs[relay];
  timer.enabled = seconds > 0 ? 1 : 0;
  timer.action = turnOn ? 1 : 0;
  timer.remainingSec = seconds;
  timer.dueEpoch = (seconds > 0 && timeSynced)
    ? (uint32_t)time(nullptr) + seconds
    : 0;
  // Always keep a monotonic fallback. dueEpoch remains authoritative while
  // time is synchronized; remainingSec is checkpointed for offline reboot.
  _timerEndMs[relay] = seconds > 0
    ? millis() + (unsigned long)seconds * 1000UL
    : 0;
  if (_cfg->saveAutomation()) return true;
  timer = previous;
  _timerEndMs[relay] = previousEnd;
  return false;
}

bool AutomationManager::cancelTimer(uint8_t relay) {
  if (!_cfg || relay >= 4) return false;
  RelayTimerConfig& timer = _cfg->automation().timers[relay];
  const RelayTimerConfig previous = timer;
  const unsigned long previousEnd = _timerEndMs[relay];
  timer.enabled = 0;
  timer.dueEpoch = 0;
  timer.remainingSec = 0;
  _timerEndMs[relay] = 0;
  if (_cfg->saveAutomation()) return true;
  timer = previous;
  _timerEndMs[relay] = previousEnd;
  return false;
}

uint32_t AutomationManager::timerRemaining(uint8_t relay, bool timeSynced) const {
  if (!_cfg || relay >= 4) return 0;
  const RelayTimerConfig& timer = _cfg->automation().timers[relay];
  if (!timer.enabled) return 0;

  if (timer.dueEpoch > 0 && timeSynced) {
    const time_t now = time(nullptr);
    return now >= (time_t)timer.dueEpoch ? 0 : (uint32_t)(timer.dueEpoch - now);
  }

  if (_timerEndMs[relay] == 0) return timer.remainingSec;
  const uint32_t remainingMs = (uint32_t)(_timerEndMs[relay] - millis());
  // A wrapped value larger than the maximum valid countdown means expired.
  if (remainingMs == 0 || remainingMs > (MAX_TIMER_SECONDS * 1000UL)) return 0;
  return (uint32_t)(((uint64_t)remainingMs + 999ULL) / 1000ULL);
}

void AutomationManager::processTimers(bool timeSynced) {
  const unsigned long nowMs = millis();
  const time_t nowEpoch = timeSynced ? time(nullptr) : 0;
  const bool checkpointDue =
    (unsigned long)(nowMs - _lastTimerSaveAt) >= TIMER_SAVE_INTERVAL_MS;
  bool changed = false;
  RelayTimerConfig previousTimers[4];
  unsigned long previousEnds[4];
  for (uint8_t i = 0; i < 4; ++i) {
    previousTimers[i] = _cfg->automation().timers[i];
    previousEnds[i] = _timerEndMs[i];
  }

  for (uint8_t i = 0; i < 4; ++i) {
    RelayTimerConfig& timer = _cfg->automation().timers[i];
    if (!timer.enabled) continue;

    if (_timerEndMs[i] == 0 && timer.remainingSec > 0) {
      _timerEndMs[i] = nowMs + (unsigned long)timer.remainingSec * 1000UL;
    }

    uint32_t remaining = 0;
    bool expired = false;

    if (timer.dueEpoch > 0 && timeSynced) {
      expired = nowEpoch >= (time_t)timer.dueEpoch;
      remaining = expired ? 0 : (uint32_t)(timer.dueEpoch - nowEpoch);
      // Refresh the fallback so a later network/time loss can continue.
      _timerEndMs[i] = expired ? 0 : nowMs + (unsigned long)remaining * 1000UL;
    } else {
      remaining = timerRemaining(i, false);
      expired = remaining == 0;
    }

    if (expired) {
      // Do not discard the timer if persistence of the relay state fails.
      if (!_relays->set(i, timer.action != 0, true)) continue;
      timer.enabled = 0;
      timer.dueEpoch = 0;
      timer.remainingSec = 0;
      _timerEndMs[i] = 0;
      changed = true;
    } else if (checkpointDue && remaining != timer.remainingSec) {
      timer.remainingSec = remaining;
      changed = true;
    }
  }

  if (changed && !_cfg->saveAutomation()) {
    for (uint8_t i = 0; i < 4; ++i) {
      _cfg->automation().timers[i] = previousTimers[i];
      _timerEndMs[i] = previousEnds[i];
    }
  }
  if (checkpointDue) _lastTimerSaveAt = nowMs;
}

int32_t AutomationManager::localDayKey(const struct tm& value) {
  return ((int32_t)value.tm_year * 400L) + (int32_t)value.tm_yday;
}

void AutomationManager::processSchedules(bool timeSynced) {
  if (!timeSynced) return;
  const time_t now = time(nullptr);
  struct tm localTime;
  if (!localtime_r(&now, &localTime)) return;

  const int32_t dayKey = localDayKey(localTime);
  const uint8_t dayBit = (uint8_t)(1U << localTime.tm_wday);
  const uint16_t minute = (uint16_t)(localTime.tm_hour * 60 + localTime.tm_min);

  for (uint8_t i = 0; i < 8; ++i) {
    ScheduleRule& rule = _cfg->automation().schedules[i];
    if (!rule.enabled || rule.minuteOfDay != minute ||
        (rule.daysMask & dayBit) == 0 || rule.lastRunDay == dayKey) {
      continue;
    }

    // Apply the idempotent relay state first. If saving the schedule marker
    // fails, the same action is retried; it cannot toggle the relay twice.
    if (!_relays->set(rule.relay, rule.action != 0, true)) return;
    const int32_t previous = rule.lastRunDay;
    rule.lastRunDay = dayKey;
    if (!_cfg->saveAutomation()) {
      rule.lastRunDay = previous;
      return;
    }
  }
}

bool AutomationManager::setSchedule(uint8_t slot, uint8_t relay, bool turnOn,
                                    uint8_t daysMask, uint16_t minuteOfDay,
                                    bool enabled) {
  if (!_cfg || slot >= 8 || relay >= 4 || daysMask == 0 || daysMask > 0x7F || minuteOfDay > 1439) return false;
  ScheduleRule& rule = _cfg->automation().schedules[slot];
  const ScheduleRule previous = rule;
  rule.enabled = enabled ? 1 : 0;
  rule.relay = relay;
  rule.action = turnOn ? 1 : 0;
  rule.daysMask = daysMask;
  rule.minuteOfDay = minuteOfDay;
  rule.lastRunDay = -1;
  if (_cfg->saveAutomation()) return true;
  rule = previous;
  return false;
}

bool AutomationManager::removeSchedule(uint8_t slot) {
  if (!_cfg || slot >= 8) return false;
  ScheduleRule& rule = _cfg->automation().schedules[slot];
  const ScheduleRule previous = rule;
  rule.enabled = 0;
  rule.lastRunDay = -1;
  if (_cfg->saveAutomation()) return true;
  rule = previous;
  return false;
}
