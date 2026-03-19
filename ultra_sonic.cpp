#include "ultra_sonic.h"
#include "driver/gpio.h"   // gpio_get_level je brži/ISR-friendly od digitalRead

UltrasonicNB* UltrasonicNB::g_instance = nullptr;

// Minimalna ISR rutina (bez Serial, bez String)
void IRAM_ATTR UltrasonicNB::isrEchoChange() {
  UltrasonicNB* self = g_instance;
  if (!self) return;

  const uint32_t t = micros();  // OK u ISR na ESP32
  int level = gpio_get_level((gpio_num_t)self->_echo);

  if (level) {
    self->_echoRiseUs = t;      // rising
  } else {
    self->_echoFallUs = t;      // falling
    if (self->_waitingEcho && self->_echoRiseUs != 0) {
      self->_sampleReady = true;
    }
  }
}
