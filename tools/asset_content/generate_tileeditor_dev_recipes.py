#!/usr/bin/env python3
"""Generate the V1 CYTEX/CYMAT recipes and response manifest for the dev pack.

The TileEditor currently has a deliberately narrow material-preview adapter.
Every generated material therefore references the shared tile_surface shader,
binds exactly one base_color texture, and uses only tint and uv_scale.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys


@dataclass(frozen=True)
class TextureSpec:
    category: str
    name: str
    source_name: str | None = None
    color_space: str = "srgb"
    generate_mips: bool = True

    @property
    def source_stem(self) -> str:
        return self.source_name or self.name

    @property
    def recipe_path(self) -> str:
        return f"textures/dev/{self.category}/{self.name}.cytex"

    @property
    def source_path(self) -> str:
        return f"textures/dev/{self.category}/{self.source_stem}.png"


@dataclass(frozen=True)
class MaterialSpec:
    category: str
    name: str
    texture_path: str
    tint: tuple[float, float, float, float] = (1.0, 1.0, 1.0, 1.0)
    uv_scale: tuple[float, float] = (1.0, 1.0)

    @property
    def recipe_path(self) -> str:
        return f"materials/dev/{self.category}/{self.name}.cymat"


TEXTURES = (
    TextureSpec("diagnostics", "diag_axis_x_red"),
    TextureSpec("diagnostics", "diag_axis_y_green"),
    TextureSpec("diagnostics", "diag_axis_z_blue"),
    TextureSpec("diagnostics", "diag_black"),
    TextureSpec("diagnostics", "diag_color_chart_srgb"),
    TextureSpec("diagnostics", "diag_grid_metric"),
    TextureSpec("diagnostics", "diag_mip_stress"),
    TextureSpec("diagnostics", "diag_missing_magenta"),
    TextureSpec("diagnostics", "diag_neutral_18"),
    TextureSpec("diagnostics", "diag_npot_checker"),
    TextureSpec("diagnostics", "diag_numbered_tiles"),
    TextureSpec("diagnostics", "diag_orientation_arrows"),
    TextureSpec("diagnostics", "diag_seam_test"),
    TextureSpec("diagnostics", "diag_texel_density"),
    TextureSpec("diagnostics", "diag_uv_checker"),
    TextureSpec("diagnostics", "diag_white"),
    TextureSpec("diagnostics", "diag_world_units"),
    TextureSpec("semantics", "sem_ai_clip"),
    TextureSpec("semantics", "sem_audio_occluder"),
    TextureSpec("semantics", "sem_collision_solid"),
    TextureSpec("semantics", "sem_ladder"),
    TextureSpec("semantics", "sem_light_volume"),
    TextureSpec("semantics", "sem_nav_blocked"),
    TextureSpec("semantics", "sem_nav_link"),
    TextureSpec("semantics", "sem_nav_walkable"),
    TextureSpec("semantics", "sem_nodraw"),
    TextureSpec("semantics", "sem_player_clip"),
    TextureSpec("semantics", "sem_reverb_zone"),
    TextureSpec("semantics", "sem_sky"),
    TextureSpec("semantics", "sem_spawn"),
    TextureSpec("semantics", "sem_trigger"),
    TextureSpec("semantics", "sem_visibility_occluder"),
    TextureSpec("semantics", "sem_visibility_portal"),
    TextureSpec("surfaces", "surface_asphalt"),
    TextureSpec("surfaces", "surface_ceramic_tile_white"),
    TextureSpec("surfaces", "surface_concrete_cast"),
    TextureSpec("surfaces", "surface_hazard_red_white"),
    TextureSpec("surfaces", "surface_masonry_block_gray"),
    TextureSpec("surfaces", "surface_plaster_warm"),
    TextureSpec("surfaces", "surface_rubber_stud_floor"),
    TextureSpec("surfaces", "surface_steel_panel_blue"),
    TextureSpec("surfaces", "surface_wood_planks"),
)


EXTRA_TEXTURES = (
    TextureSpec(
        "diagnostics",
        "diag_color_chart_linear",
        source_name="diag_color_chart_srgb",
        color_space="linear",
        generate_mips=False,
    ),
    TextureSpec(
        "diagnostics",
        "diag_mip_stress_nomips",
        source_name="diag_mip_stress",
        generate_mips=False,
    ),
)


def identity_material(texture: TextureSpec) -> MaterialSpec:
    return MaterialSpec(texture.category, texture.name, texture.recipe_path)


VARIANT_MATERIALS = (
    MaterialSpec("diagnostics", "diag_grid_metric_coarse", "textures/dev/diagnostics/diag_grid_metric.cytex", uv_scale=(0.5, 0.5)),
    MaterialSpec("diagnostics", "diag_grid_metric_dense", "textures/dev/diagnostics/diag_grid_metric.cytex", uv_scale=(4.0, 4.0)),
    MaterialSpec("diagnostics", "diag_uv_checker_repeat_2", "textures/dev/diagnostics/diag_uv_checker.cytex", uv_scale=(2.0, 2.0)),
    MaterialSpec("diagnostics", "diag_uv_checker_repeat_4", "textures/dev/diagnostics/diag_uv_checker.cytex", uv_scale=(4.0, 4.0)),
    MaterialSpec("diagnostics", "diag_uv_checker_mirror_u", "textures/dev/diagnostics/diag_uv_checker.cytex", uv_scale=(-1.0, 1.0)),
    MaterialSpec("diagnostics", "diag_uv_checker_mirror_v", "textures/dev/diagnostics/diag_uv_checker.cytex", uv_scale=(1.0, -1.0)),
    MaterialSpec("diagnostics", "diag_color_chart_linear", "textures/dev/diagnostics/diag_color_chart_linear.cytex"),
    MaterialSpec("diagnostics", "diag_mip_stress_nomips", "textures/dev/diagnostics/diag_mip_stress_nomips.cytex"),
    MaterialSpec("blockout", "blockout_neutral", "textures/dev/diagnostics/diag_grid_metric.cytex", tint=(0.48, 0.55, 0.58, 1.0)),
    MaterialSpec("blockout", "blockout_concrete", "textures/dev/diagnostics/diag_grid_metric.cytex", tint=(0.48, 0.50, 0.53, 1.0)),
    MaterialSpec("blockout", "blockout_steel", "textures/dev/diagnostics/diag_grid_metric.cytex", tint=(0.28, 0.38, 0.43, 1.0)),
    MaterialSpec("blockout", "blockout_hazard", "textures/dev/diagnostics/diag_grid_metric.cytex", tint=(0.82, 0.58, 0.13, 1.0)),
    MaterialSpec("blockout", "blockout_trim", "textures/dev/diagnostics/diag_grid_metric.cytex", tint=(0.16, 0.19, 0.22, 1.0)),
    MaterialSpec("blockout", "blockout_exterior", "textures/dev/diagnostics/diag_grid_metric.cytex", tint=(0.43, 0.40, 0.36, 1.0)),
    MaterialSpec("blockout", "blockout_accent_blue", "textures/dev/diagnostics/diag_grid_metric.cytex", tint=(0.10, 0.52, 0.73, 1.0)),
    MaterialSpec("blockout", "blockout_accent_orange", "textures/dev/diagnostics/diag_grid_metric.cytex", tint=(0.92, 0.39, 0.10, 1.0)),
    MaterialSpec("surfaces", "surface_concrete_cast_dark", "textures/dev/surfaces/surface_concrete_cast.cytex", tint=(0.65, 0.67, 0.70, 1.0)),
    MaterialSpec("surfaces", "surface_masonry_block_gray_dense", "textures/dev/surfaces/surface_masonry_block_gray.cytex", uv_scale=(2.0, 2.0)),
    MaterialSpec("surfaces", "surface_rubber_stud_floor_dense", "textures/dev/surfaces/surface_rubber_stud_floor.cytex", uv_scale=(2.0, 2.0)),
    MaterialSpec("surfaces", "surface_steel_panel_blue_dense", "textures/dev/surfaces/surface_steel_panel_blue.cytex", uv_scale=(2.0, 2.0)),
)


def texture_recipe(spec: TextureSpec) -> str:
    mips = "true" if spec.generate_mips else "false"
    return (
        '@cykv 1\n'
        '@schema "cypher.texture" 1\n'
        '{\n'
        f'    source = "{spec.source_path}"\n'
        '    usage = "color"\n'
        f'    color_space = "{spec.color_space}"\n'
        f'    generate_mips = {mips}\n'
        '}\n'
    )


def decimal(value: float) -> str:
    return f"{value:.2f}"


def material_recipe(spec: MaterialSpec) -> str:
    tint = ", ".join(decimal(value) for value in spec.tint)
    uv_scale = ", ".join(decimal(value) for value in spec.uv_scale)
    return (
        '@cykv 1\n'
        '@schema "cypher.material" 1\n'
        '{\n'
        '    shader = "shaders/tile_surface.cyshader"\n'
        f'    textures = {{ base_color = "{spec.texture_path}" }}\n'
        f'    parameters = {{ tint = [{tint}] uv_scale = [{uv_scale}] }}\n'
        '}\n'
    )


def repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def write_or_check(root: Path, relative: str, content: str, check: bool, failures: list[str]) -> None:
    path = root / relative
    encoded = content.encode("utf-8")
    if check:
        if not path.is_file():
            failures.append(f"missing: {relative}")
        elif path.read_bytes() != encoded:
            failures.append(f"stale: {relative}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encoded)
    print(f"wrote {relative}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify recipes and manifest without writing")
    arguments = parser.parse_args()
    root = repository_root()
    failures: list[str] = []

    all_textures = TEXTURES + EXTRA_TEXTURES
    all_materials = tuple(identity_material(spec) for spec in TEXTURES) + VARIANT_MATERIALS

    for spec in all_textures:
        write_or_check(root, f"assets/{spec.recipe_path}", texture_recipe(spec), arguments.check, failures)
    for spec in all_materials:
        write_or_check(root, f"assets/{spec.recipe_path}", material_recipe(spec), arguments.check, failures)

    inputs = (
        "shaders/tile_surface.cyshader",
        "shaders/dev/normal_object.cyshader",
        "shaders/dev/normal_world.cyshader",
        "shaders/dev/primitive_id.cyshader",
        "shaders/dev/unlit_base_color.cyshader",
        "shaders/dev/uv_checker.cyshader",
        "shaders/dev/uv_visualization.cyshader",
        "textures/dev/grid.cytex",
        "textures/dev/brick.cytex",
        "textures/dev/hazard.cytex",
        *(spec.recipe_path for spec in all_textures),
        "materials/dev/grid.cymat",
        "materials/dev/brick.cymat",
        "materials/dev/hazard.cymat",
        *(spec.recipe_path for spec in all_materials),
    )
    manifest = "# TileEditor Development Kit v1: one resource input per line.\n" + "\n".join(inputs) + "\n"
    write_or_check(root, "assets/tileeditor_dev_pack.rsp", manifest, arguments.check, failures)

    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1
    action = "verified" if arguments.check else "generated"
    print(f"{action} {len(all_textures)} texture recipes, {len(all_materials)} material recipes, and one manifest")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
