#!/usr/bin/env python3
"""Generate Cypher Engine's original blockout starter textures.

The generator intentionally uses only Python's standard library. Every output
is a deterministic 128 x 128 RGBA PNG designed for obvious orientation and
material separation while grayboxing maps. Run with --check in validation or
without it to rewrite the committed images beside this script.
"""

from __future__ import annotations

import argparse
import binascii
import struct
import sys
import zlib
from pathlib import Path


SIZE = 128
RGBA = tuple[int, int, int, int]


def _chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = binascii.crc32(kind)
    checksum = binascii.crc32(payload, checksum) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", checksum)


class Canvas:
    def __init__(self, color: RGBA) -> None:
        self.pixels = bytearray(color * (SIZE * SIZE))

    def set(self, x: int, y: int, color: RGBA) -> None:
        if not (0 <= x < SIZE and 0 <= y < SIZE):
            return
        offset = (y * SIZE + x) * 4
        self.pixels[offset : offset + 4] = bytes(color)

    def get(self, x: int, y: int) -> RGBA:
        offset = (y * SIZE + x) * 4
        return tuple(self.pixels[offset : offset + 4])  # type: ignore[return-value]

    def rect(self, left: int, top: int, right: int, bottom: int, color: RGBA) -> None:
        for y in range(max(0, top), min(SIZE, bottom)):
            for x in range(max(0, left), min(SIZE, right)):
                self.set(x, y, color)

    def line(self, x0: int, y0: int, x1: int, y1: int, color: RGBA, width: int = 1) -> None:
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        error = dx + dy
        radius = max(0, width - 1) // 2
        while True:
            for oy in range(-radius, radius + 1):
                for ox in range(-radius, radius + 1):
                    self.set(x0 + ox, y0 + oy, color)
            if x0 == x1 and y0 == y1:
                break
            twice = 2 * error
            if twice >= dy:
                error += dy
                x0 += sx
            if twice <= dx:
                error += dx
                y0 += sy

    def disc(self, cx: int, cy: int, radius: int, color: RGBA) -> None:
        radius_squared = radius * radius
        for y in range(cy - radius, cy + radius + 1):
            for x in range(cx - radius, cx + radius + 1):
                if (x - cx) ** 2 + (y - cy) ** 2 <= radius_squared:
                    self.set(x, y, color)

    def png(self) -> bytes:
        scanlines = bytearray()
        stride = SIZE * 4
        for y in range(SIZE):
            scanlines.append(0)
            start = y * stride
            scanlines.extend(self.pixels[start : start + stride])
        header = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
        return (
            b"\x89PNG\r\n\x1a\n"
            + _chunk(b"IHDR", header)
            + _chunk(b"IDAT", zlib.compress(bytes(scanlines), 9))
            + _chunk(b"IEND", b"")
        )


def noise(x: int, y: int, seed: int) -> int:
    value = (x * 0x1F123BB5) ^ (y * 0x5F356495) ^ (seed * 0x6C8E9CF5)
    value ^= value >> 13
    value = (value * 0x45D9F3B) & 0xFFFFFFFF
    value ^= value >> 16
    return value & 0xFF


def noisy_base(color: tuple[int, int, int], amplitude: int, seed: int) -> Canvas:
    canvas = Canvas((*color, 255))
    for y in range(SIZE):
        for x in range(SIZE):
            delta = noise(x, y, seed) % (amplitude * 2 + 1) - amplitude
            canvas.set(x, y, tuple(max(0, min(255, channel + delta)) for channel in color) + (255,))
    return canvas


def panel_border(canvas: Canvas, left: int, top: int, size: int) -> None:
    canvas.rect(left, top, left + size, top + 2, (43, 49, 53, 255))
    canvas.rect(left, top, left + 2, top + size, (43, 49, 53, 255))
    canvas.rect(left, top + size - 2, left + size, top + size, (139, 150, 156, 255))
    canvas.rect(left + size - 2, top, left + size, top + size, (139, 150, 156, 255))


def concrete_light() -> Canvas:
    canvas = noisy_base((145, 151, 154), 7, 11)
    for coordinate in (0, 64):
        canvas.rect(coordinate, 0, coordinate + 2, SIZE, (91, 98, 102, 255))
        canvas.rect(0, coordinate, SIZE, coordinate + 2, (91, 98, 102, 255))
        canvas.rect(coordinate + 2, 0, coordinate + 3, SIZE, (174, 179, 181, 255))
        canvas.rect(0, coordinate + 2, SIZE, coordinate + 3, (174, 179, 181, 255))
    for y in range(SIZE):
        for x in range(SIZE):
            value = noise(x, y, 19)
            if value < 3:
                canvas.set(x, y, (105, 112, 115, 255))
            elif value > 252:
                canvas.set(x, y, (184, 188, 189, 255))
    return canvas


def concrete_worn() -> Canvas:
    canvas = noisy_base((76, 82, 84), 9, 23)
    for coordinate in (0, 64):
        canvas.rect(coordinate, 0, coordinate + 3, SIZE, (42, 47, 49, 255))
        canvas.rect(0, coordinate, SIZE, coordinate + 3, (42, 47, 49, 255))
    cracks = (
        ((18, 10), (25, 24), (21, 38), (32, 52)),
        ((91, 69), (83, 82), (88, 99), (76, 116)),
        ((107, 17), (97, 31), (101, 47)),
    )
    for points in cracks:
        for start, end in zip(points, points[1:]):
            canvas.line(*start, *end, (34, 38, 39, 255), 2)
            canvas.line(start[0] + 1, start[1], end[0] + 1, end[1], (100, 105, 105, 255))
    for y in range(SIZE):
        for x in range(SIZE):
            if noise(x, y, 29) < 4:
                canvas.disc(x, y, 1, (111, 114, 113, 255))
    return canvas


def ceiling_panel() -> Canvas:
    canvas = Canvas((105, 114, 120, 255))
    for top in (0, 64):
        for left in (0, 64):
            panel_border(canvas, left, top, 64)
            canvas.rect(left + 8, top + 8, left + 56, top + 56, (173, 181, 184, 255))
            canvas.rect(left + 11, top + 11, left + 53, top + 14, (205, 211, 212, 255))
            canvas.rect(left + 11, top + 50, left + 53, top + 53, (126, 135, 139, 255))
            for bx, by in ((6, 6), (57, 6), (6, 57), (57, 57)):
                canvas.disc(left + bx, top + by, 2, (50, 57, 61, 255))
    return canvas


def floor_checker() -> Canvas:
    canvas = Canvas((58, 64, 68, 255))
    colors = ((66, 73, 77, 255), (104, 111, 113, 255))
    for row in range(4):
        for column in range(4):
            left, top = column * 32, row * 32
            canvas.rect(left, top, left + 32, top + 32, colors[(row + column) & 1])
            canvas.rect(left, top, left + 32, top + 2, (40, 46, 49, 255))
            canvas.rect(left, top, left + 2, top + 32, (40, 46, 49, 255))
            canvas.rect(left + 2, top + 2, left + 31, top + 3, (132, 138, 139, 255))
            canvas.rect(left + 2, top + 2, left + 3, top + 31, (132, 138, 139, 255))
    return canvas


def floor_tile() -> Canvas:
    canvas = noisy_base((70, 86, 96), 3, 31)
    for coordinate in range(0, SIZE, 32):
        canvas.rect(coordinate, 0, coordinate + 2, SIZE, (34, 47, 54, 255))
        canvas.rect(0, coordinate, SIZE, coordinate + 2, (34, 47, 54, 255))
        canvas.rect(coordinate + 2, 0, coordinate + 3, SIZE, (102, 119, 128, 255))
        canvas.rect(0, coordinate + 2, SIZE, coordinate + 3, (102, 119, 128, 255))
    for row in range(4):
        for column in range(4):
            if (row + column) % 3 == 0:
                canvas.rect(column * 32 + 8, row * 32 + 14,
                            column * 32 + 24, row * 32 + 17, (62, 76, 84, 255))
    return canvas


def metal_grate() -> Canvas:
    canvas = Canvas((61, 69, 73, 255))
    for top in range(0, SIZE, 16):
        for left in range(0, SIZE, 16):
            canvas.rect(left, top, left + 16, top + 2, (126, 137, 141, 255))
            canvas.rect(left, top, left + 2, top + 16, (112, 123, 128, 255))
            canvas.rect(left + 4, top + 4, left + 14, top + 14, (20, 25, 28, 255))
            canvas.rect(left + 5, top + 5, left + 13, top + 7, (35, 42, 45, 255))
    return canvas


def metal_panel() -> Canvas:
    canvas = Canvas((77, 91, 99, 255))
    for top in (0, 64):
        for left in (0, 64):
            panel_border(canvas, left, top, 64)
            canvas.rect(left + 8, top + 25, left + 56, top + 39, (63, 76, 83, 255))
            canvas.rect(left + 10, top + 26, left + 54, top + 28, (124, 139, 146, 255))
            for bx, by in ((7, 7), (56, 7), (7, 56), (56, 56)):
                canvas.disc(left + bx, top + by, 2, (35, 42, 46, 255))
                canvas.set(left + bx - 1, top + by - 1, (166, 174, 177, 255))
    return canvas


def service_panel() -> Canvas:
    canvas = Canvas((42, 50, 55, 255))
    for top in (0, 64):
        for left in (0, 64):
            panel_border(canvas, left, top, 64)
            canvas.rect(left + 8, top + 9, left + 56, top + 54, (55, 65, 70, 255))
            canvas.rect(left + 13, top + 14, left + 30, top + 32, (27, 33, 36, 255))
            canvas.rect(left + 35, top + 14, left + 51, top + 32, (27, 33, 36, 255))
            canvas.line(left + 17, top + 42, left + 46, top + 42, (45, 167, 193, 255), 2)
            canvas.line(left + 46, top + 42, left + 46, top + 35, (45, 167, 193, 255), 2)
            canvas.disc(left + 18, top + 48, 2, (239, 151, 42, 255))
            canvas.disc(left + 27, top + 48, 2, (66, 185, 113, 255))
            canvas.disc(left + 36, top + 48, 2, (196, 66, 59, 255))
    return canvas


def wall_ribbed() -> Canvas:
    canvas = Canvas((74, 82, 87, 255))
    for left in range(0, SIZE, 16):
        canvas.rect(left, 0, left + 3, SIZE, (39, 46, 50, 255))
        canvas.rect(left + 3, 0, left + 6, SIZE, (126, 137, 142, 255))
        canvas.rect(left + 6, 0, left + 14, SIZE, (83, 92, 97, 255))
        canvas.rect(left + 14, 0, left + 16, SIZE, (55, 62, 66, 255))
    canvas.rect(0, 59, SIZE, 69, (43, 50, 54, 255))
    canvas.rect(0, 59, SIZE, 62, (147, 157, 160, 255))
    canvas.rect(0, 66, SIZE, 69, (25, 31, 34, 255))
    for x in range(8, SIZE, 16):
        canvas.disc(x, 64, 2, (177, 183, 184, 255))
    return canvas


def trim_blue() -> Canvas:
    canvas = Canvas((24, 37, 47, 255))
    canvas.rect(0, 10, SIZE, 118, (25, 92, 131, 255))
    canvas.rect(0, 14, SIZE, 18, (59, 165, 211, 255))
    canvas.rect(0, 110, SIZE, 114, (13, 52, 75, 255))
    canvas.rect(0, 48, SIZE, 80, (31, 123, 166, 255))
    canvas.rect(0, 50, SIZE, 54, (91, 193, 224, 255))
    for x in range(16, SIZE, 32):
        canvas.disc(x, 32, 3, (18, 49, 65, 255))
        canvas.disc(x, 96, 3, (18, 49, 65, 255))
    return canvas


def trim_orange() -> Canvas:
    canvas = Canvas((43, 35, 28, 255))
    canvas.rect(0, 10, SIZE, 118, (183, 83, 26, 255))
    canvas.rect(0, 14, SIZE, 18, (246, 151, 50, 255))
    canvas.rect(0, 110, SIZE, 114, (105, 45, 18, 255))
    canvas.rect(0, 48, SIZE, 80, (220, 104, 29, 255))
    canvas.rect(0, 50, SIZE, 54, (255, 177, 66, 255))
    for x in range(16, SIZE, 32):
        canvas.disc(x, 32, 3, (75, 39, 24, 255))
        canvas.disc(x, 96, 3, (75, 39, 24, 255))
    return canvas


def warning_red() -> Canvas:
    canvas = Canvas((43, 46, 49, 255))
    canvas.rect(0, 12, SIZE, 116, (135, 37, 40, 255))
    canvas.rect(0, 16, SIZE, 20, (217, 71, 66, 255))
    canvas.rect(0, 108, SIZE, 112, (76, 24, 28, 255))
    for offset in range(-128, 256, 48):
        canvas.line(offset, 101, offset + 38, 64, (230, 226, 210, 255), 9)
        canvas.line(offset + 38, 64, offset, 27, (230, 226, 210, 255), 9)
    canvas.rect(0, 60, SIZE, 68, (90, 25, 28, 255))
    return canvas


GENERATORS = {
    "ceiling_panel": ceiling_panel,
    "concrete_light": concrete_light,
    "concrete_worn": concrete_worn,
    "floor_checker": floor_checker,
    "floor_tile": floor_tile,
    "metal_grate": metal_grate,
    "metal_panel": metal_panel,
    "service_panel": service_panel,
    "trim_blue": trim_blue,
    "trim_orange": trim_orange,
    "wall_ribbed": wall_ribbed,
    "warning_red": warning_red,
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail when committed PNGs differ")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    arguments = parser.parse_args()
    arguments.output.mkdir(parents=True, exist_ok=True)

    stale: list[str] = []
    for name, generator in sorted(GENERATORS.items()):
        path = arguments.output / f"{name}.png"
        encoded = generator().png()
        if arguments.check:
            if not path.is_file() or path.read_bytes() != encoded:
                stale.append(path.name)
        else:
            path.write_bytes(encoded)
            print(path)

    if stale:
        print("blockout textures are missing or stale: " + ", ".join(stale), file=sys.stderr)
        return 1
    if arguments.check:
        print(f"verified {len(GENERATORS)} deterministic blockout textures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
