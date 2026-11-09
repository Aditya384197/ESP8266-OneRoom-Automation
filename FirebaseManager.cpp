#include "FirebaseManager.h"

#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "FirebaseCerts.h"

static constexpr unsigned long FIREBASE_POLL_INTERVAL_MS = 6000UL;
static constexpr unsigned long FIREBASE_MIN_BACKOFF_MS = 5000UL;
static constexpr unsigned long FIREBASE_MAX_BACKOFF_MS = 60000UL;
static constexpr size_t FIREBASE_MAX_RESPONSE = 4096U;
static constexpr size_t FIREBASE_MAX_REQUEST = 2048U;
static constexpr size_t FIREBASE_MAX_URL = 1800U;
static constexpr uint32_t FIREBASE_TOKEN_REFRESH_MARGIN_SEC = 180U;

static BearSSL::X509List gFirebaseTrust(FIREBASE_ROOT_CA_BUNDLE);

void FirebaseManager::begin(ConfigManager& cfg, RelayManager& relays,
                            WiFiController& wifi, AutomationManager& automation) {
  _cfg = &cfg;
  _relays = &relays;
  _wifi = &wifi;
  _automation = &automation;
  _idToken = "";
  _tokenExpiresAt = 0;
  _nextActionAt = millis() + 5000UL;
  _lastSuccessAt = 0;
  _backoffMs = FIREBASE_MIN_BACKOFF_MS;
  _ready = cfg.firebase().enabled && cfg.firebase().refreshToken[0] != '\0';
  _status = _ready ? "Waiting for Wi-Fi and time sync" : "Not configured";
  _lastError = "";
}

bool FirebaseManager::validDatabaseUrl(const String& value) const {
  if (!value.startsWith("https://") || value.length() < 16 || value.length() > 120) return false;
  if (value.indexOf('/', 8) >= 0 || value.indexOf('?') >= 0 || value.indexOf('#') >= 0 || value.indexOf('@') >= 0) return false;
  const String host = value.substring(8);
  if (host.length() < 4 || host.indexOf('.') < 1 || host.endsWith(".")) return false;
  for (size_t i = 0; i < host.length(); ++i) {
    const char c = host[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
  }
  return true;
}

bool FirebaseManager::validDeviceId(const String& value) const {
  if (value.length() == 0 || value.length() > 39) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return true;
}

String FirebaseManager::authUrlEncoded(const String& value) const {
  static const char hex[] = "0123456789ABCDEF";
  String out;
  out.reserve(value.length() * 3U);
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = (uint8_t)value[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += (char)c;
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}

bool FirebaseManager::request(const String& method, const String& url,
                              const String& body, String& response,
                              int& statusCode, String& error) const {
  response = "";
  statusCode = 0;
  if (url.length() > FIREBASE_MAX_URL || body.length() > FIREBASE_MAX_REQUEST) {
    error = "Firebase request exceeded its configured size limit.";
    return false;
  }

  BearSSL::WiFiClientSecure client;
  client.setTrustAnchors(&gFirebaseTrust); // Verified TLS; never setInsecure().
  client.setBufferSizes(512, 512);
  client.setTimeout(2500);

  HTTPClient http;
  http.setTimeout(2500);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    error = "Could not start verified HTTPS connection to Firebase.";
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");
  statusCode = http.sendRequest(method.c_str(), body.length() ? body : String());
  if (statusCode <= 0) {
    error = String("Firebase HTTPS transport error: ") + http.errorToString(statusCode);
    http.end();
    return false;
  }
  const int contentLength = http.getSize();
  if (contentLength > (int)FIREBASE_MAX_RESPONSE) {
    error = "Firebase response exceeded the configured size limit.";
    http.end();
    return false;
  }
  response = http.getString();
  http.end();
  if (response.length() > FIREBASE_MAX_RESPONSE) {
    response = "";
    error = "Firebase response exceeded the configured size limit.";
    return false;
  }
  return true;
}

bool FirebaseManager::signIn(const FirebaseConfig& config, const String& password,
                             String& idToken, String& refresh,
                             String& uid, uint32_t& expiresInSec, String& error) {
  JsonDocument payload;
  payload["email"] = config.deviceEmail;
  payload["password"] = password;
  payload["returnSecureToken"] = true;
  String body;
  serializeJson(payload, body);

  const String url = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") +
                     authUrlEncoded(String(config.apiKey));
  String response;
  int code = 0;
  if (!request("POST", url, body, response, code, error)) return false;
  JsonDocument result;
  const DeserializationError parseError = deserializeJson(result, response);
  if (parseError) { error = "Firebase Authentication returned invalid JSON."; return false; }
  if (code != HTTP_CODE_OK) {
    error = result["error"]["message"] | "Firebase sign-in failed. Check the device account and API key.";
    return false;
  }
  idToken = result["idToken"] | "";
  refresh = result["refreshToken"] | "";
  uid = result["localId"] | "";
  const String expiresText = result["expiresIn"] | "0";
  expiresInSec = (uint32_t)strtoul(expiresText.c_str(), nullptr, 10);
  if (idToken.length() < 100 || refresh.length() < 100 || uid.length() == 0 ||
      expiresInSec < 300 || refresh.length() >= sizeof(config.refreshToken)) {
    error = "Firebase Authentication response did not contain usable device credentials.";
    return false;
  }
  return true;
}

String FirebaseManager::databasePath(const char* path, bool authenticated) const {
  if (!_cfg) return String();
  String base = _cfg->firebase().databaseUrl;
  while (base.endsWith("/")) base.remove(base.length() - 1);
  String url = base + path;
  if (authenticated) {
    url += "?auth=";
    url += authUrlEncoded(_idToken);
  }
  return url;
}

bool FirebaseManager::verifyDeviceAccount(const FirebaseConfig& config,
                                          const String& token, const String& uid,
                                          String& error) {
  String base = config.databaseUrl;
  while (base.endsWith("/")) base.remove(base.length() - 1);
  const String url = base + "/deviceAccounts/" + uid + ".json?auth=" + authUrlEncoded(token);
  String response;
  int code = 0;
  if (!request("GET", url, "", response, code, error)) return false;
  if (code != HTTP_CODE_OK) {
    error = "Firebase denied device-account verification. Check the Realtime Database rules and deviceAccounts UID record.";
    return false;
  }
  JsonDocument result;
  if (deserializeJson(result, response)) {
    error = "Could not parse the Firebase device-account record.";
    return false;
  }
  const String registeredDevice = result["deviceId"] | "";
  const bool enabled = result["enabled"] | false;
  if (!enabled || registeredDevice != String(config.deviceId)) {
    error = "This Firebase account is not enabled for the configured device ID.";
    return false;
  }
  return true;
}

bool FirebaseManager::configure(const String& databaseUrl, const String& apiKey,
                                const String& deviceId, const String& email,
                                const String& password, String& error) {
  if (!_cfg || !_wifi || !_wifi->staConnected()) {
    error = "Connect the ESP8266 to home Wi-Fi before configuring Firebase.";
    return false;
  }
  if (!_wifi->timeSynced()) {
    error = "The device clock is not synced yet. Wait for Wi-Fi time sync and retry.";
    return false;
  }
  if (!validDatabaseUrl(databaseUrl)) {
    error = "Database URL must be an HTTPS Firebase Realtime Database hostname only.";
    return false;
  }
  if (apiKey.length() < 20 || apiKey.length() > 45) {
    error = "Firebase Web API key is invalid.";
    return false;
  }
  if (!validDeviceId(deviceId)) {
    error = "Device ID may contain only English letters, numbers, hyphen and underscore.";
    return false;
  }
  if (email.length() < 5 || email.length() > 79 || email.indexOf('@') < 1 ||
      password.length() < 6 || password.length() > 64) {
    error = "Enter the dedicated device-account email and its 6–64 character password.";
    return false;
  }

  FirebaseConfig candidate{};
  candidate.magic = 0x4F524146UL;
  candidate.version = 1;
  candidate.enabled = 1;
  strlcpy(candidate.databaseUrl, databaseUrl.c_str(), sizeof(candidate.databaseUrl));
  strlcpy(candidate.apiKey, apiKey.c_str(), sizeof(candidate.apiKey));
  strlcpy(candidate.deviceId, deviceId.c_str(), sizeof(candidate.deviceId));
  strlcpy(candidate.deviceEmail, email.c_str(), sizeof(candidate.deviceEmail));

  String newIdToken, newRefreshToken, uid;
  uint32_t expiresSec = 0;
  if (!signIn(candidate, password, newIdToken, newRefreshToken, uid, expiresSec, error)) return false;
  strlcpy(candidate.deviceUid, uid.c_str(), sizeof(candidate.deviceUid));
  if (!verifyDeviceAccount(candidate, newIdToken, uid, error)) return false;
  strlcpy(candidate.refreshToken, newRefreshToken.c_str(), sizeof(candidate.refreshToken));

  const FirebaseConfig previous = _cfg->firebase();
  _cfg->firebase() = candidate;
  if (!_cfg->saveFirebase()) {
    _cfg->firebase() = previous;
    error = "Firebase login succeeded, but the refresh token could not be saved to LittleFS.";
    return false;
  }

  _idToken = newIdToken;
  _tokenExpiresAt = millis() + (unsigned long)((expiresSec > FIREBASE_TOKEN_REFRESH_MARGIN_SEC)
    ? (expiresSec - FIREBASE_TOKEN_REFRESH_MARGIN_SEC) : 60U) * 1000UL;
  _ready = true;
  _status = "Connected and device account verified";
  _lastError = "";
  _lastSuccessAt = millis();
  _nextActionAt = millis() + FIREBASE_POLL_INTERVAL_MS;
  _backoffMs = FIREBASE_MIN_BACKOFF_MS;
  return true;
}

bool FirebaseManager::refreshToken(String& error) {
  if (!_cfg || !_cfg->firebase().enabled || !_cfg->firebase().refreshToken[0]) {
    error = "Firebase device account is not configured.";
    return false;
  }
  const FirebaseConfig& config = _cfg->firebase();
  const String refresh = config.refreshToken;
  const String url = String("https://securetoken.googleapis.com/v1/token?key=") + authUrlEncoded(String(config.apiKey));
  const String body = String("grant_type=refresh_token&refresh_token=") + authUrlEncoded(refresh);

  // Token endpoint expects form-encoded data instead of JSON.
  BearSSL::WiFiClientSecure client;
  client.setTrustAnchors(&gFirebaseTrust);
  client.setBufferSizes(512, 512);
  client.setTimeout(2500);
  HTTPClient http;
  http.setTimeout(2500);
  http.setReuse(false);
  if (!http.begin(client, url)) { error = "Could not start the Firebase token refresh connection."; return false; }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.addHeader("Connection", "close");
  const int code = http.POST(body);
  if (code <= 0) {
    error = String("Firebase token refresh transport error: ") + http.errorToString(code);
    http.end();
    return false;
  }
  const int contentLength = http.getSize();
  if (contentLength > (int)FIREBASE_MAX_RESPONSE) {
    error = "Firebase token response exceeded the configured size limit.";
    http.end();
    return false;
  }
  const String response = http.getString();
  http.end();
  if (response.length() > FIREBASE_MAX_RESPONSE) { error = "Firebase token response is too large."; return false; }

  JsonDocument result;
  if (deserializeJson(result, response)) { error = "Firebase token endpoint returned invalid JSON."; return false; }
  if (code != HTTP_CODE_OK) {
    error = result["error"]["message"] | "Firebase refresh token was rejected.";
    return false;
  }
  const String token = result["id_token"] | "";
  const String newRefresh = result["refresh_token"] | "";
  const String uid = result["user_id"] | "";
  const String expiresText = result["expires_in"] | "0";
  const uint32_t expiresSec = (uint32_t)strtoul(expiresText.c_str(), nullptr, 10);
  if (token.length() < 100 || expiresSec < 300 || uid != String(config.deviceUid)) {
    error = "Firebase refreshed a token for an unexpected or invalid device account.";
    return false;
  }
  if (newRefresh.length() >= 100 && newRefresh != refresh) {
    const FirebaseConfig previous = _cfg->firebase();
    strlcpy(_cfg->firebase().refreshToken, newRefresh.c_str(), sizeof(_cfg->firebase().refreshToken));
    if (!_cfg->saveFirebase()) {
      _cfg->firebase() = previous;
      error = "Firebase token refreshed, but the rotated refresh token could not be saved.";
      return false;
    }
  }
  _idToken = token;
  _tokenExpiresAt = millis() + (unsigned long)((expiresSec > FIREBASE_TOKEN_REFRESH_MARGIN_SEC)
    ? (expiresSec - FIREBASE_TOKEN_REFRESH_MARGIN_SEC) : 60U) * 1000UL;
  return true;
}

bool FirebaseManager::pollAndPublish(String& error) {
  if (!_cfg || !_relays || !_automation) return false;
  const String id = _cfg->firebase().deviceId;
  const String url = databasePath((String("/devices/") + id + ".json").c_str());
  String response;
  int code = 0;
  if (!request("GET", url, "", response, code, error)) return false;
  if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN) {
    if (!refreshToken(error)) return false;
    const String retryUrl = databasePath((String("/devices/") + id + ".json").c_str());
    if (!request("GET", retryUrl, "", response, code, error)) return false;
  }
  if (code != HTTP_CODE_OK) {
    JsonDocument err;
    if (!deserializeJson(err, response)) error = err["error"] | "Realtime Database read was rejected.";
    else error = String("Realtime Database read failed with HTTP ") + code;
    return false;
  }

  JsonDocument remote;
  if (deserializeJson(remote, response)) { error = "Realtime Database returned invalid device JSON."; return false; }

  JsonDocument patch;
  JsonObject ackOut = patch["commandAck"].to<JsonObject>();
  bool ackChanged = false;
  JsonVariant commands = remote["commands"];
  JsonVariant ack = remote["commandAck"];
  for (uint8_t i = 0; i < 4; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "relay%u", (unsigned)(i + 1));
    JsonVariantConst command = commands[key];
    if (!command.is<JsonObject>()) continue;
    const String commandId = command["id"] | "";
    if (commandId.length() == 0 || commandId.length() > 64) continue;
    const String acknowledged = ack[key] | "";
    if (commandId == acknowledged) continue;
    if (!command["value"].is<bool>()) continue;

    const bool desired = command["value"].as<bool>();
    // Physical maintained switch is authoritative. The command is still
    // acknowledged so an old cloud command is not retried after the switch.
    if (!_automation->physicalPriorityRelay(i)) {
      if (!_automation->cancelTimer(i)) {
        error = String("Could not cancel the active timer for ") + key;
        return false;
      }
      if (!_relays->set(i, desired, true)) {
        error = String("Could not persist remote state for ") + key;
        return false;
      }
    }
    ackOut[key] = commandId;
    ackChanged = true;
  }
  if (!ackChanged) patch.remove("commandAck");

  JsonObject state = patch["state"].to<JsonObject>();
  for (uint8_t i = 0; i < 4; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "relay%u", (unsigned)(i + 1));
    state[key] = _relays->get(i);
  }
  int physicalSwitchRelay = -1;
  for (uint8_t i = 0; i < 4; ++i) {
    if (_automation->physicalPriorityRelay(i)) { physicalSwitchRelay = i; break; }
  }
  state["physicalSwitchRelay"] = physicalSwitchRelay;
  state["online"] = true;
  state["lastSeen"] = (uint32_t)time(nullptr);
  state["uptimeSec"] = millis() / 1000UL;
  state["rssi"] = _wifi->rssi();
  state["freeHeap"] = ESP.getFreeHeap();

  String body;
  serializeJson(patch, body);
  const String patchUrl = databasePath((String("/devices/") + id + ".json").c_str());
  if (!request("PATCH", patchUrl, body, response, code, error)) return false;
  if (code == HTTP_CODE_UNAUTHORIZED || code == HTTP_CODE_FORBIDDEN) {
    if (!refreshToken(error)) return false;
    const String retryUrl = databasePath((String("/devices/") + id + ".json").c_str());
    if (!request("PATCH", retryUrl, body, response, code, error)) return false;
  }
  if (code < 200 || code >= 300) {
    JsonDocument err;
    if (!deserializeJson(err, response)) error = err["error"] | "Realtime Database rejected the state update.";
    else error = String("Realtime Database update failed with HTTP ") + code;
    return false;
  }
  return true;
}

void FirebaseManager::fail(const String& error) {
  _lastError = error;
  _status = String("Retrying: ") + error;
  _nextActionAt = millis() + _backoffMs;
  _backoffMs = (_backoffMs >= FIREBASE_MAX_BACKOFF_MS / 2UL)
    ? FIREBASE_MAX_BACKOFF_MS : _backoffMs * 2UL;
}

void FirebaseManager::loop() {
  if (!_cfg || !_cfg->firebase().enabled || !_cfg->firebase().refreshToken[0]) return;
  if (!_wifi || !_wifi->staConnected()) {
    _status = "Waiting for home Wi-Fi";
    return;
  }
  if (!_wifi->timeSynced()) {
    _status = "Waiting for secure clock sync";
    return;
  }
  const unsigned long now = millis();
  if ((long)(now - _nextActionAt) < 0) return;

  String error;
  if (_idToken.length() == 0 || (long)(now - _tokenExpiresAt) >= 0) {
    _status = "Refreshing Firebase session";
    if (!refreshToken(error)) { fail(error); return; }
  }

  _status = "Syncing device state";
  if (!pollAndPublish(error)) { fail(error); return; }

  _lastSuccessAt = millis();
  _lastError = "";
  _status = "Connected";
  _backoffMs = FIREBASE_MIN_BACKOFF_MS;
  _nextActionAt = millis() + FIREBASE_POLL_INTERVAL_MS;
}

bool FirebaseManager::clearConfiguration() {
  if (!_cfg || !_cfg->clearFirebase()) return false;
  _idToken = "";
  _tokenExpiresAt = 0;
  _ready = false;
  _status = "Not configured";
  _lastError = "";
  _lastSuccessAt = 0;
  return true;
}

void FirebaseManager::getPublicStatus(JsonDocument& doc) const {
  const FirebaseConfig& config = _cfg->firebase();
  doc["enabled"] = config.enabled && config.refreshToken[0] != '\0';
  doc["configured"] = config.enabled && config.refreshToken[0] != '\0';
  doc["databaseUrl"] = config.databaseUrl;
  doc["deviceId"] = config.deviceId;
  doc["deviceEmail"] = config.deviceEmail;
  doc["apiKeySet"] = config.apiKey[0] != '\0';
  doc["status"] = _status;
  doc["lastError"] = _lastError;
  doc["lastSyncAgeSec"] = _lastSuccessAt ? (millis() - _lastSuccessAt) / 1000UL : 0;
}
