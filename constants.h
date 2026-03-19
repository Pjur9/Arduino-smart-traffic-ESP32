#pragma once

/* ================== MODOVI / OPCIJE ================== */
// Ako LED modul pali na LOW (preko tranzistora), stavi 1
// Za obične LED + otpornik → 0
#define LED_ACTIVE_LOW   0

/* ================== PINOVI - SVETLA ================== */
#define LED_NS_RED_PIN       13
#define LED_NS_YELLOW_PIN    12
#define LED_NS_GREEN_PIN     14

/* ================== PINOVI - PERIFERIJE ================== */
#define BUZZER_PIN           25
#define SERVO_PIN            26
#define LDR_PIN              35    // analog (input-only)

/* ================== I2C UREĐAJI ================== */
// LCD, BME280 i INA219 dele iste pinove (SDA=21, SCL=22)
#define LCD_ADDR             0x27
#define LCD_COLS             16
#define LCD_ROWS             2

#define BME280_ADDR          0x76

// INA219 - Senzor struje
#define INA219_ADDR          0x40

/* ================== RFID (SPI) ================== */
// SCK=18, MISO=19, MOSI=23 (podrazumevano za VSPI)
#define RFID_SS_PIN          5
#define RFID_RST_PIN         27

/* ================== RELEJI (NOVO) ================== */
#define RELAY_ENABLED            1
#define RELAY_ACTIVE_LOW         1    // 1 = Relej pali na LOW signal

#define RELAY_SERVO_ULTRA_PIN    16   // Gasi periferije noću
#define RELAY_RFID_PIN           17   // RFID (obično ostaje upaljen)
#define RELAY_LCD_PIN            4    // Kontroliše napajanje LCD ekrana (Pin 4)

/* ================== KALIBRACIJA SVETLA (NAPREDNO) ================== */
// Umesto jedne granice (1500), koristimo dve da sprečimo treperenje.
// Tvoj senzor radi OBRNUTO: 100 = Jako svetlo, 4095 = Mrak.

// Da bi postalo DAN, mora postati jako svetlo (broj pasti ispod 2500)
#define LDR_THR_BECOME_DAY      2500  

// Da bi postalo NOĆ, mora postati baš mračno (broj porasti iznad 3500)
#define LDR_THR_BECOME_NIGHT    3500  

/* ================== VREMENSKA TRAJANJA (ms) ================== */
#define GREEN_DURATION_MS        10000
#define YELLOW_DURATION_MS       3000
#define RED_DURATION_MS          8000
#define NIGHT_BLINK_INTERVAL_MS  500  // Brzina treptanja žutog

/* ================== RAMPA (SERVO) ================== */
#define SERVO_PULSE_MIN_US    500
#define SERVO_PULSE_MAX_US    2500
#define SERVO_OPEN_DEG        90    // Rampa gore
#define SERVO_CLOSED_DEG      0     // Rampa dole
#define RAMP_MOVE_DURATION_MS 700

// Koliko dugo traje "Hitno stanje" nakon RFID kartice
#define DURATION_EMERGENCY_MS    15000

/* ================== RFID KARTICE ================== */
#define AUTH_UID_LEN 4
// Tvoj UID kartice:
static const byte AUTHORIZED_RFID_UID[AUTH_UID_LEN] = { 0x73, 0x5C, 0x0B, 0x0D };

/* ================== OSTALO ================== */
#define SSE_EVENT_NAME "telemetry"

/* ================== TCRT5000 (IC SENZOR) ================== */
#define TCRT_DO_PIN        34
#define TCRT_ACTIVE_HIGH   1      // 1 = detekcija
#define TCRT_DEBOUNCE_MS   40
#define TCRT_COOLDOWN_MS   2000

/* ================== ULTRAZVUČNI SENZOR ================== */
#define ULTRASONIC_ENABLED       1
#define ULTRASONIC_TRIG_PIN      33
#define ULTRASONIC_ECHO_PIN      32
#define ULTRASONIC_EMA_ALPHA     0.35f
#define ULTRASONIC_PERIOD_MS     120
#define ULTRASONIC_NEAR_MM       600   // Ispod ovoga je "gužva"
#define ULTRASONIC_FAR_MM        2500

/* ================== FIREBASE ================== */
#define FB_TELEM_PERIOD_MS   1000
#define FB_CMD_NODE          "cmd"
#define FB_TELEM_NODE        "telemetry"