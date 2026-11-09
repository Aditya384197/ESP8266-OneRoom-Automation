#include <Arduino.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>

#include "ProjectVersion.h"
#include "ConfigManager.h"
#include "RelayManager.h"
#include "WiFiManager.h"
#include "WebManager.h"
#include "AutomationManager.h"
#include "FirebaseManager.h"

ConfigManager configManager;
RelayManager relayManager;
WiFiController wifiController;
AutomationManager automationManager;
FirebaseManager firebaseManager;
WebManager webManager;

static unsigned long lastDiag = 0;

static void setupOTA() {
  ArduinoOTA.setHostname(configManager.config().deviceName);
  if (strlen(configManager.config().otaMd5) != 32) {
    Serial.println(F("ArduinoOTA disabled: create an OTA password in Settings first."));
    return;
  }
  ArduinoOTA.setPasswordHash(configManager.config().otaMd5);
  ArduinoOTA.onStart([]() { Serial.println(F("OTA: start")); });
  ArduinoOTA.onEnd([]() { Serial.println(F("OTA: complete")); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned long last = 0;
    const unsigned long now = millis();
    if (now - last >= 1000UL) {
      last = now;
      const unsigned int percent = total ? (progress * 100U) / total : 0U;
      Serial.printf("OTA: %u%%\n", percent);
    }
  });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA error: %u\n", error); });
  ArduinoOTA.begin();
}

void setup() {
  Serial.begin(115200);
  delay(20);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.printf("%s %s\n", PROJECT_NAME, PROJECT_VERSION);
  Serial.println(F("ESP8266 smart room controller"));
  Serial.println(F("========================================"));


  // Mount exactly once. Formatting is only a recovery path after mount failure.
  if (!configManager.begin()) {
    Serial.println(F("LittleFS mount failed; formatting filesystem for recovery..."));
    LittleFS.format();
    if (!configManager.begin()) {
      Serial.println(F("LittleFS recovery failed; restarting in 5 seconds."));
      delay(5000);
      ESP.restart();
    }
  }

  // Restore settings saved in the EEPROM sector if the previous operation was
  // a LittleFS OTA update. This runs before loading any configuration records.
  if (!configManager.restoreFilesystemBackup()) {
    Serial.println(F("WARNING: LittleFS OTA settings backup failed validation; defaults/recovery may be required."));
  }

  if (!configManager.load()) Serial.println(F("Config load failed; defaults may be in use."));
  if (!configManager.loadState()) Serial.println(F("Relay state load failed; defaults may be in use."));
  if (!configManager.loadAutomation()) Serial.println(F("Automation load failed; defaults may be in use."));
  if (!configManager.loadFirebase()) Serial.println(F("Firebase config load failed; cloud access disabled."));

  relayManager.begin(configManager);
  automationManager.begin(configManager, relayManager);
  wifiController.begin(configManager);
  firebaseManager.begin(configManager, relayManager, wifiController, automationManager);
  webManager.begin(configManager, relayManager, wifiController, automationManager, firebaseManager);
  setupOTA();

  Serial.printf("AP: %s / %s\n", configManager.config().apSsid, wifiController.apIpString().c_str());
  Serial.printf("STA: %s\n", wifiController.staConnected() ? wifiController.ipString().c_str() : "waiting");
  Serial.printf("OTA: %s\n", strlen(configManager.config().otaMd5) == 32
    ? "enabled (password configured)"
    : "disabled (create a password in Settings)");
}

void loop() {
  wifiController.loop();
  ArduinoOTA.handle();
  webManager.loop();
  automationManager.loop(wifiController.timeSynced());
  firebaseManager.loop();

  const unsigned long now = millis();
  if ((unsigned long)(now - lastDiag) >= 60000UL) {
    lastDiag = now;
    Serial.printf("Uptime=%lus Heap=%u WiFi=%s RSSI=%d Time=%s\n",
      millis() / 1000UL, ESP.getFreeHeap(),
      wifiController.staConnected() ? "OK" : "OFFLINE",
      wifiController.rssi(), wifiController.timeSynced() ? "SYNCED" : "UNSYNCED");
  }
  yield();
}
