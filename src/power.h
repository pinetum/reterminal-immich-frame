#pragma once
#include <Arduino.h>

enum WakeReason {
  WAKE_POWER_ON = 0,   // cold boot / reset
  WAKE_TIMER,          // slideshow interval elapsed
  WAKE_KEY_NEXT,       // KEY0
  WAKE_KEY_PREV,       // KEY1
  WAKE_KEY_REFRESH,    // KEY2, short press
  WAKE_KEY_CONFIG,     // KEY2, held for >= 2 s
};

const char* wakeReasonName(WakeReason r);

void powerInit();

// Work out why we woke up. For KEY2 this blocks for up to ~2 s while it decides
// between a short press and a hold, beeping as feedback.
WakeReason powerWakeReason();

// Battery. `volts` is the real pack voltage (the divider is already undone).
float batteryVolts();
int   batteryPercent();
bool  usbPowered();

void ledOn();
void ledOff();
void ledBlink(int times, int onMs = 60, int offMs = 140);

void beep(int freq = 2000, int ms = 60);
void beepOk();
void beepError();

// Arm the timer + all three keys and go to sleep. Never returns.
void deepSleep(uint32_t minutes);

// Ask the next boot to go straight back into config mode. Used by the admin
// page's "Save & reconnect" button: after the Wi-Fi details are entered from
// the setup access point, a reboot is the only way to get onto the real network,
// and the settings page has to still be there afterwards.
void powerRequestConfigOnBoot();
bool powerTakeConfigRequest();
