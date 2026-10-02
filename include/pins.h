#pragma once
// =============================================================================
//  reTerminal E1004 pin map
//
//  Verified against:
//   - Seeed wiki "Arduino Cookbook: Onboard Peripherals (reTerminal E Series)"
//     (buttons GPIO3/4/5, LED GPIO48 on E1004, buzzer GPIO45, battery
//      GPIO1 ADC + GPIO21 enable, SD CS/DET/EN = 14/15/16)
//   - Seeed_GFX User_Setups/Setup523_Seeed_reTerminal_E1004.h (ePaper SPI)
//   - the official Seeed_GFX example reTerminal_E1004_SDcard_Color6
// =============================================================================

// ---- ePaper (T133A01, dual-chip controller) ---------------------------------
// These must match Setup523 inside Seeed_GFX. Listed here for the few places we
// touch the pins directly (BUSY health check) and for documentation.
#define PIN_EPD_SCK    7
#define PIN_EPD_MISO   8
#define PIN_EPD_MOSI   9
#define PIN_EPD_CS    10
#define PIN_EPD_CS2    2   // second controller half (right side of the panel)
#define PIN_EPD_DC    11
#define PIN_EPD_EN    12
#define PIN_EPD_BUSY  13   // NOTE: on T133A01 BUSY *HIGH* means ready
#define PIN_EPD_RST   38

// ---- microSD (shares the SPI bus with the ePaper; separate CS) --------------
#define PIN_SD_SCK     7
#define PIN_SD_MISO    8
#define PIN_SD_MOSI    9
#define PIN_SD_CS     14
#define PIN_SD_DET    15   // LOW = card present
#define PIN_SD_EN     16   // HIGH = power the slot (E1003 differs: GPIO39)

// ---- Front panel keys ------------------------------------------------------
// Active LOW. The board already has hardware pull-ups, so plain INPUT is
// correct -- INPUT_PULLUP is harmless but unnecessary.
// All three are within GPIO0-21, so all three are valid ext1 deep-sleep wake
// sources on the ESP32-S3.
#define PIN_KEY0       3   // E1004: front "right"   -> next photo
#define PIN_KEY1       4   // E1004: front "left"    -> previous photo
#define PIN_KEY2       5   // E1004: front "refresh" -> short: redraw, long: config mode

// ---- Indicators ------------------------------------------------------------
#define PIN_LED       48   // E1004 specific (E1001/2 use 6, E1003 uses 16). LOW = on.
#define PIN_BUZZER    45

// ---- Battery ---------------------------------------------------------------
#define PIN_BATTERY_ADC  1
#define PIN_BATTERY_EN  21   // HIGH enables the divider; VBAT = ADC * 2.0
#define BATTERY_DIVIDER 2.0f

// ---- I2C0 (SHT40 @0x44 temp/humidity, PCF8563 RTC @0x51) -------------------
// Not used by the slideshow itself; wired up here for future use.
#define PIN_I2C_SDA   19
#define PIN_I2C_SCL   20

// ---- Debug UART ------------------------------------------------------------
#define PIN_SERIAL_RX 44
#define PIN_SERIAL_TX 43

// ---- Panel geometry --------------------------------------------------------
// Setup523 already provides EPD_WIDTH / EPD_HEIGHT; these mirror them so code
// that does not include TFT_eSPI.h can still do geometry maths.
#define PANEL_W 1200
#define PANEL_H 1600
// Packed 4bpp: two pixels per byte, so one panel row is PANEL_W/2 bytes.
#define PANEL_STRIDE (PANEL_W / 2)
#define PANEL_FRAME_BYTES ((size_t)PANEL_STRIDE * PANEL_H)   // 960000
