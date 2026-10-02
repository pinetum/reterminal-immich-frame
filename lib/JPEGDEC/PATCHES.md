# Why JPEGDEC is vendored here

Upstream: https://github.com/bitbank2/JPEGDEC, **v1.8.4**, with the three patches
below. It lives in `lib/` rather than `lib_deps` because PlatformIO wipes
`.pio/libdeps/` and the patches would go with it.

## The problem

Immich generates its `preview` renditions with sharp (libvips). When libvips is
built against **mozjpeg** -- which the official Immich container is -- the
encoder optimises its Huffman tables per component and emits **three** AC
tables. libjpeg's definition of "baseline" allows at most two Huffman tables per
class, so the file is written with an **SOF1 (extended sequential)** frame
header instead of SOF0.

The entropy coding is byte-for-byte the same as baseline; only the table budget
differs. Stock JPEGDEC rejected these files twice over:

* `JPEGParseInfo()` refused the SOF1 marker outright, and
* `JPEGDecodeMCU()` bailed on any AC table index above 1, because `usHuffAC` was
  only sized for two tables.

Both show up as `JPEG_UNSUPPORTED_FEATURE` (err 3) at `open()`, which is the
same code progressive JPEGs produce -- hence the misleading error the firmware
used to print. `tools/jpegprobe.py` tells the two apart.

## The patches

1. `JPEGDEC.h` -- `ucHuffDC` and `usHuffAC` grow from 2 to 4 table slots.
   JPEG allows table ids 0-3 and `JPEGGetSOS()` already validates that range, so
   2 slots was also a latent out-of-bounds write: `JPEGMakeHuffTables()` loops
   `iTable` over 0-3 and indexes `ucHuffDC[iTable * DC_TABLE_SIZE]` with no
   bounds check at all.

   Costs 10 kB of internal RAM (8 kB AC + 2 kB DC), paid once in the `JPEGDEC`
   global. The existing `sizeof()`-based guard in `JPEGMakeHuffTables()` picks
   the new size up on its own.

2. `jpeg.inl` -- `0xffc1` moves from the reject list to the SOF handler, next to
   `0xffc0`. Everything downstream switches on `ucMode` only to special-case
   `0xc2` (progressive) and `0xc3` (lossless), so SOF1 takes the baseline path,
   which is what it needs. SOF3 stays rejected.

3. `jpeg.inl` -- the `ucACTable > 1` guard in `JPEGDecodeMCU()` becomes
   `> 3`, now that the array can hold four.

## Verifying after an upstream bump

`test/host/jpeg/` decodes a known SOF1 / 3-AC-table file natively and compares
against a reference decode; run `test/host/run.sh`.
