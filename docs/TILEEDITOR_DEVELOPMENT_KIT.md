<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/TILEEDITOR_DEVELOPMENT_KIT.md
//  Purpose: Defines the TileEditor development-asset kit and its validation.
//  Details: Records the live V1 contract, catalog, provenance, limitations, and
//           the staged asset program required by the larger Mason design.
//
//  History:
//  - Created by Karlo Siric on 2026-09-17
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# TileEditor Development Kit

## 1. Purpose

The TileEditor Development Kit is the first coherent map-authoring asset library
for CypherEngine. It gives the current editor useful graybox surfaces and, more
significantly, gives developers controlled inputs for finding defects in UVs,
texture scale, color conversion, mip generation, material binding, generated
geometry, and map semantics.

This kit is built for the engine that exists now. Its production materials use
the narrow V1 path supported end to end by Project Materials, the resource
compiler, orthographic views, the embedded 3D viewport, and the standalone F6
preview. Richer V2 formats remain valuable, but using them here would create
files that compile at the command line and then fail in the TileEditor.

The first version adds:

- 38 byte-reproducible procedural PNG sources;
- four high-detail authored surface sources;
- 44 new V1 `.cytex` recipes, including explicit sRGB/linear and mip/no-mip A/B
  pairs;
- 62 new V1 `.cymat` recipes, including identity, repeat, mirror, tint, scale,
  surface, semantic-label, and blockout variants;
- six standalone GLSL 410 debug shaders;
- one checked-in response manifest containing 119 cook inputs;
- one deterministic 110-cell development-lab map with 25 bindings;
- deterministic generators and `--check` modes;
- CMake staging for the complete manifest; and
- provenance, operating limits, known discrepancies, and an expansion plan tied
  to the ten-module Mason blueprint.

The historical `grid`, `brick`, and `hazard` resources remain at their original
paths. Existing maps keep resolving those stable identities.

## 2. Live compatibility contract

Every material intended for current TileEditor use follows this exact path:

```text
opaque RGBA8 PNG
  -> cypher.texture source schema V1
  -> CYTX cooked texture
  -> one base_color binding
  -> cypher.material source schema V1
  -> CYMT cooked material
  -> shaders/tile_surface.cyshader
  -> optional tint vec4 and uv_scale vec2
```

The adapter rejects a material when any of these conditions is violated:

- the shader path differs from `shaders/tile_surface.cyshader`;
- the material has zero textures, more than one texture, or a binding not named
  `base_color`;
- it contains a parameter other than `tint` or `uv_scale`;
- `tint` is not a finite four-component value;
- `uv_scale` is not a finite two-component value or either component is zero;
- the cooked texture is not RGBA8 UNORM/sRGB color data; or
- a file or aggregate decoded-image budget is exceeded.

Negative UV scale is supported and deliberately exercised by the mirror
materials. Alpha is not part of the positive asset contract. The orthographic
path multiplies source and tint alpha, while both 3D paths disable blending and
the shared fragment shader forces output alpha to one.

### 2.1 Current limits

| Limit | Current value | Consequence for this kit |
| --- | ---: | --- |
| Browser-discovered `.cymat` files | 512 | 65 total dev materials remain comfortably inside the bound |
| Material bindings per map | 256 | A gallery should reserve slots 8 and above and select a focused subset |
| One decoded base image | 64 MiB | Largest source is 1024×1024 RGBA8, about 4 MiB before mips |
| Complete bound base-image set | 512 MiB | The whole source set is far below the bound; maps should still bind only what they use |
| Cooked material file | 1 MiB | V1 recipes are only hundreds of bytes |
| Cooked texture file | 96 MiB | Largest generated chain is safely below the bound |
| Cell surface assignment | One `u16` slot | Floor, wall, cliff, and stair boxes from one cell share a material |
| UV projection | 0–1 per generated box face | Physical texel density is not consistent across differently sized boxes |

Doors remain diagnostic orange and do not receive a bound texture. There are no
independent floor, wall, ceiling, tread, riser, trim, or per-face slots.

## 3. Directory and build organization

```text
assets/
  tileeditor_dev_pack.rsp
  maps/tile_editor_dev_lab.cymap
  materials/dev/
    blockout/
    diagnostics/
    semantics/
    surfaces/
  shaders/dev/
  textures/dev/
    diagnostics/
    semantics/
    surfaces/
tools/asset_content/
  generate_tileeditor_dev_lab.py
  generate_tileeditor_dev_recipes.py
  generate_tileeditor_dev_textures.py
```

Authored source remains in `assets/`. Cooked `*.cyshader_c`, `*.cytex_c`, and
`*.cymat_c` products remain in build or cache output and are not source assets.

`assets/tileeditor_dev_pack.rsp` is the authoritative batch input list. CMake
reads that manifest, computes the cooked output paths, tracks PNG and GLSL stage
files as dependencies, and stages the complete pack through
`cypher_tile_material_assets`. The same manifest works directly with the CLI:

```sh
out/build/tile-editor-debug/bin/CypherResourceCompiler validate \
  -s assets --target host --profile development \
  --color never --progress none \
  @assets/tileeditor_dev_pack.rsp

out/build/tile-editor-debug/bin/CypherResourceCompiler compile \
  -s assets -o out/dev-resources \
  --target host --profile development \
  --color never --progress none \
  @assets/tileeditor_dev_pack.rsp
```

## 4. Texture catalog

### 4.1 Geometry, UV, and render diagnostics

| Source | Size | What it reveals |
| --- | ---: | --- |
| `diag_grid_metric` | 256² | Major/minor cadence, axis orientation, repetition, and stretching |
| `diag_uv_checker` | 256² | Per-cell labels, U/V direction, mirroring, rotation, and interpolation |
| `diag_orientation_arrows` | 256² | Face orientation, top/right/left identity, and winding mistakes |
| `diag_texel_density` | 256² | Four spatial frequencies for relative density and minification |
| `diag_world_units` | 256² | 8/32/64-unit visual cadence and world-axis alignment |
| `diag_numbered_tiles` | 256² | Repeat order, atlas-cell identity, and transformed orientation |
| `diag_seam_test` | 256² | Unique edges/corners, repeat seams, half-texel errors, and edge mips |
| `diag_mip_stress` | 256² | 1/2/4/8/16-pixel frequency bands and mip-transition stability |
| `diag_npot_checker` | 300×180 | NPOT import, odd mip reduction, and aspect preservation |
| `diag_color_chart_srgb` | 256² | Primary/secondary patches, dark values, grayscale, and sRGB presentation |
| `diag_neutral_18` | 256² | Neutral midtone lighting and tint calibration |
| `diag_white` | 256² | Tint-only behavior and white-point sanity |
| `diag_black` | 256² | Black level and unintended additive contribution |
| `diag_axis_x_red` | 256² | X-axis color convention and tint transport |
| `diag_axis_y_green` | 256² | Y-axis color convention and tint transport |
| `diag_axis_z_blue` | 256² | Z-axis color convention and tint transport |
| `diag_missing_magenta` | 256² | Conspicuous missing-resource substitution |

Two additional recipes reuse diagnostic PNGs with different import policy:

- `diag_color_chart_linear.cytex` declares the color chart as linear and
  disables generated mips, providing a controlled sRGB/linear A/B comparison.
- `diag_mip_stress_nomips.cytex` disables generated mips, providing a direct
  minification comparison against `diag_mip_stress.cytex`.

### 4.2 Authoring-semantic visual language

| Source | Visual convention | Intended authoring use |
| --- | --- | --- |
| `sem_collision_solid` | cyan crosshatch | collision volume placeholder |
| `sem_player_clip` | blue diagonal stripes | player-only clipping placeholder |
| `sem_ai_clip` | green diagonal stripes | AI clipping placeholder |
| `sem_trigger` | orange chevrons | trigger volume placeholder |
| `sem_nav_walkable` | green bidirectional arrows | walkable navigation region |
| `sem_nav_blocked` | red repeated X marks | blocked navigation region |
| `sem_nav_link` | purple opposing arrows | off-mesh/navigation link |
| `sem_visibility_portal` | cyan nested portal frames | room/portal boundary |
| `sem_visibility_occluder` | gray crosshatch | visibility occluder placeholder |
| `sem_audio_occluder` | blue concentric waves | acoustic occluder placeholder |
| `sem_reverb_zone` | violet concentric waves | reverb-zone placeholder |
| `sem_light_volume` | yellow radial mark | light/probe influence placeholder |
| `sem_spawn` | teal upward marker | spawn or placement point region |
| `sem_ladder` | yellow ladder glyph | climbable-surface placeholder |
| `sem_sky` | blue radial mark | sky/exterior boundary placeholder |
| `sem_nodraw` | dark-red crosshatch | hidden/non-rendered surface placeholder |

These are communication assets. A painted `sem_collision_solid` cell does not
acquire collision behavior, and a painted `sem_trigger` cell does not acquire a
trigger component. Authoritative semantics require typed map/entity/surface
data and a runtime consumer. The naming and patterns reserve a consistent visual
language without pretending that subsystem exists.

### 4.3 Readable level-design surfaces

| Source | Size | Character and diagnostic value |
| --- | ---: | --- |
| `surface_concrete_cast` | 1024² | neutral cast concrete with pores and restrained wear |
| `surface_steel_panel_blue` | 1024² | blue-gray modular panels, seams, fasteners, and wear |
| `surface_masonry_block_gray` | 1024² | large gray masonry bond for wall scale and orientation |
| `surface_rubber_stud_floor` | 1024² | dark industrial stud floor with a clear walkable repeat |
| `surface_wood_planks` | 512² | directional plank grain, staggered seams, and fasteners |
| `surface_ceramic_tile_white` | 512² | regular ceramic grid for strict alignment and scale checks |
| `surface_plaster_warm` | 512² | low-frequency warm plaster with restrained trowel marks |
| `surface_asphalt` | 512² | low-contrast exterior ground with sparse aggregate |
| `surface_hazard_red_white` | 512² | high-contrast danger stripes divided into modular panels |

The original `brick` and yellow/black `hazard` sources remain useful parts of
the surface-design set at their existing top-level paths.

## 5. Material catalog

Every base texture receives an identity material. Additional variants exercise
behavior that identical white-tinted `[1,1]` materials cannot cover.

### 5.1 UV and import-policy variants

| Material | Distinguishing value | Test |
| --- | --- | --- |
| `diag_grid_metric_coarse` | `uv_scale = [0.5, 0.5]` | sub-unit coverage and stretching |
| `diag_grid_metric_dense` | `uv_scale = [4, 4]` | high repetition and minification |
| `diag_uv_checker_repeat_2` | `uv_scale = [2, 2]` | positive repeat |
| `diag_uv_checker_repeat_4` | `uv_scale = [4, 4]` | denser positive repeat |
| `diag_uv_checker_mirror_u` | `uv_scale = [-1, 1]` | horizontal mirror |
| `diag_uv_checker_mirror_v` | `uv_scale = [1, -1]` | vertical mirror |
| `diag_color_chart_linear` | linear, no-mip descriptor | color-space handling |
| `diag_mip_stress_nomips` | no-mip descriptor | minification without a mip chain |

### 5.2 Blockout role variants

Eight materials reuse `diag_grid_metric` and apply role-specific tint:

| Material | Tint RGB | Role |
| --- | --- | --- |
| `blockout_neutral` | `[0.48, 0.55, 0.58]` | general unclassified blockout |
| `blockout_concrete` | `[0.48, 0.50, 0.53]` | concrete massing |
| `blockout_steel` | `[0.28, 0.38, 0.43]` | structural/industrial metal |
| `blockout_hazard` | `[0.82, 0.58, 0.13]` | hazard or attention area |
| `blockout_trim` | `[0.16, 0.19, 0.22]` | trim, frame, and dark separator |
| `blockout_exterior` | `[0.43, 0.40, 0.36]` | exterior massing |
| `blockout_accent_blue` | `[0.10, 0.52, 0.73]` | cool route/accent |
| `blockout_accent_orange` | `[0.92, 0.39, 0.10]` | warm route/accent |

The Material Browser currently creates icons from texture bytes without applying
material tint. These variants will look identical in its thumbnail grid until
that bug is fixed, while orthographic and 3D views will apply their tint.

### 5.3 Surface variants

The concrete library includes a darker tint variant. Masonry, rubber, and steel
each include a `[2,2]` repeat variant. These provide useful visual choices and
ensure multiple materials can share one texture dependency.

## 6. Standalone debug-shader suite

The debug shaders are compiler/runtime diagnostic assets. Current TileEditor
materials must continue to reference `tile_surface`; these recipes are not
exposed as TileEditor `.cymat` files yet.

| Shader | Diagnostic output |
| --- | --- |
| `unlit_base_color` | base texture and tint without Lambert lighting |
| `uv_visualization` | UV gradient, tile alternation, and integer boundaries |
| `uv_checker` | procedural checker independent of a texture resource |
| `normal_object` | object-space vertex normals mapped to RGB |
| `normal_world` | inverse-transpose transformed world normals mapped to RGB |
| `primitive_id` | stable pseudo-color derived from OpenGL `gl_PrimitiveID` |

All use desktop GLSL 410 core, the current position/normal/UV vertex locations,
and the existing 224-byte `Transforms` uniform block. An object/entity-ID view
still needs a versioned per-draw object identifier; primitive ID is the truthful
capability available now.

## 7. Deterministic generation

The procedural generator uses only the Python standard library. It writes RGBA8
PNG with a fixed chunk order, explicit sRGB rendering intent, PNG filter zero,
and zlib level nine. It does not call process-randomized `hash()` or use a random
seed. Its `--check` path regenerates each image in memory and performs a
byte-for-byte comparison with the checked-in file.

```sh
python3 tools/asset_content/generate_tileeditor_dev_textures.py --check
python3 tools/asset_content/generate_tileeditor_dev_recipes.py --check
```

The recipe generator owns the expected `.cytex`/`.cymat` text and response-file
ordering. Manual edits to generated recipes are reported as stale.

### 7.1 Development-lab map

`assets/maps/tile_editor_dev_lab.cymap` is a generated V3 integration scene. It
contains 24 separated 2×2 material plinths, so each selected texture appears on
both a floor and exposed walls. Floor levels cycle through −1, 0, and +1, while
wall heights cycle through one, two, and three levels. The scene also contains:

- sRGB/linear and mip/no-mip A/B pairs;
- identity, repeated, and mirrored UV materials;
- the nine surface-design materials;
- four semantic-label examples;
- a duplicate material path bound to slot 65535;
- an unbound slot 65534 cell showing the unknown-material fallback;
- north/east/south/west stairs with 16/2/32/8 treads respectively;
- four valid doors, one on each cardinal side; and
- one valid player spawn.

Regenerate or verify it with:

```sh
python3 tools/asset_content/generate_tileeditor_dev_lab.py
python3 tools/asset_content/generate_tileeditor_dev_lab.py --check
```

## 8. Authored-surface provenance

The four high-detail surfaces were created for this project with OpenAI image
generation in text-to-image mode, without a reference image. They contain no
downloaded game art, trademark, text, or logo. Each generated master was resized
to 512², stripped of metadata, mirrored horizontally into a row, and mirrored
vertically into a 1024² source. That processing makes opposite edges exactly
continuous under repeat sampling. The checked-in 1024² PNG is the authoritative
asset source.

Prompt specifications:

1. **Cast concrete** — “Create a seamless square game-development texture
   source for a professional 3D level editor: neutral industrial cast concrete,
   straight-on orthographic surface view, subtle aggregate, pores, mottling, and
   fine wear, physically plausible diffuse/albedo appearance. Tile cleanly on
   all edges. Flat neutral lighting; no directional shadow, baked ambient
   occlusion, perspective, objects, text, logo, or watermark.”
2. **Blue painted steel** — “Create a seamless square industrial painted-steel
   panel albedo for a professional 3D level editor: restrained blue-gray modular
   panels, seams, fasteners, scratches, and wear, straight-on and physically
   plausible. Tile cleanly on all edges. Flat diffuse lighting; no directional
   shadow, text, logo, or watermark.”
3. **Gray masonry** — “Create a seamless square game-development texture source
   for a professional 3D level editor: neutral gray modular masonry blocks,
   straight-on orthographic surface view, medium rectangular blocks in a clean
   staggered bond, restrained mortar joints, subtle chipped edges and natural
   stone variation, physically plausible diffuse/albedo appearance. Make it tile
   cleanly on all four edges. Keep lighting flat and neutral with no directional
   shadows, no ambient occlusion baked into corners, no perspective, no objects,
   no text, no logo, no watermark.”
4. **Rubber stud floor** — “Create a seamless square game-development texture
   source for a professional 3D level editor: dark charcoal industrial rubber
   floor tiles, straight-on orthographic surface view, fine raised circular stud
   pattern, subtle realistic wear and dust, understated seams, neutral physically
   plausible diffuse/albedo. Make it tile cleanly on all four edges. Keep
   lighting flat and even with no directional shadow, no baked ambient occlusion,
   no perspective, no objects, no text, no logo, no watermark.”

Because these are base-color development sources rather than measured PBR
captures, they should not be treated as physically calibrated production
materials. Normal, roughness, metalness, height, and acoustic/physical properties
must be authored under explicit future contracts.

## 9. Validation evidence

The complete response manifest was validated with the real resource compiler:

```text
Inputs       119 processed | 119 succeeded | 0 failed | 0 skipped
Diagnostics  0 warnings    | 0 errors
```

Two clean cooks produced 119 artifacts apiece. Their relative-path/SHA-256 maps
were byte-identical. Each cook wrote 41,727,983 bytes of cooked resources on the
validation host. This verifies source decoding, schema validation, shader
compilation/linking, texture import/mip generation, material dependency
validation, artifact publication, and repeat-cook determinism for this pack.

The pack benchmark can be repeated with:

```sh
python3 tools/asset_content/benchmark_tileeditor_dev_pack.py \
  --iterations 5 --warmup 1
```

A three-iteration development-build smoke run on the Apple M1 macOS authoring
host produced:

| Operation | Median | Range | Median throughput |
| --- | ---: | ---: | ---: |
| Validate 119 inputs | 1,381.209 ms | 1,365.837–1,390.161 ms | 86.16 inputs/s |
| Clean-cook 119 inputs | 1,552.655 ms | 1,522.623–1,750.509 ms | 76.64 inputs/s |

These numbers are a local smoke baseline, not a cross-machine performance
contract. Formal regression thresholds require a pinned release build, pinned
host image, quiet machine, more samples, and recorded CPU-frequency behavior.

Image-level verification also checks:

- every procedural output is byte-reproducible;
- every source is RGBA8 and opaque;
- dimensions match the catalog;
- the deliberate NPOT fixture remains 300×180; and
- all four mirrored high-detail surfaces have continuous opposite edges.

## 10. Known integration discrepancies exposed by the kit

The kit intentionally makes subtle paths disagree visibly when the
implementation differs:

1. The standalone runtime darkens textured walls by multiplying tint by `0.82`.
   The embedded 3D viewport uses the raw material tint. A wall can therefore
   appear darker in F6 than in the editor.
2. Material Browser icons wrap cooked base pixels directly. They do not apply
   material tint, UV scale, or linear-to-sRGB conversion. Tinted, repeated,
   mirrored, and linear variants can have misleading or identical icons.
3. Orthographic previews respect alpha, while both 3D paths force opaque output.
   Positive pack assets remain opaque until the contract is unified.
4. Orthographic records share image storage by material path. The 3D reload path
   decodes and uploads per bound slot, even when several slots reference the same
   material, and counts duplicates against its aggregate image budget.
5. The browser accepts readable cached output without a source-freshness check.
   Use **Rebuild Preview** after changing a source recipe or image.
6. Fixed 0–1 cube UVs stretch one repeat across each generated box. The grid and
   density assets expose this limitation; they do not imply a world-meter scale.

These are implementation work items, not reasons to weaken the diagnostics.

## 11. Relationship to Mason

The supplied Mason blueprint describes ten large editor/runtime modules. This
kit covers the source assets that are honest and useful before those modules
exist. Later assets should be added with their first real consumer:

| Mason area | Next asset/test corpus | Required engine contract first |
| --- | --- | --- |
| topology kernel | valid/invalid mesh fixtures, boundary/winding/non-manifold overlays | stable mesh handles, validation result schema, debug draw |
| mesh tools | golden extrude/inset/bevel/bridge/knife/snap/weld cases | transactional mesh-edit API and attribute propagation |
| CSG | disjoint, containment, coincident, coplanar, near-coplanar, sliver cases | classified fragments, robust stitch diagnostics, rollback |
| curves/terrain | spline profiles, sweep-frame fixtures, sculpt masks, finite height/blend data | curve/terrain formats and linear-data texture consumers |
| spatial/picking | face/edge/vertex IDs, BVH bounds, dense and tiny-object scenes | debug overlays and stable selection IDs |
| entities/triggers | entity/relay/target/I/O icons and trigger state visuals | reflected component/property schema and event connection graph |
| lighting | 18%-gray room, albedo ladder, lightmap density/overlap, probes | light entities, lightmap/probe formats, build artifacts |
| visibility/audio | sealed/leaking rooms, portals, occluders, impulse/sweep audio | typed portals/PVS and acoustic-surface/audio-zone contracts |
| selection/undo | gizmos, handles, candidate/rejected/snap overlays | depth-aware debug renderer and atomic transaction API |
| prefabs/build | nested/cyclic/missing prefab fixtures, collision decomposition, live-sync deltas | prefab identity, dependency graph, versioned live-sync protocol |

Visual material names must never become authoritative gameplay state. Physical,
collision, navigation, visibility, and acoustic behavior belongs in typed
resources or components, with schema validation and runtime consumers.

## 12. Next implementation gates

The highest-value follow-up work is:

1. fix browser thumbnail tint/color-space handling;
2. unify embedded/runtime wall tint;
3. add dependency freshness checks to the cooked material cache;
4. add world-space or box-projected UV density and face-specific surface roles;
5. make the TileEditor understand V2 materials and their dependency closure;
6. add multiple bindings, typed surface semantics, alpha state, normal/ORM maps,
   lightmaps, decals, and arbitrary debug shaders only when the runtime path can
   consume them end to end; and
7. benchmark mip generation, material-set reload, thumbnail
   memory, and draw/material switching instead of relying only on cooked-format
   microbenchmarks.

The current library is intentionally substantial but bounded. It gives the
TileEditor professional development content and a repeatable test surface while
keeping future Mason asset families tied to implemented engine behavior.
