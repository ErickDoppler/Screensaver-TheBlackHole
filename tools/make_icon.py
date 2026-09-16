#!/usr/bin/env python3
"""Generates res/win32/theblackhole.ico: the shadow, the photon ring around it,
an accretion disk seen from just above its plane with its far side arcing over
the top, and a scatter of cold stars. Pure Python, no image libraries - each
size is written as a 32-bit BMP icon entry, 256 as a PNG one.

    python tools/make_icon.py
"""
import math
import os
import struct
import zlib

SIZES = [16, 24, 32, 48, 64, 128, 256]
OUT = os.path.join(os.path.dirname(__file__), "..", "res", "win32", "theblackhole.ico")

SPACE = (2, 3, 10)
STAR = (191, 212, 255)
DISK = (255, 177, 74)
RING = (255, 240, 192)


def clamp(v):
    return 0 if v < 0 else 255 if v > 255 else int(v)


def hash01(a, b):
    h = (a * 374761393 + b * 668265263) & 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


def add(px, x, y, col, k):
    r, g, b, a = px[y][x]
    px[y][x] = (clamp(r + col[0] * k), clamp(g + col[1] * k), clamp(b + col[2] * k), a)


def render(n):
    """Returns rows (top to bottom) of (r, g, b, a) tuples."""
    px = [[SPACE + (255,) for _ in range(n)] for _ in range(n)]
    cx = cy = (n - 1) / 2.0
    shadow = n * 0.21          # the black disk
    ring = n * 0.235           # the photon ring, just outside it
    d_in, d_out = n * 0.30, n * 0.48   # the disk's inner and outer edge
    squash = 0.26              # seen from close to its plane

    # stars first, so everything else sits over them
    for y in range(n):
        for x in range(n):
            if hash01(x, y) > 0.965:
                k = 0.35 + 0.65 * hash01(y, x)
                add(px, x, y, STAR, k)

    for y in range(n):
        for x in range(n):
            dx, dy = x - cx, y - cy
            r = math.hypot(dx, dy)

            # the disk, front half: an ellipse below the centre line
            ry = dy / squash
            er = math.hypot(dx, ry)
            if d_in < er < d_out and dy > 0:
                t = (er - d_in) / (d_out - d_in)
                add(px, x, y, DISK, 0.95 * (1 - t) ** 1.6)
            # ... and its far side, lensed up and over the top of the shadow
            arc = abs(r - ring * 1.55)
            if dy < 0 and arc < n * 0.055 and abs(dx) < d_out:
                add(px, x, y, DISK, 0.85 * (1 - arc / (n * 0.055)) ** 1.3)

            # the photon ring
            dr = abs(r - ring)
            if dr < max(1.0, n * 0.035):
                add(px, x, y, RING, (1 - dr / max(1.0, n * 0.035)) ** 2)

            # the shadow swallows whatever was drawn there
            if r < shadow:
                edge = min(1.0, (shadow - r) / max(1.0, n * 0.02))
                r0, g0, b0, a0 = px[y][x]
                px[y][x] = (clamp(r0 * (1 - edge)), clamp(g0 * (1 - edge)),
                            clamp(b0 * (1 - edge)), a0)

    # rounded corners for the large sizes (transparent), keeps small ones square
    if n >= 48:
        rad = n * 0.14
        for y in range(n):
            for x in range(n):
                dx = max(rad - x - 0.5, 0, x + 0.5 - (n - rad))
                dy = max(rad - y - 0.5, 0, y + 0.5 - (n - rad))
                if dx * dx + dy * dy > rad * rad:
                    r, g, b, _ = px[y][x]
                    px[y][x] = (r, g, b, 0)
    return px


def bmp_entry(px):
    n = len(px)
    header = struct.pack("<IiiHHIIiiII", 40, n, n * 2, 1, 32, 0, n * n * 4, 0, 0, 0, 0)
    body = bytearray()
    for y in range(n - 1, -1, -1):            # bottom-up
        for x in range(n):
            r, g, b, a = px[y][x]
            body += bytes((b, g, r, a))
    mask_row = ((n + 31) // 32) * 4
    mask = bytearray()
    for y in range(n - 1, -1, -1):
        row = bytearray(mask_row)
        for x in range(n):
            if px[y][x][3] == 0:
                row[x // 8] |= 0x80 >> (x % 8)
        mask += row
    return header + body + mask


def png_entry(px):
    n = len(px)
    raw = bytearray()
    for y in range(n):
        raw.append(0)
        for x in range(n):
            raw += bytes(px[y][x])

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def main():
    entries = []
    for n in SIZES:
        px = render(n)
        entries.append((n, png_entry(px) if n == 256 else bmp_entry(px)))
    out = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    blobs = bytearray()
    for n, blob in entries:
        out += struct.pack("<BBBBHHII", n % 256, n % 256, 0, 0, 1, 32, len(blob), offset + len(blobs))
        blobs += blob
    out += blobs
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(out)
    print(f"wrote {OUT} ({len(out)} bytes, sizes {SIZES})")


if __name__ == "__main__":
    main()
