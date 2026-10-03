#!/usr/bin/env bash
# Host-side tests for the parts of the firmware that are pure computation: the
# calibrated palettes, the 6-colour ditherer, the pre-dither processing stages
# and the scale/rotate/pack renderer.
#
# These run on your Mac/PC with plain g++ -- no device, no PlatformIO, no
# toolchain. Arduino.h here is a small shim (see Arduino.h in this folder) that
# provides just enough of the Arduino API for those files to compile.
#
#   ./test/host/run.sh
set -euo pipefail
cd "$(dirname "$0")/../.."

CXX=${CXX:-g++}
FLAGS="-std=gnu++17 -O2 -Wall -Itest/host -Iinclude -Isrc"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

run() {
  local name=$1; shift
  printf '\n=== %s ===\n' "$name"
  $CXX $FLAGS -o "$OUT/$name" "$@" test/host/stub.cpp
  "$OUT/$name"
}

# Everything the renderer and the processing stages are built out of. Listed
# once because almost every test below needs all of them.
CORE="src/e6_dither.cpp src/palette.cpp src/colorspace.cpp src/imgproc.cpp"

# The calibrated palette tables, checked digit by digit against the hex quoted
# in paperlesspaper/epdoptimize and aitjcize/esp32-photoframe, plus the hex
# parsing the admin page's six custom fields feed into them.
run test_palette test/host/test_palette.cpp src/palette.cpp src/colorspace.cpp

# Proves the streaming 3-row error buffer is exactly equivalent to a
# whole-image implementation, for all twelve kernels and both scan directions.
# This is the one that would catch a buffer rotation bug.
run test_stream test/host/test_stream.cpp $CORE

# Pre-dither processing: tone mapping, dynamic-range compression, clarity and
# paper normalisation, each against an independent reimplementation of the
# reference code rather than against itself.
run test_imgproc test/host/test_imgproc.cpp $CORE

# Colour reconstruction accuracy of each kernel, palette and matching mode --
# and the regression guard that the DEFAULT configuration still renders exactly
# what it rendered before calibrated palettes existed.
run test_dither test/host/test_dither.cpp src/render.cpp $CORE

# Rotation mapping, cover/contain framing, 4bpp nibble packing, letterbox
# colour, the preview downsample and the calibration chart.
run test_render test/host/test_render.cpp src/render.cpp $CORE

# The vendored JPEGDEC patches: Immich's previews are SOF1 with three AC
# Huffman tables, which stock JPEGDEC rejects. See lib/JPEGDEC/PATCHES.md.
printf '\n=== test_jpeg ===\n'
$CXX $FLAGS -Ilib/JPEGDEC/src -Wno-unused-function -o "$OUT/test_jpeg" \
    test/host/test_jpeg.cpp lib/JPEGDEC/src/JPEGDEC.cpp
"$OUT/test_jpeg"

printf '\nall host tests passed\n'
