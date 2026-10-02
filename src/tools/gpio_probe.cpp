// =============================================================================
//  Hardware probe -- build with:  pio run -e gpio_probe -t upload
//                     then watch: pio device monitor -b 115200
//
//  WHY THIS EXISTS
//  ---------------
//  Seeed's documentation states that KEY0/KEY1/KEY2 on the E1004 are GPIO3/4/5
//  and correspond to the three FRONT panel buttons. It does not say what the two
//  page-turn buttons on the BACK are wired to -- most likely in parallel with
//  KEY0/KEY1, but that is an assumption, and the firmware's deep-sleep wake mask
//  depends on it.
//
//  This sketch watches every RTC-capable GPIO (0-21, the only ones that can wake
//  the ESP32-S3 from deep sleep) and prints every edge it sees. Press each
//  button in turn and read off which pin moved.
//
//  It also exercises the LED, the buzzer and the battery ADC, and reports the
//  SD card detect line -- a quick confidence check on a fresh board.
//
//  If a back button turns out to be on a pin other than 3/4/5, add it to
//  include/pins.h and to the ext1 wake mask in src/power.cpp.
// =============================================================================
#include <Arduino.h>
#include "pins.h"

#define LOG Serial1

// Only GPIO0-21 can wake the S3 from deep sleep, so that is the range worth
// knowing about.
static const int kFirstPin = 0;
static const int kLastPin  = 21;

static const char* pinNote(int p) {
  switch (p) {
    case 0:  return "strapping (BOOT)";
    case 1:  return "battery ADC";
    case 2:  return "ePaper CS2";
    case 3:  return "KEY0  <- front right";
    case 4:  return "KEY1  <- front left";
    case 5:  return "KEY2  <- front refresh";
    case 7:  return "SPI SCK (shared)";
    case 8:  return "SPI MISO (shared)";
    case 9:  return "SPI MOSI (shared)";
    case 10: return "ePaper CS";
    case 11: return "ePaper DC";
    case 12: return "ePaper EN";
    case 13: return "ePaper BUSY";
    case 14: return "SD CS";
    case 15: return "SD DET";
    case 16: return "SD EN";
    case 19: return "I2C SDA";
    case 20: return "I2C SCL";
    case 21: return "battery enable";
    default: return "";
  }
}

// Pins we must not reconfigure: they are driven outputs or bus lines, and
// poking them would just produce noise.
static bool skip(int p) {
  switch (p) {
    case 1: case 7: case 8: case 9: case 10: case 11:
    case 12: case 14: case 16: case 19: case 20: case 21:
      return true;
    default:
      return false;
  }
}

static int last[kLastPin + 1];

void setup() {
  LOG.begin(115200, SERIAL_8N1, PIN_SERIAL_RX, PIN_SERIAL_TX);
  delay(400);
  LOG.println();
  LOG.println("==================================================");
  LOG.println("  reTerminal E1004 -- hardware probe");
  LOG.println("==================================================");
  LOG.printf("PSRAM %u kB   flash %u MB\n",
             (unsigned)(ESP.getPsramSize() / 1024),
             (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  if (ESP.getPsramSize() == 0)
    LOG.println("!! PSRAM reads 0 -- the main firmware will not work like this.");

  // --- LED + buzzer -------------------------------------------------------
  pinMode(PIN_LED, OUTPUT);
  LOG.printf("LED on GPIO%d: 3 blinks now (LOW = on)\n", PIN_LED);
  for (int i = 0; i < 3; ++i) {
    digitalWrite(PIN_LED, LOW);  delay(150);
    digitalWrite(PIN_LED, HIGH); delay(150);
  }
  pinMode(PIN_BUZZER, OUTPUT);
  LOG.printf("Buzzer on GPIO%d: two beeps now\n", PIN_BUZZER);
  tone(PIN_BUZZER, 2000, 120); delay(200);
  tone(PIN_BUZZER, 2600, 120); delay(200);
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);

  // --- battery ------------------------------------------------------------
  pinMode(PIN_BATTERY_EN, OUTPUT);
  digitalWrite(PIN_BATTERY_EN, HIGH);
  delay(20);
  uint32_t acc = 0;
  for (int i = 0; i < 16; ++i) { acc += analogReadMilliVolts(PIN_BATTERY_ADC); delay(2); }
  const float v = (acc / 16.0f) * BATTERY_DIVIDER / 1000.0f;
  LOG.printf("Battery: %.3f V  (ADC GPIO%d x %.1f)%s\n", v, PIN_BATTERY_ADC,
             BATTERY_DIVIDER, v > 4.25f ? "  -- looks like USB power" : "");

  // --- SD detect ----------------------------------------------------------
  pinMode(PIN_SD_EN, OUTPUT);
  digitalWrite(PIN_SD_EN, HIGH);
  pinMode(PIN_SD_DET, INPUT_PULLUP);
  delay(50);
  LOG.printf("SD_DET (GPIO%d) = %s -> %s\n", PIN_SD_DET,
             digitalRead(PIN_SD_DET) ? "HIGH" : "LOW",
             digitalRead(PIN_SD_DET) ? "no card (or pin floating)" : "card present");

  // --- arm the watch ------------------------------------------------------
  LOG.println();
  LOG.println("Watching GPIO0-21 for changes. Current levels:");
  for (int p = kFirstPin; p <= kLastPin; ++p) {
    if (skip(p)) { last[p] = -1; continue; }
    pinMode(p, INPUT);
    last[p] = digitalRead(p);
    LOG.printf("  GPIO%-2d = %s   %s\n", p, last[p] ? "HIGH" : "LOW ", pinNote(p));
  }
  LOG.println();
  LOG.println("Now press each button in turn -- front left/right/refresh AND the");
  LOG.println("two page-turn buttons on the BACK. Note which GPIO each one moves.");
  LOG.println();
}

void loop() {
  for (int p = kFirstPin; p <= kLastPin; ++p) {
    if (last[p] < 0) continue;
    const int now = digitalRead(p);
    if (now == last[p]) continue;
    last[p] = now;
    const char* note = pinNote(p);
    LOG.printf("GPIO%-2d -> %s%s%s\n", p, now ? "RELEASED (HIGH)" : "PRESSED  (LOW)",
               *note ? "   " : "", note);
  }
  delay(8);   // crude debounce; an edge shorter than this is not a human
}
