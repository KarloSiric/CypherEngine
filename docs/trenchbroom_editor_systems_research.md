<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/trenchbroom_editor_systems_research.md
//  Purpose: Records the source-level TrenchBroom editor audit used by the
//           CypherTileEditor and the future Mason world editor.
//  Details: This source-derived research inventories editor behavior,
//           interaction, geometry, document architecture, reliability mechanisms,
//           and implementation gates. It is not a clean-room specification and
//           does not define final Cypher implementation or UI styling.
//
//  History:
//  - Created by Karlo Siric on 2026-09-17
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# TrenchBroom Editor Systems Research for TileEditor and Mason

Research snapshot: 2026-09-17.

Reference revision: TrenchBroom commit
[`e53a0ef172e10e62ff24b86b70ca3b6ea865cac4`](https://github.com/TrenchBroom/TrenchBroom/commit/e53a0ef172e10e62ff24b86b70ca3b6ea865cac4),
audited with the official
[TrenchBroom 2026.2 reference manual](https://trenchbroom.github.io/manual/latest/).

## Document status

This is the engineering reference for the editor capabilities Cypher should
study, the algorithms it must implement independently, and the order in which
those capabilities can enter CypherTileEditor and Mason. Within map authoring,
it covers the editing, command/UI, geometry, selection, picking, visibility,
diagnostic, asset-loading, compilation, and recovery behavior that materially
affects the two Cypher editors.

This document is deliberately more detailed than a feature wish list. A menu
entry is useful only when its data ownership, transaction behavior, failure
rules, persistence, validation, and test obligations are known. Those contracts
are recorded here for each major capability.

The audit found that the current upstream tree is larger than the approximate
150,000 lines assumed when this work began. Under `app/` and `lib/`, it contains
about 362,617 C/C++ header and implementation lines: 247,419 production lines
and 115,198 test lines. The official manual contains 3,083 lines and roughly
41,700 words. The test tree contains 325 C/C++ files, 384 top-level test cases,
4,907 sections, and more than 15,000 assertion sites when project assertion
helpers are included. Counts are included
to define the evidence boundary, not to equate line count with design quality.

Two exhaustive appendices preserve the source details: the geometry audit covers
99 implementation/test files and 310 checked immutable source references; the
UI audit indexes 517 relevant files and 84,368 lines and checks 454 file/line
references. The external comparison adds 53 unique official or primary links,
all of which resolved when this snapshot was prepared.

## How to use this document

Use the capability matrix to choose a vertical slice. Then follow the relevant
design section through all of its gates:

1. define the authored data and stable identity;
2. define the editor command and cancellation behavior;
3. define deterministic persistence and migration;
4. define validation and diagnostics;
5. define picking and visualization;
6. define derived render, collision, visibility, navigation, and runtime data;
7. add focused unit, property, regression, and end-to-end tests;
8. expose the action in menus, toolbars, context menus, and rebindable shortcuts.

A feature does not count as implemented because a button exists or because one
viewport can draw a preview. It counts when the document survives undo, save,
reload, compilation, runtime loading, malformed input, and cancellation without
losing identity or violating invariants.

## Table of contents

- [Conclusions](#conclusions)
- [Evidence and legal boundary](#evidence-and-legal-boundary)
- [Source architecture map](#source-architecture-map)
- [The editor data model](#the-editor-data-model)
- [Actions, tools, gestures, and transactions](#actions-tools-gestures-and-transactions)
- [Viewport navigation and picking](#viewport-navigation-and-picking)
- [Selection semantics](#selection-semantics)
- [Grid, snapping, and coordinate policy](#grid-snapping-and-coordinate-policy)
- [Object creation and transformation](#object-creation-and-transformation)
- [Brush and polygon mathematics](#brush-and-polygon-mathematics)
- [Geometry tool algorithms](#geometry-tool-algorithms)
- [Materials and UV editing](#materials-and-uv-editing)
- [Entities and typed properties](#entities-and-typed-properties)
- [Groups, linked groups, and layers](#groups-linked-groups-and-layers)
- [Visibility, filtering, hiding, isolation, and locking](#visibility-filtering-hiding-isolation-and-locking)
- [Undo, redo, repetition, and history](#undo-redo-repetition-and-history)
- [Clipboard, files, export, and recovery](#clipboard-files-export-and-recovery)
- [Issues, validation, and quick fixes](#issues-validation-and-quick-fixes)
- [Assets, game definitions, and configuration](#assets-game-definitions-and-configuration)
- [Build, compile, run, and debug workflow](#build-compile-run-and-debug-workflow)
- [Rendering, caching, and large-map performance](#rendering-caching-and-large-map-performance)
- [Testing and robustness requirements](#testing-and-robustness-requirements)
- [Complete command and control inventory](#complete-command-and-control-inventory)
- [Lessons from other editors](#lessons-from-other-editors)
- [Cypher capability map](#cypher-capability-map)
- [TileEditor implementation program](#tileeditor-implementation-program)
- [Mason implementation program](#mason-implementation-program)
- [Engine subsystem contracts](#engine-subsystem-contracts)
- [Acceptance gates](#acceptance-gates)
- [Source index](#source-index)

## Conclusions

### The important part is the editing kernel

TrenchBroom's visible tools depend on a coherent editing kernel:

```text
platform input
    -> contextual action or ordered tool controller
    -> pick request and filtered hit set
    -> gesture tracker with preview state
    -> typed document operation
    -> nested transaction / command history
    -> model notifications
    -> spatial, search, link, tag, issue, and render-cache updates
    -> all open views and inspectors refresh from document state
```

Cypher should copy this separation of responsibilities in its own architecture.
It should not copy TrenchBroom classes, source text, serialized property names,
or UI assets.

### TileEditor and Mason need different representations

CypherTileEditor is currently a bounded, dense, row-major cell document. It is
already useful for authored floors, stairs, doors, a player spawn, material-slot
bindings, deterministic geometry generation, validation, grouped edit history,
and synchronized 2D/3D selection. That representation should remain simple and
fast for tile construction.

Mason needs stable object and topology identities, arbitrary transforms,
hierarchy, instances, entities, component data, mesh or convex-solid editing,
collision, visibility, lighting, audio, triggers, and build products. Adding all
of that directly to `tile_map_cell_t` would turn a reliable tile document into a
poor general scene graph. Shared editor services should therefore sit above two
document adapters:

```text
CypherEditorCore
  actions | input contexts | selection sets | command history | tools
  snapping | picking | diagnostics | asset references | build/run | workspace
                         /                         \
             Tile document adapter          Mason world adapter
             cell/edge/marker IDs            object/entity/topology IDs
             tile geometry builder           world compiler and cookers
```

### Geometry command integrity and multi-target policy

Convex editing is full of invalid intermediate results: a moved vertex can
collapse an edge; clipping can remove the whole solid; subtraction can create
fragments; a scale can cross zero; linked copies can conflict; and floating
point error can create nearly duplicate planes. Every target result must be
built and validated before it replaces authoritative data, and the document
must never contain a partially installed or invalid candidate.

`AtomicAll` is the default policy for a command that targets several objects:
one failure leaves document data, selection, history, and the dirty revision
unchanged. An operation may use an explicitly declared best-effort policy only
when that behavior is part of its product contract. Such an operation may
commit a fully validated subset in one transaction, but it must return a
structured report naming every skipped target and reason. If no target succeeds,
the command is a no-op and creates no history entry.

### Editor concepts must map to runtime concepts

An editor-only `light`, `trigger`, `sound`, or `particle` icon is insufficient.
The authored record must compile into a versioned runtime component or resource
that the engine can load and execute. Mason should become a client of the ECS,
physics, audio, visibility, lighting, particle, and resource systems rather than
owning private simulations of them.

### Discoverability and speed are both requirements

The same command should be reachable through a discoverable menu or inspector
and through a fast contextual binding. Tooltips and status text must state the
current modifiers. Shortcuts must be rebindable, conflict-checked, searchable,
and scoped by context. The toolbar is a curated subset of the command registry,
not a second command implementation.

### Reliability features belong in the first architecture pass

Undo transactions, cancellation, autosave, incremental backup rotation,
validation, issue selection, quick fixes, stable clipboard data, atomic file
writes, and deterministic export are not polish. Adding them after geometry and
entity tools exist forces every command to be redesigned.

## Evidence and legal boundary

### Sources reviewed

The audit uses these primary sources:

- the pinned [TrenchBroom source tree](https://github.com/TrenchBroom/TrenchBroom/tree/e53a0ef172e10e62ff24b86b70ca3b6ea865cac4);
- the official [2026.2 manual](https://trenchbroom.github.io/manual/latest/);
- action construction and context code under `lib/TbUiLib`;
- interaction controllers under `lib/TbAppLib`;
- the document, geometry, formats, asset, validation, and command code under
  `lib/TbMdlLib`;
- math and geometry primitives under `lib/VmLib`;
- map rendering and caches under `lib/TbRenderLib`;
- the corresponding unit and regression tests in each library.

The manual is generated with the real action catalog, which reduces drift
between prose and bindings. Source was used to fill gaps where the manual states
what a tool does but does not describe transaction boundaries, candidate
validation, selection propagation, index maintenance, or failure behavior.

### License and provenance rule

TrenchBroom is distributed under `GPL-3.0-or-later`. This audit was produced by
reading its source and manual, so it is source-derived research and is **not** a
clean-room specification. The boundary below is an engineering policy for the
Cypher project, not a legal conclusion:

- use the audit to understand observable behavior, mathematical ideas,
  architecture, failure cases, and testing obligations;
- write Cypher requirements, APIs, type names, data layouts, algorithms,
  fixtures, and implementation independently;
- do not paste, translate line-for-line, or mechanically transform TrenchBroom
  source into Cypher;
- do not reuse upstream icons, shaders, test fixtures, configuration files, or
  serialized editor metadata without an explicit compatible licensing decision;
- prefer papers and standards as the implementation source when an algorithm
  has an independent primary reference, and record that provenance;
- treat upstream regression scenarios as evidence for independently designed
  tests, not as test bodies or fixtures to copy.

The upstream class names, action names, menu labels, paths, and line references
in this audit identify evidence. They are not Cypher naming recommendations.
Any distribution decision involving material derived from upstream source needs
an appropriate licensing review.

### Meaning and limits of complete in this audit

Within the declared map-editor scope, “complete” means that every relevant
user-facing category in the official manual and every production subsystem that
materially changes authoring behavior is represented in the inventory. It does
not mean that every helper template, operating-system wrapper, updater path, or
crash-reporting implementation is paraphrased.

The expression language's full conversion matrix and formal grammar are
summarized and linked rather than reproduced. Complete game-configuration and
legacy map-format grammars remain in their canonical upstream documentation;
this audit records the fields and behaviors that affect editor architecture.
Platform bootstrap, packaging, updater internals, and graphics-backend details
are covered only where they change an authoring contract. These exclusions keep
the claim auditable without hiding a category that TileEditor or Mason needs.

## Source architecture map

### Library responsibilities

| Area | Approximate production role | Cypher lesson |
| --- | --- | --- |
| `TbUiLib` | Qt windows, menus, actions, panels, preferences, view layouts, compile and launch dialogs | Qt widgets bind to editor services and query command state; they do not own map semantics. |
| `TbAppLib` | Tool and gesture controllers, 2D/3D interaction, handle drags, camera tools, document facade | Put interaction policy between platform events and document commands. |
| `TbMdlLib` | Node model, brush and patch geometry, map operations, commands, selection, assets, formats, validation | Keep authoritative data and invariants independent from Qt and rendering. |
| `TbRenderLib` | Map renderers, overlays, handles, grids, links, point/portal visualization | Render derived state and cache it behind model notifications. |
| `TbGlLib` | Graphics resources, materials, shaders, buffers, textures, cameras | Keep graphics ownership below editor widgets. |
| `TbFsLib` | Disk and virtual filesystem operations, packages, path matchers | Mount loose and packaged content through one normalized resource view. |
| `TbElLib` | Expression parsing, evaluation, interpolation, variables | Use a bounded declarative language for build profiles and asset metadata. |
| `VmLib` | Vectors, matrices, planes, intersections, polygons, convex polyhedra | Centralize numerical predicates and tolerances; tools must not invent local epsilon policy. |
| `KdLib` / `TbBaseLib` | Containers, contracts, results, tasks, logging, preferences, notifications | Shared foundation enables explicit failures and deterministic utility behavior. |

### Dependency direction

The useful dependency direction is:

```text
Qt shell and panels
        |
        v
interaction/application layer -----> renderer adapters
        |
        v
document/model operations ----------> asset managers / filesystem
        |
        v
math, containers, results, tasks, diagnostics
```

Mason should preserve that direction. In particular, `QWidget`, `QAction`, and
OpenGL/Vulkan handles must not enter authored world records or undo commands.

### Separate indexes for separate questions

TrenchBroom does not force one tree to answer every editor query. Its model
maintains several derived structures:

- a parent/child node hierarchy for ownership and serialized organization;
- a dynamic octree for ray and point spatial queries;
- a compact string trie for entity properties, group names, and material names;
- an entity link manager for source/target relationships;
- tag state for game-specific classification and filtering;
- per-node issue caches invalidated by changes;
- renderer caches associated with renderable nodes;
- selection caches for expanded entity, brush, and face targets.

This is the right pattern for Cypher. A scene hierarchy is not a spatial index,
an ECS archetype table is not an outliner, and a render BVH is not an authored
ownership graph. Each index should be rebuildable from authoritative state.

### Notification model

The model emits typed notifications before and after selection changes, node
addition/removal/change, visibility and locking changes, asset reloads, entity
definition changes, group open/close, transactions, and resource processing.
The document facade forwards these to tools, render caches, panels, and the
window.

Cypher needs a similarly explicit change set, but it should avoid broadcasting
an untyped “document changed” event for every operation. A useful original event
record contains:

```text
revision before / revision after
command ID and display label
created, destroyed, reparented, and modified stable IDs
changed component/property masks
changed spatial bounds
selection delta
asset dependency delta
diagnostics invalidation domains
derived-data rebuild requests
```

That record lets TileEditor update only affected cells and Mason update the
outliner, inspectors, spatial index, renderer, and compiler without rescanning
the entire world.

## The editor data model

### TrenchBroom hierarchy

The document hierarchy is conceptually:

```text
World
  Layer (one mandatory default layer plus user layers)
    Group (nested, optionally linked)
      Group ...
      Point entity
      Brush entity
        Brush
        Patch
      World brush
      Patch
```

The actual parent rules prevent arbitrary combinations. The world owns layers.
Layers own groups, entities, brushes, or patches. Groups can own nested groups
and map objects. A brush entity owns its brush or patch geometry. World geometry
belongs to the worldspawn relationship while still participating in editor
layers and groups.

Each node stores or derives logical bounds, physical bounds, parent and child
state, selection counts, visibility, locking, source line information, issues,
and tags. Logical bounds describe the authored/game object used by snapping and
selection. Physical bounds may be larger when a display model extends beyond
the logical entity bounds.

### Node invariants worth adopting

- A node has at most one parent.
- Parent compatibility is checked before insertion or removal.
- Descendant counts and selection counts propagate through the hierarchy.
- Bounds changes propagate upward and update the spatial index.
- Persistent IDs exist where serialized editor relationships need them.
- Cloning and recursive cloning are explicit operations.
- Search indexes, link indexes, tags, issue caches, assets, and render caches
  update through mutation boundaries.
- Empty containers have a defined deletion policy.
- Selection of a brush entity can resolve to its contained geometry for the
  viewport without corrupting the semantic entity relationship.

### Cypher authored identity model

Mason should use stable 128-bit or otherwise collision-resistant document IDs
for serialized objects and separate short runtime handles after cooking.
Persistent identity is required for:

- undo and redo across container reallocations;
- selection and outliner expansion;
- cross-object properties and trigger targets;
- prefab or linked-instance overrides;
- diagnostics that remain attached after unrelated edits;
- collaboration and source-control merge tooling;
- incremental compilation and dependency tracking.

TileEditor already gives markers stable IDs. Its next representation step should
give independently authored edge, region, trigger, light, sound-emitter, and
prefab records stable IDs as they are introduced. Dense cells can continue to
use coordinates as identity while a resize policy guarantees what happens to
those coordinates.

### Source and cooked data remain separate

The editor document owns rich source state: names, hierarchy, exact topology,
selection aids, layer membership, component authoring data, and diagnostic
provenance. The runtime world owns compact arrays, spatial partitions, resolved
resource handles, physics shapes, light data, visibility cells, audio zones,
particle references, and ECS spawn records.

Mason must never serialize raw pointers, Qt state, renderer handles, undo
objects, or transient selection into the cooked world. Workspace camera and
panel state may be stored alongside source data only under a clearly editor-only
namespace or in a per-user workspace file.

## Actions, tools, gestures, and transactions

### Command registry

TrenchBroom builds actions centrally and assigns each an execution callback,
enabled predicate, checked predicate where relevant, context, label, menu path,
and shortcut preference. Menus and toolbars use the same action objects.

Cypher should formalize a command descriptor similar to:

```text
command_id
display_name
description
category and menu path
icon role
default bindings per platform
input context mask
can_execute(document, selection, active_tool, active_view)
is_checked(...)
execute(command_context)
repeat policy
undo policy
telemetry/diagnostic name
```

Bindings should point to command IDs. UI code should never duplicate `can undo`,
`selection is geometry`, or `tool is active` logic in multiple menu and toolbar
handlers.

### Input contexts

Bindings need explicit contexts so the same key can have useful local meaning.
The required contexts are:

- application and welcome window;
- document window;
- any map view;
- 2D map view;
- 3D map view;
- text/property editor;
- active geometry tool;
- active UV tool;
- active camera navigation gesture;
- modal dialog and popup.

Specific contexts outrank general contexts. Text entry must suppress destructive
map commands unless the binding is intentionally global. Conflicts in the same
context should block preference acceptance and show both command names.

### Permanent and modal tools

Camera navigation, hover picking, and basic selection are permanent services.
Shape creation, clipping, rotation, scale, shear, vertex/edge/face editing, UV
editing, and sweep are modal tools. A modal tool can own temporary handles and
selection without forcing navigation to stop working.

Only compatible modal tools may be active together. Activation is allowed to
fail. Deactivation is allowed to veto if unresolved state must be committed or
cancelled. Escape applies a clear priority:

1. cancel the active drag or platform gesture;
2. cancel incomplete points or a pending shape inside the active tool;
3. deactivate the modal tool;
4. clear selection only when no operation consumed Escape.

### Ordered controller chain

An input event is offered to active controllers in a defined order. Passive
events such as picking, hover updates, modifier changes, render overlays, and
scroll may fan out. Exclusive events such as click, double click, drag start,
drop, and cancel stop when one controller accepts them.

This prevents a viewport widget from growing one giant switch over every tool.
It also provides a testable precedence order. For example, a UV handle should
receive a drag before ordinary face selection, while RMB camera look should
remain available when a shape tool is active.

### Gesture tracker lifecycle

Every drag should have one owner and one lifecycle:

```text
accept(input, initial hit)
  -> begin transaction and capture immutable start state
update(input)
  -> propose constrained handle position
  -> build and validate candidate
  -> apply preview through collatable command or preview buffer
modifier_changed(input)
  -> replace constraint/projection function without a discontinuity
scroll(input)
  -> adjust depth, speed, segment count, or other tool parameter
end(input)
  -> commit one history entry
cancel()
  -> restore exact start state and create no history entry
```

Focus loss, capture loss, document replacement, pane destruction, and tool
deactivation must end or cancel through this same lifecycle. A boolean such as
`isDragging` spread across several widgets is not enough for Mason.

### Preview strategy

Two preview strategies are valid:

- execute collatable commands during a drag and roll back the open transaction
  on cancellation;
- keep an immutable source snapshot plus transient candidate data, then submit
  one command on release.

The first provides live updates to every observer but can stress indexes. The
second isolates invalid candidates and reduces churn but requires every view to
understand preview state. Cypher should support both behind one gesture contract.
Tile painting can use grouped live commands; topology surgery should generally
use candidate data until validation succeeds.

## Viewport navigation and picking

The detailed Cypher navigation comparison remains in
[`tile_editor_navigation_research.md`](tile_editor_navigation_research.md). This
section records the editor-kernel requirements that navigation depends on.

### View layouts

TrenchBroom supports one, two, three, and four pane layouts made from one 3D
view and axis-aligned 2D views. Some panes can cycle XY, XZ, and YZ. Cypher's
current four-pane TileEditor already supports relocatable 3D and orthographic
views, view duplication for 2D projections, per-pane state, maximize/restore,
framing, and persisted splitters. Mason should preserve pane identity while
changing layout so cameras and tool context do not reset.

### Camera operations

Required 3D operations are:

- hold-to-look and configurable fly movement;
- orbit around a picked surface point or a deterministic fallback point;
- camera-space pan;
- forward dolly and temporary optical zoom as distinct operations;
- frame selection and frame document without unexpected rotation;
- exact camera position entry;
- canonical top, bottom, front, back, left, and right orientations;
- configurable FOV, speeds, inversion, and fast/slow modifiers;
- optional persistent fly mode and camera bookmarks for Mason.

Required 2D operations are:

- pan with pointer capture;
- pointer-anchored zoom;
- optional shared scale and shared common-axis pan across orthographic panes;
- visible coordinate compass and current grid scale;
- framing of selection and document bounds;
- command to place or aim the 3D camera from a 2D view.

### Picking pipeline

TrenchBroom's useful pattern is:

1. create a camera ray from the pointer;
2. query a dynamic octree for bounds intersected by that ray;
3. ask candidate nodes for exact hits;
4. let active tools append handle hits with distinct hit-type bits;
5. sort by distance, projected size, and type priority as the view requires;
6. filter by visibility, editability, active group, selection mode, and tool;
7. choose the first match, with a small error metric for nearly coincident hits.

The hit record carries a type, distance, world point, target payload, and error.
This lets tools share one result without unsafe casts from a single global
`selectedObject` pointer.

### 2D ambiguity policy

In an orthographic view, several objects may overlap at the same projected
location. Distance alone is not always the best ordering. TrenchBroom can order
hits by projected area so a small face or object remains selectable in front of
a large enclosing one, with distance as a tie breaker.

Mason needs an explicit cycling policy for coincident hits, visible feedback for
the candidate under the pointer, and a selection menu for dense overlaps. The
policy must be deterministic so repeated clicks or a cycle command traverse a
stable ordered list.

### Spatial index requirements

The audited octree stores an item's bounding box at the smallest address that
contains it, expands the root when required, supports update and removal, and
prunes queries by ray/bounds intersection. Cypher may use a dynamic AABB tree,
loose octree, BVH, or grid depending on the document. The contract matters more
than the exact structure:

- insertion, removal, and bounds update are explicit;
- bulk edits can suspend incremental maintenance and rebuild once;
- queries return candidates; exact geometry performs final intersection;
- empty or invalid bounds never enter the index;
- all derived indexes can be rebuilt and compared against brute force in tests.

TileEditor should use its grid for cell lookup and a compact list or spatial
index for independent authored objects. Mason should start with a dynamic AABB
tree or loose octree and keep renderer and physics acceleration structures
separate.

## Selection semantics

### Selection is typed state

The selection model should expose typed subsets and expanded operation targets:

- selected objects, groups, entities, brushes/solids, patches, mesh elements,
  faces, cells, edges, and markers;
- whether the selection contains only one compatible type;
- common and mixed properties;
- logical and physical bounds;
- containing entities, groups, layers, and linked-instance roots;
- operation targets after hierarchy expansion.

TrenchBroom caches “all entities,” “all brushes,” and “all brush faces” derived
from the current selection. Selecting a group can therefore apply an entity or
brush operation to its descendants without flattening the UI selection itself.

### Object and component selection modes

Mason should distinguish object selection from vertex, edge, face, UV, spline
point, and other component modes. The active component tool can own its handle
selection while the document retains the owning objects. Switching modes must
state whether component selection is preserved, converted, or cleared.

TileEditor should distinguish cells, authored edges, markers, regions, and
future entity objects. Treating a door, spawn, floor, trigger, and sound emitter
at one coordinate as one forever will prevent precise editing as the format
grows.

### Selection operations

The complete baseline includes:

- replace, add, subtract, and toggle selection;
- marquee/lasso selection using containment or intersection policy;
- select all, none, inverse, siblings, children, layer, group, same type, and
  matching property/material;
- drill into and out of groups;
- cycle coincident hits;
- frame and isolate selection;
- reusable named selection sets for Mason;
- selection history only if it has clear user value.

Selection changes may be undoable, but camera movement should remain view state.
Cypher must choose this boundary explicitly. TrenchBroom includes selection,
hiding, and locking in history, which makes complex view-state operations
reversible. Mason can adopt that behavior while keeping per-user pane layout and
camera poses outside the authored dirty flag.

### Multi-edit properties

The inspector displays the union of keys across selected entities. It must show
whether a key is absent from some targets or has different values. Editing a
field applies the value to the chosen target set as one transaction. Numeric
spin controls apply deltas to each original value; direct text entry assigns one
absolute value.

CypherTileEditor already uses a field mask in `tile_map_selection_patch_t` so
unchanged mixed fields are not overwritten. This is the correct foundation.
Mason should generalize it into typed property patches with an explicit target
query and preflight validation.

## Grid, snapping, and coordinate policy

### Grid responsibilities

The construction grid supplies:

- a power-of-two or project-configured spacing sequence;
- point, vector, and bounds snapping;
- movement delta and handle constraints;
- major/minor line visualization;
- angle increments separate from linear spacing;
- temporary fine/coarse modifiers;
- per-document units and world bounds;
- an option to disable snapping without hiding the visual grid.

Displayed grid density is a rendering decision. Authored tile size and map
metrics are document data. The two must not be coupled.

### Snap modes

Mason eventually needs:

- absolute world-grid snap;
- relative delta snap;
- vertex, edge, face-center, pivot, socket, and object snap;
- surface projection and surface-normal alignment;
- angle snap;
- scale increment snap;
- UV pixel and fractional-grid snap;
- nearest valid topological merge snap;
- explicit no-snap modifier.

Candidates should report which snap target won so the viewport can render a
small marker and label. Hidden or locked objects are excluded unless the user
enables them as snap references.

### Axis and plane restriction

Transforms need a constraint object rather than scattered modifier checks:

```text
constraint = free | axis(X,Y,Z) | plane(XY,XZ,YZ) | view_plane | surface_plane
space      = world | local | parent | custom_orientation
pivot      = selection_center | bounds_center | median | active | cursor | custom
snap       = none | grid | angle | feature
```

The handle or key binding chooses this state. A modifier change during drag may
replace the constraint, but the handle must not jump. Rebase the drag origin to
the current accepted candidate when necessary.

## Object creation and transformation

### Creation tool inventory

The audited creation surface includes:

| Tool or command | Inputs | Output | Critical rules |
| --- | --- | --- | --- |
| Box/cuboid draw | 2D or 3D drag, grid, reference depth | One convex brush | Candidate bounds must have nonzero extent and remain in world bounds. |
| Stairs | Bounds, rise direction, step count | Run of convex step brushes | Riser/tread dimensions must be representable at the current grid and produce no zero-volume step. |
| Arch | Bounds, axis, side count, thickness, circle mode, optional spandrel | Multiple convex brushes | Inner radius must remain positive; neighboring wedges must share planes without cracks. |
| Cylinder | Bounds, side count, circle mode, hollow/thickness | One or more convex brushes | Axis and cap orientation are explicit; scalable mode keeps vertices on the integer grid. |
| Cone | Bounds, side count, circle mode | One convex brush | Apex and cap must not collapse through rounding. |
| UV spheroid | Bounds, rings/sides, circle mode | Convex pieces or supported solid representation | Pole degeneracy and face planarity require defined construction. |
| Icosahedron spheroid | Bounds, subdivision parameters | Triangular convex approximation | Deterministic vertex ordering and winding are required. |
| Complex convex brush | Points sampled from existing faces and extruded polygons | Convex hull brush | Interior and duplicate points are removed; creation commits only after a valid 3D hull exists. |
| Patch from face | Selected polygon faces | One or more Bézier patches | Non-quad polygons are decomposed predictably; odd polygons may require a degenerate triangular patch. |
| Point entity | Context placement, drag/drop, or entity browser | Typed entity record | Origin snaps according to view; definition supplies bounds, color, model, and property metadata. |
| Brush entity | Selected brushes plus entity class | Entity owning selected geometry | Reparenting and class assignment form one transaction; unsupported point/brush conversion is rejected. |
| Duplicate | Selection | Cloned hierarchy at same transform | New persistent IDs are generated while internal references are preserved or remapped deliberately. |
| Duplicate and move | Selection plus drag/key delta | Clones transformed in one gesture | The user sees one history entry and the original remains unchanged. |
| Paste in place | Text clipboard | Parsed nodes or face attributes | Parse into detached candidate data, repair ID conflicts, then insert atomically. |
| Paste at cursor | Clipboard plus view hit | Parsed and positioned nodes | 3D places on hit geometry or a fallback plane; 2D derives hidden-axis depth from context. |

CypherTileEditor should adopt only the primitives that map cleanly to its cell
and independent-object data. Box rooms, corridors, doors, stairs, markers, and
parameterized tile assemblies fit now. Arbitrary convex brushes, arches,
cylinders, patches, and topology editing belong in Mason or in a shared geometry
library whose results are inserted as explicit Mason objects.

### Simple-shape parameter contract

Shape UI should edit a typed parameter block rather than invoke a separate
hard-coded builder from each widget:

```text
shape kind
world bounds and construction plane
axis / orientation
segments, rings, steps, iterations
circle alignment mode
wall or shell thickness
hollow and cap options
spandrel or support options
material assignment policy
grid and rounding policy
```

The builder returns either a complete candidate object set or structured
diagnostics. Preview and final creation call the same builder. Parameters are
serializable so a shape can be repeated, turned into a preset, or edited
non-destructively later if Cypher introduces parametric source objects.

### Circle construction modes

TrenchBroom exposes three useful policies for cylinders, cones, arches, and
related shapes:

- edge aligned: selected polygon edges align to the requested bounds;
- vertex aligned: selected polygon vertices touch the requested bounds;
- grid-scalable: a constrained side count and displaced vertex pattern keep
  every vertex on an integer construction grid, including after supported size
  changes.

Mason should present the first two as ordinary circumscribed/inscribed polygon
choices. A grid-scalable curve is useful for classic BSP export, but should be a
named compatibility construction policy rather than the default for mesh-first
worlds. The preview should show the geometric deviation from a true circle.

### Complex convex brush creation

The complex brush workflow places points on existing brush faces, copies all
vertices of a reference face, draws a rectangular point set on a face, or
duplicates and offsets an existing polygon along its normal. Creation computes
the smallest valid convex volume containing the points. Duplicate, collinear,
coplanar-interior, and volume-interior points do not become hull vertices.

Cypher improvements should include point deletion and repositioning before
commit, a visible point list, invalid-hull diagnostics, and a preview of faces
that the hull will generate. The authoritative builder remains a general convex
hull service, independent of the UI point-placement method.

### Patch conversion and control points

For patch-capable formats, polygon faces can be converted into quadratic patch
grids. Non-rectangular faces are covered by multiple quad grids, preferring a
symmetrical decomposition. An odd corner can be represented by coincident
control points in a degenerate triangular patch. Selected control points at the
same world position are clumped so adjacent patches can be edited together.

Increasing grid resolution can preserve the curve exactly through subdivision.
Reducing rows or columns is an approximation and must be presented as lossy.
Mason should defer curved patch authoring until its map source and runtime
compiler have a clear curve representation, tessellation policy, UV policy,
collision policy, and LOD/budget contract.

### Entity creation paths

Point entities are created by a context-menu command, by dragging a class from
the entity browser, or by a shortcut generated for that class. Brush entities
are created from selected brushes through a class command or drag/drop. The
browser groups and filters definitions, supplies visual previews, and treats
the entity-definition file as metadata rather than runtime truth.

Mason's equivalent should create an ECS authoring entity from a registered
prototype or component preset. The registry supplies:

- display name, category, icon, color, and preview model;
- required and optional components;
- typed property descriptors and defaults;
- placement bounds and snap behavior;
- validation rules;
- runtime spawn compiler;
- links to relevant asset editors.

### Duplication and reference repair

A correct duplicate operation does more than copy bytes. It must decide:

- which IDs are regenerated;
- whether links among duplicated objects point to duplicated peers or originals;
- whether links to objects outside the selection are retained;
- whether prefab or linked-group membership is preserved, made unique, or
  duplicated as a new instance;
- which layer and parent receive the copy;
- how protected overrides and component references are remapped;
- where the copy is placed and how collisions are handled.

Cypher should expose explicit `Duplicate`, `Duplicate Linked Instance`, and
`Make Unique` operations instead of hiding all semantics behind modifier keys.

### Clipboard interchange

Textual clipboard data is valuable because it supports cross-document copy,
debugging, and controlled interoperability. The clipboard payload should have a
small envelope:

```text
Cypher editor clipboard version
payload kind: objects | components | faces | cells | material attributes
source document format and units
serialized records with stable local IDs
optional dependency manifest
```

On paste, parse into a detached arena, validate limits and schema, remap IDs and
internal references, resolve asset paths, choose a target parent/layer, compute
placement, then submit one command. Untrusted clipboard text receives the same
bounds, nesting, allocation, and path checks as a file.

### Translation

Interactive translation maps the pointer to a plane or axis, subtracts the drag
origin, applies the active snap policy component-wise, validates the candidate,
and previews the result. Keyboard nudges move by the current grid spacing.
Exact translation accepts a vector.

In a perspective view, TrenchBroom uses the horizontal XY plane by default and
a modifier for vertical Z movement. Mason should also offer explicit transform
gizmos and world/local axis constraints so behavior remains discoverable.

Translation of an entity updates both its authored transform and any legacy
position property only through a format adapter. Cypher formats should have one
typed transform source of truth and derive compatibility properties on export.

### Rotation

The complete rotation tool provides:

- X, Y, and Z rings in 3D and the view-normal ring in 2D;
- a movable pivot with exact coordinate entry and recent values;
- angle snapping with exact numeric apply;
- world/view-relative keyboard roll, pitch, and yaw commands;
- a reset pivot at the snapped selection-bounds center;
- optional update of legacy entity angle properties;
- current-angle feedback during the drag.

Mason should add local, parent, and custom orientation modes; active-object,
median, individual-origin, cursor, and bounding-box pivot modes; quaternion or
orthonormal-matrix storage; and explicit Euler display order. A transform
candidate must reject non-finite values and preserve child transforms according
to the command's declared space.

### Reflection

Flip reflects selected geometry across a plane through the snapped selection
center. The plane normal is derived from the active 2D view or the nearest world
axis to the 3D camera right/up convention.

Reflection changes handedness. Implementations must reverse polygon winding,
repair normals and tangents, update entity orientation, transform collision,
and preserve or deliberately mirror UVs. A negative scale left on runtime
objects is not an adequate topology operation unless every downstream system
supports it.

### Scaling

The bounding-box scale handle exposes faces for one-axis stretch, edges for
two-axis proportional scale, and corners for three-axis proportional scale in
3D. In 2D, side handles affect one axis and corners affect both view axes. The
opposite handle is the normal anchor; a modifier moves the anchor to the center.
Exact XYZ factors use the selection center.

Required validation includes:

- no zero or near-zero scale that collapses volume unless the target type
  explicitly supports planar geometry;
- no NaN or infinity;
- world-bounds check;
- minimum edge and face-area thresholds;
- topology and convexity revalidation;
- component-specific scaling rules for lights, audio ranges, particle emitters,
  physics shapes, and entity display models;
- UV behavior controlled by alignment lock or UV lock.

### Shear

Dragging a bounds face applies an affine shear parallel to that face. The tool
needs a clear stationary plane, shear axis, delta, and transform space. Vertical
shear in a perspective view requires an explicit modifier or Z handle. Texture
alignment preservation depends on the UV representation and cannot be promised
for formats that cannot express the resulting axes.

Mason should store the result in geometry when runtime transforms do not support
general affine matrices. It must not leave a non-orthogonal entity transform in
systems that assume rotation plus scale.

### Deletion and container cleanup

Delete operates on the active selection domain. Removing the last brush from a
brush entity and the last child from a disposable group also removes the empty
container. The entire cascade is one transaction. Layers, prefab roots, world
roots, and required singleton entities need explicit protection rules.

Before deletion, the command collects inbound references. Policy may reject,
clear, redirect, or retain unresolved references as diagnostics. It must never
silently retarget a gameplay link to an unrelated object.

## Brush and polygon mathematics

The full algorithm and test-path audit is preserved in
[trenchbroom_geometry_algorithms.md](trenchbroom_geometry_algorithms.md). It
covers 99 implementation/test files with 310 pinned source references, including
half-edge topology, hulls, clipping and healing, intersection/SAT, every geometry
tool, shape builders, quadratic patches, UV projection and locking, hierarchy
operations, failure semantics, complexity, and an independent Cypher test plan.
The sections below state the architecture-level requirements that follow from
that evidence.

### Plane-defined convex solid

A classic brush is the bounded intersection of oriented half-spaces. Each face
stores a plane, commonly derived from three non-collinear points. With a
consistent winding convention, the plane normal points outward and the kept
space lies on the interior side:

```text
plane: dot(n, x) = d, where length(n) = 1
inside face i: dot(n_i, x) <= d_i + epsilon
brush: intersection of inside(face_i) for every i
```

The finite face polygon for one plane is obtained by clipping a large polygon on
that plane against every other brush half-space, or by enumerating valid
three-plane intersections and sorting the points on the face. The audited
implementation uses a convex polyhedron data structure and maintains vertex,
half-edge, edge, and face relationships as derived brush geometry.

### Required solid invariants

A valid convex brush has:

- at least four non-coplanar vertices and four bounding planes;
- finite coordinates inside hard world bounds;
- consistently oriented, non-degenerate face polygons;
- every vertex on or inside every face plane within the chosen tolerance;
- each undirected edge shared by exactly two faces;
- a closed manifold surface with positive volume;
- no duplicate planes, duplicate vertices beyond tolerance, zero-length edges,
  or near-zero-area faces;
- a representable serialization in the target map format.

All builders and editing commands should return structured failure causes such
as `coplanar input`, `unbounded hull`, `collapsed edge`, `concave candidate`,
`world bounds exceeded`, or `format limit exceeded`.

### Exact predicates and tolerances

One global epsilon is insufficient. Cypher geometry code needs named policies
for:

- point equality and vertex welding;
- point-to-plane classification;
- parallel and coplanar plane tests;
- minimum edge length;
- minimum face area and solid volume;
- ray-hit acceptance;
- grid-snap equality;
- UV equality;
- serialization rounding.

The policy should scale carefully with world units but remain deterministic.
Predicates that decide topology deserve double precision and, where practical,
adaptive or exact orientation tests. Rendering can later convert validated
coordinates to floats.

### Polyhedron representation

For convex-solid editing, a half-edge or equivalent adjacency structure gives
the necessary relationships:

```text
Vertex: position, outgoing half-edge, stable edit ID
HalfEdge: origin/destination, twin, next, incident face
Face: boundary half-edge, plane, material/UV attributes
Polyhedron: arrays plus bounds and validation state
```

TrenchBroom rebuilds and matches geometry as brush planes change. Mason may
store a richer editable convex solid directly, but persistent IDs must survive
ordinary moves where the corresponding element remains recognizable. Cooking
triangulates into runtime meshes and separately produces collision shapes.

### Convex hull

The convex hull operation consumes a point set, removes duplicates, detects its
dimension, and builds the smallest convex polyhedron containing the points.
Interior points disappear. A production implementation needs deterministic
tie-breaking, robust orientation predicates, coplanar-face consolidation, and a
stable face-winding convention.

For Mason, use a well-documented independent implementation of Quickhull,
incremental hull construction, or another established algorithm. The API should
support point-only payloads and propagation of source attributes onto resulting
planes.

## Geometry tool algorithms

### Face extrusion and resize

Face extrusion selects one or more coplanar, geometrically matching brush faces
and moves their supporting plane along its normal. There are four related
operations:

| Mode | Result |
| --- | --- |
| Resize | Replace the moved face plane in the original brush, preserving the face count. |
| Split/extrude | Keep the original face as the split plane and create an adjacent brush so the pair covers the resized volume. |
| Stamp | Duplicate the selected face at an offset and take the convex hull of the source and duplicate polygons. |
| Free face move | Translate a face in the view plane or along its normal, allowing adjacent planes to change. |

The resize candidate is valid only while every required original face survives.
Dragging a plane far enough to eliminate a neighboring face or the moved face is
clamped or rejected. Split mode is disabled for opposing shared faces where it
would create overlapping solids. Multi-brush extrusion requires identical face
polygons on the same plane; parallel planes alone are not enough.

Distance snapping combines two candidates:

- movement distance rounded to the active grid interval;
- positions where any moved vertex reaches a world-grid plane.

The tool chooses the valid candidate nearest the raw pointer proposal. This
explains why curved brushwork can preserve matching faces while ordinary axial
geometry lands on exact grid planes.

Independent implementation outline:

```text
preflight selected faces
  -> group faces by coplanar polygon equivalence
  -> establish oriented normal and permitted interval
for each pointer update
  -> project pointer delta onto normal or active move plane
  -> enumerate grid-distance and vertex-grid snap candidates
  -> for each candidate in nearest-first order
       replace or add plane(s)
       rebuild convex brush geometry
       verify required faces, volume, bounds, and source-format limits
       if split/stamp, verify all generated pieces and non-overlap
  -> publish first complete candidate set
on release
  -> swap all candidate brushes in one transaction
```

### Clipping

Clipping adds an oriented plane to a convex solid and keeps the back half, front
half, or both. A two-point plane is underdetermined, so the active view supplies
the missing orientation; a third point fully determines it. Double-clicking a
face can copy its exact plane points, which avoids introducing a nearly
coplanar seam.

For one brush:

```text
front = intersect(brush half-spaces, back side of inverted clip plane)
back  = intersect(brush half-spaces, back side of clip plane)
```

Classification uses a tolerance and returns `front`, `back`, `coplanar`, or
`spanning`. A keep-both slice creates two candidates only when both have valid
positive volume. A keep-one clip may legitimately leave the original unchanged
when the entire brush lies on the retained side, but must not create a no-op
history entry. If any selected brush cannot produce the requested valid output,
the command reports which brush failed and leaves all selected brushes intact.

In 3D, a clip point placed on a face is snapped in that face's projected grid
while remaining on the face plane. In a 2D view it snaps on the view grid and
may move off the original surface. A visible candidate sphere, numbered points,
oriented plane fill, kept-side color, and live result preview are required.

### Sweep and loft

The sweep tool copies selected source face polygons to a destination cap and
fills the gap with a sequence of convex brushes. The destination has translation,
rotation, and uniform scale. Generation modes are:

- straight: interpolate cap transform along a line;
- arc: revolve around an axis inferred from source/destination rotation;
- S-bend: interpolate along a smooth S-shaped path;
- iterations: continue the transform repeatedly from the previous destination.

For each segment, construct source and destination rings with identical vertex
correspondence, then create side planes between matching ring edges. Every
segment is validated as a convex brush. Twists that reverse winding, scales that
cross zero, self-intersections, non-planar caps, and integer snapping that
collapses vertices must reject the complete preview.

The source tool offers a ghost destination cap, transform handles, segments,
path, iterations, optional integer rounding, reset, and a separate perform
command. Mason should additionally display path frames, per-segment failures,
estimated brush/triangle counts, and a self-intersection warning.

### Vertex insertion, movement, welding, and deletion

Convex brushes are plane-defined even when the user manipulates vertex handles.
A vertex edit therefore maps selected handles back to incident planes or
reconstructs a convex hull from changed points. The required behaviors are:

- coincident handles across selected brushes can form one clump;
- relative and absolute grid snapping are switchable during a drag;
- a moved vertex may split incident polygon faces into triangles to preserve a
  convex hull;
- a point pushed into the hull may cease to be a hull vertex;
- adjacent vertices weld when they meet within the weld tolerance;
- adding a point expands or splits the convex hull only if it becomes extreme;
- deleting vertices, edges, or faces succeeds only if every affected brush
  remains a valid 3D solid;
- rotate, scale, and shear may operate on selected handles through the same
  candidate-validation path.

One safe original implementation is point-set based:

```text
collect source vertices with stable handle groups
apply proposed positions to selected groups
weld points using a deterministic tolerance and ordering
compute candidate convex hull
match new faces to old planes for material and UV inheritance
match surviving topology elements for selection continuity
validate dimension, volume, bounds, and target format
```

This naturally removes interior vertices and preserves convexity. It can change
face topology more aggressively than a plane-aware edit, so the preview must
show resulting face splits. A later implementation may preserve incident planes
when possible and fall back to hull reconstruction.

Edge and face tools use the same handle-selection and clumping infrastructure.
They move all vertices belonging to selected edges or faces, but forbid welds
that would silently change the operation's dimensional meaning. Shared geometry
across separate brushes is recognized by geometric equality, not pointer
identity.

### CSG convex merge

Convex merge collects vertices from all selected brushes and computes their
convex hull. It is not a true union: void regions between or around the source
brushes may become filled so the result remains one convex brush. The preview
should therefore show added volume, and the command should be named “Convex
Merge” rather than “Union.”

Material inheritance first matches each result plane to a coplanar source face.
If multiple matches exist, use a deterministic priority such as selected source
order plus maximum polygon overlap. New hull planes receive the current material
and default face attributes.

### CSG intersection

The intersection of convex brushes is the intersection of all their
half-spaces. Combine the source planes, remove equivalent redundant planes, and
rebuild a convex brush. Empty, planar, or linear results are rejected because a
brush requires positive 3D volume. Successful intersection replaces the inputs
with one result and resolves face attributes by matching planes.

### CSG subtraction

Subtracting one convex solid from another can produce a concave or disconnected
result, so it must return a set of convex fragments. The standard plane-splitting
strategy is:

```text
remaining = [minuend]
outside_fragments = []
for each oriented plane of subtrahend:
  next_remaining = []
  for each fragment in remaining:
    split fragment by plane
    append portion outside subtrahend to outside_fragments
    append portion inside/current plane to next_remaining
  remaining = next_remaining
discard remaining, which is inside all subtrahend planes
return outside_fragments
```

When there are multiple subtractors, apply them in a deterministic order and
prune disjoint bounds early. Only selectable, visible minuend brushes
participate. Hidden brushes are an explicit way to exclude geometry. Remove
zero-volume and duplicate fragments, merge redundant coplanar faces, enforce a
fragment-count budget, and commit only after every replacement set validates.

Cut faces inherit the matching subtractor face's material when a plane match is
available. Surviving exterior faces inherit from the minuend. Completely new
internal faces use the current material. UV projection on copied planes should
preserve world alignment where the format permits.

### CSG hollow

Hollow is a convenience construction equivalent to subtracting an inward
offset version from the original. For a convex brush, offset every face plane
inward by the requested wall thickness, rebuild the inner brush, then subtract
it. The result is generally one convex slab per original face.

The operation must reject thickness at or above the local inradius, offset-plane
combinations that erase the inner volume, or fragment counts beyond limits.
Concave mesh hollowing is a different and much harder offset-surface problem;
Mason must not present the convex-brush algorithm as a general mesh shell tool.

### Plane and attribute matching

Geometry operations generate new faces. Attribute transfer should score source
faces using:

1. same oriented plane within normal and distance tolerances;
2. opposite plane when the operation deliberately reverses a face;
3. polygon overlap area on the plane;
4. source role, such as minuend before subtractor for surviving exterior faces;
5. deterministic source order as the final tie breaker.

Transfer the material reference, UV coordinate system, UV attributes, surface
flags, content flags, light value, and editor tags only when their semantics
remain valid. Record a diagnostic when the target format cannot represent an
attribute.

### UV lock during topology edits

Transform alignment lock preserves the material projection while complete
objects move. UV lock tries to keep each surviving vertex's UV coordinate fixed
while vertex or face topology changes. These are distinct modes.

For a plane-axis UV representation, gather corresponding old/new vertices and
solve for new U/V axes and offsets that minimize UV error. A stable solution
needs non-collinear samples and must reject singular scales. When topology has
no sufficient correspondence, preserve world-aligned projection or fall back to
a documented default and tell the user.

### Geometry operation budgets

Every operation needs preflight limits:

- source object, face, edge, and vertex counts;
- maximum generated fragments or brushes;
- maximum preview and commit time;
- world coordinate and volume range;
- recursion and iteration count;
- allocation budget;
- target-format face and property limits.

Long-running CSG and compile operations should be cancellable background jobs
working on immutable snapshots. The final swap occurs on the document thread
only if the input revision still matches.

## Materials and UV editing

### Material browser

The material browser groups materials into mounted collections, supports name
filtering and collection visibility, shows usage counts and thumbnails, and
defines a current material. Choosing a material sets the default for new faces.
Selecting a face can also update the current material, which makes sample-and-
continue workflows fast.

Mason's browser should add dependency status, source/cooked state, compile
errors, tags, physical-material metadata, favorites, recent items, and drag/drop
payloads. Thumbnails load asynchronously and must use bounded caches.

### Applying and transferring materials

The complete interaction supports:

- apply current material to selected faces or whole brushes;
- sample material and dimensions from a face or object;
- transfer material plus UV attributes from a source face to one face, a brush,
  or all coplanar/connected targets depending on click count and modifiers;
- transfer material only;
- project full attributes from source to target;
- rotate transferred Valve-style axes to match target orientation;
- replace one material globally or within the current selection;
- reveal a picked face's material in the browser.

The replace dialog needs source pattern, replacement material, selection scope,
exact versus wildcard/substring behavior, preview count, and one transaction.

### Projection representations

The audited formats expose two projection families:

- paraxial/standard: choose axes from the dominant face normal and store offset,
  rotation, and scale;
- parallel/Valve 220: store explicit 3D U and V axes plus offsets, rotation, and
  scale, allowing more stable arbitrary-plane alignment and shear.

Cypher materials should store a clear source projection type. Mason should also
support per-corner UVs for mesh geometry. Conversion between plane projection
and per-corner UVs must be explicit because a general UV unwrap cannot always be
recovered as one planar mapping.

### Face attribute editor

Per-face state may include:

- material reference;
- U/V offset;
- U/V scale;
- rotation;
- explicit U/V axes where supported;
- surface flags;
- content flags;
- surface value, color, or game-specific fields.

Multi-face editors show mixed state. Spin buttons apply deltas to every face,
while typed values assign one absolute value. Modifier keys select coarse and
fine increments. Flag editors show one checkbox per configured bit and preserve
unknown bits so opening and saving a map does not destroy forward-compatible
data.

### Align, justify, fit, and flip

Baseline commands include reset projection, reset to world aligned, align top,
bottom, left, and right, center, justify across a selection, fit U, fit V, fit
both, flip horizontally/vertically, and rotate by fixed increments. Fit accepts
subdivision counts so a chosen number of texture repeats covers the face.

The reference space must be stated: face-local, selection bounds, world axes, or
camera axes. Commands that depend on camera orientation use the active 3D view
and should preview their effective U/V direction.

### UV editor handles

For one selected face, the UV view shows the tiled material, UV grid, face
outline, origin, independent origin axes, rotation ring, and U/V axes. It
supports:

- dragging the material for offset;
- dragging grid lines for scale;
- moving the origin or one origin coordinate;
- rotating around the origin;
- shearing explicit-axis mappings;
- snapping origin and grid lines to face vertices and center;
- snapping rotation to face edges;
- grid subdivision controls;
- reset, world reset, flips, and quarter turns.

Mason's mesh UV editor eventually needs island, vertex, edge, and face modes;
seams; unwrap and projection operations; packing; texel-density display;
overlap/out-of-bounds diagnostics; UDIM or atlas policy; and synchronization
with the 3D selection. Those are beyond TrenchBroom's planar face editor and
should use a dedicated UV topology model.

## Entities and typed properties

### From string maps to typed authoring

Classic map entities are serialized as string key/value pairs. Entity definition
files add type information, descriptions, defaults, bounds, colors, inheritance,
display models, and link semantics. The audited property model recognizes:

- string, boolean, integer, and float;
- enumerated choice;
- bit flags;
- origin/vector-like position;
- byte and float color variants;
- link source and link target;
- typed input and output parameters;
- unknown values preserved as strings.

Cypher should serialize typed component data directly through CYKV schemas. A
compatibility exporter may emit legacy strings, but Mason's inspector and
runtime compiler should not need to infer numbers, vectors, or resource paths
from arbitrary text.

### Definition and prototype registry

An entity definition contains a class name, category/group, color, description,
property descriptors, point-versus-brush kind, point bounds, optional display
model expression, and optional decal metadata. FGD inheritance can merge base
classes and properties. DEF, FGD, and ENT parsers normalize their source forms
into one model.

Mason needs a registry built from engine component reflection and project
prototype files. It should support:

- prototype inheritance or composition with cycle detection;
- required components and immutable fields;
- typed defaults and ranges;
- units, display precision, and localization labels;
- asset-reference filters;
- custom inspector providers;
- placement bounds and preview factories;
- version and migration functions;
- runtime compiler and validator hooks.

### Property table behavior

The property editor displays explicit values and optional italicized defaults.
It supports keyboard traversal, adding and removing keys, renaming keys,
multi-selection unions, and commands to set existing, missing, or all default
properties. Smart editors handle flags, colors, and choices.

For Mason, generic add/remove is appropriate only for extension data. Core
component fields should come from schemas and use typed widgets. Read-only,
computed, inherited, overridden, invalid, and mixed values need distinct visual
states.

### Entity display models

Display models can depend on entity properties. The audited expression can
choose path, skin, frame, and scale, with conditionals and fallback values.
The asset manager loads many classic model and sprite formats and updates the
viewport when relevant properties change.

Cypher should use typed preview descriptors and a bounded expression or variant
selection system. Runtime resource paths are canonical project-relative IDs.
Preview failures show the entity's editor bounds and a diagnostic; they must not
make the entity unselectable.

### Links and trigger relationships

TrenchBroom indexes link-source and link-target properties, including numbered
variants, and maintains forward and reverse maps. Missing endpoints remain in
the index as empty link sets so validators can report them. Viewports render
links according to all, selected, transitive, or hidden modes.

Mason should use stable entity IDs for direct links while allowing named tags or
channels where many-to-many discovery is intended. A link descriptor needs:

```text
source entity and output/event port
target entity and input/action port
optional payload mapping
delay, once/repeat, enabled, and conditions where gameplay requires them
editor color/style and debug label
```

Compile-time validation checks endpoint existence, port type compatibility,
forbidden cycles where relevant, cross-layer/export boundaries, and unresolved
asset dependencies. Runtime trigger dispatch belongs to the gameplay/ECS event
system, not to Mason.

### Lights, sounds, particles, and triggers are entities/components

These systems should enter the editor through typed authoring components:

- light: type, color/intensity units, range, cone, shadow policy, cookie/IES,
  bake/static/dynamic policy, channel mask;
- audio emitter: event/resource, gain, pitch, attenuation shape, min/max range,
  looping, spatialization, occlusion, bus, trigger policy;
- particle emitter: effect resource, transform, prewarm, seed policy, bounds,
  activation and budget class;
- trigger volume: collision shape, filter mask, enter/stay/exit outputs,
  repeat/cooldown/once policy, enabled state;
- visibility volume or portal: shape, cell/room membership, portal plane,
  open/closed state, compiler hints;
- collision authoring: shape, layer/mask, material, static/dynamic/query flags,
  trigger state and debug draw.

The editor supplies placement, visualization, property inspection, links, and
validation. Runtime systems define actual behavior and preview adapters.

## Groups, linked groups, and layers

### Ordinary groups

Groups provide named nested ownership and group selection. Clicking a closed
group selects the group. Double-clicking opens it for child editing and locks
the rest of the map. Closing returns to the containing group or world. Group,
ungroup, merge, rename, add selection, and remove selection are transactional.
Empty disposable groups are deleted automatically.

Mason should distinguish a lightweight editor folder from a transform-bearing
group. A folder changes organization only. A transform group supplies a pivot
and parent space. Mixing these concepts creates surprising transforms and
exports.

### Linked groups

Linked groups are synchronized copies without one permanent master. Editing one
temporarily makes it the source: its children are cloned, transformed from the
source group's space into every sibling's space, and replace sibling contents.
Each corresponding descendant carries a link identity. Updates fail atomically
if the source transform is not invertible, a child transform fails, or any
result leaves world bounds.

Selection inside one linked member implicitly locks other members in the same
link set. This prevents conflicting simultaneous edits. Nested linked groups are
supported, but recursive link relationships are detected and repaired or
rejected on paste.

Supported workflows include:

- create linked duplicate;
- transform each group instance independently;
- edit contents from any member;
- separate one member into an ordinary group;
- separate selected members into a new linked set;
- extract corresponding selected descendants into a parallel linked set;
- visualize update arrows;
- clear protected properties.

### Protected per-instance properties

An entity property can be protected in one linked member. Updates neither
overwrite that value nor propagate local changes to siblings. A protected
property may also be intentionally absent, suppressing its creation during
future synchronization. Removing protection restores the corresponding shared
value.

Mason should generalize this into prefab overrides with explicit states:

```text
inherited
overridden value
overridden removal/tombstone
added locally
conflicted/orphaned after prototype change
```

The UI needs `Revert`, `Apply to Prototype`, `Apply to All Instances`, `Make
Unique`, and an override diff. Stable source element IDs are essential for
matching descendants after structural changes.

### Layers

Every object belongs to exactly one layer, with one non-removable default layer.
Layers have names, user ordering, optional color, visibility, lock state,
current/active state, and omit-from-export state. New or pasted objects enter
the current layer unless editing inside a group; objects derived from source
objects generally inherit the source layer.

Layer operations include create, remove, rename, reorder, make active, move
selection, select contents, hide, show, isolate, lock, unlock, set color, and
toggle export. Removing a non-empty layer needs an explicit destination or a
transactional move to the default layer.

Mason should support hierarchical layers only after semantics are clear. It may
also need orthogonal collections for streaming regions, visibility cells,
lighting scenarios, gameplay phases, and teams. Those should not be overloaded
onto one layer flag system.

## Visibility, filtering, hiding, isolation, and locking

### Four distinct concepts

The editor must distinguish:

- filter: hide categories by class, tag, component, or render option;
- explicit hide/show: temporary per-object visibility state, undoable;
- isolate: hide everything outside a target set, with reversible prior state;
- lock: keep visible but remove from selection and editing.

Combining these into one `visible` boolean loses user intent. A filtered object
should reappear when the filter changes without erasing its explicit hidden
state. Inherited layer/group state must remain distinct from a local override.

### Visibility and lock state model

The audited node states use inherited, explicitly shown/hidden, and explicitly
locked/unlocked forms, resolved through ancestors and editor filters. The active
group and layer adjust editability. Linked-group constraints add temporary
locking based on the current selection.

Cypher should compute:

```text
effective_visible = passes_filters
                    && hierarchy_visibility
                    && isolation_membership
effective_editable = effective_visible
                     && hierarchy_lock_allows
                     && active_scope_allows
                     && collaboration_lock_allows
                     && tool_filter_allows
```

Renderer visibility and editor selectability are related but separate. Hidden
objects normally cannot be picked; locked objects render with a clear tint or
outline and remain available as optional snap references.

### Filter sources

Baseline filters cover point entities, brushes, patches, entity classes,
game-configured smart tags, special brush/face types, entity links, group
bounds, models, materials, and renderer overlays. Mason should add component
type, layer, prefab, collision class, light mobility, audio/particle bounds,
visibility contributor, runtime-only/editor-only state, diagnostic severity,
and named queries.

Filters and saved visibility sets belong to workspace/project configuration,
not cooked runtime data, unless a layer's export flag deliberately changes the
build.

## Undo, redo, repetition, and history

### Command contract

Every mutation is represented by a named command with explicit do and undo
behavior. A command knows whether it modifies authored data. Successful
execution records the modification count needed to restore the dirty state
correctly on undo. Failed execution is not stored. A new successful command
clears the redo branch.

Cypher commands should return a structured result:

```text
status
diagnostics
change set
inverse data or reversible snapshot
estimated/stored history bytes
repeat descriptor, when repeatable
```

Do and undo must publish equivalent model notifications. An undo failure is a
serious invariant breach and should produce a fatal editor diagnostic with a
recoverable autosave, rather than letting the history silently diverge.

### Transactions

Nested transactions group commands into one outer history entry. A transaction
can commit, roll back its commands in reverse order while remaining open, or
cancel. Committing an empty transaction creates no entry. Committing a nested
transaction inserts it as one command into its parent.

Typical transaction boundaries include:

- deselect plus hide;
- open group, change locking, and change selection;
- add container, reparent objects, and select the result;
- replace linked-group contents across all siblings;
- a complete mouse gesture;
- quick-fix selection changes plus the fix;
- a compound paste with ID repair and placement.

RAII or scope guards should cancel unfinished transactions on exception or early
return. Background jobs prepare candidates without an open transaction and use
one short transaction for the final revision-checked swap.

### Command collation

Successive compatible commands can merge when:

- neither is being replayed by undo or redo;
- the previous command accepts the next command's type and targets;
- the commands occur within a defined interval or the same gesture;
- no intervening command changes the required state.

Keyboard nudges and property spin changes benefit from timed collation. Pointer
drags should use an explicit gesture transaction and should not rely only on a
timer. Collation must compare stable target IDs and edit modes so two unrelated
objects never merge because their command names match.

### Bounded versus unbounded history

TrenchBroom presents unlimited undo for the session. CypherTileEditor currently
uses 128 entries plus a 16 MiB change-data budget, which is a sensible bounded
policy for its small deterministic document. Mason needs configurable count and
byte budgets, with visible history truncation and optional checkpoint snapshots
for very large operations.

History storage should prefer minimal diffs for ordinary edits and complete
candidate object replacement for complex topology commands. Memory estimates
must include owned strings, arrays, and dependency records rather than only the
command object's `sizeof`.

### Command repetition

TrenchBroom records repeatable actions separately from undo. The repeat stack
acts as one recent macro. Selection changes prime it to clear when the next
repeatable command arrives, so the previous macro can still be repeated until a
new edit begins. Transactions collapse into one repeat action, and actions do
not re-add themselves while replaying.

Mason should store typed repeat descriptors, not captured raw pointers or UI
callbacks. A descriptor resolves the current selection and repeats parameters
such as duplicate, translation delta, rotation, scale, or material assignment.
It must preflight targets and fail without partial effects if the new context is
incompatible.

### History panel

A professional history panel shows command labels, saved revision, current
position, transaction grouping, history memory, and truncated entries. Selecting
an earlier point should request repeated undo/redo through the command processor,
not mutate the index directly. The panel should distinguish authored mutations
from reversible editor state such as selection and visibility.

CypherTileEditor currently exposes undo/redo labels, but it does not yet have a
history panel. Those labels are the starting API. A planned shared history
model should expose immutable command metadata so a future Qt panel can show
and navigate history without inspecting internal command storage.

## Clipboard, files, export, and recovery

### Map loading pipeline

A safe source-document load has these stages:

```text
read bounded bytes
  -> detect/validate header and format version
  -> lex and parse into detached source records with locations
  -> validate structural limits and parent rules
  -> migrate supported older versions
  -> resolve stable IDs and references
  -> build typed document in a temporary arena
  -> load definitions and resolve assets asynchronously
  -> build indexes and derived caches
  -> validate document
  -> replace the current document only after success
```

Parser warnings retain file, line, column, and object/property context. Unknown
forward-compatible data should be preserved where the format contract allows
it. Unsupported versions fail clearly rather than being guessed.

### Saving

CypherTileEditor already serializes a detached text buffer and commits the map
through `QSaveFile`; its configuration writer also disables direct-write
fallback explicitly. This establishes a replacement-safe baseline. All Cypher
source save paths should preserve the same deterministic commit boundary:

1. serialize to a temporary sibling file;
2. flush and check every write;
3. optionally reopen and parse in verification or debug builds;
4. replace the destination atomically where the platform supports it;
5. update the saved revision only after replacement succeeds;
6. preserve the prior file or report the recovery path on failure.

Stable ordering, canonical number formatting, normalized resource paths, and a
canonical CYKV writer keep diffs reviewable. Editor-only metadata should be in a
separate section with stable ordering or in a workspace file.

### Export

Export is a derived operation and does not change the document's saved state.
The audited exporter can write game map text or OBJ/MTL, omit layers, strip
editor-specific properties, strip selected entity classes, add a temporary
camera/player entity, and choose material-path policy.

Cypher export and cook profiles need:

- source revision and content hash;
- target platform and configuration;
- included/excluded layers and tags;
- dependency manifest;
- deterministic compiler versions;
- geometry, collision, visibility, light, navigation, ECS, audio, and particle
  outputs;
- diagnostics and timings;
- atomic output publication.

### Clipboard ID and linked-instance repair

On paste, persistent group IDs that collide with the destination are regenerated.
Recursive linked groups that would create a link cycle are unlinked or rejected.
Internal link IDs are copied so corresponding descendants remain paired, while
external relationships follow an explicit policy. The pasted hierarchy is
detached and repaired before it is added to the live document.

This same import transaction should serve drag/drop, prefab insertion, and
cross-document duplicate. One robust import pipeline is safer than several UI
specific copy routines.

### Autosave and backup rotation

The audited autosaver attempts a backup only when the map is persistent,
modified since its last backup, past the interval, and outside an active
transaction. It stores numbered copies in an `autosave` directory, deletes the
oldest when the configured maximum is reached, and compacts numbering.

Cypher should retain those gates and add:

- atomic backup creation;
- timestamp, source path, document ID, revision, and engine version metadata;
- startup recovery comparison with the primary file;
- a recovery browser with preview and validation;
- retention by both count and byte budget;
- a crash/emergency snapshot path that never overwrites the last good save;
- background serialization from an immutable snapshot for large Mason worlds.

### Point and portal debug files

TrenchBroom loads compiler point files as a trace through a leak and portal files
as polygons, supports reload/unload, and renders them over the map. This is an
excellent model for compiler diagnostics: keep diagnostic artifacts separate
from source data, associate them with a source/build hash, and make them
selectable and reloadable.

Cypher world compilation should emit structured debug resources for leaks,
portal/visibility cells, collision decomposition, lightmap charts, navigation
regions, occlusion contributors, and failed entity references. Mason can render
them through diagnostic overlay providers.

## Issues, validation, and quick fixes

### Live issue model

The issue browser is fed by registered validators. Issues attach to a node, face,
or entity property and carry a stable sequence ID, type, description, source
line where available, hidden state, and selectable targets. Node changes
invalidate cached issues. The browser filters issue types, selects source
objects, can hide individual issues, and offers compatible fixes.

Mason's diagnostic record should include:

```text
diagnostic ID and rule ID
severity: note | warning | error | fatal
human message and optional technical detail
document revision
stable object/component/topology ID
property path or source span
world bounds or position for framing
related locations
suppression key and reason
available quick-fix IDs
build stage and target profile
```

### Audited validator set

The pinned model registers checks for:

- missing classname;
- missing entity definition;
- missing mod/search path;
- empty group;
- empty brush entity;
- point entity containing brushes;
- link source with missing target;
- link target with missing source;
- non-integer brush vertices;
- mixed brush contents;
- hard world-bounds violations;
- soft map-bounds violations;
- empty property keys and values;
- overlong property keys and values;
- quotation marks unsupported by the legacy format;
- invalid world-node path separator use;
- invalid UV scale.

Quick fixes include deleting invalid nodes, removing or transforming properties,
truncating values, snapping vertices, removing missing mods, moving brushes back
to world geometry, and resetting invalid UV scale.

### Cypher validation families

TileEditor and Mason need validators grouped by cost and ownership:

| Family | Examples |
| --- | --- |
| Structural | Duplicate IDs, invalid parent, cycles, dangling references, malformed component data. |
| Geometry | Degenerates, non-manifold topology, concavity where forbidden, out-of-bounds coordinates, self-intersection, invalid UVs. |
| Gameplay | Missing spawn, invalid trigger port, impossible door placement, unsupported ECS component combination. |
| Physics | Invalid scale, dynamic concave mesh, collision layer mismatch, trigger without query shape. |
| Visibility | Non-planar portal, disconnected room, portal outside cell, leaked sealed volume. |
| Lighting | Invalid units/ranges, static light on moving owner, missing cookie, excessive shadow budget. |
| Audio | Invalid event, zero/negative range, missing bus, overlapping zone priority ambiguity. |
| Particles | Missing effect, invalid bounds, unsupported GPU module, budget overflow. |
| Assets | Missing resource, type mismatch, dependency cycle, stale cooked result, case mismatch. |
| Build/export | Omitted dependency, unsupported target feature, non-deterministic output, compiler tool missing. |

Cheap local validation runs synchronously after a command. Expensive global or
compiler validation runs on an immutable snapshot in the background and labels
results with the revision it examined.

### Quick-fix safety

A quick fix is a normal preflighted command or transaction. It must show its
scope, be undoable, and refuse stale diagnostics. Batch fixing should group only
compatible rule IDs and revalidate after each dependency-changing stage.

Destructive fixes such as delete should list affected objects and inbound
references. “Fix all” must never mean “delete everything with an error” without
an explicit preview.

### Suppressions

Users may hide an instance temporarily or suppress a rule by object, layer,
project, or build profile. Suppressions require a reason and should remain
visible in a separate filter. Errors that make a cooked resource unsafe or
unloadable cannot be suppressed for shipping builds.

## Assets, game definitions, and configuration

### Virtual filesystem

TrenchBroom mounts built-in resources, loose game directories, additional mod
search paths, classic package archives, and WAD files into a virtual filesystem.
Priority controls conflict resolution. Asset managers read through that view so
materials and models do not care whether bytes are loose or packaged.

Cypher's existing VFS should supply the same editor-facing contract with
canonical virtual paths, mount provenance, package trust, case diagnostics,
content hashes, and change notifications. Tools must display which mount wins a
collision.

### Game/project configuration

The audited game configuration version 9 describes:

- game name, icon, and experimental status;
- supported map formats and optional initial maps;
- asset search path and package formats;
- material root, extensions, palette, world property, shader path, and excludes;
- entity definition files, default color, model scale expression, and default
  property policy;
- smart tags and their matchers;
- surface/content flag names and default face attributes;
- soft map bounds;
- named external compilation tools;
- maximum property length.

Breaking config changes increment the version and old unsupported configs are
rejected. Cypher should give `cypher.project` and each schema the same explicit
versioning, migration, bounded parsing, and diagnostics described in the format
reference manual.

### Material and model formats

The reference editor loads many legacy image, texture, shader, sprite, and model
formats because it serves several games. Cypher should avoid pulling all those
decoders into the engine. Source importers belong in compiler plugins; the
editor and runtime consume Cypher source metadata and cooked assets.

Required editor behavior remains:

- asynchronous discovery and loading;
- placeholders during load or failure;
- stable resource IDs and dependency tracking;
- hot reload after successful cook;
- usage counts;
- explicit source/cooked distinction;
- cancellation and bounded decode allocations;
- renderer resource lifetime independent from document pointers.

### Smart tags

Smart tags classify brushes or faces by entity classname, material pattern,
content flag, surface flag, or shader surface parameter. A tag can affect
rendering, filters, and generated actions that apply/remove the classification.

Mason should generalize this into saved typed queries and authoring tags. A
query can match component presence, property values, asset tags, layer, or
diagnostic state. Actions may select, hide, colorize, or batch-edit the results.
Runtime gameplay tags remain a separate typed component with explicit cooking.

### Configuration ownership

| Data | Correct owner |
| --- | --- |
| Theme, font, mouse sensitivity, personal shortcuts | User preferences |
| Pane layout, cameras, local isolation, expanded outliner rows | Workspace/session state |
| Toolchain paths and engine executable | User or machine profile |
| Project asset roots, schemas, build profiles | Project configuration |
| Geometry, entities, layers, prefab instances, gameplay components | Authored source document |
| Resolved runtime handles, BVHs, light clusters, visibility sets | Cooked/runtime resources |

Importing a preference profile validates the entire candidate before changing
live settings. Applying a theme must not alter the document, cameras, selection,
or dock layout.

## Build, compile, run, and debug workflow

### The reference workflow

TrenchBroom treats compilation as authored project data rather than a hard-coded
button. A compilation profile contains a name, an optional working-directory
expression, and an ordered list of enabled or disabled tasks. The audited task
variant contains six operations:

| Task | Inputs and behavior |
| --- | --- |
| Export Map | Target path, optional entity-class glob to strip, optional temporary entity to add at the camera, and removal of editor-only properties. Layers may opt out of export. |
| Run Tool | Executable expression, parameter expression, captured output, and policy for a nonzero result code. |
| Launch Engine | Saved engine profile ID and policy for launch failure. |
| Copy Files | Source glob and destination directory; creates directories and replaces existing targets. |
| Rename File | One source and one destination path; creates the containing directory and replaces the destination. |
| Delete Files | Target glob expanded relative to the profile working directory. |

Enabled tasks run sequentially. Each task emits start, output, error, and end
events. A failure either stops the sequence or permits the next task according
to that task's policy. The dialog can stop a running child process and has a test
mode that resolves and prints every operation without changing files or starting
programs. Closing the dialog or application asks before terminating an active
run. Compilation happens in the background, so the document remains editable.

The expression environment exposes the profile working directory, map directory,
map base and full names, game directory, enabled mods, application directory,
CPU count, and configured tool paths. The task editors provide completion for
these variables. The profile and its task order persist with project-specific
configuration.

### Expression language surface

TrenchBroom's compile and launch profiles use a small expression evaluator,
documented in the official
[Expression Language reference](https://trenchbroom.github.io/manual/latest/#expression_language)
and implemented under `TbElLib`. It is also used by portions of game and entity
model configuration. The audited language surface includes:

- `Boolean`, `String`, `Number`, `Array`, `Map`, the internal `Range` value,
  `Null`, and `Undefined`, with an explicit type-dependent conversion matrix;
- names, grouped terms, quoted strings, floating-point number literals,
  booleans, array literals, map literals, and inclusive ascending or descending
  ranges;
- string and array subscripting by indices, lists, ranges, open ranges, and
  negative indices, plus map subscripting by string key or key arrays;
- unary `+`, `-`, logical `!`, and bitwise `~`;
- arithmetic `+ - * / %`, logical `&& ||`, bitwise `& ^ |`, shifts,
  comparisons, range construction, case terms, switch terms, and documented
  precedence and left-to-right behavior for equal-precedence operators;
- a case expression that produces `Undefined` when its premise is false and a
  switch expression that returns the first non-`Undefined` result.

Conversion is not generic coercion: legality and result depend on both types.
For example, blank or numeric strings have defined Boolean/Number conversions,
`Null` converts to empty scalar/container values where allowed, and
`Undefined` generally propagates an error or acts as a missing switch result.
String, array, and map `+` operations concatenate or merge, while numeric
operators require convertible operands. Exact conversions, grammar, precedence,
and edge cases remain canonical in the pinned manual at
[`index.md`, lines 1847-2405](https://github.com/TrenchBroom/TrenchBroom/blob/e53a0ef172e10e62ff24b86b70ca3b6ea865cac4/app/TrenchBroom/resources/documentation/manual/index.md#L1847-L2405).

Cypher should add an expression language only where declarative project data
needs it. Such an evaluator must be versioned, deterministic, bounded by input,
recursion, collection, and evaluation limits, and free of implicit filesystem,
network, process, or mutation effects. Diagnostics must carry source spans and
expected/actual types. Build arguments remain typed values until process launch;
the evaluator must not become a route for constructing an implicit shell command.

### What Cypher needs beyond that workflow

Cypher should preserve the profile idea while replacing an untyped shell-like
pipeline with a typed build graph. A build node declares:

- a stable node ID, operation type, schema version, and display name;
- typed input resources, files, directories, variables, and parameters;
- declared outputs and their resource types;
- dependencies and ordering constraints;
- target platform, configuration, and feature conditions;
- whether failure stops dependents, the whole graph, or neither;
- deterministic cache keys and the inputs that contribute to them;
- execution backend, timeout, cancellation policy, and sandbox policy;
- structured diagnostics, log channel, progress, timings, and exit status.

The graph can still expose simple `Export`, `Cook`, `Copy`, and `Launch` rows in
the UI. The typed representation prevents a misspelled path or missing output
from remaining invisible until runtime. An advanced external-tool node remains
available, but its executable and arguments are separate values; commands are
never concatenated and handed to a shell by default.

### Snapshot compilation

The build button must compile a specific immutable document revision. Saving is
one way to create that snapshot, but the editor may also serialize a temporary
canonical snapshot when the user wants to test unsaved work. Every diagnostic
and build product records:

```text
project ID + document ID + source revision + schema versions
+ build profile + target platform + compiler versions + dependency hashes
```

An edit made while a build runs therefore cannot silently alter its inputs. A
diagnostic produced for an older revision remains readable but is marked stale
and cannot drive an automatic quick fix. A successful build is published with
an atomic manifest update only after all required outputs have been written and
validated.

### Required Cypher authoring pipelines

TileEditor and Mason share build infrastructure, but they do not share a source
or cooked identity. The bounded tile path is:

```text
.cymap authored snapshot (`cypher.map` V1-V3)
    -> parse and tile-schema validation
    -> deterministic tile geometry, materials, and markers
    -> editor/runtime preview
    -> optional dedicated tile cooker -> .cymap_c
       only if a tile runtime product is explicitly admitted
```

Mason's production world path is distinct:

```text
.cyscene immutable snapshot (`cypher.scene` V1 planned)
    -> parse and scene-schema validation
    -> stable object/component resolution
    -> deterministic authored world geometry
    -> collision shapes and query acceleration
    -> entity and component spawn records
    -> visibility cells/portals or conservative fallback
    -> light and probe inputs
    -> audio, trigger, particle, and navigation records
    -> CypherSceneCompiler
    -> dependency manifest and cooked .cyscene_c sections
    -> runtime validation
    -> launch REAP with scene and optional spawn override
```

TileEditor may initially generate only geometry, material assignments, markers,
and static collision. It should still use the same manifest and diagnostic
protocol as Mason. Mason imports a tile map through an explicit conversion that
creates a new `.cyscene` and leaves the `.cymap` unchanged. A shipping runtime
must not parse an editable `.cyscene`; shipping a tile map likewise requires an
explicitly admitted tile cooker and reader rather than silently treating it as
a Mason scene.

### Build and launch interface

The editor needs the following controls and states:

- profile chooser, duplicate, rename, delete, import, and export;
- task/node add, remove, duplicate, enable, disable, and reorder;
- field validation and variable completion before execution;
- Build, Rebuild, Test, Cancel, Reveal Output, Copy Command, and Clear Log;
- live node state: queued, checking cache, running, skipped, succeeded, warned,
  failed, or canceled;
- compiler output linked to object IDs, component paths, source spans, and files;
- elapsed time, cache hit state, produced size, and peak-memory summary;
- last successful build identity and an explicit indication that the document is
  newer than that build;
- launch profiles with executable, argument list, environment, working
  directory, target map, spawn mode, and debugger choice;
- rerun-last-build and rerun-last-launch commands;
- point, leak, portal, collision, navigation, and visibility debug overlays that
  can load compiler-produced artifacts.

Launch must verify that the selected product exists and matches the chosen
source revision. If the user intentionally launches an older successful build,
the UI should say so rather than implying that current edits are present.

### External-process safety

Build profiles can execute arbitrary developer tools, so imported profiles are
untrusted project data. Before first execution, Mason should show the resolved
executable, arguments, working directory, environment changes, inputs, outputs,
and destructive file operations. Path operations must remain inside declared
roots unless the profile has an explicit trusted exception. Glob expansion is
bounded, symlinks are resolved before policy checks, and deletes never accept an
empty or filesystem-root target.

## Rendering, caching, and large-map performance

### Reference renderer organization

The audited renderer does not walk the entire document and rebuild every GPU
buffer for each frame. `MapRenderer` observes document notifications and tracks
which model nodes belong in default, selected, and locked renderers. Each of
those object renderers owns group, entity, brush, and patch renderers. Separate
passes draw opaque and transparent content, while link and decal renderers are
invalidated only by relevant model or resource changes.

Brush rendering groups face indices by material, maintains vertex and edge
arrays, remembers allocation blocks for each brush, and keeps an invalid-brush
set. An invalid brush is rebuilt on demand; unrelated brushes remain cached.
Transparent brush/material groups carry a world-space sort position. A render
batch prepares shared buffers, then submits direct, indexed, retained, and
one-shot renderables through a per-view render context. That context carries the
camera, 2D/3D mode, material and edge modes, grid, fog, entity overlays,
selection display, map bounds, filtering, and DPI scale.

The lesson is the invalidation boundary. Rendering derives from document state
and reacts to typed notifications. Render caches do not become an alternative
document model.

### Cypher editor rendering contract

TileEditor and Mason should expose a renderer-neutral scene snapshot or change
stream containing stable IDs, bounds, mesh handles, material handles, display
flags, pick IDs, and overlay primitives. A viewport adapter converts this into
CypherRender submissions. The adapter owns GPU-facing resources; the document
owns authored data and resource references.

Use three cache layers with explicit invalidation:

| Cache | Contents | Invalidated by |
| --- | --- | --- |
| Document-derived | Generated cell boxes, brush polygons, triangulation, bounds, semantic queries. | Commands that change the contributing authored fields. |
| View-derived | Visible set, projected handles, label layout, hit proxies, transparent order. | Camera, viewport, filter, isolation, or document-derived changes. |
| GPU-derived | Vertex/index allocations, instance buffers, textures, pipelines, pick buffers. | Resource reload, device loss, renderer setting, or changed render payload. |

Selection tint, hover, and locks should usually be compact per-object state or
separate overlays rather than a complete geometry rebuild. Tool previews use a
transient overlay channel tied to the gesture lifetime. Debug rendering accepts
immutable line, triangle, text, bounds, and icon batches from physics,
visibility, audio, navigation, particles, and compiler diagnostics.

### Spatial and semantic indexes

One index cannot answer every editor query efficiently. Mason needs at least:

- a stable-ID object table for direct lookup and reference repair;
- a hierarchy index for parent, child, layer, group, and prefab traversal;
- a static spatial index for world geometry and immobile authored objects;
- a dynamic spatial index for moving objects, handles, and live previews;
- a semantic index for component types, names, tags, property keys and values,
  materials, assets, and diagnostic state;
- adjacency indexes for entity links, trigger routes, portals, and prefab
  dependencies;
- a resource dependency graph for hot reload and cooking.

TrenchBroom uses a dynamic octree for spatial candidates and a compact trie for
selected textual fields. Cypher should choose structures after measuring its
queries. A BVH, loose octree, dynamic AABB tree, hash grid, or sorted arrays are
all valid behind the same query contract. The reference linear implementation
must remain available for differential tests.

TileEditor is different: bounded dense cell storage already gives constant-time
coordinate access. It needs a small marker-ID index and optional occupied-cell
list, not a scene graph or octree. Large sparse or stacked tile worlds can adopt
chunked storage later without changing editor command semantics.

### Background work and revisions

Triangulation, validation, thumbnail decode, asset scan, light preview,
visibility analysis, and cooking may run in worker jobs. Every job receives an
immutable input snapshot and returns the revision and dependency versions it
used. Publication occurs on the owner thread only if those versions remain
compatible. Cancellation must be cooperative and bounded; a discarded stale
result must not leak renderer resources or overwrite a newer cache entry.

### Performance targets and telemetry

Performance gates should be written as budgets for representative maps rather
than a claim that one structure is fast. Record at least:

- command preview and commit latency at median, 95th, and 99th percentiles;
- frame CPU/GPU time in 2D and 3D views;
- candidate count and exact-test count for picking and marquee selection;
- bytes in document, history, derived geometry, indexes, and GPU caches;
- nodes or cells invalidated per command and bytes uploaded per frame;
- save, load, validate, and cook duration plus output size;
- asset scan, thumbnail, and hot-reload queue latency;
- cache hit rates and background-job cancellation counts.

Useful development gates are a 16.6 ms interactive frame at the target editor
scene, sub-frame hover feedback, bounded history memory, and cancellation that
responds within one UI frame. Final numeric scene sizes should be set with real
REAP maps and stored benchmark fixtures, not guessed from an empty document.

### Large-map failure modes to test

- one massive brush or mesh whose bounds cover most spatial cells;
- thousands of tiny coplanar objects under the cursor;
- transparent faces distributed through the full depth range;
- a filter or layer toggle that changes most objects at once;
- mass material replacement and material hot reload;
- deeply nested groups and prefab instances;
- huge sparse selections and an inspector edit across all of them;
- repeated undo/redo of topology-heavy operations;
- a camera outside world bounds or at extreme coordinates;
- minimized, zero-sized, high-DPI, and multi-viewport rendering;
- device loss or shader/resource reload during a live tool preview.

## Testing and robustness requirements

### What the upstream test volume shows

The pinned tree contains roughly 115,193 C/C++ test lines across 325 files. The
largest concentration is in the document/model library, followed by foundation,
UI, application, filesystem, math, render, image, OpenGL, and expression
libraries. That distribution is sensible: most editor failures are corrupted
state, incorrect geometry, invalid parsing, or broken command reversal rather
than a visibly wrong widget.

Cypher does not need to reproduce those tests or their fixtures. It does need
the same seriousness at its own invariants and boundaries.

### Test layers

| Layer | Required evidence |
| --- | --- |
| Scalar/math | Tolerances, intersections, projections, matrices, planes, rays, bounds, and overflow behavior. |
| Geometry unit | Known constructions and invalid candidates for every primitive and topology operation. |
| Property and fuzz | Random valid convex solids, command sequences, CYKV documents, malformed resources, and invariant preservation. |
| Command/history | Do/undo/redo equivalence, nesting, collation, cancellation, no-op suppression, selection restoration, and memory limits. |
| Serialization | Deterministic bytes, round trip, migration, unknown fields, duplicate IDs, canonical ordering, and atomic-save recovery. |
| Picking/input | Candidate filtering, hit order, occlusion, tolerance, high DPI, modifiers, drag thresholds, focus loss, and cancel priority. |
| Asset/VFS | Mount priority, case differences, package corruption, dependency cycles, hot reload, cancellation, and placeholder behavior. |
| Compiler/runtime | Source-to-cooked determinism, section bounds, dependency manifests, structured diagnostics, and runtime rejection of corrupt data. |
| UI integration | Action state, menu/toolbar reuse, shortcut conflicts, inspector multi-edit, layout persistence, and accessibility names. |
| Performance | Stable fixtures with time, allocation, cache, upload, and output-size thresholds recorded by build configuration. |

### Geometry invariants after every successful command

For a convex-solid implementation, verify:

1. every face has at least three distinct vertices;
2. every vertex is finite and lies inside or on every defining half-space within
   the declared tolerance;
3. every undirected edge belongs to exactly two faces;
4. face winding is consistent with outward normals;
5. the polyhedron is connected, closed, and has positive volume;
6. duplicate or nearly identical planes and vertices have been canonicalized;
7. coordinates stay within hard world bounds;
8. topology IDs that survive the operation remain unique and references are
   either repaired or diagnosed;
9. material and UV policy has been applied to all new faces;
10. an undo returns canonical serialization, selection, and derived bounds to
    the exact pre-command state.

For the current tile document, verify canonical empty cells, in-bounds markers,
unique marker IDs, the single-spawn rule, valid door boundary placement, valid
stairs, material binding limits, deterministic derived boxes, and sparse
selection semantics.

### Differential and metamorphic tests

A simple reference algorithm is valuable even if production uses acceleration.
Compare BVH or octree queries against a linear scan, incremental geometry against
a complete rebuild, and cached validation against a full pass. Metamorphic
properties catch failures without requiring a hand-authored answer:

- translate by `d`, then `-d`, preserves canonical output;
- four 90-degree rotations preserve a valid object;
- reflect twice across the same plane restores it;
- undo then redo matches the first committed result;
- save/load/save emits identical canonical bytes;
- changing object insertion order does not change cooked hashes;
- an empty selection command is a true no-op;
- canceling at every gesture stage leaves no model, history, cache, or dirty
  revision change.

### Fuzzing and malformed input

CYKV, map, model, image, package, clipboard, build-profile, and cooked-resource
parsers need size, recursion, token, allocation, decompression, and time limits.
Fuzz targets should retain minimized crashing inputs. A parser failure returns a
typed diagnostic with source location and never partially publishes a document.

Geometry fuzzing should generate valid seeds, mutate planes or vertices, run a
single operation, and check either a clean failure or all invariants. It must
exercise tiny and huge magnitudes, near-parallel planes, coplanar points,
zero-area faces, repeated points, negative and near-zero scales, and world-bound
contacts.

### Regression fixtures

Every fixed corruption, crash, incorrect pick, non-deterministic export, stale
cache, or undo failure earns the smallest independently authored regression
fixture. Store the triggering operation and expected invariant, not a screenshot
alone. Binary goldens carry a format version and an intentional-update tool so
ordinary test runs cannot rewrite expectations.

### Benchmark suite

Create stable fixtures for a dense tile arena, a sparse large tile map, a small
Mason graybox, a topology-heavy convex map, an entity/link-heavy scene, and an
asset-heavy scene. Benchmark cold and warm save/load, validation, spatial index
build, common picks, marquee selection, material replacement, undo/redo, build,
and runtime load. Report time, peak and retained bytes, allocation count, output
hash, and machine/build metadata. A benchmark is a regression signal; it should
not fail on noisy single samples without repeated measurements and a justified
tolerance.

## Complete command and control inventory

The exhaustive source-referenced inventory lives in
[trenchbroom_ui_command_inventory.md](trenchbroom_ui_command_inventory.md).
It is part of this research set, not optional reading for the input or action
system. It catalogs all 179 static actions from the pinned revision by registry,
user-facing command, default shortcut, and source location; it also records
dynamic tag/entity rules, six continuous fly controls, alternative shortcut
behavior, menu/toolbar realization, platform translations, and every concrete
map and UV pointer gesture. Context, enablement, checked-state, and execution
behavior are explained as architectural rules and command-specific prose rather
than reproduced as a bulk table of source expressions.

### Verified action counts

| Category | Count or rule |
| --- | ---: |
| Static actions | 179 |
| View-local actions | 68 |
| Menu actions | 111 |
| Debug-only menu actions | 8 |
| Static release actions | 171 |
| Explicit non-empty default shortcuts | 115 |
| Unbound default shortcuts | 50 |
| Platform-standard shortcut defaults | 14 |
| Checkable actions | 31 |
| Actions reused by the toolbar | 16 |
| Continuous fly-control preferences | 6 |
| Dynamic smart-tag actions | Between one and three per tag, according to supported operations |
| Dynamic entity actions | One visibility action per definition and one creation action per non-world definition |

Those counts are useful because they expose a common design mistake: a mature
editor command surface is much larger than its toolbar. Cypher must make the
registry searchable and data-driven; a row of hard-coded `QAction` connections
will become inconsistent as soon as Mason adds component and asset actions.

### Command descriptor required by Cypher

Every command exposed to a user should have one stable descriptor:

```text
stable command ID
display name and description
category and menu path
icon and optional toolbar membership
default bindings plus user alternatives
input context predicate
enabled predicate with disabled reason
optional checked or mixed-state query
parameter schema
execution callback or command factory
repeatability and history policy
telemetry/debug label
```

Menus, toolbars, command palette, context menus, settings, documentation, and
automation consume that descriptor. They do not independently recreate the
label, shortcut, or enable rules. Generated documentation should include every
configured alternative and every chord in a sequence; the reference generator
currently omits alternatives and truncates multi-chord sequences, which Cypher
should avoid.

### Context model

The reference actions match three axes: active view type, active tool, and
selection kind. Cypher should extend that model without turning it into UI
widget tests:

- workspace: TileEditor, Mason, material, particle, audio, or play mode;
- focused surface: 3D, Top, Front, Side, UV, graph, outliner, inspector, text;
- tool or interaction mode;
- selection domain: cell, object, face, edge, vertex, component, asset, graph;
- document capabilities and read-only state;
- live gesture, modal dialog, text editing, or camera-capture state.

Physical keys may share commands in disjoint contexts. Arrow keys can nudge an
object, adjust UVs, move a graph selection, or navigate text, but only the
focused context may claim them. Continuous movement axes remain held-input
bindings rather than one-shot commands.

### Pointer and gesture precedence

The reference 3D controller order starts with camera navigation, continues
through movement and active geometry tools, and leaves ordinary selection and
shape drawing near the end. Passive move, hover, modifier, and pick events can
reach multiple controllers. Exclusive click and drag acquisition stop at the
first accepting active controller. UV controllers similarly order rotation,
origin, scale, shear, offset, and camera handling.

Mason should use the same explicit precedence contract. Each controller answers
whether it can begin a gesture from an immutable input/pick snapshot. The first
accepted controller owns updates, modifier changes, commit, and cancel until
the gesture ends. Crossing the drag threshold must re-use the original press
position for the initial pick, or a two-pixel move can accidentally change the
target.

### Cancellation order

Escape is a stack, not a single command:

1. cancel mouse capture or the current drag and roll back its transaction;
2. cancel an unfinished multi-stage tool operation;
3. deactivate the active modal tool;
4. clear an appropriate selection only if the workspace defines that behavior;
5. close a transient popup when no editor interaction owns cancellation.

Focus loss, document replacement, viewport destruction, and application
shutdown must invoke equivalent cleanup. No abandoned controller may retain a
transaction, mouse capture, preview object, or raw document pointer.

### Platform behavior

Bindings are stored in a platform-neutral form and displayed through the host
UI toolkit. On macOS, menu Command semantics, physical Control-click, Delete vs
Backspace, Spotlight's Command+Space reservation, and wheel-axis conventions
require explicit tests. Cypher's serialized action IDs must remain stable across
platforms even when the displayed key names differ.

## Lessons from other editors

The complete evidence, feature matrix, 53-link primary-source index, and
editor-by-editor analysis are in
[editor_comparison_beyond_trenchbroom.md](editor_comparison_beyond_trenchbroom.md).
It compares Valve Hammer and Source 2 Hammer, Dota 2's Source 2 tile editor,
GtkRadiant/Q3Radiant, J.A.C.K., Blender, and Unity ProBuilder. Facts in that
document are separated from Cypher recommendations.

### Source caveats

Source 2 tools vary by game branch. Dota's tile editor is evidence for a real
Valve workflow, not a promise that every current Hammer branch exposes the same
tools. Valve Developer Community is Valve-hosted and partly community
maintained. The official Radiant manual documents an older Q3Radiant lineage,
and the J.A.C.K. manual is dated, so both are evidence for established patterns
rather than an exhaustive current product specification. Blender and ProBuilder
are broader mesh tools; their features should be adopted only where an
engine-aware map editor benefits.

### Capability comparison

| Capability | Strong reference lesson | Cypher decision |
| --- | --- | --- |
| Semantic tile editing | Dota stores height, path, water, vegetation, placement categories, variants, probabilities, and edge connectivity as tile-domain data. | Add explicit tile sockets/constraints and a deterministic resolver to TileEditor; keep visual variation separate from gameplay data. |
| Convex grayboxing | TrenchBroom, Hammer, Radiant, and J.A.C.K. show the speed and predictability of grid-aligned brushes. | Preserve a robust convex-solid workflow in Mason. |
| Arbitrary mesh topology | Blender and ProBuilder expose loop/ring selection, cut, bridge, dissolve, weld, bevel, inset, and attribute-aware editing. | Add a controlled manifold mesh kernel after convex editing is reliable; do not attempt a full DCC feature set. |
| Parametric shapes | ProBuilder retains editable shape parameters until topology edits bake them. | Retain parameters for stairs, doors/openings, arches, ramps, pipes, and cylinders; make the bake boundary explicit. |
| Terrain and blend data | Hammer displacements, Dota tile terrain, and DCC paint tools treat terrain, holes, and blend channels as first-class data. | Add terrain only with real collision, navigation, streaming, render, and compiler consumers; use finite explicit blend channels. |
| Prefabs and instances | Hammer/Source 2 instances, Blender linked data/overrides, and TrenchBroom linked groups separate shared source from local overrides. | Use external prefab assets, stable local IDs, dependency tracking, curated field/path overrides, cycle detection, and reference repair. |
| Paths and splines | J.A.C.K. offers insertion, deletion, relinking, closure, join/split, inversion, traversal, and entity conversion. | Define one reusable path component for AI, roads, cameras, particles, audio, triggers, and moving platforms. |
| Decals/projectors | J.A.C.K. exposes decal preview as a dedicated workflow. | Add a typed projector object with bounds, receiver mask, sort order, material, and bake/runtime policy. |
| Organization | Hammer VisGroups and Blender collections show that parentage, grouping, visibility, selection lock, and build inclusion differ. | Model these as orthogonal relationships; do not force one scene tree to express all of them. |
| Build iteration | Radiant region builds, J.A.C.K. error navigation, and Source build stages shorten edit-test loops. | Add selection/region builds with dependency closure, cached stages, cancellation, artifact provenance, and clickable diagnostics. |
| Geometry diagnostics | J.A.C.K. problem checks, Radiant compiler guidance, Blender topology tools, and ProBuilder repair commands expose invalid data. | Validate at every command boundary and offer explicit, undoable repair; never silently change authored geometry. |
| Collaboration | Stable external source assets and deterministic text help ordinary version control more than an opaque monolith. | Prefer stable IDs, canonical serialization, small prefab/path/tile assets, and explicit merge conflicts; defer live multi-user editing. |

### Tile rules are a distinct authoring model

A geometric grid answers where an object is. A semantic tile model answers
which content is compatible with its neighbors and gameplay constraints. The
TileEditor should eventually record:

- edge and corner height signatures;
- path sockets, class, width, direction, and intersection semantics;
- water presence, level, shoreline transition, and optional flow metadata;
- wall, door, fence, traversal, and visibility sockets;
- collision, navigation, occlusion, biome, tileset, and gameplay tags;
- deterministic variant weights with a stable seed;
- dependency radius for incremental neighborhood resolution;
- an explanation when no authored candidate satisfies the constraints.

Preview, commit, undo, load validation, and headless build must call one resolver.
An edit first produces a candidate resolution plan. If any mandatory cell has no
valid candidate, the document remains unchanged or commits only through an
explicit user-reviewed partial policy.

Decorative choices may depend on the semantic result, but they cannot become the
only source for collision, navigation, triggers, or traversal. A changed random
seed must not silently change whether a door is passable.

### Controlled mesh editing

Mason should eventually support object, face, edge, and vertex selection plus:

- connected, boundary, loop/ring, material, normal-angle, similar-attribute,
  grow, and shrink selection;
- cut/bisect, bridge, weld/merge-by-distance, dissolve, triangulate, bevel,
  inset, fill-hole, detach, and controlled extrusion;
- explicit seam, hard-edge/smoothing, primary UV, lightmap UV, vertex color, and
  finite blend-channel attributes;
- a documented propagation rule for every attribute through every topology
  command.

This is not a reason to import Blender's modifier stack, sculpting, retopology,
Geometry Nodes, or full texture-paint environment. Content that does not require
engine-aware authoring should remain in a dedicated DCC tool and pass through
Cypher importers.

### Prefab and reference contract

A production prefab instance needs more than copied children or a name match:

```text
prefab resource ID + source revision
instance ID and transform
stable local object/component IDs
mapping from local IDs to instance/runtime IDs
curated override paths and tombstones
nested dependency graph with cycle checks
reference fix-up policy for internal, external and missing targets
deterministic expansion/cooking
```

Paste, duplicate, array, import, unpack, relink, and source updates all use the
same reference remapper. Overrides are displayed as inherited, locally changed,
removed, invalid, or conflicting. Updating the source is transactional across
all affected instances.

### Region build and navigable diagnostics

A region build is useful only when it includes dependency closure. Building the
selected room may still require shared materials, prefab sources, adjacent tile
resolution, portal neighbors, collision seams, navigation borders, shadow/light
inputs, and entity targets. The resulting artifact is labeled partial and may
not replace a full shipping product.

Compiler diagnostics carry severity, stable rule ID, message, source document
and revision, object/component/element ID, source span when relevant, world
position/bounds, build node, and suggested repairs. Clicking one selects the
object or topology element, focuses an appropriate viewport, and displays the
same debug artifact that caused the failure.

### Explicit rejection and deferral

Reject blind feature parity, unseeded variants, visual tiles as authoritative
gameplay, silent repair, hidden modal state, one relationship for every scene
concern, and arbitrary mesh Boolean operations as the foundation of modeling.

Defer a Blender-scale modifier or node system, freeform sculpting, a complete
DCC UV/paint suite, arbitrary structural prefab overrides, synchronous live
co-editing, advanced terrain simulation, and any content editor whose runtime
consumer does not yet exist.

## Cypher capability map

### Current evidence

The current repository has a real TileEditor implementation, but the similarly
named engine subsystem directories do not imply equivalent runtime systems.
`CypherEntity`, `CypherPhysics`, and `CypherAudio` currently contain placeholder
files in the audited branch. Particle authoring is planned in the source catalog
but has no runtime module. `CypherWorld/Visibility` documents a future boundary;
the full visibility runtime is not present. Lighting currently belongs to the
renderer/world plan and should be coordinated with the renderer work already in
progress.

TileEditor itself is further along. Its core owns a dense row-major cell
document, stable marker IDs, bounded grouped history, validation, deterministic
serialization, material-slot bindings, flat and stair cells, doors and one
player spawn, arbitrary cell selections, region transforms, and renderer-neutral
box generation. The Qt frontend provides four synchronized views, 3D picking,
navigation, ten tile tools, multi-edit inspection, an outliner, material and
piece palettes, configurable appearance and shortcuts, validation, geometry
build, and runtime-preview controls.

### Capability matrix

| Area | Implemented or usable now | Next TileEditor contract | Mason/full-engine destination |
| --- | --- | --- | --- |
| Tile document | Dense cells, materials, stairs, doors, spawn, IDs, history, validation, persistence. | Add schema-versioned authored object/component records without bloating every cell; define migrations and dependency manifests. | Separate scene document with arbitrary objects, topology, hierarchy, layers, prefab instances, and components. |
| Selection and views | Exact sparse cell selection across Top, Front, Side, and 3D; region moves/copies/rotation and bulk properties. | Extract reusable typed selection/action/gesture services; add saved filters and diagnostic selection. | Object, face, edge, vertex, component, asset, and graph selections with stable topology IDs. |
| Geometry | Floors, exposed boundary walls, doors, stairs, boxes, line/fill/rectangle/stamps. | Collision-ready mesh metadata, explicit openings, height/stack policy, deterministic compiler handoff. | Convex solids, patches/meshes, extrusion, clipping, sweep, vertex/edge/face editing, transforms, CSG, UV tools. |
| Build/playtest | In-editor geometry build and preview controls. | Immutable revision snapshot, typed build profile, cooked map section, static collision, structured diagnostics, launch handshake. | Incremental world build graph with visibility, lighting, navigation, resources, streaming, and remote runtime inspection. |
| Entity/ECS | Player spawn and doors are specialized markers. | A small authored component envelope and schema inspector; compile spawn, transform, door, trigger, light, sound, and particle descriptors. | Generation-safe runtime entities, registered component storage, queries, prefabs, hierarchy, events, serialization, replication bridge. |
| Collision/physics | Geometry can be generated; no complete runtime collision engine is evidenced. | Cook static tile colliders, ray/shape casts, collision layers, character step/slide, trigger overlaps, debug draw. | Broadphase/narrowphase, contact manifolds, bodies, constraints, queries, deterministic gameplay facade, optional mature backend. |
| Triggers | Door placement only; no general trigger volume or routing system. | Box/cell trigger volumes, filters, enter/stay/exit events, one-shot/cooldown policy, named outputs, preview and validation. | Arbitrary shapes, typed event graph or connections, server authority, save/network state, debugging and profiling. |
| Audio | No implemented runtime audio subsystem in the audited branch. | Sound-emitter and audio-zone authored components with asset IDs, radius previews, buses, validation, and cooked records. | Device/backend, mixer, voices, buffers/streams, events, buses, effects, spatialization, occlusion, reverb, capture and stats. |
| Particles/VFX | No implemented particle runtime is evidenced. | Particle-emitter authoring component, effect resource reference, bounds, seed, trigger policy, preview controls, budgets. | Versioned effect graph, CPU baseline, optional GPU simulation, pooling, events, renderer submission, deterministic test mode, stats. |
| Lights | No general TileEditor light authoring record. | Point/spot/area records as supported by renderer contract; units, range, color/temperature, shadows, mobility, icons and influence preview. | World light registry, clustered/culling handoff, baked/static/dynamic policy, probes, cookies, lightmaps, compiler diagnostics. |
| Visibility | View filters exist; runtime visibility compilation is not complete. | Frustum/layer/distance visibility first; optional room/portal authored hints with conservative fallback and debug overlay. | Static/dynamic spatial indexes, portal/PVS data, streaming cells, occlusion integration, renderer-neutral visible submissions. |
| Navigation/pathfinding | No complete cooked navigation runtime is evidenced in the audited branch. | Mark tile walkability, area costs, exclusions, links, and agent profiles; cook deterministic tiles with seam diagnostics and preview queries. | Streamable versioned nav resources, bounded projection/path/raycast queries, dynamic-obstacle policy, path corridors, invalidation, overlays, and profiling. |
| Assets | Material slots and previews exist; wider source/cooked graph remains in progress. | Stable resource IDs, async thumbnails, dependencies, hot reload, missing-resource diagnostics, cooker manifest. | Unified resource database, compiler plugins, package mounts, residency, versioned cooked formats, editor/runtime lifetime separation. |
| Reliability | Grouped undo/redo, validation, status codes, limits, deterministic source, and replacement-safe `QSaveFile` commits are present. | Add save durability/error-path tests, optional reopen validation, autosave/backups, stale-result handling, richer issue browsing, quick fixes, fuzzing, and benchmark fixtures. | Cross-document transactions, crash recovery, migrations, audit logs, background validation, build reproducibility. |

### What should remain TileEditor-specific

- dense coordinate identity and direct row-major access;
- one authored floor record per cell until stacked floors become a real game need;
- cell-connected fill and one-cell line operations;
- tile stamps and construction slices;
- renderer-neutral generated boxes as an immediate graybox preview;
- simple marker workflows for the first playable arena.

### What should move into shared editor services

- stable command descriptors and contextual shortcuts;
- ordered tool controllers and gesture lifetime;
- typed selection sets and common transform intentions;
- transaction/history interfaces and dirty revisions;
- asset reference fields and resource browser model;
- diagnostics, issue selection, suppression, and quick fixes;
- build profiles, structured logs, and runtime launch;
- viewport camera/navigation settings, pick result types, and overlay batches;
- workspace/session persistence separate from document persistence.

### What belongs only in Mason

- arbitrary scene hierarchy and transform parenting;
- topology identities and direct face/edge/vertex editing;
- convex brush, patch, free-mesh, terrain, spline, and decal authoring;
- linked prefab instances and protected overrides;
- world partition, portal, visibility, navigation, and lighting build controls;
- component-schema inspectors and connection/event graph editing;
- large-scene spatial and semantic indexes.

## TileEditor implementation program

The order below protects the working tile tool while turning it into the first
consumer of engine systems. Each phase must end in a usable application and a
loadable map. Do not replace the dense document with Mason's future scene model.

### T0: Freeze and measure the current baseline

Before architectural extraction:

- add canonical demo maps for empty, minimal playable, dense, sparse, stairs,
  doors, mixed materials, maximum supported dimensions, and malformed input;
- capture source hashes, generated geometry counts/hashes, validation results,
  history behavior, save/load time, build time, and viewport frame time;
- document current `.cymap` version and all limits;
- add an automated launch smoke test that opens the demo, builds geometry, and
  exits without a graphics or document error;
- list every existing action and preference ID so refactoring cannot silently
  reset user settings.

Exit gate: existing maps round-trip identically and focused TileEditor tests pass
from a clean build.

### T1: Shared action, input, and gesture kernel

Move command metadata out of ad hoc UI construction into a stable registry.
Define input contexts for text fields, panels, each viewport, camera capture,
selection tools, and authoring tools. Introduce one gesture-owner interface with
begin/update/commit/cancel and one layered cancellation path.

Adapt the current Qt actions to the registry without changing behavior. Keep
the existing canvas and render viewport as clients. Add searchable shortcut
settings with primary/alternative bindings, conflicts, platform display, reset,
import/export, and disabled-reason reporting.

Exit gate: all existing commands remain reachable and rebindable; mouse capture,
focus loss, Escape, document close, and tool changes leave no open edit group.

### T2: Document identity, notifications, and source migration

Keep coordinate identity for cells and stable IDs for markers. Add a generic
stable authored-object ID for records that are not cells: trigger volumes,
lights, sound emitters/zones, particle emitters, notes, and future gameplay
objects. Store these in typed arrays or an object table rather than optional
fields on every cell.

Commands publish typed change sets after commit:

```text
revision changed
cells added/removed/changed
objects added/removed/changed
selection changed
materials changed
document description changed
```

Extend `.cymap` through an explicit version and migration. Unknown optional
component data is preserved when safe; unknown required data blocks editing
with a diagnostic. Source remains canonical and deterministic.

Exit gate: all old fixtures migrate, IDs stay stable through save/load and
undo/redo, and incremental listeners produce the same derived state as a full
rebuild.

### T3: Optional tile compiler boundary and cooked map shell

Execute this phase only if a dedicated tile runtime product is approved. If the
production path instead converts a blockout into `.cyscene`, implement that
deterministic converter and keep `.cymap` as TileEditor source. An admitted tile
cooker accepts a canonical snapshot and emits a versioned sectioned product plus
a dependency manifest and structured diagnostics. Initial sections are:

- document/world metadata and source identity;
- material/resource references;
- render-neutral tile geometry;
- static collision geometry and layer metadata;
- spawn and door records;
- generic authored object/component records that the runtime understands;
- bounds and integrity checksums.

The editor invokes the same headless code path used by CI. It may preview
in-memory build data, but success is defined by loading the cooked product
through the runtime reader.

Exit gate: two clean builds from identical inputs are byte-identical, corrupt or
truncated sections are rejected, and the runtime can load and unload the demo
map repeatedly without leaked handles.

### T4: Static collision and player playtest

Generate collision from floor, wall, stair, and door semantics. Keep visual and
collision generation separate but derived from the same source. Add collision
layers, masks, surface/material IDs, queries, and debug overlays. Implement the
first-person character needs before general rigid bodies: swept shape, step up,
slide, ground classification, ceiling response, depenetration, and deterministic
test traces.

Door collision must have an explicit state model; deleting render geometry alone
cannot open a doorway. Runtime Preview loads cooked collision, places the player
at the selected or authored spawn, and reports contact/query statistics.

Exit gate: automated movement courses cover flat ground, walls, corners, stairs,
door openings, ceilings, slopes if supported, high speed, starting overlap, and
world bounds.

### T5: Trigger and ECS bridge

Add box/cell trigger authoring with an inspector for name, shape, collision
filter, enabled state, one-shot/repeat, cooldown, required tags/components, and
typed outputs. Compile it into an ECS entity with transform plus trigger and
event-routing components. The physics system emits overlap transitions; the ECS
owns gameplay state and dispatch.

Add a connection view or focused inspector list that validates targets and
event/parameter types. Visualize trigger bounds, links, invalid targets, and
live enter/exit events during playtest.

Exit gate: enter, stay, exit, disable, destroy, teleport, map unload, and network
authority policies are specified and tested; no raw editor pointer or source
string is required at runtime.

### T6: Lights, audio, and particles as authored components

Introduce one system at a time behind versioned component schemas:

1. lights with type, units, color/temperature, intensity, range, cone/area,
   shadow and mobility policy;
2. sound emitters and zones with event/resource, bus, gain, range, attenuation,
   looping, priority, occlusion, and reverb policy;
3. particle emitters with effect resource, transform, bounds, seed, start/stop,
   looping, rate scale, visibility policy, and budget class.

Each has a viewport icon, influence/bounds overlay, typed inspector, asset
picker, validation, copy/paste, undo, source persistence, cooked record, runtime
spawn, debug panel, and missing-resource behavior. A placeholder preview is
acceptable before the final renderer integration, but the authored schema and
runtime lifecycle must already be real.

Exit gate: maps containing each component build and run headlessly; component
creation/destruction and map unload release resources; disabled or culled
components behave consistently.

### T7: Layers, filters, issues, and recovery

Add source layers with visibility, lock, color, export, and build tags. Add saved
typed filters across cells, component types, materials, validation state, and
names. Build an issue browser with severity/rule filters, object selection,
camera focus, suppression, and safe quick fixes.

Retain the existing `QSaveFile` replacement commit and the configuration
writer's disabled direct-write fallback. Add durability and failure-path tests,
optional post-write reopen validation, timed and command-count autosave, rotating
backups, crash-recovery choice, and backup provenance. Autosave never mutates
the user's saved revision identity.

Exit gate: forced termination during write cannot corrupt the last good source;
recovery identifies exact map and revision; hidden/locked/export-omitted state
survives round trip and has defined compiler behavior.

### T8: Build graph, live diagnostics, and profiling

Replace the direct preview sequence with the typed build profile described
earlier. Add cancellation, dry run, per-node logs, cache keys, artifact links,
and launch profiles. Feed runtime collision, trigger, audio, particle, and world
statistics back into editor debug panels over a versioned local protocol.

Exit gate: CI, command line, and editor produce the same product; canceled or
failed builds never publish partial outputs; every diagnostic identifies its
source revision.

## Mason implementation program

Mason should begin only after the shared editor kernel and at least one complete
TileEditor-to-runtime slice are stable. Its first milestone is a small graybox
map editor, not a universal content-creation suite.

### M0: Scene document and stable identity

Define a source document with:

- map/world settings and hard/soft bounds;
- stable object IDs and generation-checked editor handles;
- hierarchy with world, layer, group, prefab instance, entity, solid, mesh,
  patch, terrain, path, decal, and volume categories as they become real;
- local transform, world-transform cache, local/world bounds, visibility, lock,
  color, export policy, and component envelope;
- explicit resource references and dependency provenance;
- typed selection sets and source locations;
- structural validation and deterministic serialization.

Begin with world, layer, group, entity, and convex solid. Other categories stay
schema-reserved only when a concrete next phase needs them.

Exit gate: create, rename, reparent, group, layer, transform, save, load,
copy/paste, undo/redo, and duplicate preserve valid IDs and hierarchy.

### M1: Picking, transforms, and viewport foundation

Provide perspective plus orthographic views through the shared navigation and
action services. Implement broadphase candidates, exact hits, hit ordering,
occlusion policy, cycling, marquee/frustum selection, focus, and typed handles.
Implement translate, rotate, scale, reflect, shear, duplicate-drag, snapping,
axis/plane constraints, coordinate spaces, and numeric entry.

Exit gate: transformations either commit a validated candidate atomically or do
nothing; multi-view selection and handles stay consistent at extreme zoom and
high DPI.

### M2: Convex-solid kernel and simple shape creation

Implement plane-defined convex solids and a proven internal topology
representation. Add cuboid, wedge/stair, cylinder/prism, cone, arch, and sphere
generators only after their parameter constraints and output policies are
defined. Preserve material/UV and topology identity where possible.

Exit gate: all geometry invariants, deterministic reconstruction, world bounds,
candidate failure, and undo are covered by unit, property, fuzz, and regression
tests before interactive topology editing begins.

### M3: Face, edge, and vertex editing

Add extrusion, free face move, resize, clipping, vertex insertion/movement/weld,
edge movement, face movement, split, and deletion. Each tool previews a candidate
and states why an invalid result cannot commit. Selection restoration uses
stable topology matching rather than array offsets.

Exit gate: every operation has documented new-face material/UV rules, merge and
tolerance behavior, multi-object semantics, and cancellation at every stage.

### M4: CSG and advanced construction

Add convex merge, intersection, subtraction, hollow, sweep/loft, and reusable
shape parameters. Subtraction may emit multiple convex fragments; it must carry
source provenance and repair selection/references. Provide fragment count and
complexity preview before expensive operations.

Exit gate: operations are deterministic under stable input ordering, reject
pathological output with diagnostics, and meet measured complexity budgets.

### M5: Materials and UVs

Add material browser, collections, search, usage, replacement, face assignment,
projection modes, texture lock, UV lock, transfer, alignment, fit, flip, and a UV
view with origin/rotate/scale/shear/offset handles. Cypher material references
remain stable IDs; import paths never become runtime identifiers.

Exit gate: topology operations preserve or deliberately regenerate UVs according
to a documented policy, and UV commands round-trip deterministically.

### M6: Layers, groups, prefabs, and linked instances

Add source layers and ordinary groups first. Then add prefab or linked-group
instances with a canonical source, instance transforms, protected typed
overrides, cycle detection, broken-link diagnostics, unpack/separate, and
transactional propagation. Do not infer instance identity from names.

Exit gate: editing one source updates every valid instance atomically; invalid
transforms or references roll back all instances; overrides survive source
property removal and reintroduction by explicit policy.

### M7: Entities, components, and connections

Drive inspectors and creation catalogs from registered component schemas. Add
display icons/models, smart editors, multi-edit, copy/paste, link visualization,
incoming/outgoing traversal, event and parameter type checking, prefab
composition, and runtime debug state.

Exit gate: every authored component has source, cooker, cooked, runtime, debug,
and migration ownership; unknown required schemas block build clearly.

### M8: World-building systems

Add static collision, visibility cells/portals, navigation inputs, light build
inputs, audio zones, particle bounds, decals, terrain, paths/splines, and
streaming cells in vertical slices. Each system uses the common diagnostics and
debug-overlay contracts.

Exit gate: one REAP arena builds through all required systems and launches from
Mason with inspectable artifacts and reproducible hashes.

### M9: Production hardening

Add autosave/recovery, project migration, command palette, layouts, scripting or
automation only through a stable command API, large-map profiling, async asset
pipelines, reference repair, source control friendliness, and packaging.

Exit gate: multiple representative maps survive long edit sessions, repeated
build/run cycles, crashes, schema upgrades, missing assets, and branch merges
without silent data loss.

## Engine subsystem contracts

The editor can author these systems only after their ownership and runtime
interfaces are defined. The contracts below are deliberately renderer-neutral
so renderer work can continue independently.

### Rules shared by every subsystem

- Public references use typed generation-checked handles or stable resource IDs,
  never owning pointers across subsystem boundaries.
- Create, update, destroy, map unload, device/backend loss, and engine shutdown
  have explicit order and idempotent failure handling.
- Configuration is versioned and validated before live state changes.
- Runtime APIs return typed status and diagnostics; logs supplement results but
  are not the control protocol.
- Read-only snapshots or command queues cross thread boundaries. A subsystem
  states which thread owns mutation and callbacks.
- Debug state has bounded snapshots and stable labels so tools never inspect
  private containers directly.
- Source data, compiler intermediate data, cooked resources, runtime instances,
  and editor previews are distinct types with explicit conversion.
- Every resource and component declares hard limits, memory ownership,
  determinism expectations, and behavior when a dependency is missing.
- Maps unload through dependency order and invalidate handles without reusing a
  live generation.

### ECS and entity runtime

#### Entity first production slice

The first entity system needs identity and explicit component storage, not an
immediate commitment to the most sophisticated archetype ECS. Implement:

- a 64-bit or equivalently robust entity handle with index/generation and one
  invalid value;
- allocation, destruction, liveness checks, and generation rollover policy;
- a registered component-type ID with version, size, alignment, lifecycle
  functions, serialization/cooking hooks, debug name, and schema identity;
- sparse or dense per-type storage with iteration and direct lookup;
- creation/destruction command buffering when systems iterate;
- transform plus optional parent with cycle prevention and dirty propagation;
- deterministic prefab/spawn descriptors and an old-to-new ID remap;
- typed queries sufficient for the game loop;
- event queues with clear ownership, delivery phase, overflow policy, and no
  retained component pointers;
- debug enumeration and validation.

An editor object's stable source ID is not the runtime entity handle. The cooked
spawn record may retain a source-debug ID for diagnostics, while each load
allocates runtime handles for that world instance.

#### Authored component descriptor

Every editor-visible component schema defines:

```text
schema ID and version
display name, category, icon
field IDs, types, units, ranges, defaults and constraints
single/multiple-instance policy
required and conflicting component types
source serialization and migrations
cooker and cooked version
runtime construction/destruction ownership
editor visualization and picking policy
validation rules and quick fixes
```

The inspector edits typed values through commands. It does not store arbitrary
display strings and ask runtime systems to interpret them later.

#### ECS tests

Cover stale handles, mass create/destroy, generation changes, mutation during
iteration, component move/copy/destruction, alignment, parent cycles, prefab
remapping, deterministic query order where promised, event overflow, map unload,
serialization migration, and allocation-failure rollback.

### Collision and physics

#### Boundary and data model

`CypherPhysics` owns query acceleration, shapes, collider instances, bodies,
contacts, triggers, character movement, and physics debug data. `CypherWorld`
owns world placement and coarse visibility. ECS owns gameplay identity and
component state. The renderer only receives debug primitives or transform
snapshots.

Core types:

- world handle and immutable step/query settings;
- shape resource: box, sphere, capsule, convex hull, triangle mesh, and compound
  only as each is implemented;
- collider instance: shape, transform, material, layer, mask, flags, and user
  handle;
- body handle and static/kinematic/dynamic motion type;
- ray cast, overlap, closest-point, and swept-shape request/result;
- contact point/manifold and trigger transition;
- character input/result with position, velocity, grounded state, ground normal,
  touched handle, step result, and failure flags;
- bounded debug and statistics snapshot.

#### Staged implementation

1. Use a linear list as the correctness reference.
2. Add static triangle/convex queries for cooked tile maps.
3. Add a broadphase with differential tests against the linear list.
4. Implement capsule or chosen character shape casts, step/slide/depenetration,
   stable ground detection, and moving-platform contract.
5. Add overlap triggers and kinematic doors.
6. Add dynamic rigid bodies, contacts, islands, constraints, sleeping, and a
   solver only when gameplay demonstrates the need; a mature backend may sit
   behind the same facade.

Triangle meshes are appropriate for static cooked world collision. Dynamic
concave bodies must be rejected or decomposed into supported shapes. Nonuniform
and negative scale require an explicit cook rule; they cannot silently distort
a cached collision shape.

#### Query rules

A query states world, shape/ray, start/end or direction/range, layer/mask,
ignored handles, backface policy, closest/all/any mode, and output capacity.
Results carry distance/fraction, point, normal, collider/body/entity handles,
subshape/triangle ID, material/surface ID, and start-overlap information.
Results never contain transient pointers.

#### Determinism

The game needs repeatable movement and prediction-friendly rules before it needs
bit-identical general rigid-body simulation. Specify fixed-step timing, query
tie-breaking, iteration order, tolerances, maximum depenetration, maximum step,
and quantization or reconciliation policy. Record movement inputs and collision
results in regression replays.

### Trigger system

A trigger is a collaboration among systems:

```text
authored trigger volume and routing
    -> cooked shape + ECS component + typed connection table
    -> physics overlap pairs
    -> enter/stay/exit transition normalization
    -> ECS/gameplay event dispatch
    -> save/network state and debug stream
```

The trigger component needs enabled state, activation mode, one-shot/repeat,
cooldown/delay, team/layer/tag/component filters, target connections, authority
policy, and optional payload. Physics detects pairs but does not execute game
logic. Destruction, disable, teleport, filter changes, map unload, and missed
frames define whether an `exit` is emitted.

Editor validation rejects missing targets, incompatible event parameters,
zero-volume shapes, forbidden overlaps, cycles where a rule prohibits them, and
connections across unloaded ownership domains. Live preview colors inactive,
overlapping, fired, cooldown, disabled, and invalid states distinctly.

### Audio runtime

#### Ownership

The audio thread/backend owns the device, mixer graph, active voices, streaming
decoders, DSP state, and hardware buffers. The game thread submits bounded
commands and reads delayed statistics. Resource decoding/cooking stays in the
resource/compiler systems. ECS/world components describe emitters, listeners,
zones, and event state.

#### Audio first production slice

- enumerate/open/recover an output device and support a null backend for tests;
- immutable decoded sound buffers and bounded streaming sources;
- voice allocation with priorities, stealing policy, pause/stop/fade, looping,
  pitch, gain, and completion events;
- master and named buses with gain/mute and simple effects routing;
- one listener and 2D/3D sources with distance attenuation and panning;
- sample-accurate or block-defined command timing contract;
- resource/event handles rather than paths at playback call sites;
- statistics for underruns, voice counts, streams, command queue, memory, and
  callback time.

Later stages add HRTF or richer spatialization, obstruction/occlusion queries,
reverb zones and sends, snapshots, music transitions, multiple listeners if
required, capture, and platform backends.

#### Editor authoring

A sound event resource separates authored behavior from one audio file. An
emitter references the event and exposes auto-start, loop, gain, pitch range,
priority, spatial blend, min/max distance, attenuation curve, cone, bus, and
occlusion policy. A zone exposes shape, priority, blend distance, reverb/snapshot
resource, and send level. The viewport draws range/cone/zone overlays and can
solo, audition, restart, and stop previews without saving runtime voice handles
in the document.

#### Audio tests

Run the mixer and event logic against a null/offline backend. Test device loss,
underrun recovery, command overflow, voice stealing, streaming EOF/seek,
loop points, map unload, rapid start/stop, missing resources, NaN/Inf rejection,
attenuation curves, zone priority, and bounded callback allocations. Offline
renders may use hashes or signal metrics with tolerances rather than fragile
byte equality across codecs.

### Particle and VFX system

#### Resource and instance split

A versioned particle-effect resource contains an emitter graph or bounded module
list. A runtime particle-system instance contains transform, simulation time,
seed, spawn state, live-particle storage, bounds, LOD/budget state, and renderer
submission data. An ECS emitter component owns the effect reference and instance
lifecycle through the VFX API.

Start with a CPU baseline whose behavior is testable:

- rate, burst, duration, loop, delay, and maximum-particle emission;
- deterministic seed and per-particle random streams;
- position/shape, initial velocity, lifetime, color, size, and rotation;
- gravity/acceleration, drag, color/size-over-life curves;
- local/world simulation space;
- sprite/billboard or simple mesh output contract;
- conservative bounds, frustum/distance culling, pause/catch-up policy;
- pool and per-effect/global budget behavior;
- start, stop-emitting, restart, kill, and completion semantics.

Collision, sub-emitters, ribbons, lights, audio events, vector fields, GPU
simulation, sorting, and complex graphs come after the baseline has profiling
and content pressure. GPU simulation must keep a CPU-visible bounds and lifecycle
contract and a deterministic editor test mode, even if live particles are not
bit-identical.

#### Editor authoring and preview

TileEditor places an emitter component and previews a referenced effect. Mason
later supplies a dedicated graph workspace. The map inspector exposes instance
overrides only: transform, seed mode, auto-start, rate scale, color tint, LOD,
visibility policy, and trigger connections. Preview has play, pause, restart,
single-step, fixed-time scrub where supported, bounds, overdraw/debug modes, and
live counts. The editor never persists preview time or pooled instance handles.

#### VFX tests and budgets

Test spawn counts over time, fixed-seed reproducibility, lifetime removal,
curves, pause/restart, local/world transforms, bounds, culling, budget eviction,
pool reuse, map unload, zero/huge delta time, and invalid modules. Benchmarks
record simulation time, renderer payload size, allocation, peak live particles,
and over-budget behavior.

### Lights and lighting authoring

Lighting crosses editor, world, resource, compiler, and renderer ownership. The
authoring contract should use physical or clearly documented engine units and
avoid storing API-specific renderer objects.

Common fields:

- stable authored ID and type: directional, point, spot, area only when the
  renderer supports it;
- transform/direction, color or temperature+tint, intensity with stated units,
  range or attenuation cutoff;
- spot inner/outer angles or area dimensions;
- static, stationary, or movable policy;
- shadow enable, resolution/budget class, bias policy, and layer/channel mask;
- cookie/IES or equivalent resource references when implemented;
- volumetric, specular, diffuse, and indirect contribution controls only when
  they map to real renderer features;
- build and debug metadata.

The editor renders icons, direction/cone/area/range overlays, channel filters,
and warnings. The compiler validates units, finite values, mobility conflicts,
resource types, shadow budgets, and baked-light participation. `CypherWorld`
produces renderer-neutral visible light records; CypherRender chooses clustering,
shadow allocation, and graphics-API resources.

Start with one directional light plus bounded point lights needed by the first
arena. Add spot lights, shadows, probes, baked lighting/lightmaps, area lights,
and volumetrics in measured stages aligned with renderer capability.

### Visibility and world spatial system

#### Layered visibility

Visibility is not one boolean. The runtime candidate set is reduced by layers:

1. world or streaming-cell residency;
2. authored enable/layer/channel masks;
3. spatial query against the camera frustum;
4. distance and LOD policy;
5. room/portal or precomputed visibility when available;
6. renderer-side occlusion and final draw decisions.

Each stage must permit a conservative fallback that shows too much rather than
incorrectly hiding visible geometry.

#### Visibility first production slice

Create a world object table with transform, bounds, layer mask, resource proxy,
and source-debug ID. Implement a linear frustum query, then a measured static
BVH and dynamic structure with differential tests. Produce immutable
renderer-neutral render, light, decal, particle, and environment candidate
lists. Record tested, accepted, rejected-by-reason, and fallback counts.

#### Rooms and portals

Indoor visibility can later use authored or compiler-derived convex cells and
planar portals. Validate portal planarity, positive area, ownership of adjacent
cells, winding, closure, and reachability. Traversal carries a clipped view
volume through visible portals, tracks visited state carefully, and caps depth
and work. Compiler output may include conservative PVS data. Point and portal
debug files or native Cypher artifacts should show leaks, cells, portal planes,
connectivity, and the current traversal.

Portal/visibility data must not double as collision or gameplay trigger state.
They may share source geometry but produce distinct cooked sections and rules.

### Navigation and pathfinding

Navigation is a cooked world system with editor inputs, deterministic build
products, runtime queries, and diagnostics. It must not be inferred ad hoc from
render meshes during play.

#### Agent profiles and authored inputs

Each agent profile has a stable ID and version plus radius, height, maximum
slope, step height, drop and jump policy, clearance, and any movement-mode
flags. Authored geometry supplies walkable/blocked semantics independently from
visual material. Area types define stable IDs, default traversal costs, and
capability flags. Modifier volumes can replace or multiply area cost, exclusion
volumes remove space, and explicit off-mesh links define endpoints, direction,
radius, cost, required capability, activation policy, and owning object.

TileEditor should derive basic walkable cells and ledges from tile semantics,
then allow explicit exclusions, costs, and links. Mason should accept the same
records from arbitrary world geometry. Every derived input retains source object
and component IDs so a cook error can select and frame its author.

#### Deterministic tiled cooking

The cooker consumes an immutable map revision, agent profile, canonical source
geometry, area/modifier records, and fixed build settings. Settings include cell
and tile size, border expansion, contour simplification, region thresholds,
maximum polygon complexity, and vertical tolerances. The output records source
and cooker versions, target platform, dependency hashes, bounds, tile
coordinates, area IDs, polygons, adjacency, boundary portals, off-mesh links,
and optional debug data.

Tile ownership and border rules must be explicit. Neighboring tiles build with
enough overlapping source data to agree at a seam, while one canonical tile
owns each published boundary. Parallel cooking may change scheduling but not
bytes, polygon/link identity, diagnostics, or path tie-breaking. A tile is
published only after its topology, bounds, area references, reciprocal
connections, and neighbor seams validate. Failed or canceled work preserves the
last good product.

#### Runtime ownership and queries

The navigation runtime owns generation-checked world and tile handles plus
immutable cooked tile data. Its first bounded API should provide:

- project/nearest navigable point with agent, filter, search extents, and
  maximum work;
- path request returning complete, partial, unreachable, canceled, stale, or
  capacity-limited status plus a polygon corridor and optional straight path;
- corridor repair after bounded movement, surface movement constrained to the
  navmesh, and next-corner extraction;
- navigation raycast, reachable test, polygon/area lookup, and bounded random
  point sampling when gameplay requires them;
- include/exclude area flags, per-area cost overrides, ignored links, and a
  query budget on every filter;
- stable query IDs, source-debug IDs, visited-node/work counts, and no pointers
  into mutable runtime storage in returned results.

Tie-breaking, open-set ordering, floating-point tolerances, output truncation,
and partial-path policy are part of the public contract. Synchronous queries
serve short gameplay requests; queued jobs operate on an immutable navigation
snapshot and reject or mark results stale if their world generation changes.

#### Streaming, obstacles, and invalidation

Streaming adds and removes complete validated tiles at declared cell borders.
Requests pin the tile generations they inspect; unload cancels or invalidates
dependent jobs before memory is released. Dynamic doors and temporary blockers
begin as a bounded obstacle/query overlay. Full local rebaking is a later
feature with dirty-tile expansion, dependency tracking, debounce, cancellation,
and atomic multi-tile publication. A moving crowd must not trigger an
uncontrolled navmesh rebuild every frame.

The dependency graph invalidates navigation when source geometry, collision
semantics, agent profiles, area tables, modifier volumes, off-mesh links, or
cooker settings change. Visual-only material and lighting changes do not dirty
navigation unless their schemas explicitly carry navigation semantics.

#### Diagnostics, overlays, and tests

The editor and runtime expose tile bounds, walkable polygons, area colors,
clearance, ledges, islands, border seams, links, blocked links, query start/end,
visited polygons, corridor, corners, stale products, dirty tiles, cook time,
memory, and query-budget exhaustion. Diagnostics identify agent profile, tile,
source object/component, and suggested repair.

Tests cover slopes, steps, clearance, narrow passages, stacked floors, holes,
islands, one-way and disabled links, cost choice, equal-cost tie-breaking,
partial paths, projection failure, tile seams, streaming unload during queries,
dynamic blockers, malformed cooked data, deterministic single/parallel cook,
map unload, and differential queries against a small reference graph. Benchmarks
record cook time, peak memory, product size, tile update cost, query latency,
visited nodes, and concurrent queue pressure on representative arenas.

### Resource and cooker integration

Every authored reference resolves through the resource system to a stable type
and ID. The compiler records dependency ID, source/cooked versions, content hash,
mount provenance, optional/required status, and target variant. Runtime sections
refer to cooked resource IDs, never editor filesystem paths.

Hot reload is a transaction:

1. discover source change;
2. cook into a new immutable result;
3. validate dependencies and runtime compatibility;
4. publish a new resource generation;
5. notify clients to refresh handles or derived state;
6. retire the old generation after users release it.

A failed cook preserves the last good runtime resource and exposes the new
diagnostic. It does not replace working content with an incomplete object.

### Cross-system frame and unload order

A practical first runtime order is:

```text
fixed input sample
  -> gameplay/ECS command phase and bounded navigation requests
  -> navigation-result consumption for the matching world generation
  -> character/physics step and trigger transitions
  -> gameplay event consumption and transform finalization
  -> world spatial update and visibility snapshot
  -> audio listener/emitter command snapshot
  -> particle simulation/update snapshot
  -> immutable render/audio/debug submissions
  -> deferred destruction and event retirement
```

Exact threading can change, but data ownership and snapshot boundaries must not.
Map unload stops new gameplay and navigation requests, invalidates queued
navigation jobs, disables triggers, destroys particle/audio instances, removes
physics colliders/bodies and navigation tiles, removes world proxies, destroys
ECS entities/components, and then releases map resources. Tests should randomize
partial initialization and repeat unload to prove cleanup paths.

## Acceptance gates

### Feature-complete editor command

An editor command is complete only when it has:

- stable ID, localized label, help text, context, enable and checked state;
- menu, context, palette, toolbar, or documented intentional discovery path;
- configurable shortcut behavior with conflict tests when appropriate;
- typed parameters and preflight validation;
- candidate preview for interactive or destructive changes;
- atomic commit, cancellation, undo, redo, no-op behavior, and dirty revision;
- selection and focus policy;
- persistence and migration impact;
- incremental notification and derived-cache impact;
- validation, diagnostics, and repair behavior;
- unit/integration/regression coverage and performance evidence where material.

### Geometry operation

It must define supported input topology, multi-selection behavior, coordinate
space, snapping, tolerance, world bounds, identity preservation, new-face
material/UV policy, invalid-result behavior, fragment handling, preview,
cancellation, undo, serialization, compilation, and invariant tests.

### Authored component

It must have a registered schema, source version and migration, stable object
identity, inspector and multi-edit behavior, visualization/picking, validation,
clipboard and prefab behavior, cooker, cooked version, runtime construction and
destruction, debug state, resource dependencies, unload tests, and missing-data
policy.

### Engine subsystem

It must have a public lifecycle, typed handles, ownership/threading rules,
configuration, limits, allocation policy, errors, debug/stats snapshot,
headless/null testing path, serialization/cooking boundary, integration test,
performance fixture, and clean repeated shutdown.

### File format or cooked section

It must have magic/type identity where binary, version, bounds, endianness,
canonical source syntax, duplicate/unknown-field policy, deterministic writer,
size/recursion/allocation limits, checksums where useful, migration policy,
fuzz target, corrupt/truncated tests, and documentation in the engine reference
manual.

### Build pipeline

It must identify exact input revision and dependency hashes, validate before
execution, support cancellation and dry run, publish atomically, emit structured
diagnostics, reject stale quick fixes, reproduce outputs in CI, and retain last
good products after failure.

### Production map gate

Before Mason or TileEditor calls a map production-ready, the map must:

1. save, reload, migrate, autosave, recover, copy/paste, and undo/redo without
   identity or reference loss;
2. build deterministically from editor, CLI, and CI;
3. load and unload repeatedly in the runtime;
4. spawn the player and required gameplay entities;
5. provide valid collision, movement, doors, and triggers;
6. expose required lighting, visibility, audio, and particle data through real
   runtime systems;
7. report no unsuppressed blocking validation or compiler errors;
8. remain within measured memory, build-time, load-time, and frame budgets;
9. preserve a useful debug path from runtime object back to source ID;
10. survive missing optional resources and reject missing required resources
    with a clear diagnostic.

### Work that does not satisfy a gate

A toolbar button without a document command, an editor icon without a runtime
component, a preview mesh without source persistence, a parser without limits,
a cooker without deterministic output, a collision mesh without queries and
unload, or a subsystem folder containing placeholders does not count as the
corresponding system. These may be valid intermediate scaffolds, but status and
documentation must name them accurately.

## Source index

### Pinned primary references

- [TrenchBroom source at the audited revision](https://github.com/TrenchBroom/TrenchBroom/tree/e53a0ef172e10e62ff24b86b70ca3b6ea865cac4)
- [TrenchBroom 2026.2 manual](https://trenchbroom.github.io/manual/latest/)
- [TrenchBroom GPL-3.0-or-later license](https://github.com/TrenchBroom/TrenchBroom/blob/e53a0ef172e10e62ff24b86b70ca3b6ea865cac4/LICENSE.txt)

### High-value source paths

| Subject | Pinned source area |
| --- | --- |
| Actions, menus, contexts, shortcut state | `lib/TbUiLib/include/ui/Action*.h`, `lib/TbUiLib/src/Action*.cpp`, `ActionManager.cpp` |
| Shortcut settings and generated manual | `KeyboardShortcut*`, `KeyboardPreferencePane.cpp`, `app/DumpShortcuts`, manual CMake and JavaScript helpers |
| Input and controller routing | `lib/TbAppLib/include/ui/InputState.h`, `Tool.h`, `ToolController.h`, `ToolChain.h`, `GestureTracker.h`; matching sources |
| 2D/3D controller order | `lib/TbUiLib/src/MapView2D.cpp`, `MapView3D.cpp`, `ToolBox.cpp`, `ToolBoxConnector.cpp` |
| Document/model root | `lib/TbMdlLib/include/mdl/Map.h`, `Node.h`, `WorldNode.h`, `LayerNode.h`, `GroupNode.h`, `EntityNode.h`, `BrushNode.h`, `PatchNode.h` |
| Commands and history | `lib/TbMdlLib/include/mdl/Command*.h`, `CommandProcessor.h`, transaction and repeat-stack files |
| Spatial and semantic indexes | `WorldNode`, `NodeTree`, `NodeIndex`, entity-link and node-handle manager files |
| Picking and selection | pick-result, node/face query, selection-change, and map-view selection controller files |
| Convex geometry | `Brush.h`, `BrushGeometry.h`, `BrushBuilder.h`, `Polyhedron.h`, `Polyhedron3.h`, and `lib/VmLib` plane/intersection/hull helpers |
| Geometry operations | brush operations, CSG, clipping, extrude, sweep, scale, shear, and vertex/edge/face tool files plus tests |
| Materials and UVs | brush-face attributes, projection, face inspector, material browser/manager, and UV tool/controller files |
| Entities and definitions | entity/property/definition/model/link manager files; FGD/DEF/ENT parsers and tests |
| Groups, layers, instances | group/layer model operations, linked-group update, protected-property, and outliner/layer UI files |
| Visibility and validation | `EditorContext`, tag/filter files, validators, issue generators, quick fixes, Issues UI |
| Clipboard and serialization | map readers/writers, parser status, clipboard serializer/parser, export and filesystem writers |
| Autosave and recovery | `Autosaver`, document save/revert, backup rotation, crash recovery UI |
| Assets and VFS | `TbFsLib`, game filesystem/config, material/model/resource managers and loaders |
| Build and launch | `CompilationTask`, `CompilationProfile`, parsing/writing, `CompilationRunner`, dialog/editors, engine-profile files |
| Rendering and invalidation | `lib/TbRenderLib` `MapRenderer`, `ObjectRenderer`, brush/entity/patch renderers, `RenderContext`, `RenderBatch` |
| Tests | each library's `test/src` tree, with the largest geometry/document coverage under `lib/TbMdlLib/test/src` |

### Companion exhaustive inventories

[trenchbroom_geometry_algorithms.md](trenchbroom_geometry_algorithms.md)
contains the source-level geometry and map-editing audit: 99 implementation and
test files, 310 checked immutable source references, all convex/CSG/editing/UV
algorithms, tolerance and rollback behavior, complexity analysis, and a Cypher
implementation/test plan.

[trenchbroom_ui_command_inventory.md](trenchbroom_ui_command_inventory.md)
contains the exact menu hierarchy, toolbars, view bar, panels, inspectors,
preferences, dialogs, context menus, status surfaces, tool behavior, validators,
compile/launch UI, 67-row pointer and UV gesture matrix, and all 179 static
action rows. Each row records its registry, user-facing command, default
shortcut, and pinned source line. The preceding analysis explains context,
enable/check policy, and execution behavior without reproducing internal source
expressions as a bulk data table.

[editor_comparison_beyond_trenchbroom.md](editor_comparison_beyond_trenchbroom.md)
contains the official-source comparison with Hammer/Source 2 and Dota's tile
editor, GtkRadiant/Q3Radiant, J.A.C.K., Blender, and Unity ProBuilder. It records
the full capability matrix, editor-specific evidence, missing semantic systems,
adoption/defer/reject decisions, geometry-command contract, staging, acceptance
criteria, and all 53 unique primary-source links.

### Audit reproducibility

The repository-wide count covered C/C++ headers and implementations under
`app/` and `lib/`, separated production and test trees, and counted the manual
independently. The UI appendix indexed 517 directly relevant files and 84,368
lines and mechanically checked 454 parseable file/line references for existence
and range. Source counts will change upstream; the commit hash makes every claim
in this snapshot re-checkable.

When updating this research, record a new upstream commit, regenerate counts,
diff the action registry and manual headings, rerun source-reference checks, and
review behavioral changes before altering Cypher requirements. Do not silently
mix evidence from different upstream revisions.
