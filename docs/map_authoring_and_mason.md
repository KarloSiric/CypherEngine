<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/map_authoring_and_mason.md
//  Purpose: Defines the Cypher data, map-authoring, world-compilation, and
//           Mason editor architecture.
//  Details: This document separates editable source data from cooked runtime
//           resources and records the intended workflow from CYKV through Mason,
//           CypherMapCompiler, CypherWorld, and CypherPak delivery.
//
//  History:
//  - Created by Karlo Siric on 2026-08-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Cypher Map Authoring And Mason Architecture

## Status

This document records the agreed long-term direction for:

- `CYKV`, the general Cypher data format
- editable and cooked asset formats
- `.cymap` map source documents
- `.cymap_c` compiled runtime worlds
- the role of brushes, meshes, BSP, visibility, collision, and lighting
- `CypherMapCompiler` and related command-line tools
- `Mason`, the long-term Qt 6 editor application
- asset validation, versioning, diagnostics, testing, and packaging

It is an architectural target, not a claim that these systems currently exist.
Names marked as provisional must be reviewed when their subsystem reaches the
design stage.

This document is authoritative for the map pipeline. More general engine build
order remains in `master_plan.md`, `development_phases.md`, and
`toolchain_plan.md`.

## Core Decision

CypherEngine will use a source-and-cooked content pipeline:

```text
Human-authored source data
        |
        v
CYKV-backed Cypher source formats
        |
        v
Mason and command-line tools
        |
        v
Validation and resource compilation
        |
        v
Cooked binary runtime resources
        |
        v
CypherFileSystem / CypherResource / CypherWorld
        |
        v
Loose development files or .cypak archives
```

For maps specifically:

```text
facility.cymap
        |
        v
CypherMapCompiler
        |
        v
facility.cymap_c
        |
        v
CypherWorld runtime
```

The editable `.cymap` file will be encoded using CYKV. Mason will edit a typed
in-memory map document and serialize that document to CYKV. It will not edit the
text file through ad hoc string replacement.

The runtime will normally load `.cymap_c`. Shipping builds must not repeatedly
parse and transform a large text map when the same work can be performed once by
an offline compiler.

## Terminology

The normative language rules live in [formats/CYKV.md](formats/CYKV.md). This
document uses those names and does not define a second data language.

### CYKV

`CYKV`, or Cypher KeyValues, is the general typed hierarchical data language.
Tier1 provides its semantic document, parser, and deterministic writer. Tier2
provides schema descriptors, registry lookup, and bounded validation.

CYKV is comparable in purpose to JSON5, Valve KeyValues, KV3, or a text encoding
of a typed data model. It is not itself a map format.

### CYKV Semantic Document

The CYKV semantic document is the owned in-memory tree of objects, arrays, keys,
and typed values. It intentionally discards comments and exact source spelling.
A future Mason lossless syntax tree will preserve authoring trivia and map source
ranges to semantic nodes.

### `.cymap`

`.cymap` is an editable map source document with a map-specific schema. Its
serialized representation uses CYKV, but its meaning comes from the CypherMap
schema and map compiler.

### `.cymap_c`

`.cymap_c` is the compiled runtime world resource. It is binary, chunked,
versioned, validated, and designed for efficient loading. It is not expected to
preserve every piece of Mason-only editing state.

### Mason

Mason is the long-term Qt 6 editor product. It hosts multiple workspaces, with
map editing as its central world-authoring workflow. Mason consumes public engine
and tool contracts. Qt-specific types must not leak into runtime data formats or
CypherCommon public runtime structures.

### BSP

`BSP` can mean three different things:

1. a binary-space-partitioning algorithm
2. a BSP tree used to organize runtime space
3. a monolithic `.bsp` map file containing many kinds of engine data

CypherEngine may use the first or second where they solve a measured problem. It
will not make the third the universal world architecture.

## Historical Lessons

### Quake And GoldSrc

The classic pipeline used editable brush maps and compiled them into BSP files.
The BSP commonly combined:

- rendered world surfaces
- planes, nodes, and convex leaves
- collision hulls
- entity text
- visibility data
- lightmaps
- texture or resource references

This was coherent and extremely effective for constrained indoor environments.
It also coupled many world responsibilities to one representation.

### Source 1

Source 1 retained the brush-oriented workflow:

```text
.vmf -> VBSP -> VVIS -> VRAD -> .bsp
```

Its `.bsp` evolved into a general lump container rather than merely a spatial
tree. This provided an effective Half-Life-style map pipeline, but arbitrary
mesh authoring, large open spaces, modern streaming, and independent resource
evolution remained constrained by the legacy world model.

### Source 2

Source 2 moved to `.vmap` editable documents, direct polygon-mesh authoring,
compiled `_c` resources, precomputed visibility data, separate physics data,
modern lighting data, and resource packaging. It did not replace BSP with one
single universal algorithm. Instead, it separated the responsibilities that old
BSP files bundled together.

### CypherEngine Lesson

CypherEngine should preserve the strongest parts of both generations:

- fast grid-based blockout using convex brushes
- direct vertex, edge, and face editing
- arbitrary imported mesh instances
- deterministic offline compilation
- precomputed visibility where useful
- collision data independent from render geometry
- a chunked runtime world resource
- a WYSIWYG editor using the real engine

This is a hybrid world pipeline, not a rejection of BSP algorithms and not a
commitment to a classic BSP-only engine.

## Design Principles

### Source Data And Runtime Data Are Different Products

Source data prioritizes:

- readability
- stable serialization
- version-control diffs
- editor fidelity
- recoverability
- comments and source locations
- forward migration

Runtime data prioritizes:

- predictable loading
- contiguous arrays
- compact indexes
- low allocation counts
- platform-ready values
- direct renderer and physics consumption
- optional memory mapping
- per-chunk compression

Trying to make one file ideal for both purposes weakens both.

### The Runtime Contract Comes Before The Editor

Mason must edit real engine-owned concepts. The required order is:

1. define the runtime world data that the game needs
2. load a minimal cooked world
3. prove rendering, collision, entities, and resource references
4. define the source document that can produce that runtime data
5. build the compiler
6. build Mason as a visual client of those systems

### One Responsibility Per Representation

Render meshes, collision meshes, navigation, visibility, entity spawn data, and
editor topology may be derived from the same source object, but they are not the
same data structure.

### Determinism Is A Requirement

The same source document, compiler version, target profile, and dependency set
must produce byte-identical cooked output where practical. Determinism supports:

- reliable caching
- reproducible builds
- useful binary comparisons
- multiplayer content verification
- package integrity checking
- trustworthy CI

### Stable IDs, Not Pointers

Serialized references must use stable IDs and normalized virtual resource paths.
Raw pointers, container indexes that change after reordering, and compiler ABI
details must never become persistent identifiers.

## CYKV Responsibilities

CYKV 1 provides the following language-level capabilities.

### Required Value Types

- null or explicitly absent value, if justified by the schema model
- boolean
- signed integer
- unsigned integer
- floating-point number
- UTF-8 string
- array
- object/key-value collection

Vectors, colors, transforms, paths, and asset references should normally be
schema-level types built from these primitives rather than hard-coded lexer
tokens for every engine concept.

### Required Syntax Behavior

- single-line comments
- block comments
- escaped strings
- explicit numeric rules
- predictable trailing-comma policy
- useful line and column locations
- deterministic canonical writer
- bounded nesting and token limits
- no ambiguous implicit type coercion

The exact punctuation and spelling are fixed by the CYKV 1 specification. A
breaking grammar change requires a new CYKV language version.

### Includes And References

CYKV 1 has no textual include directive. Arbitrary textual inclusion is not the
map-composition mechanism.

Maps should compose content through:

- resource references
- prefab instances
- map layers or submaps
- explicit inheritance where a schema allows it

This preserves dependency tracking and avoids hidden textual substitution.

### CYKV Is Not A Gameplay Language

CYKV describes data. Lua or another dedicated scripting runtime handles gameplay
behavior. CYKV may describe events, properties, graphs, and script references,
but it must not grow uncontrolled programming-language semantics.

### Parsing Pipeline

```text
bytes
  -> UTF-8 validation
  -> lexer
  -> tokens with source locations
  -> parser
  -> CYKV semantic document
  -> schema validation
  -> typed asset/map object
```

The lexer, token reader, parser, schema validator, and typed decoder are separate
layers. A syntax error, schema error, missing dependency, and map compile error
must remain distinguishable.

### Parser Safety

All CYKV readers must enforce configured limits for:

- source byte count
- token count
- nesting depth
- string length
- array and object entry count
- integer and floating-point range
- total semantic node count
- decoded string and binary byte count

Malformed or hostile data must produce diagnostics, not unbounded allocation or
stack recursion.

## Format Families

The following names are the current direction. A subsystem may refine a name
before its first stable format version, but source and cooked forms should remain
visibly related.

### General And Project Data

| Purpose | Editable/source | Cooked/runtime |
| --- | --- | --- |
| Generic structured data | `.cykv` | specialized format or none |
| Engine/user command configuration | `.cycfg` or `.cfg` | none |
| Project definition | `.cyproject` | none |
| Resource manifest | `.cymanifest` | compiled resource index |

Command-oriented configuration should remain separate from structured CYKV
assets. A configuration file can execute a constrained sequence of console/CVar
assignments without forcing every asset format to behave like a script.

### World And Gameplay Data

| Purpose | Editable/source | Cooked/runtime |
| --- | --- | --- |
| Map/world | `.cymap` | `.cymap_c` |
| General scene | `.cyscene` | `.cyscene_c` |
| Prefab | `.cyprefab` | `.cyprefab_c` or folded into owning world |
| Navigation authoring | `.cynav` | `.cynav_c` |
| Mission/objective graph | `.cyflow` | `.cyflow_c` |
| Gameplay/UI data | `.cykv` with schema | subsystem-specific cooked data |

### Render And Asset Data

| Purpose | Editable/source | Cooked/runtime |
| --- | --- | --- |
| Texture recipe/metadata | `.cytex` | `.cytex_c` |
| Material | `.cymat` | `.cymat_c` |
| Shader metadata/recipe | `.cyshader` | `.cyshader_c` |
| Mesh | `.cymesh` | `.cymesh_c` |
| Model assembly | provisional | provisional cooked model |
| Skeleton | `.cyskel` | `.cyskel_c` |
| Animation clip | `.cyanim` | `.cyanim_c` |
| Animation graph | provisional | provisional cooked graph |
| Particle system | provisional `.cyparticle` | `.cyparticle_c` |
| Font recipe | `.cyfont` | `.cyfont_c` |

Large imported source images, FBX/glTF files, WAV files, and similar interchange
assets remain ordinary external source files. Cypher source recipes describe how
to import and cook them.

### Simulation And Presentation Data

| Purpose | Editable/source | Cooked/runtime |
| --- | --- | --- |
| Physics setup | `.cyphys` | `.cyphys_c` |
| Sound recipe/event | `.cysnd` | `.cysnd_c` |
| Audio mix graph | provisional | provisional cooked mix data |
| UI layout/style | provisional `.cyui` | `.cyui_c` |
| Cinematic sequence | provisional `.cycine` | `.cycine_c` |

### Distribution

| Purpose | Format |
| --- | --- |
| Cypher package archive | `.cypak` |
| Derived-data cache | implementation detail, not a source asset |
| Build reports and manifests | CYKV, JSON, or text as appropriate |

`.cypak` is the existing CypherPak archive extension. The package file is a
delivery container, not an authoring format and not a replacement for the VFS.

## `.cymap` Source Document

### Required Top-Level Data

A map source document should be capable of representing:

- format and schema versions
- stable map identity
- map name and descriptive metadata
- coordinate-system and unit declarations
- world settings
- object hierarchy
- layers and shared visibility groups
- convex brushes
- editable polygon meshes
- entities and components
- imported model instances
- prefab instances
- triggers and gameplay volumes
- splines and paths
- lights and probes
- audio emitters and zones
- navigation hints
- visibility hints and contributors
- map-specific build overrides
- shared editor annotations

The exact schema must be introduced incrementally. A field should not exist only
because another engine happens to have a similarly named field.

The following is a non-normative shape example. It illustrates the relationship
between CYKV and the map schema; it does not freeze CYKV punctuation or spelling.

```text
cymap
{
    format_version = 1
    schema_version = 1
    map_id          = guid("...")
    name            = "research_facility"

    world =
    {
        units   = "centimeters"
        up_axis = "z"
    }

    objects =
    [
        entity
        {
            id       = 1001
            name     = "player_start"
            class    = "info_player_start"
            position = [ 0.0, 0.0, 64.0 ]
        },

        prefab_instance
        {
            id     = 1002
            source = resource("prefabs/security_door.cyprefab")
        }
    ]
}
```

### Stable Object Identity

Every addressable map object requires a stable ID. Stable IDs are used by:

- hierarchy references
- entity input/output connections
- undo and redo commands
- prefab overrides
- selection sets
- diagnostics
- merge and migration tools
- external references when explicitly supported

The eventual choice between 64-bit generated IDs and 128-bit GUIDs remains open.
The format must never depend on memory addresses.

### Shared Map Objects

The expected object categories include:

```text
map root
group
layer
brush
editable mesh
entity
model instance
prefab instance
volume
spline/path
editor annotation
```

Lights, sounds, triggers, and gameplay objects should generally use the entity
and component/property system rather than each becoming an incompatible special
object hierarchy.

### External Resource References

`.cymap` should reference imported and reusable assets rather than embedding all
of their data:

- model instances reference model or mesh resources
- surfaces reference materials
- audio emitters reference sound resources or events
- scripts reference Lua modules
- prefabs reference `.cyprefab`
- particle emitters reference particle resources

References use normalized VFS paths and, once the resource system supports them,
stable resource IDs and dependency metadata.

### Embedded Geometry

Geometry authored directly in Mason may be embedded in `.cymap` because its
editable topology is part of the map source. Imported high-density models should
remain external resources.

If map files become too large, the solution is explicit submaps, layers, and
prefabs, not opaque untracked binary blobs inserted into CYKV.

### Personal Editor State

Personal state must not cause noisy shared map changes. Use a local sidecar such
as:

```text
facility.cymap
facility.cymap.user
```

The shared `.cymap` may contain intentional team-visible data such as named
layers and annotations. The `.cymap.user` sidecar may contain:

- viewport cameras
- current selection
- hidden local objects
- open panels
- local bookmarks
- recent tool state

The sidecar is normally excluded from source control.

## Geometry Model

### Convex Brushes

Brushes provide the fastest workflow for rooms, corridors, stairs, walls,
triggers, blockouts, and sealed indoor spaces.

An editable brush should preserve:

- defining planes or equivalent convex representation
- per-face material assignments
- per-face texture axes and UV settings
- face flags
- stable brush and face IDs where required

Brushes must remain convex. Concave construction is represented by multiple
brushes or converted to an editable mesh.

### Editable Polygon Meshes

Mason should support direct vertex, edge, and face editing. A half-edge or
equivalent topology structure is a strong candidate because it supports:

- adjacency queries
- edge loops
- extrusion
- inset
- bevel
- splitting and welding
- face deletion and creation
- non-triangle source faces
- robust selection propagation

The editor topology is not the runtime mesh. Compilation triangulates and packs
the final geometry.

### Imported Asset Instances

Reusable art should normally be authored in external DCC software, cooked into
Cypher resources, and placed as instances. Mason may expose transforms,
materials, collision modes, LOD choices, and prefab overrides without duplicating
the imported mesh inside the map.

### Terrain And Specialized Geometry

Terrain, foliage, water, ropes, decals, and procedural geometry should be added
as distinct authored systems only when the game requires them. They should
compile into the same runtime world contract where possible without forcing the
base brush and mesh representations to absorb every specialized behavior.

### Numerical Policy

Before format version 1 is fixed, the project must decide:

- handedness
- up axis
- world unit and scale
- maximum authored coordinates
- editor/compiler precision
- runtime precision
- geometric epsilon policy
- grid and angle snapping rules

Editor and compiler geometry may use double precision while the runtime uses
carefully bounded floating-point data. This is a policy decision, not an
automatic requirement.

## Role Of BSP

CypherEngine will not use `.cybsp_c` as a required parallel map product.

BSP remains available as an internal technique for:

- brush constructive solid geometry
- convex-space classification
- portal generation
- indoor visibility
- point and volume queries
- selected static collision workflows

If BSP-derived data is useful, it can appear as:

- an intermediate compiler artifact
- an optional `.cymap_c` chunk
- a debugging visualization
- a separately cached build product

The runtime renderer must not assume that every world is a classic BSP tree.
Open areas, arbitrary meshes, streaming regions, and future terrain need other
spatial representations.

## CypherMapCompiler

### Responsibilities

`CypherMapCompiler` transforms a validated source document into runtime-ready
world data.

```text
read .cymap
  -> parse CYKV
  -> validate schema and versions
  -> migrate supported older source versions
  -> resolve resource and prefab dependencies
  -> flatten or preserve hierarchy as required
  -> normalize transforms and coordinate data
  -> validate brushes and mesh topology
  -> perform brush CSG where required
  -> remove hidden/internal surfaces where safe
  -> triangulate editable faces
  -> generate normals, tangents, UV data, and material batches
  -> optimize render geometry
  -> generate collision representation
  -> build visibility data
  -> gather or build lighting data
  -> build or attach navigation data
  -> compile entity spawn records
  -> partition world/streaming regions
  -> write deterministic .cymap_c chunks
  -> emit dependency and diagnostic reports
```

Not every stage must exist in the first usable compiler. The pipeline should
make each stage explicit so later stages do not become hidden side effects.

### Build Profiles

The compiler should support named profiles such as:

- fast development compile
- full validation compile
- release compile
- dedicated-server compile
- editor preview compile

A profile controls work such as lighting quality, visibility generation,
collision detail, debug chunks, compression, and target platform.

### Incremental Compilation

Mature builds should avoid recompiling unaffected data. Incremental compilation
requires:

- content hashes
- compiler and schema version hashes
- explicit dependency graphs
- per-stage cache keys
- dirty object or region tracking
- deterministic stage outputs

Correct full compilation comes first. Incremental compilation is added after the
full dependency model is trustworthy.

## `.cymap_c` Runtime Resource

### Binary Contract

The cooked file should use the shared Cypher binary skeleton:

```text
magic
format version
header size
endian marker
target/platform flags
format/build flags
content and dependency hashes
chunk table
aligned chunks
optional per-chunk compression
optional debug/source mapping data
```

Do not write compiler C++ structs directly to disk. Padding, alignment, compiler
ABI, pointer size, and endianness must not define the file format.

### Candidate Chunks

- string table
- dependency table
- world settings
- spatial regions
- render vertices and indexes
- material batches
- static object instances
- entity spawn records
- collision data
- visibility cells and sets
- lighting references or baked data
- probes and reflection data
- navigation data or references
- audio zones
- optional editor/debug names
- optional source-object mapping for diagnostics

Chunks permit independent format evolution and allow dedicated-server builds to
omit renderer-only data.

### Runtime Loading

The runtime loader should:

1. validate the header before trusting offsets
2. validate all chunk bounds and arithmetic
3. verify required versions and feature flags
4. resolve dependencies through CypherResource and the VFS
5. load or map chunks according to platform policy
6. construct runtime-owned handles and arrays
7. reject partial invalid state cleanly
8. expose useful load diagnostics

Future streaming should operate on explicit world regions or cells, not by
assuming the entire map must be resident as one monolithic object.

## Mason Product Architecture

### Product Direction

Mason is an all-in-one editor shell with independently testable workspaces. It
should eventually host:

- Map Editor
- Material Editor
- Model Viewer and Model Editor
- Animation Editor
- Particle Editor
- Physics Test Editor
- AI and Navigation Editor
- Audio Editor and Mixer
- Objective and Flow Graph Editor
- Cinematic/Sequence Editor
- Asset Browser and resource inspector
- Console, logs, diagnostics, and profiler views

These workspaces may also be exposed through focused launch modes or lightweight
tool executables. Shared document, compiler, preview, and property code must not
be duplicated.

### Application Layers

```text
Qt 6 application shell
        |
Mason workspace and document layer
        |
Editor commands, selection, properties, and asset tools
        |
Public editor/runtime bridge
        |
CypherWorld / CypherResource / CypherRenderer / CypherPhysics
```

Qt owns windows, docking, menus, native dialogs, and desktop input integration.
The engine owns world data, rendering, resources, simulation, and runtime
diagnostics.

### Map Editor Layout

The map workspace should provide:

- central engine-rendered 3D viewport
- optional classic four-viewport mode
- compact vertical tool palette
- scene hierarchy/outliner
- inspector/property panel
- asset and material browser
- layers and visibility controls
- transform and snapping controls
- console and output log
- build/compile panel
- profiler and runtime statistics
- undo history
- play, pause, stop, simulate, and possession controls

The visual direction is a restrained professional dark Qt interface inspired by
effective Hammer 5 and CryEngine workflows. Mason must not copy Valve assets,
branding, layouts, or implementation. Familiar interaction patterns are used
where they improve usability.

### Editing Modes

The map workspace should distinguish:

- object selection
- component selection
- vertex selection
- edge selection
- face selection
- brush creation and clipping
- entity placement
- material/UV editing
- volume and trigger editing
- path and spline editing
- terrain or specialized modes later

Mode state must not be hidden across unrelated panels. Selection filters,
snapping, local/world transforms, pivots, and orientation should be visible and
predictable.

### Core Editing Operations

- select, multi-select, invert, filter, and isolate
- move, rotate, scale, and pivot editing
- grid, surface, vertex, and angle snapping
- duplicate, group, layer, hide, lock, and delete
- create and reshape convex brushes
- clip and split brushes
- extrude, inset, bevel, split, merge, and weld mesh elements
- assign and align materials
- create and override prefab instances
- connect entity inputs, outputs, events, or flow data
- inspect validation errors in the viewport

### Command And Undo Model

Every persistent edit should be expressed as a command with enough information
to apply and reverse it.

```text
user gesture
  -> editor command
  -> validate preconditions
  -> mutate map document
  -> record inverse or previous state
  -> mark dependencies and regions dirty
  -> notify views
```

Commands enable:

- undo and redo
- command history
- transaction grouping for drags
- repeat-last-command behavior
- crash recovery journals
- future collaboration or change review

The UI must not mutate arbitrary engine memory directly.

### Schema-Driven Inspector

The property inspector should be generated primarily from reflection/schema
metadata:

- field name and stable ID
- value type
- display name and category
- numeric range and units
- enum choices
- resource type restrictions
- editor-only and runtime-only flags
- read-only state
- validation rules
- default value

Specialized controls remain appropriate for transforms, colors, curves,
materials, resources, and other high-value workflows.

### Engine Viewport

Mason should render through the actual Cypher renderer. Editor overlays are
additional passes for:

- grid
- outlines
- selection highlighting
- icons and helpers
- transforms and gizmos
- collision visualization
- visibility cells
- navigation
- lighting diagnostics

The editor must not maintain a visually different fake renderer for normal map
preview.

### Play In Editor

The initial in-process direction is:

```text
editable world
  -> validate and clone/instantiate
  -> simulation world
  -> play, pause, step, inspect
  -> stop
  -> destroy simulation world
  -> return to unchanged editable world
```

Runtime changes are not silently written back. Explicit keep/apply operations
may be introduced later for selected supported properties.

An out-of-process play session may later provide stronger crash isolation and
shipping-equivalent behavior. It is an additional mode, not a reason to delay a
useful integrated viewport.

## Asset Browser And Dependency Model

Mason's asset browser should be backed by the VFS, resource metadata, and a
derived asset index. SQLite is a candidate for the local index; it must not
become the authoritative source format.

The asset system should track:

- virtual source path
- stable resource identity
- source type and cooked type
- importer and compiler version
- direct and transitive dependencies
- source and cooked content hashes
- target platform/profile
- build status
- diagnostics
- thumbnails and preview metadata

Moving or renaming an asset must be a resource-aware operation with reference
repair and clear diagnostics.

## Command-Line Tools

GUI actions should invoke shared compiler libraries or headless tools. Required
tool directions include:

- CYKV parser/formatter/validator
- resource compiler
- map compiler
- package create/list/extract/verify tool
- format dump and inspection tool
- dependency scanner
- asset validation tool
- build cache inspection tool

Working executable names should be selected when the target structure exists.
`CypherScope` remains the planned general asset/package inspection application,
while Mason remains the authoring application.

No compiler's core behavior should exist only inside a Qt button callback.

## Diagnostics

Every format and compiler diagnostic should carry as much of the following as is
available:

- stable diagnostic code
- severity
- subsystem/domain
- source virtual path
- line and column
- byte range
- map object ID
- property path
- compiler stage
- concise message
- optional remediation hint

Mason should allow a diagnostic to select and frame the responsible object.
Command-line tools should emit the same diagnostic information in text and a
machine-readable report format.

## Versioning And Migration

### Source Formats

Source documents need explicit format and schema versions. Readers should:

- reject unsupported future major versions
- migrate supported older versions deliberately
- preserve unknown fields only when the schema policy allows it
- never silently reinterpret a field with changed meaning
- report lossy migration

Mason should save current canonical source syntax after a successful migration.

### Cooked Formats

Cooked files are rebuildable products. Compatibility policy can be stricter:

- reject incompatible versions
- rebuild from source when compiler versions change
- key caches by compiler, schema, platform, and dependency hashes
- avoid maintaining indefinite cooked backward compatibility

### Deterministic Serialization

The canonical CYKV writer should define:

- stable key ordering policy
- stable object ordering policy where semantics allow it
- canonical number formatting
- canonical escaping
- newline and indentation rules
- no pointer- or hash-table-order dependence

Human formatting and deterministic output must coexist. A formatter tool can
normalize hand-edited files.

## Validation And Security

Validation happens at several boundaries:

1. CYKV syntax validation
2. generic type/schema validation
3. asset-specific semantic validation
4. compiler-stage geometry and dependency validation
5. cooked binary structural validation
6. runtime capability validation

Examples include:

- duplicate stable IDs
- invalid references
- cyclic prefab dependencies
- non-convex brushes
- non-manifold or self-intersecting geometry
- degenerate faces and triangles
- invalid material assignments
- out-of-range coordinates
- impossible chunk offsets or sizes
- unsupported compression or platform flags
- missing runtime dependencies

Tools must fail with actionable diagnostics rather than producing partially
valid output that crashes later.

## Testing Strategy

### CYKV

- token and parser unit tests
- UTF-8 and escape tests
- numeric boundary tests
- malformed input tests
- depth and size limit tests
- parse/write/parse round trips
- canonical serialization golden files
- fuzz testing when the parser contract stabilizes

### CypherMap

- schema validation tests
- stable ID and reference tests
- source migration tests
- prefab override and cycle tests
- document round-trip tests
- deterministic ordering tests

### Geometry And Compiler

- brush clipping and CSG fixtures
- degenerate and adversarial geometry
- triangulation tests
- hidden-surface tests
- collision generation tests
- visibility tests
- deterministic build tests
- incremental/full build equivalence
- golden cooked-file manifests

### Runtime Loader

- valid and truncated files
- invalid offsets, sizes, counts, and arithmetic overflow
- unknown required chunks
- unsupported versions and flags
- dependency failures
- dedicated-server data stripping
- load/unload/reload ownership tests

### Mason

- command apply/undo/redo tests
- selection and stable-ID tests
- document dirty-state tests
- crash recovery tests
- inspector/schema tests
- compile diagnostic navigation tests
- play-mode isolation tests
- representative end-to-end map workflows

Performance benchmarks belong on parser throughput, large-map load/save,
compiler stages, picking, topology operations, and cooked runtime loading after
their correctness contracts are stable.

## Performance Direction

Performance comes primarily from architecture:

- do expensive conversion offline
- use contiguous cooked arrays
- reduce runtime allocations
- partition worlds explicitly
- batch by material and region
- load dependencies through stable resource handles
- use hashes and dependency graphs for incremental builds
- perform background editor tasks through the job system
- update only dirty views and regions
- keep source parsing out of normal shipping gameplay

SIMD or assembly is considered only after profiling identifies a stable hot loop.
It is not a substitute for the source/cooked separation or an efficient world
representation.

## Implementation Sequence

### Phase 0: Foundations

- complete the required CypherCommon Tier0 and Tier1 contracts
- finish allocator, containers, text, parsing, diagnostics, filesystem, jobs,
  resource handles, and math prerequisites
- define the coordinate and numeric policy

Exit condition: tools and runtime can share stable low-level data contracts.

### Phase 1: CYKV

- complete lexer and token reader (implemented)
- design and implement parser (implemented for CYKV 1)
- define semantic document ownership (implemented in Tier1)
- implement deterministic writer (implemented)
- implement syntax locations and diagnostics (implemented)
- add static schema validation (implemented in Tier2)

Lossless authoring trivia and semantic-node source maps remain deferred until
Mason needs them.

Exit condition: typed documents can round-trip deterministically and malformed
input fails safely.

### Phase 2: Minimal Runtime World

- define world settings and static instance data
- define entity spawn records
- load a minimal cooked world
- render and collide with one test room
- unload and reload without leaks

Exit condition: the game can consume a real world without Mason.

### Phase 3: Minimal CypherMap Pipeline

- define the smallest `.cymap` schema
- write the typed map document
- compile one brush room, one material, one light, and one spawn point
- write and load `.cymap_c`
- add deterministic compiler tests

Exit condition: command-line compilation produces a playable map.

### Phase 4: Mason Foundation

- Qt application shell
- document open/save
- embedded engine viewport
- hierarchy and inspector
- selection, transforms, grid snapping, and undo/redo
- compile and play controls

Exit condition: a user can modify the test room, compile it, and play it.

### Phase 5: Production Map Editing

- full brush operations
- editable polygon meshes
- material and UV tools
- entities and connections
- prefabs and layers
- diagnostics and visualization
- incremental build support

Exit condition: the first complete REAP arena can be authored entirely through
Mason.

### Phase 6: Advanced World Pipeline

- visibility construction
- lighting and probes
- navigation
- streaming regions
- specialized geometry required by the game
- release cooking and packaging

Exit condition: representative production maps meet runtime performance,
correctness, and iteration targets.

### Phase 7: Additional Mason Workspaces

- material
- model
- animation
- particle
- physics
- navigation
- audio
- objective/flow
- cinematic
- asset inspection

Each workspace begins only after the underlying runtime and compiler data are
real enough to edit.

## Approximate Scope

These ranges are planning aids, not success criteria. They exclude Qt, third-
party libraries, the renderer and physics implementations themselves, generated
files, shaders, and game content.

| Area | Approximate first-party LOC |
| --- | ---: |
| CYKV-backed map schema and serialization | 8k-20k |
| Map document, identity, references, and migration | 10k-25k |
| Brush and editable-mesh geometry kernel | 25k-70k |
| Map compiler | 25k-80k |
| Cooked world loader, reload, and streaming foundation | 10k-30k |
| Mason application/workspace foundation | 15k-35k |
| Viewports, picking, selection, and gizmos | 25k-70k |
| Map panels, materials, entities, layers, and prefabs | 25k-60k |
| Undo, build integration, and play-in-editor | 20k-50k |

After overlap and shared code are considered:

- production map pipeline and map editor: roughly `160k-350k` LOC
- focused tests and test utilities: roughly `50k-150k` LOC
- complete mature mapping stack: roughly `210k-500k` LOC
- complete long-term Mason suite: potentially `500k-1M+` LOC

The first useful vertical slice should be much smaller. A room, entity, material,
save, compile, and play workflow can prove the architecture before the complete
suite exists.

Public editor repositories support the order of magnitude, although raw line
counts are not directly comparable. A 2026-08-08 snapshot count found roughly
`182k` raw C/C++ lines across GtkRadiant's core/libraries/plugins, roughly `237k`
non-test plus `113k` test lines under TrenchBroom's application/libraries, and
roughly `322k` project lines in DarkRadiant after excluding obvious bundled
third-party and test directories. These counts include comments and blank lines
and are context, not targets.

## Explicit Non-Goals

- do not copy Valve, CryEngine, id Software, or community-editor implementation
  code without explicit legal review and provenance
- do not clone Valve branding or UI assets
- do not make CYKV a general gameplay programming language
- do not make classic BSP mandatory for every map
- do not make `.cymap` the shipping runtime representation
- do not serialize raw C++ structs or pointers
- do not hide compilers inside Qt callbacks
- do not start the complete Mason UI before runtime world data exists
- do not add formats only to increase file count

## Decisions Still Required

Before stable map and cooked-resource version 1, decide and document:

- schema-to-reflection integration beyond the current static Tier2 descriptors
- stable ID width and generation policy
- coordinate system, units, and precision
- brush representation and geometric tolerances
- editable mesh topology representation
- map/submap/layer composition rules
- visibility algorithm and runtime representation
- world-region and streaming policy
- cooked chunk compatibility policy
- provisional model, animation-graph, particle, audio-mix, UI, and cinematic
  extensions
- in-process versus child-process play modes and their responsibilities

These are expected design tasks. They are not reasons to weaken the source and
cooked architecture already decided here.

## Reference Material

These projects and documents are architecture references only:

- [Quake III Arena source](https://github.com/id-Software/Quake-III-Arena)
- [Valve Map Format](https://developer.valvesoftware.com/wiki/VMF_%28Valve_Map_Format%29)
- [Valve Hammer Editor for Source 2](https://developer.valvesoftware.com/wiki/Valve_Hammer_Editor_%28Source_2%29)
- [Source 2 legacy content porting](https://developer.valvesoftware.com/wiki/Source_2/Docs/Porting_Legacy_Content)
- [Source 2 lightmaps](https://developer.valvesoftware.com/wiki/Lightmap_%28Source_2%29)
- [GtkRadiant](https://github.com/TTimo/GtkRadiant)
- [TrenchBroom](https://github.com/TrenchBroom/TrenchBroom)
- [DarkRadiant](https://github.com/codereader/DarkRadiant)

Reference study does not grant permission to copy proprietary implementation or
assets. `reference_policy.md` remains the legal and provenance policy.

## Final Direction

CypherEngine will use CYKV as the typed source-data foundation. `.cymap` will be
a CYKV-backed editable map document. Mason will visually edit a typed map model,
not raw text. CypherMapCompiler will transform that source into a deterministic,
chunked `.cymap_c` runtime world. Brushes and BSP-derived algorithms remain
valuable tools, while arbitrary meshes, explicit runtime resources, visibility,
collision, lighting, navigation, and streaming remain independent systems.

The first milestone is not a visually complete Hammer replacement. It is a
complete vertical path in which one real map can be authored, validated,
compiled, loaded, rendered, simulated, and played using the same data contracts
that later production tools will extend.
