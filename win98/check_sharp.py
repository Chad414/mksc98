#!/usr/bin/env python3
"""Test harness: is the picture on screen an exact whole-number enlargement?

Compares a --screenshot of a fullscreen run with the --dump-frame of the same
final frame. Every screen pixel inside the picture must equal the frame pixel
it was enlarged from; any smoothing by the video path shows up as mismatches.
Usage: check_sharp.py SCREEN.bmp FRAME.bmp"""
import struct
import sys

def load(path):
    data = open(path, "rb").read()
    offset, = struct.unpack_from("<I", data, 10)
    width, height = struct.unpack_from("<ii", data, 18)
    row = (width * 3 + 3) & ~3
    return width, height, [data[offset + (height - 1 - y) * row: offset + (height - 1 - y) * row + width * 3]
                           for y in range(height)]

sw, sh, screen = load(sys.argv[1])
fw, fh, frame = load(sys.argv[2])
scale = min(sw // fw, sh // fh)
left, top = (sw - fw * scale) // 2, (sh - fh * scale) // 2
wrong = worst = 0
for y in range(fh * scale):
    source, shown = frame[y // scale], screen[top + y]
    for x in range(fw * scale):
        a = source[(x // scale) * 3:(x // scale) * 3 + 3]
        b = shown[(left + x) * 3:(left + x) * 3 + 3]
        if a != b:
            wrong += 1
            worst = max(worst, max(abs(p - q) for p, q in zip(a, b)))
total = fw * fh * scale * scale
print(f"screen {sw}x{sh}, frame {fw}x{fh}, scale {scale}x at ({left},{top}): "
      f"{wrong} of {total} pixels differ (largest channel difference {worst})")
print("SHARP: exact whole-number enlargement" if wrong == 0 else "NOT EXACT")
