#include "display.h"
#include "log.h"
#include "TFT_eSPI.h"
#include <qrcode.h>

#ifndef EPAPER_ENABLE
#error "Seeed_GFX did not select an ePaper setup -- check -D BOARD_SCREEN_COMBO=523"
#endif

EPaper epaper;
static bool s_ready = false;

// How long to wait for the panel to say it is ready after reset.
static const uint32_t kBusyTimeoutMs = 2000;

bool displayInit() {
  if (s_ready) return true;
  epaper.begin();

  // The EPaper constructor allocates the 1200x1600 4bpp sprite in PSRAM. If that
  // failed, every later draw call would write through a null pointer, so catch
  // it here rather than crashing somewhere unrelated.
  if (!epaper.created()) {
    LOG.println("[epd] framebuffer not allocated -- is OPI PSRAM enabled?");
    return false;
  }

  // On the T133A01 controller BUSY *HIGH* means ready (its CHECK_BUSY waits for
  // exactly that). begin() has just reset the panel, so a healthy one releases
  // BUSY quickly; a line stuck low means power, ribbon or controller trouble.
  pinMode(PIN_EPD_BUSY, INPUT);
  const uint32_t deadline = millis() + kBusyTimeoutMs;
  while (millis() < deadline) {
    if (digitalRead(PIN_EPD_BUSY)) {
      s_ready = true;
      LOG.printf("[epd] ready (%dx%d, 6 colour)\n", EPD_WIDTH, EPD_HEIGHT);
      return true;
    }
    delay(10);
  }
  LOG.println("[epd] BUSY stuck low -- panel not responding");
  return false;
}

bool displayReady() { return s_ready; }

SPIClass& displaySpi() { return epaper.getSPIinstance(); }

void displayPushFrame(const uint8_t* frame) {
  if (!s_ready || !frame) return;
  epaper.pushImage(0, 0, PANEL_W, PANEL_H, (uint16_t*)frame);
}

void displayUpdate() {
  if (!s_ready) return;
  const uint32_t t0 = millis();
  LOG.println("[epd] refresh starting (~40 s)");
  LOG.flush();
  epaper.update();
  LOG.printf("[epd] refresh done in %lu ms\n", (unsigned long)(millis() - t0));
}

void displaySleep() {
  if (s_ready) epaper.sleep();
}

// -----------------------------------------------------------------------------
// Text pages
// -----------------------------------------------------------------------------
namespace {

// Word-wrapped text block. Returns the y below the last line drawn.
int drawWrapped(const String& text, int x, int y, int maxW, int lineH) {
  String line;
  int cy = y;
  int start = 0;
  while (start <= (int)text.length()) {
    const int sp = text.indexOf(' ', start);
    const String word = (sp < 0) ? text.substring(start) : text.substring(start, sp);
    const String probe = line.isEmpty() ? word : line + " " + word;
    if (epaper.textWidth(probe) > maxW && !line.isEmpty()) {
      epaper.drawString(line, x, cy);
      cy += lineH;
      line = word;
    } else {
      line = probe;
    }
    if (sp < 0) break;
    start = sp + 1;
  }
  if (!line.isEmpty()) { epaper.drawString(line, x, cy); cy += lineH; }
  return cy;
}

void pageHeader(const char* title, uint16_t colour) {
  epaper.fillScreen(TFT_WHITE);
  epaper.setTextDatum(TL_DATUM);
  epaper.setTextColor(colour);
  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.drawString(title, 80, 100);
  epaper.fillRect(80, 170, PANEL_W - 160, 4, colour);
}

// Draw a QR code scaled to roughly `targetPx` across, centred on x.
void drawQr(const char* text, int cx, int y, int targetPx) {
  QRCode qr;
  // Version 5 at ECC_LOW holds 106 alphanumeric characters -- far more than any
  // "http://<ip-or-host>/" we will ever produce.
  //
  // ricmoo/QRCode's qrcode_getBufferSize() is a runtime FUNCTION, not a macro, so
  // it cannot size an array. The arithmetic is the same: a version-N code is
  // 4N+17 modules per side, one bit each, plus the library's one spare byte.
  static constexpr int kQrVersion  = 5;
  static constexpr int kQrModules  = 4 * kQrVersion + 17;                      // 37
  static constexpr int kQrBufBytes = ((kQrModules * kQrModules) + 7) / 8 + 1;  // 173
  static uint8_t buf[kQrBufBytes];
  if (qrcode_initText(&qr, buf, kQrVersion, ECC_LOW, text) != 0) {
    LOG.println("[epd] QR encode failed");
    return;
  }
  const int modules = qr.size;
  int s = targetPx / modules;
  if (s < 2) s = 2;
  const int side = modules * s;
  const int x0 = cx - side / 2;

  // Quiet zone: the code is unscannable without it.
  const int q = 4 * s;
  epaper.fillRect(x0 - q, y - q, side + 2 * q, side + 2 * q, TFT_WHITE);
  for (int my = 0; my < modules; ++my)
    for (int mx = 0; mx < modules; ++mx)
      if (qrcode_getModule(&qr, mx, my))
        epaper.fillRect(x0 + mx * s, y + my * s, s, s, TFT_BLACK);
}

}  // namespace

void displayFooter(const String& text) {
  if (!s_ready) return;
  const int h = 54;
  const int y = PANEL_H - h;
  epaper.fillRect(0, y, PANEL_W, h, TFT_WHITE);
  epaper.setTextDatum(ML_DATUM);
  epaper.setTextColor(TFT_BLACK);
  epaper.setFreeFont(&FreeSans9pt7b);
  epaper.drawString(text, 24, y + h / 2);
}

void displayError(const char* title, const String& detail, const char* hint) {
  if (!s_ready) return;
  pageHeader(title, TFT_RED);
  epaper.setTextColor(TFT_BLACK);
  epaper.setFreeFont(&FreeSans18pt7b);
  int y = drawWrapped(detail, 80, 240, PANEL_W - 160, 46);
  if (hint && *hint) {
    epaper.setFreeFont(&FreeSans12pt7b);
    drawWrapped(String(hint), 80, y + 40, PANEL_W - 160, 34);
  }
  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.setTextColor(TFT_BLACK);
  epaper.drawString("Hold the Refresh key for 2 s to open the settings page.",
                    80, PANEL_H - 120);
}

void displayLowBattery(int percent, float volts) {
  if (!s_ready) return;
  pageHeader("Battery low", TFT_RED);
  epaper.setTextColor(TFT_BLACK);
  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.drawString(String(percent) + "%   (" + String(volts, 2) + " V)", 80, 250);
  epaper.setFreeFont(&FreeSans12pt7b);
  drawWrapped("The slideshow is paused to protect the battery. Connect USB-C to charge; "
              "it resumes on its own once charged.",
              80, 340, PANEL_W - 160, 34);
}

void displayConfigScreen(const String& url, const String& ssid, const String& pass,
                         bool apMode, uint32_t windowMinutes) {
  if (!s_ready) return;
  pageHeader("Settings", TFT_BLUE);

  epaper.setTextColor(TFT_BLACK);
  epaper.setFreeFont(&FreeSans18pt7b);
  int y = 250;

  if (apMode) {
    epaper.drawString("1.  Join this Wi-Fi network:", 80, y); y += 52;
    epaper.setFreeFont(&FreeSansBold24pt7b);
    epaper.setTextColor(TFT_BLUE);
    epaper.drawString(ssid, 130, y); y += 60;
    if (pass.length()) {
      epaper.setFreeFont(&FreeSans18pt7b);
      epaper.setTextColor(TFT_BLACK);
      epaper.drawString("password:  " + pass, 130, y); y += 56;
    }
    epaper.setFreeFont(&FreeSans18pt7b);
    epaper.setTextColor(TFT_BLACK);
    epaper.drawString("2.  Open this address:", 80, y); y += 52;
  } else {
    epaper.drawString("On the " + ssid + " network, open:", 80, y); y += 56;
  }

  epaper.setFreeFont(&FreeSansBold24pt7b);
  epaper.setTextColor(TFT_BLUE);
  epaper.drawString(url, 130, y); y += 80;

  drawQr(url.c_str(), PANEL_W / 2, y + 40, 520);

  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.setTextColor(TFT_BLACK);
  epaper.setTextDatum(TC_DATUM);
  epaper.drawString("Settings stay open for " + String(windowMinutes) +
                        " minutes, then the slideshow resumes.",
                    PANEL_W / 2, PANEL_H - 160);
  epaper.drawString("Hold Refresh for 2 s to leave now.", PANEL_W / 2, PANEL_H - 120);
  epaper.setTextDatum(TL_DATUM);
}
