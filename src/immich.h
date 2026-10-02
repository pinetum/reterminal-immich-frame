#pragma once
#include <Arduino.h>

// All of these need Wi-Fi to be up already (see net.h) and read the server URL
// and API key from g_cfg.

// GET /api/albums -> compact JSON array for the admin page's album picker:
// [{"id":"..","name":"..","count":N}, ...]
bool immichListAlbums(String& jsonOut, String& err);

// Verify the base URL + API key actually work. Cheap: it just lists albums.
bool immichTestConnection(String& err);

// POST /api/search/metadata for the configured album, writing one asset UUID per
// line to SD_PATH_PLAYLIST. Returns the number of photos found.
//
// Paged deliberately: a 5000-photo album parsed in one go would need a JSON
// document far larger than necessary, and paging keeps peak memory flat.
int immichFetchPlaylist(String& err);

// GET /api/assets/{id}/thumbnail?size=... streamed straight to a file on SD.
bool immichDownloadAsset(const String& assetId, const char* destPath, String& err);
