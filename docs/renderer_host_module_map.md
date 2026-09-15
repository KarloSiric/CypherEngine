# Cypher Host And Renderer Module Map

This document defines the intended source ownership for the first production
renderer path. It is a sequencing contract, not a promise to create every
possible rendering feature before a game needs it.

## Design Rules

1. Host selects policy and owns process-level startup order.
2. CypherSystem owns native windows and graphics-context integration.
3. CypherWorld owns loaded spatial state and coarse, view-specific visibility.
4. CypherRender owns backend-neutral renderer state and live render objects.
5. A backend owns native API objects and never exposes native types to Host or
   CypherWorld.
6. Cooked assets are immutable CPU-side resources; renderer handles identify
   live backend objects created from those resources.
7. Public APIs are added only when their ownership, lifetime, failure behavior,
   and validation rules are defined.
8. The tree must compile and tests must pass after every implementation stage.

## Runtime Dependency Direction

```text
main
  -> CypherHost
       -> Entity / Physics simulation
       -> CypherWorld view query
            -> immutable renderer-neutral submission
       -> CypherRender public frontend
            -> private backend dispatch
                 -> OpenGL backend
                      -> CypherSystem GL integration
       -> CypherSystem window API
       -> CVar / Config / FS / Memory / Log
```

Dependencies do not point back toward Host. CypherRender does not traverse
CypherWorld, and OpenGL files do not become public dependencies of the renderer
frontend, world, gameplay, resources, or tools.

## Host Files

| File | Visibility | Responsibility |
| --- | --- | --- |
| `CypherHost.h` | Public | Process-level initialization, frame loop, and shutdown entry points. |
| `CypherHost_Types.h` | Public | Host configuration and process-lifetime state records. |
| `CypherHost_Error.h` | Public | Stable Host failure values and diagnostic conversion. |
| `CypherHost_Local.h` | Private | Ordered startup stages and implementation-only integration helpers. |
| `CypherHost.cpp` | Private | Current lifecycle implementation; split only when concrete sections become independently testable. |

Possible future implementation splits are `CypherHost_Config.cpp`,
`CypherHost_Commands.cpp`, and `CypherHost_Render.cpp`. They are created when
their code moves out of `CypherHost.cpp`; empty translation units are forbidden.

## Renderer Foundation

| File | Status | Responsibility |
| --- | --- | --- |
| `CypherRender_Public.h/.cpp` | Active | Stable lifecycle, frame ordering, resize, and presentation API. |
| `CypherRender_Types.h` | Active | Backend-neutral configuration, device information, limits, and shared values. |
| `CypherRender_Error.h/.cpp` | Active | Stable renderer failures and diagnostic names. |
| `CypherRender_Backend.h/.cpp` | Active | Private function table, validation, and backend selection. |
| `CypherRender_Local.h` | Active | Private frontend process state shared by renderer implementation files. |

## First Indexed Draw

These contracts are designed and implemented in dependency order. Together
they are the smallest complete route to a colored indexed cube without placing
OpenGL calls in Host.

| Order | File | Responsibility |
| --- | --- | --- |
| 1 | `CypherRender_Buffer.h/.cpp` | Immutable and updateable byte storage with typed usage intent. |
| 2 | `CypherRender_VertexLayout.h/.cpp` | Vertex bindings, attributes, formats, strides, and index types. |
| 3 | `CypherRender_Shader.h/.cpp` | Runtime shader/program objects created from validated cooked shader views. |
| 4 | `CypherRender_Pipeline.h/.cpp` | Immutable topology, raster, depth, blend, shader, and vertex-layout state. |
| 5 | `CypherRender_Command.h/.cpp` | Ordered frame commands and bounded command storage. |
| 6 | `CypherRender_Draw.h/.cpp` | Backend-neutral vertex/index bindings and indexed draw validation. |

Each public operation first validates frontend state and handles, then invokes
the selected backend callback. Handles are generation-checked and type-checked;
native OpenGL names never become renderer handles directly.

## Textured Surface Stage

These files are created after the colored indexed draw works through Host:

| File | Responsibility |
| --- | --- |
| `CypherRender_Texture.h/.cpp` | Image dimensions, mip levels, array layers, formats, and uploads. |
| `CypherRender_Sampler.h/.cpp` | Filtering, addressing, anisotropy, LOD, and comparison policy. |
| `CypherRender_Binding.h/.cpp` | Shader-visible buffer, texture, and sampler bindings. |
| `CypherRender_Mesh.h/.cpp` | Reusable geometry objects built from renderer buffers. |
| `CypherRender_Material.h/.cpp` | Pipeline plus resource bindings created from cooked materials. |

## Offscreen And Submission Stage

The following modules are deliberately later because the first draw does not
need them:

| File | Responsibility |
| --- | --- |
| `CypherRender_RenderTarget.h/.cpp` | Color, depth, and stencil attachment sets. |
| `CypherRender_Pass.h/.cpp` | Load/store, clear, viewport, and attachment policy. |
| `CypherRender_View.h/.cpp` | Camera matrices, viewport, scissor, and view constants. |
| `CypherRender_RenderQueue.h/.cpp` | Accepts visible candidates, classifies passes, sorts, and batches work. |
| `CypherRender_Occlusion.h/.cpp` | Optional renderer-side fine or GPU occlusion after coarse world visibility. |
| `CypherRender_Light.h/.cpp` | Backend-neutral light submission and GPU records. |
| `CypherRender_Shadow.h/.cpp` | Shadow views, atlases, filtering, and update policy. |
| `CypherRender_Debug.h/.cpp` | Debug labels, markers, wireframe, lines, and renderer reports. |
| `CypherRender_Capture.h/.cpp` | Screenshots, readback, and later frame-capture integration. |

`CypherRender_View` describes renderer constants for an already selected view;
it does not own a gameplay camera or search the world. Frustum traversal,
VisAreas, portals, terrain LOD, vegetation selection, and streaming decisions
belong to CypherWorld.

## CypherWorld Handoff

CypherWorld produces one immutable submission per requested view. The initial
submission contains visible mesh/surface candidates, transform values, material
and renderer handles, visible light candidates, terrain chunks, and environment
values. It contains no OpenGL names and no backend commands.

CypherRender consumes the submission by:

1. validating all live renderer handles
2. classifying items into render passes
3. sorting by pipeline, material, geometry, and depth policy
4. batching compatible work
5. applying optional fine/GPU occlusion
6. recording backend-neutral commands
7. dispatching those commands to the selected backend

The renderer never calls CypherWorld to find more objects. Auxiliary shadow,
reflection, probe, and editor views will use explicit view requests after the
primary-view path is working.

## OpenGL Backend Files

`CypherRender_OpenGL.cpp` currently owns lifecycle and capability discovery.
As public contracts become executable, native implementations split into:

```text
OpenGL/CypherRender_OpenGL_Local.h
OpenGL/CypherRender_OpenGL_Buffer.cpp
OpenGL/CypherRender_OpenGL_VertexLayout.cpp
OpenGL/CypherRender_OpenGL_Shader.cpp
OpenGL/CypherRender_OpenGL_Pipeline.cpp
OpenGL/CypherRender_OpenGL_Command.cpp
OpenGL/CypherRender_OpenGL_Draw.cpp
OpenGL/CypherRender_OpenGL_Texture.cpp
OpenGL/CypherRender_OpenGL_Sampler.cpp
OpenGL/CypherRender_OpenGL_RenderTarget.cpp
OpenGL/CypherRender_OpenGL_Debug.cpp
```

An implementation file is added to CMake only when it owns executable code.
This avoids empty files and prevents unfinished APIs from appearing supported.

## Implementation Gates

### Gate 1: Host Lifecycle

- Host configures the window through `R_ConfigureWindow` before creation.
- Host initializes the renderer after window creation.
- The real executable clears and presents through `R_BeginFrame/R_EndFrame`.
- Resize, minimize, restore, failure rollback, and shutdown are tested.

### Gate 2: Colored Indexed Draw

- Generation-checked buffers and shader programs work through the backend table.
- Vertex layout and pipeline validation reject malformed descriptions.
- A cube is rendered without direct OpenGL calls outside the backend.

### Gate 3: Textured Material

- Cooked texture and material resources create renderer objects.
- Sampler and binding contracts work across the frontend/backend boundary.
- A textured, depth-tested cube renders through the same public draw path.

### Gate 4: World Submission Foundation

- Camera/view constants and renderer queue sorting/batching are active.
- CypherWorld supplies coarse visible candidates through its public handoff.
- Debug lines and wireframe modes can inspect the scene.
- Only after this gate should Mason or ImGui renderer integration begin.

## Immediate Snippet Order

1. Parse Host renderer CVars into `render_config_t`.
2. Call `R_ConfigureWindow` before `Sys_CreateWindow`.
3. Add renderer initialization and complete rollback.
4. Add resize/minimize-safe frame presentation.
5. Add Host integration tests.
6. Design and implement `CypherRender_Buffer.cpp`.
7. Design the vertex-layout contract.
