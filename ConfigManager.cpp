#include "ConfigManager.h"
#include <LittleFS.h>
#include <MD5Builder.h>

static constexpr uint32_t CONFIG_MAGIC = 0x4F524143UL; // ORAC
static constexpr uint32_t STATE_MAGIC  = 0x4F524153UL; // ORAS
static constexpr uint16_t FORMAT_VERSION = 1;

static const char* DEFAULT_AP_SSID = "RoomAutomation-ESP";
static const char* DEFAULT_AP_PASS = "RA8266@Setup2026";
static const char* DEFAULT_DEVICE   = "RoomAutomation";

const char* ConfigManager::defaultOtaPassword() {
  return "R8!vQ2#nL7@xP4$k";
}

bool ConfigManager::begin() {
  return LittleFS.begin();
}

void ConfigManager::defaults() {
  memset(&_config, 0, sizeof(_config));
  _config.magic = CONFIG_MAGIC;
  _config.version = FORMAT_VERSION;
  strlcpy(_config.apSsid, DEFAULT_AP_SSID, sizeof(_config.apSsid));
  strlcpy(_config.apPassword, DEFAULT_AP_PASS, sizeof(_config.apPassword));
  _config.staSsid[0] = '\0';
  _config.staPassword[0] = '\0';
  strlcpy(_config.deviceName, DEFAULT_DEVICE, sizeof(_config.deviceName));
  MD5Builder md5;
  md5.begin();
  md5.add(defaultOtaPassword());
  md5.calculate();
  strlcpy(_config.otaMd5, md5.toString().c_str(), sizeof(_config.otaMd5));
  _config.apChannel = 6;
  _config.relayActiveLow = 1;
  updateConfigCrc();
}

void ConfigManager::defaultState() {
  memset(&_state, 0, sizeof(_state));
  _state.magic = STATE_MAGIC;
  _state.version = FORMAT_VERSION;
  _state.sequence = 1;
  updateStateCrc();
}

bool ConfigManager::load() {
  if (loadBinary("/config.bin", "/config.bak", &_config, sizeof(_config))) return true;
  defaults();
  return save();
}

bool ConfigManager::loadState() {
  if (loadBinary("/state.bin", "/state.bak", &_state, sizeof(_state))) return true;
  defaultState();
  return saveState();
}

bool ConfigManager::save() {
  updateConfigCrc();
  return saveBinary("/config.bin", "/config.bak", &_config, sizeof(_config));
}

bool ConfigManager::saveState() {
  _state.sequence++;
  updateStateCrc();
  return saveBinary("/state.bin", "/state.bak", &_state, sizeof(_state));
}

bool ConfigManager::factoryReset() {
  defaults();
  defaultState();
  return save() && saveState();
}

bool ConfigManager::loadBinary(const char* path, const char* backup, void* data, size_t size) {
  auto validFile = [&](const char* p) -> bool {
    File f = LittleFS.open(p, "r");
    if (!f || (size_t)f.size() != size) {
      if (f) f.close();
      return false;
    }
    size_t n = f.read((uint8_t*)data, size);
    f.close();
    if (n != size) return false;
    if (size == sizeof(DeviceConfig)) return validateConfig(*(DeviceConfig*)data);
    return validateState(*(RelayState*)data);
  };

  if (validFile(path)) return true;
  if (validFile(backup)) {
    // Recreate the primary from the validated backup.
    saveBinary(path, backup, data, size);
    return true;
  }
  return false;
}

bool ConfigManager::saveBinary(const char* path, const char* backup, const void* data, size_t size) {
  const char* tmp = "/write.tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  const size_t n = f.write((const uint8_t*)data, size);
  f.flush();
  f.close();
  if (n != size) {
    LittleFS.remove(tmp);
    return false;
  }

  // The new data is fully written before the short rename sequence begins.
  if (LittleFS.exists(backup)) LittleFS.remove(backup);
  if (LittleFS.exists(path) && !LittleFS.rename(path, backup)) {
    LittleFS.remove(tmp);
    return false;
  }
  if (!LittleFS.rename(tmp, path)) {
    if (LittleFS.exists(backup) && !LittleFS.exists(path)) LittleFS.rename(backup, path);
    return false;
  }
  return true;
}

bool ConfigManager::validateConfig(const DeviceConfig& c) const {
  if (c.magic != CONFIG_MAGIC || c.version != FORMAT_VERSION) return false;
  if (c.apSsid[0] == '\0' || strlen(c.apSsid) > 32) return false;
  if (strlen(c.apPassword) < 8 || strlen(c.apPassword) > 64) return false;
  if (strlen(c.staSsid) > 32 || strlen(c.staPassword) > 64) return false;
  if (strlen(c.deviceName) == 0 || strlen(c.deviceName) > 32) return false;
  if (strlen(c.otaMd5) != 32) return false;
  if (c.apChannel < 1 || c.apChannel > 13) return false;
  if (c.relayActiveLow > 1) return false;

  DeviceConfig tmp = c;
  const uint32_t stored = tmp.crc32;
  tmp.crc32 = 0;
  return stored == crc32((const uint8_t*)&tmp, sizeof(tmp));
}

bool ConfigManager::validateState(const RelayState& s) const {
  if (s.magic != STATE_MAGIC || s.version != FORMAT_VERSION) return false;
  for (uint8_t i = 0; i < 4; i++) if (s.relay[i] > 1) return false;
  RelayState tmp = s;
  const uint32_t stored = tmp.crc32;
  tmp.crc32 = 0;
  return stored == crc32((const uint8_t*)&tmp, sizeof(tmp));
}

void ConfigManager::updateConfigCrc() {
  _config.crc32 = 0;
  _config.crc32 = crc32((const uint8_t*)&_config, sizeof(_config));
}

void ConfigManager::updateStateCrc() {
  _state.crc32 = 0;
  _state.crc32 = crc32((const uint8_t*)&_state, sizeof(_state));
}

uint32_t ConfigManager::crc32(const uint8_t* data, size_t len) const {
  uint32_t crc = 0xFFFFFFFFUL;
  while (len--) {
    crc ^= *data++;
    for (uint8_t i = 0; i < 8; i++) {
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
    }
  }
  return ~crc;
}
