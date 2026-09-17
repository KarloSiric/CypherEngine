#!/usr/bin/env python3
"""Verify source-image invariants for the TileEditor Development Kit."""

from __future__ import annotations

import binascii
from pathlib import Path
import re
import struct
import sys
import zlib


EXPECTED_AUTHORED_SURFACES = {
    "surface_concrete_cast.png": (1024, 1024),
    "surface_masonry_block_gray.png": (1024, 1024),
    "surface_rubber_stud_floor.png": (1024, 1024),
    "surface_steel_panel_blue.png": (1024, 1024),
}
EXPECTED_NPOT = {"diag_npot_checker.png": (300, 180)}
SOURCE_PATTERN = re.compile(r'^\s*source\s*=\s*"([^"]+)"\s*$', re.MULTILINE)


def paeth(left: int, above: int, upper_left: int) -> int:
    prediction = left + above - upper_left
    distance_left = abs(prediction - left)
    distance_above = abs(prediction - above)
    distance_upper_left = abs(prediction - upper_left)
    if distance_left <= distance_above and distance_left <= distance_upper_left:
        return left
    if distance_above <= distance_upper_left:
        return above
    return upper_left


def decode_rgba8_png(path: Path) -> tuple[int, int, bytes]:
    encoded = path.read_bytes()
    if encoded[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("missing PNG signature")
    cursor = 8
    width = height = 0
    compressed = bytearray()
    while cursor < len(encoded):
        if cursor + 12 > len(encoded):
            raise ValueError("truncated PNG chunk")
        length = struct.unpack_from(">I", encoded, cursor)[0]
        kind = encoded[cursor + 4 : cursor + 8]
        payload_start = cursor + 8
        payload_end = payload_start + length
        crc_offset = payload_end
        if crc_offset + 4 > len(encoded):
            raise ValueError("truncated PNG payload")
        expected_crc = struct.unpack_from(">I", encoded, crc_offset)[0]
        actual_crc = binascii.crc32(kind + encoded[payload_start:payload_end]) & 0xFFFFFFFF
        if expected_crc != actual_crc:
            raise ValueError(f"bad CRC in {kind.decode('ascii', errors='replace')} chunk")
        payload = encoded[payload_start:payload_end]
        if kind == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
            if (bit_depth, color_type, compression, filtering, interlace) != (8, 6, 0, 0, 0):
                raise ValueError("expected non-interlaced RGBA8 PNG")
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        cursor = crc_offset + 4
    if width <= 0 or height <= 0 or not compressed:
        raise ValueError("missing PNG header or image data")

    filtered = zlib.decompress(bytes(compressed))
    stride = width * 4
    expected_size = height * (stride + 1)
    if len(filtered) != expected_size:
        raise ValueError(f"decoded byte count {len(filtered)} does not equal {expected_size}")

    pixels = bytearray(height * stride)
    source = 0
    for y in range(height):
        filter_type = filtered[source]
        source += 1
        row = bytearray(filtered[source : source + stride])
        source += stride
        previous_start = (y - 1) * stride
        for x in range(stride):
            left = row[x - 4] if x >= 4 else 0
            above = pixels[previous_start + x] if y > 0 else 0
            upper_left = pixels[previous_start + x - 4] if y > 0 and x >= 4 else 0
            if filter_type == 1:
                row[x] = (row[x] + left) & 0xFF
            elif filter_type == 2:
                row[x] = (row[x] + above) & 0xFF
            elif filter_type == 3:
                row[x] = (row[x] + ((left + above) // 2)) & 0xFF
            elif filter_type == 4:
                row[x] = (row[x] + paeth(left, above, upper_left)) & 0xFF
            elif filter_type != 0:
                raise ValueError(f"unsupported PNG filter {filter_type}")
        destination = y * stride
        pixels[destination : destination + stride] = row
    return width, height, bytes(pixels)


def repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def main() -> int:
    root = repository_root()
    texture_root = root / "assets" / "textures" / "dev"
    failures: list[str] = []
    decoded: dict[Path, tuple[int, int, bytes]] = {}

    recipes = sorted(texture_root.rglob("*.cytex"))
    source_paths: set[Path] = set()
    for recipe in recipes:
        match = SOURCE_PATTERN.search(recipe.read_text(encoding="utf-8"))
        if not match:
            failures.append(f"{recipe.relative_to(root)}: missing source field")
            continue
        source = root / "assets" / match.group(1)
        source_paths.add(source)
        if not source.is_file():
            failures.append(f"{recipe.relative_to(root)}: source does not exist: {match.group(1)}")

    for path in sorted(source_paths):
        try:
            width, height, pixels = decode_rgba8_png(path)
        except (OSError, ValueError, zlib.error) as error:
            failures.append(f"{path.relative_to(root)}: {error}")
            continue
        decoded[path] = (width, height, pixels)
        if any(pixels[index] != 255 for index in range(3, len(pixels), 4)):
            failures.append(f"{path.relative_to(root)}: positive source contains non-opaque pixels")
        expected = EXPECTED_AUTHORED_SURFACES.get(path.name) or EXPECTED_NPOT.get(path.name)
        if expected and (width, height) != expected:
            failures.append(f"{path.relative_to(root)}: {(width, height)} != expected {expected}")

    for name in EXPECTED_AUTHORED_SURFACES:
        path = texture_root / "surfaces" / name
        if path not in decoded:
            failures.append(f"{path.relative_to(root)}: authored surface was not decoded")
            continue
        width, height, pixels = decoded[path]
        stride = width * 4
        for y in range(height):
            left = pixels[y * stride : y * stride + 4]
            right = pixels[(y + 1) * stride - 4 : (y + 1) * stride]
            if left != right:
                failures.append(f"{path.relative_to(root)}: left/right repeat edge mismatch at row {y}")
                break
        top = pixels[:stride]
        bottom = pixels[(height - 1) * stride : height * stride]
        if top != bottom:
            failures.append(f"{path.relative_to(root)}: top/bottom repeat edge mismatch")

    manifest = root / "assets" / "tileeditor_dev_pack.rsp"
    inputs = [
        line.strip()
        for line in manifest.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    if len(inputs) != 119:
        failures.append(f"assets/tileeditor_dev_pack.rsp: {len(inputs)} inputs != expected 119")
    if len(inputs) != len(set(inputs)):
        failures.append("assets/tileeditor_dev_pack.rsp: duplicate resource inputs")
    for resource in inputs:
        if not (root / "assets" / resource).is_file():
            failures.append(f"assets/tileeditor_dev_pack.rsp: missing input {resource}")

    material_basenames: dict[str, Path] = {}
    for material in sorted((root / "assets" / "materials" / "dev").rglob("*.cymat")):
        previous = material_basenames.get(material.stem)
        if previous:
            failures.append(
                f"duplicate browser label {material.stem}: {previous.relative_to(root)} and {material.relative_to(root)}"
            )
        material_basenames[material.stem] = material

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1
    print(
        f"verified {len(source_paths)} unique RGBA8 opaque PNG sources, "
        f"{len(recipes)} texture recipes, {len(material_basenames)} unique material labels, "
        f"and {len(inputs)} manifest inputs"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
