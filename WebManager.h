#pragma once

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <Updater.h>
#include <ArduinoJson.h>
#include <functional>

#include "ConfigManager.h"
#include "RelayManager.h"
#include "WiFiManager.h"
#include "AutomationManager.h"
#include "FirebaseManager.h"

class WebManager {
public:
  void begin(ConfigManager& cfg, RelayManager& relays,
             WiFiController& wifi, AutomationManager& automation,
             FirebaseManager& firebase);
  void loop();
  bool otaInProgress() const { return _otaInProgress; }

private:
  using JsonRouteHandler = std::function<void(AsyncWebServerRequest*, JsonDocument&)>;

  AsyncWebServer _server{80};
  DNSServer _dnsServer;
  ConfigManager* _cfg = nullptr;
  RelayManager* _relays = nullptr;
  WiFiController* _wifi = nullptr;
  AutomationManager* _automation = nullptr;
  FirebaseManager* _firebase = nullptr;
  bool _dnsStarted = false;
  bool _otaInProgress = false;
  bool _otaRejected = false;
  bool _otaReady = false;
  bool _otaFilesystem = false;
  AsyncWebServerRequest* _otaOwner = nullptr;
  unsigned long _restartAt = 0;

  void setupRoutes();
  void onJson(const char* uri, WebRequestMethodComposite method, JsonRouteHandler handler);
  void startCaptiveDNS();
  void handleCaptivePortal(AsyncWebServerRequest* request);
  void handleState(AsyncWebServerRequest* request);
  void handleSettingsGet(AsyncWebServerRequest* request);
  void handleSettingsSave(AsyncWebServerRequest* request, JsonDocument& doc);
  void handleRelayConfigSave(AsyncWebServerRequest* request, JsonDocument& doc);
  void handleFirebaseGet(AsyncWebServerRequest* request);
  void handleFirebaseSave(AsyncWebServerRequest* request, JsonDocument& doc);
  void handleFirebaseDelete(AsyncWebServerRequest* request);
  void handleLogoSave(AsyncWebServerRequest* request, JsonDocument& doc);
  void handleAutomationGet(AsyncWebServerRequest* request);
  void handleTimer(AsyncWebServerRequest* request, JsonDocument& doc);
  void handleSchedule(AsyncWebServerRequest* request, JsonDocument& doc);
  void handleRelay(AsyncWebServerRequest* request, JsonDocument& doc);
  void handleWifiScan(AsyncWebServerRequest* request);
  void handleWifiScanResult(AsyncWebServerRequest* request);
  void handleOtaPassword(AsyncWebServerRequest* request, JsonDocument& doc);
  void handleOtaUploadDone(AsyncWebServerRequest* request);
  void handleOtaUpload(AsyncWebServerRequest* request, const String& filename,
                       size_t index, uint8_t* data, size_t len, bool final);
  void handleFactoryReset(AsyncWebServerRequest* request);
  void handleReboot(AsyncWebServerRequest* request);
  void sendFile(AsyncWebServerRequest* request, const char* path, const char* contentType);

  bool validOtaPassword(const String& password) const;
  String md5Of(const String& value) const;
  bool validText(const String& value, size_t maxLen, bool allowEmpty) const;
  void jsonError(AsyncWebServerRequest* request, int code, const char* message);
  void scheduleRestart(unsigned long delayMs = 500);
};
