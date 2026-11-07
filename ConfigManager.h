#pragma once
#include <Arduino.h>

struct DeviceConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  char apSsid[33];
  char apPassword[65];
  char staSsid[33];
  char staPassword[65];
  char deviceName[33];
  char otaMd5[33];
  uint8_t apChannel;
  uint8_t relayActiveLow;
  uint8_t reserved2[2];
  uint32_t crc32;
};

struct RelayState {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint8_t relay[4];
  uint32_t sequence;
  uint32_t crc32;
};

struct RelayTimerConfig {
  uint8_t enabled;
  uint8_t action;          // 0 = OFF, 1 = ON
  uint16_t reserved;
  uint32_t dueEpoch;       // 0 when wall-clock time was unavailable
  uint32_t remainingSec;   // reboot-resume fallback when dueEpoch is 0
};

struct ScheduleRule {
  uint8_t enabled;
  uint8_t relay;           // 0..3
  uint8_t action;          // 0 = OFF, 1 = ON
  uint8_t daysMask;        // bit 0 = Sunday ... bit 6 = Saturday
  uint16_t minuteOfDay;    // 0..1439
  uint16_t reserved;
  int32_t lastRunDay;      // local calendar day key; prevents duplicate runs
};

struct AutomationConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint8_t switchPin[4];
  uint8_t switchActiveLow[4];
  uint8_t switchMode[4];     // 0 = momentary toggle, 1 = maintained input
  char relayName[4][24];
  char logoText[256];
  uint8_t relayActiveLow[4]; // 1 = Active-Low, 0 = Active-High
  RelayTimerConfig timers[4];
  ScheduleRule schedules[8];
  uint32_t crc32;
};

class ConfigManager {
public:
  bool begin();
  bool load();
  bool save();
  bool loadState();
  bool saveState();
  bool loadAutomation();
  bool saveAutomation();
  bool factoryReset();

  DeviceConfig& config() { return _config; }
  RelayState& state() { return _state; }
  AutomationConfig& automation() { return _automation; }
  const AutomationConfig& automation() const { return _automation; }


private:
  DeviceConfig _config{};
  RelayState _state{};
  AutomationConfig _automation{};

  bool loadBinary(const char* path, const char* backup, void* data, size_t size);
  bool saveBinary(const char* path, const char* backup, const void* data, size_t size);
  void defaults();
  void defaultState();
  void defaultAutomation();
  bool validateConfig(const DeviceConfig& c) const;
  bool validateState(const RelayState& s) const;
  bool validateAutomation(const AutomationConfig& a) const;
  uint32_t crc32(const uint8_t* data, size_t len) const;
  void updateConfigCrc();
  void updateStateCrc();
  void updateAutomationCrc();
};
