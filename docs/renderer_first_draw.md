# First drawing milestone: a graybox scene from a visible World view

This milestone connects cooked shader loading, runtime program creation,
immutable graphics pipelines, vertex input, an in-memory World view submission,
aligned uniform records, and indexed drawing.
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

The window displays three differently tinted, rotating cubes over a graybox
floor with a UV grid. A fourth cube sits behind the camera and is rejected by
the World view query. The cubes share one mesh; the floor is a second mesh.
Both use one pipeline and a distinct transform/tint record for each visible
object. Depth testing hides rear surfaces, and back-face culling removes
triangles facing away from the camera. Resize the window to check the
projection. Press Escape or close the window to exit.

In the interactive run, hold the right mouse button to capture the pointer and
look around. WASD moves along the view, Space/C moves up/down in the Z-up
world, and Shift increases movement speed. Release the right button to free
the pointer. Focus loss, minimization, and event-queue overflow also release
capture and clear held movement so a lost key-up cannot leave the camera moving.
The sample feeds the same camera pose to its view matrix, view frustum, and
World visibility query. This is sample-host navigation, not a renderer-owned
input system or a gameplay camera.

Run a finite smoke check through the same public API:

```sh
./out/build/shader-tools-debug/bin/cypher_render_cube --hidden --frames 120
```

`--frames N` exits successfully only after presenting exactly `N` frames and
completing cleanup. A timeout also prevents a minimized finite run from waiting
forever. Finite runs advance animation at a fixed 1/60 second per rendered frame;
interactive runs use elapsed time. `--hidden` defaults to 120 frames if no frame
limit is supplied. The current five-object scene produces four visible draws
per frame, or 480 draws in the default hidden run. A graphics context is still
required for hidden runs. Finite runs keep the original fixed camera and do not
consume fly controls, so the smoke check remains deterministic.

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
        | World_QueryView -> copied, frame-scoped World submission
        | host resolves logical mesh/material slots and validates live bundles
        | one view upload + one aligned object-record batch for accepted objects
        | ordered draw-list record -> R_BeginFrame -> flush -> R_DrawIndexed -> R_EndFrame
        | WorldSubmission_End invalidates the view token
        v
36 cube indices or 6 floor indices -> distinct indexed draws
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
counterclockwise from outside the cube. The floor uses four vertices and six
unsigned 16-bit indices with a +Z normal and counterclockwise winding from
above. Its zero-thickness local bounds match the XY quad, and its translated
world bounds are submitted to the World visibility query. Both meshes use the
same vertex layout and immutable GPU buffers. The host uploads one view record
and batches the visible objects' records each frame. The CPU structures match
two GLSL std140 blocks:

| Block and slot | Field | Byte offset | Purpose |
| --- | --- | --- | --- |
| `ViewConstants`, 0 | `view` | 0 | Transform world positions into camera space. |
| `ViewConstants`, 0 | `projection` | 64 | Project camera-space positions into clip space. |
| `ObjectConstants`, 1 | `model` | 0 | Transform one cube or the floor from local to world space. |
| `ObjectConstants`, 1 | `tint` | 64 | Multiply that object's face colors. |

The view block is 128 bytes and each object block is 80 bytes. Compile-time
checks protect the CPU offsets and sizes; pipeline creation checks the shader's
reflected block sizes. OpenGL 4.1 assigns binding slots through the renderer
pipeline, so GLSL 4.10 does not use a `binding` qualifier in source. The
current two-slot limit is enough for this demonstration, not a final material
binding convention.

Common Math uses column vectors, so the vertex shader applies
`projection * view * model * position`. World space follows the engine's Z-up
convention. The right-handed view looks along negative camera Z, and the
projection explicitly selects normalized depth from -1 to +1 for OpenGL.

The shader's normal transform uses `mat3(model)` because this example applies
rotation only. A future mesh instance with nonuniform scale needs the
inverse-transpose normal matrix. That is a separate transform requirement;
silently extending this example's expression to arbitrary scale would be wrong.

## Wireframe inspection

For a triangle-list mesh, an immutable graphics pipeline can select
`render_polygon_mode_t::LINE` to rasterize triangle edges instead of filled
triangles. `FILL` remains the default. The mode belongs to the pipeline rather
than a global OpenGL setting, so a later filled draw restores filled
rasterization without depending on what a previous draw did. Line-list
pipelines already draw lines and reject the polygon `LINE` mode.

This is a topology inspection view, not a second mesh format or an automatic
outline overlay. Shared edges can be drawn twice, line width remains the
driver's one-pixel behavior, and a wire overlay on filled geometry still needs
an explicit depth policy to avoid z-fighting. The host's prior polygon state
is restored when a hosted frame ends.

## Many object transform records

`CypherRenderUniformRuntime` stages a bounded set of per-object uniform records
on the CPU and uploads the populated span to one mutable GPU uniform buffer.
`R_AppendUniformRecord` copies each borrowed record immediately and places it
at the device's required uniform-buffer offset alignment. After one successful
`R_CommitUniformBatch`, `R_ApplyUniformRecordToDraw` selects a committed record
for a chosen uniform slot without changing the draw's geometry or material.
The OpenGL 4.1 draw then uses the existing `glBindBufferRange` path. This
replaces one upload per object with one upload for the object batch, while keeping
each object's model or tint distinct. The shared camera matrices use a separate
128-byte mutable buffer and one update per view, rather than being copied into
each object record. This increases the number of upload calls from one to two
in the small sample, while establishing distinct per-view and per-object
lifetimes; it is not a measured speedup.

Capacity is fixed at creation; an append that does not fit leaves the previous
records intact. Failed GPU upload leaves the batch staged for a retry, and
`R_ResetUniformBatch` invalidates old record tokens before reuse. The caller
destroys the batch and owns the surrounding frame and draw order. Reusing the
same GPU buffer in a later frame is correct but can stall while the GPU reads
older draws; a measured multi-buffer strategy can be added when needed. This
graybox example updates four cube transforms and conservative world-space bounds,
while the floor stays static. It holds five World objects and builds one
frustum/layer/distance view submission, then stages only its copied
objects' model matrices and tints. The submission storage is allocated once;
each build uses the linear World query without allocating. A checked token
permits copied record reads until the host ends the submission after the
renderer frame. It uploads those object records once and selects one per
indexed draw, alongside the shared view buffer. The host resolves the objects'
logical mesh and material slots through bounded tables containing two static
mesh bundles and five sample tints. It checks every visible slot and live mesh
bundle before appending any draw. The final pipeline, uniform, and draw-range
validation still happens inside `R_DrawIndexed` during flush. World never owns
GPU handles.
The batch resets after the preceding frame ends. This exercises a bounded
linear visibility query through the public OpenGL 4.1 path. The camera and
objects remain hardcoded; there is no scene asset loading, spatial
acceleration, material sorting, or pass scheduling.

## Ordered draw list

The sample now copies each completed `render_draw_indexed_desc_t` into a
fixed-capacity renderer draw list before beginning the frame. A flush in the
active frame calls the existing validated `R_DrawIndexed` path in append order.
The list holds renderer handles by value but does not retain the underlying GPU
resources, so the host keeps both meshes, pipeline, view buffer, and uniform batch alive
through flush. It allocates at initialization, then resets between frames
without another allocation. This is the first CPU command-storage boundary,
not pass classification, sorting, or asynchronous GPU submission.

If a draw fails, the list reports how many earlier draws returned success.
The failing draw may already have reached the GPU before its error was
reported, so the list never retries or replays it. Every flush attempt seals
the list, even an empty or failed one. Reset outside the frame discards the
records and permits reuse. This explicit partial-progress contract matters
before adding larger scene queues.

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
values during mesh loading. This example's fixed cube and floor indices meet
that contract.
For one interleaved static stream, `R_CreateStaticMeshBundle` now performs the
CPU index scan before creating its vertex buffer, index buffer, and vertex input.
It accepts native-endian U16 or U32 indices and exact byte extents, then copies
the source bytes synchronously. The caller can release its CPU arrays after
creation. `R_ApplyStaticMeshBundleToDraw` fills the input and counts while
preserving the draw's pipeline, material textures, uniforms, and frame state.
`R_DestroyStaticMeshBundle` releases the input before its buffers; a failed
destroy leaves still-live handles in the bundle for retry. Direct `R_DrawIndexed`
callers remain responsible for validating their own indices. This helper does
not define a `.cymesh` format, importer, or scene draw queue.

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

The graybox sample uses two triangle-list meshes, two uniform blocks, a copied
World submission, and an ordered draw list for direct window drawing. The
renderer also has textures, samplers, two binding slots,
offscreen color and sampleable depth targets, and a limited cooked-material
bridge. Authored scene and mesh loading, broader material support, pass
classification and sorting, full command scheduling, and shader hot reload
remain separate work.
