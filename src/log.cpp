#include "log.h"
#include "pins.h"

void logBegin() {
  LOG.begin(115200, SERIAL_8N1, PIN_SERIAL_RX, PIN_SERIAL_TX);
  // The USB-UART bridge needs a moment after a reset or the first lines are lost.
  delay(300);
  LOG.println();
}

void logMem(const char* tag) {
  LOG.printf("[mem] %-20s heap=%u kB  psram=%u/%u kB\n", tag,
             (unsigned)(ESP.getFreeHeap() / 1024),
             (unsigned)(ESP.getFreePsram() / 1024),
             (unsigned)(ESP.getPsramSize() / 1024));
}
