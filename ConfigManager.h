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

class ConfigManager {
public:
  bool begin();
  bool load();
  bool save();
  bool loadState();
  bool saveState();
  bool factoryReset();

  DeviceConfig& config() { return _config; }
  RelayState& state() { return _state; }

  static const char* defaultOtaPassword();

private:
  DeviceConfig _config{};
  RelayState _state{};

  bool loadBinary(const char* path, const char* backup, void* data, size_t size);
  bool saveBinary(const char* path, const char* backup, const void* data, size_t size);
  void defaults();
  void defaultState();
  bool validateConfig(const DeviceConfig& c) const;
  bool validateState(const RelayState& s) const;
  uint32_t crc32(const uint8_t* data, size_t len) const;
  void updateConfigCrc();
  void updateStateCrc();
};
