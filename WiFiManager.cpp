#include "WiFiManager.h"
#include <time.h>

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
    _connectPending = false;
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
  _connectPending = true;
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
    _connectPending = false;

    if (!_ntpStarted) {
      // India Standard Time (UTC+05:30). Time-based schedules are paused
      // until a valid NTP clock has been acquired.
      configTime(19800, 0, "pool.ntp.org", "time.google.com");
      _ntpStarted = true;
    }

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

  // Do not repeatedly call WiFi.begin() while the previous association is
  // still in progress; the ESP8266 can otherwise restart its scan loop.
  if (_connectPending) {
    if ((unsigned long)(now - _lastAttempt) < 15000UL) {
      yield();
      return;
    }
    // A connection attempt timed out. Increase the delay before the next
    // attempt instead of continuously restarting the ESP8266 association.
    _connectPending = false;
    _lastAttempt = now;
    const unsigned long next = _backoff >= 30000UL ? 60000UL : _backoff * 2UL;
    _backoff = next > 60000UL ? 60000UL : next;
  }

  if ((unsigned long)(now - _lastAttempt) >= _backoff) {
    connectSTA();
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
  _connectPending = false;
  connectSTA();
}

bool WiFiController::staConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool WiFiController::timeSynced() const {
  return time(nullptr) > 1700000000;
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

bool WiFiController::startNetworkScan() {
  // A second client may request a scan while one is already active. Reuse
  // the existing scan rather than starting a competing radio operation.
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return true;

  WiFi.scanDelete();
  const int result = WiFi.scanNetworks(true, true); // async, include hidden APs
  return result == WIFI_SCAN_RUNNING || result >= 0;
}

int WiFiController::collectNetworkScan(String& json) {
  json = "[]";
  const int completed = WiFi.scanComplete();
  if (completed == WIFI_SCAN_RUNNING) return 0;
  if (completed == WIFI_SCAN_FAILED || completed < 0) {
    WiFi.scanDelete();
    return -1;
  }

  int scanCount = completed;
  if (scanCount > 40) scanCount = 40;
  json.reserve((size_t)scanCount * 110U + 4U);
  json = "[";

  for (int i = 0; i < scanCount; ++i) {
    if (i > 0) json += ",";
    String ssid = WiFi.SSID(i);
    const bool hidden = ssid.length() == 0;
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
    json += WiFi.encryptionType(i) != ENC_TYPE_NONE ? "true" : "false";
    json += ",\"hidden\":";
    json += hidden ? "true}" : "false}";
  }

  json += "]";
  WiFi.scanDelete();
  return 1;
}
