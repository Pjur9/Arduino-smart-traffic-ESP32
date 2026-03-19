#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_BME280.h>
#include <Adafruit_INA219.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include "constants.h"
#include "ultra_sonic.h"

struct SystemState {
  String mode;
  String light;
  String ramp;
  float  busVoltage; 
  float  current_mA; 
  float  power_mW;
  uint32_t vehicles;
  float  temperature;
  float  humidity;
  float  pressure;
  int    ldr;
  #if ULTRASONIC_ENABLED
  uint16_t ultra_mm;
  uint8_t  congestion;
  #endif
};

enum SemState { INIT, GREEN, YELLOW, RED, RED_YELLOW, NIGHT_BLINK, EMERGENCY };

class TrafficController {
public:
  // *** NOVO: Javna varijabla za čuvanje IP adrese ***
  String currentIP = "Cekam WiFi..."; 
  // *************************************************

  void begin();
  void update();

  // API
  void forceRamp(bool open);
  void setEmergency(bool on);
  SystemState getCurrentStateData() const;
  
  // Event komunikacija
  void pushEvent(const String& msg);
  String takeLastEvent();

private:
  LiquidCrystal_I2C lcd{LCD_ADDR, LCD_COLS, LCD_ROWS};
  Adafruit_BME280   bme;
  Adafruit_INA219   ina219{INA219_ADDR};
  MFRC522           rfid{RFID_SS_PIN, RFID_RST_PIN};
  Servo             servo;

#if ULTRASONIC_ENABLED
  UltrasonicNB _ultra{ULTRASONIC_TRIG_PIN, ULTRASONIC_ECHO_PIN, ULTRASONIC_EMA_ALPHA};
  uint16_t _ultraLastMm = 0;
  uint8_t  _ultraCongestion = 0;
#endif

  SemState state = INIT;
  bool isNight = false;
  bool emergencyActive = false;
  
  // *** NOVO: Tajmer za početni ekran ***
  unsigned long startupTimer = 0;
  bool startupDone = false;
  // *************************************

  bool isRampOpen = false;
  bool isRampMoving = false;
  unsigned long rampMoveTimer = 0;

  unsigned long stateTimer = 0;
  unsigned long lcdTimer = 0;

  bool motionDetected = false;
  int ldrRaw = 0;
  float t = NAN, h = NAN, p = NAN;

  String prevLine0, prevLine1;
  volatile uint32_t vehicleCount = 0;
  bool tcrtPrevActive = false;
  unsigned long tcrtLastEdgeMs = 0;

  // Buzzer & Proximity
  bool buzzerToneOn = false;
  unsigned long buzzerTimerMs = 0;
  unsigned long buzzerIntervalMs = 0;
  unsigned long lastProxEvtMs = 0;

  String lastEvent; // Ovde čuvamo poslednji log

  void changeState(SemState next);
  void readSensors();
  void checkRFID();
  void updateOutputs();
  void updateLcd();
  void setLight(bool r, bool y, bool g);
  void beep(uint16_t ms);
  void buzzerProximityLoop();
  void controlRamp(bool open);
  void rampStop();
};