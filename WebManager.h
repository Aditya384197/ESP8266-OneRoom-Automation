#pragma once
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <Updater.h>
#include "ConfigManager.h"
#include "RelayManager.h"
#include "WiFiManager.h"

class WebManager {
public:
  void begin(ConfigManager& cfg, RelayManager& relays, WiFiController& wifi);
  void loop();
  bool otaInProgress() const { return _otaInProgress; }

private:
  ESP8266WebServer _server{80};
  DNSServer _dnsServer;
  ConfigManager* _cfg = nullptr;
  RelayManager* _relays = nullptr;
  WiFiController* _wifi = nullptr;
  bool _dnsStarted = false;
  bool _otaInProgress = false;
  bool _otaRejected = false;
  bool _otaReady = false;
  unsigned long _restartAt = 0;

  void setupRoutes();
  void startCaptiveDNS();
  void handleCaptivePortal();
  void handleState();
  void handleSettingsGet();
  void handleSettingsSave();
  void handleRelay();
  void handleWifiScan();
  void handleOtaPassword();
  void handleOtaUploadDone();
  void handleOtaUpload();
  void handleFactoryReset();
  void handleReboot();
  void sendFile(const char* path, const char* contentType);

  bool validOtaPassword(const String& password) const;
  String md5Of(const String& value) const;
  bool validText(const String& value, size_t maxLen, bool allowEmpty) const;
  void jsonError(int code, const char* message);
  void scheduleRestart(unsigned long delayMs = 500);
};
