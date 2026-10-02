// =============================================================================
//  reTerminal E1004 -- Immich ePaper photo frame
//
//  Power-saving design: the device is in deep sleep almost all of the time. Each
//  wake does one job and goes straight back to sleep, so the whole program is
//  really a state machine that runs once in setup(). loop() only does anything
//  while the settings window is open.
//
//      reset / deep-sleep wake
//        |- why did we wake?  (timer / KEY0 / KEY1 / KEY2-tap / KEY2-hold)
//        |- battery too low?  -> warning page -> sleep
//        |- not configured, or KEY2 held -> CONFIG MODE
//        |- otherwise: move the cursor, show that photo, sleep
//
//  Everything expensive is avoided when it can be: a photo already rendered to
//  the SD cache is displayed without touching the radio or the decoder, and the
//  radio is switched off before the 40-second panel refresh.
// =============================================================================
#include <Arduino.h>

#include "app.h"
#include "display.h"
#include "log.h"
#include "net.h"
#include "pins.h"
#include "playlist.h"
#include "power.h"
#include "sdcard.h"
#include "settings.h"
#include "webui.h"

RTC_DATA_ATTR static uint32_t s_bootCount = 0;
// Remembers that the low-battery page is already on the panel, so a flat pack is
// not drained further by redrawing the same warning every hour.
RTC_DATA_ATTR static bool s_lowBattShown = false;

static bool s_configMode = false;
static uint32_t s_ledNext = 0;
static bool s_ledState = false;
static uint32_t s_statusNext = 0;

// -----------------------------------------------------------------------------
static void goToSleep() {
  displaySleep();
  playlistSave();
  netStop();
  deepSleep(g_cfg.intervalMinutes);
}

static void fatalPage(const char* title, const String& detail, const char* hint) {
  LOG.printf("[app] %s: %s\n", title, detail.c_str());
  beepError();
  if (displayReady()) {
    displayError(title, detail, hint);
    displayUpdate();
  }
}

// -----------------------------------------------------------------------------
static void enterConfigMode() {
  s_configMode = true;
  g_status.keepNetwork = true;
  LOG.println("[app] === config mode ===");

  // Prefer the real network: the phone is probably already on it, and Immich is
  // only reachable from there (album list, connection test). Fall back to the
  // setup access point when there are no credentials or they do not work.
  String ssid;
  bool apMode = false;
  if (g_cfg.configured() && netConnectSta(20000)) {
    ssid = netSsid();
  } else {
    netStartAp(ssid);
    apMode = true;
  }

  webuiBegin();

  const String url = "http://" + netIp() + "/";
  g_status.configEndsAt = millis() + g_cfg.configWindowMinutes * 60UL * 1000UL;

  // Confirm as soon as the server is actually reachable. Drawing the address
  // costs a 40-second refresh, and someone who already knows the address should
  // not have to wait for it.
  beepOk();
  LOG.printf("[app] settings open for %u min at %s\n",
             (unsigned)g_cfg.configWindowMinutes, url.c_str());

  displayConfigScreen(url, ssid, apMode ? AP_PASSWORD : String(""), apMode,
                      g_cfg.configWindowMinutes);
  displayUpdate();
}

static void leaveConfigMode() {
  LOG.println("[app] leaving config mode");
  webuiEnd();
  s_configMode = false;
  g_status.keepNetwork = false;
  ledOff();

  if (!g_cfg.immichReady()) {
    fatalPage("Immich not configured",
              "Still missing the server address, API key or album.",
              "Hold the Refresh key for 2 s to reopen the settings page.");
    goToSleep();
  }

  // A first-time setup finishes here, so the playlist usually still has to be
  // fetched -- appShowCurrent() does that itself when it is stale.
  if (!appShowCurrent(true)) {
    fatalPage("Cannot show a photo", g_status.lastError,
              "Check the Immich address, API key and album in the settings page.");
  }
  goToSleep();
}

// -----------------------------------------------------------------------------
void setup() {
  logBegin();
  ++s_bootCount;

  LOG.println("==================================================");
  LOG.println("  reTerminal E1004 -- Immich photo frame");
  LOG.printf("  boot #%u   PSRAM %u kB   flash %u MB\n", (unsigned)s_bootCount,
             (unsigned)(ESP.getPsramSize() / 1024),
             (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  LOG.println("==================================================");
  if (ESP.getPsramSize() == 0)
    LOG.println("[sys] !! PSRAM reads 0 -- build with board_build.arduino.memory_type=qio_opi");

  powerInit();
  settingsLoad();

  const WakeReason wake = powerWakeReason();
  const bool configRequested = powerTakeConfigRequest();
  LOG.printf("[app] wake reason: %s%s\n", wakeReasonName(wake),
             configRequested ? " (+config requested)" : "");

  // No wall clock on board, so the playlist's age is tracked by accumulating the
  // sleep interval. Good enough to expire a daily TTL.
  if (wake == WAKE_TIMER) playlistAgeBy(g_cfg.intervalMinutes);

  // The panel has to come up before the SD card: they share one SPI bus and the
  // card reuses the instance the panel driver creates.
  if (!displayInit()) {
    LOG.println("[app] display unavailable -- sleeping, nothing can be shown");
    deepSleep(g_cfg.intervalMinutes);
  }
  sdBegin();
  playlistInit();
  appRefreshStatus();
  logMem("after init");

  const int pct = batteryPercent();
  LOG.printf("[app] battery %d%% (%.2f V)%s\n", pct, batteryVolts(),
             usbPowered() ? ", USB powered" : "");

  // ---- config mode ---------------------------------------------------------
  if (wake == WAKE_KEY_CONFIG || configRequested || !g_cfg.configured()) {
    if (!g_cfg.configured()) LOG.println("[app] not configured yet -- opening setup");
    enterConfigMode();
    return;   // loop() takes over from here
  }

  // ---- battery guard -------------------------------------------------------
  // Checked before anything that costs energy, so a flat pack cannot be drained
  // further by a pointless download-and-refresh cycle.
  if (pct < (int)g_cfg.lowBatteryPercent && !usbPowered()) {
    LOG.println("[app] battery below threshold -- pausing the slideshow");
    // Draw the warning once. A refresh costs around 2 mAh, so repeating it every
    // wake would be the thing that finally flattens the pack.
    if (!s_lowBattShown) {
      displayLowBattery(pct, batteryVolts());
      displayUpdate();
      s_lowBattShown = true;
    }
    displaySleep();
    // Back off to at least an hour: a 2-minute slideshow interval must not keep
    // waking a nearly empty battery.
    deepSleep(max(60UL, (unsigned long)g_cfg.intervalMinutes));
  }
  s_lowBattShown = false;

  if (!sdReady()) {
    fatalPage("No SD card", "The frame needs a FAT32 microSD card for the photo cache.",
              "Insert a card (64 GB or smaller, formatted FAT32) and press Refresh.");
    goToSleep();
  }

  if (!g_cfg.immichReady()) {
    fatalPage("Immich not configured", "The server address, API key or album is missing.",
              "Hold the Refresh key for 2 s to open the settings page.");
    goToSleep();
  }

  // ---- move the cursor -----------------------------------------------------
  switch (wake) {
    case WAKE_KEY_NEXT: playlistAdvance(1);  break;
    case WAKE_KEY_PREV: playlistAdvance(-1); break;
    case WAKE_TIMER:    playlistAdvance(1);  break;
    case WAKE_KEY_REFRESH:                        // redraw what is already up
    case WAKE_POWER_ON:                           // show where we left off
    default: break;
  }

  ledOn();   // "working" indicator; the e-paper cannot show progress

  // ---- show ----------------------------------------------------------------
  // appShowCurrent() re-reads the album itself when the playlist has expired, so
  // there is one place that decides freshness rather than three.
  if (!appShowCurrent(true)) {
    ledOff();
    fatalPage("Cannot show a photo", g_status.lastError,
              "Check the Immich address, API key and album in the settings page.");
    goToSleep();
  }

  ledOff();
  logMem("before sleep");
  goToSleep();
}

// -----------------------------------------------------------------------------
void loop() {
  if (!s_configMode) {
    // setup() always ends in deep sleep, so getting here means something went
    // wrong. Sleep rather than spin and flatten the battery.
    LOG.println("[app] unexpected loop() -- sleeping");
    goToSleep();
    return;
  }

  webuiLoop();

  // The battery ADC and the SD card are only ever read from this task, so the
  // webserver can serve /api/status without touching either.
  if (millis() >= s_statusNext) {
    appRefreshStatus();
    s_statusNext = millis() + 2000;
  }

  // Saving new settings can invalidate the playlist or the rendered frames. The
  // webserver only flags that; the SD work happens here.
  if (g_status.cacheInvalid) {
    g_status.cacheInvalid = false;
    cacheClear();
  }
  if (g_status.playlistInvalid) {
    g_status.playlistInvalid = false;
    playlistInit();
  }

  // Slow heartbeat so it is obvious the settings window is open.
  if (millis() >= s_ledNext) {
    s_ledState = !s_ledState;
    s_ledState ? ledOn() : ledOff();
    s_ledNext = millis() + (s_ledState ? 80 : 1900);
  }

  // Anything the admin page queued runs here, on the main task, because a panel
  // refresh blocks for ~40 s and must never run on the webserver's task.
  if (g_status.action != ACT_NONE) {
    if (appRunPendingAction()) { leaveConfigMode(); return; }
    // A long action can easily eat the whole window; give the user time to see
    // the result and carry on.
    g_status.configEndsAt = millis() + g_cfg.configWindowMinutes * 60UL * 1000UL;
  }

  // Holding Refresh leaves immediately.
  if (digitalRead(PIN_KEY2) == LOW) {
    const uint32_t t0 = millis();
    while (digitalRead(PIN_KEY2) == LOW && millis() - t0 < 2000) delay(10);
    if (millis() - t0 >= 2000) {
      beep(2600, 150);
      leaveConfigMode();
      return;
    }
  }

  if ((long)(millis() - g_status.configEndsAt) >= 0) {
    LOG.println("[app] settings window expired");
    leaveConfigMode();
    return;
  }

  delay(20);
}
