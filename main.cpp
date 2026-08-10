#include <Arduino.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>

#include "ProjectVersion.h"
#include "ConfigManager.h"
#include "RelayManager.h"
#include "WiFiManager.h"
#include "WebManager.h"

static constexpr uint8_t FACTORY_RESET_PIN = 13; // NodeMCU D7
static constexpr unsigned long FACTORY_HOLD_MS = 10000UL;

ConfigManager configManager;
RelayManager relayManager;
WiFiController wifiController;
WebManager webManager;

static unsigned long factoryPressedAt = 0;
static bool factoryResetDone = false;
static unsigned long lastDiag = 0;

static void setupOTA() {
  ArduinoOTA.setHostname(configManager.config().deviceName);
  ArduinoOTA.setPasswordHash(configManager.config().otaMd5);

  ArduinoOTA.onStart([]() {
    Serial.println(F("OTA: start"));
  });

  ArduinoOTA.onEnd([]() {
    Serial.println(F("OTA: complete"));
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned long last = 0;
    const unsigned long now = millis();
    if (now - last >= 1000UL) {
      last = now;
      const unsigned int percent = total ? (progress * 100U) / total : 0U;
      Serial.printf("OTA: %u%%\n", percent);
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA error: %u\n", error);
  });

  ArduinoOTA.begin();
}

static void checkFactoryButton() {
  const bool pressed = digitalRead(FACTORY_RESET_PIN) == LOW;

  if (!pressed) {
    factoryPressedAt = 0;
    factoryResetDone = false;
    return;
  }

  if (factoryPressedAt == 0) {
    factoryPressedAt = millis();
  }

  if (!factoryResetDone &&
      (unsigned long)(millis() - factoryPressedAt) >= FACTORY_HOLD_MS) {
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
  Serial.println(F("ESP8266 production-oriented controller"));
  Serial.println(F("========================================"));

  pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);

  if (!LittleFS.begin()) {
    Serial.println(F("LittleFS mount failed; formatting filesystem for recovery..."));
    LittleFS.format();

    if (!LittleFS.begin()) {
      Serial.println(F("LittleFS recovery failed; continuing with defaults."));
    }
  }

  if (!configManager.begin()) {
    Serial.println(F("Config filesystem unavailable."));
  }

  if (!configManager.load()) {
    Serial.println(F("Config load failed; defaults may be in use."));
  }

  if (!configManager.loadState()) {
    Serial.println(F("Relay state load failed; defaults may be in use."));
  }

  relayManager.begin(configManager);
  wifiController.begin(configManager);
  webManager.begin(configManager, relayManager, wifiController);

  // OTA over the network is enabled only after configuration is loaded.
  setupOTA();

  Serial.printf("AP: %s / %s\n",
                configManager.config().apSsid,
                wifiController.apIpString().c_str());

  Serial.printf("STA: %s\n",
                wifiController.staConnected()
                  ? wifiController.ipString().c_str()
                  : "waiting");

  Serial.println(F("OTA password: configured (not displayed by UI)"));
}

void loop() {
  // Keep each subsystem's loop short and non-blocking.
  wifiController.loop();
  ArduinoOTA.handle();
  webManager.loop();
  checkFactoryButton();

  const unsigned long now = millis();

  if ((unsigned long)(now - lastDiag) >= 60000UL) {
    lastDiag = now;

    Serial.printf(
      "Uptime=%lus Heap=%u WiFi=%s RSSI=%d\n",
      millis() / 1000UL,
      ESP.getFreeHeap(),
      wifiController.staConnected() ? "OK" : "OFFLINE",
      wifiController.rssi()
    );
  }

  yield();
}
