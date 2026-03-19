#include <Arduino.h>
#include <WiFi.h>

// *** LEGACY Mobizt biblioteka ***
#include <FirebaseESP32.h>

#include "constants.h"
#include "secrets.h"
#include "traffic_controller.h"
#include "WebServerManager.h"

// Fallback konstante
#ifndef FB_TELEM_PERIOD_MS
#define FB_TELEM_PERIOD_MS 1000
#endif

// ====== GLOBALNI OBJEKTI ======
TrafficController traffic;
WebServerManager  web;

// ====== Firebase (LEGACY) ======
FirebaseData fbdo;      
FirebaseData fbStream;  
FirebaseConfig config;
FirebaseAuth auth;      

String deviceId;
unsigned long lastTelemPush = 0;
unsigned long lastSsePush   = 0;

static String makeDeviceIdFromMac() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "esp32-%04X%04X", (uint16_t)(mac >> 16), (uint16_t)mac);
  return String(buf);
}

static void handleCmdJson(FirebaseJson &json) {
  FirebaseJsonData res;
  
  // Detaljna obrada komandi iz Firebase-a
  if (json.get(res, F("forceRamp"))) {
    if (res.success && res.type == "bool") {
      bool val = res.to<bool>();
      traffic.forceRamp(val);
      web.logEvent(String("FIREBASE CMD: Ramp ") + (val ? "OPEN" : "CLOSE"));
    }
  }

  if (json.get(res, F("desiredMode"))) {
    if (res.success && res.type == "string") {
      String mode = res.to<String>();
      if (mode == "EMERGENCY") {
        traffic.setEmergency(true);
        web.logEvent("FIREBASE CMD: Emergency ON");
      } else if (mode == "NORMAL") {
        traffic.setEmergency(false);
        web.logEvent("FIREBASE CMD: Emergency OFF");
      }
    }
  }
}

static void startStream() {
  String path = "/devices/" + deviceId + "/cmd"; 
  if (!Firebase.beginStream(fbStream, path)) {
    Serial.printf("[FB] Stream Error: %s\n", fbStream.errorReason().c_str());
  } else {
    Serial.println("[FB] Stream started: " + path);
  }
}

static void firebaseInit() {
  deviceId = makeDeviceIdFromMac();
  config.database_url = FB_DB_URL;
  config.api_key      = FB_API_KEY;
  auth.user.email     = FB_USER_EMAIL;
  auth.user.password  = FB_USER_PASS;

  Firebase.reconnectNetwork(true);
  Firebase.begin(&config, &auth);
  fbdo.setBSSLBufferSize(4096, 1024);
  fbStream.setBSSLBufferSize(4096, 1024);

  startStream();
}

static void pushTelemetry() {
  SystemState s = traffic.getCurrentStateData();
  FirebaseJson json;
  json.set("ts/.sv", "timestamp");
  json.set("mode",   s.mode);
  json.set("light",  s.light);
  json.set("ramp",   s.ramp);
  json.set("vehicles", s.vehicles);
  json.set("t",      s.temperature);
  json.set("h",      s.humidity);
  json.set("ultra_mm", (int)s.ultra_mm);
  json.set("cong",     (int)s.congestion);
  json.set("power/voltage", s.busVoltage);
  json.set("power/current", s.current_mA);
  json.set("power/power_mW", s.power_mW);

  // *** NOVO: Dodajemo IP adresu u bazu! ***
  json.set("ip", WiFi.localIP().toString());
  // ****************************************

  String path = "/devices/" + deviceId + "/telemetry";
  Firebase.updateNode(fbdo, path, json);
}

void setup() {
  Serial.begin(115200);
  
  // 1. Pokreni Kontroler
  traffic.begin();

  // 2. Pokreni Web Server
  web.begin(WIFI_SSID, WIFI_PASS, &traffic, "semafor");

  // 3. Pokreni Firebase
  firebaseInit();
}

void loop() {
  traffic.update();
  web.tick();

  unsigned long now = millis();

  // SSE (Web Update) - svake 300ms
  if (now - lastSsePush >= 300) {
    lastSsePush = now;
    web.sendSensorData(); // Ovo sada šalje i LOGOVE
  }

  // Firebase Telemetrija - svake 1s
  if (now - lastTelemPush >= FB_TELEM_PERIOD_MS) {
    lastTelemPush = now;
    pushTelemetry();
  }

  // Firebase Stream
  if (!Firebase.readStream(fbStream)) {
    // Error handling...
  } else if (fbStream.streamAvailable()) {
    if (fbStream.dataTypeEnum() == fb_esp_rtdb_data_type_json) {
      FirebaseJson *json = fbStream.to<FirebaseJson *>();
      handleCmdJson(*json);
    }
  }
}