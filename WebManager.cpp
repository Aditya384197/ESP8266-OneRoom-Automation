#include "WebManager.h"

#include <LittleFS.h>
#include <ArduinoJson.h>
#include <MD5Builder.h>
#include <ESP8266WiFi.h>

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
                       WiFiController& wifi) {
  _cfg = &cfg;
  _relays = &relays;
  _wifi = &wifi;

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

  for (uint8_t i = 0; i < 4; ++i) {
    relays.add(_relays->get(i));
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

  JsonDocument doc;

  doc["apSsid"] = c.apSsid;
  doc["apChannel"] = c.apChannel;
  doc["staSsid"] = c.staSsid;
  doc["deviceName"] = c.deviceName;
  doc["relayActiveLow"] = c.relayActiveLow != 0;

  // Passwords are NEVER returned to the browser.
  String out;
  serializeJson(doc, out);

  _server.send(
    200,
    "application/json; charset=utf-8",
    out
  );
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
  const uint8_t apChannel =
    (uint8_t)(doc["apChannel"] | 6);

  const bool relayActiveLow =
    doc["relayActiveLow"] | true;

  if (!validText(apSsid, 32, false) ||
      apSsid.length() < 1) {
    jsonError(400, "Invalid AP SSID");
    return;
  }

  if (apChannel < 1 || apChannel > 13) {
    jsonError(400, "Invalid AP channel");
    return;
  }

  if (!validText(deviceName, 32, false) ||
      deviceName.length() < 1) {
    jsonError(400, "Invalid device name");
    return;
  }

  if (!validText(apPassword, 64, true) ||
      !validText(staSsid, 32, true) ||
      !validText(staPassword, 64, true)) {
    jsonError(400, "Invalid network field");
    return;
  }

  DeviceConfig& next = _cfg->config();

  strlcpy(
    next.apSsid,
    apSsid.c_str(),
    sizeof(next.apSsid)
  );

  if (apPassword.length() > 0) {
    strlcpy(
      next.apPassword,
      apPassword.c_str(),
      sizeof(next.apPassword)
    );
  }

  next.apChannel = apChannel;

  strlcpy(
    next.staSsid,
    staSsid.c_str(),
    sizeof(next.staSsid)
  );

  if (staPassword.length() > 0) {
    strlcpy(
      next.staPassword,
      staPassword.c_str(),
      sizeof(next.staPassword)
    );
  }

  strlcpy(
    next.deviceName,
    deviceName.c_str(),
    sizeof(next.deviceName)
  );

  next.relayActiveLow = relayActiveLow ? 1 : 0;

  if (!_cfg->save()) {
    jsonError(500, "Could not save configuration");
    return;
  }

  _server.send(
    200,
    "application/json; charset=utf-8",
    "{\"message\":\"Settings saved. Rebooting.\"}"
  );

  scheduleRestart(900);
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

  if (!validOtaPassword(oldPassword)) {
    jsonError(
      403,
      "Old OTA password is incorrect"
    );
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

    // ESP8266 Arduino core 3.1.2 requires the firmware size.
    // HTTPUpload::totalSize is the uploaded firmware size at START.
    const size_t firmwareSize = upload.totalSize;

    if (firmwareSize == 0) {
      _otaRejected = true;
      return;
    }

    if (!Update.begin(firmwareSize)) {
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

  return md5Of(password) ==
         String(_cfg->config().otaMd5);
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
