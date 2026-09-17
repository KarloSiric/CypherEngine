#!/usr/bin/env python3
"""Generate the checked-in TileEditor material and geometry development lab."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys


MATERIALS = {
    8: "materials/dev/diagnostics/diag_uv_checker.cymat",
    9: "materials/dev/diagnostics/diag_uv_checker_repeat_4.cymat",
    10: "materials/dev/diagnostics/diag_uv_checker_mirror_u.cymat",
    11: "materials/dev/diagnostics/diag_texel_density.cymat",
    12: "materials/dev/diagnostics/diag_seam_test.cymat",
    13: "materials/dev/diagnostics/diag_mip_stress.cymat",
    14: "materials/dev/diagnostics/diag_mip_stress_nomips.cymat",
    15: "materials/dev/diagnostics/diag_color_chart_srgb.cymat",
    16: "materials/dev/diagnostics/diag_color_chart_linear.cymat",
    17: "materials/dev/surfaces/surface_concrete_cast.cymat",
    18: "materials/dev/surfaces/surface_steel_panel_blue.cymat",
    19: "materials/dev/surfaces/surface_masonry_block_gray.cymat",
    20: "materials/dev/surfaces/surface_rubber_stud_floor.cymat",
    21: "materials/dev/surfaces/surface_wood_planks.cymat",
    22: "materials/dev/surfaces/surface_ceramic_tile_white.cymat",
    23: "materials/dev/surfaces/surface_plaster_warm.cymat",
    24: "materials/dev/surfaces/surface_asphalt.cymat",
    25: "materials/dev/surfaces/surface_hazard_red_white.cymat",
    26: "materials/dev/blockout/blockout_neutral.cymat",
    27: "materials/dev/semantics/sem_trigger.cymat",
    28: "materials/dev/semantics/sem_collision_solid.cymat",
    29: "materials/dev/semantics/sem_nav_walkable.cymat",
    30: "materials/dev/semantics/sem_audio_occluder.cymat",
    31: "materials/dev/diagnostics/diag_missing_magenta.cymat",
    65535: "materials/dev/diagnostics/diag_uv_checker.cymat",
}


@dataclass(frozen=True)
class Cell:
    x: int
    y: int
    floor: int
    walls: int
    material: int
    shape: str = "flat"
    steps: int = 8


def repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def build_cells() -> list[Cell]:
    cells: dict[tuple[int, int], Cell] = {}

    def add(cell: Cell) -> None:
        coordinate = (cell.x, cell.y)
        if coordinate in cells:
            raise ValueError(f"duplicate cell {coordinate}")
        cells[coordinate] = cell

    # Twenty-four 2x2 gallery plinths. Separation creates exposed walls so each
    # material is visible on horizontal and vertical generated geometry.
    gallery_slots = list(range(8, 32))
    for index, slot in enumerate(gallery_slots):
        origin_x = 1 + (index % 8) * 4
        origin_y = 1 + (index // 8) * 4
        floor = (-1, 0, 1)[index % 3]
        walls = 1 + index % 3
        for dy in range(2):
            for dx in range(2):
                add(Cell(origin_x + dx, origin_y + dy, floor, walls, slot))

    # Explicit duplicate-path/high-slot case and an unbound unknown-material
    # case. Slot 65534 is intentionally absent from MATERIALS.
    add(Cell(26, 13, 0, 2, 65535))
    add(Cell(28, 13, 0, 2, 65534))

    # Four isolated stair lanes exercise every orientation and the complete
    # 2/8/16/32 step range. A stair starts at its lower floor and rises one level
    # toward the named cardinal direction.
    add(Cell(4, 18, 0, 1, 17))
    add(Cell(5, 18, 0, 1, 13, "stairs_east", 2))
    add(Cell(6, 18, 1, 2, 18))

    add(Cell(10, 18, 1, 2, 19))
    add(Cell(11, 18, 0, 1, 12, "stairs_west", 8))
    add(Cell(12, 18, 0, 1, 20))

    add(Cell(18, 18, 1, 3, 21))
    add(Cell(18, 19, 0, 1, 11, "stairs_north", 16))
    add(Cell(18, 20, 0, 1, 22))

    add(Cell(24, 18, 0, 1, 23))
    add(Cell(24, 19, 0, 1, 10, "stairs_south", 32))
    add(Cell(24, 20, 1, 3, 24))

    return sorted(cells.values(), key=lambda cell: (cell.y, cell.x))


def cell_text(cell: Cell) -> str:
    fields = (
        f'"x" = {cell.x}u "y" = {cell.y}u '
        f'"floor_level" = {cell.floor} "wall_height_levels" = {cell.walls}u '
        f'"material_slot" = {cell.material}u "flags" = 1u'
    )
    if cell.shape != "flat":
        fields += f' "shape" = "{cell.shape}" "stair_steps" = {cell.steps}u'
    return f"    {{ {fields} }}"


def generate() -> str:
    material_lines = [
        f'    {{ "slot" = {slot}u "path" = "{path}" }}'
        for slot, path in sorted(MATERIALS.items())
    ]
    cell_lines = [cell_text(cell) for cell in build_cells()]
    marker_lines = [
        '    { "id" = "d4ae7742-2ae1-4ad1-8c62-0bea93ba0001" "kind" = "player_spawn" "x" = 1u "y" = 1u "yaw_degrees" = 0.0 }',
        '    { "id" = "d4ae7742-2ae1-4ad1-8c62-0bea93ba0002" "kind" = "door" "x" = 4u "y" = 18u "side" = "west" }',
        '    { "id" = "d4ae7742-2ae1-4ad1-8c62-0bea93ba0003" "kind" = "door" "x" = 12u "y" = 18u "side" = "east" }',
        '    { "id" = "d4ae7742-2ae1-4ad1-8c62-0bea93ba0004" "kind" = "door" "x" = 18u "y" = 18u "side" = "north" }',
        '    { "id" = "d4ae7742-2ae1-4ad1-8c62-0bea93ba0005" "kind" = "door" "x" = 24u "y" = 20u "side" = "south" }',
    ]
    return "\n".join(
        (
            "@cykv 1",
            '@schema "cypher.map" 3',
            "",
            "// TileEditor Development Kit material and generated-geometry lab.",
            "// Semantic-label materials are visual only and carry no gameplay behavior.",
            "{",
            '  "map_id" = "d4ae7742-2ae1-4ad1-8c62-0bea93ba0000"',
            '  "dimensions" = { "width" = 32u "height" = 24u }',
            '  "metrics" = { "cell_size" = 4.0 "level_height" = 2.0 }',
            '  "materials" = [',
            ",\n".join(material_lines),
            "  ]",
            '  "cells" = [',
            ",\n".join(cell_lines),
            "  ]",
            '  "markers" = [',
            ",\n".join(marker_lines),
            "  ]",
            "}",
            "",
        )
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify the checked-in map without writing")
    arguments = parser.parse_args()
    destination = repository_root() / "assets" / "maps" / "tile_editor_dev_lab.cymap"
    encoded = generate().encode("utf-8")
    if arguments.check:
        if not destination.is_file():
            print(f"missing: {destination.relative_to(repository_root())}", file=sys.stderr)
            return 1
        if destination.read_bytes() != encoded:
            print(f"stale: {destination.relative_to(repository_root())}", file=sys.stderr)
            return 1
        print(f"verified {destination.relative_to(repository_root())} ({len(build_cells())} cells, {len(MATERIALS)} bindings)")
        return 0
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(encoded)
    print(f"wrote {destination.relative_to(repository_root())} ({len(build_cells())} cells, {len(MATERIALS)} bindings)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
