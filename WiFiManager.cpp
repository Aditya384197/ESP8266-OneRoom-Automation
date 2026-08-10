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

  const DeviceConfig& config = _cfg->config();

  if (config.staSsid[0] == '\0') {
    _lastAttempt = millis();
    return;
  }

  WiFi.disconnect(false);
  yield();

  /*
   * Do not manually lock the STA to a channel here.
   *
   * WiFi.begin(ssid, password) allows the ESP8266
   * station to discover the AP and follow its current
   * channel when reconnecting.
   */
  WiFi.begin(
    config.staSsid,
    config.staPassword
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
      (_backoff < 60000UL)
        ? (_backoff * 2UL)
        : 60000UL;

    _backoff =
      (next > 60000UL)
        ? 60000UL
        : next;
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
  json = "[]";

  /*
   * Remove results from any previous scan.
   */
  WiFi.scanDelete();
  yield();

  /*
   * Real synchronous RF scan.
   *
   * Parameter 1:
   *   false = synchronous scan
   *
   * Parameter 2:
   *   true = show hidden networks
   */
  int scanCount = WiFi.scanNetworks(false, true);

  if (scanCount < 0) {
    WiFi.scanDelete();
    return false;
  }

  /*
   * Keep memory usage controlled on ESP8266.
   */
  const int maxResults = 40;

  if (scanCount > maxResults) {
    scanCount = maxResults;
  }

  json.reserve(
    (size_t)scanCount * 110U + 4U
  );

  json = "[";

  for (int i = 0; i < scanCount; ++i) {

    if (i > 0) {
      json += ",";
    }

    String ssid = WiFi.SSID(i);

    /*
     * Empty SSID means the network is hidden.
     */
    const bool hidden =
      (ssid.length() == 0);

    /*
     * Escape characters that could break JSON.
     */
    ssid.replace("\\", "\\\\");
    ssid.replace("\"", "\\\"");
    ssid.replace("\r", "\\r");
    ssid.replace("\n", "\\n");

    const int32_t signal =
      WiFi.RSSI(i);

    const uint8_t channel =
      WiFi.channel(i);

    const bool secure =
      (WiFi.encryptionType(i) != ENC_TYPE_NONE);

    json += "{";

    json += "\"ssid\":\"";
    json += ssid;
    json += "\",";

    json += "\"rssi\":";
    json += String(signal);
    json += ",";

    json += "\"channel\":";
    json += String(channel);
    json += ",";

    json += "\"secure\":";
    json += secure ? "true" : "false";
    json += ",";

    json += "\"hidden\":";
    json += hidden ? "true" : "false";

    json += "}";

    /*
     * Give the ESP8266 background time between entries.
     */
    yield();
  }

  json += "]";

  /*
   * Free scan-result memory.
   */
  WiFi.scanDelete();

  return true;
}
