<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/CYPHERENGINE_REFERENCE_MANUAL.md
//  Purpose: Serves as the public reference manual for CypherEngine and REAP tools.
//  Details: The manual records current behavior, proprietary format contracts,
//           compatibility, limits, workflows, diagnostics, and explicitly marked
//           future design without presenting proposals as implemented features.
//
//  History:
//  - Created by Karlo Siric on 2026-09-17
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine Reference Manual

| Manual property | Value |
| --- | --- |
| Edition | 0.1 living draft |
| Snapshot | 2026-09-17 |
| Engine | CypherEngine |
| Game | REAP |
| Language | C++20 with C-style, data-oriented subsystem contracts |

This manual is the primary human-readable reference for building, configuring,
authoring content for, compiling resources for, and extending CypherEngine. It is
written for engine programmers, game programmers, technical artists, level
designers, tool developers, and contributors reviewing the proprietary file
formats.

CypherEngine is under active development. This manual distinguishes working code
from partial integrations, planned contracts, and proposals. A name appearing in
the manual does not make that feature available.

## Table of Contents

1. [About This Manual](#1-about-this-manual)
2. [Status Vocabulary](#2-status-vocabulary)
3. [Engine and Game Overview](#3-engine-and-game-overview)
4. [Architecture and Ownership](#4-architecture-and-ownership)
5. [Repository and Build Quick Reference](#5-repository-and-build-quick-reference)
6. [Virtual Paths and Resource Identity](#6-virtual-paths-and-resource-identity)
7. [Authoring-to-Runtime Data Flow](#7-authoring-to-runtime-data-flow)
8. [Versioning and Compatibility](#8-versioning-and-compatibility)
9. [Format Admission Rules](#9-format-admission-rules)
10. [Complete Format Catalog](#10-complete-format-catalog)
11. [CYKV Language Reference](#11-cykv-language-reference)
12. [CYKV Schema and Configuration Formats](#12-cykv-schema-and-configuration-formats)
13. [CYRS Cooked-Resource Container](#13-cyrs-cooked-resource-container)
14. [Shader Format](#14-shader-format)
15. [Texture Format](#15-texture-format)
16. [Material Format](#16-material-format)
17. [Map Format](#17-map-format)
18. [Input Actions and User Bindings](#18-input-actions-and-user-bindings)
19. [Package Archive](#19-package-archive)
20. [Candidate and Missing Format Families](#20-candidate-and-missing-format-families)
21. [Resource Compiler](#21-resource-compiler)
22. [Runtime Resource Loading](#22-runtime-resource-loading)
23. [Diagnostics and Failure Behavior](#23-diagnostics-and-failure-behavior)
24. [Determinism, Security, and Trust](#24-determinism-security-and-trust)
25. [Compatibility and Migration Policy](#25-compatibility-and-migration-policy)
26. [Documentation Rules](#26-documentation-rules)
27. [Contributor Checklists](#27-contributor-checklists)
28. [Glossary](#28-glossary)
29. [Reference Index](#29-reference-index)
30. [External Design References](#30-external-design-references)

## 1. About This Manual

This is a reference manual. It answers questions such as:

- Which engine subsystems exist today?
- Which subsystem owns a piece of data?
- What is the exact version of a proprietary format?
- Which fields are authored, compiled, or generated?
- What does the compiler accept?
- What can the runtime actually consume?
- Which limits are part of the contract?
- How are errors reported?
- What happens when a file is from an older generation?
- Which proposed formats are still names without frozen layouts?

The manual is not a general game-development textbook and is not a substitute
for API headers or tests. Source code remains authoritative where this living
manual falls behind. A disagreement between code and documentation is a defect to
report and resolve.

Task-oriented tutorials, screenshots, and editor walkthroughs may eventually
live beside this manual. The reference chapters remain organized around stable
concepts, commands, formats, and behavior so readers can find one exact answer.

### 1.1 Intended reading paths

**New engine contributor**

1. Read [Engine and Game Overview](#3-engine-and-game-overview).
2. Read [Architecture and Ownership](#4-architecture-and-ownership).
3. Build the project through [Repository and Build Quick Reference](#5-repository-and-build-quick-reference).
4. Follow the links in [Reference Index](#29-reference-index) for the subsystem being changed.

**Content and tool developer**

1. Read [Virtual Paths and Resource Identity](#6-virtual-paths-and-resource-identity).
2. Read [Authoring-to-Runtime Data Flow](#7-authoring-to-runtime-data-flow).
3. Read [CYKV Language Reference](#11-cykv-language-reference).
4. Read the exact format chapter.
5. Use [Resource Compiler](#21-resource-compiler).

**Format maintainer**

1. Read [Versioning and Compatibility](#8-versioning-and-compatibility).
2. Read [Format Admission Rules](#9-format-admission-rules).
3. Read [Determinism, Security, and Trust](#24-determinism-security-and-trust).
4. Apply the format checklist in [Contributor Checklists](#27-contributor-checklists).

### 1.2 Normative language

The words **must**, **must not**, **required**, **should**, **should not**, and
**may** describe contract strength when they appear in a format or API rule.
Explanatory prose elsewhere describes current behavior without creating a new
binary compatibility promise.

### 1.3 Manual update rule

Every accepted source or cooked format change must update:

1. the format constants and implementation;
2. tests and golden data;
3. the dedicated format specification;
4. this manual's version and capability tables;
5. the format catalog;
6. the changelog;
7. migration guidance when old data remains in use.

## 2. Status Vocabulary

Every feature and format is assigned one of these labels:

| Label | Meaning |
| --- | --- |
| **Implemented** | The named reader, writer, decoder, compiler, runtime consumer, or tool behavior exists and is tested. |
| **Partial** | A real working slice exists, while listed parts of the end-to-end path remain unavailable. |
| **Active** | A lower-level contract exists and its next integration layer is current work. |
| **Planned** | Responsibility and provisional identity are documented, but the contract is not frozen. |
| **Proposal** | A design is being evaluated. Names, syntax, versions, and responsibilities may change. |
| **Reserved** | A field or name exists to prevent incompatible reuse, but the associated feature is unavailable. |
| **Deprecated** | Accepted for compatibility; new content should not use it. |
| **Removed** | No longer accepted by the current reader or tool. |

Status applies per layer. A source schema can be implemented while its cooker is
partial and its runtime consumer is planned. Every format chapter therefore
reports source, compiler, cooked, resource-loader, runtime, and editor status
separately.

## 3. Engine and Game Overview

CypherEngine is a from-scratch engine and toolchain being built around REAP, a
fast 3D arena-survival first-person shooter. The current goal is a small playable
loop: movement, one arena, waves, combat, and minimal UI. Engine features must
justify themselves through that game or through an immediate authoring need.

The codebase uses C++20 with a C-style and data-oriented architecture:

- structs and free functions over deep class hierarchies;
- explicit ownership and lifetime;
- generation-checked handles for runtime objects;
- module prefixes instead of inheritance-driven architecture;
- offline processing for expensive validation and conversion;
- versioned source and cooked contracts;
- strict failure and rollback behavior;
- bounded allocations and hostile-input checks;
- editor/runtime separation.

### 3.1 Current project state

At this snapshot, working slices include:

- Host startup, logging, memory, filesystem, package mounting, commands, CVars,
  configuration, and System window/event services;
- CYKV parsing, writing, schemas, canonical hashing, and binary tree packing;
- CYRS cooked resources;
- shader, texture, and material source/cooked formats and offline compilers;
- VFS-backed resource ownership;
- generation-checked OpenGL shaders, buffers, textures, vertex input, pipelines,
  and indexed drawing;
- a cooked-resource textured-cube example;
- `.cymap` V1–V3 authoring, deterministic persistence, validation, generated
  blockout geometry, and preview;
- a Qt Tile Editor with orthographic and embedded 3D views;
- Picasso texture/material authoring groundwork;
- extensive correctness, malformed-input, sanitizer, and benchmark coverage.

The integrated Host does not yet own a full World/gameplay loop. General World,
Input, Physics, Audio, AI, Networking, Animation, and gameplay systems are still
partial, scaffolded, or planned.

The current resume point is [current_status.md](current_status.md).

## 4. Architecture and Ownership

### 4.1 Major boundaries

| Boundary | Responsibility |
| --- | --- |
| Common Tier 0/1/2 | Primitive types, containers, parsing, schemas, portable contracts |
| System | OS window, timing, native events, physical input events, host services |
| FileSystem/VFS | Canonical paths, mounts, loose/package providers, sync/async reads |
| Resource | Typed resource registration, loading, owned backing storage, references |
| Tools and compilers | Authoring validation, dependency discovery, deterministic cooking |
| Render | Native GPU objects and draw behavior from validated resource views |
| World | Runtime spatial/game object representation and submission boundaries |
| Input | Device state, contexts, bindings, named actions, action frames |
| Physics | Collision queries, controllers, rigid-body integration when required |
| Audio | Runtime voices, buses, event playback, streaming, platform backend |
| Game | REAP rules, entities/components, combat, waves, progression |
| Editors | Mutable source documents, undo/redo, visualization, tool workflows |

### 4.2 Ownership rules

- A source recipe describes author intent; it does not own native runtime handles.
- A cooker validates source and dependencies; it does not initialize the renderer.
- A cooked resource stores bounded runtime data; it does not store native pointers.
- A resource loader owns the complete validated file blob.
- A borrowed format view remains valid only while its owning blob remains alive.
- A renderer owns OpenGL/Vulkan/native objects created from validated views.
- An editor owns mutable documents and framework widgets.
- The renderer never parses CYKV or discovers source dependencies.
- Runtime code does not mutate packaged source recipes.
- User-writable preferences remain separate from packaged developer assets.

Detailed architecture documents are indexed in [Reference Index](#29-reference-index).

## 5. Repository and Build Quick Reference

### 5.1 Prerequisites

- Git
- CMake 3.20 or newer
- Ninja
- C++20 compiler
- Project-local pinned vcpkg dependencies

### 5.2 First checkout

```bash
git submodule update --init --recursive
cmake -P cmake/CypherBootstrapVcpkg.cmake
```

### 5.3 Build and run

```bash
cmake --preset debug
cmake --build --preset debug
./out/build/debug/bin/CypherEngine
```

Some local build layouts may place the executable under `build/bin`. The selected
CMake preset and configure output are authoritative.

### 5.4 Tests

```bash
ctest --preset debug
```

### 5.5 Benchmarks

```bash
cmake --preset bench-release
cmake --build --preset bench-release
```

### 5.6 Dependency qualification

A declared or downloaded dependency is not automatically a runtime dependency.
The owning subsystem must provide a Cypher contract, explicit link relationship,
lifetime policy, error boundary, and tests.

The complete build notes are in [build_guide.md](build_guide.md).

## 6. Virtual Paths and Resource Identity

Cypher source recipes and compiled resources use canonical virtual paths instead
of arbitrary native paths.

A canonical resource path:

- is UTF-8;
- is relative;
- uses `/` separators;
- contains no empty component;
- contains no `.` or `..` component;
- has no drive letter or absolute root;
- normally uses lowercase ASCII for portable asset identity;
- is at most 259 bytes where the current render schemas require that limit;
- includes the source or cooked extension;
- resolves through the mounted VFS.

Examples:

```text
shaders/world/tile_surface.cyshader
textures/dev/grid.cytex
materials/dev/grid.cymat
maps/arena_01.cymap
```

Native developer paths such as
`/Users/name/project/assets/materials/dev/grid.cymat` are diagnostic locations,
not persistent resource identities.

### 6.1 Identity hierarchy

| Identity | Purpose |
| --- | --- |
| Canonical virtual path | Stable project resource identity |
| CYKV semantic hash | Identity of a typed authored document |
| Compiler source hash | Recipe, dependencies, versions, toolchain, target, and profile |
| Logical binding ID | Stable named shader/material interface member |
| Shader interface hash | Complete cooked logical shader ABI |
| Material variant hash | Resolved material feature set |
| Chunk hash | Decoded or stored chunk identity according to the finalized contract |
| CYRS content hash | Complete cooked bytes after the fixed CYRS header |
| Package content hash | Per-entry corruption check in `.cypak` V10 |

Hashes used today are primarily deterministic cache or corruption identities.
They are not cryptographic trust signatures unless a contract explicitly says so.

## 7. Authoring-to-Runtime Data Flow

```text
source-controlled authored file
    -> VFS path resolution
    -> CYKV syntax parse
    -> exact language/schema selection
    -> closed structural validation
    -> typed semantic decode
    -> dependency loading through VFS
    -> offline compiler
    -> deterministic CYRS resource
    -> package or loose cooked tree
    -> runtime VFS
    -> CypherResource-owned blob
    -> validated borrowed resource view
    -> subsystem-owned native/runtime object
```

Not every format has every stage. Examples:

- `.cysettings` is writable configuration and has no cooked form.
- `.cymap` currently has editor/source preview but no `.cymap_c` cooker.
- `.cypak` is a distribution container, not a CYRS resource.
- `.cybindings` is proposed user state and must never be packaged.
- `.cyreplay` and `.cysave` would be generated runtime records, not authored
  source-to-cooked pairs.

### 7.1 Transactional publication

A loader, compiler, or writer builds temporary state first. On failure:

- the caller receives a stable error;
- no partial destination view is published;
- an existing valid output remains intact;
- hot reload keeps the previous live object when possible;
- a writable user file is replaced only after a complete successful write.

## 8. Versioning and Compatibility

Cypher uses explicit version layers:

| Layer | Example | Meaning |
| --- | --- | --- |
| Language | `@cykv 1` | Syntax and primitive typed-tree semantics |
| Schema | `@schema "cypher.material" 2` | Domain fields and source semantics |
| Compiler API | Material compiler API 1 | Tool host/compiler call contract |
| Compiler implementation | Material compiler 2 | Source-to-cooked behavior generation |
| Container | CYRS 1 | Generic cooked envelope layout |
| Resource | CYMT 2 | Domain cooked layout |
| Internal metadata | MTMD 2 | Domain chunk payload layout |
| Package | CYPACKAGE version 10 | Distribution archive layout |

Rules:

- Unknown versions are rejected.
- Readers dispatch by explicit version number.
- Readers do not guess from file length or field shape.
- A compatibility reader is an intentional tested code path.
- A writer normally emits only the current generation.
- Changing source-to-cooked meaning requires a compiler-version increase.
- Changing binary layout or serialized meaning requires a resource-version or
  metadata-version decision.
- Adding an optional field to a closed source schema requires a schema-version
  decision; V1 behavior is not silently widened.
- Old content is migrated explicitly or retained through a compatibility route.

## 9. Format Admission Rules

A proprietary extension is added only when all of these questions have concrete
answers:

1. Which author or tool creates it?
2. Which subsystem owns its semantics?
3. Which real consumer needs it?
4. Is it authored source, generated data, cooked runtime data, user state, or a
   distribution container?
5. Why is an existing standard format insufficient?
6. What are its source identity and canonical path rules?
7. Does it use CYKV, another standard encoding, or a custom binary layout?
8. What version number and compatibility policy apply?
9. What are the maximum bytes, records, nesting, and dependencies?
10. What is the deterministic ordering and hash policy?
11. What does a failed load or write leave behind?
12. What tests, inspector, validator, and migration tools exist?
13. What security and trust boundary applies?
14. Which manual and catalog chapters must change?

A provisional name remains **Planned** or **Proposal** until a working consumer
and exact contract exist.

Avoid proprietary wrappers where established source formats are already useful:

- PNG, JPEG, TGA, and EXR for source images;
- glTF where appropriate for interchange;
- WAV or FLAC for source audio;
- JSON or NDJSON for machine reports where CYKV ownership adds no value;
- platform-native crash dumps plus a small portable metadata manifest;
- standard trace formats for profilers.

## 10. Complete Format Catalog

### 10.1 Implemented or partial families

| Purpose | Source | Cooked/generated | Status |
| --- | --- | --- | --- |
| Generic structured data | CYKV text V1 | CYKV binary pack V1 | Implemented, with documented binary identity gaps |
| Project manifest | `.cyproject`, schema V1 | None | Schema and typed decoder implemented; application integration incomplete |
| User settings | `.cysettings`, schema V1 | None | Display subset implemented; full settings service incomplete |
| Commands/CVars | `.cfg` / `.cycfg` | None | Implemented unversioned runtime command stream |
| Generic cooked resource | N/A | CYRS container V1 | Implemented |
| Shader | `.cyshader` V1/V2 | `.cyshader_c`, CYSH V2/V3 | Compiler and loader implemented; general runtime binding partial |
| Texture | `.cytex` V1/V2 | `.cytex_c`, CYTX V1/V2 | Compiler and loader implemented; general upload/streaming partial |
| Material | `.cymat` V1/V2 | `.cymat_c`, CYMT V1/V2 | Compiler and loader implemented; production runtime binding partial |
| Tile map | `.cymap` V1/V2/V3 | `.cymap_c` planned | Source editor/persistence/preview implemented |
| Package archive | N/A | `.cypak` V10 | Reader/writer/VFS mount implemented; important V10 gaps documented |

### 10.2 Planned format names already in the project catalog

| Purpose | Source | Cooked |
| --- | --- | --- |
| Surface definition | `.cysurface` | `.cysurface_c` |
| Mesh | `.cymesh` | `.cymesh_c` |
| Scene | `.cyscene` | `.cyscene_c` |
| Prefab/entity template | `.cyprefab` | `.cyprefab_c` |
| Physics setup | `.cyphys` | `.cyphys_c` |
| Navigation | `.cynav` | `.cynav_c` |
| Mission/logic graph | `.cyflow` | `.cyflow_c` |
| Skeleton | `.cyskel` | `.cyskel_c` |
| Animation clip | `.cyanim` | `.cyanim_c` |
| Particle system | `.cyparticle` | `.cyparticle_c` |
| Sound recipe/stream | `.cysnd` | `.cysnd_c` |
| Font | `.cyfont` | `.cyfont_c` |
| UI layout/style | `.cyui` | `.cyui_c` |
| Cinematic sequence | `.cycine` | `.cycine_c` |
| Resource/build manifest | `.cymanifest` | Tool/build output as defined later |

### 10.3 Candidate families added by the current audit

| Priority | Purpose | Candidate identity | Status |
| ---: | --- | --- | --- |
| 1 | Developer action maps | `.cyinput` -> `.cyinput_c` / CYIN | Proposal |
| 1 | User binding overrides | `.cybindings` | Proposal |
| 2 | Schema definition/registry | `.cyschema` | Proposal; CYKV 1 schema descriptors currently live in code |
| 2 | Schema-selected gameplay data | `.cydata` -> `.cydata_c` | Proposal |
| 2 | Localization catalog | `.cyloc` -> `.cyloc_c` | Proposal |
| 2 | Closed captions/subtitles | `.cycaption` -> `.cycaption_c` | Proposal |
| 3 | Animation state/evaluation graph | `.cyanimgraph` -> cooked evaluator | Proposal |
| 3 | Audio event/rule stack | `.cyaudioevent` -> cooked event | Provisional name |
| 3 | Audio mixer/bus graph | `.cymix` -> cooked graph | Provisional name |
| 3 | Post-processing profile/LUT recipe | `.cypostfx` -> cooked profile | Proposal |
| 4 | Runtime replay/demo | `.cyreplay` / CYRP | Generated record proposal |
| 4 | Save/checkpoint | `.cysave` / CYSV | Generated record proposal |
| 4 | Mod metadata | `.cymod` | Proposal |
| 4 | Plug-in metadata | `.cyplugin` | Proposal |

Candidate names must pass the admission rules before becoming frozen contracts.
Detailed prioritization appears in [Candidate and Missing Format Families](#20-candidate-and-missing-format-families).

## 11. CYKV Language Reference

### 11.1 CYKV 1 status

| Contract property | Value |
| --- | --- |
| Language version | 1 |
| Status | Implemented |
| Normative specification | [formats/CYKV.md](formats/CYKV.md) |
| V2 design proposal | [formats/CYKV_2_PROPOSAL.md](formats/CYKV_2_PROPOSAL.md) |

Every complete CYKV 1 document begins with:

```cykv
@cykv 1
@schema "schema.identifier" 1
```

The root value of current schema documents is an object.

### 11.2 Value types

CYKV 1 supports:

| Type | Meaning |
| --- | --- |
| `null` | Explicit absence |
| Boolean | `true` or `false` |
| `i64` | Signed 64-bit integer |
| `u64` | Unsigned 64-bit integer |
| `f64` | Finite 64-bit floating point |
| String | Valid UTF-8 text |
| Binary | Opaque byte sequence |
| Object | Named members |
| Array | Ordered values |

### 11.3 Lexical features

- UTF-8 source.
- LF and CRLF line endings.
- `//` line comments.
- Nested `/* ... */` block comments.
- Quoted strings.
- Triple-quoted multiline strings.
- Defined escape sequences.
- Signed and unsigned numeric spelling.
- Based integer spelling.
- Binary `hex""` values.
- Optional trailing commas.
- Unquoted keys under the implemented identifier grammar.

CYKV rejects an input BOM, embedded NUL, bare carriage return where prohibited,
invalid UTF-8, invalid numeric ranges, and non-finite floating point.

### 11.4 Duplicate and ordering policy

Objects reject duplicate keys. Repeated ordered data belongs in an array. This
avoids the first-match and insertion-order ambiguity found in several older
key/value formats.

Semantic canonicalization:

- sorts object members bytewise by name;
- preserves array order;
- normalizes scalar spelling;
- removes comments and formatting;
- includes language and schema identity;
- hashes the canonical typed result with XXH3-128.

A formatter or canonical writer does not retain the author's comments or layout.
Editors that require lossless round trips will need a separate syntax tree.

### 11.5 Principal limits

| Resource | Current bound |
| --- | ---: |
| Input text | 64 MiB |
| Parser nesting depth | 128 |
| In-memory model depth | 512 |
| Nodes | 1,048,576 |
| Direct values in one container | 1,048,576 |
| Nested block comments | 64 |
| Decoded name/string/binary data | 64 MiB per bounded operation |

Format-specific schemas and compilers normally impose lower limits.

### 11.6 Transactional behavior

A parse builds temporary state. A failed parse leaves the destination unpublished
and reports one stable status with source location. Structural schema validation
and typed semantic decoding are separate stages and have their own diagnostics.

### 11.7 CYKV 1 known limitations

- No includes or imports.
- No reusable typed constants.
- No conditionals.
- No general schema migration execution.
- No retained comments or formatting.
- No per-node source range after a successful parse.
- No semantic path, UUID, color, vector, matrix, or resource-reference core type.
- No NaN or infinity.
- No generated language bindings.
- No self-hosted `.cyschema` files.
- The binary pack does not preserve schema identity.

Known specification drift that must be resolved before declaring full conformance:

- empty quoted keys and the implemented bare-identifier grammar are broader than
  the normative wording;
- the deterministic precision-17 float writer has not been formally proven to
  meet the specification's “shortest representation” phrase;
- low-level model APIs can construct values that the normal parser would reject,
  leaving final writers responsible for validation.

### 11.8 CYKV binary pack V1

The binary pack uses `CYKV` magic, version 1, and a 40-byte little-endian header.
Each tree node is encoded as a fixed preorder record followed by name and
string/binary bytes.

It currently omits:

- CYKV language version;
- schema ID and version;
- comparison policy;
- dependency identity;
- semantic or whole-content hash.

It is an internal typed-tree encoding, not a complete self-identifying replacement
for a source document.

### 11.9 CYKV 2 direction

CYKV 1 remains frozen. Proposed CYKV 2 capabilities include:

- relative VFS-resolved fragments;
- explicit base/inheritance composition;
- immutable typed constants rather than textual macros;
- a small caller-supplied build-condition expression language;
- optional exact-width numeric intent;
- schema-gated `nan`, `+inf`, and `-inf`;
- dependency and expansion provenance;
- per-node origin ranges for tools;
- semantic and provenance hashes;
- a self-identifying binary generation.

The proposed processing pipeline is:

```text
parse every source
    -> resolve bounded dependency graph
    -> expand includes, bases, and typed constants
    -> evaluate explicit build context
    -> produce one typed tree with origins
    -> select/migrate exact schema
    -> validate and decode
    -> canonicalize, hash, or compile
```

The full proposal, limits, examples, exclusions, and acceptance criteria are in
[formats/CYKV_2_PROPOSAL.md](formats/CYKV_2_PROPOSAL.md).

## 12. CYKV Schema and Configuration Formats

### 12.1 Schema registry

CYKV schemas are currently C++ descriptor tables registered under an exact schema
ID and version. Structural validation checks:

- root and field types;
- required and optional fields;
- closed-object unknown-field policy;
- array and object bounds;
- basic string/numeric constraints;
- nested descriptors.

Typed decoders add domain behavior such as canonical paths, extension checks,
defaults, cross-field relationships, uniqueness, and target limitations.

The current schema architecture is documented in
[formats/CYKV_SCHEMAS.md](formats/CYKV_SCHEMAS.md).

A future `.cyschema` format may make schemas available to editors, reflection,
migrations, generic data tools, and generated bindings. It remains a proposal.
The in-code descriptors stay authoritative until a self-hosted schema compiler is
implemented and bootstrapped safely.

### 12.2 `.cyproject` — `cypher.project` V1

**Status:** Schema and typed decoder implemented; project-host integration partial.

Fields:

| Field | Requirement |
| --- | --- |
| `id` | Stable lowercase project identifier, 1–64 characters |
| `name` | Display name, 1–128 characters |
| `start_map` | Canonical `.cymap` path |
| `search_paths` | Optional 1–64 unique canonical paths, each at most 259 bytes |

Missing project concerns include target profiles, cook profiles, package roots,
plug-ins, build features, cache policy, launch profiles, game metadata, and
per-platform overrides.

### 12.3 `.cysettings` — `cypher.settings` V1

**Status:** Display subset implemented.

Current optional display fields:

| Field | Range/default |
| --- | --- |
| Width | 320–16,384; default 1280 |
| Height | 200–16,384; default 720 |
| Mode | windowed, borderless, fullscreen; default windowed |
| VSync | Boolean; default true |

Future settings need explicit sections for audio, input, accessibility, renderer
quality, backend selection, editor behavior, network preferences, and gameplay.
User settings are writable data and should not be cooked or packaged.

### 12.4 `.cfg` / `.cycfg`

**Status:** Implemented unversioned runtime command stream.

Supported forms include:

```text
exec path
set name value
seta name value
registered_command arguments
```

The parser supports quoted text and `#`/`//` comments outside quotes. Current
limits include 64 KiB per file, 1,024 bytes per line including terminator, a
260-byte path capacity, and nested `exec` depth eight.

Startup reads required `config/default.cfg` and optional
`config/autoexec.cfg`.

The `.cycfg` spelling currently has no distinct header or semantics. There is no
schema migration or deterministic writer, and `seta` persistence behavior is not
complete.

## 13. CYRS Cooked-Resource Container

| Container property | Value |
| --- | --- |
| Magic | `CYRS` |
| Container version | 1 |
| Status | Implemented |

Every current cooked render resource uses:

```text
80-byte fixed header
ordered 64-byte chunk descriptors
alignment padding
payload chunks
```

### 13.1 Header contents

- magic;
- container version;
- header size;
- domain resource FourCC;
- domain resource version;
- flags;
- chunk count;
- total file size;
- chunk-table offset;
- optional 128-bit source hash;
- optional 128-bit whole-content hash.

### 13.2 Chunk descriptor

- chunk FourCC;
- codec;
- flags;
- power-of-two alignment;
- absolute file offset;
- stored byte count;
- decoded byte count;
- optional 128-bit chunk hash.

Current general limits include 4,096 chunks and alignment up to 1 MiB.

### 13.3 Current behavior

The common reader validates header identity, known flags, counts, ranges,
alignment, ordered nonoverlapping chunks, and whole-content hash before
publishing decoded descriptors.

Domain readers add exact expected chunk sets, metadata versions, padding rules,
record order, string-table policy, and payload validation.

### 13.4 Current gaps

- LZ4 and Zstandard identifiers exist, but common decompression is unavailable.
- Chunk-hash responsibility and stored-versus-decoded byte meaning need one
  authoritative rule before compression.
- Common validation alone may accept gaps or trailing unreferenced storage that
  stricter domain readers reject.
- The common writer owns header/table construction; domain writers still own
  payload placement and final sealing.
- XXH3 identities detect corruption and drive caches; they do not authenticate
  hostile content.
- No signature or encryption layer exists.

## 14. Shader Format

### 14.1 Version matrix

| Layer | Current | Compatibility |
| --- | ---: | --- |
| CYKV language | 1 | 1 |
| `cypher.shader` schema | 2 | 1 and 2 |
| Compiler API / implementation | 1 / 5 | V1 and V2 source |
| CYRS | 1 | 1 |
| CYSH | 3 | reader accepts 2 and 3 |
| SHMD | 3 | V2 or V3 with matching CYSH |
| SHRF | 1 | CYSH V3 only |

### 14.2 `.cyshader` V1

Required fields:

- `language = "glsl"`;
- vertex stage path;
- fragment stage path.

Optional:

- up to 64 unique defines.

Both stages use entry `main`. V1 has no logical material interface and compiles
to CYSH V2.

### 14.3 `.cyshader` V2

V2 adds:

- stage objects with source and optional entry;
- texture declarations;
- sampler declarations at the schema level;
- typed material parameters;
- defaults and required flags;
- scalar ranges;
- Boolean and enum features;
- static and dynamic feature modes;
- variant budget, default 64 and maximum 1,024.

Texture shapes declared by the schema:

- 2D;
- cube;
- 2D array;
- 3D.

Parameter types:

- Boolean;
- signed/unsigned 32-bit integer;
- scalar float;
- two-, three-, and four-component float/vector/color;
- 3×3 and 4×4 matrix.

The compiler uses glslang and SPIRV-Cross to prove the active linked GLSL
resources against the authored interface. SPIR-V is used for offline validation
and reflection. Runtime code remains preprocessed GLSL.

Current compiler gates:

- entry must be `main`;
- feature tables are rejected because there is no cooked variant table;
- independent samplers are rejected because association is not versioned;
- descriptor arrays, shadow samplers, multisample images, integer sampled images,
  cube arrays, and ordinary uniform arrays are rejected;
- compiler output is vertex+fragment OpenGL graphics only;
- macOS target ceiling is GLSL 410; other current targets use at most 450, while
  the generic cooked layer can represent 460.

### 14.4 CYSH V2

Chunks:

```text
SHMD  program and two-stage metadata
SHCD  vertex GLSL
SHCD  fragment GLSL
```

V2 contains one OpenGL graphics program and no logical interface.

### 14.5 CYSH V3

Chunks:

```text
SHMD  program metadata, interface hash, stage records
SHRF  fixed logical binding records
SHST  canonical names
SHCD  vertex GLSL
SHCD  fragment GLSL
```

V3 records up to 128 logical bindings with stable names/IDs, kind, type,
resource shape, array count, stage visibility, flags, and constant-storage
layout. It can describe more resource kinds than the source compiler currently
emits.

Limits include two stages, 1,024 array elements per binding, 127-byte binding
names, a 64 KiB string table, and 16 MiB source per stage.

### 14.6 Runtime status

The resource loader accepts CYSH V2 and V3. The OpenGL renderer creates a
validated native vertex/fragment program.

The current draw path does not yet allocate or bind the full V3 logical interface.
It supports a narrow uniform-block and sampled-2D-texture slice. General constant
upload, other texture shapes, independent samplers, variants, additional stages,
non-OpenGL payloads, and general hot reload remain incomplete.

All 102 checked-in `.cyshader` documents currently use source schema V1.

Complete renderer-format details are in
[formats/RENDER_ASSETS.md](formats/RENDER_ASSETS.md).

## 15. Texture Format

### 15.1 Version matrix

| Layer | Current | Compatibility |
| --- | ---: | --- |
| `cypher.texture` schema | 2 | 1 and 2 |
| Compiler API / implementation | 1 / 4 | V1 and V2 |
| CYTX | 2 | reader accepts 1 and 2 |
| TXMD | 2 | V1 or V2 according to CYTX |

Both current source routes write CYTX V2.

### 15.2 `.cytex` V1

Fields:

- source path;
- usage: color, normal, or data;
- color space: sRGB or linear;
- mip-generation Boolean.

Defaults are color, sRGB, and generated mips. Supported import sources are PNG,
JPEG, TGA, and finite RGBA EXR. Normal, data, and EXR inputs are linear.

### 15.3 `.cytex` V2

Required fields:

- source;
- type, currently only 2D;
- usage;
- color space.

Optional policy groups:

| Group | Current schema capability |
| --- | --- |
| Alpha | none, straight, premultiplied, mask, data; cutoff and RGB dilation policy |
| Mips | generate, preserve, none; box/Kaiser/Lanczos; clamp/repeat/mirror; coverage policy |
| Output | auto or uncompressed; fast/balanced/production quality |
| Streaming | scheduling class and resident coarse-mip count |

Current compiler support is narrower:

- PNG/JPEG/TGA to canonical RGBA8;
- finite RGBA EXR to RGBA32 float;
- one 2D frame/layer/face;
- generated box-filtered clamped mip chain or no mips;
- uncompressed output;
- sRGB linear-space filtering;
- temporary premultiplication for straight alpha;
- normal-vector renormalization.

Current compiler rejections include DDS/KTX2 preservation, Kaiser/Lanczos,
repeat/mirror mip edges, alpha-coverage preservation, RGB dilation, and
compression.

### 15.4 CYTX V1

Compatibility format for one 2D mip chain. The current reader maps it into the
V2 subresource view. New writes use V2.

### 15.5 CYTX V2

```text
TXMD  128-byte header and 64-byte subresource records
TXDT  one independently hashed payload per subresource
```

The format records dimension, storage format, semantic use, color space, alpha,
target profile, residency, complete dimensions/counts, streaming priority, and
per-subresource coordinates, extents, pitches, chunk index, and bytes.

The contract can represent 1D/2D/3D/cube data, layers, frames, faces, and block
compression. Current source cooking emits one uncompressed 2D image.

Limits:

- 16,384 texels per dimension;
- 15 mips;
- 256 layers;
- 256 frames;
- six faces;
- 1,024 subresources;
- 512 MiB encoded data.

### 15.6 Runtime status

The current renderer implements the first immutable RGBA8 2D upload and binding
path. General CYTX subresources, float textures, cube/array/3D resources,
compression, and runtime streaming remain incomplete.

All three checked-in `.cytex` documents use source schema V1.

## 16. Material Format

### 16.1 Version matrix

| Layer | Current | Compatibility |
| --- | ---: | --- |
| `cypher.material` schema | 2 | 1 and 2 |
| Compiler API / implementation | 1 / 2 | V1 and V2 |
| CYMT | 2 | reader accepts 1 and 2 |
| MTMD | 2 | V1 or V2 according to CYMT |

### 16.2 `.cymat` V1

Fields:

- required shader path;
- optional named texture map, up to 32 entries;
- optional parameter map, up to 64 entries;
- Boolean, scalar `f64`, and 2–4 component vector values.

V1 validates direct dependencies but not shader reflection. It writes CYMT V1.

### 16.3 `.cymat` V2

V2 adds:

- bounded inheritance up to 16 layers;
- cycle detection;
- domains: surface, decal, UI, postprocess, particle;
- reserved surface reference;
- alpha mode and cutoff;
- two-sided and shadow state;
- Boolean and enum feature overrides;
- texture resource, combined sampler preset, and UV transform overrides;
- Boolean, signed, unsigned, floating, vector/color/matrix-compatible values;
- `null` removal of inherited entries;
- exact validation against a CYSH V3 interface;
- exact texture usage/color-space checks against CYTX source meaning.

Current gates:

- nonempty features are rejected until CYSH has a variant table;
- surface references are rejected until `.cysurface` exists;
- independent shader samplers are rejected;
- referenced textures are limited to the current 2D V2 compiler subset.

Every member of a V2 inheritance/dependency chain must use V2 source schemas.

### 16.4 CYMT V1

Stores one shader path, up to 32 named texture paths, up to 64 legacy values, and
a canonical string table.

### 16.5 CYMT V2

```text
MTMD  128-byte header and feature/texture/parameter records
MTCD  optional packed constants
MTST  canonical strings
```

It persists the resolved shader/surface paths, shader interface hash, material
variant hash, domain/state, features, textures, sampler presets, binding IDs, UV
state, typed parameter layout, and zero-padded little-endian constants.

Limits include 32 features, 32 textures, 64 parameters, a 64 KiB string table,
and 64 KiB constant block.

### 16.6 Runtime/editor status

Both versions can be loaded and validated. A complete dependency-owned GPU
material object is not yet implemented.

The Tile Editor preview recognizes a bounded base-color/tint/UV subset. Its
legacy field access does not yet bridge every CYMT V2 typed field, so valid V2
state can be unavailable to preview. This is a high-priority integration gap.

All three checked-in `.cymat` documents use source schema V1.

## 17. Map Format

| Map property | Value |
| --- | --- |
| Schema ID | `cypher.map` |
| Writer | V3 |
| Reader | V1, V2, V3 |
| Cooked map | Not implemented |

### 17.1 V1

Root data:

- UUID map ID;
- width and height;
- physical cell size and level height;
- sparse active cells;
- player-spawn and door markers.

Cell data:

- X/Y coordinate;
- signed floor level;
- unsigned wall-height levels;
- material slot;
- floor flag.

Marker data:

- player spawn: ID, X/Y, yaw;
- door: ID, X/Y, cardinal side.

### 17.2 V2

Adds optional cell shape:

- flat;
- stairs north/east/south/west;
- stair steps from 2 through 32, default 8.

### 17.3 V3

Adds up to 256 material-slot bindings from a stable numeric slot to a canonical
`.cymat` path.

### 17.4 Limits

- dimensions 1–1,024;
- minimum cell size 0.20;
- minimum level height 0.25;
- 262,144 active cells;
- 4,096 markers;
- 256 material bindings;
- 64 MiB text.

### 17.5 Canonical persistence

The writer always emits V3 with row-major cells, UUID-sorted markers,
slot-sorted materials, omitted empty material table, omitted default cell shape,
two-space indentation, and final LF.

V1/V2 documents load through explicit compatibility and re-save as V3.

### 17.6 Validation

Structural loading rejects unknown or missing fields, duplicates, invalid paths,
ranges, and marker kinds transactionally.

A separate gameplay validator checks one spawn, marker placement, stair rules,
door boundary placement, and duplicate edges. Saving structurally valid work does
not require every gameplay diagnostic to be resolved.

### 17.7 Current consumer and missing cooker

Implemented:

- Tile Editor mutable document;
- undo/redo;
- deterministic source persistence;
- generated floors, exposed walls, cliffs, doors, and stairs;
- editor and standalone source preview;
- material slots and bounded cooked preview dependencies.

Missing:

- `.cymap_c`;
- shared map compiler;
- production World loader;
- partitioning and streaming;
- collision, visibility, navigation, and lighting sections;
- general entities/components and script bindings;
- arbitrary brush/mesh placement.

Checked-in maps: three V1, one V2, and one V3.

## 18. Input Actions and User Bindings

**Status:** Proposal. Low-level keyboard/text/mouse System events exist; the action
runtime, compiler, schemas, cooked resource, and writable binding store do not.

Recommended family:

| Layer | Identity |
| --- | --- |
| Project source | `.cyinput`, `cypher.input` V1 |
| Cooked runtime | `.cyinput_c`, CYIN V1 inside CYRS V1 |
| User overrides | `.cybindings`, `cypher.input_bindings` V1 |

The main format should not be called `.cykeymap`: the required contract covers
physical keyboard controls, text separation, mouse buttons/axes/wheel, gamepads,
chords, composites, contexts, dead zones, curves, accessibility, conflicts, and
user rebinding.

Core responsibilities:

- stable named actions;
- digital, axis1D, and axis2D value types;
- gameplay/UI/editor contexts;
- canonical control tokens such as `keyboard.w`, `mouse.delta_x`, and
  `gamepad.south`;
- default device schemes;
- triggers and ordered processors;
- context-aware conflict policy;
- stable IDs and full-name collision validation;
- deterministic cooked tables;
- sparse user overrides and unbind tombstones;
- contract-hash migration and orphan preservation;
- focus/device-loss release recovery.

Native numeric key codes, SDL instance IDs, Qt enums, arbitrary command strings,
and current held state must not be serialized as the primary binding identity.
Text/IME input remains a separate System/UI path.

Complete design, schema examples, limits, runtime rules, and acceptance criteria
are in [formats/INPUT_ACTIONS.md](formats/INPUT_ACTIONS.md).

## 19. Package Archive

| Archive property | Value |
| --- | --- |
| Extension | `.cypak` |
| Version | 10 |
| Status | Reader, writer, and VFS mount implemented with uncompressed payloads |

The 16-byte magic is `CYPACKAGE` followed by seven zero bytes. Version 10 is a
separate little-endian field.

### 19.1 Layout

```text
136-byte header
64-byte entry records
NUL-terminated canonical path strings
padding
aligned payloads
```

Header data includes archive size, file count, index/string/data section ranges,
flags, archive-hash field, and reserves.

Each entry records path and payload offsets/sizes, unpacked size, timestamp,
content hash, path hash, compression, and flags.

### 19.2 Paths and lookup

Paths are canonical lowercase ASCII relative paths with `/`. Absolute paths,
drives, traversal, invalid components, embedded NUL, control bytes, and invalid
characters are rejected. Hash lookup confirms the complete path so a hash
collision does not alias files.

### 19.3 Publication

The writer stages a sibling temporary file, writes explicit little-endian
records and zero padding, flushes, and atomically replaces the destination. VFS
can mount and read the resulting archive.

### 19.4 V10 gaps

- The deterministic flag is accepted but not used; timestamps always enter the
  archive, so equal content with changed modification time changes bytes.
- The fail-on-duplicate flag is accepted but duplicates are always rejected.
- `defaultCompression` is accepted but never applied.
- Only `NONE` compression works; LZ4/Zstandard and compressed index remain
  unavailable.
- Whole-archive hashing is not emitted by the writer and verification is not
  implemented for a present archive hash.
- File hashes use noncryptographic FNV and do not authenticate content.
- Hash flags and populated hash fields are not fully cross-validated.
- Reader allocation has no practical engine caps for hostile file count,
  string-table bytes, or archive bytes.
- Reserved-zero, alignment, payload aliasing, gap, and trailing-data canonical
  checks are incomplete.
- Alignment arithmetic needs complete overflow hardening.
- Writer memory is proportional to total staged package contents.
- Signing, encryption, signer identity, and rollback protection are absent.

The format catalog must describe V10 as ordered and hash-bearing, not fully
reproducible, until timestamp policy is corrected.

## 20. Candidate and Missing Format Families

This chapter records the audit result. It does not freeze every suggested name.

### 20.1 Priority 1: playable-loop and foundation data

#### Input actions and bindings

Use the family described in [Input Actions and User Bindings](#18-input-actions-and-user-bindings).
It is required for the free camera, REAP movement, UI navigation, rebinding, and
action-frame capture.

#### Self-hosted schema definition

Candidate: `.cyschema`.

Purpose:

- data-driven schema registry;
- editor property generation;
- hover/help metadata;
- migrations;
- generated bindings;
- generic data validation;
- mod-facing extension policy.

Do not replace the current C++ schema system until bootstrapping, trust, versioning,
and code-generation behavior are proven.

#### Generic typed gameplay data

Candidate: `.cydata` -> `.cydata_c`, schema selected by document header.

Suitable content:

- weapons;
- items and pickups;
- damage definitions;
- player movement tuning;
- enemies;
- wave tables;
- difficulty profiles;
- game modes;
- AI tunables;
- tags and response tables.

Avoid one extension per gameplay noun. `.cyweapon`, `.cyenemy`, and `.cywave`
would multiply compiler and tooling contracts without providing different storage
needs. A specialized format should appear only when its runtime representation
and cooker truly differ.

#### Manifest family

`.cymanifest` needs exact schemas rather than one vague document:

- `cypher.resource_manifest`;
- `cypher.preload_manifest`;
- `cypher.package_manifest`;
- `cypher.release_manifest`.

These schemas can share an extension while retaining independent versions and
consumers.

### 20.2 Priority 2: content production

#### Localization and captions

Candidates:

- `.cyloc` -> `.cyloc_c` for locale-key catalogs;
- `.cycaption` -> `.cycaption_c` for timed subtitles and accessibility captions.

The engine already contains localization foundations and tool plans. Locale
fallback, placeholder typing, plural/category policy, coverage validation, font
coverage, and hot-reload behavior need explicit contracts.

#### Animation graph

`.cyanim` should remain clip/sample data. A candidate `.cyanimgraph` owns:

- states;
- transitions;
- parameters;
- blend trees;
- events;
- masks;
- root-motion policy;
- synchronization groups;
- runtime evaluator data.

#### Audio split

The current `.cysnd` row is too broad if it means both source sample/stream
recipes and event logic.

Recommended responsibility split:

- `.cysnd`: imported sample or stream resource and compression/streaming policy;
- provisional `.cyaudioevent`: random/sequence layers, conditions, attenuation,
  spatial and concurrency policy;
- provisional `.cymix`: buses, sends, snapshots, ducking, effects, and master mix.

Exact names remain provisional until the Audio runtime consumes them.

#### Post-processing

Candidate `.cypostfx` for exposure, tone mapping, bloom, color grading, masks,
volumes, transitions, and optional LUT dependencies. It should compile to a
runtime profile rather than embed backend handles.

#### VFX

Use `.cyparticle` as the initial VFX/effect graph umbrella. Do not add a duplicate
`.cyvfx` family until non-particle effects demonstrate a different ownership or
runtime contract.

### 20.3 Priority 3: generated runtime records

#### Replay/demo

Candidate `.cyreplay` / FourCC `CYRP`.

It should record normalized simulation actions or versioned user commands,
authoritative events/snapshots as needed, build/protocol identity, map/resource
identity, tick rate, checkpoints, and integrity metadata. It should not record raw
OS keyboard or transient controller events as the replay contract.

#### Save/checkpoint/profile

Candidate `.cysave` / FourCC `CYSV`.

It needs explicit game/build/schema versions, migration steps, transaction/backup
behavior, checksums, optional compression, user/profile identity, and a clear
policy for unavailable mods or content. Save data is hostile input when shared.

### 20.4 Priority 4: distribution and extension metadata

Candidates:

- `.cymod`: identity, dependencies, compatibility, mounts, packages, permissions;
- `.cyplugin`: ABI/API version, exported services, dependencies, target support,
  load phase;
- structured server profile or map rotation under an exact CYKV schema;
- release and patch manifests with package hashes, lineage, and signatures.

### 20.5 Tool-local and recovery data

Possible internal formats:

- `.cymap.user` for personal viewport cameras, selection, visibility, bookmarks,
  and layout associated with a map;
- recovery journal;
- autosave snapshot;
- workspace state;
- import/migration report;
- asset dependency index;
- derived-data-cache index;
- cook database;
- thumbnail cache.

These data sets still require versioning and corruption handling. They do not all
need public `.cy*` extensions. SQLite or another internal representation may be
more appropriate for query-heavy caches.

### 20.6 Formats intentionally deferred or avoided

- `.cyassetmeta` sidecars are deferred. Current resource identity is canonical
  path plus resource type. Rename-stable GUID identity would require an ADR and
  coordinated asset database/reference/package migration.
- Network packets are protocol records, not authored assets.
- Crash dumps remain platform-native with a small portable manifest.
- Logs and build reports remain text or NDJSON.
- Backend-specific shader/texture payloads remain target outputs of existing
  compilers.
- Existing standard source media remain standard source media.

## 21. Resource Compiler

| Compiler property | Value |
| --- | --- |
| Product | `CypherResourceCompiler` 1.0.0 |
| API | Registry-driven format compiler host |
| Reference | [CYPHER_RESOURCE_COMPILER.md](CYPHER_RESOURCE_COMPILER.md) |

### 21.1 Current format support

| Source | Cooked | Compiler ID |
| --- | --- | --- |
| `.cyshader` | `.cyshader_c` | `cypher.shader` |
| `.cytex` | `.cytex_c` | `cypher.texture` |
| `.cymat` | `.cymat_c` | `cypher.material` |

### 21.2 Commands

| Command | Purpose |
| --- | --- |
| `compile` | Validate and transactionally publish cooked output |
| `validate` | Run compiler checks without writing artifacts |
| `list-compilers` | List live registered compiler modules |
| `describe-compiler` | Show one compiler's identity and capabilities |
| `list-formats` | Show source-to-cooked routes backed by live modules |
| `completion zsh` | Emit zsh completion |
| `--help` | Generated help |
| `--version` | Stable product version |

Examples:

```bash
CypherResourceCompiler validate -s assets shaders/world.cyshader
CypherResourceCompiler compile -s assets -o cooked shaders/world.cyshader
CypherResourceCompiler compile -s assets -o cooked -r shaders
CypherResourceCompiler list-formats --output-format json
```

Quote wildcard input in shells such as zsh:

```bash
CypherResourceCompiler compile -s assets -o cooked 'shaders/*.cyshader'
```

### 21.3 Key options

- repeatable file/directory/wildcard input;
- recursive discovery;
- source and output roots;
- explicit target;
- development/release/shipping profile;
- requested worker count;
- text or NDJSON output;
- progress and color policy;
- verbosity;
- warnings as errors;
- keep-going behavior.

Version 1 records `--jobs` but executes sequentially.

### 21.4 Machine output

NDJSON output can include:

- progress;
- diagnostics;
- events;
- dependencies;
- artifacts;
- final `cypher.tool-report.v1` record.

Machine mode emits no banner or ANSI sequences. Each nonempty line is one JSON
object.

### 21.5 Response files

Response files support quoted arguments, escapes, `#`/`//` comments, nested
response files, and cycle detection. Current bounds are 16 nested files, 65,536
arguments, and 16 MiB copied argument text.

### 21.6 Exit codes

| Code | Meaning |
| ---: | --- |
| 0 | Success |
| 1 | Validation/compilation failure |
| 2 | Invalid command-line use |
| 3 | Invalid project/configuration |
| 4 | Filesystem/cache/infrastructure failure |
| 5 | Internal or out-of-memory failure |
| 6 | Cancellation |

### 21.7 Deferred coordinator behavior

- project-aware roots and target profiles;
- complete dependency closure and reverse queries;
- content-addressed incremental cache;
- actual parallel scheduling;
- remote workers;
- package/release coordination;
- shader variant matrices;
- migration orchestration.

No-op command flags must not advertise these before implementation.

## 22. Runtime Resource Loading

`CypherResource` provides typed registration, reference ownership, VFS reads, and
complete blob lifetime. Render-resource runtime loaders:

1. resolve a canonical cooked path through VFS;
2. enforce a per-format maximum file size;
3. read the entire file into owned storage;
4. validate CYRS and domain data;
5. publish a typed borrowed view after complete success;
6. retain storage until the final resource reference is released.

Typical current default caps are approximately:

- cooked shader: 33 MiB;
- cooked texture: 513 MiB;
- cooked material: 1 MiB.

A view's strings, records, code, and payload spans become invalid when its resource
owner is released. Native renderer objects must copy the metadata they need or
retain the resource dependency.

### 22.1 Runtime/source boundary

Runtime loaders consume cooked files. They do not:

- parse source CYKV;
- discover source dependencies;
- invoke offline compilers;
- resolve editor documents;
- create Qt objects;
- write source files.

Development hot reload may coordinate cooker output and resource replacement, but
the boundary remains explicit.

## 23. Diagnostics and Failure Behavior

### 23.1 Diagnostic properties

A useful diagnostic contains:

- stable code;
- severity;
- subsystem/tool identity;
- canonical virtual path;
- source line and column when available;
- schema field path;
- element index when relevant;
- expected and actual type/value summary;
- dependency/include chain;
- bounded native diagnostic path where policy permits;
- concise repair guidance.

### 23.2 Failure classes

| Class | Examples |
| --- | --- |
| Syntax | Invalid token, string, number, delimiter |
| Header/version | Wrong magic, language, schema, resource generation |
| Structural schema | Missing/unknown member, wrong container type |
| Semantic | Invalid path, duplicate ID, illegal range, cross-field conflict |
| Dependency | Missing, wrong type/version, cycle, stale interface |
| Compiler | GLSL link error, unsupported image policy, variant gate |
| Cooked validation | Bad offset, overlap, hash, padding, record order |
| Runtime | Unsupported backend, native compile/upload failure, stale handle |
| Infrastructure | I/O, permission, memory, cancellation |

### 23.3 Transaction rules

- Failed parse leaves no partial document.
- Failed decode leaves no partial typed view.
- Failed cook preserves the previous artifact.
- Failed resource load publishes no resource.
- Failed hot reload keeps the previous valid object where possible.
- Failed user-setting save preserves the previous file.
- A batch compiler may continue independent inputs while returning a failing
  aggregate exit class.

### 23.4 Tool inspection requirements

Every frozen binary resource should eventually have:

- validator;
- inspector/dumper;
- version report;
- dependency report;
- semantic hash report;
- malformed-input tests;
- maximum-size tests;
- deterministic golden output;
- migration or compatibility explanation.

## 24. Determinism, Security, and Trust

### 24.1 Determinism

A deterministic compiler identifies all inputs:

- canonical source document;
- exact source schema and language versions;
- compiler API and implementation version;
- cooked container/resource/metadata versions;
- direct and transitive dependency content;
- target and profile;
- relevant toolchain/library identity;
- expansion/build context;
- versioned configuration.

Records that are semantically unordered are sorted canonically. Padding is zero.
Host-native structures and pointers are never serialized.

Determinism claims must state their qualification boundary. The current texture
mip path uses host math library behavior for operations such as power and square
root. Repeated cooks on a pinned build image are qualified; arbitrary
cross-architecture byte identity is not yet proven.

### 24.2 Hostile input

All source, cooked, package, save, replay, mod, and user files can be malformed.
Readers use:

- explicit byte limits;
- checked arithmetic;
- depth and count limits;
- canonical path validation;
- exact flag/version allowlists;
- ordered contained range checks;
- UTF-8 and embedded-NUL policy;
- collision validation with full identity;
- transactional output;
- fuzzing and malformed-input tests.

### 24.3 Hashes versus trust

XXH3 and FNV hashes currently support identity, cache behavior, lookup, and
accidental-corruption detection. A hash stored beside mutable data does not prove
who created it.

Authenticity requires a separate policy covering:

- cryptographic hash;
- signature algorithm;
- trusted signer identity;
- key lifecycle;
- canonical signed region;
- version and rollback policy;
- mod/community trust;
- failure behavior.

The broader policy is in [security_model.md](security_model.md).

### 24.4 Dependency resolution security

Future CYKV includes and other source dependencies must enforce:

- logical VFS paths;
- no absolute native paths;
- no `..` traversal;
- no URL/network fetch;
- allowlisted source mounts;
- cycle detection;
- depth/file/byte/node limits;
- complete dependency hashing;
- include-chain diagnostics;
- no environment/time/random hidden inputs.

## 25. Compatibility and Migration Policy

### 25.1 Reader policy

A reader may support old generations when:

- shipped or checked-in data still needs them;
- the old meaning is unambiguous;
- compatibility has dedicated tests;
- support does not weaken current validation.

Unknown generations fail. Readers do not infer versions.

### 25.2 Writer policy

Writers emit the current canonical generation unless an explicit compatibility
command exists. Opening and saving an old map currently upgrades it to map V3.
Other editors must document whether they preserve or upgrade source generations.

### 25.3 Migration policy

A migration has:

- exact input identity/version;
- exact output identity/version;
- deterministic transformation;
- migration implementation version;
- diagnostics and loss report;
- transactional destination;
- golden tests;
- editor preview or explicit user command when author intent could change.

Schema migration, encoding migration, and cooked-resource recooking are separate
operations.

### 25.4 Deprecation policy

Before removal:

1. mark the route deprecated in this manual and tool output;
2. provide an inspector/migration route;
3. convert checked-in content;
4. retain compatibility for the stated window;
5. remove only after tests and product policy agree;
6. record removal in the changelog.

## 26. Documentation Rules

Every format chapter should follow this template:

1. Purpose and owner.
2. Status by source/compiler/cooked/runtime/editor layer.
3. Extension, magic/FourCC, and current versions.
4. Compatibility matrix.
5. Source header and complete field reference.
6. Defaults, ranges, units, uniqueness, and ordering.
7. Dependency types and resolution rules.
8. Compiler support and deliberate gates.
9. Cooked header/chunks/records and byte order.
10. Limits and allocation policy.
11. Canonicalization and identity.
12. Runtime ownership and lifetime.
13. Diagnostics and transactional behavior.
14. Security/trust boundary.
15. Examples.
16. Migration policy.
17. Tests, inspector, and tools.
18. Known limitations.
19. Version history.

The manual follows the useful structure of mature editor documentation: define
terms before workflows, keep a navigable table of contents, separate conceptual
models from UI actions, document platform paths and preferences, enumerate
formats and versions, and provide cross-links instead of assuming readers know
repository layout.

## 27. Contributor Checklists

### 27.1 New source schema

- [ ] Real consumer and owner identified.
- [ ] Language/schema ID and version selected.
- [ ] Closed/open object policy explicit.
- [ ] Complete field table written.
- [ ] Defaults, ranges, units, and path rules written.
- [ ] Count, byte, and nesting limits selected.
- [ ] Structural descriptor implemented.
- [ ] Typed semantic decoder implemented transactionally.
- [ ] Unknown version rejected.
- [ ] Minimum, representative, maximum, and malformed tests added.
- [ ] Canonical example checked in.
- [ ] Manual, catalog, and changelog updated.

### 27.2 New cooked resource

- [ ] Resource FourCC and version selected.
- [ ] CYRS or alternative container decision recorded.
- [ ] Byte order explicit.
- [ ] Header/chunk/record widths documented.
- [ ] No native pointer, handle, enum layout, or padding serialized.
- [ ] Offsets and size arithmetic checked.
- [ ] Canonical order and zero padding defined.
- [ ] Hash coverage defined.
- [ ] Reader validates before publication.
- [ ] Writer size query and output agree.
- [ ] Old versions handled explicitly.
- [ ] Round-trip, golden, maximum, and hostile tests added.
- [ ] Runtime owner and lifetime implemented.
- [ ] Inspector/validator updated.
- [ ] Manual, catalog, and changelog updated.

### 27.3 New compiler

- [ ] Reusable library separated from CLI process concerns.
- [ ] Input/source and output/cooked versions exact.
- [ ] VFS-only dependency access.
- [ ] Direct and transitive dependencies reported.
- [ ] Target/profile inputs included in identity.
- [ ] Toolchain/library identity included where output can change.
- [ ] Validation-only mode supported.
- [ ] Cancellation checked at bounded points.
- [ ] Output publication transactional.
- [ ] Stable diagnostics and exit class.
- [ ] Deterministic repeat test.
- [ ] Benchmark for expensive representative work.
- [ ] Registry/help/list-formats integration.

### 27.4 New manual chapter

- [ ] Scope and audience stated.
- [ ] Status label present.
- [ ] Implemented and proposed behavior separated.
- [ ] Terms defined before use.
- [ ] Table of contents updated.
- [ ] Relative links checked.
- [ ] Code examples identified as working or illustrative.
- [ ] Limits and failure behavior included.
- [ ] Source of truth linked.
- [ ] Version history updated.

## 28. Glossary

| Term | Definition |
| --- | --- |
| Authored source | Human/tool-edited input preserving author intent |
| Canonical path | Normalized VFS-relative resource identity |
| Canonical representation | One deterministic serialization for one semantic value |
| Cooked resource | Offline-produced bounded runtime data |
| CYKV | Cypher KeyValues typed source-data language |
| CYRS | Generic Cypher cooked-resource container |
| Decoder | Converts a validated generic tree into a typed domain view/model |
| Dependency | Another resource whose identity/content affects a result |
| FourCC | Four-byte domain resource or chunk identifier |
| Generation | Explicit format/schema/resource version |
| Handle generation | Counter preventing stale runtime handles from aliasing reused slots |
| Hot reload | Transactional replacement while retaining the previous valid state on failure |
| Logical binding | Backend-neutral named shader/material interface member |
| Manifest | Exact list/graph and policy for resources, packages, preload, or release |
| Provenance | Paths, versions, dependencies, context, and tools that produced data |
| Resource view | Validated borrowed spans into owned resource storage |
| Schema | Domain shape and semantic contract selected by ID/version |
| Semantic hash | Hash of canonical typed meaning rather than source layout |
| Source hash | Compiler identity over source, dependencies, target, versions, and toolchain |
| Transactional | Publishes only after complete success |
| VFS | Virtual filesystem providing canonical resource paths over providers/mounts |

## 29. Reference Index

### 29.1 Project and architecture

- [Documentation hub](index.md)
- [Current status](current_status.md)
- [Architecture](architecture.md)
- [Project structure](project_structure.md)
- [Subsystems](subsystems.md)
- [Subsystem source catalog](subsystem_source_catalog.md)
- [CypherCommon architecture](cyphercommon_architecture.md)
- [World/runtime module map](world_runtime_module_map.md)
- [Renderer/Host module map](renderer_host_module_map.md)
- [Function-pointer policy](function_pointer_policy.md)

### 29.2 Build and toolchain

- [Build guide](build_guide.md)
- [Resource compiler](CYPHER_RESOURCE_COMPILER.md)
- [Tool suite](tool_suite.md)
- [Toolchain plan](toolchain_plan.md)
- [Coding style](coding_style.md)

### 29.3 Formats

- [Format catalog](formats/FORMAT_CATALOG.md)
- [CYKV 1 specification](formats/CYKV.md)
- [CYKV 2 proposal](formats/CYKV_2_PROPOSAL.md)
- [CYKV schemas](formats/CYKV_SCHEMAS.md)
- [Renderer asset contracts](formats/RENDER_ASSETS.md)
- [Input actions and bindings proposal](formats/INPUT_ACTIONS.md)
- [Map authoring and Mason](map_authoring_and_mason.md)

### 29.4 Runtime and editor

- [Renderer first draw](renderer_first_draw.md)
- [Renderer host surface](renderer_host_surface.md)
- [Tile Editor reference research](tile_editor_reference_research.md)
- [Tile Editor navigation research](tile_editor_navigation_research.md)
- [Picasso V1 design](PICASSO_V1.md)
- [Picasso workflow reference](picasso_ui_workflow_reference.md)

### 29.5 Planning and evidence

- [Six-month engine plan](six_month_engine_plan.md)
- [Master plan](master_plan.md)
- [Development phases](development_phases.md)
- [Roadmap](roadmap.md)
- [Non-renderer validation](non_renderer_validation_2026-09-16.md)
- [Non-renderer work log](non_renderer_work_log.md)
- [Changelog](../CHANGELOG.md)

### 29.6 Policy and external lessons

- [Reference policy](reference_policy.md)
- [Reference-engine lessons](reference_engine_lessons.md)
- [Source 2 tooling reference](source2_tooling_reference.md)
- [Security model](security_model.md)

## 30. External Design References

CypherEngine's formats are independent. External projects are studied for durable
responsibility boundaries, documentation practices, and failure lessons.

### 30.1 Valve

- [Valve Source SDK 2013 KeyValues header](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/public/tier1/KeyValues.h)
- [Valve Source SDK 2013 KeyValues implementation](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/tier1/KeyValues.cpp)
- [Valve Data Model interfaces](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/public/datamodel/idatamodel.h)
- [Valve Data Model attribute types](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/public/datamodel/dmattributetypes.h)
- [Valve VTF public contract](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/public/vtf/vtf.h)
- [Valve material interface](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/public/materialsystem/imaterial.h)

Lessons retained:

- relative data composition and base/default layering are useful;
- encoding version and domain-format version are separate concepts;
- strongly typed domain attributes help tools;
- migrations are explicit stages;
- author-facing material data remains separate from native rendering objects.

Behaviors Cypher deliberately avoids:

- silent required-include failure;
- duplicate object keys with first-match lookup;
- parser meaning controlled by an API escape-mode switch;
- host-library numeric inference as the format definition;
- substring platform conditionals over hidden process state;
- global parser/token state;
- runtime pointers in binary data;
- binary data without magic, version, limits, schema identity, and integrity.

### 30.2 TrenchBroom

- [TrenchBroom Reference Manual](https://trenchbroom.github.io/manual/latest/)
- [TrenchBroom repository](https://github.com/TrenchBroom/TrenchBroom)
- [TrenchBroom manual source](https://github.com/TrenchBroom/manual)

Documentation lessons retained:

- state the manual's audience and scope;
- introduce the conceptual model before editing operations;
- maintain a strong navigable hierarchy;
- document preferences, shortcuts, paths, formats, and version history;
- distinguish editor concepts from serialized map representation;
- explain failure recovery, autosaves, and external compilation workflow;
- cross-link exact topics instead of requiring a linear read.

### 30.3 Legal and provenance boundary

Reference engines and tools are studied as architecture and documentation
examples. Cypher's CYKV grammar, schemas, CYRS container, FourCCs, binary layouts,
hashes, IDs, limits, compilers, and runtime APIs are independently designed.
Code is not copied wholesale. Any future adapted code requires provenance and
license review before merge.
