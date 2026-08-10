#pragma once
#include <Arduino.h>
#include "ConfigManager.h"

class RelayManager {
public:
  void begin(ConfigManager& cfg);
  bool set(uint8_t index, bool on, bool persist = true);
  bool toggle(uint8_t index);
  bool get(uint8_t index) const;
  void restore();
  void allOff();

private:
  ConfigManager* _cfg = nullptr;
  static const uint8_t PINS[4];
  void writePin(uint8_t index, bool on);
};
