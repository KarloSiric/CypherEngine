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

The CPU structure and the GLSL `Transforms` block both contain three column-major
matrices, with std140-compatible offsets:

| Matrix | Byte offset | Purpose |
| --- | --- | --- |
| `model` | 0 | Rotate the cube from local space into world space. |
| `view` | 64 | Transform world positions into camera space. |
| `projection` | 128 | Project camera-space positions into clip space. |

The complete block is 192 bytes. Compile-time checks protect the CPU offsets and
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

This slice supports an indexed triangle list, a single optional uniform block,
and direct submission to the window framebuffer. Mesh asset loading, materials,
textures, render targets, command recording, and shader hot reload remain later
milestones. The first visible cube establishes the renderer path those features
can build on.
