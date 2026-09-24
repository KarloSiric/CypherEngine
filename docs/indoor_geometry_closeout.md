<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/indoor_geometry_closeout.md
//  Purpose: Records the indoor geometry audit and the next testable milestones.
//  Details: Distinguishes implemented kernel operations from document integration
//           and complete editor workflows. Research informs original design.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Indoor geometry: evidence and completion gates

Audit date: 2026-09-24. This is a working-tree snapshot, including uncommitted
implementation work, not a claim about the last release or remote CI.

The immediate goal is reliable indoor map construction shared by TileEditor and
Mason. Outdoor terrain authoring follows this milestone. A larger line count or
a populated directory is not a completion criterion: an operation must preserve
geometry, identity, attributes, selection, undo, persistence, and cooked output.

This supplements [the geometry implementation plan](../src/CypherEditor/Geometry/IMPLEMENTATION_PLAN.md),
[map authoring architecture](map_authoring_and_mason.md), and
[Source 2 tooling research](source2_tooling_reference.md). It does not change the
runtime-first engine priorities or authorize a separate frontend implementation.

## Lessons from Source and Hammer

Source 1 and Source 2 must be distinguished. Valve's Source SDK 2013
[VBSP map reader](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/utils/vbsp/map.cpp)
loads plane-defined brush sides and builds their windings. Its separate
[VBSP](https://github.com/ValveSoftware/source-sdk-2013/tree/master/src/utils/vbsp),
[VVIS](https://github.com/ValveSoftware/source-sdk-2013/tree/master/src/utils/vvis),
and [VRAD](https://github.com/ValveSoftware/source-sdk-2013/tree/master/src/utils/vrad)
tools illustrate distinct compilation responsibilities. Editable source and
runtime products serve different purposes.

Source 2 Hammer documents editable vertices, edges, faces, extrusion, and mesh
operations in [Mesh Editing 1](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Basic_Construction/Mesh_Editing_1)
and [Mesh Editing 2](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Basic_Construction/Mesh_Editing_2).
Its [workplane documentation](https://developer.valvesoftware.com/wiki/Source_2/Docs/Level_Design/Basic_Construction/Mesh_Editing_4#The_Workplane)
shows why transformations need a coordinate frame selected by the author, not
only global XYZ axes. These are documented workflow references, not a complete
specification of every current Hammer branch or evidence of its private internals.

Cypher's proposed improvements are measurable design targets:

| Target | Consequence for the geometry core | Evidence of completion |
| --- | --- | --- |
| Predictable editing | Shared selection, pivot, workplane, preview, commit, and cancel contracts | The same edit behaves consistently through different hosts and viewports |
| Dependable CSG | Explicit input domain, degeneracy policy, provenance, and result adoption | Valid result or a useful failure with the original document intact |
| Continuous feedback | Immutable cooked snapshots and dependency invalidation | Local edits rebuild affected products; stale jobs cannot overwrite a newer document |
| Explainable failures | Diagnostics carry object/component IDs and numerical context | The host can select the exact offending face and explain the remedy |
| Reliable reuse | One Qt-free library for tools, compilers, and editor hosts | Headless tests reproduce an editor operation without Qt or a renderer |
| Content continuity | Versioned persistence and attribute-aware operations | Cut, undo, reload, and cook preserve material/UV intent and stable identity |

These targets do not establish that Cypher already exceeds Source. Performance
and workflow claims require benchmarks and completed user tasks. Runtime
visibility, lighting, collision, entity, and audio products remain separate
integration milestones; geometric correctness alone cannot prove playability.

## Current indoor capability inventory

The earlier fifteen-item missing-feature list is partly outdated. The following
files are implementation evidence; the limitations remain part of the contract.

| Area | Implemented now | Still required for the intended indoor workflow |
| --- | --- | --- |
| Primitive/brush construction | Brush generators; convex clipping, subtraction, hollowing, and constrained union | Atomic adoption of multiple result objects; richer authored recipes when needed |
| Component transforms | Selected vertex/edge/face affine edits in `Operations/Modeling/CypherGeometry_MeshSourceComponents.cpp` | Unified pivot/workplane commands and host interaction; current transform rejects non-positive determinant |
| Knife | Multi-face path implementation in `Representations/Mesh/CypherGeometry_MeshKnife.*` | Interior endpoints and repeated-face paths; continuous authoring and preview integration |
| Face creation | Add-face/boundary operations with existing and new vertices | Complete continuous Poly Pen interaction through the command layer |
| Bevel | Multiple selected edges and 1–64 segments in `Representations/Mesh/CypherGeometry_MeshBevel.*` | Broader corner/boundary support; current reflex, open-boundary, and junction restrictions remain |
| Weld/separate | Constrained connected-vertex welding and face detachment into shells | Arbitrary target weld, cross-object merge, and extraction into document objects |
| Bridge | Equal-size boundary-loop bridge | Unequal-loop correspondence, lofting, and associated attribute policy |
| Modeling operations | Extrude, inset, loop cut, and other topology operations | Loop slide and mesh solidify; brush hollowing is not mesh solidify |
| General mesh CSG | Convex mesh conversion through the brush path | Concave operands, coplanar corefinement, robust reconstruction, and multiple fragments |
| Surface authoring | Material assignment, projections, smoothing/hard edges, seam storage | Seam editing workflow, islands, unwrap, packing, and texel density |
| Spatial queries | Brush bounds index and queries | The current linear index is not an editable multi-representation BVH |
| Document lifecycle | Brush/mesh roots, transactions, snapshots, serialization, and cooking | Multi-object structural commands and complete integration for the other representations |
| Validation | Unit, randomized, allocation-failure, and integration tests | Wider architectural fixtures, repeated-edit sequences, coverage-guided fuzzing, and measured performance |

The staged `Csg/` directories remain design documents. The working convex CSG
implementation lives under `Operations/CSG/`; empty staged directories do not
mean no Boolean code exists. Conversely, convex Boolean tests do not establish
arbitrary concave mesh support. `MeshBooleans.cpp` still rejects multi-fragment
subtraction in its current single-mesh result path.

Backend implementation is also distinct from an exposed editor tool: this audit
found no TileEditor use of the `MeshSourceEdit` or `GeometryMeshTransaction`
APIs. Do not mark a modeling workflow complete solely because its kernel test
passes.

## New architectural regression tests

Added [CypherGeometry_IndoorWorkflow_Tests.cpp](../tests/CypherEditor/Geometry/Operations/CypherGeometry_IndoorWorkflow_Tests.cpp)
to the geometry contract target. The tests subtract door and window openings
from a thin box wall at the origin and translated coordinates. They check:

- analytical point membership with exactly one owning fragment inside the solid;
- clear ray passage through the opening and pickable jamb/lintel surfaces;
- outward cooked triangle winding, valid indices, and resolvable face IDs;
- total solid volumes of 17 and 18 cubic units for the door and window fixtures;
- per-fragment save/load/save equality and repeatable cooked content hashes;
- atomic rejection when raw fragment provenance collides in one document,
  including live/claimed identity membership and the ID allocation sequence.

The last test deliberately characterizes the existing boundary. A passing test
does not mean the whole doorway operation can already be committed as one
document edit. Each fragment is independently cooked and round-tripped because
the missing adoption stage must be designed explicitly. Materials and UV
transfer are not established by these fixtures.

## First blocker: adopting a CSG result into a document

`BrushCSG_TrySubtract` explicitly preserves original side IDs and attribute
indices on inherited sides. Several output fragments can descend from the same
input side. `GeometryDocument_TryAddBrush` correctly requires unique live IDs.
Consequently, inserting a first fragment succeeds but inserting a second with a
shared side ID returns `IDENTITY_CONFLICT` without changing the document.

Neither weakening the registry nor blindly renumbering faces is sufficient.
The raw CSG result and the authored document have different identity contracts.
A face occurrence needs its own persistent identity, while provenance records
which input face or cutter generated it. One source face can have many children.

The existing deterministic ID remap is one-to-one and registers IDs immediately.
The existing brush replacement identity helper and brush transaction target one
replacement brush. They are useful mechanisms, but are not a one-to-many CSG
adoption transaction.

The next learning/implementation slice should define this operation:

```text
prepare Boolean edit(document revision, operands, raw result)
    validate operation intent and operand revisions
    create private output objects and prepare unique destination IDs
    record each output face's operand-qualified ancestry
    transfer material and UV data from the correct operand's attribute store
    build one-to-many selection remaps and document changeset
    validate geometry, identities, attributes, and output budgets
    prepare undo payload, registry transition, and dirty spatial regions

commit prepared edit
    reject a changed baseline revision
    publish the complete prepared state without allocation
    advance the document revision once

cancel or fail before commit
    discard staged work; leave the authored document unchanged
```

Clip sides currently receive fresh IDs and a cutter attribute index. Their
origin needs an explicit record while computing the result; a destination ID
alone cannot recover it afterward. Attribute indices must be qualified by their
owning store: index 0 in operand A need not mean the same material as index 0 in
operand B. A plane match is not a general substitute for provenance, especially
with coincident or repeated geometry.

Acceptance tests for this slice:

1. Doorway subtraction replaces one wall with all fragments in one document.
2. All live identities are unique; every result face has valid ancestry.
3. Material and UV checks distinguish the wall from cutter-origin faces.
4. Undo restores the exact original; redo restores the same output identities.
5. Save/reload, selection resolution, picking, and cook work on the entire result.
6. Allocation failure, cancellation, invalid input, and stale revisions leave the
   document unchanged. No partially published fragment list is observable.
7. Empty, unchanged, and split results have explicitly defined command semantics.

## Remaining indoor completion gates

1. **Document command integrity:** close the adoption boundary above; use the
   same atomic batch model for delete, duplicate, separate, merge, and clipboard
   operations. Keep persistent IDs separate from ancestry and runtime handles.
2. **Common modeling commands:** bind existing component operations to shared
   selection, snapping, pivots, workplanes, preview, commit/cancel, and undo.
   Complete missing loop slide, solidify, knife paths, weld/extract, unequal
   bridge/loft, and documented bevel cases with explicit supported domains.
3. **General CSG:** broad phase, robust predicates and intersection construction,
   corefinement, coplanar overlay, classification, reconstruction, and attribute
   transfer. Test concavity, containment, disjoint sets, cavities, multiple shells,
   touching/coincident surfaces, and repeated operations. Define whether
   non-manifold results are rejected; never silently publish invalid topology.
4. **Surface continuity:** complete seam/island/unwrap/packing/density operations
   and test materials/UVs through every topology-changing command. Lightmap
   charts are a separately specified compiler product, not an implicit use of
   author texture UVs.
5. **Queries and robustness:** geometry-aware snapping/picking acceleration,
   dirty-region updates, golden architectural cases, randomized operation
   sequences, malformed input, allocation failure, determinism, and performance.
6. **Indoor authoring acceptance:** a room, corridor, doorway, window, staircase,
   arch, trimmed/beveled junction, and material boundaries survive editing,
   undo/redo, saving/reopening, picking, validation, and cooking together.
   First run this headlessly; then replay it through the shared TileEditor/Mason
   command adapter when the host is connected.

Kernel correctness comes before aggressive optimization. In particular,
double-precision vectors and robust classification do not by themselves make
repeated Boolean intersections robust. The CGAL
[corefinement documentation](https://doc.cgal.org/5.3/Polygon_mesh_processing/index.html)
distinguishes exact predicates from exact constructions: correct topological
decisions alone cannot guarantee an intersection embedding free of rounding
problems. Cypher needs an explicit policy for constructed coordinates, snapping,
degeneracies, and reproducible topology. This is a design lesson, not a decision
to import CGAL or assume one global epsilon solves every scale.

Benchmark candidate hot paths using fixed inputs and reported compiler/build
settings: selection and ray queries, large structural batches, Boolean candidate
pairs versus actual intersections, cook time, allocation counts, and peak memory.
Compare relative to a recorded baseline; do not invent latency targets or claim
optimization from algorithm names alone.

## Boundary with later work

Indoor geometry completion enables renderer and gameplay tests; it does not
depend on finishing an audio engine or entity scripting first. Actual playable
level acceptance later requires the runtime collision, rendering, movement,
entity, and other products needed by that game.

Retain extensible source references and representation-independent command
contracts now. Defer terrain sculpting, vegetation placement, tiled heightfield
streaming, terrain LOD, and broader outdoor authoring until the indoor gate is
closed. Curves and patches needed for indoor architecture can enter the indoor
scope, but they must receive the same document/undo/serialization/cook guarantees
as brushes and meshes. A primitive generator or isolated surface evaluator does
not supply that integration by itself.

## Verification record

The audit's commands rebuild the `cypher_editor_geometry_contract_tests` target
before running its executable with `--reporter compact`. Build directories are
`build` (Debug), `out/build/release`, and `out/build/asan-ubsan` (Debug with address
and undefined-behavior sanitizers).

| Local configuration | Result on 2026-09-24 |
| --- | --- |
| Debug | 1,058 cases / 271,046 assertions passed |
| Release | 1,058 cases / 271,046 assertions passed |
| ASan + UBSan | 1,058 cases / 271,046 assertions passed; no sanitizer failure reported |

The three new indoor cases account for 3,583 assertions. The sanitizer run used
`ASAN_OPTIONS=halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.

These are local geometry-library checks. They do not establish a full engine
build, GUI behavior, leak-sanitizer coverage on macOS, or remote CI status.
