#pragma once
#include <Arduino.h>

class UltrasonicNB {
public:
  UltrasonicNB(uint8_t trigPin, uint8_t echoPin, float emaAlpha = 0.35f)
      : _trig(trigPin), _echo(echoPin), _emaAlpha(emaAlpha) {}

  void begin(uint32_t triggerPeriodMs = 120) {
    pinMode(_trig, OUTPUT);
    digitalWrite(_trig, LOW);
    pinMode(_echo, INPUT);

    _triggerPeriodUs = triggerPeriodMs * 1000UL;
    _lastTriggerUs = micros();

    g_instance = this; // single instance routing for ISR
    attachInterrupt(digitalPinToInterrupt(_echo), UltrasonicNB::isrEchoChange, CHANGE);
  }

  // pozivaj često (u readSensors)
  void update() {
    const uint32_t now = micros();

    // 1) Periodični TRIG (10us impuls)
    if ((now - _lastTriggerUs) >= _triggerPeriodUs && !_waitingEcho) {
      _waitingEcho = true;
      _echoRiseUs = 0;
      _echoFallUs = 0;
      _sampleReady = false;

      digitalWrite(_trig, LOW);  delayMicroseconds(2);
      digitalWrite(_trig, HIGH); delayMicroseconds(10);
      digitalWrite(_trig, LOW);

      _lastTriggerUs = now;
    }

    // 2) Ako je ISR latovao pulse, izračunaj distancu
    if (_sampleReady) {
      _sampleReady = false;

      if (_echoFallUs > _echoRiseUs) {
        const uint32_t pulse = _echoFallUs - _echoRiseUs; // µs
        float mm = (float)pulse * 0.1715f;                // 0.343/2 mm/µs

        if (mm >= 30.0f && mm <= 5000.0f) {
          if (!_emaInitialized) { _ema = mm; _emaInitialized = true; }
          else { _ema = _emaAlpha * mm + (1.0f - _emaAlpha) * _ema; }
          _lastValidMm = (uint16_t)_ema;
          _hasValid = true;
        }
      }
      _waitingEcho = false;
    }

    // 3) Timeout
    if (_waitingEcho && (now - _lastTriggerUs) > 45000UL) {
      _waitingEcho = false;
    }
  }

  bool hasValid() const { return _hasValid; }
  uint16_t distanceMm() const { return _lastValidMm; }
  float distanceEmaMm() const { return _emaInitialized ? _ema : -1.0f; }

  // 0..100 (100 = blizu/gužva)
  uint8_t congestionIndex(uint16_t nearMm = 600, uint16_t farMm = 2500) const {
    if (!_hasValid || !_emaInitialized) return 0;
    float x = _ema;
    if (x <= nearMm) return 100;
    if (x >= farMm) return 0;
    float idx = (farMm - x) / float(farMm - nearMm) * 100.0f;
    if (idx < 0) idx = 0; if (idx > 100) idx = 100;
    return (uint8_t)(idx + 0.5f);
  }

  void setTriggerPeriodMs(uint32_t ms) { _triggerPeriodUs = ms * 1000UL; }
  void setEmaAlpha(float a) { _emaAlpha = constrain(a, 0.05f, 0.95f); }

private:
  static void IRAM_ATTR isrEchoChange();  // samo deklaracija

  uint8_t _trig, _echo;
  volatile uint32_t _echoRiseUs = 0, _echoFallUs = 0;
  volatile bool _sampleReady = false;
  bool _waitingEcho = false;

  uint32_t _lastTriggerUs = 0;
  uint32_t _triggerPeriodUs = 120000; // 120 ms

  bool _hasValid = false;
  bool _emaInitialized = false;
  float _ema = 0.0f;
  float _emaAlpha = 0.35f;
  uint16_t _lastValidMm = 0;

  static UltrasonicNB* g_instance;       // samo deklaracija
};
