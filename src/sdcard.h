#pragma once
#include <Arduino.h>

// Paths used on the card.
#define SD_DIR_ROOT     "/immich"
#define SD_DIR_CACHE    "/immich/cache"
#define SD_PATH_TMP     "/immich/tmp.img"
#define SD_PATH_PLAYLIST "/immich/playlist.txt"
#define SD_PATH_STATE   "/immich/state.txt"

// The playlist is stored as FIXED-WIDTH records so that "show photo #n" is a
// single seek instead of a scan of the whole file: 36 characters of UUID,
// space-padded if shorter, plus a newline.
#define PLAYLIST_ID_LEN 36
#define PLAYLIST_REC    37

// Mount the card. MUST be called after displayInit(), because the card shares
// the SPI bus with the ePaper and we reuse the SPI instance the panel driver
// created. See the comment in sdcard.cpp.
bool sdBegin();
bool sdReady();
bool sdCardPresent();
uint64_t sdSizeMB();

// ---- rendered-frame cache --------------------------------------------------
// A cache entry is a ready-to-push packed 4bpp panel frame (960 kB). Hitting the
// cache skips the network AND the decode+dither, which is what makes the
// previous/next buttons usable on a battery-powered, deep-sleeping device.
bool cacheHas(const String& assetId);
bool cacheLoadFrame(const String& assetId, uint8_t* frame);
bool cacheStoreFrame(const String& assetId, const uint8_t* frame);
void cacheTouch(const String& assetId);
void cachePrune(uint32_t keep);
uint32_t cacheCount();
void cacheClear();
