#include "WebManager.h"

#include <LittleFS.h>
#include <MD5Builder.h>
#include <ESP8266WiFi.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "ProjectVersion.h"

static constexpr size_t MAX_JSON_BODY = 4096U;
struct JsonBodyBuffer {
  size_t total;
  size_t received;
  bool tooLarge;
  bool allocationFailed;
  char data[1];
};
static void sendJson(AsyncWebServerRequest* request, int code, const String& body) {
  AsyncWebServerResponse* response = request->beginResponse(code, "application/json; charset=utf-8", body);
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("Connection", "close");
  request->send(response);
}

void WebManager::begin(ConfigManager& cfg, RelayManager& relays,
                       WiFiController& wifi, AutomationManager& automation) {
  _cfg = &cfg;
  _relays = &relays;
  _wifi = &wifi;
  _automation = &automation;
  setupRoutes();
  startCaptiveDNS();
  _server.begin();
}

void WebManager::loop() {
  if (_dnsStarted) _dnsServer.processNextRequest();
  if (_restartAt != 0 && (long)(millis() - _restartAt) >= 0) {
    _restartAt = 0;
    yield();
    ESP.restart();
  }
}

void WebManager::startCaptiveDNS() {
  _dnsStarted = _dnsServer.start(53, "*", _wifi->apAddress());
}

void WebManager::onJson(const char* uri, WebRequestMethodComposite method,
                       JsonRouteHandler handler) {
  _server.on(uri, method,
    [this, handler](AsyncWebServerRequest* request) {
      JsonBodyBuffer* body = static_cast<JsonBodyBuffer*>(request->_tempObject);
      if (!body) {
        jsonError(request, 400, "Missing JSON request body");
        return;
      }
      if (body->tooLarge) {
        request->_tempObject = nullptr;
        free(body);
        jsonError(request, 413, "Request body is too large");
        return;
      }
      if (body->allocationFailed) {
        request->_tempObject = nullptr;
        free(body);
        jsonError(request, 503, "Not enough memory for request body");
        return;
      }

      if (body->received != body->total) {
        request->_tempObject = nullptr;
        free(body);
        jsonError(request, 400, "Incomplete JSON request body");
        return;
      }
      JsonDocument doc;
      const DeserializationError error = deserializeJson(doc, body->data, body->total);
      request->_tempObject = nullptr;
      free(body);
      if (error) {
        jsonError(request, 400, "Invalid JSON");
        return;
      }
      handler(request, doc);
    }, nullptr,
    [](AsyncWebServerRequest* request, uint8_t* data, size_t len,
       size_t index, size_t total) {
      if (index == 0) {
        const size_t allocationSize = total > MAX_JSON_BODY
          ? sizeof(JsonBodyBuffer)
          : sizeof(JsonBodyBuffer) + (total ? total : 1U);
        JsonBodyBuffer* body = static_cast<JsonBodyBuffer*>(malloc(allocationSize));
        if (!body) {
          // Keep a tiny valid malloc block so request destruction can safely
          // free _tempObject even when allocation of the body fails.
          body = static_cast<JsonBodyBuffer*>(malloc(sizeof(JsonBodyBuffer)));
          if (!body) return;
          body->total = 0;
          body->received = 0;
          body->tooLarge = false;
          body->allocationFailed = true;
          body->data[0] = '\0';
          request->_tempObject = body;
          return;
        }
        body->total = total;
        body->received = 0;
        body->tooLarge = total > MAX_JSON_BODY;
        body->allocationFailed = false;
        body->data[0] = '\0';
        request->_tempObject = body;
      }

      JsonBodyBuffer* body = static_cast<JsonBodyBuffer*>(request->_tempObject);
      if (!body || body->tooLarge || body->allocationFailed) return;
      if (index != body->received || index > body->total || len > body->total - index) {
        body->tooLarge = true;
        return;
      }
      if (len) memcpy(body->data + index, data, len);
      body->received += len;
      if (body->received == body->total) body->data[body->total] = '\0';
    }
  );
}

void WebManager::setupRoutes() {
  _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* request) {
    sendFile(request, "/index.html", "text/html; charset=utf-8");
  });
  _server.on("/style.css", HTTP_GET, [this](AsyncWebServerRequest* request) {
    sendFile(request, "/style.css", "text/css; charset=utf-8");
  });
  _server.on("/app.js", HTTP_GET, [this](AsyncWebServerRequest* request) {
    sendFile(request, "/app.js", "application/javascript; charset=utf-8");
  });
  _server.on("/settings.html", HTTP_GET, [this](AsyncWebServerRequest* request) {
    sendFile(request, "/settings.html", "text/html; charset=utf-8");
  });

  const char* captivePaths[] = {
    "/generate_204", "/hotspot-detect.html", "/connecttest.txt",
    "/ncsi.txt", "/fwlink", "/success.txt"
  };
  for (const char* path : captivePaths) {
    _server.on(path, HTTP_GET, [this](AsyncWebServerRequest* request) {
      handleCaptivePortal(request);
    });
  }

  _server.on("/api/state", HTTP_GET, [this](AsyncWebServerRequest* request) { handleState(request); });
  _server.on("/api/settings", HTTP_GET, [this](AsyncWebServerRequest* request) { handleSettingsGet(request); });
  onJson("/api/settings", HTTP_POST, [this](AsyncWebServerRequest* request, JsonDocument& doc) {
    handleSettingsSave(request, doc);
  });
  onJson("/api/logo", HTTP_POST, [this](AsyncWebServerRequest* request, JsonDocument& doc) {
    handleLogoSave(request, doc);
  });
  _server.on("/api/automation", HTTP_GET, [this](AsyncWebServerRequest* request) { handleAutomationGet(request); });
  onJson("/api/timer", HTTP_POST, [this](AsyncWebServerRequest* request, JsonDocument& doc) { handleTimer(request, doc); });
  onJson("/api/schedule", HTTP_POST, [this](AsyncWebServerRequest* request, JsonDocument& doc) { handleSchedule(request, doc); });
  onJson("/api/schedule", HTTP_DELETE, [this](AsyncWebServerRequest* request, JsonDocument& doc) { handleSchedule(request, doc); });
  onJson("/api/relay", HTTP_POST, [this](AsyncWebServerRequest* request, JsonDocument& doc) { handleRelay(request, doc); });
  _server.on("/api/wifi/scan", HTTP_GET, [this](AsyncWebServerRequest* request) { handleWifiScan(request); });
  _server.on("/api/wifi/scan/result", HTTP_GET, [this](AsyncWebServerRequest* request) { handleWifiScanResult(request); });
  onJson("/api/ota/password", HTTP_POST, [this](AsyncWebServerRequest* request, JsonDocument& doc) { handleOtaPassword(request, doc); });
  _server.on("/api/factory-reset", HTTP_POST, [this](AsyncWebServerRequest* request) { handleFactoryReset(request); });
  _server.on("/api/reboot", HTTP_POST, [this](AsyncWebServerRequest* request) { handleReboot(request); });
  _server.on("/api/ota/upload", HTTP_POST,
    [this](AsyncWebServerRequest* request) { handleOtaUploadDone(request); },
    [this](AsyncWebServerRequest* request, const String& filename, size_t index,
           uint8_t* data, size_t len, bool final) {
      handleOtaUpload(request, filename, index, data, len, final);
    }
  );
  _server.onNotFound([this](AsyncWebServerRequest* request) {
    if (request->url().startsWith("/api/")) {
      jsonError(request, 404, "Not found");
      return;
    }
    handleCaptivePortal(request);
  });
}

void WebManager::sendFile(AsyncWebServerRequest* request, const char* path, const char* contentType) {
  if (!LittleFS.exists(path)) {
    request->send(404, "text/plain; charset=utf-8", "File not found");
    return;
  }
  AsyncWebServerResponse* response = request->beginResponse(LittleFS, path, contentType);
  response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  response->addHeader("Connection", "close");
  request->send(response);
}

void WebManager::handleCaptivePortal(AsyncWebServerRequest* request) {
  AsyncWebServerResponse* response = request->beginResponse(302, "text/plain; charset=utf-8", "Room Automation: redirecting to dashboard");
  response->addHeader("Location", "/");
  response->addHeader("Cache-Control", "no-store");
  request->send(response);
}

void WebManager::handleState(AsyncWebServerRequest* request) {
  JsonDocument doc;
  doc["version"] = PROJECT_VERSION;
  doc["wifiConnected"] = _wifi->staConnected();
  doc["ip"] = _wifi->ipString();
  doc["apIp"] = _wifi->apIpString();
  doc["rssi"] = _wifi->rssi();
  doc["heap"] = ESP.getFreeHeap();
  doc["uptimeSec"] = millis() / 1000UL;
  doc["apClients"] = WiFi.softAPgetStationNum();
  doc["logoText"] = _cfg->automation().logoText;
  JsonArray relays = doc["relays"].to<JsonArray>();
  JsonArray names = doc["relayNames"].to<JsonArray>();
  for (uint8_t i = 0; i < 4; ++i) {
    relays.add(_relays->get(i));
    names.add(_cfg->automation().relayName[i]);
  }
  String out;
  serializeJson(doc, out);
  sendJson(request, 200, out);
}

void WebManager::handleSettingsGet(AsyncWebServerRequest* request) {
  const DeviceConfig& c = _cfg->config();
  const AutomationConfig& a = _cfg->automation();
  JsonDocument doc;
  doc["apSsid"] = c.apSsid;
  doc["apChannel"] = c.apChannel;
  doc["staSsid"] = c.staSsid;
  doc["deviceName"] = c.deviceName;
  doc["logoText"] = a.logoText;
  doc["apPasswordSet"] = c.apPassword[0] != '\0';
  doc["staPasswordSet"] = c.staPassword[0] != '\0';
  doc["otaConfigured"] = strlen(c.otaMd5) == 32;
  JsonArray names = doc["relayNames"].to<JsonArray>();
  JsonArray switchPins = doc["switchPins"].to<JsonArray>();
  JsonArray switchActiveLow = doc["switchActiveLow"].to<JsonArray>();
  JsonArray switchMode = doc["switchMode"].to<JsonArray>();
  JsonArray relayActiveLow = doc["relayActiveLow"].to<JsonArray>();
  for (uint8_t i = 0; i < 4; ++i) {
    names.add(a.relayName[i]);
    switchPins.add(a.switchPin[i] == 255 ? -1 : a.switchPin[i]);
    switchActiveLow.add(a.switchActiveLow[i] != 0);
    switchMode.add(a.switchMode[i]);
    relayActiveLow.add(a.relayActiveLow[i] != 0);
  }
  String out;
  serializeJson(doc, out);
  sendJson(request, 200, out);
}

void WebManager::handleLogoSave(AsyncWebServerRequest* request, JsonDocument& doc) {
  const String logoText = doc["logoText"] | "";
  if (!validText(logoText, 255, false) || logoText.length() < 1) {
    jsonError(request, 400, "Logo text must be 1 to 255 characters");
    return;
  }
  AutomationConfig previous = _cfg->automation();
  strlcpy(_cfg->automation().logoText, logoText.c_str(), sizeof(_cfg->automation().logoText));
  if (!_cfg->saveAutomation()) {
    _cfg->automation() = previous;
    jsonError(request, 500, "Could not save logo text");
    return;
  }
  sendJson(request, 200, String("{\"message\":\"Logo saved successfully.\"}"));
}

void WebManager::handleSettingsSave(AsyncWebServerRequest* request, JsonDocument& doc) {
  const String apSsid = doc["apSsid"] | "";
  const String apPassword = doc["apPassword"] | "";
  const String staSsid = doc["staSsid"] | "";
  const String staPassword = doc["staPassword"] | "";
  const String deviceName = doc["deviceName"] | "";
  const String logoText = doc["logoText"] | _cfg->automation().logoText;
  const int apChannelValue = doc["apChannel"] | 6;

  if (!validText(apSsid, 32, false) || apSsid.length() < 1) { jsonError(request, 400, "Invalid AP SSID"); return; }
  if (apChannelValue < 1 || apChannelValue > 13) { jsonError(request, 400, "Invalid AP channel"); return; }
  if (!validText(logoText, 255, false) || logoText.length() < 1) { jsonError(request, 400, "Logo text must be 1 to 255 characters"); return; }
  if (!validText(deviceName, 32, false) || deviceName.length() < 1 || deviceName[0] == '-' || deviceName[deviceName.length() - 1] == '-') {
    jsonError(request, 400, "Device name must be 1 to 32 letters, digits or hyphens"); return;
  }
  for (size_t i = 0; i < deviceName.length(); ++i) {
    const char ch = deviceName[i];
    const bool valid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-';
    if (!valid) { jsonError(request, 400, "Device name must use only letters, digits and hyphens"); return; }
  }
  if (!validText(apPassword, 63, true) || (apPassword.length() != 0 && apPassword.length() < 8) ||
      !validText(staSsid, 32, true) || !validText(staPassword, 64, true)) {
    jsonError(request, 400, "Invalid network field"); return;
  }

  const DeviceConfig previousConfig = _cfg->config();
  const AutomationConfig previousAutomation = _cfg->automation();
  DeviceConfig nextConfig = previousConfig;
  AutomationConfig nextAutomation = previousAutomation;
  strlcpy(nextConfig.apSsid, apSsid.c_str(), sizeof(nextConfig.apSsid));
  if (apPassword.length() != 0) strlcpy(nextConfig.apPassword, apPassword.c_str(), sizeof(nextConfig.apPassword));
  nextConfig.apChannel = (uint8_t)apChannelValue;
  strlcpy(nextConfig.staSsid, staSsid.c_str(), sizeof(nextConfig.staSsid));
  if (staSsid.length() == 0) nextConfig.staPassword[0] = '\0';
  else if (staSsid != String(previousConfig.staSsid)) strlcpy(nextConfig.staPassword, staPassword.c_str(), sizeof(nextConfig.staPassword));
  else if (staPassword.length() != 0) strlcpy(nextConfig.staPassword, staPassword.c_str(), sizeof(nextConfig.staPassword));
  strlcpy(nextConfig.deviceName, deviceName.c_str(), sizeof(nextConfig.deviceName));
  strlcpy(nextAutomation.logoText, logoText.c_str(), sizeof(nextAutomation.logoText));

  if (doc["relayActiveLow"].is<JsonArray>()) {
    JsonArrayConst values = doc["relayActiveLow"].as<JsonArrayConst>();
    if (values.size() != 4) { jsonError(request, 400, "Exactly four relay polarity values are required"); return; }
    for (uint8_t i = 0; i < 4; ++i) nextAutomation.relayActiveLow[i] = values[i].as<bool>() ? 1 : 0;
  }
  if (doc["relayNames"].is<JsonArray>()) {
    JsonArrayConst names = doc["relayNames"].as<JsonArrayConst>();
    if (names.size() != 4) { jsonError(request, 400, "Exactly four relay names are required"); return; }
    for (uint8_t i = 0; i < 4; ++i) {
      const String name = names[i] | "";
      if (!validText(name, 23, false) || name.length() == 0) { jsonError(request, 400, "Invalid relay name"); return; }
      strlcpy(nextAutomation.relayName[i], name.c_str(), sizeof(nextAutomation.relayName[i]));
    }
  }
  if (doc["switchPins"].is<JsonArray>()) {
    JsonArrayConst pins = doc["switchPins"].as<JsonArrayConst>();
    if (pins.size() != 4) { jsonError(request, 400, "Exactly four switch port values are required"); return; }
    for (uint8_t i = 0; i < 4; ++i) {
      const int pin = pins[i] | -1;
      if (pin != -1 && (pin < 32 || pin > 35)) { jsonError(request, 400, "Switch input must be Disabled or PCF8574 P0-P3"); return; }
      nextAutomation.switchPin[i] = pin < 0 ? 255 : (uint8_t)pin;
    }
  }
  if (doc["switchActiveLow"].is<JsonArray>()) {
    JsonArrayConst values = doc["switchActiveLow"].as<JsonArrayConst>();
    if (values.size() != 4) { jsonError(request, 400, "Exactly four switch polarity values are required"); return; }
    for (uint8_t i = 0; i < 4; ++i) nextAutomation.switchActiveLow[i] = 1; // GND-closing inputs are always Active-Low.
  }
  if (doc["switchMode"].is<JsonArray>()) {
    JsonArrayConst modes = doc["switchMode"].as<JsonArrayConst>();
    if (modes.size() != 4) { jsonError(request, 400, "Exactly four switch modes are required"); return; }
    for (uint8_t i = 0; i < 4; ++i) {
      const int mode = modes[i] | 1;
      if (mode < 0 || mode > 1) { jsonError(request, 400, "Invalid switch mode"); return; }
      nextAutomation.switchMode[i] = (uint8_t)mode;
    }
  }
  for (uint8_t i = 0; i < 4; ++i) {
    for (uint8_t j = i + 1; j < 4; ++j) {
      if (nextAutomation.switchPin[i] != 255 && nextAutomation.switchPin[i] == nextAutomation.switchPin[j]) {
        jsonError(request, 400, "A PCF8574 input cannot be assigned to more than one relay"); return;
      }
    }
  }

  _cfg->automation() = nextAutomation;
  if (!_cfg->saveAutomation()) {
    _cfg->automation() = previousAutomation;
    jsonError(request, 500, "Could not save automation settings");
    return;
  }
  _cfg->config() = nextConfig;
  if (!_cfg->save()) {
    _cfg->config() = previousConfig;
    _cfg->automation() = previousAutomation;
    const bool rollbackOk = _cfg->saveAutomation();
    jsonError(request, 500, rollbackOk ? "Could not save device settings; previous settings restored" : "Settings save and rollback failed; inspect device storage before continuing");
    return;
  }
  sendJson(request, 200, String("{\"message\":\"Settings saved. Restarting to apply network and switch input changes.\"}"));
  scheduleRestart(1200);
}

void WebManager::handleAutomationGet(AsyncWebServerRequest* request) {
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
  } else doc["localTime"] = "Waiting for internet time sync";
  JsonArray timers = doc["timers"].to<JsonArray>();
  JsonArray schedules = doc["schedules"].to<JsonArray>();
  const AutomationConfig& a = _automation->config();
  for (uint8_t i = 0; i < 4; ++i) {
    JsonObject t = timers.add<JsonObject>();
    t["relay"] = i; t["enabled"] = a.timers[i].enabled != 0;
    t["action"] = a.timers[i].action != 0; t["remainingSec"] = _automation->timerRemaining(i, synced);
  }
  for (uint8_t i = 0; i < 8; ++i) {
    const ScheduleRule& r = a.schedules[i];
    if (!r.enabled) continue;
    JsonObject s = schedules.add<JsonObject>();
    s["slot"] = i; s["relay"] = r.relay; s["action"] = r.action != 0;
    s["daysMask"] = r.daysMask; s["minuteOfDay"] = r.minuteOfDay; s["enabled"] = true;
  }
  String out; serializeJson(doc, out); sendJson(request, 200, out);
}

void WebManager::handleTimer(AsyncWebServerRequest* request, JsonDocument& doc) {
  const int relay = doc["relay"] | -1;
  const int seconds = doc["seconds"] | -1;
  const bool action = doc["action"] | false;
  if (relay < 0 || relay > 3 || seconds < 0 || seconds > 2592000) { jsonError(request, 400, "Invalid relay or timer duration (0 to 30 days)"); return; }
  const bool ok = seconds == 0 ? _automation->cancelTimer((uint8_t)relay)
    : _automation->setTimer((uint8_t)relay, action, (uint32_t)seconds, _wifi->timeSynced());
  if (!ok) { jsonError(request, 500, "Could not save timer"); return; }
  handleAutomationGet(request);
}

void WebManager::handleSchedule(AsyncWebServerRequest* request, JsonDocument& doc) {
  const int slot = doc["slot"] | -1;
  if (slot < 0 || slot >= 8) { jsonError(request, 400, "Schedule slot must be 0 to 7"); return; }
  bool ok = false;
  if (request->method() == HTTP_DELETE) ok = _automation->removeSchedule((uint8_t)slot);
  else {
    const int relay = doc["relay"] | -1;
    const int daysMask = doc["daysMask"] | 0;
    const int minute = doc["minuteOfDay"] | -1;
    const bool action = doc["action"] | false;
    const bool enabled = doc["enabled"] | true;
    if (relay < 0 || relay > 3 || daysMask < 1 || daysMask > 127 || minute < 0 || minute > 1439) {
      jsonError(request, 400, "Invalid schedule. Select relay, days and a valid time"); return;
    }
    ok = _automation->setSchedule((uint8_t)slot, (uint8_t)relay, action, (uint8_t)daysMask, (uint16_t)minute, enabled);
  }
  if (!ok) { jsonError(request, 500, "Could not save schedule"); return; }
  handleAutomationGet(request);
}

void WebManager::handleRelay(AsyncWebServerRequest* request, JsonDocument& doc) {
  const int index = doc["index"] | -1;
  const bool on = doc["on"] | false;
  if (index < 0 || index >= 4) { jsonError(request, 400, "Invalid relay index"); return; }
  if (!_relays->set((uint8_t)index, on, true)) { jsonError(request, 500, "Could not save relay state"); return; }
  handleState(request);
}

void WebManager::handleWifiScan(AsyncWebServerRequest* request) {
  if (!_wifi->startNetworkScan()) { jsonError(request, 503, "Wi-Fi scan could not start"); return; }
  sendJson(request, 202, String("{\"scanning\":true}"));
}

void WebManager::handleWifiScanResult(AsyncWebServerRequest* request) {
  String networks;
  const int result = _wifi->collectNetworkScan(networks);
  if (result == 0) {
    sendJson(request, 200, String("{\"scanning\":true}"));
    return;
  }
  if (result < 0) { jsonError(request, 500, "Wi-Fi scan failed"); return; }
  String payload;
  payload.reserve(networks.length() + 32U);
  payload = "{\"scanning\":false,\"networks\":";
  payload += networks;
  payload += "}";
  sendJson(request, 200, payload);
}

void WebManager::handleOtaPassword(AsyncWebServerRequest* request, JsonDocument& doc) {
  const String oldPassword = doc["oldPassword"] | "";
  const String newPassword = doc["newPassword"] | "";
  const String confirmPassword = doc["confirmPassword"] | "";
  const bool otaAlreadyConfigured = strlen(_cfg->config().otaMd5) == 32;
  if (otaAlreadyConfigured && !validOtaPassword(oldPassword)) { jsonError(request, 403, "Old OTA password is incorrect"); return; }
  if (newPassword.length() < 8 || newPassword.length() > 64) { jsonError(request, 400, "New OTA password must be 8 to 64 characters"); return; }
  if (newPassword != confirmPassword) { jsonError(request, 400, "New passwords do not match"); return; }
  DeviceConfig previous = _cfg->config();
  const String hash = md5Of(newPassword);
  strlcpy(_cfg->config().otaMd5, hash.c_str(), sizeof(_cfg->config().otaMd5));
  if (!_cfg->save()) {
    _cfg->config() = previous;
    jsonError(request, 500, "Could not save OTA password");
    return;
  }
  sendJson(request, 200, String("{\"message\":\"OTA password changed successfully. Restarting.\"}"));
  scheduleRestart(1200);
}

void WebManager::handleOtaUpload(AsyncWebServerRequest* request, const String& filename,
                                 size_t index, uint8_t* data, size_t len, bool final) {
  (void)filename;
  if (index == 0) {
    // A second client must not reset or poison the active client upload state.
    if (_otaInProgress && _otaOwner != request) return;
    _otaOwner = request;
    _otaInProgress = false; _otaRejected = false; _otaReady = false;
    const String supplied = request->hasParam("password") ? request->getParam("password")->value() : String();
    if (!validOtaPassword(supplied)) { _otaRejected = true; return; }
    const size_t freeSketchSpace = ESP.getFreeSketchSpace();
    if (freeSketchSpace <= 0x1000U) { _otaRejected = true; return; }
    const size_t maxSketchSpace = (freeSketchSpace - 0x1000U) & 0xFFFFF000U;
    if (maxSketchSpace == 0 || !Update.begin(maxSketchSpace)) { _otaRejected = true; return; }
    _otaInProgress = true;
  }
  if (_otaOwner != request || _otaRejected) return;
  if (len && _otaInProgress) {
    const size_t written = Update.write(data, len);
    if (written != len) { _otaRejected = true; _otaInProgress = false; Update.end(false); return; }
  }
  if (final) {
    if (_otaInProgress && !_otaRejected && Update.end(true)) _otaReady = true;
    else { _otaRejected = true; if (_otaInProgress) Update.end(false); }
    _otaInProgress = false;
  }
}

void WebManager::handleOtaUploadDone(AsyncWebServerRequest* request) {
  if (_otaOwner != request || !_otaReady || _otaRejected) {
    if (_otaOwner == request) { _otaOwner = nullptr; _otaInProgress = false; _otaReady = false; }
    request->send(403, "application/json; charset=utf-8", "{\"error\":\"OTA update was rejected\"}");
    return;
  }
  _otaOwner = nullptr; _otaReady = false;
  request->send(200, "application/json; charset=utf-8", "{\"message\":\"Firmware uploaded successfully. Restarting.\"}");
  scheduleRestart(1200);
}

void WebManager::handleFactoryReset(AsyncWebServerRequest* request) {
  if (!request->hasParam("confirm", true) || request->getParam("confirm", true)->value() != "RESET") {
    jsonError(request, 400, "Factory reset confirmation required"); return;
  }
  if (!_cfg->factoryReset()) { jsonError(request, 500, "Factory reset failed"); return; }
  sendJson(request, 200, String("{\"message\":\"Factory reset complete. Restarting.\"}"));
  scheduleRestart(1200);
}

void WebManager::handleReboot(AsyncWebServerRequest* request) {
  sendJson(request, 200, String("{\"message\":\"Controller is restarting\"}"));
  scheduleRestart(700);
}

bool WebManager::validOtaPassword(const String& password) const {
  return password.length() != 0 && strlen(_cfg->config().otaMd5) == 32 && md5Of(password) == String(_cfg->config().otaMd5);
}

String WebManager::md5Of(const String& value) const {
  MD5Builder md5; md5.begin(); md5.add(value); md5.calculate(); return md5.toString();
}

bool WebManager::validText(const String& value, size_t maxLen, bool allowEmpty) const {
  if (!allowEmpty && value.length() == 0) return false;
  if (value.length() > maxLen) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = (uint8_t)value[i];
    if (c < 0x20 || c == 0x7F) return false;
  }
  return true;
}

void WebManager::jsonError(AsyncWebServerRequest* request, int code, const char* message) {
  JsonDocument doc; doc["error"] = message;
  String out; serializeJson(doc, out); sendJson(request, code, out);
}

void WebManager::scheduleRestart(unsigned long delayMs) {
  _restartAt = millis() + delayMs;
}
