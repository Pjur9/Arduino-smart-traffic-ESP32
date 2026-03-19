#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "traffic_controller.h"
#include "constants.h"

class WebServerManager {
public:
  // Pokretanje web servera i Wi-Fi STA konekcije
  void begin(const char* ssid,
             const char* pass,
             TrafficController* controller,
             const char* hostName = "semafor");

  // Pozivaj povremeno iz loop()
  void tick();

  // Pošalji trenutna stanja kroz SSE (event: "telemetry") i logove
  void sendSensorData();

  // Ručno upiši poruku u event-log i kroz SSE kao "log"
  void logEvent(const String& msg);

private:
  /* ====== Infra ====== */
  AsyncWebServer server{80};
  AsyncEventSource events{"/events"};
  TrafficController* trafficController = nullptr;

  String hostname = "semafor";
  unsigned long lastConnCheck = 0;

  // Reconnect timing
  static constexpr unsigned long WIFI_CHECK_PERIOD_MS = 3000;
  static constexpr unsigned long WIFI_RETRY_BACKOFF_MS = 5000;
  unsigned long lastWifiRetry = 0;

  /* ====== Event Log (Ring Buffer) ====== */
  static constexpr size_t LOG_SIZE = 50; // Pamtimo zadnjih 50 linija
  String logBuf[LOG_SIZE];
  size_t logHead = 0;
  size_t logCount = 0;

  /* ====== Pomoćne ====== */
  bool connectSTA(const char* ssid, const char* pass);
  void setupRoutes();
  void tryStartMDNS();

  String buildTelemetryJson() const;
  
  // Generiše HTML istoriju logova za novi load stranice
  String getLogHistoryHTML() const;
  
  // Glavni HTML handler
  String handleRoot(); 
};