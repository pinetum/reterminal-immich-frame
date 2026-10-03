#pragma once
#include <Arduino.h>

// Work the admin page can ask for. NOTHING slow happens on the webserver task:
// ESPAsyncWebServer's handlers run on the AsyncTCP task, which is the same task
// that services lwIP callbacks, so a blocking socket call (an Immich request, a
// Wi-Fi scan) there can deadlock the network stack. SD access from there would
// also race the main task. So every handler records an intent and returns, and
// main.cpp's loop() does the work.
enum PendingAction {
  ACT_NONE = 0,
  ACT_NEXT,
  ACT_PREV,
  ACT_REFRESH,
  ACT_INDEX,
  ACT_PLAYLIST_REFRESH,
  ACT_CLEAR_CACHE,
  ACT_LOAD_ALBUMS,
  ACT_IMMICH_TEST,
  ACT_WIFI_SCAN,
  ACT_EXIT_CONFIG,
  ACT_REBOOT,
  // Re-render the current photo at 1/4 scale into SD_PATH_PREVIEW so the
  // settings page can show the effect of the processing options without
  // spending a 40-second panel refresh on every tweak.
  ACT_PREVIEW,
  // Paint the six palette inks as solid bands, for measuring them with a
  // camera and a colour picker. Costs a real refresh, like any other draw.
  ACT_PALETTE_CHART,
};

struct AppStatus {
  String  lastError;
  String  busy;              // non-empty while a long action is running
  String  currentAsset;
  int     renderMs    = 0;
  int     rotation    = 0;
  int     batteryPct  = 0;
  float   batteryV    = 0;
  bool    sdOk        = false;
  uint32_t photos     = 0;
  int     position    = 0;
  uint32_t cacheFrames = 0;
  uint32_t configEndsAt = 0;   // millis() deadline while in config mode

  // Results of queued actions, published for the admin page to read back.
  String  albumsJson;        // "[]" until ACT_LOAD_ALBUMS has run
  String  albumsErr;
  String  testMsg;           // result of ACT_IMMICH_TEST
  bool    testOk = false;
  String  scanJson;          // result of ACT_WIFI_SCAN
  bool    previewReady = false;   // SD_PATH_PREVIEW holds a current preview
  String  previewErr;             // why the last ACT_PREVIEW failed
  int     previewRotation = 0;    // rotation the preview was composed at

  // Set by the config handler, acted on by the main task so SD access stays on
  // one task.
  bool    cacheInvalid    = false;
  bool    playlistInvalid = false;

  // True while the settings window is open. The show pipeline normally powers
  // the radio down once a photo is downloaded, which would cut the admin page
  // off mid-request.
  bool    keepNetwork     = false;

  PendingAction action = ACT_NONE;
  int actionArg = 0;
};

extern AppStatus g_status;

// Reads the battery and the SD card, so it must only be called from the main
// task. /api/status just serialises whatever this last stored.
void appRefreshStatus();

// Fetch (if needed), render and display the photo at the current cursor. Sets
// g_status.lastError on failure but never draws -- the caller decides whether an
// error is worth a 40-second refresh.
bool appShowCurrent(bool allowNetwork);

// Re-read the album from Immich into the playlist on SD.
bool appRefreshPlaylist();

// Re-render the current photo into SD_PATH_PREVIEW. Downloads the source first
// if SD_PATH_TMP does not already hold it. Main task only: decodes, touches SD
// and may use the network.
bool appBuildPreview();

// Run whatever the admin page queued. Returns true if the caller should leave
// config mode.
bool appRunPendingAction();
