#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "pins.h"

// Bring the panel up. Returns false if the framebuffer could not be allocated
// (PSRAM misconfigured) or the panel never reports ready.
bool displayInit();
bool displayReady();

// The SPI instance Seeed_GFX created. The SD card has to reuse it because both
// devices sit on the same bus.
SPIClass& displaySpi();

// Blit a packed 4bpp panel-sized frame into the sprite. Does NOT refresh.
void displayPushFrame(const uint8_t* frame);

// Clock the sprite out to the panel. Blocks for roughly 40 seconds -- that is
// what a full-colour Spectra 6 refresh costs, and there is no partial mode.
void displayUpdate();

// Power the panel down. Always call before deep sleep; the image stays visible.
void displaySleep();

// Overlay a one-line footer on the current sprite contents (call between
// displayPushFrame and displayUpdate).
void displayFooter(const String& text);

// ---- full-screen informational pages ---------------------------------------
void displayError(const char* title, const String& detail, const char* hint);
void displayLowBattery(int percent, float volts);

// The config-mode page: shows how to reach the admin UI, with a QR code.
// `apMode` selects the "join this access point" wording over "on your network".
void displayConfigScreen(const String& url, const String& ssid, const String& pass,
                         bool apMode, uint32_t windowMinutes);
