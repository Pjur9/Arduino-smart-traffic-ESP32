#include <Arduino.h>
#include "traffic_controller.h"
#include "constants.h"

// --- KONFIGURACIJA ---
#define DEBUG_LOGGING true 

// Defaults
#ifndef SERVO_PULSE_MIN_US
#define SERVO_PULSE_MIN_US    500
#endif
#ifndef SERVO_PULSE_MAX_US
#define SERVO_PULSE_MAX_US    2500
#endif
#ifndef SERVO_OPEN_DEG
#define SERVO_OPEN_DEG        90
#endif
#ifndef SERVO_CLOSED_DEG
#define SERVO_CLOSED_DEG      0
#endif
#ifndef RAMP_MOVE_DURATION_MS
#define RAMP_MOVE_DURATION_MS 700
#endif

// Helpers
static inline void ledWrite(uint8_t pin, bool on) {
#if LED_ACTIVE_LOW
  digitalWrite(pin, on ? LOW : HIGH);
#else
  digitalWrite(pin, on ? HIGH : LOW);
#endif
}

#if RELAY_ENABLED
static inline void relayWrite(uint8_t pin, bool on) {
  pinMode(pin, OUTPUT);
#if RELAY_ACTIVE_LOW
  digitalWrite(pin, on ? LOW : HIGH);
#else
  digitalWrite(pin, on ? HIGH : LOW);
#endif
}
#endif

// Implementacija klase

void TrafficController::begin() {
  // PINOVI
  pinMode(LED_NS_RED_PIN, OUTPUT);
  pinMode(LED_NS_YELLOW_PIN, OUTPUT);
  pinMode(LED_NS_GREEN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TCRT_DO_PIN, INPUT);

  // LCD Init
  Wire.begin();
  lcd.init();
  lcd.backlight();
  
  // *** STARTUP ***
  startupTimer = millis(); 
  lcd.setCursor(0,0); lcd.print(F("Semafor Boot..."));
  // ***************
  
  // BME Init
  if (!bme.begin(BME280_ADDR)) {
    Serial.println(F("BME280 not found"));
  }

  // INA219 Init
  if (!ina219.begin()) {
    Serial.println(F("INA219 not found!"));
  } else {
    ina219.setCalibration_32V_2A(); 
    Serial.println(F("INA219 OK"));
  }

  // RFID Init
  SPI.begin();
  rfid.PCD_Init();

#if ULTRASONIC_ENABLED
  _ultra.begin(ULTRASONIC_PERIOD_MS);
#endif

  // Init vars
  vehicleCount = 0;
  buzzerToneOn = false;

  // RELEJI - Inicijalizacija
#if RELAY_ENABLED
  relayWrite(RELAY_SERVO_ULTRA_PIN, true);
  relayWrite(RELAY_RFID_PIN, true);
  relayWrite(RELAY_LCD_PIN, true); // Početno stanje: LCD UPALJEN
#endif

  // Servo Start
  servo.attach(SERVO_PIN, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
  servo.write(SERVO_CLOSED_DEG);
  delay(300);
  servo.detach();

  changeState(RED); 
  pushEvent(F("SYSTEM STARTUP COMPLETE"));
}

void TrafficController::update() {
  readSensors();
  checkRFID();
  buzzerProximityLoop();
  updateOutputs();
  updateLcd();
}

void TrafficController::readSensors() {
  const unsigned long now = millis();
  
  // --- LDR LOGIKA ---
  ldrRaw = analogRead(LDR_PIN);
  if (isNight) {
    if (ldrRaw < LDR_THR_BECOME_DAY) {
      isNight = false;
      if (DEBUG_LOGGING) pushEvent(F("Senzor: Preslo u DAN"));
    }
  } else {
    if (ldrRaw > LDR_THR_BECOME_NIGHT) {
      isNight = true;
      if (DEBUG_LOGGING) pushEvent(F("Senzor: Preslo u NOC"));
    }
  }
  
  t = bme.readTemperature();
  h = bme.readHumidity();
  p = bme.readPressure() / 100.0f;

#if ULTRASONIC_ENABLED
  _ultra.update();
  if (_ultra.hasValid()) {
    _ultraLastMm = _ultra.distanceMm();
    _ultraCongestion = _ultra.congestionIndex(ULTRASONIC_NEAR_MM, ULTRASONIC_FAR_MM);
  }
#endif

  // TCRT Vehicle Detection
  const bool tcrtActive = (digitalRead(TCRT_DO_PIN) == (TCRT_ACTIVE_HIGH ? HIGH : LOW));
  if (tcrtActive && !tcrtPrevActive) {
    if (now - tcrtLastEdgeMs >= TCRT_COOLDOWN_MS) {
      vehicleCount++;
      tcrtLastEdgeMs = now;
      if (DEBUG_LOGGING) pushEvent("VEHICLE DETECTED");
      if (state == GREEN && !emergencyActive) controlRamp(true);
    }
  }
  tcrtPrevActive = tcrtActive;
}

void TrafficController::checkRFID() {
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  bool authorized = (rfid.uid.size == AUTH_UID_LEN);
  if (authorized) {
    for (byte i = 0; i < AUTH_UID_LEN; i++) {
      if (rfid.uid.uidByte[i] != AUTHORIZED_RFID_UID[i]) { authorized = false; break; }
    }
  }

  String uidStr = "";
  if (DEBUG_LOGGING || !authorized) {
    for (byte i = 0; i < rfid.uid.size; i++) {
      if(i>0) uidStr += ":";
      uidStr += String(rfid.uid.uidByte[i], HEX);
    }
    uidStr.toUpperCase();
  }

  if (authorized) {
    if (!emergencyActive) {
      emergencyActive = true;
      controlRamp(true);
      changeState(EMERGENCY);
      beep(200);
      if (DEBUG_LOGGING) pushEvent("RFID AUTH OK -> EMERGENCY");
    }
  } else {
    if (DEBUG_LOGGING) pushEvent("RFID DENIED");
    beep(500); 
  }
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void TrafficController::updateOutputs() {
  const unsigned long now = millis();

  #if RELAY_ENABLED
  bool needLcd = (!isNight) || emergencyActive;
  // Ako smo u startup modu, LCD mora biti upaljen!
  if (!startupDone) needLcd = true; 
  relayWrite(RELAY_LCD_PIN, needLcd);
  #endif

  if (emergencyActive) {
#if RELAY_ENABLED
    relayWrite(RELAY_SERVO_ULTRA_PIN, true);
    relayWrite(RELAY_RFID_PIN, true);
#endif
    lcd.backlight();
    bool blink = (now / 250) % 2 == 0;
    setLight(blink, false, false); 
    
    if (now - stateTimer >= DURATION_EMERGENCY_MS) {
      setEmergency(false);
      pushEvent(F("EMERGENCY TIMEOUT"));
    }
    if (isRampMoving && (now - rampMoveTimer >= RAMP_MOVE_DURATION_MS)) rampStop();
  }
  
  if (!emergencyActive) {
      if (isNight) {
#if RELAY_ENABLED
        relayWrite(RELAY_SERVO_ULTRA_PIN, false); 
        relayWrite(RELAY_RFID_PIN, true);
#endif
        lcd.noBacklight();
        
        bool on = ((now / NIGHT_BLINK_INTERVAL_MS) % 2) == 0;
        setLight(false, on, false); 
        if (state != NIGHT_BLINK) changeState(NIGHT_BLINK);
        if (isRampMoving && (now - rampMoveTimer >= RAMP_MOVE_DURATION_MS)) rampStop();
      }
      
      if (!isNight) {
#if RELAY_ENABLED
        relayWrite(RELAY_SERVO_ULTRA_PIN, true);
        relayWrite(RELAY_RFID_PIN, true);
#endif
        lcd.backlight();

        switch (state) {
          case GREEN:
            {
              uint32_t currentDuration = GREEN_DURATION_MS;
              #if ULTRASONIC_ENABLED
              currentDuration += (7000 * _ultraCongestion) / 100;
              #endif
              
              unsigned long elapsed = now - stateTimer;
              bool shouldBlink = false;
              if (_ultraCongestion > 80) shouldBlink = ((now / 250) % 2 == 0); 
              else if (elapsed >= (currentDuration - 3000)) shouldBlink = ((now / 500) % 2 == 0); 
              else shouldBlink = true;

              setLight(false, false, shouldBlink);
              if (elapsed >= currentDuration) changeState(YELLOW);
            }
            break;
          case YELLOW:
            setLight(false, true, false);
            if (now - stateTimer >= YELLOW_DURATION_MS) changeState(RED);
            break;
          case RED:
            setLight(true, false, false);
            if (now - stateTimer >= RED_DURATION_MS) changeState(RED_YELLOW); 
            break;
          case RED_YELLOW:
            setLight(true, true, false); 
            if (now - stateTimer >= 2000) changeState(GREEN); 
            break;
          case NIGHT_BLINK:
            changeState(RED); 
            break;
          default:
            changeState(RED);
            break;
        }
        if (isRampMoving && (now - rampMoveTimer >= RAMP_MOVE_DURATION_MS)) rampStop();
      }
  } 

  if (buzzerToneOn) {
    if (now - buzzerTimerMs >= buzzerIntervalMs) {
      int buzzerState = digitalRead(BUZZER_PIN);
      digitalWrite(BUZZER_PIN, !buzzerState); 
      buzzerTimerMs = now;
      if (buzzerState == LOW) buzzerIntervalMs = 50; 
      else buzzerIntervalMs = 150; 
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void TrafficController::updateLcd() {
  // *** NOVO: STARTUP LOGIKA (6-10 sekundi) ***
  if (!startupDone) {
      // Ako je prošlo manje od 6000ms (6 sekundi)
      if (millis() - startupTimer < 6000) {
          static String lastIpMsg = "";
          // Osveži samo ako se IP promenio (da ne treperi ekran)
          if (currentIP != lastIpMsg) {
              lcd.clear();
              lcd.setCursor(0,0); lcd.print(F("IP Adresa:"));
              lcd.setCursor(0,1); lcd.print(currentIP);
              lastIpMsg = currentIP;
          }
          return; // Prekini, ne ispisuj semafor podatke još
      } else {
          // Vreme isteklo, briši ekran i kreni normalno
          startupDone = true;
          lcd.clear();
      }
  }
  // *******************************************

  // Ako je noć, ekran je ugašen
  if (isNight && !emergencyActive) return;

  unsigned long now = millis();
  if (now - lcdTimer < 350) return;
  lcdTimer = now;

  const char* line0_text;
  if (emergencyActive) line0_text = "!!! HITNO !!!";
  else line0_text = "Dnevni rezim";
  
  String line1 = "";
  switch (state) {
    case GREEN:  line1 = "Zeleno"; break;
    case YELLOW: line1 = "Zuto"; break;
    case RED:    line1 = "Crveno"; break;
    case RED_YELLOW: line1 = "Priprema..."; break;
    case EMERGENCY: line1 = "PROLAZ SLUZBI"; break;
    case NIGHT_BLINK: line1 = "Nocni rad"; break;
    default:     line1 = "Cekanje"; break;
  }
  
  if (state == GREEN && !emergencyActive) {
    line1 += " (" + String((unsigned int)_ultraCongestion) + "%)";
  }

  if (String(line0_text) != prevLine0) { 
    lcd.setCursor(0,0); lcd.print(F("                ")); 
    lcd.setCursor(0,0); lcd.print(line0_text); 
    prevLine0 = line0_text; 
  }
  
  if (line1 != prevLine1) { 
    lcd.setCursor(0,1); lcd.print(F("                ")); 
    lcd.setCursor(0,1); lcd.print(line1); 
    prevLine1 = line1; 
  }
}

void TrafficController::changeState(SemState next) {
  state = next;
  stateTimer = millis();
  if (DEBUG_LOGGING) {
     // log
  }
}

void TrafficController::controlRamp(bool open) {
  if (isRampMoving) return;
  if (!servo.attached()) servo.attach(SERVO_PIN, SERVO_PULSE_MIN_US, SERVO_PULSE_MAX_US);
  servo.write(open ? SERVO_OPEN_DEG : SERVO_CLOSED_DEG);
  isRampOpen = open;
  isRampMoving = true;
  rampMoveTimer = millis();
  if (DEBUG_LOGGING) pushEvent(open ? F("Ramp OPEN") : F("Ramp CLOSE"));
}

void TrafficController::rampStop() {
  if (servo.attached()) {
    delay(20);
    servo.detach();
  }
  isRampMoving = false;
}

void TrafficController::forceRamp(bool open) {
  controlRamp(open);
}

void TrafficController::setEmergency(bool on) {
  if (on) {
    emergencyActive = true;
    controlRamp(true);
    changeState(EMERGENCY);
    pushEvent(F("CMD: Emergency Activated"));
  } else {
    emergencyActive = false;
    controlRamp(false);
    changeState(RED);
    pushEvent(F("CMD: Emergency Deactivated"));
  }
}

void TrafficController::buzzerProximityLoop() {
#if ULTRASONIC_ENABLED
  if (_ultraLastMm > 0 && _ultraLastMm < 300) {
    if (!buzzerToneOn) {
        buzzerToneOn = true;
        buzzerIntervalMs = 150; 
        buzzerTimerMs = millis();
        pushEvent(String(F("PROXIMITY ALERT: ")) + String(_ultraLastMm) + F("mm"));
    }
  } else {
    buzzerToneOn = false;
  }
#endif
}

void TrafficController::setLight(bool r, bool y, bool g) {
  ledWrite(LED_NS_RED_PIN, r);
  ledWrite(LED_NS_YELLOW_PIN, y);
  ledWrite(LED_NS_GREEN_PIN, g);
}

void TrafficController::beep(uint16_t ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

SystemState TrafficController::getCurrentStateData() const {
  SystemState s;
  s.mode = emergencyActive ? F("EMERGENCY") : (isNight ? F("NIGHT") : F("DAY"));
  
  switch (state) {
    case GREEN: s.light=F("GREEN"); break;
    case YELLOW: s.light=F("YELLOW"); break;
    case RED: s.light=F("RED"); break;
    case RED_YELLOW: s.light=F("RED_YELLOW"); break;
    case NIGHT_BLINK: s.light=F("BLINK"); break;
    case EMERGENCY: s.light=F("EMERGENCY"); break;
    default: s.light=F("INIT");
  }
  
  s.ramp = isRampMoving ? F("MOVING") : (isRampOpen ? F("OPEN") : F("CLOSED"));
  s.vehicles = vehicleCount;
  s.temperature = t;
  s.humidity = h;
  s.pressure = p;
  s.ldr = ldrRaw;
  TrafficController* self = const_cast<TrafficController*>(this);
  s.busVoltage = self->ina219.getBusVoltage_V() + (self->ina219.getShuntVoltage_mV() / 1000);
  s.current_mA = self->ina219.getCurrent_mA();
  s.power_mW   = self->ina219.getPower_mW();
#if ULTRASONIC_ENABLED
  s.ultra_mm = _ultraLastMm;
  s.congestion = _ultraCongestion;
#endif
  return s;
}

void TrafficController::pushEvent(const String& msg) {
  if (DEBUG_LOGGING) {
    lastEvent = msg;
  }
}

String TrafficController::takeLastEvent() {
  if (lastEvent.length() == 0) return "";
  String out = lastEvent;
  lastEvent = "";
  return out;
}