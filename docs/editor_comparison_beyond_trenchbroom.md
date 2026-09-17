<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/editor_comparison_beyond_trenchbroom.md
//  Purpose: Compares editor capabilities beyond TrenchBroom for TileEditor
//           and Mason planning.
//  Details: Claims are separated from Cypher recommendations and cite official
//           or primary documentation directly.
//
//  History:
//  - Created by Karlo Siric on 2026-09-17
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Editor Comparison: Capabilities Beyond TrenchBroom

**Prepared for:** CypherTileEditor and Mason
**Date:** 2026-09-17
**Scope:** Valve Hammer and Source 2/Dota tile workflows, GtkRadiant/Q3Radiant, J.A.C.K., Blender mesh/UV editing, and Unity ProBuilder, compared with TrenchBroom as the baseline.

## Table of contents

- [1. Reading this audit](#1-reading-this-audit)
- [2. Executive findings](#2-executive-findings)
- [3. Capability comparison](#3-capability-comparison)
- [4. Editor-by-editor verified findings](#4-editor-by-editor-verified-findings)
- [5. Capabilities materially absent from TrenchBroom's normal model](#5-capabilities-materially-absent-from-trenchbrooms-normal-model)
- [6. Geometry robustness contract](#6-geometry-robustness-contract)
- [7. Staged Cypher adoption plan](#7-staged-cypher-adoption-plan)
- [8. Explicit rejection and deferral decisions](#8-explicit-rejection-and-deferral-decisions)
- [9. Concrete acceptance criteria](#9-concrete-acceptance-criteria)
- [10. Primary-source index](#10-primary-source-index)

## 1. Reading this audit

This document separates two kinds of statements:

- **Verified fact** describes behavior documented by an editor's official project, vendor, or product documentation. Each substantive claim has a direct source link.
- **Cypher recommendation** is a proposed design choice. It is not a claim that the cited editor uses the same internal representation or algorithm.

“Absent from TrenchBroom” means **not exposed as a first-class workflow in the current public TrenchBroom manual**, not “provably absent from every code path.” TrenchBroom already has substantial functionality that should not be understated: convex brush construction and CSG, Quake 3 patches, texture projection, groups and layers, linked groups with property protection, entity definitions, compilation profiles, and manual terrain techniques through vertex/patch editing. Its deliberate constraint is that it remains a brush-first level editor rather than a general mesh DCC package or a domain-specific tile-rule editor.

The Source 2 references require one caveat. Valve's Source 2 tools differ by game branch. The Dota 2 tile-editor material documents a real Source 2 workflow, but it should be treated as a design reference rather than a promise about the current Counter-Strike 2 Hammer interface. Valve Developer Community pages are Valve-hosted documentation; some content is community maintained. The Dota pages cited from the official Chinese Dota 2 site mirror Valve Developer Community material, and the Steam news posts are direct Valve release notes.

The GtkRadiant manual is legacy Q3Radiant documentation still linked by the official GtkRadiant project. It is useful evidence for established Radiant interaction patterns, but it is not a complete description of every current GtkRadiant 1.6.7 behavior. The J.A.C.K. manual is official but dated November 2016; the official feature page provides the current product-level claims. Unity pages refer to ProBuilder 5.0.7. Blender links use the current manual where possible and pinned manual versions where a stable page was easier to cite.

## 2. Executive findings

The largest gaps are not a longer list of primitive shapes. They are missing **semantic authoring models**:

1. **A tile-rule model.** Valve's Dota tile editor stores meaning at tile boundaries and in tileset metadata: height, path connectivity, water, vegetation rules, placement categories, variants, and probabilities. TrenchBroom manipulates spatial objects but does not provide a comparable rule resolver for a tile game's adjacency and gameplay data.
2. **An arbitrary-topology mesh workflow.** Blender and ProBuilder expose loop/ring selection, cut, bridge, dissolve, weld, bevel, topology-aware growth, and robust element selection. TrenchBroom's brush model intentionally keeps solids convex; patches are curved grids rather than a full polygon-mesh kernel.
3. **A durable instance/prefab model.** Source Hammer instances, Source 2 prefabs, Blender linked data/library overrides, and TrenchBroom linked groups each solve part of the problem. Cypher needs an external asset, stable instance identity, explicit overrides, reference fix-up rules, dependency tracking, and deterministic expansion during build.
4. **Dedicated terrain and paint data.** Manual vertex movement is not equivalent to a terrain/displacement object with height operations, blend layers, holes, collision/nav invalidation, water boundaries, and diagnostics.
5. **First-class paths and projectors.** J.A.C.K. documents rich path editing and decal preview. Cypher needs reusable path/spline data and decal/projector objects rather than encoding both as fragile conventions over generic entities.
6. **A tighter build/debug loop.** Radiant region compilation, J.A.C.K. compiler-error navigation and leak/portal overlays, and Source 2's selectively skippable build stages directly improve iteration. TrenchBroom's compile profiles are useful but do not, by themselves, provide those diagnostics or dependency-aware incremental rebuilding.
7. **Geometry validation as a product feature.** J.A.C.K.'s problem checker and compiler-error navigation, Radiant's crack/z-fighting guidance, Blender's topology tools, and ProBuilder's repair operations show that creation tools alone are insufficient. Every destructive Cypher command needs preconditions, typed failure, validation, predictable snapping, and undo/redo invariants.

## 3. Capability comparison

Legend: **Strong** = a first-class documented workflow; **Partial** = useful support with narrower semantics; **No first-class workflow** = not documented as a dedicated tool in the reviewed manual; **Branch-specific** = documented for a particular Source/Source 2 game toolchain.

| Capability | TrenchBroom baseline | Valve Hammer / Source 2 | GtkRadiant / Q3Radiant | J.A.C.K. | Blender | Unity ProBuilder | Cypher direction |
|---|---|---|---|---|---|---|---|
| Tile/grid world authoring | Grid snapping and brush construction; no semantic tile-rule editor | **Strong, branch-specific:** Dota tile grid, height/path/water/vegetation/object modes, rule-aware variants | Strong geometric grid, no comparable semantic tile resolver | Strong geometric grid and anchor snapping, no comparable semantic tile resolver | Flexible snapping; no gameplay tile-rule editor | Grid/surface snapping and parametric primitives; no comparable tile-rule editor | Build a dedicated tile domain model in CypherTileEditor; share only generic primitives with Mason |
| Convex brush CSG | **Strong** | **Strong** in brush-oriented Hammer workflows; Source 2 also documents mesh editing | **Strong** | **Strong** | Boolean modifier available, but DCC-oriented | Boolean is documented as experimental | Retain deterministic convex CSG for grayboxing; do not make general booleans the only modeling path |
| Arbitrary mesh topology | No first-class full mesh-editing workflow | **Strong/branch-specific:** Source 2 Hammer has a documented mesh-editing series | No; brushes plus patch grids | Mostly brush-centric; offers triangulation and manipulation aids | **Strong** | **Strong** for in-editor polygon modeling | Add a controlled manifold mesh kernel to Mason after brush editing is reliable |
| Curves, patches, splines | Quake 3 patch support | Displacements/terrain and game-specific path entities; Source 2 feature set varies by branch | **Strong patch primitives** and control grids | **Strong path editor**; brush/patch support varies by target game | **Strong** curve and mesh ecosystem | Bezier shape is experimental | Define a first-class path asset; retain patch surfaces only where a runtime consumer exists |
| Terrain/displacement | Manual brush/patch terrain techniques | **Strong:** Hammer displacements; Dota tile terrain and blend paint | Patch-based curved terrain; no integrated terrain rules | Brush terrain workflows; no comparable integrated rule-driven tile terrain | **Strong DCC sculpt/model/paint**, but outside a game-map build contract | General mesh editing, no mature terrain authoring model | Add a constrained height/terrain layer with deterministic tile integration and derived-data rebuilds |
| Prefab/instance reuse | **Partial/strong:** linked groups with protected properties | **Strong:** instances and Source 2 prefabs | Map fragments/selection workflows; no equally rich documented override model | Official FAQ acknowledges no traditional prefab system | **Strong:** linked duplicates, collection instances, linked libraries, library overrides | Relies mostly on Unity prefabs outside ProBuilder | Design external prefab assets, nested instances, overrides, dependency graph, and reference fix-ups |
| UV/material editing | Face texture projection and alignment | Material/mesh texturing documented; exact features vary by branch | Face and patch texture operations | UV lock, application modes, shader/decal preview | **Strong:** seams, unwrap, pins, islands, packing, pixel snapping | **Strong:** automatic/manual UV modes and UV2 generation | Separate surface material, primary UV, and lightmap UV; implement the minimum needed in stages |
| Terrain/material blend paint | No comparable multichannel paint layer | **Strong, branch-specific:** Dota supports vertex blend/color painting with up to four texture channels | No comparable integrated multichannel paint workflow | Shader preview, not equivalent terrain paint | **Strong generalized attribute/paint workflows** | Vertex color/material tools, but not a terrain-rule system | Add explicit finite blend channels, visualization, flood fill, and deterministic serialization |
| Decals | Usually game entities/material conventions; no first-class projector workflow in manual | Supported in Source content workflows; details vary by branch | Commonly entity/shader conventions | **Strong:** real-time decal preview | General decal workflows can be built, but not a map-specific projector contract | Unity components can be used; ProBuilder does not define the whole decal system | Add a dedicated decal/projector object with bounds, material, receiver mask, sort order, bake/runtime policy |
| Layers/visibility organization | **Strong:** groups, layers, hide/lock; linked groups | **Strong:** hierarchical VisGroups; objects may belong to multiple groups | Filters, regions, entity list, selection isolation | **Strong:** multiple VisGroups per object, independent visibility/grouping, colors and cleanup | **Strong:** collections, local view, visibility/selectability | Uses Unity hierarchy/scenes plus ProBuilder selection | Make collections/layers orthogonal to parentage, prefab membership, visibility, selection lock, and build inclusion |
| Path/waypoint authoring | Generic entities can encode paths; no dedicated path editor | Dota documents `path_corner` chains and gameplay paths | Entity chains are possible; no rich dedicated path workflow in reviewed manual | **Strong:** insert/delete/relink, closed and traversal modes, join/split/invert, entity conversion | **Strong general curves**, not game-specific semantics | Experimental Bezier shape; no complete gameplay path contract | Create one reusable path component consumed by roads, AI, cameras, audio, particles, and triggers |
| Compile/build iteration | Configurable compile profiles | **Strong:** build map, staged compilation, game launch; Dota docs allow skipping unchanged world compile | **Strong:** configurable build menu, fast/no-vis presets, region compile | **Strong:** non-blocking compile and stop | Export/build depends on game pipeline | Unity play/build loop, outside ProBuilder itself | Build a dependency graph with cancellable stages, region/selection builds, cached artifacts, and provenance |
| Debug diagnostics | Compile output and entity tooling; narrower visual diagnostics | Game-specific overlays and tool views | Region isolation, map statistics, entity list; engine/compiler diagnostics | **Strong:** problem checker, brush lookup, error-coordinate navigation, leak/portal/hull overlays | Mesh analysis tools exist across Blender | Repair operations available; diagnostics are narrower | Make diagnostics navigable and persistent: select offending object/element, explain invariant, offer explicit repair |
| Collaboration/version control | Text map formats are diffable but semantically noisy | External VMF/VMAP/prefab assets support file-level sharing | Text maps and modular map fragments | Rich map files and incremental saves; no reviewed live collaboration workflow | External libraries and overrides; binary `.blend` limits textual merges | Unity asset/prefab workflow; YAML behavior is outside ProBuilder docs | Prefer stable IDs, deterministic text/chunk serialization, small external assets, explicit conflicts; defer live co-editing |
| Geometry robustness | Convexity constraints simplify validity | Mature compile validation, but branch details vary | Grid discipline; docs warn about T-junction cracks and z-fighting | Problem checker, non-planar-face triangulation, exact picking and compiler navigation | Mature topology editing; users can still create non-manifold geometry | Many repair/edit commands; Boolean remains experimental | Enforce invariants at command boundaries; never silently “repair” authored data |

## 4. Editor-by-editor verified findings

### 4.1 Valve Hammer, Source 2 Hammer, and the Dota 2 tile editor

#### General Hammer and Source 2

**Verified facts**

- Valve documents Source 2 Hammer as having dedicated pages for mesh editing, mesh texturing, prefabs/instances, lighting, navigation, and visibility. The exact tool set is game-branch dependent. See the [Source 2 Hammer overview](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Hammer_Overview), [mesh editing part 1](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Basic_Construction/Mesh_Editing_1), [mesh texturing](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Basic_Construction/Mesh_Texturing), and [prefabs and instances](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Basic_Construction/Prefabs_and_Instances).
- Source Hammer 4 documentation describes displacement surfaces for connected, sculptable terrain; entity input/output connections; hierarchical VisGroups with multi-membership; external VMF instances; autosave; and paste-special behavior that can fix entity connections. See [Valve Hammer Editor](https://developer.valvesoftware.com/wiki/Valve_Hammer_Editor).
- Dota's Source 2 workflow creates a tile grid with `Shift+C`, exposes prefabs through the Assets pane, allows prefabs to be opened separately or collapsed into editable geometry, uses target names and entity I/O, supports waypoint chains, previews navigation, and builds with `F9`. See Valve's [Creating a Dota-Style Map](https://developer.valvesoftware.com/wiki/Dota_2_Workshop_Tools/Level_Design/Creating_A_Dota-Style_Map).

**Material lesson for Cypher**

The important pattern is the separation of four concerns: editable source objects, reusable external instances, game-semantic connections, and compiled artifacts. Cypher should preserve that separation. A prefab should not become an untracked pasted object graph merely because the build needs flattened data.

#### Dota tile-domain workflow

**Verified facts**

The official Dota 2 site documents a mode-based tile editor with three persistent UI regions: a mode toolbar, mode-dependent settings, and the active hotkeys. Its domain tools are unusually relevant to CypherTileEditor:

- The height mode can raise/lower, flatten or extend the sampled center height, add a relative height, or assign a tileset without changing height. Brush radius is retained per tool. Terrain moves in 128-unit height steps with a documented range of 15 levels above and below the base.
- Water paint creates/removes water and regularizes surrounding terrain. The documentation warns that multiple visible water planes may require multiple reflection renders.
- Path paint changes terrain to form roads and ramps, removes or restores obstructing trees, and stores path connectivity at tile edges. It distinguishes parallel paths from a single wide connected path.
- Tree and plant brushes expose density/type controls. Tree placement is aligned to the navigation grid and gives visual validity feedback for prohibited cells and adjacency rules.
- Object placement uses categories, subtypes, variations, a placement lattice, and transform/delete operations. Selection offers point, box, and lasso modes; type filters; additive/subtractive selection; variation cycling; tileset reassignment; and height changes.
- Copy/paste presents a placement preview before commit. Tiles can be disabled non-destructively to make room for custom geometry. “Collapse” exits the tile abstraction and produces directly editable content.
- A tile grid may use up to four active tilesets.

See [Dota 2 Tile Editor](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Tile_Editor.htm) and [Tile Editor Basics](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Tile_Editor_Basics.htm).

Tilesets themselves are reusable VMAP scenes organized into collections for ground, details, trees, plants, and props. Embedded metadata defines interaction with the tile system. Ground tiles carry boundary height nodes and path nodes. Proxy objects and proxy groups expose categories/values, preview information, and weighted/probabilistic selection; proxy silhouettes describe matching shapes such as corners and walls. See [New Tilesets](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/New_Tilesets.htm).

Dota's terrain painting supports scopes such as all objects, selected objects, and selected faces, includes visualization/debug views, exposes flood fill and blend/color modes, and blends up to four texture channels with adjustable brush radius and strength. See [Terrain Blending](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Terrain_Blending.htm). Valve later added blend painting directly on tiles without requiring collapse, as recorded in the official [Dota 2 Workshop Tools update](https://store.steampowered.com/news/14680/). A later official update added alternate tilesets, tree rotation/pitch controls, plant swapping, tile lookup, and high-contrast viewport lighting; see [Workshop Tools update 15306](https://store.steampowered.com/news/15306/).

Dota maps are built with `F9` or **File > Build Map**. The documented build emits a packaged VPK and can omit an unchanged world compilation stage during iteration. See [Compile and Run](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Compile_and_Run.htm).

**Cypher recommendation**

Adopt the domain ideas, not Valve's numeric constants or file structures:

- Store **semantic boundary data** for each tile: edge/corner height, path connectivity, water transition, collision/nav flags, and any gameplay socket.
- Resolve visual variants from explicit constraints and a stable seed. Make the resolver deterministic across platforms and builds.
- Keep decoration separate from functional collision, navigation, trigger, spawn, and traversal data.
- Provide validity overlays before placement, and make preview and commit run the same resolver.
- Treat “collapse to geometry” as an escape hatch. Preserve source provenance and make the transition explicit. Prefer a reversible baked view where feasible.
- Rebuild only dirty tile neighborhoods and the derived systems that depend on them.

### 4.2 GtkRadiant / Q3Radiant

**Verified facts**

- The official GtkRadiant project links the classic Q3Radiant manual and currently lists GtkRadiant 1.6.7 downloads. See the [GtkRadiant documentation hub](https://icculus.org/projects/gtkradiant/documentation.html) and [downloads](https://icculus.org/gtkradiant/downloads.html).
- Radiant supports Bezier patch primitives including cylinders, denser cylinders/torus forms, square cylinders, end caps, bevels, cones, and arbitrary simple patch grids. The manual documents inserting/deleting rows or columns, inverting, redistributing, capping, and snapping control points. See [Curve and patch construction](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch06/pg6_1.htm).
- A **region** can be defined from viewport bounds, a brush volume, or selected objects. It hides the rest of the map and can be compiled independently. See [The View Menus and region controls](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch04/pg4_1.htm).
- Preferences are game-specific and include patch subdivision and filter behavior. The build menu and command lines are configurable. See [Preferences](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch01/pg1_2.htm).
- The texture workflow includes natural patch mapping and fit/shift/rotate/scale operations. See [Texturing](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch07/pg7_1.htm).
- Filters can hide categories such as detail, curves, and caulk. The editor provides map statistics and an entity list, and can save selected or region content. See [Miscellaneous menus](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch10/pg10_1.htm).
- Build presets include fast/no-visibility variants. See [Build menu](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch11/pg11_1.htm).
- The manual explicitly warns about patch/brush T-junction cracks and z-fighting, and recommends grid alignment and hidden-face materials such as caulk. See [Hints, tips, and troubleshooting](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch12/pg12_1.htm).

**Material lesson for Cypher**

Radiant's highest-value patterns are small and practical:

- A **region is both a viewing scope and a build scope**.
- Game profiles own compiler commands and content rules; editor geometry is not tied to one executable.
- Filters describe semantic categories rather than forcing users to reorganize the scene hierarchy.
- Patch tools expose topology operations at the level of rows, columns, caps, and control grids.

**Cypher recommendation**

Add region/selection builds early. They produce more iteration benefit than broad modeling parity. Treat build filters, visibility filters, selection sets, and scene hierarchy as separate concepts even if the first UI presents them together.

### 4.3 J.A.C.K.

**Verified facts**

The official J.A.C.K. site and manual document several mature quality-of-life and debugging features:

- 2D reference images support offset, scale, luminance, filtering, inversion, and persistence in the richer map format.
- Drag selection works in 3D. SmartEdit provides key previews and tooltips. Model display supports animation/body/skin inspection and reload.
- Incremental save can create versioned filenames. Autosave and map-problem checks are built in.
- Objects may belong to multiple VisGroups. Visibility organization is independent of normal grouping/hiding; VisGroups can be color coded, merged, marked, or purged.
- Plugin interfaces extend primitives, formats, and game configurations. Resources can be loaded on demand.
- The viewport previews texture effects, decals, dynamic skies, and Quake III shaders; a shader editor is included.
- A dedicated path editor can create nodes, insert/delete nodes while relinking, define one-way/circular/ping-pong traversal, copy/paste, join, split, invert, and convert a path to an entity chain. That conversion is documented as one-way.
- Texture behavior includes UV lock and application modes that can apply material, values, and axes together.
- Compile processes can run without blocking editing and can be stopped.
- Snap-to-grid can cycle through eight bounding-box anchors and can use a selected vertex as the snap anchor.
- Clone/paste can preserve or repair internal target links. Paste Special creates repeated copies with accumulated transforms and internal connections, which is especially useful for repeated stairs and similar structures.
- The map checker and navigation tools can jump to an entity/brush number or compiler-reported coordinates. Point/leak files and portal/hull overlays provide spatial diagnostics.
- Non-planar faces produced by vertex manipulation can be triangulated to avoid invalid-solid errors. Exact model picking is available as an alternative to bounding-box picking.

See the official [J.A.C.K. feature list](https://jack.hlfx.ru/en/features.html), [support page](https://jack.hlfx.ru/en/support.html), and [VDK/J.A.C.K. manual PDF](https://jack.hlfx.ru/pub/VDKManual.pdf). The official FAQ notes the absence of some lower-priority functions, including a traditional prefab system; see [J.A.C.K. FAQ](https://jack.hlfx.ru/en/articles/1/faq.html).

**Material lesson for Cypher**

J.A.C.K. demonstrates that editor maturity is often visible in error recovery and repetitive work rather than primitive count. A path tool that maintains links, a paste operation that understands internal references, and a compiler message that selects the exact offending element all remove common sources of map corruption.

**Cypher recommendation**

Prioritize reference-aware duplication, problem navigation, point/portal/collision overlays, and cancellable builds. Implement accumulated-transform duplication as a generic command, then use it for stairs, radial arrays, repeated lights, and tile decorations.

### 4.4 Blender mesh, UV, instances, and assets

Blender is a DCC application, so its complete feature surface would overwhelm a focused game-map editor. The useful reference is its editing semantics, especially where those semantics prevent users from rebuilding selections or destroying reusable data.

**Verified facts**

- Similar selection can compare normals, topology degree, edge length/direction, face angle, seam/sharp/material state, area, perimeter, or side count; thresholds and comparison modes are exposed. See [Select Similar](https://docs.blender.org/manual/en/latest/modeling/meshes/selecting/similar.html).
- Linked and shortest-path selections operate over connectivity and can respect delimiters. See [Select Linked](https://docs.blender.org/manual/en/latest/modeling/meshes/selecting/linked.html). The broader selection system includes grow/shrink, loops/rings, boundaries, random/checker patterns, and trait-based selection; see [Mesh selection introduction](https://docs.blender.org/manual/en/latest/modeling/meshes/selecting/introduction.html).
- Loop Cut gives modal preview, multiple cuts, and sliding while maintaining UV behavior. See [Loop Cut and Slide](https://docs.blender.org/manual/en/latest/modeling/meshes/tools/loop.html).
- Bisect uses a user-defined plane and can fill or clear either side with a configurable threshold. See [Bisect](https://docs.blender.org/manual/en/5.1/modeling/meshes/editing/mesh/bisect.html).
- Bridge Edge Loops supports multiple loops, unequal vertex counts, twist, intermediate cuts, interpolation, and profile control. See [Bridge Edge Loops](https://docs.blender.org/manual/en/5.2/modeling/meshes/editing/edge/bridge_edge_loops.html).
- Dissolve can preserve boundaries such as material, seam, sharp, and UV distinctions and can interpolate attributes. See [Delete and Dissolve](https://docs.blender.org/manual/en/5.2/modeling/meshes/editing/mesh/delete.html).
- Snapping distinguishes grid increments from relative increments; exposes vertex, edge, and face targets; can align rotation; can project individual elements; filters targets; and can average multiple targets. See [Snapping](https://docs.blender.org/manual/en/3.6/editors/3dview/controls/snapping.html).
- Proportional editing exposes adjustable radius, falloff curves, connectivity constraints, and view-projected influence. See [Proportional Editing](https://docs.blender.org/manual/en/3.3/editors/3dview/controls/proportional_editing.html).
- UV seams guide unwrap. UV workflows support pinning/live unwrap, island packing, pixel snapping, and image-bound constraints. See [UV seams](https://docs.blender.org/manual/en/latest/modeling/meshes/uv/unwrapping/seams.html) and [UV editing](https://docs.blender.org/manual/en/2.82/modeling/meshes/editing/uv/editing.html).
- Linked duplicates share mesh data while retaining independent transforms. Collection instances can instance nested collections and externally linked data and can be converted to real objects. See [Linked Duplicates](https://docs.blender.org/manual/en/5.0/scene_layout/object/editing/duplicate_linked.html) and [Collection Instancing](https://docs.blender.org/manual/en/3.6/scene_layout/object/properties/instancing/collection.html).
- Library overrides allow local property changes while unmodified values continue to resynchronize from the source. Blender supports multiple independent overrides and nested/chained overrides, with documented resync behavior and limitations. See [Library Overrides](https://docs.blender.org/manual/en/5.1/files/linked_libraries/library_overrides.html).
- Modifiers form an ordered, non-destructive stack with separate viewport/render controls and an explicit apply operation. See [Modifiers introduction](https://docs.blender.org/manual/en/latest/modeling/modifiers/introduction.html).
- Asset libraries index reusable data blocks and organize them with catalogs. See [Asset Libraries](https://docs.blender.org/manual/en/5.2/files/asset_libraries/introduction.html).

**Material lesson for Cypher**

Blender's most transferable ideas are:

1. Selection is its own subsystem with topology, geometric, semantic, and attribute queries.
2. Commands preserve or explicitly cross attribute boundaries.
3. Instance overrides distinguish inherited data from local changes.
4. Non-destructive authoring is useful when the resulting dependency graph remains visible and predictable.

**Cypher recommendation**

Implement a small, explicit subset:

- element modes for vertex/edge/face/object;
- connected, loop/ring, boundary, material, normal-angle, and grow/shrink selection;
- cut, bridge, weld, dissolve, triangulate, bevel, inset, and controlled extrusion;
- seam marking and a separate UV selection state;
- instance override recording at field/path granularity;
- an explicit **bake/apply** boundary for parametric or procedural data.

Do not copy Blender's full modifier stack, sculpt system, Geometry Nodes, or workspace complexity into Mason. Mason should hand off specialized content to a DCC tool when that content does not need engine-aware editing.

### 4.5 Unity ProBuilder

**Verified facts**

- ProBuilder's Shape tool creates editable parametric forms inside a bounding box and supports arch, cone, cube, cylinder, door, pipe, plane, prism, sphere, sprite, stairs, and torus shapes. A shape can be re-entered for parameter editing until topology edits require it to be reset/recreated as a parametric shape. See [Shape tool](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/shape-tool.html).
- Mesh editing includes fill holes, split/collapse, merge/detach, a modal Cut tool, polygon editing, extrusion, and normal operations. See [Editing objects](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/workflow-edit.html).
- The geometry menu documents bevel, bridge, collapse, conform normals, delete/detach/duplicate/extrude, fill hole, flip edge/normals, insert loop, merge, connect, subdivide, split, triangulate, and weld. See [Geometry actions](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/menu-geometry.html).
- Selection supports rectangle containment/intersection behavior, hidden-element policy, global/local/normal orientation, and selection by loops, rings, material, color, growth/shrinkage, and holes. See [Selection tools](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/selection-tools.html).
- UV editing permits automatic and manual face modes on one mesh. Automatic UVs react to geometry changes; manual modes expose box and planar projections. Lightmap UVs can be regenerated separately. See [UV Editor](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/uv-editor.html).
- Object-level commands include merge, mirror, conversion to ProBuilder geometry, collider assignment, trigger assignment, subdivision, and triangulation. See [Object actions](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/menu-object.html).
- Boolean operations and the Bezier shape are documented as experimental. See [Experimental features](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/experimental.html).

**Material lesson for Cypher**

ProBuilder offers a credible “good enough inside the engine” boundary: parametric graybox shapes, a compact mesh tool set, automatic/manual UV modes, and direct collider/trigger designation. Its experimental Boolean label is also a useful warning: arbitrary CSG requires careful numerical and topological design and should not be accepted as robust merely because a UI command exists.

**Cypher recommendation**

Use retained parametric primitives for stairs, doors, arches, ramps, pipes, and cylinders. Show clearly when an operation will bake a primitive into an editable mesh. Keep collision/trigger roles as components or build flags that can be assigned from geometry tools without merging rendering and physics data models.

## 5. Capabilities materially absent from TrenchBroom's normal model

These are the strongest opportunities for Cypher. Some can be represented indirectly in TrenchBroom with entities, groups, or game-specific conventions, but they are not first-class, integrated workflows in the reviewed public manual.

### 5.1 Semantic tile resolution

A generic editor can place a square mesh on a grid. A tile editor must answer a different question: **which compatible authored variant satisfies the local gameplay constraints?** Required data includes:

- edge and corner height signatures;
- path sockets, width/class, direction, and intersection shape;
- water presence, level, shoreline/corner transitions, and flow metadata;
- wall/door/fence/traversal sockets;
- collision, navigation, occlusion, and gameplay tags;
- biome/tileset categories and compatibility tags;
- deterministic variant weights and a stable random seed;
- dependency radius so an edit recomputes only affected neighbors;
- an explanation when no candidate matches.

The resolver should return a plan before mutating the map. Preview, commit, undo, load-time validation, and headless build should call the same deterministic core.

### 5.2 Mesh topology and topology-aware selection

Brush faces are not enough for authored props, irregular transitions, portals, terrain skirts, or detailed graybox forms. Mason needs a mesh representation with stable vertex/edge/face handles and explicit adjacency. The first useful command set is smaller than Blender's:

- extrude face/edge;
- inset face;
- bevel selected edges with bounded segments;
- loop cut/slide;
- knife/bisect;
- bridge compatible boundaries;
- weld by explicit selection or tolerance;
- dissolve while respecting material/UV/sharp boundaries;
- fill boundary/hole;
- triangulate with visible policy;
- recalculate/flip orientation;
- select connected, boundary, loop/ring, material, normal angle, grow/shrink.

Each operation must define what happens to materials, smoothing, UVs, vertex color/blend channels, collision annotations, and stable IDs.

### 5.3 Terrain/displacement and multichannel paint

Manual vertex clumping or patch bending is useful but does not provide:

- a height/displacement data contract;
- flatten, terrace, smooth, ramp, erosion-like, hole, and restore operations;
- controlled border stitching between chunks or tiles;
- material blend weights and normalization;
- independent paint scopes and diagnostic visualization;
- collision/nav/visibility invalidation;
- water/shoreline integration;
- deterministic import/export and LOD generation.

Cypher should begin with a constrained heightfield or tile-height layer. Arbitrary sculpt meshes can remain a DCC responsibility until gameplay demonstrates a need.

### 5.4 First-class paths and splines

A path should be a typed object, not a naming convention among unrelated point entities. Minimum source data:

- stable path and node IDs;
- ordered nodes with position and optional tangent/up/width;
- open/closed topology;
- linear, smooth, or custom interpolation per segment;
- traversal direction and tags;
- event markers and named sockets;
- optional lane or branch information;
- reference-safe insert/delete/reorder/split/join/reverse commands.

Consumers should derive data without owning the authoring path: AI patrols, roads, camera rails, moving platforms, sound emitters, particle trails, trigger volumes, and editor guides.

### 5.5 Prefab assets, instances, overrides, and reference repair

TrenchBroom linked groups are a useful precedent, but a full engine pipeline needs:

- external prefab files with stable asset IDs and schema versions;
- stable local object IDs inside the prefab;
- nested instances with cycle detection and a maximum expansion depth;
- per-instance transforms and an explicit set of overridable fields;
- recorded overrides as patches, with orphan/conflict diagnostics after source changes;
- target/reference fix-up rules for duplicate, paste, instantiate, and flatten;
- dependency tracking, source revision/hash, and deterministic build expansion;
- unpack/bake with a provenance record;
- thumbnails/previews generated from the same source revision.

Blender library overrides show the value and complexity of resynchronization. Cypher should initially allow only a curated set of overrides rather than arbitrary structural edits inside an instance.

### 5.6 UV, lightmap, smoothing, color, and blend attributes

Mason does not need a full DCC UV suite on day one. It does need an explicit attribute model:

- surface material slots;
- primary UV coordinates and projection metadata;
- an independent lightmap UV set;
- smoothing/hard-edge state;
- vertex color or finite blend channels;
- seam/pin flags if local unwrap is supported;
- validation for finite coordinates, overlap policy, padding, and texel density.

Start with planar/box/cylindrical projection, face fit/alignment, UV lock, transform, and automatic lightmap generation. Add seam unwrap and island packing only when artists need to finish production meshes inside Mason.

### 5.7 Decal/projector authoring

A decal object should store at least:

- material/asset reference;
- projector transform and finite bounds;
- receiver masks/tags;
- projection direction and depth tolerance;
- fade distance/angle and sort/layer priority;
- static-bake versus runtime policy;
- preview clipping and overdraw diagnostics.

This keeps decals distinct from ordinary coplanar geometry, which otherwise invites z-fighting.

### 5.8 Build, debug, and data provenance

The build graph should make each stage visible and cancellable:

1. validate source;
2. resolve tiles and prefabs;
3. bake geometry and attributes;
4. cook collision;
5. build navigation;
6. build visibility/occlusion;
7. bake lighting where applicable;
8. package assets;
9. launch or hot-reload the selected target.

Each artifact should record input hashes, tool/schema versions, target platform, and diagnostic provenance. Region/selection builds should operate on a well-defined dependency closure rather than simply excluding everything outside a box.

Diagnostics should be clickable and preserve:

- source asset and object ID;
- mesh element or tile coordinate;
- build stage and diagnostic code;
- severity;
- explanation and suggested explicit repairs;
- whether the displayed artifact is stale.

### 5.9 Collaboration and source control

None of the reviewed primary documentation establishes synchronous multi-user co-editing as the essential model. External prefabs, linked assets, VisGroups, and libraries support modular work, but they do not solve concurrent semantic merging.

**Cypher recommendation:** optimize for version control before live collaboration:

- deterministic serialization and ordering;
- stable GUIDs independent of array order;
- one asset or chunk per meaningful ownership boundary;
- external prefabs and tilesets;
- per-user viewport, selection, and panel state outside shared map data;
- human-readable diffs for metadata and a canonical diff tool for geometry;
- explicit conflict records rather than last-writer-wins repair;
- transactional save and autosave recovery;
- optional ownership hints/locks in team tooling, without making the file unreadable offline.

Defer live co-editing until file-level collaboration, undo/redo, and deterministic builds are reliable.

## 6. Geometry robustness contract

A professional editor should treat every modeling command as a transaction:

```text
user intent
  -> capture immutable command inputs
  -> compute preview/result in scratch storage
  -> validate topology, geometry, and attributes
  -> display warnings or typed failure
  -> atomically commit one undoable command
  -> invalidate only dependent derived data
```

### 6.1 Core numerical policy

- Use double precision for authoring calculations even if runtime vertices are quantized to floats.
- Centralize configurable tolerances for position equality, coplanarity, collinearity, area, and normal comparison. Never scatter literal epsilons among tools.
- Define world bounds and safe coordinate ranges.
- Make snap policy explicit: absolute grid, relative increment, element target, surface target, or authored socket.
- Quantize only at documented boundaries; repeated edit/save cycles must not drift.

### 6.2 Required validation

Before a command commits, check the invariants relevant to that object type:

- finite coordinates and attributes;
- valid handles and adjacency;
- duplicate or near-duplicate vertices/edges;
- zero-length edges and zero-area faces;
- face vertex count and winding;
- planarity where the representation requires it;
- non-manifold edges/vertices where closed-manifold output is required;
- open boundaries where watertight output is required;
- self-intersection and inverted volume;
- convexity for brush solids;
- minimum thickness and build-tool limits;
- consistent material, smoothing, UV, and blend-channel arrays;
- prefab cycles, missing targets, and orphan overrides;
- tile socket compatibility and unsatisfied resolver constraints;
- collision-cook and navigation-build acceptance.

A failed operation should return a typed error and leave source state unchanged. Automatic repair must be an explicit command with a preview; silent repair destroys author intent and makes version-control changes hard to explain.

### 6.3 Verification strategy

- Property tests for topology commands: valid input either yields a valid result or fails without mutation.
- Fuzz tests using degenerate, huge, tiny, coplanar, near-coplanar, duplicate, and self-intersecting inputs.
- Undo/redo round-trip tests that compare canonical serialized state.
- Determinism tests across repeated runs and supported platforms.
- Golden maps for compiler diagnostics, prefab update/override conflicts, tile-rule resolution, UV propagation, and region builds.
- Differential tests between preview and commit to ensure they use the same algorithm.
- Load/save/load canonicalization tests across schema versions.
- Performance benchmarks with realistic scene distributions, not only synthetic maximum polygons.

## 7. Staged Cypher adoption plan

### Stage A — CypherTileEditor foundations

Adopt first:

1. **Tile schema and resolver** with edge/corner sockets, stable IDs, constraints, deterministic weighted variants, and an explanation trace.
2. **Mode-based brushes** for height, terrain/material, path, water, vegetation, objects, selection, and erase/restore. Retain radius/strength/settings per mode.
3. **Validity overlays** for path connectivity, height mismatches, navigation, blocked placement, water boundaries, collision, and unresolved tiles.
4. **Decoration versus function separation.** Decorative variations must never silently change collision, navigation, triggers, spawns, or visibility.
5. **Tileset assets** with categories/tags, previews, nested proxies, explicit probabilities, cycle/depth checks, and four-or-more configurable palette slots rather than baking Valve's limit into the format.
6. **Preview-before-commit copy/paste** and deterministic multi-placement.
7. **Incremental dirty-neighborhood rebuilds** for tiles plus downstream collision/navigation/visibility artifacts.
8. **Disable and bake/collapse commands** with provenance. Keep disable non-destructive; make bake explicit and undoable.
9. **Headless validation and build** so maps are testable without opening the editor.

### Stage B — shared scene and prefab infrastructure

1. External prefab assets, nested instances, stable local IDs, curated overrides, reference fix-up, and dependency tracking.
2. Collections/layers with multi-membership, visibility, selection lock, color, build inclusion, and semantic filters independent of scene parentage.
3. Reference-aware duplicate/paste and accumulated-transform arrays.
4. Region/selection build with dependency closure, cancellable stages, artifact cache, and clickable diagnostics.
5. First-class path objects and a dedicated decal/projector object.
6. Autosave/recovery and deterministic version-control serialization.

### Stage C — Mason geometry core

1. Retained parametric primitives for stairs, arches, doors/openings, ramps, pipes, cylinders, and common collision shapes.
2. A stable, manifold-aware mesh representation with explicit adjacency and element IDs.
3. Topology-aware selection and the compact operation set listed in section 5.2.
4. Attribute propagation rules for every geometry command.
5. Primary projection UVs, UV lock, separate lightmap UVs, smoothing/hard edges, and finite blend/color channels.
6. Validation/repair UI with precise element navigation.
7. Terrain/heightfield chunks only after their runtime collision, navigation, streaming, and rendering contracts are defined.

### Stage D — later production features

Add when a shipped gameplay need justifies them:

- seam-based unwrap and island packing inside Mason;
- richer terrain erosion/sculpt operations;
- spline deformation and road-mesh generation;
- non-destructive geometry operators with a small visible dependency stack;
- team ownership/locking services;
- advanced decal baking and atlas tooling;
- plugin APIs for importers, validators, compiler stages, and game profiles.

## 8. Explicit rejection and deferral decisions

### Reject

- **Blind feature parity.** A map editor should not absorb Blender's whole DCC surface or every historical Hammer/Radiant command.
- **Destructive collapse as the routine workflow.** Baking is necessary, but the default source should retain tile, primitive, path, and prefab semantics.
- **Unseeded random variants.** They make diffs, debugging, replay, multiplayer verification, and reproducible builds unreliable.
- **Visual tiles as authoritative gameplay data.** Collision, navigation, triggers, traversal, and spawn semantics need explicit data even when generated from a tile rule.
- **Silent geometry repair.** It conceals corruption and creates unexplained output changes.
- **Hidden modal behavior.** Active mode, modifiers, snap target, selection filter, and affected scope must remain visible.
- **A single scene-tree concept for parentage, organization, visibility, build inclusion, and ownership.** These relationships overlap but are not identical.
- **Arbitrary Boolean CSG as the foundation of all modeling.** Preserve robust convex brush operations and add tested mesh commands incrementally.

### Defer

- a Blender-scale modifier stack or procedural geometry-node system;
- freeform sculpting and retopology;
- a complete DCC UV/texture-paint suite;
- arbitrary nested overrides that can structurally rewrite prefab internals;
- synchronous multi-user scene editing;
- generalized simulation editors before runtime consumers exist.

### Preserve from TrenchBroom

- fast keyboard-led brush construction;
- strong grid discipline and predictable snapping;
- convex solids as a robust graybox representation;
- linked editing where it remains understandable;
- game-specific entity definitions and compilation profiles;
- simple text-oriented authoring where practical;
- a small number of visible modes rather than pervasive hidden state.

## 9. Concrete acceptance criteria

A first professional CypherTileEditor milestone should be considered complete when:

- the same map and seed produce byte-identical canonical source and derived tile choices on all supported developer machines;
- a height/path/water edit recomputes only the declared dependency neighborhood;
- every invalid placement explains the violated constraint and points to the responsible tile edge/corner/object;
- decorative variation cannot alter functional gameplay data without an explicit authored override;
- prefab duplication and instantiation preserve internal references while generating collision-free instance/object IDs;
- save/load/undo/redo preserve canonical state;
- a headless validator catches missing assets, resolver failures, prefab cycles, broken references, invalid geometry, and stale build artifacts;
- selection/region builds produce the same results as the corresponding subset of a clean full build;
- editor previews identify stale navigation, collision, visibility, and lighting data;
- bake/collapse is explicit, undoable in-session, and records its source asset/revision.

A first Mason mesh milestone should be considered complete when:

- topology commands either produce a valid result or fail without mutating the document;
- material, UV, smoothing, blend/color, and stable-ID propagation rules are documented and tested for every command;
- connected, boundary, loop/ring, material, normal-angle, and grow/shrink selection work on realistic meshes;
- collider/trigger assignment remains a separate semantic layer over render geometry;
- compiler diagnostics select the exact object and, when available, element that caused the problem;
- export is deterministic and records tool/schema versions.

## 10. Primary-source index

### TrenchBroom baseline

- [TrenchBroom manual](https://trenchbroom.github.io/manual/latest/)
- [TrenchBroom documentation source](https://github.com/TrenchBroom/TrenchBroom/tree/master/app/TrenchBroom/resources/documentation)

### Valve

- [Valve Hammer Editor](https://developer.valvesoftware.com/wiki/Valve_Hammer_Editor)
- [Source 2 Hammer overview](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Hammer_Overview)
- [Source 2 mesh editing part 1](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Basic_Construction/Mesh_Editing_1)
- [Source 2 mesh texturing](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Basic_Construction/Mesh_Texturing)
- [Source 2 prefabs and instances](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Basic_Construction/Prefabs_and_Instances)
- [Creating a Dota-Style Map](https://developer.valvesoftware.com/wiki/Dota_2_Workshop_Tools/Level_Design/Creating_A_Dota-Style_Map)
- [Dota 2 Tile Editor](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Tile_Editor.htm)
- [Tile Editor Basics](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Tile_Editor_Basics.htm)
- [New Tilesets](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/New_Tilesets.htm)
- [Terrain Blending](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Terrain_Blending.htm)
- [Compile and Run](https://www.dota2.com.cn/wiki/Dota_2_Workshop_Tools/Level_Design/Compile_and_Run.htm)
- [Valve Workshop Tools update 14680](https://store.steampowered.com/news/14680/)
- [Valve Workshop Tools update 15306](https://store.steampowered.com/news/15306/)

### GtkRadiant / Q3Radiant

- [GtkRadiant documentation hub](https://icculus.org/projects/gtkradiant/documentation.html)
- [GtkRadiant downloads](https://icculus.org/gtkradiant/downloads.html)
- [Q3Radiant manual](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/index.htm)
- [Curve and patch construction](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch06/pg6_1.htm)
- [Region controls](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch04/pg4_1.htm)
- [Preferences](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch01/pg1_2.htm)
- [Texturing](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch07/pg7_1.htm)
- [Miscellaneous menus](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch10/pg10_1.htm)
- [Build menu](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch11/pg11_1.htm)
- [Hints and troubleshooting](https://www.icculus.org/gtkradiant/documentation/q3radiant_manual/ch12/pg12_1.htm)

### J.A.C.K.

- [Official features](https://jack.hlfx.ru/en/features.html)
- [Official support](https://jack.hlfx.ru/en/support.html)
- [Official VDK/J.A.C.K. manual PDF](https://jack.hlfx.ru/pub/VDKManual.pdf)
- [Official FAQ](https://jack.hlfx.ru/en/articles/1/faq.html)

### Blender

- [Select Similar](https://docs.blender.org/manual/en/latest/modeling/meshes/selecting/similar.html)
- [Select Linked](https://docs.blender.org/manual/en/latest/modeling/meshes/selecting/linked.html)
- [Mesh selection introduction](https://docs.blender.org/manual/en/latest/modeling/meshes/selecting/introduction.html)
- [Loop Cut and Slide](https://docs.blender.org/manual/en/latest/modeling/meshes/tools/loop.html)
- [Bisect](https://docs.blender.org/manual/en/5.1/modeling/meshes/editing/mesh/bisect.html)
- [Bridge Edge Loops](https://docs.blender.org/manual/en/5.2/modeling/meshes/editing/edge/bridge_edge_loops.html)
- [Delete and Dissolve](https://docs.blender.org/manual/en/5.2/modeling/meshes/editing/mesh/delete.html)
- [Snapping](https://docs.blender.org/manual/en/3.6/editors/3dview/controls/snapping.html)
- [Proportional Editing](https://docs.blender.org/manual/en/3.3/editors/3dview/controls/proportional_editing.html)
- [UV seams](https://docs.blender.org/manual/en/latest/modeling/meshes/uv/unwrapping/seams.html)
- [UV editing](https://docs.blender.org/manual/en/2.82/modeling/meshes/editing/uv/editing.html)
- [Linked Duplicates](https://docs.blender.org/manual/en/5.0/scene_layout/object/editing/duplicate_linked.html)
- [Collection Instancing](https://docs.blender.org/manual/en/3.6/scene_layout/object/properties/instancing/collection.html)
- [Library Overrides](https://docs.blender.org/manual/en/5.1/files/linked_libraries/library_overrides.html)
- [Modifiers introduction](https://docs.blender.org/manual/en/latest/modeling/modifiers/introduction.html)
- [Asset Libraries](https://docs.blender.org/manual/en/5.2/files/asset_libraries/introduction.html)

### Unity ProBuilder

- [ProBuilder manual 5.0.7](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/index.html)
- [Shape tool](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/shape-tool.html)
- [Editing objects](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/workflow-edit.html)
- [Geometry actions](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/menu-geometry.html)
- [Selection tools](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/selection-tools.html)
- [UV Editor](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/uv-editor.html)
- [Object actions](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/menu-object.html)
- [Experimental features](https://docs.unity3d.com/Packages/com.unity.probuilder@5.0/manual/experimental.html)
