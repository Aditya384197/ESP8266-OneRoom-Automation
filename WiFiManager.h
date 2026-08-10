#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include "ConfigManager.h"

class WiFiController {
public:
  WiFiController() = default;

  void begin(ConfigManager& cfg);
  void loop();
  void applySettings();

  bool staConnected() const;
  String ipString() const;
  String apIpString() const;
  IPAddress apAddress() const;
  int32_t rssi() const;

  // Returns a JSON array string containing nearby networks.
  bool scanNetworks(String& json);

private:
  ConfigManager* _cfg = nullptr;
  unsigned long _lastAttempt = 0;
  unsigned long _backoff = 1000;
  bool _mdnsStarted = false;

  void startAP();
  void connectSTA();
  void stopMDNS();
};
