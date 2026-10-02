# reTerminal E1004 — Immich Photo Frame Firmware

*English · [繁體中文](README.zh-TW.md)*

Turns a Seeed Studio reTerminal E1004 (13.3" full-colour E Ink Spectra 6,
1200×1600) into an Immich digital photo frame: it pulls photos from an album of
your choice, cycles through them in order or shuffled, lets you page through
with the front keys, and is configured entirely from a built-in web page.

Designed around **battery life**: the device is asleep nearly all the time and
does exactly one thing each time it wakes.

> ### ⚡ [Flash it from your browser](https://pinetum.github.io/reterminal-immich-frame/)
>
> <https://pinetum.github.io/reterminal-immich-frame/> — plug the board in over USB-C and
> click one button. No PlatformIO, no toolchain. Needs Chrome, Edge or Opera on
> a desktop (Web Serial).

---

## Features

- Reads the photo list for one Immich album over the API and rotates through it
  on a timer (interval set in minutes)
- Three front keys: next / previous / refresh; hold Refresh to open the settings
  page
- Web admin UI: Wi-Fi, Immich server and API key, **album dropdown**, interval,
  shuffle, image size, dithering algorithm, brightness, orientation, crop mode,
  low-battery threshold, and more
- Full-resolution 1200×1600 colour Floyd–Steinberg error diffusion (see
  [Why this looks better than the vendor sample](#why-this-looks-better-than-the-vendor-sample))
- Automatic photo orientation: landscape photos are rotated to 1600×1200,
  portrait photos use the panel's native orientation
- SD frame cache: a photo you have already seen redisplays with no network and
  no decode
- Pauses by itself on low battery, and says so on screen

---

## Hardware

| | |
|---|---|
MCU | ESP32-S3, dual Xtensa LX7 @240 MHz
Memory | 8 MB OPI PSRAM, 32 MB flash
Display | 13.3" E Ink Spectra 6, 1200×1600, 6 colours (white/green/red/yellow/blue/black), ~40 s full refresh
Storage | microSD, FAT32, 64 GB or smaller
Battery | 5000 mAh, charges over USB-C 5 V/1 A
Also on board | SHT40 temperature/humidity, PCF8563 RTC, buzzer, status LED

### Pins

The full table is in [`include/pins.h`](include/pins.h); the ones that matter:

| Function | GPIO |
|---|---|
ePaper SPI | SCK 7, MISO 8, MOSI 9, CS 10, CS2 2, DC 11, EN 12, BUSY 13, RST 38
microSD (**shares the display's SPI bus**) | CS 14, DET 15, EN 16
Keys (active-LOW, pulled up in hardware) | KEY0 `3`, KEY1 `4`, KEY2 `5`
LED (inverted — LOW is on) | 48
Buzzer | 45
Battery | ADC 1, enable 21 (VBAT = ADC × 2.0)
Debug UART | TX 43, RX 44 @115200

> **Logs go out on `Serial1` (GPIO43/44)**, not the USB CDC `Serial`. The carrier
> board's USB-to-UART bridge is wired to UART1; USB CDC needs to be enabled
> separately and is unreliable on this board.

---

## Building and flashing

Only needed if you want to change the code — to just install the firmware, use
the [browser flasher](https://pinetum.github.io/reterminal-immich-frame/) or the images
attached to any [release](https://github.com/pinetum/reterminal-immich-frame/releases).

You need [PlatformIO](https://platformio.org/). If you do not have it:

```bash
# pick one
brew install platformio
pipx install platformio
python3 -m pip install --user platformio
```

Then:

```bash
pio run                      # build (first run fetches the libraries)
pio run -t upload            # flash
pio device monitor -b 115200 # watch the log
```

If flashing will not start: hold **Boot**, tap **Reset**, release Boot, and run
`pio run -t upload` again.

Two settings in `platformio.ini` **must not be changed**:

```ini
board_build.arduino.memory_type = qio_opi   ; without OPI PSRAM the panel buffer fails to allocate
build_flags = -D BOARD_SCREEN_COMBO=523     ; selects the E1004's T133A01 panel driver
```

### Verified build environment

This code builds with zero warnings against:

```
PlatformIO Core 6.2.0
espressif32 7.1.3  →  framework-arduinoespressif32 2.0.17
Seeed_GFX 2.0.3        (the E1004's T133A01 driver needs 2.0.3 or newer)
JPEGDEC 1.8.4          (vendored and patched, see lib/JPEGDEC/PATCHES.md)
PNGdec 1.1.6
ArduinoJson 7.4.3      QRCode 0.0.1
AsyncTCP 3.5.0         ESPAsyncWebServer 3.12.1
```

```
RAM   37.2%  (121 984 / 327 680 bytes)   ← internal RAM, excludes the 8 MB PSRAM
Flash 36.3%  (1 214 797 / 3 342 336 bytes)
```

The code works on both Arduino core 2.x and 3.x. The only difference that bites
is `File::name()`, which returns a full path on 2.x and a bare filename on 3.x;
`cacheEntryPath()` in [`src/sdcard.cpp`](src/sdcard.cpp) handles both.

---

## Hardware self-test (optional, but worth running on a fresh board)

A standalone probe sketch checks the LED, buzzer, battery ADC and SD detect one
by one, and **watches every edge on GPIO0–21**:

```bash
pio run -e gpio_probe -t upload
pio device monitor -b 115200
```

Then press all five keys in turn (front left / right / Refresh, plus the **two
page-turn keys on the back**) and watch which pin moves.

The reason this exists: Seeed's documentation only says KEY0–KEY2 are GPIO3/4/5
and correspond to the **front** keys. It **does not say where the two rear
page-turn keys are wired** — most likely in parallel with KEY0/KEY1, but that is
a guess. If the probe shows they are on their own pins, add them to
[`include/pins.h`](include/pins.h) and to the
`esp_sleep_enable_ext1_wakeup()` wake mask in [`src/power.cpp`](src/power.cpp).

The probe pulls in no libraries at all, so it builds in seconds. Run
`pio run -t upload` afterwards to put the real firmware back.

---

## First-time setup

1. Insert a FAT32 microSD card (64 GB or smaller) and plug in USB-C
2. On the first boot after flashing the device has no configuration, so it
   enters setup mode by itself and shows on screen:
   - Wi-Fi name `reTerminal-E1004-XXXX`, password `immich1004`
   - the URL `http://192.168.4.1` and a **QR code**

   (expect to wait ~40 s for that screen — that is how long an E Ink full
   refresh takes)
3. Join that Wi-Fi from your phone, scan the QR code or open `http://192.168.4.1`
4. Press **Scan**, pick your home Wi-Fi, enter the password
5. Enter the Immich server URL (e.g. `http://192.168.1.50:2283`, no `/api`
   suffix) and an API key (Immich → Account Settings → API Keys)
6. Press **Save & reconnect**. The device reboots onto your network and
   **returns to the settings page by itself**
7. **Test connection** should now succeed, and **Load albums** fills the album
   dropdown
8. Set the interval and press **Save & resume slideshow**

To get back into settings later: **hold the front Refresh key for 2 seconds**
(it beeps). The screen shows the URL and a QR code, and the window stays open
for 10 minutes by default.

---

## Keys

| Key | GPIO | While asleep | In setup mode |
|---|---|---|---|
KEY0 (right) | 3 | Next photo | — |
KEY1 (left) | 4 | Previous photo | — |
KEY2 (Refresh) | 5 | Tap: redraw the current photo<br>**Hold 2 s: open settings** | Hold 2 s: leave settings |

> A new photo takes about 40 seconds to appear after a key press — E Ink Spectra
> 6 has no partial-refresh mode, and that is simply what a full-colour update
> costs. The beep and the LED fire immediately so you know the press landed.

---

## Battery life

One photo change is roughly 50–60 seconds awake (Wi-Fi 3 s + download 2 s +
decode and dither ~10 s + panel refresh 40 s), about **2.5 mAh**.

| Interval | Per day | Roughly, on 5000 mAh |
|---|---|---|
10 minutes | ~360 mAh | 2 weeks
1 hour | ~60 mAh | 2–3 months
6 hours | ~10 mAh | 6 months or more

A few of the things that buy that: Wi-Fi is **shut down the moment the download
finishes** (neither decoding nor refreshing needs it); an SD cache hit never
turns the radio on at all; and below the battery threshold the warning screen is
drawn once, after which the device wakes hourly to check and goes straight back
to sleep without redrawing.

---

## Why this looks better than the vendor sample

Seeed's own E1004 image pipeline
(`Seeed_GFX/examples/.../reTerminal_E1004_SDcard_Color6`) processes a whole
image at once: it needs a `width × height` index buffer, and error diffusion on
top of that needs a `width × height × 3 × int16` error buffer. At 1200×1600 that
error buffer alone is about **11 MB**, which an 8 MB module cannot allocate — so
`dither_image()` **silently degrades to no dithering at all**. The vendor sample
says as much in its own comments, which is why it defaults to ordered Bayer8.

This firmware streams **one destination row at a time**
([`src/e6_dither.cpp`](src/e6_dither.cpp), [`src/render.cpp`](src/render.cpp))
and keeps only **3 rows** of error — the deepest kernels (Jarvis, Atkinson)
diffuse as far as dy=+2 — which is about **29 KB** instead of 11 MB. That is
what makes real Floyd–Steinberg affordable at full resolution.

Peak memory looks roughly like this:

```
decoded source image (RGB565)   ≤ 4.0 MB
our packed-4bpp frame             0.94 MB
Seeed_GFX's panel sprite          0.94 MB
3 error rows + row scratch       ~0.04 MB
                               ------------
                                ~6 MB of 8 MB
```

RGB565 rather than RGB888 for the source is deliberate: it halves the single
largest allocation, and since the panel has only six colours the 5/6/5
quantisation is invisible once dithered.

JPEG decoding uses JPEGDEC's 1/2, 1/4 and 1/8 **decode-time downscale**: the
header is read first and the best quality that fits the budget is chosen. That
is why pointing the image size at `original` and pulling a 48 MP file does not
OOM the device.

JPEGDEC here is a **patched copy**, vendored in `lib/JPEGDEC/` (which is why it
is absent from `lib_deps` in `platformio.ini`). Stock JPEGDEC cannot decode the
previews Immich generates: Immich's sharp is linked against mozjpeg, which
optimises its Huffman tables per component and emits three AC tables, so the
frame header is written as SOF1 (extended sequential) instead of SOF0 — and
stock JPEGDEC fails at `open()` with `JPEG_UNSUPPORTED_FEATURE`. The three
patches and the reasoning behind them are in
[`lib/JPEGDEC/PATCHES.md`](lib/JPEGDEC/PATCHES.md), and
`test/host/test_jpeg.cpp` verifies them.

Before handing a file to the decoder, `decode_jpeg.cpp` walks the marker
segments itself and applies exactly the same acceptance tests JPEGDEC does, so a
failure logs the real cause (which SOF, which Huffman table) instead of a guess.
`tools/jpegprobe.py` runs the same checks on your computer:

```bash
python3 tools/jpegprobe.py photo.jpg
```

---

## Settings worth knowing about

Every field is explained on the page itself; these are the ones with real
trade-offs:

| Setting | Notes |
|---|---|
**Image size** | Which rendition to ask Immich for. The default `preview` (1440 px on the long edge) is the right answer. `original` looks best but is slowest and may trigger the decode-time downscale. |
**Orientation** | `Automatic` rotates landscape photos to 1600×1200 and assumes you turned the frame **clockwise** to hang it landscape. If photos come out upside down, pick the other landscape option. |
**Dithering** | `Floyd-Steinberg` is the recommendation. `Jarvis` is smoother but slower; `Atkinson` has more contrast because it deliberately discards 2/8 of the error. |
**Brightness (gamma)** | Above 1.0 brightens. Try 1.2 if photos look muddy and dark on the panel. |
**Cached frames** | 960 KB each. A cached photo redisplays with no network and no decode, which makes a big difference to how the keys feel. |

Secret fields (Wi-Fi password, API key, admin password) are **never** sent back
to the browser; leaving one blank means "keep the current value".

---

## Tests

The purely computational parts (the ditherer, the scale/rotate/pack renderer,
the JPEG patches) run on your computer — no device, no PlatformIO:

```bash
./test/host/run.sh
```

Four suites:

- `test_stream` — proves the streaming 3-row error buffer is **pixel-for-pixel
  identical** to a whole-image reference implementation (FS, Jarvis and Atkinson
  all covered). This is the one that would catch a buffer-rotation bug.
- `test_dither` — colour reconstruction accuracy per algorithm. Note that
  Spectra 6 has no grey, so the nearest colour to mid-grey is actually *green*;
  the correct test is therefore that the *area-averaged RGB* comes back close to
  the input, not the ratio of black to white dots. Measured mean error for FS is
  0.4–5.0 out of 255.
- `test_render` — coordinate mapping for all four rotations, cover filling the
  canvas completely, contain's letterbox proportions, and 4bpp nibble packing
  order.
- `test_jpeg` — proves the `lib/JPEGDEC` patches do what they claim: a baseline
  JPEG and a variant carrying the *same* entropy-coded data behind an SOF1
  header with a third AC Huffman table must decode to identical pixels. Without
  the patches the second one fails at `open()`.

`test/host/Arduino.h` is a minimal stand-in for the Arduino API, just enough for
those files to compile on a desktop.

---

## Cutting a release

```bash
./tools/package-release.sh v1.0.0
```

That builds the firmware and fills `dist/` with everything a release needs: the
four flash images, a **merged image** covering the flash from 0x0 (so someone
who has never installed PlatformIO can flash with a single esptool command, and
so ESP Web Tools can serve it), the ELF for decoding panic backtraces, a
`SHA256SUMS`, and a `FLASHING.md` with the exact offsets.

The ESP32-S3 bootloader lives at **0x0**, not at 0x1000 as on the original
ESP32. The script reads the offsets out of the build environment rather than
hard-coding them from memory
(`pio run -t envdump | grep -A6 FLASH_EXTRA_IMAGES`).

Then, with the [GitHub CLI](https://cli.github.com/):

```bash
gh release create v1.0.0 dist/* --title "v1.0.0" --notes-file dist/FLASHING.md
```

`dist/` is gitignored.

---

## Browser flasher (GitHub Pages)

`docs/` is a one-page [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
installer, published by `.github/workflows/pages.yml` whenever a release is
published. Someone who has never heard of PlatformIO can open the page in
Chrome, plug the board in, and flash it.

It is live at **<https://pinetum.github.io/reterminal-immich-frame/>**.


If that first run fails with a 403 instead, the repository has Actions blocked
from managing Pages: **Settings → Pages → Source → GitHub Actions** does the
same thing by hand, once.

### Why the page does not fetch the release asset directly

It would be neater for the page to pull the binary from the release at load
time, and that does not work: GitHub serves release assets from
`release-assets.githubusercontent.com` with **no `access-control-allow-origin`
header**, so the browser blocks the cross-origin fetch. Routing through
`api.github.com/repos/.../releases/assets/<id>` does not help either — the API's
302 does carry `access-control-allow-origin: *`, but the asset host it redirects
to does not, and the browser checks the final response.

So the workflow does it server-side instead, where CORS does not exist: it
downloads `*-merged.bin` from the release with `gh release download`, writes
`manifest.json` beside it, and deploys both with the page. The binary is then
same-origin and fetches cleanly. The flash source really is the release
attachment — it is just resolved at deploy time rather than at page load.

To republish an older tag, or to rebuild the page after editing `docs/`, run the
workflow manually (**Actions → Deploy web flasher → Run workflow**) and give it
a tag.

---

## Architecture

```
src/
  main.cpp       power state machine: why did we wake → do one thing → sleep
  app.cpp        orchestrates "show this photo" end to end, plus the web action queue
  power.cpp      battery, LED, buzzer, long-press detection, deep sleep (timer + ext1 key wake)
  display.cpp    EPaper init and refresh; status, error and setup screens (incl. QR code)
  sdcard.cpp     shared-SPI mount order, frame cache and LRU eviction
  net.cpp        Wi-Fi STA / SoftAP + captive portal
  immich.cpp     album listing, playlist fetch, streaming photo download
  playlist.cpp   on-SD playlist, cursor, shuffle
  decode.cpp     format sniffing (magic bytes, not extensions), shared file access, allocation
  decode_jpeg.cpp  header probe + JPEGDEC with the decode-time downscale guard → PSRAM RGB565
  decode_png.cpp   PNGdec → PSRAM RGB565
  render.cpp     streaming scale + rotate + dither → packed 4bpp
  e6_dither.cpp  Spectra 6 palette and streaming error diffusion
  webui.cpp      non-blocking admin API
  web_assets.h   the admin page (HTML/CSS/JS, in PROGMEM)
  tools/
    gpio_probe.cpp  standalone hardware self-test (separate PlatformIO env, never in the firmware)
include/         pins.h, settings.h, e6_dither.h, log.h
lib/JPEGDEC/     the patched JPEGDEC (see PATCHES.md)
test/host/       desktop tests
tools/
  jpegprobe.py   desktop JPEG header check, same rules as the firmware's probe
```

A few decisions that are not obvious but matter:

**The SD card and the display share one SPI bus.** You must call
`epaper.begin()` first and then hand the `SPIClass` instance the display driver
created to `SD.begin()` (see [`src/sdcard.cpp`](src/sdcard.cpp)). Creating your
own SPIClass fights the display for the bus, and that is the single most common
reason SD fails to mount on this board. The SD card must also not be touched
during the 40 seconds a panel refresh takes.

**Nothing slow happens inside a web handler.** ESPAsyncWebServer handlers run on
the AsyncTCP task, which also services lwIP callbacks — a blocking socket call
in there (reaching Immich, scanning for Wi-Fi) can deadlock the network stack,
and touching the SD card races the main task. So each handler records the intent
in `g_status.action` and returns 202; the actual work happens in `loop()` in
`main.cpp` and the page polls `/api/status`.

**The two decoders have to be separate translation units.** JPEGDEC and PNGdec
both define helper macros with the same names but different definitions
(`INTELSHORT`, `MOTOSHORT`, `MOTOLONG`, …), so including both in one .cpp
produces a pile of redefinition warnings. Hence `decode_jpeg.cpp` and
`decode_png.cpp`, with [`src/decode_internal.h`](src/decode_internal.h) holding
only the little they share.

**Rotation does not use `setRotation()`.** It lives in the destination → source
coordinate mapping, which makes it a free index transform, and error diffusion
still runs along the **destination** scan order — which is the order the eye
reads the image in, and therefore the correct one.

---

## One Immich API trap

The photo list comes from `POST /api/search/metadata` with `albumIds` in the
body, **not** from `GET /api/albums/{id}`. The reason: newer Immich versions
dropped the `assets` array from `AlbumResponseDto` entirely, leaving only
`assetCount`.

The `albumIds` / `page` / `type` / `order` fields have been marked deprecated
since Immich v3.2, but they still work and they are the only formulation that
works on both v1.9x and v3.x. If a future v4 removes them, switch to cursor
pagination (`query.cursor` / `nextCursor`) — [`src/immich.cpp`](src/immich.cpp)
has a comment marking the spot.

---

## Troubleshooting

| Symptom | What to check |
|---|---|
Nothing on screen, log says `framebuffer not allocated` | `board_build.arduino.memory_type = qio_opi` did not take effect; `pio run -t clean` and rebuild |
Log says `BUSY stuck low` | The panel is not responding: check the ribbon cable and power |
`SD.begin FAILED` | Card must be FAT32, 64 GB or smaller; make sure it is seated; the log prints what SD_DET reads |
Photo shows `WEBP NOT SUPPORTED` | Immich's thumbnail format is set to WebP. Switch it to JPEG under Administration → Settings → Image Settings |
Photo shows `DECODE FAILED` | The log prints the exact reason (`[dec] cannot decode: ...`). Usually a progressive JPEG: turn **Progressive** off under Administration → Settings → Image Settings and re-run Generate Thumbnails. To confirm first, download the file and run `python3 tools/jpegprobe.py <file.jpg>` |
Photo shows `IMAGE TOO LARGE` | Switch the image size to `preview` (PNG has no decode-time downscale) |
`HTTP 401` / `API key rejected` | Wrong API key, or stray whitespace got pasted with it |
`HTTP 404` | Wrong server URL or album UUID. The URL is just the origin — do not append `/api` |
Landscape photos are upside down | Pick the other landscape option under Orientation |
Cannot reach the settings page | The settings window (10 minutes by default) expired; hold Refresh for 2 s to reopen it |
Every photo takes ages | Expected. An E Ink Spectra 6 full refresh is ~40 s and there is no partial refresh |

The fastest way to diagnose anything is the log: `pio device monitor -b 115200`.
Memory use is printed at every stage, so an OOM shows up as a trend rather than
a sudden reboot.

---

## Known limitations and trade-offs

1. **The 40-second full refresh is a hardware limit.** Spectra 6 has no partial
   refresh, so paging with the keys always means waiting. The SD frame cache
   saves the download and the decode (~12 s), but not the refresh.
2. **The admin page is not always available.** That is the direct cost of the
   sleep schedule. It is made workable by "hold Refresh to enter settings" plus
   showing the URL and a QR code right on the panel.
3. **No WebP, no progressive JPEG.** Immich's thumbnail format defaults to JPEG
   but can be switched to WebP, and the preview has a Progressive toggle. Both
   produce a clear on-screen error with the fix, rather than failing silently.
   Baseline and SOF1 (extended sequential) are both supported.
4. **HTTPS does not verify certificates by default** (`setInsecure()`). A
   pragmatic choice for self-signed home setups; there is no trust store on the
   device. Paste a CA PEM into the settings (`caPem`) to turn verification on.
5. **On-screen text is ASCII only.** Seeed_GFX's built-in fonts have no CJK
   glyphs, so the device screens (errors, setup, low battery) are in English.
   The web UI is not affected.
6. **The rear page-turn keys' GPIOs are unconfirmed.** The Seeed wiki documents
   KEY0–KEY2 as the *front* keys only and never says where the two rear keys go
   (quite possibly in parallel with KEY0/KEY1). The firmware currently handles
   GPIO 3/4/5 only. If the rear keys turn out to be separate, add them to
   `include/pins.h` and to the `esp_sleep_enable_ext1_wakeup()` mask in
   `src/power.cpp`.
7. **There is no wall clock.** No NTP; playlist age is estimated by accumulating
   sleep time, which is accurate enough to expire a daily TTL.

---

## References

- [Getting started with reTerminal E1004](https://wiki.seeedstudio.com/getting_started_with_reterminal_e1004/)
- [Arduino Cookbook: Onboard Peripherals](https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino_peripherals_2/) — the source for the key, LED, battery and SD pins
- [Seeed_GFX](https://github.com/Seeed-Studio/Seeed_GFX) — the `EPaper` class and `Setup523`
- [Immich API docs](https://api.immich.app/endpoints)
