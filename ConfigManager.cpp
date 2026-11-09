#include "ConfigManager.h"
#include <LittleFS.h>

static constexpr uint32_t CONFIG_MAGIC = 0x4F524143UL; // ORAC
static constexpr uint32_t STATE_MAGIC  = 0x4F524153UL; // ORAS
static constexpr uint32_t AUTOMATION_MAGIC = 0x4F524141UL; // ORAA
static constexpr uint16_t FORMAT_VERSION = 1;
static constexpr uint16_t AUTOMATION_VERSION = 5;
static constexpr uint32_t FIREBASE_MAGIC = 0x4F524146UL; // ORAF
static constexpr uint16_t FIREBASE_VERSION = 1;

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

  // One direct maintained switch may be assigned to GPIO13 / NodeMCU D7.
  // Disabled by default until the switch is wired.
  const uint8_t pins[4] = {255, 255, 255, 255};
  const uint8_t activeLow[4] = {1, 1, 1, 1};
  const char* names[4] = {"Ceiling Fan", "Charging Socket", "LED 1", "LED 2"};
  strlcpy(_automation.logoText, "Room Automation", sizeof(_automation.logoText));
  for (uint8_t i = 0; i < 4; ++i) {
    _automation.switchPin[i] = pins[i];
    _automation.switchActiveLow[i] = activeLow[i];
    _automation.switchMode[i] = 1;
    _automation.relayActiveLow[i] = 1; // Safe default for common low-trigger relay boards.
    strlcpy(_automation.relayName[i], names[i], sizeof(_automation.relayName[i]));
  }
  for (uint8_t i = 0; i < 8; ++i) {
    _automation.schedules[i].relay = 0;
    _automation.schedules[i].lastRunDay = -1;
  }
  updateAutomationCrc();
}

void ConfigManager::defaultFirebase() {
  memset(&_firebase, 0, sizeof(_firebase));
  _firebase.magic = FIREBASE_MAGIC;
  _firebase.version = FIREBASE_VERSION;
  updateFirebaseCrc();
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
  struct LegacyAutomationConfigV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint8_t switchPin[4];
    uint8_t switchActiveLow[4];
    uint8_t switchMode[4];
    char relayName[4][24];
    RelayTimerConfig timers[4];
    ScheduleRule schedules[8];
    uint32_t crc32;
  };
  // Legacy v1-v4 records are migrated safely. Version 4 used D0/GPIO16;
  // that prior switch assignment is remapped to D7/GPIO13 in v5.
  struct LegacyAutomationConfigV2 {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint8_t switchPin[4];
    uint8_t switchActiveLow[4];
    uint8_t switchMode[4];
    char relayName[4][24];
    char logoText[256];
    uint8_t relayActiveLow[4];
    RelayTimerConfig timers[4];
    ScheduleRule schedules[8];
    uint32_t crc32;
  };

  auto validLegacyCrc = [&](void* value, size_t size, uint32_t stored) -> bool {
    uint8_t* bytes = static_cast<uint8_t*>(value);
    // The caller zeroes the CRC field before calculating it.
    return stored == crc32(bytes, size);
  };
  auto installSwitchDefaults = [&]() {
    for (uint8_t i = 0; i < 4; ++i) {
      _automation.switchPin[i] = 255;
      _automation.switchActiveLow[i] = 1;
      _automation.switchMode[i] = 1;
    }
  };

  struct LegacyAutomationConfigV4 {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint8_t switchPin[4];
    uint8_t switchActiveLow[4];
    uint8_t switchMode[4];
    char relayName[4][24];
    char logoText[256];
    uint8_t relayActiveLow[4];
    RelayTimerConfig timers[4];
    ScheduleRule schedules[8];
    uint32_t crc32;
  };

  struct LegacyAutomationConfigV3 {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint8_t switchPin[4];
    uint8_t switchActiveLow[4];
    uint8_t switchMode[4];
    char relayName[4][24];
    char logoText[256];
    uint8_t relayActiveLow[4];
    RelayTimerConfig timers[4];
    ScheduleRule schedules[8];
    uint32_t crc32;
  };

  auto migrateV4 = [&](const char* path) -> bool {
    File f = LittleFS.open(path, "r");
    if (!f || (size_t)f.size() != sizeof(LegacyAutomationConfigV4)) {
      if (f) f.close();
      return false;
    }
    LegacyAutomationConfigV4 old{};
    const size_t n = f.read((uint8_t*)&old, sizeof(old));
    f.close();
    if (n != sizeof(old) || old.magic != AUTOMATION_MAGIC || old.version != 4) return false;
    const uint32_t stored = old.crc32;
    old.crc32 = 0;
    if (!validLegacyCrc(&old, sizeof(old), stored)) return false;
    defaultAutomation();
    memcpy(_automation.relayName, old.relayName, sizeof(old.relayName));
    strlcpy(_automation.logoText, old.logoText, sizeof(_automation.logoText));
    memcpy(_automation.relayActiveLow, old.relayActiveLow, sizeof(old.relayActiveLow));
    memcpy(_automation.timers, old.timers, sizeof(old.timers));
    memcpy(_automation.schedules, old.schedules, sizeof(old.schedules));
    for (uint8_t i = 0; i < 4; ++i) {
      // Previous firmware used D0/GPIO16. Remap that one assignment to D7/GPIO13.
      _automation.switchPin[i] = old.switchPin[i] == 16 ? 13 : 255;
      _automation.switchActiveLow[i] = 1;
      _automation.switchMode[i] = 1;
      if (_automation.switchPin[i] == 13) {
        _automation.timers[i].enabled = 0;
        _automation.timers[i].dueEpoch = 0;
        _automation.timers[i].remainingSec = 0;
        for (uint8_t j = 0; j < 8; ++j)
          if (_automation.schedules[j].relay == i) _automation.schedules[j].enabled = 0;
      }
    }
    updateAutomationCrc();
    return saveAutomation();
  };

  auto migrateV3 = [&](const char* path) -> bool {
    File f = LittleFS.open(path, "r");
    if (!f || (size_t)f.size() != sizeof(LegacyAutomationConfigV3)) {
      if (f) f.close();
      return false;
    }
    LegacyAutomationConfigV3 old{};
    const size_t n = f.read((uint8_t*)&old, sizeof(old));
    f.close();
    if (n != sizeof(old) || old.magic != AUTOMATION_MAGIC || old.version != 3) return false;
    const uint32_t stored = old.crc32;
    old.crc32 = 0;
    if (!validLegacyCrc(&old, sizeof(old), stored)) return false;
    defaultAutomation();
    memcpy(_automation.relayName, old.relayName, sizeof(old.relayName));
    strlcpy(_automation.logoText, old.logoText, sizeof(_automation.logoText));
    memcpy(_automation.relayActiveLow, old.relayActiveLow, sizeof(old.relayActiveLow));
    memcpy(_automation.timers, old.timers, sizeof(old.timers));
    memcpy(_automation.schedules, old.schedules, sizeof(old.schedules));
    installSwitchDefaults();
    updateAutomationCrc();
    return saveAutomation();
  };

  auto migrateV2 = [&](const char* path) -> bool {
    File f = LittleFS.open(path, "r");
    if (!f || (size_t)f.size() != sizeof(LegacyAutomationConfigV2)) {
      if (f) f.close();
      return false;
    }
    LegacyAutomationConfigV2 old{};
    const size_t n = f.read((uint8_t*)&old, sizeof(old));
    f.close();
    if (n != sizeof(old) || old.magic != AUTOMATION_MAGIC || old.version != 2) return false;
    const uint32_t stored = old.crc32;
    old.crc32 = 0;
    if (!validLegacyCrc(&old, sizeof(old), stored)) return false;

    defaultAutomation();
    memcpy(_automation.relayName, old.relayName, sizeof(old.relayName));
    strlcpy(_automation.logoText, old.logoText, sizeof(_automation.logoText));
    memcpy(_automation.relayActiveLow, old.relayActiveLow, sizeof(old.relayActiveLow));
    memcpy(_automation.timers, old.timers, sizeof(old.timers));
    memcpy(_automation.schedules, old.schedules, sizeof(old.schedules));
    installSwitchDefaults();
    updateAutomationCrc();
    return saveAutomation();
  };

  auto migrateV1 = [&](const char* path) -> bool {
    File f = LittleFS.open(path, "r");
    if (!f || (size_t)f.size() != sizeof(LegacyAutomationConfigV1)) {
      if (f) f.close();
      return false;
    }
    LegacyAutomationConfigV1 old{};
    const size_t n = f.read((uint8_t*)&old, sizeof(old));
    f.close();
    if (n != sizeof(old) || old.magic != AUTOMATION_MAGIC || old.version != 1) return false;
    const uint32_t stored = old.crc32;
    old.crc32 = 0;
    if (!validLegacyCrc(&old, sizeof(old), stored)) return false;

    defaultAutomation();
    memcpy(_automation.relayName, old.relayName, sizeof(old.relayName));
    memcpy(_automation.timers, old.timers, sizeof(old.timers));
    memcpy(_automation.schedules, old.schedules, sizeof(old.schedules));
    installSwitchDefaults();
    updateAutomationCrc();
    return saveAutomation();
  };

  if (loadBinary("/automation.bin", "/automation.bak", &_automation, sizeof(_automation))) return true;
  if (migrateV4("/automation.bin") || migrateV4("/automation.bak")) return true;
  if (migrateV3("/automation.bin") || migrateV3("/automation.bak")) return true;
  if (migrateV2("/automation.bin") || migrateV2("/automation.bak")) return true;
  if (migrateV1("/automation.bin") || migrateV1("/automation.bak")) return true;
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
  defaultFirebase();
  return save() && saveState() && saveAutomation() && saveFirebase();
}

bool ConfigManager::loadFirebase() {
  if (loadBinary("/firebase.bin", "/firebase.bak", &_firebase, sizeof(_firebase))) return true;
  defaultFirebase();
  return saveFirebase();
}

bool ConfigManager::saveFirebase() {
  updateFirebaseCrc();
  return saveBinary("/firebase.bin", "/firebase.bak", &_firebase, sizeof(_firebase));
}

bool ConfigManager::clearFirebase() {
  const FirebaseConfig previous = _firebase;
  defaultFirebase();
  if (saveFirebase()) return true;
  _firebase = previous;
  return false;
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
    if (size == sizeof(FirebaseConfig)) return validateFirebase(*(FirebaseConfig*)data);
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
  const size_t logoLen = strnlen(a.logoText, sizeof(a.logoText));
  if (logoLen == 0 || logoLen >= sizeof(a.logoText)) return false;
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t pin = a.switchPin[i];
    if (pin != 255 && pin != 13) return false;
    if (a.switchActiveLow[i] > 1 || a.switchMode[i] > 1 || a.relayActiveLow[i] > 1) return false;
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

bool ConfigManager::validateFirebase(const FirebaseConfig& f) const {
  if (f.magic != FIREBASE_MAGIC || f.version != FIREBASE_VERSION || f.enabled > 1) return false;
  const size_t urlLen = strnlen(f.databaseUrl, sizeof(f.databaseUrl));
  const size_t keyLen = strnlen(f.apiKey, sizeof(f.apiKey));
  const size_t idLen = strnlen(f.deviceId, sizeof(f.deviceId));
  const size_t emailLen = strnlen(f.deviceEmail, sizeof(f.deviceEmail));
  const size_t uidLen = strnlen(f.deviceUid, sizeof(f.deviceUid));
  const size_t tokenLen = strnlen(f.refreshToken, sizeof(f.refreshToken));
  if (f.enabled && (urlLen < 12 || urlLen >= sizeof(f.databaseUrl) ||
      keyLen < 20 || keyLen >= sizeof(f.apiKey) || idLen == 0 || idLen >= sizeof(f.deviceId) ||
      emailLen == 0 || emailLen >= sizeof(f.deviceEmail) || uidLen == 0 || uidLen >= sizeof(f.deviceUid) ||
      tokenLen == 0 || tokenLen >= sizeof(f.refreshToken))) return false;
  FirebaseConfig tmp = f;
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

void ConfigManager::updateFirebaseCrc() {
  _firebase.crc32 = 0;
  _firebase.crc32 = crc32((const uint8_t*)&_firebase, sizeof(_firebase));
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
