#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "ConfigManager.h"
#include "RelayManager.h"
#include "WiFiManager.h"
#include "AutomationManager.h"

class FirebaseManager {
public:
  void begin(ConfigManager& cfg, RelayManager& relays,
             WiFiController& wifi, AutomationManager& automation);
  void loop();

  bool configure(const String& databaseUrl, const String& apiKey,
                 const String& deviceId, const String& email,
                 const String& password, String& error);
  bool clearConfiguration();
  void getPublicStatus(JsonDocument& doc) const;

private:
  ConfigManager* _cfg = nullptr;
  RelayManager* _relays = nullptr;
  WiFiController* _wifi = nullptr;
  AutomationManager* _automation = nullptr;
  String _idToken;
  unsigned long _tokenExpiresAt = 0;
  unsigned long _nextActionAt = 0;
  unsigned long _lastSuccessAt = 0;
  unsigned long _backoffMs = 5000;
  bool _ready = false;
  String _status = "Not configured";
  String _lastError;

  bool signIn(const FirebaseConfig& config, const String& password,
              String& idToken, String& refreshToken, String& uid,
              uint32_t& expiresInSec, String& error);
  bool refreshToken(String& error);
  bool verifyDeviceAccount(const FirebaseConfig& config,
                           const String& token, const String& uid,
                           String& error);
  bool pollAndPublish(String& error);
  bool request(const String& method, const String& url, const String& body,
               String& response, int& statusCode, String& error) const;
  String databasePath(const char* path, bool authenticated = true) const;
  String authUrlEncoded(const String& value) const;
  bool validDatabaseUrl(const String& value) const;
  bool validDeviceId(const String& value) const;
  void fail(const String& error);
};
