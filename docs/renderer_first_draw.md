# First drawing milestone: indexed rotating cube

This milestone connects cooked shader loading, runtime program creation,
immutable graphics pipelines, vertex input, uniform updates, and indexed drawing.
The example is a small host for the renderer, with explicit startup and cleanup.
It is not a new gameplay framework or a replacement for the engine Host.

## Build and run

The shader-tools preset supplies the existing offline compiler. Building the
example with that preset cooks its authored shader automatically:

```sh
cmake --preset shader-tools-debug
cmake --build --preset shader-tools-debug --target cypher_render_cube --parallel 4
./out/build/shader-tools-debug/bin/cypher_render_cube
```

The window displays a lit cube with a UV grid. The cube rotates, depth testing
hides rear surfaces, and back-face culling removes outward triangles facing away
from the camera. Resize the window to check the viewport and projection. Press
Escape or close the window to exit.

Run a finite smoke check through the same public API:

```sh
./out/build/shader-tools-debug/bin/cypher_render_cube --hidden --frames 120
```

`--frames N` exits successfully only after presenting exactly `N` frames and
completing cleanup. A timeout also prevents a minimized finite run from waiting
forever. Finite runs advance animation at a fixed 1/60 second per rendered frame;
interactive runs use elapsed time. `--hidden` defaults to 120 frames if no frame
limit is supplied. A graphics context is still required for hidden runs.

`--shader PATH` selects another **already cooked** `.cyshader_c` file. The runtime
does not invoke a compiler or open raw GLSL source. This override also lets a
renderer-only debug build use a shader produced by a separate tools build:

```sh
cmake --build --preset debug --target cypher_render_cube --parallel 4
./out/build/debug/bin/cypher_render_cube \
  --shader out/build/shader-tools-debug/cooked/render_smoke/cube.cyshader_c
```

To run the offline step explicitly:

```sh
./out/build/shader-tools-debug/bin/CypherResourceCompiler compile \
  --source-root assets \
  --output-root out/build/shader-tools-debug/cooked \
  --color never --progress none render_smoke/cube.cyshader
```

The example's default cooked path is supplied by CMake. Cooked output lives in
the build directory; source control owns the authored recipe and stage files.

## Follow the data

```text
assets/render_smoke/cube.cyshader
    + cube.vert + cube.frag
        |
        | CypherResourceCompiler, offline
        v
cooked/render_smoke/cube.cyshader_c
        |
        | VfsDirectory -> Vfs_ReadAll -> owned blob
        v
CookedShader_Read -> borrowed cooked_shader_view_t
        |
        | R_CreateShader, synchronous
        v
public shader handle -> frontend record -> linked native program
        |
        | R_CreateGraphicsPipeline
        v
pipeline: program + vertex layout + depth/cull/blend policy
        |
        | R_BeginFrame -> R_DrawIndexed -> R_EndFrame
        v
36 indices -> 12 triangles -> visible cube
```

The VFS directory provider is initialized with the cooked file's parent directory.
The filename is a canonical virtual path within that provider. The file is read
through `Vfs_ReadAll`, and `CookedShader_Read` validates the container before the
renderer sees its view. The owning blob survives through `R_CreateShader` and is
then released. The renderer retains no borrowed source pointers.

## Geometry and transforms

The cube has 24 vertices rather than eight because a vertex contains a complete
attribute tuple. Each face needs its own normal and UV square, even when the
position is shared with neighboring faces. Each vertex has this 32-byte layout:

| Location | Attribute | Format | Byte offset |
| --- | --- | --- | --- |
| 0 | Position | Three 32-bit floats | 0 |
| 1 | Normal | Three 32-bit floats | 12 |
| 2 | UV | Two 32-bit floats | 24 |

Six faces each use the index pattern `0, 1, 2, 0, 2, 3`, shifted to that face's
four vertices. All 36 indices are unsigned 16-bit values, and every triangle is
counterclockwise from outside the cube. The vertex and index buffers are
immutable; only the transform buffer changes each frame.

The CPU structure and the GLSL `Transforms` block contain three column-major
matrices and a tint color, with std140-compatible offsets:

| Field | Byte offset | Purpose |
| --- | --- | --- |
| `model` | 0 | Rotate the cube from local space into world space. |
| `view` | 64 | Transform world positions into camera space. |
| `projection` | 128 | Project camera-space positions into clip space. |
| `tint` | 192 | Multiply the cube's face colors. |

The complete block is 208 bytes. Compile-time checks protect the CPU offsets and
size. Pipeline creation verifies the native uniform block's reflected size, and
the renderer assigns that block to uniform binding zero. GLSL 4.10 does not use
a `binding = 0` qualifier in the shader source.

Common Math uses column vectors, so the vertex shader applies
`projection * view * model * position`. World space follows the engine's Z-up
convention. The right-handed view looks along negative camera Z, and the
projection explicitly selects normalized depth from -1 to +1 for OpenGL.

The shader's normal transform uses `mat3(model)` because this example applies
rotation only. A future mesh instance with nonuniform scale needs the
inverse-transpose normal matrix. That is a separate transform requirement;
silently extending this example's expression to arbitrary scale would be wrong.

## Depth comparison policy

Graphics pipelines default to `render_depth_compare_t::LESS` with depth testing
and writing enabled. A pipeline may instead select any of the eight standard
comparisons, such as `LESS_EQUAL` for geometry intentionally placed on the far
plane or `ALWAYS` for a deliberate overlay. The compare operation is immutable
pipeline state and is applied before each draw, so pipeline order does not
inherit an accidental depth function from an earlier pass or editor host.

`render_frame_info_t::clearDepth` and target-pass clear depth remain separate
values, both defaulting to 1.0. A pass using `GREATER` needs a compatible depth
clear and projection convention; changing the comparison alone does not create
a reversed-Z camera. Disabling depth testing also disables the OpenGL depth
test stage's writes, even if the pipeline's `depthWrite` flag is true.

Pipelines can also disable color writes while leaving depth testing and depth
writes active. For a depth prepass, draw opaque geometry with `colorWrite =
false`, `depthWrite = true`, and `LESS`; then shade that geometry with color
writes enabled, `depthWrite = false`, and `EQUAL`. The color mask is set by each
draw, so a later ordinary pipeline does not inherit the prepass mask. This
controls draw output only; no scene queue schedules these passes yet.

## Sampleable target depth

An offscreen target accepts a linear RGBA8 color texture, a GPU-only D24 depth
texture, or both. A color-only target keeps the existing private D24
renderbuffer. When both textures are supplied, their extents must match. D24
textures have no initial pixel data, generated mip chain, sRGB conversion, or
repeat addressing. The target retains each supplied caller-owned texture until
it is destroyed.

For a depth-only target, leave `color` invalid and supply the D24 `depth`
handle. Set the pass's `clearFlags` to `R_CLEAR_DEPTH`: the default includes
`R_CLEAR_COLOR`, which is rejected when no color attachment exists. The OpenGL
framebuffer routes both draw and read color buffers to `GL_NONE`; a pipeline
with `colorWrite = false` makes the intent explicit. The target extent comes
from the D24 texture.

Clear or draw depth in a target pass, then end that pass before binding the
texture in a later draw. Binding either active attachment for sampling is
rejected to prevent read/write feedback. The current shader interface accepts
ordinary `sampler2D`; read `texture(depth_image, uv).r` and perform any depth
comparison explicitly in the shader. This supports depth diagnostics and
manual shadow experiments. For an OpenGL 4.1 shadow comparison, declare the
pipeline slot as `DEPTH_COMPARE_2D` for an active `sampler2DShadow` uniform,
then pair the D24 texture with an explicit sampler whose `compare` is not
`NONE`. A scalar slot rejects a comparison sampler; a shadow slot rejects a
color texture or missing comparison sampler. Automatic shadow-pass scheduling
and light-space view setup remain separate work.

OpenGL 4.1 treats depth textures as red textures for ordinary sampling; see
sections 3.8.9 and 3.8.16 of the [OpenGL 4.1 Core Specification](https://registry.khronos.org/OpenGL/specs/gl/glspec41.core.pdf).

## Per-draw viewport and clipping

An indexed draw can enable a viewport rectangle to map clip-space coordinates
into one region, and a scissor rectangle to prevent pixels outside a region.
These controls serve different purposes: a viewport changes where geometry
lands; a scissor clips fragments after that mapping. For an editor panel, set
both to the panel's bounds. Coordinates are physical pixels from the
bottom-left corner; when drawing into an offscreen target, they are relative to
that target rather than the presentation surface. For example:

```cpp
draw.viewport.enabled = true;
draw.viewport.x = panelLeft;
draw.viewport.y = panelBottom;
draw.viewport.width = panelWidth;
draw.viewport.height = panelHeight;
draw.scissor.enabled = true;
draw.scissor.x = panelLeft;
draw.scissor.y = panelBottom;
draw.scissor.width = panelWidth;
draw.scissor.height = panelHeight;
```

Each enabled rectangle must have a positive size and fit entirely inside the
active extent. The renderer sets viewport and scissor state on every draw. A
later draw with both rectangles disabled again maps to, and can cover, the full
framebuffer. Frame and target-pass clears still cover the whole attachment. A
host supplying its own OpenGL context gets its viewport, scissor box, and
scissor enable state restored at frame end. This supplies draw controls for
panels and overlays; the renderer does not yet schedule viewport draws or own
editor panel layout.

## Lifecycle and cleanup

The host creates the System window after renderer configuration has selected its
graphics attributes. `R_Init` borrows the window and owns the graphics context.
The example uses only public System, Common, and renderer functions: no native
OpenGL or SDL calls appear in the host.

Each frame pumps System events, handles Escape and window closure, and reads the
physical drawable dimensions. Minimized or zero-size drawables suspend drawing.
Dimension changes call `R_Resize` outside a frame and rebuild the projection with
the new aspect ratio. Uniform data is uploaded before beginning the next frame.

Shutdown waits for outstanding graphics work, then releases objects in dependency
order: pipeline, vertex input, buffers, shader, renderer, window, System. The
same cleanup runs after partial initialization and failed drawing. Destroying the
pipeline releases its shader reference; destroying vertex input releases its
buffer references. The context remains alive until native objects are gone.

## Validation

The renderer test targets cover public contracts, lifecycle, shader ownership,
native compile/link rollback, pipeline dependencies, and draw validation. The
live OpenGL smoke test reads framebuffer pixels: the triangle must first be red
and then green after a uniform update. It also checks that disabling depth writes
in one frame does not suppress the next frame's depth clear. The live check skips
on machines without a usable OpenGL 4.1 desktop session; hidden windows still
require graphics support.

```sh
cmake --build --preset shader-tools-debug --parallel 4 --target \
  cypher_render_contract_tests cypher_render_runtime_tests \
  cypher_render_shader_tests cypher_render_draw_tests \
  cypher_render_opengl_shader_tests cypher_render_opengl_smoke_tests
ctest --preset shader-tools-debug --output-on-failure \
  -R '^cypher_render_(contract|runtime|shader|draw|opengl_shader|opengl_smoke)_tests$'
```

Draw validation checks the selected index byte range and vertex-buffer capacity.
It does not download GPU indices to inspect each value: the caller must ensure
that every selected index is less than `vertexCount`. Validate imported index
values during mesh loading. This example's fixed cube indices meet that contract.

## Where to read next

- `examples/CypherRender/CypherRender_Cube.cpp`: host ownership, geometry, file
  loading, per-frame transforms, events, and ordered cleanup.
- `assets/render_smoke/cube.cyshader`, `cube.vert`, `cube.frag`: authoring input,
  shader interface, lighting, and the UV grid.
- `src/CypherRender/CypherRender_Shader.cpp`: public generational shader handles.
- `src/CypherRender/OpenGL/CypherRender_OpenGL_Shader.cpp`: stage compilation,
  link diagnostics, and native program ownership.
- `src/CypherRender/CypherRender_Pipeline.cpp`: immutable drawing state and shader
  dependencies.
- `src/CypherRender/CypherRender_Draw.cpp`: frame, layout, buffer, and range
  validation before native draw submission.

The cube itself uses a triangle list, one uniform block, and direct window
submission. The renderer has since gained textures, samplers, two binding slots,
offscreen color targets, and additional draw state; those contracts are described
in the resource-binding notes. Mesh asset loading, cooked material binding, a
world draw queue, sampleable depth, command recording, and shader hot reload
remain separate work. The visible cube continues to exercise the first complete
draw path.
