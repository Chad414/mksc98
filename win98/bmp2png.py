#!/usr/bin/env python3
"""Converts a 24-bit BMP written by --dump-frame to PNG (optionally scaled
by an integer factor) so it can be viewed anywhere. Standard library only.
Usage: bmp2png.py IN.bmp OUT.png [SCALE]"""
import struct
import sys
import zlib

data = open(sys.argv[1], "rb").read()
offset, = struct.unpack_from("<I", data, 10)
width, height = struct.unpack_from("<ii", data, 18)
scale = int(sys.argv[3]) if len(sys.argv) > 3 else 1
row_bytes = (width * 3 + 3) & ~3
rows = []
for y in range(height - 1, -1, -1):
    row = data[offset + y * row_bytes: offset + y * row_bytes + width * 3]
    rgb = bytearray()
    for x in range(width):
        pixel = bytes((row[x * 3 + 2], row[x * 3 + 1], row[x * 3]))
        rgb += pixel * scale
    rows += [b"\x00" + bytes(rgb)] * scale

def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))

png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width * scale, height * scale, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
open(sys.argv[2], "wb").write(png)
