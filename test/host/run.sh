#!/usr/bin/env bash
# Host-side tests for the parts of the firmware that are pure computation:
# the 6-colour ditherer and the scale/rotate/pack renderer.
#
# These run on your Mac/PC with plain g++ -- no device, no PlatformIO, no
# toolchain. Arduino.h here is a small shim (see Arduino.h in this folder) that
# provides just enough of the Arduino API for those two files to compile.
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

# Proves the streaming 3-row error buffer is exactly equivalent to a
# whole-image implementation. This is the one that would catch a buffer
# rotation bug.
run test_stream test/host/test_stream.cpp src/e6_dither.cpp

# Colour reconstruction accuracy of each dithering kernel.
run test_dither test/host/test_dither.cpp src/render.cpp src/e6_dither.cpp

# Rotation mapping, cover/contain framing and 4bpp nibble packing.
run test_render test/host/test_render.cpp src/render.cpp src/e6_dither.cpp

# The vendored JPEGDEC patches: Immich's previews are SOF1 with three AC
# Huffman tables, which stock JPEGDEC rejects. See lib/JPEGDEC/PATCHES.md.
printf '\n=== test_jpeg ===\n'
$CXX $FLAGS -Ilib/JPEGDEC/src -Wno-unused-function -o "$OUT/test_jpeg" \
    test/host/test_jpeg.cpp lib/JPEGDEC/src/JPEGDEC.cpp
"$OUT/test_jpeg"

printf '\nall host tests passed\n'
