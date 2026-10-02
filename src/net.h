#pragma once
#include <Arduino.h>

#define AP_PASSWORD "immich1004"

// Join the configured network. Returns false on timeout. Radio is left on.
bool netConnectSta(uint32_t timeoutMs = 30000);

// Start the setup access point plus a catch-all DNS server (captive portal).
// Fills in the SSID that was actually created.
void netStartAp(String& ssidOut);

// Pump the captive-portal DNS server. Call from loop() while in AP mode.
void netLoop();

// Turn the radio off completely. Worth doing before the 40 s panel refresh and
// before sleeping: Wi-Fi is the single biggest avoidable current draw here.
void netStop();

bool   netIsSta();
bool   netIsAp();
bool   netConnected();
String netIp();
int    netRssi();
String netSsid();

// Scan for networks; returns a JSON array string [{"ssid":..,"rssi":..,"lock":bool}].
String netScanJson();
