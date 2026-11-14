#!/usr/bin/env python3
"""Convert the resized boot PNG to LVGL 8 RGB565 + per-pixel alpha.

No runtime PNG decoder or additional Python packages are required.
Resize a copy of the original first (macOS):
  sips --resampleHeightWidth 256 256 reference/crystalos-boot_icon.png \
    --out components/crystal_shell/assets/crystalos_boot_logo_256.png
Then run this script. The original reference artwork is never modified.
"""
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'components/crystal_shell/assets'


def rgba_png(path):
    data = path.read_bytes()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('Not a PNG')
    offset, compressed, header = 8, bytearray(), None
    while offset < len(data):
        length = struct.unpack_from('>I', data, offset)[0]
        kind = data[offset + 4:offset + 8]
        chunk = data[offset + 8:offset + 8 + length]
        crc = struct.unpack_from('>I', data, offset + 8 + length)[0]
        if zlib.crc32(kind + chunk) != crc:
            raise ValueError('PNG chunk checksum mismatch')
        if kind == b'IHDR':
            header = struct.unpack('>IIBBBBB', chunk)
        elif kind == b'IDAT':
            compressed.extend(chunk)
        offset += length + 12
        if kind == b'IEND':
            break
    if header is None or header[2:] != (8, 6, 0, 0, 0):
        raise ValueError('Expected non-interlaced 8-bit RGBA PNG')
    width, height = header[:2]
    raw = zlib.decompress(compressed)
    stride = width * 4
    if len(raw) != height * (stride + 1):
        raise ValueError('Unexpected PNG scanline size')
    previous = bytearray(stride)
    pixels = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        filter_type = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        if filter_type > 4:
            raise ValueError('Unknown PNG filter')
        for x in range(stride):
            left = row[x - 4] if x >= 4 else 0
            up = previous[x]
            corner = previous[x - 4] if x >= 4 else 0
            prediction = left + up - corner
            distances = (abs(prediction - left), abs(prediction - up), abs(prediction - corner))
            paeth = (left, up, corner)[distances.index(min(distances))]
            row[x] = (row[x] + (0, left, up, (left + up) // 2, paeth)[filter_type]) & 255
        pixels.extend(row)
        previous = row
    return width, height, pixels


def main():
    width, height, rgba = rgba_png(ASSETS / 'crystalos_boot_logo_256.png')
    if (width, height) != (256, 256):
        raise ValueError('Boot asset must be 256x256; preserve its square proportions')
    pixels = bytearray()
    for i in range(0, len(rgba), 4):
        red, green, blue, alpha = rgba[i:i + 4]
        rgb565 = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        pixels.extend((rgb565 & 255, rgb565 >> 8, alpha))
    output = ASSETS / 'crystalos_boot_logo_256.bin'
    output.write_bytes(pixels)
    print(f'{output.name}: {width}x{height}, {len(pixels):,} bytes; original alpha preserved')


if __name__ == '__main__':
    main()
