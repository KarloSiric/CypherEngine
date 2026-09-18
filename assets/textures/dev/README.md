# TileEditor development textures

This directory contains the source images and `.cytex` recipes for the
TileEditor Development Kit. The kit is original Cypher project content. It
contains no copied game textures.

The library is split into three groups:

- `diagnostics/` isolates UV orientation, repetition, mirroring, texel density,
  seams, mip behavior, color space, neutral values, world axes, and missing
  resources.
- `semantics/` provides a consistent visual language for collision, clipping,
  triggers, navigation, visibility, audio, lighting, spawns, ladders, sky, and
  nodraw authoring. These images are **visual labels only**; painting one does
  not add gameplay behavior.
- `surfaces/` provides readable graybox and material-composition surfaces for
  concrete, masonry, steel, rubber, wood, ceramic, plaster, asphalt, and hazard
  areas.

Most diagnostic sources are deterministic 256×256 RGBA8 PNGs. The intentional
NPOT fixture is 300×180. Procedural design surfaces are 512×512. The four
authored high-detail surfaces are 1024×1024. All positive TileEditor assets are
opaque because the current 2D and 3D preview paths do not share one alpha
contract.

Regenerate and verify the procedural images with:

```sh
python3 tools/asset_content/generate_tileeditor_dev_textures.py
python3 tools/asset_content/generate_tileeditor_dev_textures.py --check
```

Regenerate and verify all V1 `.cytex`, V1 `.cymat`, and response-manifest files
with:

```sh
python3 tools/asset_content/generate_tileeditor_dev_recipes.py
python3 tools/asset_content/generate_tileeditor_dev_recipes.py --check
```

The four authored high-detail surfaces are checked-in source assets. Their
prompts, processing method, intended use, and provenance are recorded in
[`docs/TILEEDITOR_DEVELOPMENT_KIT.md`](../../../docs/TILEEDITOR_DEVELOPMENT_KIT.md).

The authoritative batch input list is
[`assets/tileeditor_dev_pack.rsp`](../../tileeditor_dev_pack.rsp). Cook it with:

```sh
CypherResourceCompiler compile \
  -s assets -o out/dev-resources \
  --target host --profile development \
  @assets/tileeditor_dev_pack.rsp
```

Cooked `*_c` files belong in build/cache output and must not be committed.

The original, smaller procedural starter pack remains in `textures/blockout/`,
with matching recipes in `materials/blockout/`. It is retained for existing
maps and focused tests while the Development Kit becomes the primary library.
