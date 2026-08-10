#include "WiFiManager.h"

void WiFiController::begin(ConfigManager& cfg) {
  _cfg = &cfg;

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);
  WiFi.hostname(_cfg->config().deviceName);

  startAP();
  connectSTA();
}

void WiFiController::startAP() {
  if (!_cfg) return;

  WiFi.softAPdisconnect(true);
  yield();

  const IPAddress apIP(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress mask(255, 255, 255, 0);

  WiFi.softAPConfig(apIP, gateway, mask);

  WiFi.softAP(
    _cfg->config().apSsid,
    _cfg->config().apPassword,
    _cfg->config().apChannel,
    false,
    4
  );
}

void WiFiController::connectSTA() {
  if (!_cfg) return;

  if (_cfg->config().staSsid[0] == '\0') {
    _lastAttempt = millis();
    return;
  }

  WiFi.disconnect(false);
  yield();

  WiFi.begin(
    _cfg->config().staSsid,
    _cfg->config().staPassword
  );

  _lastAttempt = millis();
}

void WiFiController::stopMDNS() {
  if (_mdnsStarted) {
    MDNS.close();
    _mdnsStarted = false;
  }
}

void WiFiController::loop() {
  if (!_cfg) return;

  if (WiFi.status() == WL_CONNECTED) {
    _backoff = 1000;

    if (!_mdnsStarted) {
      if (MDNS.begin(_cfg->config().deviceName)) {
        MDNS.addService("http", "tcp", 80);
        _mdnsStarted = true;
      }
    }

    if (_mdnsStarted) {
      MDNS.update();
    }

    return;
  }

  stopMDNS();

  if (_cfg->config().staSsid[0] == '\0') {
    return;
  }

  const unsigned long now = millis();

  if ((unsigned long)(now - _lastAttempt) >= _backoff) {
    connectSTA();

    const unsigned long next =
      _backoff < 60000UL ? _backoff * 2UL : 60000UL;

    _backoff = next > 60000UL ? 60000UL : next;
  }

  yield();
}

void WiFiController::applySettings() {
  if (!_cfg) return;

  stopMDNS();

  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);
  WiFi.hostname(_cfg->config().deviceName);

  startAP();

  _backoff = 1000;
  connectSTA();
}

bool WiFiController::staConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

String WiFiController::ipString() const {
  if (!staConnected()) {
    return String("-");
  }

  return WiFi.localIP().toString();
}

String WiFiController::apIpString() const {
  return WiFi.softAPIP().toString();
}

IPAddress WiFiController::apAddress() const {
  return WiFi.softAPIP();
}

int32_t WiFiController::rssi() const {
  return staConnected() ? WiFi.RSSI() : 0;
}

bool WiFiController::scanNetworks(String& json) {
  WiFi.scanDelete();

  // Synchronous real RF scan. The second argument explicitly requests hidden SSIDs.\n  // WiFi.begin(ssid,password) below intentionally does not pin a channel: ESP8266\n  // will re-scan/follow the AP if the router changes channel.\n  const int count = WiFi.scanNetworks(false, true);

  if (count < 0) {
    json = "[]";
    return false;
  }

  json.reserve((size_t)count * 90U + 4U);
  json = "[";

  for (int i = 0; i < count; ++i) {
    if (i > 0) {
      json += ",";
    }

    String ssid = WiFi.SSID(i);

    // Minimal JSON escaping for SSID.
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    ssid.replace("\r", "\\r");
    ssid.replace("\n", "\\n");

    json += "{\"ssid\":\"";
    json += ssid;
    json += "\",\"rssi\":";
    json += String(WiFi.RSSI(i));
    json += ",\"channel\":";
    json += String(WiFi.channel(i));
    json += ",\"secure\":";
    json += (WiFi.encryptionType(i) == ENC_TYPE_NONE)
              ? "false"
              : "true";
    json += "}";

    yield();
  }

  json += "]";

  WiFi.scanDelete();
  return true;
}
