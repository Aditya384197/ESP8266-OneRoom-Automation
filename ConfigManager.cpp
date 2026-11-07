#include "ConfigManager.h"
#include <LittleFS.h>

static constexpr uint32_t CONFIG_MAGIC = 0x4F524143UL; // ORAC
static constexpr uint32_t STATE_MAGIC  = 0x4F524153UL; // ORAS
static constexpr uint32_t AUTOMATION_MAGIC = 0x4F524141UL; // ORAA
static constexpr uint16_t FORMAT_VERSION = 1;
static constexpr uint16_t AUTOMATION_VERSION = 1;

static const char* DEFAULT_AP_SSID = "RoomAutomation-ESP";
static const char* DEFAULT_AP_PASS = "RA8266@Setup2026";
static const char* DEFAULT_DEVICE   = "RoomAutomation";
// MD5 of the publicly documented v1.3.0 default OTA password. Clear this
// legacy credential on upgrade so the owner must provision a private one.
static const char* LEGACY_PUBLIC_OTA_MD5 = "59cc37a188dd68a20d35d664eb644fc7";

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
  _config.otaMd5[0] = '\0'; // OTA stays disabled until the owner creates a password.
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

void ConfigManager::defaultAutomation() {
  memset(&_automation, 0, sizeof(_automation));
  _automation.magic = AUTOMATION_MAGIC;
  _automation.version = AUTOMATION_VERSION;

  // Physical inputs are disabled until the owner confirms their wiring.
  // GPIO0/GPIO2/GPIO15 are boot-strap pins and can prevent startup when
  // externally driven to the wrong level during reset.
  const uint8_t pins[4] = {255, 255, 255, 255};
  const uint8_t activeLow[4] = {1, 1, 1, 1};
  const char* names[4] = {"Ceiling Fan", "Charging Socket", "LED 1", "LED 2"};
  for (uint8_t i = 0; i < 4; ++i) {
    _automation.switchPin[i] = pins[i];
    _automation.switchActiveLow[i] = activeLow[i];
    _automation.switchMode[i] = 0;
    strlcpy(_automation.relayName[i], names[i], sizeof(_automation.relayName[i]));
  }
  for (uint8_t i = 0; i < 8; ++i) {
    _automation.schedules[i].relay = 0;
    _automation.schedules[i].lastRunDay = -1;
  }
  updateAutomationCrc();
}

bool ConfigManager::load() {
  if (loadBinary("/config.bin", "/config.bak", &_config, sizeof(_config))) {
    // v1.3.0 published its factory OTA password in README. Do not carry that
    // known credential into the upgraded device; require owner provisioning.
    if (strncmp(_config.otaMd5, LEGACY_PUBLIC_OTA_MD5, 32) == 0) {
      _config.otaMd5[0] = '\0';
      return save();
    }
    return true;
  }
  defaults();
  return save();
}

bool ConfigManager::loadState() {
  if (loadBinary("/state.bin", "/state.bak", &_state, sizeof(_state))) return true;
  defaultState();
  return saveState();
}

bool ConfigManager::loadAutomation() {
  if (loadBinary("/automation.bin", "/automation.bak", &_automation, sizeof(_automation))) return true;
  defaultAutomation();
  return saveAutomation();
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

bool ConfigManager::saveAutomation() {
  updateAutomationCrc();
  return saveBinary("/automation.bin", "/automation.bak", &_automation, sizeof(_automation));
}

bool ConfigManager::factoryReset() {
  defaults();
  defaultState();
  defaultAutomation();
  return save() && saveState() && saveAutomation();
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
    if (size == sizeof(RelayState)) return validateState(*(RelayState*)data);
    if (size == sizeof(AutomationConfig)) return validateAutomation(*(AutomationConfig*)data);
    return false;
  };

  if (validFile(path)) return true;
  if (validFile(backup)) {
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
  const size_t apSsidLen = strnlen(c.apSsid, sizeof(c.apSsid));
  const size_t apPassLen = strnlen(c.apPassword, sizeof(c.apPassword));
  const size_t staSsidLen = strnlen(c.staSsid, sizeof(c.staSsid));
  const size_t staPassLen = strnlen(c.staPassword, sizeof(c.staPassword));
  const size_t deviceNameLen = strnlen(c.deviceName, sizeof(c.deviceName));
  const size_t otaHashLen = strnlen(c.otaMd5, sizeof(c.otaMd5));
  if (apSsidLen == 0 || apSsidLen >= sizeof(c.apSsid)) return false;
  // WPA2 SoftAP passphrases are 8..63 characters.
  if (apPassLen < 8 || apPassLen > 63) return false;
  if (staSsidLen >= sizeof(c.staSsid) || staPassLen >= sizeof(c.staPassword)) return false;
  if (deviceNameLen == 0 || deviceNameLen >= sizeof(c.deviceName)) return false;
  if (otaHashLen != 0 && otaHashLen != 32) return false;
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

bool ConfigManager::validateAutomation(const AutomationConfig& a) const {
  if (a.magic != AUTOMATION_MAGIC || a.version != AUTOMATION_VERSION) return false;
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t pin = a.switchPin[i];
    if (pin != 255 && pin != 16 && pin != 0 && pin != 2 && pin != 15) return false;
    if (a.switchActiveLow[i] > 1 || a.switchMode[i] > 1) return false;
    const size_t nameLen = strnlen(a.relayName[i], sizeof(a.relayName[i]));
    if (nameLen == 0 || nameLen >= sizeof(a.relayName[i])) return false;
    for (uint8_t j = i + 1; j < 4; ++j) {
      if (pin != 255 && pin == a.switchPin[j]) return false;
    }
    if (a.timers[i].enabled > 1 || a.timers[i].action > 1) return false;
    if (a.timers[i].remainingSec > 2592000UL) return false;
  }
  for (uint8_t i = 0; i < 8; ++i) {
    const ScheduleRule& r = a.schedules[i];
    if (r.enabled > 1 || r.relay > 3 || r.action > 1 || r.daysMask > 0x7F ||
        r.minuteOfDay > 1439 || (r.enabled && r.daysMask == 0)) return false;
  }
  AutomationConfig tmp = a;
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

void ConfigManager::updateAutomationCrc() {
  _automation.crc32 = 0;
  _automation.crc32 = crc32((const uint8_t*)&_automation, sizeof(_automation));
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
