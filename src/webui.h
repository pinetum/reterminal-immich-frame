#pragma once
#include <Arduino.h>

// Start the admin HTTP server. Only ever called while the settings window is
// open -- in the power-saving design the device is asleep the rest of the time.
void webuiBegin();
void webuiEnd();
void webuiLoop();
