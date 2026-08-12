<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/source2_tooling_reference.md
//  Purpose: Records the Source 2 tooling and format lessons used by CypherEngine.
//  Details: This study maps publicly documented Source 2 authoring workflows to
//           original Cypher products, formats, services, and implementation gates.
//           It is a capability reference, not a plan to copy Valve code or names.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Source 2 Tooling Reference For CypherEngine

## Purpose

This document records what CypherEngine can learn from the publicly documented
Source 2 production environment. It has four goals:

1. identify the responsibilities behind the visible Source 2 tools
2. separate reusable architecture from Valve-specific product behavior
3. map useful capabilities to original Cypher tools and data contracts
4. prevent an impressive tool list from overriding the runtime-first build order

This is a research snapshot dated 2026-08-12. Source 2 is proprietary, its tools
vary by game and branch, and public documentation is incomplete. In particular,
the community-maintained Source2 Wiki has several useful deep pages and several
empty placeholders. It must not be treated as a complete Source 2 specification.

CypherEngine may study behavior, workflows, architecture, and public formats. It
must not copy Valve code, UI assets, branding, icons, proprietary data, or file
layouts.

## Primary Conclusion

The important lesson is not that Source 2 has many executables. The important
lesson is that its tools appear to share one production model:

```text
authored content
    -> typed source document
    -> schema and dependency validation
    -> type-specific compiler
    -> cooked runtime resource
    -> package or loose cooked tree
    -> runtime resource system

                         shared by

Asset Browser -> specialized editor -> live engine preview -> diagnostics
```

CypherEngine should reproduce that coherence, not the exact tool count. Mason,
focused Qt launch modes, headless compilers, CypherScope, and the runtime should
all use the same schemas, resource IDs, dependency graph, compiler libraries,
preview bridge, and diagnostics model.

## Evidence Boundary

The review used these public references:

- [Source2 Wiki Engine Tools](https://www.source2.wiki/EngineTools?game=any)
- [Source2 Wiki File Formats](https://www.source2.wiki/FileFormats)
- [Source2 Wiki Asset Structure](https://www.source2.wiki/FileFormats/asset-structure)
- [Source2 Wiki VMAP](https://www.source2.wiki/FileFormats/vmap)
- [Source2 Wiki Resource Compiler](https://www.source2.wiki/EngineTools/ResourceCompiler)
- [Official Source 2 Tools category](https://developer.valvesoftware.com/wiki/Category:Official_Source_2_Tools)
- [Valve Hammer Editor for Source 2](https://developer.valvesoftware.com/wiki/Valve_Hammer_Editor_%28Source_2%29)
- [Source 2 Filmmaker category](https://developer.valvesoftware.com/wiki/Category:Source_Filmmaker)

Statements about internal implementation that are not established by public
documentation remain hypotheses and must not become Cypher requirements.

## Tool-System Architecture

### Asset Browser As The Front Door

The documented Asset Browser is more than a file picker. It browses core, game,
and add-on content; filters and previews assets; launches the associated editor
for an asset type; and acts as a hub for other tools.

Cypher lesson:

- Mason's Asset Browser is the normal entry point for authored content.
- Double-click dispatch comes from the Cypher resource/compiler registry, not a
  hard-coded switch in Qt code.
- The same registry owns icons, type names, source extensions, cooked types,
  preview providers, validators, compilers, and editor launch modes.
- Core engine content, project content, add-ons, and mounted packages must remain
  distinguishable in search results and diagnostics.

### Shared Tool Shell

Source 2 tools expose recurring concepts: file/edit/view/help menus, dockable
panels, current-asset lists, property inspectors, engine viewports, undo/redo,
recent files, and a Tools menu that opens other editors.

Cypher lesson:

- Mason workspaces and focused applications use `CypherToolFramework`.
- A focused editor is a launch mode over shared document and compiler libraries,
  unless isolation or startup requirements justify a separate executable.
- Tool state, editor-only preview controls, and runtime resource data are
  different ownership domains.

### Content And Runtime Separation

Source 2 commonly keeps editable source under a content tree and compiled `_c`
resources under a game tree. The runtime consumes cooked resources while tools
retain the richer source representation.

Cypher lesson:

- source and cooked roots are logical mount roles, even if their final directory
  names differ from Valve's names
- source files are optimized for editing, mergeability, diagnostics, and schema
  migration
- cooked files are optimized for validation, bounded loading, streaming, and
  target-specific runtime use
- cooked data is generated and disposable; authored source is authoritative
- Mason may display both sides, but it must never silently treat cooked output as
  the editable master

### Resource Compiler Coordination

The documented Source 2 Resource Compiler accepts individual files, file lists,
wildcards, recursion, force modes, target/game context, loose output, and package
updates. Asset-specific compile behavior exists behind the coordinator.

Cypher lesson:

- `CypherResourceCompiler` is a coordinator over type-specific compiler libraries
- each type-specific compiler remains directly testable and callable by Mason
- all compilers accept an explicit project, target, profile, and dependency
  context
- full, shallow, dependency-only, and forced builds require defined semantics
- machine-readable diagnostics and dependency reports are mandatory
- incremental builds are hash- and dependency-driven, not timestamp folklore

### Live Preview And Global Preview

Particle and post-processing documentation shows editor-only controls, engine
viewports, debug visualizations, preview fixtures, and changes broadcast to
other tool or game views.

Cypher lesson:

- preview state must be explicitly tagged as editor-only or persistent source
- the preview uses the real renderer and resource compiler path
- global preview is an engine/editor protocol, not direct shared-memory mutation
- multiple open documents need an explicit active-preview owner
- shipping behavior must be testable outside tool mode

### External Diagnostics

VConsole separates high-volume logs and diagnostics from the game window. It
supports commands, search, filters, channels, dockable plugins, and multiple
runtime/tool connections.

Cypher lesson:

- `CypherConsole` is a standalone Qt product as well as a Mason panel
- logging, CVars, commands, events, and telemetry share a versioned transport
- channel verbosity is runtime-configurable
- tool plugins consume structured events rather than parsing colored log text
- the console must remain useful when Mason or the game crashes

## Publicly Documented Tool Matrix

| Source 2 capability | Core responsibility | Cypher direction |
| --- | --- | --- |
| Asset Browser | Browse, filter, preview, compile, and dispatch assets to editors. | Mason Asset Browser plus resource-type registry. |
| Hammer | World geometry, entities, layers, visibility, lighting, physics preview, build, and play workflows. | Mason Map and World workspace. |
| Material Editor | Author materials and preview renderer-facing parameters. | Mason Material workspace plus `CypherMaterialCompiler`. |
| ModelDoc | Assemble model source, skeleton, meshes, collision, LODs, attachments, and related data. | Mason Model and Character Setup workspaces plus `CypherModelCompiler`. |
| Legacy Model Editor | Older focused model workflow used by specific branches. | No duplicate product; retain a focused Mason Model launch mode. |
| Animgraph Editor | Author parameters, state machines, blend graphs, procedural constraints, events, and runtime behavior. | Mason Animation Graph workspace plus a dedicated graph compiler/runtime. |
| Particle Editor | Stack typed particle functions and preview the result in-engine. | Mason VFX workspace, with `Quark` remaining an optional display name. |
| Post Processing Editor | Layer, mask, preview, and compile post effects and color operations. | Mason PostFX and Color workspace plus `CypherPostFXCompiler`. |
| Image Subrect Editor | Define reusable image regions/hotspots for world and texture workflows. | Mason Texture Atlas/Subrect mode; initially part of Texture Lab. |
| VConsole | External logs, commands, filters, channels, events, and plugin views. | `CypherConsole` plus shared telemetry protocol. |
| Resource Compiler | Coordinate type-specific cooking and package output. | `CypherResourceCompiler` plus compiler registry. |
| Workshop Manager | Package, validate, upload, and administer workshop content. | Mason Mod/Release workspace plus `CypherModManager` and publisher adapters. |
| CS2 Workshop Item Editor | Game-specific economy/skin authoring and submission. | Do not generalize prematurely; build game-specific item tools only when needed. |
| Source 2 Filmmaker | Timeline-based cameras, shots, actors, animation, sound, effects, recording, and final output. | Deferred Mason Cinematic workspace and focused launch mode. |

The public asset inventory also implies tools or workflows not fully represented
on the Source2 Wiki Engine Tools page:

| Implied capability | Cypher direction |
| --- | --- |
| Typed generic data editing (`vdata`) | Mason Data and Schema workspace using CYKV schemas. |
| Audio event, stack, and mix graph authoring | Mason Audio Event and Mix Graph modes. |
| Visual gameplay/logic graphs (`vpulse`) | Mason Objective and Flow workspace; Lua remains gameplay scripting. |
| Smart/parametric props (`vsmart`) | Mason Procedural Object mode after prefab and rule systems exist. |
| Response rules | Mason Dialogue/Response Rules mode when AI dialogue requires it. |
| Resource manifests | Mason Build/Cook and Resource Manifest views. |
| Module metadata | Mason Plugin/Module workspace and `cypher.project` metadata. |

## Detailed Capability Lessons

### Hammer And World Authoring

The surveyed Hammer material establishes several concrete capabilities:

- editable mesh geometry rather than brush-only world construction
- physics simulation for models and editor meshes
- explicit collision representations, including convex requirements for dynamic
  simulation
- visibility contributors, exclusions, debug views, hints, and compiled data
- modern visibility output that can include GPU-cullable meshlet data
- lightmap player-space volumes and luxel-density visualization
- in-editor and in-game inspection of compiled world products

Cypher must therefore treat geometry, collision, visibility, lighting, and
runtime partitioning as separate compiler stages. `.cymap_c` may coordinate or
contain those products, but no renderer API should assume a classic BSP-only
world.

### Editable Mesh Topology

The documented VMAP representation uses a half-edge polygon mesh with streams
for vertex, per-corner, edge, and face data. This supports arbitrary polygon
faces and efficient topology editing, but carries strict topology invariants.

Mason's eventual topology layer needs:

- stable vertex, half-edge, edge, loop, and face IDs
- twin, next, face, and destination relationships
- per-corner UV, normal, tangent, and other attribute streams
- validation for manifoldness, winding, duplicate edges, degenerate faces, and
  disconnected or ambiguous topology
- explicit sanitation and diagnostics when importing arbitrary triangle meshes
- deterministic triangulation for runtime output

The exact VMAP array layout is not a Cypher contract. Cypher should design its
own topology representation around Mason operations and compiler invariants.

### Animation Graphs

The surveyed Animgraph material covers more than clip blending. Its documented
categories include:

- typed parameters with defaults, ranges, interpolation, and write permissions
- client/server evaluation and replication policy
- state machines, conditions, actions, transition timing, and blend curves
- one- and two-dimensional blends, additive/subtractive layers, aim and lean
- masks, tags, audio/particle events, root motion, and sequence completion
- look-at, IK, foot pinning, path following, hit reactions, jiggle, slope, and
  movement helpers
- motion matching metrics and selection controls
- graph components, subgraphs, priorities, LOD, ragdoll, scripts, and demo data

Cypher should split this into four contracts:

1. authored animation graph and schema
2. deterministic graph compiler
3. compact runtime graph evaluator
4. Mason graph editor and live debugger

Networking flags in an editor do not replace a replication design. Graph values
must have explicit authority, interpolation, serialization, and prediction rules.

### Particle And VFX Authoring

The surveyed Particle Editor presents effects as ordered typed functions:

- base properties and defaults
- pre-emission operations
- emitters
- spawn-time initializers
- update operators and renderers in the broader particle model
- control points and preview-only fixtures
- live viewport visualization of particle attributes and control data

Cypher VFX should use registered module descriptors with explicit phases,
read/write attribute declarations, ordering rules, deterministic random streams,
CPU/GPU capability flags, budgets, and fallback policy. The editor should not
encode runtime behavior in Qt widgets.

### Post Processing

The surveyed Post Processing Editor demonstrates:

- ordered layers and per-layer opacity
- masks and tonal-range selection
- live local or global preview
- source layers that may compile into a lower-cost LUT
- runtime-only operations such as exposure, tone mapping, and bloom
- map volumes, master profiles, overlap priority, and transitions
- diagnostic views for albedo, normals, roughness, AO, and other buffers

Cypher must preserve source layers even when cooking bakes them into a LUT. A
cooked resource is not expected to reconstruct the complete authored stack.

### Model And Character Authoring

Public Source2 Wiki pages for ModelDoc are currently sparse, but the asset model
and Valve documentation establish the useful boundary: imported geometry is
assembled with skeletons, LODs, material groups, hitboxes, physics, attachments,
morph/facial data, animation references, and reusable templates.

Cypher should not build a replacement for Blender. Mason owns engine-specific
assembly, validation, preview, collision, sockets, LOD policy, animation binding,
and compilation. General mesh sculpting and character creation remain DCC work.

### Distribution Tools

Workshop Manager and the CS2 item editor prove the value of integrated package
validation and publishing, but they are not first-runtime requirements. Cypher's
general solution is:

```text
project/mod manifest
  -> dependency closure
  -> validation
  -> deterministic package
  -> signature and provenance
  -> platform-specific publisher adapter
```

Game-specific economy or cosmetic submission tools are separate products built
only when a game design and distribution target require them.

## Surveyed Source 2 Format Families

The following inventory records every family listed by the reviewed Source2 Wiki
asset-structure page. `Study` means the responsibility is relevant. `Do not
mirror` means Cypher should not create a matching format merely for parity.

| Family | Source 2 formats | Cypher decision |
| --- | --- | --- |
| Package and generic data | `vpk`, `kv3`, DMX, `vdata` | Study. Use `.cypak`, CYKV, typed schemas, and Cypher-owned binary formats. |
| Resource metadata | `vrman`, `vrmap`, `vmmd`, tool asset-info `.bin` | Study manifests/module metadata; defer remap tables until a real override workflow exists. |
| Map and world | `vmap`, `vwrld`, `vwnod`, `vvis`, `vents`, `rect` | Study source map, world partition, visibility, entity, and subrect responsibilities. |
| Render resources | `vmat`, `vtex`, `vpost`, `vsurf`, `vcs`, `vcompmat`, `vsvg`, `vsgraph` | Study materials, textures, post profiles, surfaces, shaders, composites, and vector graphics. Surface graphs remain demand-driven. |
| Model and legacy animation | `vmdl`, `vmesh`, `vanim`, `vagrp`, `vseq`, `vphys`, `vmorf` | Study responsibilities, not legacy decomposition. Cypher compilers choose their own cooked chunks. |
| Model reuse | `vmodel_template`, `vmdl_prefab` | Study reusable templates and prefab inheritance. |
| Animation Graph 1 | `vanmgrph`, `vsubgrph` | Study graph/subgraph separation. Do not reproduce a legacy version sequence. |
| Animation Graph 2 | `vnmskel`, `vnmclip`, `vnmgraph`, `vnmvar` | Study skeleton/clip/graph separation and variation policy. |
| Particle/VFX | `vpcf`, `vsnap` | Study effect definitions and auxiliary snapshots. |
| Audio | `vsnd`, sound containers, `vsndevts`, `vsndstck`, `vmix` | Study sample, event, rule stack, container, and mix-graph separation. |
| UI and web-like assets | `vcss`, `vxml`, `vpdi`, `vjs`, `vts`, `vsvg` | Study layout/style/resource separation. CyGUI need not use JavaScript or copy Panorama. |
| Logic graphs | `vpulse` | Study visual graph compilation; use only where a graph improves a real workflow. |
| Procedural objects | `vsmart` | Study parameterized prop rules after normal prefabs are proven. |
| Dialogue and response | `vrr`, `vcd`, `vcdlist`, `vfe` | Study response selection, choreography, and facial data; design original Cypher formats. |
| Text and accessibility | caption `.dat`, `vfont` | Study compiled captions and font metadata. |
| Game-specific commerce | `econitem` | Do not mirror in the general engine. |
| Game-specific Dota data | `vdacdefs`, `vdvn`, `vdpn`, `herolist`, `item` | Do not mirror. Use typed project/game schemas when Cypher games need equivalent data. |
| Historical or uncertain | `vpram`, legacy `vnmvar`, unused legacy animation assets | Do not mirror. Research only when a concrete Cypher subsystem needs the behavior. |

This table is a responsibility inventory, not an extension reservation list.

## Cypher Format Direction

### Shared Source Language, Separate Schemas

CYKV may encode many editable source documents, but each domain owns a distinct
schema and typed decoder. Sharing syntax does not make maps, materials, models,
audio events, and settings interchangeable.

```text
CYKV syntax
  -> domain schema
  -> typed source model
  -> domain compiler
  -> domain runtime resource
```

### Generic Data

Cypher needs a typed generic-data path for project- or game-defined records that
do not justify a bespoke binary format immediately. The working direction is a
schema-selected CYKV source document compiled into a bounded `.cydata_c`-style
runtime resource. The exact extension is not locked until its runtime consumer
and schema identity rules are designed.

Likely uses include weapons, items, damage profiles, enemy archetypes, waves,
difficulty, response rules, surface properties, tags, and game modes.

### World Output Is A Resource Graph

Source 2's map asset inventory reinforces an existing Cypher decision: a cooked
map is not necessarily one monolithic spatial structure. `CypherMapCompiler` may
produce or reference:

- map descriptor and dependencies
- world regions or nodes
- render batches
- entity records
- collision
- visibility
- lightmaps and probes
- navigation
- audio and post-process zones

Early versions may store these as chunks in `.cymap_c`. Streaming pressure may
later justify independently addressable child resources. The runtime contract,
not Source 2 naming, decides that transition.

### Resource Manifests

Cypher needs explicit preload/resource-set manifests for startup groups, maps,
mods, servers, and tests. A manifest records stable resource references and load
policy; it does not own the resources themselves. `CypherResourceCompiler`
validates and compiles manifests through the normal dependency graph.

## Mason Name

`Mason` remains the authoritative product name because it communicates building
and construction without borrowing Valve branding.

If a formal long form is useful, the recommended backronym is:

**MASON: Map and Scene Operations Nexus**

It describes Mason's origin as a world editor and its eventual role as the hub
for asset, scene, simulation, and production workspaces. The codebase and normal
documentation should still write `Mason`, not force uppercase `MASON` into every
identifier.

## Cypher Capability Deltas

The Source 2 review makes these previously broad requirements explicit:

1. asset types need associated editor, preview, validator, and compiler metadata
2. Texture Lab needs atlas, subrect, sprite, and hotspot authoring
3. Animation needs a graph compiler/runtime and network-authority diagnostics
4. Audio needs distinct sample, event, rule/stack, and mix-graph models
5. VFX needs typed phase-aware modules and preview control points
6. PostFX needs source layers, masks, LUT baking, volumes, and global preview
7. Resource manifests need a first-class schema and compiler path
8. generic typed game data needs a runtime cooking path
9. procedural objects and response rules are valid later workspaces
10. CypherConsole needs a structured plugin/event protocol
11. cooked maps need independently evolvable world, entity, visibility, lighting,
    collision, and navigation products
12. source/cooked paired inspection belongs in CypherScope

## Correct Development Order

The engine should not be declared fully complete before any tools are built. That
would freeze formats without proving the authoring pipeline. The correct order is
vertical and dependency-driven:

1. runtime subsystem and data consumer
2. source schema and typed source model
3. headless compiler and validator
4. cooked loader and runtime diagnostics
5. command-line inspector and regression fixtures
6. minimal Mason workspace using the same libraries
7. production authoring features after the vertical slice works

The first renderer can remain OpenGL. A renderer backend boundary should exist
before Vulkan, but Vulkan is not a prerequisite for CYKV, resources, compilers,
world authoring, or the first Mason viewport. Add Vulkan only when the OpenGL
path and real game/tool workloads establish the requirement.

## Acceptance Rules

Every accepted Cypher tool or format eventually requires:

- an owner and explicit non-responsibilities
- source schema and version policy
- deterministic compiler or serializer
- cooked validation and bounded loading
- dependency and reference reporting
- actionable diagnostics with source/object locations
- headless automation
- undo/redo and recovery for persistent GUI edits
- representative correctness, malformed-input, golden, and round-trip tests
- large-data and hot-path benchmarks where performance matters
- Windows, Linux, and macOS verification
- migration and deprecation policy

The destination is broad. The next implementation milestone remains narrow:
finish the runtime resource path and one complete source-to-cooked vertical slice
before constructing the full Mason application suite.
