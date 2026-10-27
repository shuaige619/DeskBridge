#!/usr/bin/env python3
"""Convert Xvfb 24-bit little-endian XWD captures to PNG, using stdlib."""
from pathlib import Path
import struct
import sys
import zlib

source, target = map(Path, sys.argv[1:])
data = source.read_bytes()
header = struct.unpack('>25I', data[:100])
size, version, format_, depth, width, height = header[:6]
assert version == 7 and format_ == 2 and depth == 24
assert header[7] == 0 and header[11] in (24, 32)
step = header[11] // 8
assert header[14:17] == (0xff0000, 0xff00, 0xff)
stride = header[12]
offset = size + header[19] * 12
assert len(data) >= offset + stride * height
scanlines = bytearray()
for y in range(height):
    row = data[offset + y * stride:offset + y * stride + width * step]
    rgb = bytearray(width * 3)
    rgb[0::3], rgb[1::3], rgb[2::3] = row[2::step], row[1::step], row[0::step]
    scanlines.extend(b'\x00' + rgb)
def chunk(kind, payload):
    body = kind + payload
    return struct.pack('>I', len(payload)) + body + struct.pack('>I', zlib.crc32(body))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 2, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(scanlines, 9)) + chunk(b'IEND', b'')
target.write_bytes(png)
print(f'{width}x{height} PNG: {target}')
