#pragma once
#include <Arduino.h>

// All diagnostics go out on UART1 (GPIO43 TX / GPIO44 RX) -- that is where the
// reTerminal carrier board's USB-to-UART bridge is wired. USB CDC `Serial` only
// works with "USB CDC On Boot" enabled and is unreliable for diagnostics here.
#define LOG Serial1

void logBegin();

// Print heap / PSRAM usage. Called at every stage of the render pipeline so an
// OOM shows up in the log as a trend rather than a silent reboot.
void logMem(const char* tag);
