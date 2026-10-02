#pragma once
#include <Arduino.h>

// The playlist (one asset UUID per fixed-width record on the SD card) plus the
// play cursor. The cursor and the playlist's age live in RTC memory so they
// survive deep sleep, and are mirrored to the SD card so they also survive a
// full power cycle.

void playlistInit();

// Number of photos in the playlist on the card.
uint32_t playlistCount();

// Does the playlist need re-fetching? True when it is missing, empty, older
// than the configured TTL, or the album changed.
bool playlistStale();

// Called after a successful immichFetchPlaylist().
void playlistMarkFresh(uint32_t count);

// Account for time spent asleep so the TTL can expire without a wall clock.
void playlistAgeBy(uint32_t minutes);
uint32_t playlistAgeMinutes();

int  playlistCursor();
void playlistSetCursor(int cursor);
void playlistAdvance(int delta);

// The asset at the current cursor, honouring the shuffle setting. Empty string
// when the playlist is unusable.
String playlistCurrentAsset();

// Position shown in the UI, 1-based.
int playlistPosition();

void playlistSave();
