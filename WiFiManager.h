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
  bool timeSynced() const;
  String ipString() const;
  String apIpString() const;
  IPAddress apAddress() const;
  int32_t rssi() const;

  // Starts a non-blocking scan. Returns false if the scan could not start.
  bool startNetworkScan();
  // Returns: 0 = still scanning, 1 = complete (JSON array filled), -1 = failed.
  int collectNetworkScan(String& json);

private:
  ConfigManager* _cfg = nullptr;
  unsigned long _lastAttempt = 0;
  unsigned long _backoff = 1000;
  bool _mdnsStarted = false;
  bool _ntpStarted = false;
  bool _connectPending = false;

  void startAP();
  void connectSTA();
  void stopMDNS();
};
