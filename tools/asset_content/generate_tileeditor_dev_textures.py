#!/usr/bin/env python3
"""Generate Cypher TileEditor development textures without third-party packages.

The output is deliberately simple RGBA8 PNG.  Patterns are diagnostic rather
than decorative: each one isolates a scale, UV, color, mip, seam, or gameplay
authoring convention that a level designer needs while grayboxing a map.

Run from any directory:

    python3 tools/asset_content/generate_tileeditor_dev_textures.py
    python3 tools/asset_content/generate_tileeditor_dev_textures.py --check

`--check` regenerates every image in memory and fails if a checked-in PNG is
missing or differs byte-for-byte.
"""

from __future__ import annotations

import argparse
import binascii
import math
from pathlib import Path
import struct
import sys
import zlib
from collections.abc import Callable


Color = tuple[int, int, int, int]


FONT: dict[str, tuple[str, ...]] = {
    " ": ("00000",) * 7,
    "+": ("00000", "00100", "00100", "11111", "00100", "00100", "00000"),
    "-": ("00000", "00000", "00000", "11111", "00000", "00000", "00000"),
    ".": ("00000", "00000", "00000", "00000", "00000", "00110", "00110"),
    "/": ("00001", "00010", "00100", "01000", "10000", "00000", "00000"),
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "2": ("01110", "10001", "00001", "00010", "00100", "01000", "11111"),
    "3": ("11110", "00001", "00001", "01110", "00001", "00001", "11110"),
    "4": ("00010", "00110", "01010", "10010", "11111", "00010", "00010"),
    "5": ("11111", "10000", "10000", "11110", "00001", "00001", "11110"),
    "6": ("01110", "10000", "10000", "11110", "10001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    "9": ("01110", "10001", "10001", "01111", "00001", "00001", "01110"),
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "B": ("11110", "10001", "10001", "11110", "10001", "10001", "11110"),
    "C": ("01111", "10000", "10000", "10000", "10000", "10000", "01111"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    "G": ("01111", "10000", "10000", "10111", "10001", "10001", "01111"),
    "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    "I": ("01110", "00100", "00100", "00100", "00100", "00100", "01110"),
    "J": ("00001", "00001", "00001", "00001", "10001", "10001", "01110"),
    "K": ("10001", "10010", "10100", "11000", "10100", "10010", "10001"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    "N": ("10001", "11001", "10101", "10011", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "Q": ("01110", "10001", "10001", "10001", "10101", "10010", "01101"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    "W": ("10001", "10001", "10001", "10101", "10101", "10101", "01010"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
    "Z": ("11111", "00001", "00010", "00100", "01000", "10000", "11111"),
}


class Canvas:
    def __init__(self, width: int, height: int, color: Color) -> None:
        self.width = width
        self.height = height
        self.pixels = bytearray(color * (width * height))

    def set(self, x: int, y: int, color: Color) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            index = (y * self.width + x) * 4
            self.pixels[index : index + 4] = bytes(color)

    def rect(self, x0: int, y0: int, x1: int, y1: int, color: Color) -> None:
        left = max(0, min(x0, x1))
        right = min(self.width, max(x0, x1))
        top = max(0, min(y0, y1))
        bottom = min(self.height, max(y0, y1))
        if left >= right or top >= bottom:
            return
        x0, x1 = left, right
        y0, y1 = top, bottom
        row = bytes(color) * max(0, x1 - x0)
        for y in range(y0, y1):
            index = (y * self.width + x0) * 4
            self.pixels[index : index + len(row)] = row

    def line(self, x0: int, y0: int, x1: int, y1: int, color: Color, width: int = 1) -> None:
        dx = abs(x1 - x0)
        sx = 1 if x0 < x1 else -1
        dy = -abs(y1 - y0)
        sy = 1 if y0 < y1 else -1
        error = dx + dy
        radius = max(0, width // 2)
        while True:
            self.rect(x0 - radius, y0 - radius, x0 + radius + 1, y0 + radius + 1, color)
            if x0 == x1 and y0 == y1:
                break
            twice = 2 * error
            if twice >= dy:
                error += dy
                x0 += sx
            if twice <= dx:
                error += dx
                y0 += sy

    def circle(self, cx: int, cy: int, radius: int, color: Color, width: int = 1) -> None:
        outer2 = radius * radius
        inner = max(0, radius - width)
        inner2 = inner * inner
        for y in range(cy - radius, cy + radius + 1):
            for x in range(cx - radius, cx + radius + 1):
                distance2 = (x - cx) ** 2 + (y - cy) ** 2
                if inner2 <= distance2 <= outer2:
                    self.set(x, y, color)

    def text(self, x: int, y: int, value: str, color: Color, scale: int = 2) -> None:
        cursor = x
        for character in value.upper():
            glyph = FONT.get(character, FONT[" "])
            for gy, row in enumerate(glyph):
                for gx, bit in enumerate(row):
                    if bit == "1":
                        self.rect(
                            cursor + gx * scale,
                            y + gy * scale,
                            cursor + (gx + 1) * scale,
                            y + (gy + 1) * scale,
                            color,
                        )
            cursor += 6 * scale

    def centered_text(self, y: int, value: str, color: Color, scale: int = 2) -> None:
        width = max(0, len(value) * 6 * scale - scale)
        self.text((self.width - width) // 2, y, value, color, scale)

    def border(self, color: Color, width: int = 4) -> None:
        self.rect(0, 0, self.width, width, color)
        self.rect(0, self.height - width, self.width, self.height, color)
        self.rect(0, 0, width, self.height, color)
        self.rect(self.width - width, 0, self.width, self.height, color)

    def arrow(self, x0: int, y0: int, x1: int, y1: int, color: Color, width: int = 5) -> None:
        self.line(x0, y0, x1, y1, color, width)
        angle = math.atan2(y1 - y0, x1 - x0)
        head = max(12, width * 3)
        for offset in (-0.65, 0.65):
            hx = int(round(x1 - head * math.cos(angle + offset)))
            hy = int(round(y1 - head * math.sin(angle + offset)))
            self.line(x1, y1, hx, hy, color, width)


def checker(canvas: Canvas, size: int, a: Color, b: Color) -> None:
    for y in range(0, canvas.height, size):
        for x in range(0, canvas.width, size):
            canvas.rect(x, y, x + size, y + size, a if (x // size + y // size) % 2 == 0 else b)


def hatch(canvas: Canvas, spacing: int, color: Color, width: int = 3, reverse: bool = False) -> None:
    span = canvas.width + canvas.height
    for offset in range(-canvas.height, canvas.width + canvas.height, spacing):
        if reverse:
            canvas.line(offset, 0, offset - canvas.height, canvas.height, color, width)
        else:
            canvas.line(offset, 0, offset + canvas.height, canvas.height, color, width)


def banner(canvas: Canvas, label: str, accent: Color, text_color: Color = (245, 248, 250, 255)) -> None:
    height = 34
    canvas.rect(0, 0, canvas.width, height, (15, 18, 22, 255))
    canvas.rect(0, height - 4, canvas.width, height, accent)
    canvas.centered_text(9, label, text_color, 2)


def metric_grid() -> Canvas:
    c = Canvas(256, 256, (105, 110, 116, 255))
    for coordinate in range(0, 256, 8):
        major = coordinate % 64 == 0
        color = (220, 224, 228, 255) if major else (145, 150, 156, 255)
        width = 3 if major else 1
        c.line(coordinate, 0, coordinate, 255, color, width)
        c.line(0, coordinate, 255, coordinate, color, width)
    c.line(128, 0, 128, 255, (236, 78, 70, 255), 4)
    c.line(0, 128, 255, 128, (70, 205, 108, 255), 4)
    c.rect(91, 104, 165, 151, (22, 25, 29, 255))
    c.centered_text(111, "1M GRID", (245, 248, 250, 255), 2)
    c.centered_text(132, "X/R Y/G", (210, 215, 220, 255), 1)
    return c


def uv_checker() -> Canvas:
    c = Canvas(256, 256, (0, 0, 0, 255))
    palette = ((34, 45, 61, 255), (220, 222, 218, 255))
    checker(c, 32, *palette)
    for y in range(8):
        for x in range(8):
            cell_color = (234, 80, 80, 255) if x == 0 else (36, 40, 46, 255)
            label = f"{x}{y}"
            c.text(x * 32 + 9, y * 32 + 11, label, cell_color, 1)
    c.line(8, 244, 235, 244, (238, 75, 70, 255), 4)
    c.arrow(12, 244, 244, 244, (238, 75, 70, 255), 4)
    c.arrow(12, 244, 12, 12, (65, 215, 112, 255), 4)
    c.text(218, 225, "U+", (238, 75, 70, 255), 2)
    c.text(18, 9, "V+", (65, 215, 112, 255), 2)
    return c


def orientation_arrows() -> Canvas:
    c = Canvas(256, 256, (43, 47, 55, 255))
    checker(c, 32, (43, 47, 55, 255), (53, 58, 67, 255))
    c.arrow(128, 178, 128, 55, (252, 205, 70, 255), 11)
    c.arrow(78, 128, 181, 128, (70, 196, 238, 255), 8)
    c.circle(128, 128, 25, (242, 245, 248, 255), 5)
    c.rect(96, 16, 160, 43, (16, 19, 23, 255))
    c.centered_text(23, "TOP", (252, 205, 70, 255), 2)
    c.text(19, 117, "L", (240, 244, 247, 255), 2)
    c.text(222, 117, "R", (240, 244, 247, 255), 2)
    c.centered_text(211, "ORIENT", (240, 244, 247, 255), 2)
    c.border((12, 14, 17, 255), 4)
    return c


def texel_density() -> Canvas:
    c = Canvas(256, 256, (28, 31, 36, 255))
    sizes = (32, 16, 8, 4)
    colors = (
        ((238, 239, 230, 255), (36, 45, 57, 255)),
        ((236, 184, 62, 255), (45, 39, 28, 255)),
        ((69, 200, 231, 255), (25, 45, 52, 255)),
        ((233, 84, 119, 255), (54, 28, 37, 255)),
    )
    for band, size in enumerate(sizes):
        x0 = band * 64
        for y in range(0, 256, size):
            for x in range(x0, x0 + 64, size):
                pair = colors[band]
                c.rect(x, y, x + size, y + size, pair[(x // size + y // size) & 1])
        c.rect(x0, 0, x0 + 64, 24, (12, 14, 18, 255))
        c.centered_text(6, "" if band else "", (255, 255, 255, 255), 1)
        c.text(x0 + 5, 7, f"{size}PX", (246, 248, 250, 255), 1)
        c.line(x0, 0, x0, 255, (245, 247, 250, 255), 2)
    c.border((245, 247, 250, 255), 3)
    return c


def color_chart() -> Canvas:
    c = Canvas(256, 256, (18, 20, 24, 255))
    patches = (
        (230, 52, 62, 255), (56, 190, 88, 255), (48, 112, 232, 255),
        (240, 201, 55, 255), (49, 201, 211, 255), (213, 65, 220, 255),
        (255, 255, 255, 255), (188, 188, 188, 255), (118, 118, 118, 255),
        (46, 46, 46, 255), (18, 18, 18, 255), (0, 0, 0, 255),
    )
    for index, color in enumerate(patches):
        x = 8 + (index % 3) * 82
        y = 42 + (index // 3) * 47
        c.rect(x, y, x + 76, y + 40, color)
    for x in range(8, 248):
        v = int(round((x - 8) * 255 / 239))
        c.rect(x, 232, x + 1, 248, (v, v, v, 255))
    banner(c, "SRGB CHART", (92, 150, 255, 255))
    c.border((230, 234, 238, 255), 3)
    return c


def mip_stress() -> Canvas:
    c = Canvas(256, 256, (28, 30, 35, 255))
    bands = ((1, (235, 74, 88, 255)), (2, (248, 188, 60, 255)), (4, (68, 210, 118, 255)),
             (8, (67, 185, 235, 255)), (16, (172, 104, 238, 255)))
    band_height = 44
    for index, (period, accent) in enumerate(bands):
        y0 = 34 + index * band_height
        for x in range(256):
            value = accent if (x // period) % 2 == 0 else (19, 21, 25, 255)
            c.rect(x, y0, x + 1, y0 + band_height, value)
        c.rect(5, y0 + 7, 58, y0 + 29, (10, 12, 15, 255))
        c.text(10, y0 + 11, f"{period}PX", (245, 247, 250, 255), 1)
    banner(c, "MIP STRESS", (235, 74, 88, 255))
    c.border((244, 246, 248, 255), 3)
    return c


def seam_test() -> Canvas:
    c = Canvas(256, 256, (36, 40, 47, 255))
    checker(c, 32, (36, 40, 47, 255), (53, 58, 66, 255))
    c.border((245, 75, 194, 255), 8)
    c.rect(0, 0, 28, 28, (250, 78, 72, 255))
    c.rect(228, 0, 256, 28, (74, 215, 109, 255))
    c.rect(0, 228, 28, 256, (72, 132, 240, 255))
    c.rect(228, 228, 256, 256, (250, 207, 68, 255))
    c.line(128, 8, 128, 248, (235, 238, 242, 255), 2)
    c.line(8, 128, 248, 128, (235, 238, 242, 255), 2)
    c.rect(65, 105, 191, 151, (15, 18, 22, 255))
    c.centered_text(113, "SEAM TEST", (245, 247, 250, 255), 2)
    c.centered_text(134, "EDGE MATCH", (202, 207, 213, 255), 1)
    return c


def npot_checker() -> Canvas:
    c = Canvas(300, 180, (32, 36, 42, 255))
    checker(c, 30, (38, 44, 52, 255), (207, 210, 214, 255))
    c.line(150, 0, 150, 179, (239, 76, 74, 255), 5)
    c.line(0, 90, 299, 90, (69, 208, 109, 255), 5)
    c.rect(66, 61, 234, 119, (15, 18, 22, 255))
    c.centered_text(70, "NPOT 300 X 180", (244, 247, 250, 255), 2)
    c.centered_text(96, "ASPECT 5 / 3", (194, 201, 209, 255), 1)
    c.border((244, 247, 250, 255), 4)
    return c


def numbered_tiles() -> Canvas:
    c = Canvas(256, 256, (25, 28, 34, 255))
    colors = ((76, 121, 210, 255), (60, 168, 113, 255), (211, 153, 54, 255), (183, 75, 118, 255))
    number = 0
    for y in range(4):
        for x in range(4):
            x0, y0 = x * 64, y * 64
            c.rect(x0 + 2, y0 + 2, x0 + 62, y0 + 62, colors[(x + y) % len(colors)])
            c.rect(x0 + 8, y0 + 18, x0 + 56, y0 + 49, (15, 17, 21, 255))
            c.text(x0 + 20, y0 + 25, f"{number:02d}", (246, 248, 250, 255), 2)
            number += 1
    return c


def world_units() -> Canvas:
    c = Canvas(256, 256, (224, 226, 222, 255))
    for x in range(0, 256, 8):
        height = 28 if x % 64 == 0 else (18 if x % 32 == 0 else 10)
        c.rect(x, 0, x + (3 if x % 64 == 0 else 1), height, (28, 31, 36, 255))
        c.rect(x, 256 - height, x + (3 if x % 64 == 0 else 1), 256, (28, 31, 36, 255))
    for y in range(0, 256, 8):
        length = 28 if y % 64 == 0 else (18 if y % 32 == 0 else 10)
        c.rect(0, y, length, y + (3 if y % 64 == 0 else 1), (28, 31, 36, 255))
        c.rect(256 - length, y, 256, y + (3 if y % 64 == 0 else 1), (28, 31, 36, 255))
    c.line(128, 28, 128, 228, (225, 68, 66, 255), 4)
    c.line(28, 128, 228, 128, (48, 177, 91, 255), 4)
    c.circle(128, 128, 52, (45, 50, 58, 255), 3)
    c.rect(55, 103, 201, 153, (22, 25, 30, 255))
    c.centered_text(110, "WORLD UNITS", (246, 248, 250, 255), 2)
    c.centered_text(134, "8 / 32 / 64", (214, 219, 224, 255), 1)
    return c


def flat(color: Color, label: str, accent: Color) -> Canvas:
    c = Canvas(256, 256, color)
    c.rect(20, 92, 236, 164, (16, 18, 22, 255))
    c.centered_text(107, label, (246, 248, 250, 255), 3 if len(label) <= 7 else 2)
    c.centered_text(139, "CALIBRATION", (184, 191, 199, 255), 1)
    c.border(accent, 6)
    return c


def semantic(label: str, base: Color, accent: Color, motif: str) -> Canvas:
    c = Canvas(256, 256, base)
    if motif == "crosshatch":
        hatch(c, 28, accent, 5)
        hatch(c, 28, accent, 5, True)
    elif motif == "chevron":
        for x in range(-64, 320, 64):
            c.line(x, 54, x + 42, 128, accent, 11)
            c.line(x + 42, 128, x, 202, accent, 11)
    elif motif == "x":
        for y in range(42, 230, 64):
            for x in range(14, 244, 64):
                c.line(x, y, x + 36, y + 36, accent, 7)
                c.line(x + 36, y, x, y + 36, accent, 7)
    elif motif == "arrows":
        c.arrow(35, 128, 221, 128, accent, 9)
        c.arrow(221, 173, 35, 173, accent, 7)
    elif motif == "portal":
        for inset in (18, 34, 50):
            c.rect(inset, 48 + inset // 2, 256 - inset, 225 - inset // 2, accent)
            c.rect(inset + 5, 53 + inset // 2, 251 - inset, 220 - inset // 2, base)
    elif motif == "waves":
        for radius in (28, 51, 75, 99):
            c.circle(128, 139, radius, accent, 5)
    elif motif == "ladder":
        c.line(76, 42, 76, 236, accent, 12)
        c.line(180, 42, 180, 236, accent, 12)
        for y in range(62, 225, 28):
            c.line(76, y, 180, y, accent, 9)
    elif motif == "rays":
        c.circle(128, 139, 29, accent, 12)
        for index in range(12):
            angle = index * math.tau / 12
            c.line(
                int(128 + math.cos(angle) * 50), int(139 + math.sin(angle) * 50),
                int(128 + math.cos(angle) * 93), int(139 + math.sin(angle) * 93),
                accent, 7,
            )
    elif motif == "spawn":
        c.circle(128, 139, 72, accent, 8)
        c.arrow(128, 194, 128, 77, accent, 13)
    else:
        hatch(c, 34, accent, 8)
    banner(c, label, accent)
    c.border((18, 20, 24, 255), 5)
    return c


def noise_byte(x: int, y: int, salt: int = 0) -> int:
    """Return a stable, well-distributed byte without process-randomized hash()."""
    value = (x * 0x1F123BB5) ^ (y * 0x05491333) ^ (salt * 0x045D9F3B)
    value = ((value ^ (value >> 16)) * 0x045D9F3B) & 0xFFFFFFFF
    value = ((value ^ (value >> 16)) * 0x045D9F3B) & 0xFFFFFFFF
    return (value ^ (value >> 16)) & 0xFF


def wood_planks() -> Canvas:
    c = Canvas(512, 512, (112, 71, 40, 255))
    plank_height = 64
    for y in range(512):
        plank = y // plank_height
        local_y = y % plank_height
        for x in range(512):
            grain = 8.0 * math.sin((x + plank * 37) * math.tau / 91.0 + math.sin(y * 0.11))
            fine = (noise_byte(x, y, 17) - 128) * 0.045
            edge = -9.0 if local_y < 5 or local_y > plank_height - 6 else 0.0
            base = 112 + plank % 3 * 5 + grain + fine + edge
            c.set(x, y, (int(base + 22), int(base), int(base - 30), 255))
        if local_y in (0, 1, 2):
            c.rect(0, y, 512, y + 1, (48, 34, 25, 255))
    for plank in range(8):
        seam_x = (plank % 2) * 256
        y0 = plank * plank_height
        c.rect(seam_x, y0, seam_x + 4, y0 + plank_height, (55, 38, 27, 255))
        for x in (seam_x + 14, (seam_x + 498) % 512):
            c.circle(x, y0 + 14, 4, (47, 35, 27, 255), 3)
            c.circle(x, y0 + 50, 4, (47, 35, 27, 255), 3)
    return c


def ceramic_tile() -> Canvas:
    c = Canvas(512, 512, (218, 220, 217, 255))
    tile_size = 128
    grout = 7
    for y in range(512):
        for x in range(512):
            local_x = x % tile_size
            local_y = y % tile_size
            if local_x < grout or local_y < grout:
                shade = 123 + (noise_byte(x, y, 29) % 13)
                c.set(x, y, (shade, shade + 2, shade + 3, 255))
                continue
            distance = min(local_x - grout, local_y - grout, tile_size - local_x, tile_size - local_y)
            bevel = max(0, 9 - distance)
            variation = ((x // tile_size) * 5 + (y // tile_size) * 7) % 8
            fine = (noise_byte(x, y, 31) - 128) // 32
            value = 217 + variation + bevel + fine
            c.set(x, y, (value, value + 1, value, 255))
    return c


def plaster_warm() -> Canvas:
    c = Canvas(512, 512, (190, 181, 168, 255))
    for y in range(512):
        for x in range(512):
            broad = 5.0 * math.sin(x * math.tau / 256.0) + 4.0 * math.cos(y * math.tau / 171.0)
            fine = (noise_byte(x, y, 41) - 128) * 0.035
            value = int(188 + broad + fine)
            c.set(x, y, (value + 5, value, value - 10, 255))
    for index in range(18):
        y = (index * 79 + 31) % 512
        x = (index * 113 + 17) % 512
        c.line(x - 34, y, x + 55, y + 11, (176, 168, 157, 255), 2)
    return c


def asphalt() -> Canvas:
    c = Canvas(512, 512, (65, 66, 65, 255))
    for y in range(512):
        for x in range(512):
            noise = noise_byte(x, y, 53)
            value = 56 + noise // 12
            if noise > 247:
                value += 34
            elif noise < 7:
                value -= 20
            c.set(x, y, (value + 3, value + 2, value, 255))
    for index in range(70):
        x = (index * 193 + 29) % 512
        y = (index * 139 + 71) % 512
        radius = 1 + noise_byte(x, y, 59) % 3
        shade = 88 + noise_byte(x, y, 61) % 42
        c.circle(x, y, radius, (shade, shade - 2, shade - 5, 255), radius + 1)
    return c


def hazard_red_white() -> Canvas:
    c = Canvas(512, 512, (0, 0, 0, 255))
    for y in range(512):
        for x in range(512):
            stripe = ((x + y) // 48) & 1
            noise = (noise_byte(x, y, 67) - 128) // 28
            if stripe:
                color = (219 + noise, 220 + noise, 214 + noise, 255)
            else:
                color = (158 + noise, 36 + noise, 43 + noise, 255)
            c.set(x, y, color)
    for coordinate in range(0, 512, 128):
        c.line(coordinate, 0, coordinate, 511, (38, 40, 43, 255), 4)
        c.line(0, coordinate, 511, coordinate, (38, 40, 43, 255), 4)
    return c


GENERATORS: dict[str, Callable[[], Canvas]] = {
    "diagnostics/diag_grid_metric.png": metric_grid,
    "diagnostics/diag_uv_checker.png": uv_checker,
    "diagnostics/diag_orientation_arrows.png": orientation_arrows,
    "diagnostics/diag_texel_density.png": texel_density,
    "diagnostics/diag_color_chart_srgb.png": color_chart,
    "diagnostics/diag_mip_stress.png": mip_stress,
    "diagnostics/diag_seam_test.png": seam_test,
    "diagnostics/diag_npot_checker.png": npot_checker,
    "diagnostics/diag_numbered_tiles.png": numbered_tiles,
    "diagnostics/diag_world_units.png": world_units,
    "diagnostics/diag_neutral_18.png": lambda: flat((118, 118, 118, 255), "NEUTRAL 18", (205, 208, 212, 255)),
    "diagnostics/diag_white.png": lambda: flat((255, 255, 255, 255), "WHITE", (78, 84, 92, 255)),
    "diagnostics/diag_black.png": lambda: flat((0, 0, 0, 255), "BLACK", (216, 220, 224, 255)),
    "diagnostics/diag_axis_x_red.png": lambda: flat((196, 42, 47, 255), "X RED", (255, 147, 147, 255)),
    "diagnostics/diag_axis_y_green.png": lambda: flat((31, 151, 73, 255), "Y GREEN", (143, 244, 172, 255)),
    "diagnostics/diag_axis_z_blue.png": lambda: flat((39, 83, 190, 255), "Z BLUE", (145, 181, 255, 255)),
    "diagnostics/diag_missing_magenta.png": lambda: semantic("MISSING", (25, 25, 28, 255), (238, 44, 218, 255), "crosshatch"),
    "semantics/sem_collision_solid.png": lambda: semantic("COLLISION", (17, 71, 79, 255), (64, 230, 234, 255), "crosshatch"),
    "semantics/sem_player_clip.png": lambda: semantic("PLAYER CLIP", (26, 55, 102, 255), (87, 155, 255, 255), "hatch"),
    "semantics/sem_ai_clip.png": lambda: semantic("AI CLIP", (26, 79, 53, 255), (80, 228, 139, 255), "hatch"),
    "semantics/sem_trigger.png": lambda: semantic("TRIGGER", (104, 53, 17, 255), (255, 155, 52, 255), "chevron"),
    "semantics/sem_nav_walkable.png": lambda: semantic("NAV WALK", (27, 83, 48, 255), (76, 228, 119, 255), "arrows"),
    "semantics/sem_nav_blocked.png": lambda: semantic("NAV BLOCK", (97, 29, 34, 255), (245, 76, 83, 255), "x"),
    "semantics/sem_nav_link.png": lambda: semantic("NAV LINK", (58, 36, 91, 255), (184, 100, 246, 255), "arrows"),
    "semantics/sem_visibility_portal.png": lambda: semantic("VIS PORTAL", (22, 69, 84, 255), (74, 220, 244, 255), "portal"),
    "semantics/sem_visibility_occluder.png": lambda: semantic("VIS OCCLUDE", (45, 48, 54, 255), (137, 146, 158, 255), "crosshatch"),
    "semantics/sem_audio_occluder.png": lambda: semantic("AUDIO BLOCK", (25, 46, 86, 255), (86, 151, 241, 255), "waves"),
    "semantics/sem_reverb_zone.png": lambda: semantic("REVERB", (59, 34, 87, 255), (187, 103, 241, 255), "waves"),
    "semantics/sem_light_volume.png": lambda: semantic("LIGHT VOL", (91, 72, 21, 255), (255, 217, 75, 255), "rays"),
    "semantics/sem_spawn.png": lambda: semantic("SPAWN", (18, 76, 73, 255), (61, 228, 204, 255), "spawn"),
    "semantics/sem_nodraw.png": lambda: semantic("NODRAW", (57, 26, 29, 255), (185, 53, 62, 255), "crosshatch"),
    "semantics/sem_sky.png": lambda: semantic("SKY", (48, 102, 151, 255), (136, 205, 255, 255), "rays"),
    "semantics/sem_ladder.png": lambda: semantic("LADDER", (87, 67, 18, 255), (250, 202, 55, 255), "ladder"),
    "surfaces/surface_asphalt.png": asphalt,
    "surfaces/surface_ceramic_tile_white.png": ceramic_tile,
    "surfaces/surface_hazard_red_white.png": hazard_red_white,
    "surfaces/surface_plaster_warm.png": plaster_warm,
    "surfaces/surface_wood_planks.png": wood_planks,
}


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF)


def encode_png(canvas: Canvas) -> bytes:
    signature = b"\x89PNG\r\n\x1a\n"
    header = struct.pack(">IIBBBBB", canvas.width, canvas.height, 8, 6, 0, 0, 0)
    stride = canvas.width * 4
    rows = bytearray()
    for y in range(canvas.height):
        rows.append(0)  # PNG filter type None, chosen for exact reproducibility.
        start = y * stride
        rows.extend(canvas.pixels[start : start + stride])
    return (
        signature
        + png_chunk(b"IHDR", header)
        + png_chunk(b"sRGB", b"\x00")
        + png_chunk(b"IDAT", zlib.compress(bytes(rows), level=9))
        + png_chunk(b"IEND", b"")
    )


def repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify checked-in output without writing")
    arguments = parser.parse_args()
    output_root = repository_root() / "assets" / "textures" / "dev"
    failures: list[str] = []

    for relative_path, generator in sorted(GENERATORS.items()):
        destination = output_root / relative_path
        encoded = encode_png(generator())
        if arguments.check:
            if not destination.is_file():
                failures.append(f"missing: {destination.relative_to(repository_root())}")
            elif destination.read_bytes() != encoded:
                failures.append(f"stale: {destination.relative_to(repository_root())}")
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(encoded)
        print(f"wrote {destination.relative_to(repository_root())}")

    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1

    action = "verified" if arguments.check else "generated"
    print(f"{action} {len(GENERATORS)} deterministic TileEditor textures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
