# First drawing milestone: a graybox scene from a visible World view

This milestone connects cooked shader and texture loading, runtime program creation,
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
floor with a cooked checker texture. A fourth cube sits behind the camera and
is rejected by the World camera-view query, but still casts into a fixed
directional shadow map. Two overlapping translucent panels sit above the
floor. The cubes share one mesh; the floor and panels share a second mesh.
The fixed shadow pass draws opaque casters, then the HDR camera pass draws
opaque surfaces before depth-tested translucent panels sorted back-to-front.
A fullscreen-triangle pass tone-maps the linear RGBA16F scene and encodes it
for the presentation window. Depth testing hides rear surfaces, and back-face
culling removes triangles facing away from the camera. Resize
the window to check the projection and HDR target recreation. Press Escape or
close the window to exit.

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
limit is supplied. The current seven-object scene produces five opaque shadow
draws, four camera-visible opaque draws, two translucent draws, and one
presentation draw per frame: 600 shadow, 720 scene (240 translucent), and 120
tone-map draws in the default hidden run. A graphics
context is still required for hidden runs. Finite runs keep the original fixed
camera and do not consume fly controls, so the smoke check remains
deterministic.

`--shader PATH`, `--shadow-shader PATH`, `--tonemap-shader PATH`, and
`--texture PATH` select **already cooked** `.cyshader_c` and `.cytex_c` files.
A replacement camera shader must declare the sample's reflected uniform blocks,
`albedoTexture`, and the `shadowMap` comparison sampler. A replacement depth
shader must declare the light and object blocks plus `albedoTexture` for the
matching cutout; a replacement presentation
shader must declare `sceneColor` as a scalar `sampler2D`. The runtime does not
invoke a compiler or open raw source assets. These overrides also let a
renderer-only debug build use assets produced by a separate tools build:

```sh
cmake --build --preset debug --target cypher_render_cube --parallel 4
./out/build/debug/bin/cypher_render_cube \
  --shader out/build/shader-tools-debug/cooked/render_smoke/cube.cyshader_c \
  --shadow-shader out/build/shader-tools-debug/cooked/render_smoke/shadow.cyshader_c \
  --tonemap-shader out/build/shader-tools-debug/cooked/render_smoke/tonemap.cyshader_c \
  --texture out/build/shader-tools-debug/cooked/textures/blockout/floor_checker.cytex_c
```

To run the offline step explicitly:

```sh
./out/build/shader-tools-debug/bin/CypherResourceCompiler compile \
  --source-root assets \
  --output-root out/build/shader-tools-debug/cooked \
  --color never --progress none render_smoke/cube.cyshader
./out/build/shader-tools-debug/bin/CypherResourceCompiler compile \
  --source-root assets \
  --output-root out/build/shader-tools-debug/cooked \
  --color never --progress none render_smoke/shadow.cyshader
./out/build/shader-tools-debug/bin/CypherResourceCompiler compile \
  --source-root assets \
  --output-root out/build/shader-tools-debug/cooked \
  --color never --progress none render_smoke/tonemap.cyshader
./out/build/shader-tools-debug/bin/CypherResourceCompiler compile \
  --source-root assets \
  --output-root out/build/shader-tools-debug/cooked \
  --color never --progress none textures/blockout/floor_checker.cytex
```

The example's default cooked path is supplied by CMake. Cooked output lives in
the build directory; source control owns the authored recipe and stage files.
The separate tile-map preview uses `render_smoke/tile_blockout.cyshader` for its
untextured fallback. That shader preserves the preview's single 208-byte
`Transforms` block. It cannot use this cube shader, which declares separate
view/object blocks and a texture sampler. Textured tile materials continue to
use their own `tile_surface` shader.

## Follow the data

```text
assets/render_smoke/cube.cyshader + cube.vert + cube.frag
assets/render_smoke/shadow.cyshader + shadow.vert + shadow.frag
assets/render_smoke/tonemap.cyshader + tonemap.vert + tonemap.frag
assets/textures/blockout/floor_checker.cytex + floor_checker.png
        |
        | CypherResourceCompiler, offline
        v
cooked shader and texture files
        |
        | VfsDirectory -> Vfs_ReadAll -> owned blob
        v
CookedShader_Read / CookedTexture_Read -> borrowed validated views
        |
        | R_CreateShader / R_CreateTexture2DFromCooked, synchronous
        v
public shader and texture handles -> native programs and images
        |
        | R_CreateGraphicsPipeline
        v
opaque, translucent, depth, and presentation pipelines: program + layout + state
        |
        | World objects -> five opaque shadow casters
        | World_QueryView -> copied camera-visible World submission
        | host resolves logical mesh/material/texture slots and validates live handles
        | camera and light uploads + one aligned object-record batch
        | WorldSubmission_End invalidates the view token after visibility marking
        | CPU scene classifier selects shadow/opaque/blend indices and sorts BLEND
        | opaque, translucent, and shadow draw queues -> R_BeginFrame
        | D24 target pass -> five depth draws -> end target pass
        | RGBA16F target pass -> four opaque camera draws -> two sorted translucent draws -> end pass
        | indexed fullscreen triangle samples HDR, tone-maps and encodes
        | R_EndFrame presents the window
        v
36 cube indices, 6 floor indices, or 3 fullscreen indices -> indexed draws
```

For each cooked file, the VFS directory provider is initialized with its parent
directory. The filename is a canonical virtual path within that provider.
`Vfs_ReadAll` owns the bytes while the cooked shader or texture view is validated
and adopted synchronously. The blob is then released; the renderer retains no
borrowed source pointers. Most cube material slots bind a 1×1 white image, one
cube binds a small CPU-authored cutout image, and the floor binds the cooked
checker. Camera draws bind albedo in slot 0 and the comparison-sampled D24
shadow map in slot 1; depth draws bind the same albedo in slot 0 to discard
masked texels consistently.
The renderer validates live handles again when the queued draw is flushed.

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
same vertex layout and immutable GPU buffers. The host uploads camera and light
view records and batches the objects' records before both passes. The camera
pipeline's CPU structures match two GLSL std140 blocks:

| Block and slot | Field | Byte offset | Purpose |
| --- | --- | --- | --- |
| `ViewConstants`, 0 | `view` | 0 | Transform world positions into camera space. |
| `ViewConstants`, 0 | `projection` | 64 | Project camera-space positions into clip space. |
| `ViewConstants`, 0 | `ambientLight` | 128 | Add per-view ambient RGB illumination. |
| `ViewConstants`, 0 | `lightDirection` | 144 | Point from a surface toward one directional light. |
| `ViewConstants`, 0 | `lightColor` | 160 | Scale the light's diffuse RGB contribution. |
| `ViewConstants`, 0 | `lightViewProjection` | 176 | Project world positions into the shadow map. |
| `ObjectConstants`, 1 | `model` | 0 | Transform one cube or the floor from local to world space. |
| `ObjectConstants`, 1 | `tint` | 64 | Multiply that object's face colors. |
| `ObjectConstants`, 1 | `normalMatrix` | 80 | Three padded `vec4` columns holding the inverse-transpose of the model's linear part. |
| `ObjectConstants`, 1 | `cutout` | 128 | Alpha cutoff in X; negative disables discard for the sample's other surfaces. |

The camera view block is 240 bytes and the shared camera/depth object block is
144 bytes. The depth pipeline reads a 64-byte `LightConstants` matrix at slot 0
and the same 144-byte object record at slot 1. Compile-time checks protect the CPU
offsets and sizes; pipeline creation checks the shader's
reflected block sizes. OpenGL 4.1 assigns binding slots through the renderer
pipeline, so GLSL 4.10 does not use a `binding` qualifier in source. The
current two-slot limit is enough for this demonstration, not a final material
binding convention.

The sample host writes ambient color, one directional light, and a matching
fixed light view/projection into the camera record. The fragment shader
combines albedo, normal, diffuse light, and one depth comparison from the
shadow map. The directional RGB is 1.75, so lit fragments can exceed 1.0
before tone mapping. This is sample-owned lighting, not authored World lights
or a general lighting pass.

Common Math uses column vectors, so the vertex shader applies
`projection * view * model * position`. World space follows the engine's Z-up
convention. The right-handed view looks along negative camera Z, and the
projection explicitly selects normalized depth from -1 to +1 for OpenGL.

The sample computes `transpose(inverse(linear(model)))` once per object and
uploads it in the shared object record. The vertex shader multiplies normals
by this matrix; the fragment shader normalizes the result before lighting.
Using `mat3(model)` would make a normal cease to be perpendicular to its
surface under nonuniform scale or shear. The visible center cube applies both,
and its World bounds are recomputed from the same transform. Singular or
nonfinite transforms fail before the uniform record is staged. The focused
normal-packing test checks transformed tangent orthogonality, std140 padding,
and failure atomicity. Mirrored transforms still need matching front-face
handling before they can be shown with back-face culling.

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
each object's model or tint distinct. The shared 240-byte camera record and
64-byte light record use separate mutable buffers. The depth and camera passes
share one aligned object-record batch, staged and uploaded before the frame.
These are distinct lifetimes rather than a measured speedup.

Capacity is fixed at creation; an append that does not fit leaves the previous
records intact. Failed GPU upload leaves the batch staged for a retry, and
`R_ResetUniformBatch` invalidates old record tokens before reuse. The caller
destroys the batch and owns the surrounding frame and draw order. Reusing the
same GPU buffer in a later frame is correct but can stall while the GPU reads
older draws; a measured multi-buffer strategy can be added when needed. This
graybox example updates four cube transforms and conservative world-space bounds,
while the floor and two translucent panels stay static. It holds seven World
objects and builds one frustum/layer/distance camera-view submission. Five
opaque objects contribute to the shadow depth pass, including the cube rejected
by that camera query. The
submission storage is allocated once; each build uses the linear World query
without allocating. A checked token
permits copied record reads until the host marks camera-visible scene items and
ends the submission. The host selects one model/tint/normal/cutout record for each
object from one batch upload and binds it in both passes. The host
resolves the objects'
logical mesh and material slots through bounded tables containing two static
mesh bundles and seven sample tint/texture-slot records. It checks every visible
slot and live mesh/texture handle before appending any draw. The final
pipeline, texture, uniform, and draw-range validation still happens inside
`R_DrawIndexed` during flush. World never owns GPU handles.
The batch and all three draw queues reset after the preceding frame ends. This
exercises a bounded linear visibility query through the public OpenGL 4.1
path. Interactive camera input can move the view; the object layout remains
hardcoded. There is no scene asset loading, spatial acceleration, general
material asset loading or a general pass scheduler.

## Ordered draw list

The sample resolves its material's explicit opaque or blend class and shadow
policy into caller-owned scene items. `R_ClassifySceneItems` first validates
the classes, finite blend distances, and all three output capacities. On
failure it leaves the caller's counts and index arrays unchanged. It then
returns shadow indices independent of camera visibility, opaque-camera indices
in input order, and blend-camera indices far-to-near by positive view distance
with input order breaking equal-distance ties. It performs no allocation or GPU
work and retains no draw handles. The caller copies selected draw descriptors
into separate fixed-capacity queues before beginning the frame. Each
flush in the active frame calls the existing validated `R_DrawIndexed` path in
append order. The lists hold renderer handles by value but do not retain the
underlying GPU resources, so the host keeps the meshes, textures, pipelines,
view buffers, and uniform batch alive through both flushes. They allocate at
initialization, then reset between frames without another allocation. The
queue itself preserves append order. Classification and queues are a bounded CPU
preparation boundary, not asynchronous GPU submission or a general render graph.

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

## Offscreen color and sampleable depth

An offscreen target accepts a linear RGBA8 or RGBA16F color texture, a GPU-only
D24 depth texture, or both color and depth. A color-only target keeps the
existing private D24 renderbuffer. When both textures are supplied, their
extents must match. Target textures have no initial pixel data, generated mip
chain, sRGB conversion, or repeat addressing. The target retains each supplied
caller-owned texture until it is destroyed.

### GPU-only HDR color

Create an RGBA16F texture with `renderTarget = true`, `sRGB = false`, and no
initial bytes, then pass its handle as the color attachment to
`R_CreateRenderTarget`. Creation requires
`R_CAPABILITY_FLOAT_RENDER_TARGETS`; the OpenGL 4.1 backend allocates
`GL_RGBA16F` storage and checks framebuffer completeness. End the target pass
before binding that texture as an ordinary sampled color image in a later
draw. The same feedback rule that protects depth attachments also applies to
this color attachment.

RGBA16F preserves linear values above 1.0 between passes. A later pass must
explicitly map those values into the chosen output range and color space;
merely sampling the texture does not tone-map it. The native smoke test draws
different HDR values into this target and checks distinct tone-mapped RGBA8
pixels in a second offscreen pass. Float uploads, cooked float textures, HDR
CPU readback, configurable exposure, and an automatic postprocess chain remain
separate work. `R_ReadTargetRGBA8` rejects an RGBA16F target without changing
the caller's buffer; it does not silently clamp HDR pixels.

### HDR presentation in the graybox sample

The sample requires float render targets and creates a linear RGBA16F color
texture matching the drawable's physical pixel size. Its target owns a private
D24 renderbuffer. Each frame, the host finishes the shadow-map pass, draws the
camera-visible objects into this HDR target, ends that pass, and draws one
indexed fullscreen triangle to the window. The presentation shader samples
the HDR texture, applies fixed unit-exposure Reinhard mapping
(`linear / (1 + linear)`), then applies the piecewise linear-to-sRGB transfer.
The sample sets `sRGBFramebuffer = false`, so shader encoding is the only
presentation transfer. The OpenGL backend now follows that configuration even
when the window happens to support automatic sRGB conversion. Enabling
framebuffer sRGB conversion with this shader would encode twice.

On a nonzero drawable resize, the host calls `R_Resize` and recreates the
fixed-size HDR target between frames. It destroys the old target before its
retained color texture; partial creation failure exits through the sample's
normal cleanup path. Native pixel oracles check float preservation, tone
mapping, translucent compositing, and opaque-depth occlusion; a separate
regression checks the disabled sRGB policy on a
capable context. The 120-frame hidden sample checks all three pass submissions
and cleanup. Its interactive appearance and resize are not covered by an
automated pixel comparison. This is a sample-owned presentation path, not a
general pass graph, exposure controller, or HDR editor viewport.

### Bounded translucent surfaces

The graybox sample has two translucent panels whose tint alpha passes through
the vertex and fragment shaders. Their graphics pipeline enables straight-alpha
blending and depth testing while disabling depth writes. Opaque camera draws
run first, then the scene classifier sorts visible translucent objects by their
view-space bounds-center distance and the sample appends them to a separate
fixed-capacity queue. In this right-handed camera view, greater distance draws
first. Input order breaks equal-distance ties. Neither panel enters the
shadow-caster queue because its sample material explicitly disables casting.

The native OpenGL smoke test paints an opaque near surface and two half-alpha
far/near surfaces into an RGBA16F target, then tone-maps into RGBA8 and checks
both an overlap pixel and an opaque-occluded pixel. This verifies the existing
blend and depth-write states through actual rendered output. Sorting whole
objects by center cannot correctly order intersecting transparent geometry or
every transparent triangle within one mesh. The cooked material bridge can
build opaque, straight-alpha `BLEND`, or `ADDITIVE` pipelines and expose their
authored modes. A scene owner maps opaque and straight-alpha materials, plus
its own shadow policy, into classifier input. The classifier has no dedicated
additive list yet, so an owner schedules additive draws after opaque geometry.
The sample still uses fixed material records instead of cooked material bundles.

The pipeline blend mode is explicit: disabled, straight alpha, or additive.
Additive color uses `source.rgb * source.a + destination.rgb`; its separate
alpha factors preserve destination alpha. An additive material tests depth but
does not write it, so the scene owner submits it after opaque geometry. A
native RGBA8 pixel check verifies the RGB sum, preserved alpha, and that an
opaque depth probe behind the additive surface can still draw. The graybox
sample does not yet place an additive object or schedule a distinct additive
pass; the bridge exposes the state for a caller to use.

### Alpha cutout and matching shadows

One cube uses an 8×8 RGBA image with a transparent center. Its material record
keeps opaque depth and blend state, but supplies a 0.5 cutoff through the shared
object uniform block. The camera fragment shader discards alpha below that
cutoff before writing color or depth. The shadow vertex shader carries the
same UV, and the shadow fragment shader samples the same image and cutoff
before its D24 write. Other sample materials use a negative cutoff so their
existing opaque and blended behavior is unchanged. The sample's masked cube
still occupies the opaque camera list and casts into the light pass.

The native OpenGL pixel test supplies cutoff through a uniform buffer. Below-
cutoff texels leave the background visible and allow a later depth-tested draw;
surviving texels block it. The same mask in a depth-only pass produces lit and
shadowed receiver pixels on the expected sides. CYMT V2 `MASK` adoption now
copies its authored cutoff into a caller-owned bundle and creates an opaque,
depth-writing pipeline. The scene owner must pass that cutoff to matching color
and shadow shaders; the bridge cannot inspect shader code to prove discard
semantics. The sample still uses fixed records and a temporary CPU image, so it
does not demonstrate loading an authored masked material. Coverage-preserving
mips and anti-aliased cutout edges are separate work.

### Depth-only target

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
color texture or missing comparison sampler. The graybox sample now manually
schedules a fixed-light depth pass followed by a camera pass; general
shadow-pass scheduling and authored light-space setup remain separate work.

### Fixed directional shadow in the graybox sample

The sample creates a 1024×1024 D24 depth texture, a depth-only target, and a
pipeline that writes depth without color. A fixed orthographic light view
covers its five opaque-pass World objects, including the masked cube. The host
stages one aligned uniform batch and three draw queues before beginning the
frame, because queues seal on flush and cannot reset inside a frame. Every
sample object marked as a caster enters the light pass,
including the cube outside the camera frustum. After ending the target pass,
the four opaque camera-visible draws and two translucent panel draws bind the
depth texture with a nearest-filtered
`LESS_EQUAL` comparison sampler in texture slot 1; slot 0 still carries the
cube white image, cutout image, or cooked floor checker. The shadow pass also
samples slot 0 and discards the same masked texels before writing depth.

The camera fragment shader transforms world position by the same light matrix,
maps its clip coordinates to depth-texture coordinates, and treats fragments
outside the fixed light volume as lit. It applies a constant reference-depth
bias and attenuates directional diffuse light on a failed comparison; ambient
light remains. The light position and volume, map size, bias, and single sample
are demonstration choices. There is no filtering, cascades, dynamic light
selection, or authored World light component here.

### Pipeline depth bias

Filled triangle pipelines can enable a slope factor and constant depth-offset
term for their rasterized fragments. The shadow caster pipeline uses this
offset while writing the D24 map; ordinary and wireframe pipelines leave it
disabled. A positive offset moves generated depth toward larger window-depth
values under OpenGL's conventional `LESS` projection. This helps keep a surface
from shadowing itself when its sampled depth differs slightly from the value
written during rasterization. The fragment shader retains a small comparison
bias; both terms still need tuning for a real scene, because too much offset
can detach a shadow from its caster.

Pipeline creation rejects non-finite bias values and enabled bias on line or
wireframe topology. Each draw sets the fill-offset state explicitly, so a
shadow pass cannot leak it into subsequent camera draws. When the renderer
borrows an editor's OpenGL context, it restores the host's polygon-offset
factor, units, and enable bits at the end of the hosted frame. This is
pipeline state, not an authored material property or an automatic shadow
quality setting.

OpenGL 4.1 treats depth textures as red textures for ordinary sampling; see
sections 3.8.9 and 3.8.16 of the [OpenGL 4.1 Core Specification](https://registry.khronos.org/OpenGL/specs/gl/glspec41.core.pdf).

## Offscreen color readback

`R_ReadTargetRGBA8` copies a rectangle from an explicit offscreen target's
linear RGBA8 color attachment into caller-owned bytes. Call it inside an active
frame after `R_EndTargetPass` and before `R_EndFrame`; it rejects an open target
pass, depth-only or RGBA16F target, stale handle, or out-of-bounds rectangle.
Coordinates start at the target's bottom-left pixel. Rows are bottom-up RGBA8, with the
caller choosing a four-byte-aligned row pitch of at least `width * 4` bytes.
Capacity must cover the last row's pixels but need not include padding after
them. Validation errors do not modify the destination; a native transfer
failure may leave a partial result.

The OpenGL backend reads from the target's private framebuffer and restores
the host's previous read framebuffer and pixel-pack state, including a bound
pixel-pack buffer. It is synchronous and may stall the CPU for the GPU. The
initial use is a deterministic pixel oracle for renderer tests and a narrow
diagnostic path. A presentation screenshot, multisample resolve, asynchronous
transfer, image encoding, row flip, and linear-to-sRGB conversion are separate
features.

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

When drawing into a borrowed OpenGL context, Cypher also disables inherited
alpha-to-coverage, alpha-to-one, sample-coverage, and sample-mask controls for
its draws. Those controls could otherwise change straight-alpha blending or
discard samples in a multisampled host target. Their enable bits are restored
at frame end; the host's numeric coverage and mask values are left untouched.
The native hosted-context smoke checks this state isolation, though it does
not supply a multisampled pixel oracle.

## Lifecycle and cleanup

The host creates the System window after renderer configuration has selected its
graphics attributes. `R_Init` borrows the window and owns the graphics context.
The example uses only public System, Common, and renderer functions: no native
OpenGL or SDL calls appear in the host.

Each frame pumps System events, handles Escape and window closure, and reads the
physical drawable dimensions. Minimized or zero-size drawables suspend drawing.
Dimension changes call `R_Resize` outside a frame, recreate the physical-size
HDR target, and rebuild the projection with the new aspect ratio. Uniform data
is uploaded before beginning the next frame.

Shutdown waits for outstanding graphics work, then releases objects in dependency
order: pipelines and targets, samplers and textures, vertex inputs, buffers,
shaders, renderer, window, System. The
same cleanup runs after partial initialization and failed drawing. Destroying the
pipeline releases its shader reference; destroying vertex input releases its
buffer references. The context remains alive until native objects are gone.

## Validation

The renderer test targets cover public contracts, lifecycle, shader ownership,
native compile/link rollback, pipeline dependencies, and draw validation. The
live OpenGL smoke test reads framebuffer pixels: the triangle must first be red
and then green after a uniform update. A second pixel oracle renders an
occluding triangle into a D24 target, shades a receiver using the target's
comparison sampler, and checks a shadowed pixel against a lit neighbor. A
separate two-pass HDR pixel oracle checks RGBA16F storage and explicit
tone-mapping into RGBA8. These tests exercise rendering paths; they do not
inspect the graybox scene's visible appearance. The smoke suite also checks
that disabling depth writes in one frame does not suppress the next frame's
depth clear. A separate native case verifies that a capable standalone
framebuffer stays sRGB-disabled when the renderer configuration requests it,
including after external GL code enables conversion between frames. The live
check skips on machines without a usable OpenGL 4.1 desktop session; hidden
windows still require graphics support.

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
- `assets/render_smoke/cube.cyshader`, `cube.vert`, `cube.frag`: camera-pass
  shader interface, sample lighting, and texture/shadow sampling.
- `assets/render_smoke/shadow.cyshader`, `shadow.vert`, `shadow.frag`: the
  depth-only shadow-pass shader.
- `assets/render_smoke/tonemap.cyshader`, `tonemap.vert`, `tonemap.frag`: the
  fullscreen presentation shader and explicit output transfer.
- `src/CypherRender/CypherRender_Shader.cpp`: public generational shader handles.
- `src/CypherRender/OpenGL/CypherRender_OpenGL_Shader.cpp`: stage compilation,
  link diagnostics, and native program ownership.
- `src/CypherRender/CypherRender_Pipeline.cpp`: immutable drawing state and shader
  dependencies.
- `src/CypherRender/CypherRender_Draw.cpp`: frame, layout, buffer, and range
  validation before native draw submission.

The graybox sample uses two scene meshes and one fullscreen triangle, a fixed
D24 light pass, a linear RGBA16F camera pass, and a tone-map presentation pass.
The camera pipelines have two uniform blocks and two sampled texture slots; a
copied World submission and separate shadow, opaque, and sorted translucent
draw lists feed those passes. The depth pipeline also samples albedo so its
masked silhouette matches the camera pass.
The renderer also has samplers, two binding slots, offscreen RGBA8/RGBA16F
color and sampleable depth targets, and a limited cooked-material bridge.
Authored scene and mesh loading, broader material support, full command
scheduling, and shader hot reload
remain separate work.

Device capability queries describe what the OpenGL driver reports, not a promise
that the public renderer already exposes every such feature. In particular,
indirect draws, geometry/tessellation shaders, and texture arrays still need
public contracts and tests before game or editor code may depend on them. Keep
those interfaces backend-independent so another
graphics API can implement the same engine behavior later.
