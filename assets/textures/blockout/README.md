# Cypher blockout material starter pack

This directory contains twelve original, deterministic 128 x 128 RGBA textures
for graybox maps and renderer tests. They were authored specifically for
Cypher Engine with `generate_blockout_textures.py`; they are not copied,
traced, sampled, or derived from Valve, id Software, or another game/tool.
The textures, recipes, and generator are Copyright (c) 2026 Karlo Siric and are
covered by the repository's proprietary `LICENSE`.

The pack deliberately favors clear shapes, restrained colors, obvious texture
orientation, and visible repetition over finished environment art:

| Texture | Intended blockout use |
| --- | --- |
| `ceiling_panel` | Bright modular ceilings and light wells |
| `concrete_light` | Neutral rooms, floors, and exterior slabs |
| `concrete_worn` | Dark service spaces and damaged routes |
| `floor_checker` | Readable room-scale floor zoning |
| `floor_tile` | Cool industrial floors and corridors |
| `metal_grate` | Catwalks, drains, and ventilation surfaces |
| `metal_panel` | Neutral machinery and wall panels |
| `service_panel` | Interactive-looking technical walls |
| `trim_blue` | Navigation, team, and route accents |
| `trim_orange` | Construction, objective, and focus accents |
| `wall_ribbed` | Large industrial walls and supports |
| `warning_red` | Restricted paths, damage, and danger zones |

Each PNG has a matching `.cytex` recipe here and a `.cymat` recipe under
`assets/materials/blockout/`. All materials use `tile_surface`, sRGB color,
repeating UVs, and generated mipmaps. CMake cooks and stages the complete pack
with the editor, while the Project Materials browser discovers the recipes by
recursively scanning the asset root.

Regenerate or verify the committed images with:

```sh
python3 assets/textures/blockout/generate_blockout_textures.py
python3 assets/textures/blockout/generate_blockout_textures.py --check
```

The generator uses only the Python standard library. Keep its `GENERATORS`
table, the recipe pairs, and `CYPHER_TILE_MATERIAL_IDS` in the root
`CMakeLists.txt` synchronized when extending the starter pack.
