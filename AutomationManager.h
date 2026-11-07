#pragma once

#include <Arduino.h>
#include <time.h>
#include "ConfigManager.h"
#include "RelayManager.h"

class AutomationManager {
public:
  void begin(ConfigManager& cfg, RelayManager& relays);
  void loop(bool timeSynced);

  bool setTimer(uint8_t relay, bool turnOn, uint32_t seconds, bool timeSynced);
  bool cancelTimer(uint8_t relay);
  uint32_t timerRemaining(uint8_t relay, bool timeSynced) const;

  bool setSchedule(uint8_t slot, uint8_t relay, bool turnOn,
                   uint8_t daysMask, uint16_t minuteOfDay, bool enabled);
  bool removeSchedule(uint8_t slot);

  const AutomationConfig& config() const;

private:
  struct SwitchRuntime {
    bool rawPressed = false;
    bool stablePressed = false;
    unsigned long changedAt = 0;
  };

  ConfigManager* _cfg = nullptr;
  RelayManager* _relays = nullptr;
  SwitchRuntime _switches[4];
  unsigned long _timerEndMs[4] = {0, 0, 0, 0};
  unsigned long _lastTimerSaveAt = 0;
  unsigned long _lastExpanderRetryAt = 0;
  unsigned long _lastSwitchScanAt = 0;
  bool _expanderAvailable = false;
  bool _needsExpander = false;
  uint8_t _expanderBits = 0xFF;

  void beginSwitches();
  bool readExpander(uint8_t& bits);
  void scanSwitches();
  void processTimers(bool timeSynced);
  void processSchedules(bool timeSynced);
  static int32_t localDayKey(const struct tm& value);
  bool readSwitchPressed(uint8_t virtualPin, bool activeLow, uint8_t expanderBits) const;
};
