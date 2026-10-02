#!/usr/bin/env python3
"""Explain why the firmware's JPEG decoder would reject a file.

Reproduces the acceptance tests JPEGDEC performs in JPEGParseInfo() and
JPEGMakeHuffTables(), so an "err 3 / JPEG_UNSUPPORTED_FEATURE" can be attributed
to the real cause instead of being blamed on progressive encoding.

This mirrors the PATCHED decoder in lib/JPEGDEC -- SOF1 and four Huffman tables
per class are accepted here because the patches accept them. The same checks run
on the device in src/decode_jpeg.cpp; keep the two in step.

    python3 tools/jpegprobe.py photo.jpg
"""
import sys

SOF = {0xc0: "baseline sequential      (SUPPORTED)",
       0xc1: "extended sequential      (SUPPORTED, needs the lib/JPEGDEC patch)",
       0xc2: "progressive              (REJECTED by JPEGDEC)",
       0xc3: "lossless                 (REJECTED by JPEGDEC)",
       0xc5: "differential sequential  (REJECTED)",
       0xc6: "differential progressive (REJECTED)",
       0xc7: "differential lossless    (REJECTED)",
       0xc9: "arithmetic sequential    (REJECTED)",
       0xca: "arithmetic progressive   (REJECTED)",
       0xcb: "arithmetic lossless      (REJECTED)"}


def huff_codes(bits):
    """Yield (bit_length, canonical_code) in the order JPEGDEC walks them."""
    cc = 0
    for n in range(1, 17):
        for _ in range(bits[n - 1]):
            yield n, cc
            cc += 1
        cc <<= 1


def check_dc(bits):
    """JPEGDEC DC table: 6-bit short table + long table for codes whose first
    5 bits are all ones. Max code length 12."""
    for n, cc in huff_codes(bits):
        if n > 12:
            return f"DC code of {n} bits (JPEGDEC max is 12)"
        if n >= 6 and (cc >> (n - 5)) == 0x1f:
            continue                       # long table
        if 6 - n < 0:
            return f"DC code of {n} bits not in the '5 leading 1s' long-table bucket"
    return None


def check_ac(bits):
    """JPEGDEC AC table: 10-bit short table + long table for codes whose first
    6 bits are all ones. Anything else hits `return 0` -> UNSUPPORTED_FEATURE."""
    for n, cc in huff_codes(bits):
        if n >= 6 and (cc >> (n - 6)) == 0x3f:
            continue                       # long table
        if 10 - n < 0:
            return f"AC code of {n} bits not in the '6 leading 1s' long-table bucket"
    return None


def probe(path):
    d = open(path, "rb").read()
    print(f"{path}: {len(d)} bytes")
    if d[:2] != b"\xff\xd8":
        print("  not a JPEG (no SOI)")
        return
    i, verdict = 2, []
    while i < len(d) - 1:
        if d[i] != 0xFF:
            i += 1
            continue
        m = d[i + 1]
        if m in (0xD8, 0x01) or 0xD0 <= m <= 0xD7:
            i += 2
            continue
        if m == 0xD9:
            break
        ln = int.from_bytes(d[i + 2:i + 4], "big")
        seg = d[i + 4:i + 2 + ln]

        if m in SOF:
            prec, h, w, nc = seg[0], int.from_bytes(seg[1:3], "big"), int.from_bytes(seg[3:5], "big"), seg[5]
            print(f"  SOF ff{m:02x}  {SOF[m]}")
            print(f"    {w}x{h}  {prec}-bit  {nc} components")
            samp = []
            for c in range(nc):
                cid, hv, tq = seg[6 + c * 3], seg[7 + c * 3], seg[8 + c * 3]
                samp.append(f"id{cid}:{hv >> 4}x{hv & 15}/q{tq}")
            print("    sampling " + "  ".join(samp))
            if m not in (0xC0, 0xC1):
                verdict.append(f"frame mode ff{m:02x} is neither baseline nor extended sequential")
            if prec != 8:
                verdict.append(f"{prec}-bit samples")
            if nc not in (1, 3):
                verdict.append(f"{nc} components (only 1 or 3 supported)")
        elif m == 0xC4:                      # DHT
            p = 0
            while p < len(seg):
                tc, th = seg[p] >> 4, seg[p] & 15
                bits = list(seg[p + 1:p + 17])
                n = sum(bits)
                longest = max((k + 1 for k, v in enumerate(bits) if v), default=0)
                kind = "AC" if tc else "DC"
                bad = (check_ac if tc else check_dc)(bits)
                print(f"  DHT {kind}{th}  {n} codes, longest {longest} bits"
                      + (f"   <-- {bad}" if bad else ""))
                if bad:
                    verdict.append(f"{kind} table {th}: {bad}")
                p += 17 + n
        elif m == 0xDD:
            print(f"  DRI  restart interval {int.from_bytes(seg[0:2], 'big')} MCUs")
        elif 0xE0 <= m <= 0xEF:
            print(f"  APP{m - 0xE0}  {ln} bytes  {bytes(seg[:12])!r}")
        elif m == 0xDA:
            print(f"  SOS  {seg[0]} components in scan")
            break
        i += 2 + ln

    print()
    if verdict:
        print("REJECTED by JPEGDEC (err 3, JPEG_UNSUPPORTED_FEATURE) because:")
        for v in dict.fromkeys(verdict):
            print("  - " + v)
    else:
        print("JPEGDEC should accept this file.")


if __name__ == "__main__":
    for a in sys.argv[1:] or ["-"]:
        probe(a)
