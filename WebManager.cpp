#include "WebManager.h"

#include <LittleFS.h>
#include <ArduinoJson.h>
#include <MD5Builder.h>
#include <ESP8266WiFi.h>
#include <time.h>

#include "ProjectVersion.h"

static const char* contentTypeForPath(const String& path) {
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css"))  return "text/css; charset=utf-8";
  if (path.endsWith(".js"))   return "application/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".ico"))  return "image/x-icon";
  return "text/plain; charset=utf-8";
}

void WebManager::begin(ConfigManager& cfg,
                       RelayManager& relays,
                       WiFiController& wifi,
                       AutomationManager& automation) {
  _cfg = &cfg;
  _relays = &relays;
  _wifi = &wifi;
  _automation = &automation;

  setupRoutes();
  startCaptiveDNS();
  _server.begin();
}

void WebManager::loop() {
  if (_dnsStarted) {
    _dnsServer.processNextRequest();
  }

  _server.handleClient();

  if (_restartAt != 0 &&
      (long)(millis() - _restartAt) >= 0) {
    _restartAt = 0;
    yield();
    ESP.restart();
  }
}

void WebManager::startCaptiveDNS() {
  _dnsStarted = _dnsServer.start(53, "*", _wifi->apAddress());
}

void WebManager::setupRoutes() {
  // Do NOT call collectHeaders() here.
  // ESP8266WebServer 3.1.2 has an overload/template interaction that can
  // fail when passed a C-array. OTA password is supplied as a normal
  // HTTP argument instead.

  _server.on("/", HTTP_GET, [this]() {
    sendFile("/index.html", "text/html; charset=utf-8");
  });

  _server.on("/style.css", HTTP_GET, [this]() {
    sendFile("/style.css", "text/css; charset=utf-8");
  });

  _server.on("/app.js", HTTP_GET, [this]() {
    sendFile("/app.js", "application/javascript; charset=utf-8");
  });

  _server.on("/settings.html", HTTP_GET, [this]() {
    sendFile("/settings.html", "text/html; charset=utf-8");
  });

  // Captive portal detection endpoints.
  _server.on("/generate_204", HTTP_GET, [this]() {
    handleCaptivePortal();
  });

  _server.on("/hotspot-detect.html", HTTP_GET, [this]() {
    handleCaptivePortal();
  });

  _server.on("/connecttest.txt", HTTP_GET, [this]() {
    handleCaptivePortal();
  });

  _server.on("/ncsi.txt", HTTP_GET, [this]() {
    handleCaptivePortal();
  });

  _server.on("/fwlink", HTTP_GET, [this]() {
    handleCaptivePortal();
  });

  _server.on("/success.txt", HTTP_GET, [this]() {
    handleCaptivePortal();
  });

  _server.on("/api/state", HTTP_GET, [this]() {
    handleState();
  });

  _server.on("/api/settings", HTTP_GET, [this]() {
    handleSettingsGet();
  });

  _server.on("/api/settings", HTTP_POST, [this]() {
    handleSettingsSave();
  });

  _server.on("/api/automation", HTTP_GET, [this]() {
    handleAutomationGet();
  });

  _server.on("/api/timer", HTTP_POST, [this]() {
    handleTimer();
  });

  _server.on("/api/schedule", HTTP_POST, [this]() {
    handleSchedule();
  });

  _server.on("/api/schedule", HTTP_DELETE, [this]() {
    handleSchedule();
  });

  _server.on("/api/relay", HTTP_POST, [this]() {
    handleRelay();
  });

  _server.on("/api/wifi/scan", HTTP_GET, [this]() {
    handleWifiScan();
  });

  _server.on("/api/ota/password", HTTP_POST, [this]() {
    handleOtaPassword();
  });

  _server.on("/api/factory-reset", HTTP_POST, [this]() {
    handleFactoryReset();
  });

  _server.on("/api/reboot", HTTP_POST, [this]() {
    handleReboot();
  });

  _server.on(
    "/api/ota/upload",
    HTTP_POST,
    [this]() {
      handleOtaUploadDone();
    },
    [this]() {
      handleOtaUpload();
    }
  );

  _server.onNotFound([this]() {
    if (_server.uri().startsWith("/api/")) {
      jsonError(404, "Not found");
      return;
    }

    handleCaptivePortal();
  });
}

void WebManager::sendFile(const char* path,
                          const char* contentType) {
  if (!LittleFS.exists(path)) {
    _server.send(
      404,
      "text/plain; charset=utf-8",
      "File not found"
    );
    return;
  }

  File f = LittleFS.open(path, "r");

  if (!f) {
    _server.send(
      500,
      "text/plain; charset=utf-8",
      "File open failed"
    );
    return;
  }

  _server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  _server.streamFile(
    f,
    contentType ? contentType : contentTypeForPath(path)
  );

  f.close();
}

void WebManager::handleCaptivePortal() {
  _server.sendHeader(
    "Cache-Control",
    "no-store, no-cache, must-revalidate"
  );

  _server.sendHeader("Location", "/", true);

  _server.send(
    302,
    "text/plain; charset=utf-8",
    "Room Automation: redirecting to dashboard"
  );
}

void WebManager::handleState() {
  JsonDocument doc;

  doc["version"] = PROJECT_VERSION;
  doc["wifiConnected"] = _wifi->staConnected();
  doc["ip"] = _wifi->ipString();
  doc["apIp"] = _wifi->apIpString();
  doc["rssi"] = _wifi->rssi();
  doc["heap"] = ESP.getFreeHeap();
  doc["uptimeSec"] = millis() / 1000UL;
  doc["apClients"] = WiFi.softAPgetStationNum();

  JsonArray relays = doc["relays"].to<JsonArray>();
  JsonArray names = doc["relayNames"].to<JsonArray>();
  for (uint8_t i = 0; i < 4; ++i) {
    relays.add(_relays->get(i));
    names.add(_cfg->automation().relayName[i]);
  }

  String out;
  serializeJson(doc, out);

  _server.send(
    200,
    "application/json; charset=utf-8",
    out
  );
}

void WebManager::handleSettingsGet() {
  const DeviceConfig& c = _cfg->config();
  const AutomationConfig& a = _cfg->automation();
  JsonDocument doc;

  doc["apSsid"] = c.apSsid;
  doc["apChannel"] = c.apChannel;
  doc["staSsid"] = c.staSsid;
  doc["deviceName"] = c.deviceName;
  doc["relayActiveLow"] = c.relayActiveLow != 0;
  doc["apPasswordSet"] = c.apPassword[0] != '\0';
  doc["staPasswordSet"] = c.staPassword[0] != '\0';
  doc["otaConfigured"] = strlen(c.otaMd5) == 32;

  JsonArray names = doc["relayNames"].to<JsonArray>();
  JsonArray switchPins = doc["switchPins"].to<JsonArray>();
  JsonArray switchActiveLow = doc["switchActiveLow"].to<JsonArray>();
  JsonArray switchMode = doc["switchMode"].to<JsonArray>();
  for (uint8_t i = 0; i < 4; ++i) {
    names.add(a.relayName[i]);
    switchPins.add(a.switchPin[i] == 255 ? -1 : a.switchPin[i]);
    switchActiveLow.add(a.switchActiveLow[i] != 0);
    switchMode.add(a.switchMode[i]);
  }

  // Passwords are NEVER returned to the browser.
  String out;
  serializeJson(doc, out);
  _server.send(200, "application/json; charset=utf-8", out);
}

void WebManager::handleSettingsSave() {
  JsonDocument doc;
  if (deserializeJson(doc, _server.arg("plain"))) {
    jsonError(400, "Invalid JSON");
    return;
  }

  const String apSsid = doc["apSsid"] | "";
  const String apPassword = doc["apPassword"] | "";
  const String staSsid = doc["staSsid"] | "";
  const String staPassword = doc["staPassword"] | "";
  const String deviceName = doc["deviceName"] | "";
  const int apChannelValue = doc["apChannel"] | 6;
  const bool relayActiveLow = doc["relayActiveLow"] | true;

  if (!validText(apSsid, 32, false) || apSsid.length() < 1) {
    jsonError(400, "Invalid AP SSID"); return;
  }
  if (apChannelValue < 1 || apChannelValue > 13) {
    jsonError(400, "Invalid AP channel"); return;
  }
  if (!validText(deviceName, 32, false) || deviceName.length() < 1 ||
      deviceName[0] == '-' || deviceName[deviceName.length() - 1] == '-') {
    jsonError(400, "Device name must be 1 to 32 letters, digits or hyphens"); return;
  }
  for (size_t i = 0; i < deviceName.length(); ++i) {
    const char ch = deviceName[i];
    const bool valid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') || ch == '-';
    if (!valid) { jsonError(400, "Device name must use only letters, digits and hyphens"); return; }
  }
  if (!validText(apPassword, 63, true) ||
      (apPassword.length() != 0 && apPassword.length() < 8) ||
      !validText(staSsid, 32, true) ||
      !validText(staPassword, 64, true)) {
    jsonError(400, "Invalid network field"); return;
  }

  const DeviceConfig previousConfig = _cfg->config();
  const AutomationConfig previousAutomation = _cfg->automation();
  DeviceConfig nextConfig = previousConfig;
  AutomationConfig nextAutomation = previousAutomation;

  strlcpy(nextConfig.apSsid, apSsid.c_str(), sizeof(nextConfig.apSsid));
  if (apPassword.length() != 0) strlcpy(nextConfig.apPassword, apPassword.c_str(), sizeof(nextConfig.apPassword));
  nextConfig.apChannel = (uint8_t)apChannelValue;
  strlcpy(nextConfig.staSsid, staSsid.c_str(), sizeof(nextConfig.staSsid));
  if (staSsid.length() == 0) {
    nextConfig.staPassword[0] = '\0';
  } else if (staSsid != String(previousConfig.staSsid)) {
    // A changed SSID with a blank password means an open network; do not
    // accidentally reuse credentials from the previously selected router.
    strlcpy(nextConfig.staPassword, staPassword.c_str(), sizeof(nextConfig.staPassword));
  } else if (staPassword.length() != 0) {
    strlcpy(nextConfig.staPassword, staPassword.c_str(), sizeof(nextConfig.staPassword));
  }
  strlcpy(nextConfig.deviceName, deviceName.c_str(), sizeof(nextConfig.deviceName));
  nextConfig.relayActiveLow = relayActiveLow ? 1 : 0;

  // Relay GPIOs are fixed by the user's hardware definition. Switch inputs
  // can be disabled (-1) or mapped only to the four documented spare pins.
  if (doc["relayNames"].is<JsonArray>()) {
    JsonArrayConst names = doc["relayNames"].as<JsonArrayConst>();
    if (names.size() != 4) { jsonError(400, "Exactly four relay names are required"); return; }
    for (uint8_t i = 0; i < 4; ++i) {
      const String name = names[i] | "";
      if (!validText(name, 23, false) || name.length() == 0) { jsonError(400, "Invalid relay name"); return; }
      strlcpy(nextAutomation.relayName[i], name.c_str(), sizeof(nextAutomation.relayName[i]));
    }
  }

  if (doc["switchPins"].is<JsonArray>()) {
    JsonArrayConst pins = doc["switchPins"].as<JsonArrayConst>();
    if (pins.size() != 4) { jsonError(400, "Exactly four switch pin values are required"); return; }
    for (uint8_t i = 0; i < 4; ++i) {
      const int pin = pins[i] | -1;
      if (pin != -1 && pin != 0 && pin != 2 && pin != 15 && pin != 16) {
        jsonError(400, "Switch pin must be disabled, GPIO0, GPIO2, GPIO15 or GPIO16"); return;
      }
      nextAutomation.switchPin[i] = pin < 0 ? 255 : (uint8_t)pin;
    }
  }

  if (doc["switchActiveLow"].is<JsonArray>()) {
    JsonArrayConst levels = doc["switchActiveLow"].as<JsonArrayConst>();
    if (levels.size() != 4) { jsonError(400, "Exactly four switch polarity values are required"); return; }
    for (uint8_t i = 0; i < 4; ++i) nextAutomation.switchActiveLow[i] = (levels[i] | true) ? 1 : 0;
  }

  if (doc["switchMode"].is<JsonArray>()) {
    JsonArrayConst modes = doc["switchMode"].as<JsonArrayConst>();
    if (modes.size() != 4) { jsonError(400, "Exactly four switch modes are required"); return; }
    for (uint8_t i = 0; i < 4; ++i) {
      const int mode = modes[i] | 0;
      if (mode < 0 || mode > 1) { jsonError(400, "Invalid switch mode"); return; }
      nextAutomation.switchMode[i] = (uint8_t)mode;
    }
  }

  for (uint8_t i = 0; i < 4; ++i) {
    for (uint8_t j = i + 1; j < 4; ++j) {
      if (nextAutomation.switchPin[i] != 255 && nextAutomation.switchPin[i] == nextAutomation.switchPin[j]) {
        jsonError(400, "A GPIO cannot be assigned to more than one switch"); return;
      }
    }
  }

  // Save the automation record first, then the network/device record. If the
  // second write fails, restore the previous automation record as well.
  _cfg->automation() = nextAutomation;
  if (!_cfg->saveAutomation()) {
    _cfg->automation() = previousAutomation;
    jsonError(500, "Could not save automation settings");
    return;
  }

  _cfg->config() = nextConfig;
  if (!_cfg->save()) {
    _cfg->config() = previousConfig;
    _cfg->automation() = previousAutomation;
    const bool rollbackOk = _cfg->saveAutomation();
    jsonError(500, rollbackOk
      ? "Could not save device settings; previous settings restored"
      : "Settings save and rollback failed; inspect device storage before continuing");
    return;
  }

  _server.send(200, "application/json; charset=utf-8",
    "{\"message\":\"Settings saved. Rebooting to apply network and switch GPIO changes.\"}");
  scheduleRestart(900);
}

void WebManager::handleAutomationGet() {
  JsonDocument doc;
  const bool synced = _wifi->timeSynced();
  doc["timeSynced"] = synced;
  doc["staConnected"] = _wifi->staConnected();
  if (synced) {
    const time_t now = time(nullptr);
    struct tm localTime;
    if (localtime_r(&now, &localTime)) {
      char buffer[24];
      strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);
      doc["localTime"] = buffer;
    }
  } else {
    doc["localTime"] = "Waiting for internet time sync";
  }

  JsonArray timers = doc["timers"].to<JsonArray>();
  JsonArray schedules = doc["schedules"].to<JsonArray>();
  const AutomationConfig& a = _automation->config();
  for (uint8_t i = 0; i < 4; ++i) {
    JsonObject t = timers.add<JsonObject>();
    t["relay"] = i;
    t["enabled"] = a.timers[i].enabled != 0;
    t["action"] = a.timers[i].action != 0;
    t["remainingSec"] = _automation->timerRemaining(i, synced);
  }
  for (uint8_t i = 0; i < 8; ++i) {
    const ScheduleRule& r = a.schedules[i];
    if (!r.enabled) continue;
    JsonObject s = schedules.add<JsonObject>();
    s["slot"] = i;
    s["relay"] = r.relay;
    s["action"] = r.action != 0;
    s["daysMask"] = r.daysMask;
    s["minuteOfDay"] = r.minuteOfDay;
    s["enabled"] = true;
  }
  String out;
  serializeJson(doc, out);
  _server.send(200, "application/json; charset=utf-8", out);
}

void WebManager::handleTimer() {
  JsonDocument doc;
  if (deserializeJson(doc, _server.arg("plain"))) { jsonError(400, "Invalid JSON"); return; }
  const int relay = doc["relay"] | -1;
  const int seconds = doc["seconds"] | -1;
  const bool action = doc["action"] | false;
  if (relay < 0 || relay > 3 || seconds < 0 || seconds > 2592000) {
    jsonError(400, "Invalid relay or timer duration (0 to 30 days)"); return;
  }
  const bool ok = seconds == 0 ? _automation->cancelTimer((uint8_t)relay)
                               : _automation->setTimer((uint8_t)relay, action, (uint32_t)seconds, _wifi->timeSynced());
  if (!ok) { jsonError(500, "Could not save timer"); return; }
  handleAutomationGet();
}

void WebManager::handleSchedule() {
  JsonDocument doc;
  if (deserializeJson(doc, _server.arg("plain"))) { jsonError(400, "Invalid JSON"); return; }
  const int slot = doc["slot"] | -1;
  if (slot < 0 || slot >= 8) { jsonError(400, "Schedule slot must be 0 to 7"); return; }

  bool ok = false;
  if (_server.method() == HTTP_DELETE) {
    ok = _automation->removeSchedule((uint8_t)slot);
  } else {
    const int relay = doc["relay"] | -1;
    const int daysMask = doc["daysMask"] | 0;
    const int minute = doc["minuteOfDay"] | -1;
    const bool action = doc["action"] | false;
    const bool enabled = doc["enabled"] | true;
    if (relay < 0 || relay > 3 || daysMask < 1 || daysMask > 127 || minute < 0 || minute > 1439) {
      jsonError(400, "Invalid schedule. Select relay, days and a valid time"); return;
    }
    ok = _automation->setSchedule((uint8_t)slot, (uint8_t)relay, action,
      (uint8_t)daysMask, (uint16_t)minute, enabled);
  }
  if (!ok) { jsonError(500, "Could not save schedule"); return; }
  handleAutomationGet();
}

void WebManager::handleRelay() {
  JsonDocument doc;

  if (deserializeJson(doc, _server.arg("plain"))) {
    jsonError(400, "Invalid JSON");
    return;
  }

  const int index = doc["index"] | -1;
  const bool on = doc["on"] | false;

  if (index < 0 || index >= 4) {
    jsonError(400, "Invalid relay index");
    return;
  }

  if (!_relays->set(
        (uint8_t)index,
        on,
        true)) {
    jsonError(
      500,
      "Could not save relay state"
    );
    return;
  }

  handleState();
}

void WebManager::handleWifiScan() {
  String json;

  if (!_wifi->scanNetworks(json)) {
    jsonError(500, "Wi-Fi scan failed");
    return;
  }

  _server.send(
    200,
    "application/json; charset=utf-8",
    json
  );
}

void WebManager::handleOtaPassword() {
  JsonDocument doc;

  if (deserializeJson(doc, _server.arg("plain"))) {
    jsonError(400, "Invalid JSON");
    return;
  }

  const String oldPassword =
    doc["oldPassword"] | "";

  const String newPassword =
    doc["newPassword"] | "";

  const String confirmPassword =
    doc["confirmPassword"] | "";

  const bool otaAlreadyConfigured = strlen(_cfg->config().otaMd5) == 32;
  if (otaAlreadyConfigured && !validOtaPassword(oldPassword)) {
    jsonError(403, "Old OTA password is incorrect");
    return;
  }

  if (newPassword.length() < 8 ||
      newPassword.length() > 64) {
    jsonError(
      400,
      "New OTA password must be 8 to 64 characters"
    );
    return;
  }

  if (newPassword != confirmPassword) {
    jsonError(
      400,
      "New passwords do not match"
    );
    return;
  }

  DeviceConfig& c = _cfg->config();

  const String hash = md5Of(newPassword);

  strlcpy(
    c.otaMd5,
    hash.c_str(),
    sizeof(c.otaMd5)
  );

  if (!_cfg->save()) {
    jsonError(
      500,
      "Could not save OTA password"
    );
    return;
  }

  _server.send(
    200,
    "application/json; charset=utf-8",
    "{\"message\":\"OTA password changed successfully. Rebooting.\"}"
  );

  scheduleRestart(900);
}

void WebManager::handleOtaUpload() {
  HTTPUpload& upload = _server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    _otaInProgress = false;
    _otaRejected = false;
    _otaReady = false;

    // Password is supplied as a normal HTTP argument named "password".
    // The UI should submit the firmware to:
    // /api/ota/upload?password=YOUR_PASSWORD
    //
    // The password is never returned by the device UI.
    const String supplied =
      _server.arg("password");

    if (!validOtaPassword(supplied)) {
      _otaRejected = true;
      return;
    }

    // HTTPUpload::totalSize is zero at UPLOAD_FILE_START and grows only
    // as chunks arrive. contentLength is the complete multipart request,
    // not the raw firmware size. Reserve the maximum safe sketch space and
    // let Update.end(true) finalize using the number of bytes actually sent.
    const size_t freeSketchSpace = ESP.getFreeSketchSpace();
    if (freeSketchSpace <= 0x1000U) {
      _otaRejected = true;
      return;
    }

    const size_t maxSketchSpace =
      (freeSketchSpace - 0x1000U) & 0xFFFFF000U;
    if (maxSketchSpace == 0 || !Update.begin(maxSketchSpace)) {
      _otaRejected = true;
      return;
    }

    _otaInProgress = true;
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!_otaInProgress || _otaRejected) {
      return;
    }

    const size_t written =
      Update.write(
        upload.buf,
        upload.currentSize
      );

    if (written != upload.currentSize) {
      _otaRejected = true;
      _otaInProgress = false;
      Update.end(false); // Discard the incomplete image and release updater state.
      return;
    }

    yield();
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (!_otaInProgress || _otaRejected) {
      return;
    }

    if (Update.end(true)) {
      _otaReady = true;
      _otaInProgress = false;
    } else {
      _otaRejected = true;
      _otaInProgress = false;
      _otaReady = false;
    }

    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    if (_otaInProgress) Update.end(false);
    _otaRejected = true;
    _otaInProgress = false;
    _otaReady = false;
  }
}

void WebManager::handleOtaUploadDone() {
  if (!_otaReady || _otaRejected) {
    _otaInProgress = false;
    _otaReady = false;

    _server.send(
      403,
      "application/json; charset=utf-8",
      "{\"error\":\"OTA update was rejected\"}"
    );

    return;
  }

  _otaReady = false;

  _server.send(
    200,
    "application/json; charset=utf-8",
    "{\"message\":\"Firmware uploaded successfully. Rebooting.\"}"
  );

  scheduleRestart(1200);
}

void WebManager::handleFactoryReset() {
  if (_server.arg("confirm") != "RESET") {
    jsonError(
      400,
      "Factory reset confirmation required"
    );
    return;
  }

  if (!_cfg->factoryReset()) {
    jsonError(
      500,
      "Factory reset failed"
    );
    return;
  }

  _server.send(
    200,
    "application/json; charset=utf-8",
    "{\"message\":\"Factory reset complete. Rebooting.\"}"
  );

  scheduleRestart(1000);
}

void WebManager::handleReboot() {
  _server.send(
    200,
    "application/json; charset=utf-8",
    "{\"message\":\"Controller is rebooting\"}"
  );

  scheduleRestart(500);
}

bool WebManager::validOtaPassword(
  const String& password) const {
  if (password.length() == 0) {
    return false;
  }

  return strlen(_cfg->config().otaMd5) == 32 &&
         md5Of(password) == String(_cfg->config().otaMd5);
}

String WebManager::md5Of(
  const String& value) const {
  MD5Builder md5;

  md5.begin();
  md5.add(value);
  md5.calculate();

  return md5.toString();
}

bool WebManager::validText(
  const String& value,
  size_t maxLen,
  bool allowEmpty) const {
  if (!allowEmpty && value.length() == 0) {
    return false;
  }

  if (value.length() > maxLen) {
    return false;
  }

  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c =
      (uint8_t)value[i];

    if (c < 0x20 || c == 0x7F) {
      return false;
    }
  }

  return true;
}

void WebManager::jsonError(
  int code,
  const char* message) {
  JsonDocument doc;
  doc["error"] = message;

  String out;
  serializeJson(doc, out);

  _server.send(
    code,
    "application/json; charset=utf-8",
    out
  );
}

void WebManager::scheduleRestart(
  unsigned long delayMs) {
  _restartAt = millis() + delayMs;
}
