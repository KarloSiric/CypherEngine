# Cypher development shader suite

This directory contains small, deterministic shaders for inspecting mesh and
material data while developing Cypher Engine. They target desktop GLSL 4.10 core
and use `cypher.shader` schema V1 so the current resource compiler and OpenGL 4.1
runtime can consume them without the unfinished general material-binding path.

These are diagnostic views rather than shipping surface shaders. Their output is
deliberately direct and stable: a rendered color should identify one property of
the submitted mesh, transform, or texture.

## Shared vertex contract

Every recipe uses `debug_mesh.vert`. It accepts the current mesh layout and
transform block:

| Location or binding | GLSL declaration | Meaning |
| --- | --- | --- |
| Vertex location 0 | `vec3 position` | Object-space position |
| Vertex location 1 | `vec3 normal` | Object-space vertex normal |
| Vertex location 2 | `vec2 texcoord` | Primary texture coordinate |
| Uniform block | `Transforms` | `model`, `view`, `projection`, `tint`, and `uvScale` |

The normal matrix uses the inverse transpose of `model`, so the world-normal view
remains correct under non-uniform scaling. `uvScale.xy` follows the existing tile
surface contract; `uvScale.zw` remains reserved.

## Recipes

| Recipe | Texture binding | Purpose |
| --- | --- | --- |
| `unlit_base_color.cyshader` | `sampler2D base_color` | Shows the base-color texture and tint without lighting. It performs the same explicit linear-to-sRGB output conversion as the current editor surface shader because framebuffer sRGB is presently disabled. |
| `uv_visualization.cyshader` | None | Encodes local U in red and V in green, alternates the blue component per UV tile, and marks integer tile boundaries in white. Use it to find flips, seams, wrapping, and scale errors. |
| `uv_checker.cyshader` | None | Draws an eight-cell procedural checker per UV tile, minor cell edges, and cyan integer-tile boundaries. Use it to inspect UV distortion and relative texel density without a texture resource. |
| `normal_object.cyshader` | None | Encodes the normalized object-space vertex normal from `[-1, 1]` to RGB `[0, 1]`. Use it to inspect imported vertex normals independently of transforms. |
| `normal_world.cyshader` | None | Encodes the inverse-transpose-corrected world-space normal. Compare it with the object-normal view to diagnose transform and normal-matrix errors. |
| `primitive_id.cyshader` | None | Hashes fragment-stage `gl_PrimitiveID` into a stable, high-contrast color. This is available in OpenGL 4.1 and helps reveal triangle boundaries, duplicated faces, and unexpected topology. |

`primitive_id` identifies the rasterized primitive within a draw. A true object or
entity ID view still requires the renderer to upload a per-draw ID; the current
V1 shader recipe has no typed interface for that value.

## Compile and validate

From the repository root, validate every recipe with:

```sh
CypherResourceCompiler validate -s assets 'shaders/dev/*.cyshader'
```

Cook them into a scratch output tree with:

```sh
CypherResourceCompiler compile -s assets -o out/cooked-dev-shaders \
  'shaders/dev/*.cyshader'
```

The quoted wildcard is expanded by the resource compiler against the source VFS,
which keeps the invocation platform-independent.

## TileEditor status

The current TileEditor material preview accepts only
`shaders/tile_surface.cyshader`, one `base_color` texture, `tint`, and `uv_scale`.
These standalone recipes therefore do not have `.cymat` files and do not appear
as selectable TileEditor materials yet. They are ready for resource-compiler and
renderer diagnostics, and can be wired into editor view modes when the preview
pipeline supports arbitrary shaders.
