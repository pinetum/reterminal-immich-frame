#include "power.h"
#include "pins.h"
#include "log.h"

#include <esp_sleep.h>
#include <driver/rtc_io.h>

// Survives a reboot (not a power cut), which is all this needs to do.
RTC_DATA_ATTR static uint32_t s_configOnBoot = 0;
static const uint32_t kConfigOnBootMagic = 0xC0FFEE01;

void powerRequestConfigOnBoot() { s_configOnBoot = kConfigOnBootMagic; }

bool powerTakeConfigRequest() {
  const bool want = (s_configOnBoot == kConfigOnBootMagic);
  s_configOnBoot = 0;
  return want;
}

// How long KEY2 must be held to mean "open the settings page" rather than
// "redraw the current photo".
static const uint32_t kConfigHoldMs = 2000;

const char* wakeReasonName(WakeReason r) {
  switch (r) {
    case WAKE_POWER_ON:    return "power-on";
    case WAKE_TIMER:       return "timer";
    case WAKE_KEY_NEXT:    return "key:next";
    case WAKE_KEY_PREV:    return "key:prev";
    case WAKE_KEY_REFRESH: return "key:refresh";
    case WAKE_KEY_CONFIG:  return "key:config";
  }
  return "?";
}

void powerInit() {
  // The keys have hardware pull-ups, so plain INPUT is what the Seeed examples
  // use. They read LOW when pressed.
  pinMode(PIN_KEY0, INPUT);
  pinMode(PIN_KEY1, INPUT);
  pinMode(PIN_KEY2, INPUT);

  pinMode(PIN_LED, OUTPUT);
  ledOff();

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  pinMode(PIN_BATTERY_EN, OUTPUT);
  digitalWrite(PIN_BATTERY_EN, HIGH);   // enable the divider
}

WakeReason powerWakeReason() {
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  if (cause == ESP_SLEEP_WAKEUP_TIMER) return WAKE_TIMER;

  if (cause == ESP_SLEEP_WAKEUP_EXT1) {
    const uint64_t mask = esp_sleep_get_ext1_wakeup_status();
    if (mask & (1ULL << PIN_KEY0)) return WAKE_KEY_NEXT;
    if (mask & (1ULL << PIN_KEY1)) return WAKE_KEY_PREV;
    if (mask & (1ULL << PIN_KEY2)) {
      // Distinguish a tap from a hold. Beep every 500 ms so the user gets
      // feedback that holding is doing something -- on a 40-second e-paper
      // there is no other way to acknowledge a keypress promptly.
      const uint32_t t0 = millis();
      uint32_t nextBeep = 0;
      while (millis() - t0 < kConfigHoldMs) {
        if (digitalRead(PIN_KEY2) == HIGH) return WAKE_KEY_REFRESH;   // released early
        if (millis() - t0 >= nextBeep) { beep(1600, 40); nextBeep += 500; }
        delay(10);
      }
      beep(2600, 150);
      return WAKE_KEY_CONFIG;
    }
    return WAKE_KEY_REFRESH;
  }

  // Also honour a hold-at-power-on, so a misconfigured device can be rescued
  // without waiting for a sleep cycle.
  if (digitalRead(PIN_KEY2) == LOW) {
    const uint32_t t0 = millis();
    while (millis() - t0 < kConfigHoldMs) {
      if (digitalRead(PIN_KEY2) == HIGH) break;
      delay(10);
    }
    if (digitalRead(PIN_KEY2) == LOW) { beep(2600, 150); return WAKE_KEY_CONFIG; }
  }
  return WAKE_POWER_ON;
}

// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------
float batteryVolts() {
  // A reading takes ~50 ms (16 samples with settling delays) and several callers
  // want it within the same moment -- the status line alone would otherwise do
  // three separate bursts. Cache it briefly.
  static float s_cached = 0;
  static uint32_t s_cachedAt = 0;
  if (s_cached > 0 && millis() - s_cachedAt < 2000) return s_cached;

  digitalWrite(PIN_BATTERY_EN, HIGH);
  delay(10);
  // Average a handful of samples: the ADC on the S3 is noisy and the panel
  // refresh makes the rail move around.
  uint32_t acc = 0;
  for (int i = 0; i < 16; ++i) { acc += analogReadMilliVolts(PIN_BATTERY_ADC); delay(2); }
  const float mv = acc / 16.0f;

  s_cached = (mv * BATTERY_DIVIDER) / 1000.0f;
  s_cachedAt = millis();
  return s_cached;
}

int batteryPercent() {
  const float v = batteryVolts();
  // Piecewise approximation of a single-cell LiPo discharge curve. A linear
  // 3.3-4.2 V map would read ~50% for most of the useful life, which makes the
  // low-battery cut-off fire far too late.
  static const struct { float v; int pct; } kCurve[] = {
      {3.30f, 0}, {3.60f, 10}, {3.70f, 20}, {3.75f, 30}, {3.79f, 40}, {3.83f, 50},
      {3.87f, 60}, {3.92f, 70}, {3.98f, 80}, {4.06f, 90}, {4.20f, 100},
  };
  if (v <= kCurve[0].v) return 0;
  const int n = sizeof(kCurve) / sizeof(kCurve[0]);
  if (v >= kCurve[n - 1].v) return 100;
  for (int i = 1; i < n; ++i) {
    if (v <= kCurve[i].v) {
      const float f = (v - kCurve[i - 1].v) / (kCurve[i].v - kCurve[i - 1].v);
      return (int)(kCurve[i - 1].pct + f * (kCurve[i].pct - kCurve[i - 1].pct) + 0.5f);
    }
  }
  return 100;
}

bool usbPowered() {
  // No dedicated VBUS sense pin is exposed, so this is inferred: above the
  // charge-termination voltage the pack can only be on external power.
  return batteryVolts() > 4.25f;
}

// ---------------------------------------------------------------------------
// LED / buzzer
// ---------------------------------------------------------------------------
void ledOn()  { digitalWrite(PIN_LED, LOW);  }   // inverted
void ledOff() { digitalWrite(PIN_LED, HIGH); }

void ledBlink(int times, int onMs, int offMs) {
  for (int i = 0; i < times; ++i) { ledOn(); delay(onMs); ledOff(); delay(offMs); }
}

void beep(int freq, int ms) {
  tone(PIN_BUZZER, freq, ms);
  delay(ms);
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);
}

void beepOk()    { beep(2000, 60); delay(40); beep(2600, 80); }
void beepError() { beep(700, 180); delay(60); beep(500, 220); }

// ---------------------------------------------------------------------------
// Deep sleep
// ---------------------------------------------------------------------------
void deepSleep(uint32_t minutes) {
  if (minutes < 1) minutes = 1;

  // Stop driving the divider so it does not leak through the sleep.
  digitalWrite(PIN_BATTERY_EN, LOW);
  ledOff();
  noTone(PIN_BUZZER);
  digitalWrite(PIN_BUZZER, LOW);

  const uint64_t mask = (1ULL << PIN_KEY0) | (1ULL << PIN_KEY1) | (1ULL << PIN_KEY2);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

  // Keep the pull-ups alive through sleep, otherwise the inputs float and the
  // device wakes on noise and drains the battery overnight.
  for (int pin : {PIN_KEY0, PIN_KEY1, PIN_KEY2}) {
    rtc_gpio_pullup_en((gpio_num_t)pin);
    rtc_gpio_pulldown_dis((gpio_num_t)pin);
  }

  esp_sleep_enable_timer_wakeup((uint64_t)minutes * 60ULL * 1000000ULL);

  LOG.printf("[pwr] deep sleep for %u min (wake also on KEY0/1/2)\n", (unsigned)minutes);
  LOG.flush();
  delay(20);
  esp_deep_sleep_start();
}
