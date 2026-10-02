#include "app.h"
#include "settings.h"
#include "display.h"
#include "sdcard.h"
#include "render.h"
#include "decode.h"
#include "immich.h"
#include "playlist.h"
#include "net.h"
#include "power.h"
#include "log.h"

#include <SD.h>

AppStatus g_status;

void appRefreshStatus() {
  g_status.batteryV   = batteryVolts();
  g_status.batteryPct = batteryPercent();
  g_status.sdOk       = sdReady();
  g_status.photos     = playlistCount();
  g_status.position   = playlistPosition();
  g_status.cacheFrames = cacheCount();
}

// How much PSRAM the decoder may use for the source image. The panel frame
// (960 kB) is already allocated by the time this is asked, and ~1 MB is left
// spare for Wi-Fi/TLS buffers and fragmentation headroom.
static size_t decodeBudget() {
  const size_t freePs = ESP.getFreePsram();
  const size_t reserve = 1024u * 1024u;
  return (freePs > reserve + 256u * 1024u) ? (freePs - reserve) : 0;
}

bool appRefreshPlaylist() {
  String err;
  if (!netConnected() && !netConnectSta()) {
    g_status.lastError = "Wi-Fi connection failed";
    return false;
  }
  const int n = immichFetchPlaylist(err);
  if (n <= 0) {
    g_status.lastError = err.length() ? err : String("album is empty");
    return false;
  }
  playlistMarkFresh((uint32_t)n);
  g_status.photos = n;
  g_status.lastError = "";
  return true;
}

static String footerText() {
  String s = String(playlistPosition()) + " / " + String(playlistCount());
  if (g_cfg.albumName.length()) s += "   -   " + g_cfg.albumName;
  s += "   -   battery " + String(batteryPercent()) + "%";
  return s;
}

bool appShowCurrent(bool allowNetwork) {
  if (!displayReady()) return false;

  // Freshness is handled here rather than in each caller, so a button press, a
  // timer wake and a web request all behave the same. It matters most right
  // after the album is changed from the admin page: the playlist on the card
  // still describes the OLD album until this runs.
  if (allowNetwork && playlistStale()) {
    LOG.printf("[app] playlist stale (age %u min, ttl %u h) -- re-reading the album\n",
               (unsigned)playlistAgeMinutes(), (unsigned)g_cfg.playlistTtlHours);
    if (!appRefreshPlaylist() && playlistCount() == 0) return false;
    // A failed refresh with a usable old playlist is not fatal -- showing a
    // slightly stale photo beats showing an error because the server blipped.
  }

  const String assetId = playlistCurrentAsset();
  if (assetId.isEmpty()) {
    g_status.lastError = "no photos in the playlist";
    return false;
  }
  g_status.currentAsset = assetId;
  LOG.printf("[app] showing %s (%d/%u)\n", assetId.c_str(), playlistPosition(),
             (unsigned)playlistCount());

  uint8_t* frame = frameAlloc();
  if (!frame) {
    g_status.lastError = "out of memory (panel frame)";
    return false;
  }

  bool have = false;

  // ---- fast path: a previously rendered frame is on the card ---------------
  // This is what makes the prev/next keys worthwhile: no radio, no decode, just
  // a 960 kB read.
  if (cacheHas(assetId) && cacheLoadFrame(assetId, frame)) {
    have = true;
  } else if (!allowNetwork) {
    g_status.lastError = "photo not cached and the network is unavailable";
    frameFree(frame);
    return false;
  } else {
    // ---- slow path: download, decode, dither ------------------------------
    if (!netConnected() && !netConnectSta()) {
      g_status.lastError = "Wi-Fi connection failed";
      frameFree(frame);
      return false;
    }
    String err;
    if (!immichDownloadAsset(assetId, SD_PATH_TMP, err)) {
      g_status.lastError = "Immich: " + err;
      frameFree(frame);
      return false;
    }
    // The radio is the biggest avoidable draw and nothing below needs it, so
    // shut it down before the (slow) decode and the 40 s refresh -- unless the
    // settings page is open, in which case turning it off would disconnect the
    // very browser that asked for this photo.
    if (!netIsAp() && !g_status.keepNetwork) netStop();

    logMem("before decode");
    SrcImage src;
    const DecodeResult dr = decodeToRgb565(SD_PATH_TMP, decodeBudget(), &src);
    if (dr != DEC_OK) {
      // Report, do not draw: the caller owns the decision to spend a refresh on
      // an error page, and drawing here as well would cost two.
      g_status.lastError = String(decodeResultName(dr)) + " - " + decodeResultHint(dr);
      if (decodeLastDetail()[0])
        g_status.lastError += String(" (") + decodeLastDetail() + ")";
      srcFree(&src);
      frameFree(frame);
      return false;
    }
    logMem("after decode");

    RenderStats st;
    const bool ok = renderFrame(src, frame, &st);
    srcFree(&src);
    if (!ok) {
      g_status.lastError = "render failed (out of memory)";
      frameFree(frame);
      return false;
    }
    g_status.renderMs = st.ms;
    g_status.rotation = st.rotation;
    logMem("after render");

    cacheStoreFrame(assetId, frame);
    cachePrune(g_cfg.cacheFrames);
    SD.remove(SD_PATH_TMP);
    have = true;
  }

  if (!have) { frameFree(frame); return false; }

  displayPushFrame(frame);
  frameFree(frame);           // the sprite owns a copy now

  if (g_cfg.showFooter) displayFooter(footerText());

  displayUpdate();
  playlistSave();
  g_status.lastError = "";
  return true;
}

bool appRunPendingAction() {
  const PendingAction a = g_status.action;
  if (a == ACT_NONE) return false;
  // Mark busy BEFORE clearing the action, so a poll landing between the two can
  // never see an idle device and conclude the work already finished.
  g_status.busy = "Working";
  g_status.action = ACT_NONE;

  switch (a) {
    case ACT_NEXT:
      g_status.busy = "Loading the next photo";
      playlistAdvance(1);
      appShowCurrent(true);
      break;
    case ACT_PREV:
      g_status.busy = "Loading the previous photo";
      playlistAdvance(-1);
      appShowCurrent(true);
      break;
    case ACT_REFRESH:
      g_status.busy = "Refreshing the panel";
      appShowCurrent(true);
      break;
    case ACT_INDEX:
      g_status.busy = "Jumping to photo " + String(g_status.actionArg);
      playlistSetCursor(g_status.actionArg - 1);
      appShowCurrent(true);
      break;
    case ACT_PLAYLIST_REFRESH:
      g_status.busy = "Re-reading the album";
      appRefreshPlaylist();
      break;
    case ACT_CLEAR_CACHE:
      g_status.busy = "Clearing the cache";
      cacheClear();
      break;
    case ACT_LOAD_ALBUMS: {
      g_status.busy = "Loading albums";
      String json, err;
      if (!netConnected() && !netConnectSta()) {
        g_status.albumsErr = "Wi-Fi connection failed";
      } else if (immichListAlbums(json, err)) {
        g_status.albumsJson = json;
        g_status.albumsErr = "";
      } else {
        g_status.albumsErr = err;
      }
      break;
    }
    case ACT_IMMICH_TEST: {
      g_status.busy = "Testing the connection";
      String err;
      if (!netConnected() && !netConnectSta()) {
        g_status.testOk = false;
        g_status.testMsg = "Wi-Fi connection failed";
      } else if (immichTestConnection(err)) {
        g_status.testOk = true;
        g_status.testMsg = "Connected.";
      } else {
        g_status.testOk = false;
        g_status.testMsg = err;
      }
      break;
    }
    case ACT_WIFI_SCAN:
      g_status.busy = "Scanning for networks";
      g_status.scanJson = netScanJson();
      break;
    case ACT_EXIT_CONFIG:
      g_status.busy = "";
      return true;
    case ACT_REBOOT:
      g_status.busy = "Restarting";
      // Come back into the settings page rather than dropping to the slideshow,
      // so a first-time setup can carry on where it left off (now on the real
      // network instead of the setup access point).
      powerRequestConfigOnBoot();
      delay(300);
      ESP.restart();
      break;
    default:
      break;
  }
  g_status.busy = "";
  appRefreshStatus();
  return false;
}
