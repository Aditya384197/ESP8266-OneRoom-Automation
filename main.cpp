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

static constexpr uint8_t FACTORY_RESET_PIN = 13; // NodeMCU D7
static constexpr unsigned long FACTORY_HOLD_MS = 10000UL;

ConfigManager configManager;
RelayManager relayManager;
WiFiController wifiController;
AutomationManager automationManager;
WebManager webManager;

static unsigned long factoryPressedAt = 0;
static bool factoryResetDone = false;
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

static void checkFactoryButton() {
  const bool pressed = digitalRead(FACTORY_RESET_PIN) == LOW;
  if (!pressed) {
    factoryPressedAt = 0;
    factoryResetDone = false;
    return;
  }
  if (factoryPressedAt == 0) factoryPressedAt = millis();
  if (!factoryResetDone && (unsigned long)(millis() - factoryPressedAt) >= FACTORY_HOLD_MS) {
    factoryResetDone = true;
    Serial.println(F("Factory reset button held: resetting configuration"));
    relayManager.allOff();
    if (configManager.factoryReset()) {
      delay(250);
      ESP.restart();
    }
    factoryResetDone = false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(20);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.printf("%s %s\n", PROJECT_NAME, PROJECT_VERSION);
  Serial.println(F("ESP8266 smart room controller"));
  Serial.println(F("========================================"));

  pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);

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

  if (!configManager.load()) Serial.println(F("Config load failed; defaults may be in use."));
  if (!configManager.loadState()) Serial.println(F("Relay state load failed; defaults may be in use."));
  if (!configManager.loadAutomation()) Serial.println(F("Automation load failed; defaults may be in use."));

  relayManager.begin(configManager);
  automationManager.begin(configManager, relayManager);
  wifiController.begin(configManager);
  webManager.begin(configManager, relayManager, wifiController, automationManager);
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
  checkFactoryButton();

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
