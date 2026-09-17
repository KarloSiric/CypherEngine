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
//  - Expanded into a field-complete format reference on 2026-09-17
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CypherEngine Reference Manual

| Manual property | Value |
| --- | --- |
| Edition | 0.2 living reference |
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

### 9.1 Required contents of a format reference

An implemented format is not considered documented by naming its purpose alone.
Its reference entry must contain, as applicable:

1. extension, magic/FourCC, language, schema, container, resource, metadata, and
   compiler versions;
2. producer, owner, compiler, reader, runtime consumer, editor, and write policy;
3. every serialized field or record member in declaration or byte order;
4. exact value type, required/optional state, default, legal values, range, units,
   and version availability for every field;
5. closed/open object policy, duplicate rules, ordering, identity, and reference
   resolution;
6. nested object, array-element, flag, enum, chunk, and binary-record contracts;
7. complete valid examples and important invalid examples;
8. hard limits, overflow behavior, allocation bounds, and hostile-input rules;
9. canonical writer, hashing, dependency, and deterministic-build behavior;
10. compatibility, migration, deprecation, and unknown-version behavior;
11. current compiler and runtime support, including recognized-but-gated values;
12. stable diagnostics and transactional failure behavior.

Field tables use these conventions:

| Column | Meaning |
| --- | --- |
| Field/member | Exact serialized spelling; dotted names describe nesting |
| Type | CYKV type or explicit fixed-width binary type |
| Req. | `Yes`, `No`, or the condition that makes the value mandatory |
| Default | Semantic value used when an optional field is absent; `—` means no default |
| Versions | Source/resource generations that accept the field |
| Values/constraints | Closed enum, numeric range, path rule, count limit, units, or cross-field condition |
| Meaning | Domain responsibility and effect |

For a binary format, byte offsets are relative to the beginning of the named
header or record. Multi-byte numeric values are little-endian unless the format
entry explicitly says otherwise. Reserved bytes are required to be zero when the
reader enforces that rule; otherwise the entry identifies the current validation
gap.

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
| Commands/CVars | `.cfg` (`.cycfg` is a documented alias only) | None | Two unversioned command-stream APIs; Host uses `CypherConfig`/`.cfg`, Tier1 is test/benchmark-only, and no `.cycfg`-specific runtime exists |
| Generic cooked resource | N/A | CYRS container V1 | Implemented |
| Shader | `.cyshader` V1/V2 | `.cyshader_c`, CYSH V2/V3 | Compiler and loader implemented; general runtime binding partial |
| Texture | `.cytex` V1/V2 | `.cytex_c`: CYTX V1 read compatibility, V2 current/write | Compiler and loader implemented; general upload/streaming partial |
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

CYKV is CypherEngine's owned, typed, hierarchical authoring-data language. The
current implementation is **CYKV language version 1**. It is used as the source
representation for project data and resource recipes, then validated by an exact
domain schema before a domain decoder or compiler consumes it. CYKV is not a
script language, package format, database, or replacement for specialized cooked
runtime resources.

This section documents the behavior of the shipped Tier1 implementation. The
dedicated [CYKV 1 draft specification](formats/CYKV.md) records the intended
contract; the implementation links below are authoritative for what the current
build accepts and emits:

- [semantic document and value API](../src/CypherCommon/Tier1/CypherCommon_KeyValue.h#L31-L162)
- [bounded transactional parser API](../src/CypherCommon/Tier1/CypherCommon_KeyValueParser.h#L30-L91)
- [deterministic writer and canonical hash API](../src/CypherCommon/Tier1/CypherCommon_KeyValueWriter.h#L30-L101)
- [generic binary pack API](../src/CypherCommon/Tier1/CypherCommon_KeyValuePack.h#L38-L81)

### 11.1 Source, schema, and cooked-data boundary

The normal data path is:

```text
UTF-8 CYKV source
    -> Tier1 parse
    -> owned typed document
    -> Tier2 exact schema validation
    -> domain decode or compilation
    -> specialized runtime state or cooked resource
```

The language version and schema version are independent. `@cykv 1` selects the
grammar and primitive semantics described here. `@schema "cypher.material" 2`,
for example, selects version 2 of one domain contract while the source still uses
CYKV language version 1. A schema revision therefore does not require a CYKV
language revision.

### 11.2 Encoding and document framing

A text document uses UTF-8 without a byte-order mark. The parser validates the
complete input as UTF-8, rejects embedded NUL bytes, accepts LF and CRLF line
endings, and rejects a bare CR. The first byte must be `@`; leading whitespace,
comments, and a UTF-8 BOM therefore fail the header check. Canonical output uses
LF. Diagnostic byte offsets are zero-based, while lines and columns are
one-based; columns count decoded Unicode scalar values. These checks are
implemented before transactional parsing begins
([source](../src/CypherCommon/Tier1/CypherCommon_KeyValueParser.cpp#L163-L235)).

Every CYKV 1 text document has exactly two required header lines followed by one
root object:

```cykv
@cykv 1
@schema "cypher.project" 1

{
    id = "reap"
    name = "REAP"
    start_map = "maps/facility.cymap"
}
```

The implemented framing grammar is:

```ebnf
document          = language-header, line-end,
                    schema-header, line-end,
                    trivia, object, trivia, end-of-input ;

language-header   = "@", horizontal-space*, "cykv",
                    horizontal-space+, positive-u32 ;
schema-header     = horizontal-space*, "@", horizontal-space*, "schema",
                    horizontal-space*, normal-string,
                    horizontal-space*, positive-u32 ;

positive-u32      = nonzero-decimal-digit, { decimal-digit } ;
schema-id         = component, ".", component, { ".", component } ;
component         = lowercase-letter,
                    { lowercase-letter | decimal-digit | "_" | "-" } ;
line-end          = LF | CRLF ;
horizontal-space  = " " | TAB ;
```

Header keywords are lowercase and exact. The token-based implementation permits
horizontal space between `@` and its keyword and does not currently require a
space around the schema-ID string; canonical output always uses the conventional
`@cykv 1` and `@schema "id" 1` spelling. Versions have no sign, prefix,
separator, or leading zero; both must be positive and fit in `u32`. A schema ID
has at least two nonempty dotted components, and each component starts with
`a`-`z`. The schema ID must be a normal double-quoted string, not a multiline
string. Comments are not enabled while the two header lines are being read.
Unknown CYKV versions return `UNSUPPORTED_VERSION`. The parser records schema ID
and version without resolving their availability; a caller later validates
against a selected descriptor or consults the optional Tier2 registry. Other
`@` directives are not part of CYKV 1. Header parsing and its exact source locations are implemented
in [CypherCommon_KeyValueParser.cpp](../src/CypherCommon/Tier1/CypherCommon_KeyValueParser.cpp#L641-L734).

### 11.3 Whitespace and comments

Outside the header, whitespace separates tokens and otherwise has no semantic
value. With the default parser options CYKV accepts:

- `//` line comments;
- `/* ... */` block comments;
- nested block comments, bounded by `nMaxCommentDepth`; and
- comments anywhere ordinary trivia is accepted, including between object
  members and after a value.

`#` is not a CYKV comment marker. Comments and exact whitespace are discarded by
the semantic parser. The current document model cannot reproduce them when it
writes the document again.

Comment support is controlled by `KEY_VALUE_PARSE_FLAG_ALLOW_COMMENTS`. When it
is disabled, comment delimiters are not accepted as trivia. The default maximum
nested block-comment depth is 64.

### 11.4 Semantic value model

The semantic tree has nine exact value types
([declaration](../src/CypherCommon/Tier1/CypherCommon_KeyValue.h#L35-L45)):

| Type | Text spelling | Stored meaning |
| --- | --- | --- |
| `NULL_VALUE` | `null` | Explicit null value |
| `BOOL` | `true`, `false` | Boolean |
| `I64` | `42`, `-7`, `0x2a` | Signed 64-bit integer |
| `U64` | `42u`, `0xffu` | Unsigned 64-bit integer |
| `F64` | `1.0`, `6.25e-2` | Finite IEEE-754 binary64 value |
| `STRING` | `"text"` or `"""..."""` | Owned UTF-8 byte string |
| `BINARY` | `hex"00ff80"` | Owned arbitrary bytes |
| `OBJECT` | `{ name = value }` | Insertion-ordered named children |
| `ARRAY` | `[value, value]` | Ordered unnamed children |

The parser performs no implicit coercion. An `I64` is different from a `U64`; a
numeric-looking string remains a string; and `null` remains a first-class value.
The root of a native CYKV 1 text document is always an object.

A document owns all nodes, member names, strings, binary blocks, and header data
through one explicit allocator. Node pointers remain valid until the node is
removed or the document is cleared or destroyed. Object lookup is case-sensitive
by default. A document created with `bCaseInsensitiveKeys = CY_TRUE` folds ASCII
case for lookup and duplicate detection while preserving the original spelling
([document options](../src/CypherCommon/Tier1/CypherCommon_KeyValue.h#L50-L61),
[lookup comparison](../src/CypherCommon/Tier1/CypherCommon_KeyValue.cpp#L358-L365)).

The direct tree API has a hard recursive-operation bound of 512 levels. That is a
library safety ceiling; the parser, writer, pack reader, and schema validator each
apply their own usually lower caller-configurable limits.

### 11.5 Objects and arrays

Native CYKV objects use `=` and separate members with trivia rather than commas:

```ebnf
object       = "{", trivia,
               [ member, { required-trivia, member }, trivia ],
               "}" ;
member       = key, trivia, "=", trivia, value ;
key          = bare-key | normal-string ;
```

The current parser's bare-key scanner accepts an ASCII letter, `_`, or a valid
non-ASCII UTF-8 sequence at the start. Later bytes may additionally contain
ASCII digits, `.`, `-`, and `/`. Quoted keys use the normal-string grammar.
Object member order is retained in the semantic document. Duplicate names are
rejected before insertion, using the destination document's key-comparison
policy. Commas and semicolons are not object separators.

Arrays use commas and preserve order:

```ebnf
array = "[", trivia,
        [ value, trivia,
          { ",", trivia, value, trivia },
          [ ",", trivia ] ],
        "]" ;
```

The final comma is accepted only when
`KEY_VALUE_PARSE_FLAG_ALLOW_TRAILING_COMMA` is set; that flag is enabled by
default. A leading comma, doubled comma, or missing comma is invalid. Empty
objects and arrays are valid. Container parsing, node limits, and direct-child
limits are enforced at insertion time
([implementation](../src/CypherCommon/Tier1/CypherCommon_KeyValueParser.cpp#L758-L933)).

### 11.6 Strings

Normal strings use double quotes. Single-quoted strings are not CYKV strings.
Raw line endings and raw control characters are invalid in a normal string. The
implemented escape vocabulary is:

| Escape | Decoded value |
| --- | --- |
| `\"` | quotation mark |
| `\\` | reverse solidus |
| `\/` | solidus |
| `\b` | backspace |
| `\f` | form feed |
| `\n` | line feed |
| `\r` | carriage return |
| `\t` | horizontal tab |
| `\uHHHH` | four-hex-digit Unicode code unit |
| `\UHHHHHHHH` | eight-hex-digit Unicode scalar value |

Two `\u` escapes may form one valid UTF-16 surrogate pair. Invalid or isolated
surrogates, values above `U+10FFFF`, unknown escapes such as `\x`, invalid UTF-8,
and a decoded `U+0000` are rejected. The parser explicitly validates the escape
spelling before invoking the shared decoder
([source](../src/CypherCommon/Tier1/CypherCommon_KeyValueParser.cpp#L333-L395)).

Multiline strings use triple double quotes:

```cykv
description = """
    Emergency lighting is active.
    Proceed to the lower facility.
    """
```

Their implemented rules are:

1. A line ending must immediately follow the opening `"""`.
2. The closing delimiter is on its own line after optional horizontal
   whitespace. Only horizontal whitespace and a line ending or end of input may
   follow it.
3. The whitespace before the closing delimiter defines the common margin.
4. Every nonblank content line must begin with that exact margin. Up to the
   margin width is removed from blank lines.
5. The line ending after the opening delimiter and the line ending immediately
   before the closing delimiter are excluded from the value.
6. Remaining CRLF sequences are normalized to LF.
7. The normal escape vocabulary is decoded after margin normalization.

A nonblank line with less or different indentation than the closing margin is
invalid. The normalization algorithm is implemented in
[CypherCommon_KeyValueParser.cpp](../src/CypherCommon/Tier1/CypherCommon_KeyValueParser.cpp#L397-L537).

### 11.7 Numbers and binary literals

An unsuffixed integer is an `I64`. A lowercase `u` suffix makes it a `U64`:

```cykv
health = 100
offset = -64
mask = 0xff00u
large_id = 18446744073709551615u
```

Integer rules are:

- decimal has no prefix; hexadecimal uses `0x`, octal uses `0o`, and binary uses
  `0b`;
- a signed integer may have `+` or `-`; an unsigned integer may not have a sign;
- `_` separators are allowed only between digits accepted for that component;
- decimal values other than zero may not have a redundant leading zero;
- a prefix must be followed by at least one digit valid for its base;
- an unsuffixed value must fit exactly in `i64`, and a suffixed value must fit
  exactly in `u64`; and
- the unsigned suffix is lowercase `u` in CYKV 1.

A token containing a decimal point or decimal exponent is an `F64`. Floating
point is decimal only. The whole part is required, a decimal point requires at
least one digit on both sides, and an exponent requires at least one digit after
its optional sign. Separators may occur only between digits. Base prefixes and
unsigned suffixes are invalid for floats. Parsing is locale-independent and
rejects out-of-range and non-finite results. Negative zero is a valid finite
binary64 value and its sign bit remains part of the semantic value.

**`NaN`, `Inf`, and `Infinity` are not implemented CYKV values.** They are
rejected by CYKV 1. The parser's number validation and exact signed/unsigned
dispatch are in
[CypherCommon_KeyValueParser.cpp](../src/CypherCommon/Tier1/CypherCommon_KeyValueParser.cpp#L1027-L1282).

Binary values use an explicit hexadecimal string:

```cykv
signature = hex"89504e470d0a1a0a"
```

The payload contains an even number of ASCII hexadecimal digits. Uppercase and
lowercase input digits decode to the same bytes. Whitespace, separators,
multiline spelling, and escapes are not accepted inside the payload. An empty
payload is valid. Text output always uses lowercase hex. Large payloads should
remain separate resources rather than being embedded in authoring documents.

### 11.8 Parser options, limits, results, and failure behavior

`KeyValue_ParseText` accepts caller policy and resource limits. Its defaults are
declared in
[key_value_parse_options_t](../src/CypherCommon/Tier1/CypherCommon_KeyValueParser.h#L59-L70):

| Option | Default | Exact effect in native CYKV parsing |
| --- | ---: | --- |
| `ALLOW_COMMENTS` | enabled | Enables `//`, `/* */`, and nested block comments after the header |
| `ALLOW_TRAILING_COMMA` | enabled | Allows one final comma in arrays |
| `ALLOW_UNQUOTED_KEYS` | enabled | Allows bare object keys |
| `REJECT_DUPLICATE_KEYS` | enabled | Public policy bit; native CYKV currently rejects duplicates even if this bit is cleared |
| `ALLOW_ROOT_VALUE` | disabled | Public policy bit; native CYKV currently requires an object even if this bit is set |
| `cbMaxInput` | 64 MiB | Maximum source byte count |
| `nMaxDepth` | 128 | Maximum parsed container depth; valid range is 1 through 512 |
| `nMaxNodes` | 1,048,576 | Maximum semantic node count, including the root |
| `nMaxContainerValues` | 1,048,576 | Maximum direct children of one object or array |
| `nMaxCommentDepth` | 64 | Maximum nested block-comment depth |
| `cbMaxStringData` | 64 MiB | Maximum bytes allocated in the document data arena during parse |

`cbMaxStringData` is an implementation-storage budget. Its reported
`cbStringData` includes copied member names, strings, binary bytes, the schema ID,
and the internal terminating NUL allocated for each copied string. It should not
be interpreted as only the sum of logical user payload lengths.

The parser returns the first failure plus a source location, the exact locations
of the language version, schema ID, and schema version, and final node/data
counters. Status values are:

| Status | Meaning |
| --- | --- |
| `OK` | The complete temporary document was committed |
| `INVALID_ARGUMENT` | Invalid source view, options, limits, flags, or destination document |
| `INPUT_LIMIT` | Source exceeds `cbMaxInput` |
| `INVALID_ENCODING` | Invalid UTF-8, embedded NUL, or bare CR |
| `INVALID_HEADER` | Missing or malformed required header syntax |
| `UNSUPPORTED_VERSION` | `@cykv` declares a language version other than 1 |
| `INVALID_SCHEMA` | Malformed schema ID or schema version |
| `LEXER_ERROR` | Tokenization failed |
| `SYNTAX_ERROR` | Token sequence or scalar spelling violates CYKV grammar |
| `DUPLICATE_KEY` | An object key repeats under the document comparison policy |
| `DEPTH_LIMIT` | Container nesting exceeds `nMaxDepth` |
| `NODE_LIMIT` | Total values exceed `nMaxNodes` |
| `CONTAINER_LIMIT` | One container exceeds `nMaxContainerValues` |
| `COMMENT_DEPTH_LIMIT` | Nested comments exceed `nMaxCommentDepth` |
| `STRING_LIMIT` | A token or aggregate document data exceeds the configured budget |
| `OUT_OF_MEMORY` | Temporary document allocation failed |
| `TRAILING_INPUT` | Nontrivia tokens follow the root object |

Parsing is transactional. It builds a sibling document with the destination's
allocator, initial capacities, and key-comparison policy. Only a complete success
moves that document into the destination; every failure preserves the previous
destination tree
([commit path](../src/CypherCommon/Tier1/CypherCommon_KeyValueParser.cpp#L1348-L1424)).

### 11.9 Text writing and canonical representation

The writer accepts a root object that belongs to a structurally valid document
with a CYKV 1 header. It can measure or write into a NUL-terminated bounded
buffer, or stream fragments through a callback. Default options are pretty
output, a final LF, four spaces per indentation level, and a maximum depth of
128.

| Writer flag | Behavior |
| --- | --- |
| `NONE` | Compact insertion-order output |
| `PRETTY` | Indentation and line breaks; ignored by canonical mode |
| `CANONICAL` | Stable compact spelling and byte-wise object-key ordering |
| `FINAL_NEWLINE` | Appends LF in noncanonical mode |
| `ASCII_ONLY` | Escapes non-ASCII code points in strings and keys |

Canonical mode currently emits:

- the exact `@cykv 1` and `@schema "id" version` header with LF;
- the root object immediately after the schema-header LF;
- every object key as a quoted string;
- object members sorted lexicographically by their key bytes, without mutating
  insertion order;
- one ASCII space between members in a compact object;
- arrays in semantic order, separated by commas;
- signed integers in decimal, unsigned integers in decimal with lowercase `u`,
  booleans and null in lowercase, and binary bytes as lowercase hex;
- finite `F64` values through `GENERAL` formatting with 17 significant digits
  and trailing-zero trimming, followed by `.0` when the result otherwise has no
  decimal point or exponent; and
- no comments, pretty whitespace, or final newline.

The exact implementation is in
[CypherCommon_KeyValueWriter.cpp](../src/CypherCommon/Tier1/CypherCommon_KeyValueWriter.cpp#L187-L325)
and
[the object/header output path](../src/CypherCommon/Tier1/CypherCommon_KeyValueWriter.cpp#L396-L628).
Canonical mode does not automatically enable `ASCII_ONLY`; canonical hashes use
the valid UTF-8 bytes directly.

Writer statuses are `OK`, `INVALID_ARGUMENT`, `INVALID_DOCUMENT`, `DEPTH_LIMIT`,
`OUT_OF_MEMORY`, `SIZE_OVERFLOW`, `OUTPUT_TRUNCATED`, and `SINK_FAILED`. A bounded
buffer reserves one byte for NUL, returns the complete `cchRequired`, and may
contain a prefix when it reports `OUTPUT_TRUNCATED`. The streaming API reports a
sink rejection as `SINK_FAILED`.

`KeyValue_HashCanonicalDocument` streams the complete canonical text, including
both header lines, into **XXH3-128 with seed 0**. The result contains the 128-bit
hash and the exact number of canonical bytes hashed. Comments, input whitespace,
input number spelling, and object insertion order do not affect the result after
they have produced the same semantic document. Array order, exact types, values,
schema ID, and schema version do affect it. XXH3 is a deterministic content/cache
identity here; it is not a cryptographic signature
([hash implementation](../src/CypherCommon/Tier1/CypherCommon_KeyValueWriter.cpp#L709-L746)).

### 11.10 CYKV generic binary pack version 1

The Tier1 packer serializes a generic semantic tree for storage, transport, and
tests. It is explicitly little-endian, bounds-checked, and self-identifying. It
is **not** a domain-specific cooked resource and does not replace CYRS-based
formats.

#### 11.10.1 File header

The fixed header is 40 bytes
([layout constants](../src/CypherCommon/Tier1/CypherCommon_KeyValuePack.cpp#L33-L37)):

| Offset | Size | Type | Field | Required value or meaning |
| ---: | ---: | --- | --- | --- |
| 0 | 4 | `u32` | `magic` | FourCC `CYKV` |
| 4 | 4 | `u32` | `version` | `1` |
| 8 | 4 | `u32` | `header_size` | `40` |
| 12 | 4 | `u32` | `flags` | `0`; unknown flags are rejected |
| 16 | 8 | `u64` | `total_bytes` | Exact complete input size |
| 24 | 8 | `u64` | `node_count` | Number of preorder node records; nonzero |
| 32 | 8 | `u64` | `data_bytes` | Sum of all name, string, and binary payload bytes |

`total_bytes` must equal `40 + 32 * node_count + data_bytes`, as implied by a
complete valid traversal, and no trailing bytes are accepted.

#### 11.10.2 Node record

Nodes are written in depth-first preorder. Each node begins with this 32-byte
record, immediately followed by `name_size` name bytes, then `value_size` payload
bytes, then each child record and payload recursively:

| Relative offset | Size | Type | Field | Meaning |
| ---: | ---: | --- | --- | --- |
| 0 | 1 | `u8` | `type` | `0` null, `1` bool, `2` i64, `3` u64, `4` f64, `5` string, `6` binary, `7` object, `8` array |
| 1 | 1 | `u8` | `flags` | Must be zero |
| 2 | 2 | `u16` | `reserved` | Must be zero |
| 4 | 4 | `u32` | `child_count` | Immediate children in preorder |
| 8 | 8 | `u64` | `name_size` | Name bytes following the record |
| 16 | 8 | `u64` | `value_size` | String or binary bytes following the name |
| 24 | 8 | `u64` | `scalar_bits` | Exact bool, integer, or binary64 bit pattern |

The root name must be empty. Array-element names must be empty. Containers have
zero `value_size` and zero `scalar_bits`. Scalars have zero children. Null has
zero payload and zero scalar bits; bool has scalar bits 0 or 1; signed and
unsigned integers use all 64 scalar bits; `F64` stores the exact finite binary64
bit pattern; string and binary values use their payload bytes and require zero
scalar bits. The reader rejects non-finite packed floats. These invariants are
checked in a complete first pass before any destination nodes are built
([record validation](../src/CypherCommon/Tier1/CypherCommon_KeyValuePack.cpp#L169-L283)).

Pack-reader defaults are 128 levels, 1,048,576 nodes, and 256 MiB of aggregate
name/value bytes. Depth must be between 1 and the library ceiling of 512; node
and data limits must be nonzero. Reading is transactional: a fully validated
stream is reconstructed in a sibling document and moved into the destination
only on success. Pack statuses are:

| Status | Meaning |
| --- | --- |
| `OK` | Complete write or transactional read succeeded |
| `INVALID_ARGUMENT` | Invalid tree, span, destination, or limits |
| `OUTPUT_TOO_SMALL` | Output span cannot hold the complete pack |
| `INVALID_MAGIC` | First FourCC is not `CYKV` |
| `VERSION_MISMATCH` | Packed-layout version is not 1 |
| `CORRUPT_DATA` | Header, counts, types, shapes, reserved fields, or stream extent are inconsistent |
| `LIMIT_EXCEEDED` | Caller depth, node, or data budget rejects the input |
| `OUT_OF_MEMORY` | Transactional reconstruction allocation failed |

The writer calculates `cbRequired` before writing and emits the tree in its
current insertion order. The reader returns consumed bytes on success and may
return the validation/construction offset on failure
([read/write implementation](../src/CypherCommon/Tier1/CypherCommon_KeyValuePack.cpp#L385-L557)).

#### 11.10.3 Pack limitations that callers must account for

Pack version 1 serializes **only the semantic tree**. It does not serialize the
document's CYKV language version, schema ID, or schema version. A document
produced by `KeyValuePack_Read` therefore has an empty document header until a
trusted caller explicitly restores it with `KeyValue_SetDocumentHeader`. Before
that restoration, native text writing, exact schema validation, and canonical
document hashing fail or report header mismatches.

The pack is insertion-order dependent and has no checksum or content hash. It
also does not enforce all text-language invariants: it does not validate UTF-8 or
embedded NULs in names and string payloads, require nonempty object-member names,
reject duplicate object names, require an object root, or normalize object order.
Those omissions make the binary pack a bounded tree transport, not a canonical
or self-contained CYKV document container.

### 11.11 Current conformance discrepancies and API hazards

The following differences between the intended CYKV 1 draft and the current code
must remain visible until the implementation or draft is changed:

| Area | Current implementation | Consequence |
| --- | --- | --- |
| Empty quoted object keys | The text parser accepts `"" = value` | The draft says empty keys are invalid |
| Bare keys | Valid UTF-8 may start or continue a bare key, and `/` is accepted after its first character | The draft's narrower ASCII `letter/_` plus `letter/digit/_.-` grammar is not what the parser enforces |
| Header token spacing | Horizontal space is accepted between `@` and its keyword, and the schema string/version need not be separated by spaces | Canonical output is strict, but the parser accepts more spellings than the draft grammar |
| Root-value flag | Native `KeyValue_ParseText` always requires `{...}` | `ALLOW_ROOT_VALUE` currently affects the internal strict-JSON route only |
| Duplicate-key flag | Native CYKV always rejects duplicate keys | Clearing `REJECT_DUPLICATE_KEYS` does not permit them in native CYKV |
| Canonical floats | Writer uses general formatting with 17 significant digits | The draft's stronger “shortest round-trippable” wording is not yet guaranteed |
| Programmatic strings and names | Direct setters/insertion accept byte views without CYKV UTF-8/NUL/uniqueness checks | A programmatically built tree can be structurally valid yet fail text writing or violate source-language rules |
| Binary pack identity | Pack omits language/schema header | A read tree is not a complete document until the caller restores trusted metadata |
| Binary pack validation | Pack accepts arbitrary name/string bytes, duplicates, empty object names, any root type, and insertion order | Pack acceptance does not imply CYKV text conformance or canonical identity |
| Source ranges | Only parse/header error locations are retained | Schema diagnostics have logical paths but no per-node source span |

The semantic builder also permits duplicate names because
`KeyValue_ObjectInsert` is a low-level insertion API rather than a validating
parser. Callers that build documents directly must enforce the same invariants
as the text parser before treating the tree as conforming CYKV
([insertion implementation](../src/CypherCommon/Tier1/CypherCommon_KeyValue.cpp#L382-L416)).

### 11.12 Proposed language evolution — not implemented in CYKV 1

The following items are design candidates for a later version. They are not
accepted by the current parser and must not appear in production CYKV 1 files:

- dependency-aware `include` or `base` composition with VFS-only resolution,
  explicit cycle/depth/byte limits, deterministic merge rules, and dependency
  reporting to the cooker;
- hygienic, bounded constants or macros expanded by an explicit preprocessing
  phase, with expansion limits and origin-aware diagnostics;
- additional domain-friendly scalar spellings such as stable resource-reference,
  vector, color, duration, angle, or identifier types, provided their canonical
  representation and schema semantics are specified first;
- opt-in semantic `nan`, `+inf`, and `-inf` categories only for fields whose
  future schema rule explicitly permits them, with fixed canonical spelling and
  hashing rather than platform NaN payloads;
- schema-authored defaults, migrations, deprecation replacements, editor hints,
  and generated bindings; and
- a lossless syntax tree/source map for comment-preserving editor writes.

The safest evolution keeps the core semantic tree small and represents most
engine concepts as schema-defined structures until a new primitive proves a
clear correctness or tooling benefit. Includes and macros must run before
canonical hashing and compilation, and their resolved dependencies must become
part of source identity. Non-finite floats remain outside the current language:
**CYKV 1 does not implement NaN or infinity.** Their schema-gated treatment is a
CYKV 2 design candidate documented in
[the language-evolution proposal](formats/CYKV_2_PROPOSAL.md#10-non-finite-floating-point),
not an accepted V1 value.

## 12. CYKV Schema and Configuration Formats

Tier2 schemas give a parsed CYKV tree domain meaning. They validate exact header
identity, types, members, ranges, and collection shape without mutating the
document. Typed decoders then enforce domain-specific invariants and produce a
small runtime view or value. Command configuration is a separate executable text
family and does not use CYKV syntax.

### 12.1 Tier2 static schema system

#### 12.1.1 Descriptor ownership and type masks

A `schema_descriptor_t` contains a borrowed canonical `schemaId`, a positive
exact `nVersion`, and a borrowed immutable `pRootRule`. Descriptors and all rule,
member, and allowed-value arrays must outlive registration and validation; the
schema system owns no descriptor memory. The complete public model is declared in
[CypherCommon_Schema.h](../src/CypherCommon/Tier2/CypherCommon_Schema.h#L30-L204).

`schema_rule_t::allowedTypes` is a mask of `NULL`, `BOOL`, `I64`, `U64`, `F64`,
`STRING`, `BINARY`, `OBJECT`, and `ARRAY`. `NUMBER` is the union of the three
numeric types and `ANY` is the union of every type. Multiple types may be accepted
by one rule; constraints are applied only when their corresponding actual type is
selected.

#### 12.1.2 Rule fields

| Rule area | Fields | Meaning and defaults |
| --- | --- | --- |
| Common | `allowedTypes` | Required nonzero mask; unknown bits are invalid |
| Object | `pMembers`, `nMembers` | Borrowed fixed-member table; pointer/count must either both be empty or both be present |
| Object | `pAdditionalMemberRule` | Optional rule for names absent from the fixed table |
| Object | `nMinMembers`, `nMaxMembers` | Inclusive total-child bounds; defaults 0 through unbounded |
| Object | `flags` | `SCHEMA_OBJECT_REJECT_UNKNOWN_MEMBERS` closes the object; it cannot be combined with an additional-member rule |
| Array | `pElementRule` | Required whenever `ARRAY` is allowed; shared by every element |
| Array | `nMinElements`, `nMaxElements` | Inclusive element-count bounds; defaults 0 through unbounded |
| String | `cbMinLength`, `cbMaxLength` | Inclusive UTF-8 **byte** length, not scalar count; defaults 0 through unbounded |
| String | `pAllowedValues`, `nAllowedValues` | Optional case-sensitive exact-value set; pointer/count must agree and entries must be unique and NUL-free |
| Binary | `cbMinSize`, `cbMaxSize` | Inclusive payload byte bounds; defaults 0 through unbounded |
| Signed integer | `nMin`, `nMax` | Inclusive `i64` bounds; defaults full `i64` range |
| Unsigned integer | `nMin`, `nMax` | Inclusive `u64` bounds; defaults 0 through `u64` maximum |
| Floating point | `flMin`, `flMax` | Inclusive finite `f64` bounds; defaults `-DBL_MAX` through `DBL_MAX` |

A fixed `schema_member_t` has `name`, `pRule`, and flags. No flag means optional.
`SCHEMA_MEMBER_REQUIRED` makes absence an error.
`SCHEMA_MEMBER_DEPRECATED` can emit a warning when present. Required and
deprecated are mutually exclusive in one descriptor. Member names must be
nonempty and NUL-free; duplicate fixed names are invalid.

Descriptor checking rejects null roots, malformed dotted schema IDs, version
zero, empty or unknown type masks, inconsistent rule fields, duplicate members,
invalid member flags, reversed or non-finite ranges, and rule graphs deeper than
128. Shared rules and recursive references are permitted; active DFS cycles are
recognized rather than recursed forever
([descriptor checker](../src/CypherCommon/Tier2/CypherCommon_Schema.cpp#L95-L245)).

Descriptor statuses are `OK`, `INVALID_ARGUMENT`, `INVALID_SCHEMA_ID`,
`INVALID_VERSION`, `INVALID_TYPE_MASK`, `INVALID_RULE`, `INVALID_MEMBER`,
`DUPLICATE_MEMBER`, `INVALID_RANGE`, and `DESCRIPTOR_DEPTH_LIMIT`.

#### 12.1.3 Validation, limits, and diagnostics

Validation is read-only and allocation-free. It first requires the document
header to match CYKV language version 1 and the descriptor's schema ID and version
exactly. If header identity fails, it emits the corresponding diagnostics and
does not traverse the tree. Otherwise it applies the root rule recursively.

Default validation options are:

| Option | Default | Meaning |
| --- | ---: | --- |
| `nMaxDepth` | 128 | Maximum document traversal depth |
| `nMaxNodes` | 1 Mi nodes | Maximum values visited |
| `bReportDeprecatedMembers` | true | Emits deprecated-member warnings |

`CY_SCHEMA_MAX_PATH` is 512 bytes including the NUL terminator. Diagnostic paths
are bounded JSON Pointer-style paths: root is `/`, object members append `/name`,
and arrays append `/0`, `/1`, and so on. In member names, `~` becomes `~0`, `/`
becomes `~1`, and ASCII control bytes become lowercase `~xHH`. An unrepresentable
path produces `PATH_LIMIT`; it is never ambiguously truncated
([path implementation](../src/CypherCommon/Tier2/CypherCommon_Schema.cpp#L248-L365)).

Each `schema_diagnostic_t` contains only a stable code, warning/error severity,
expected type mask, actual CYKV type, and logical path. The semantic document has
no node source ranges, so schema diagnostics cannot yet point to an exact source
span. Diagnostic codes are:

| Category | Codes |
| --- | --- |
| Header/registry | `LANGUAGE_VERSION_MISMATCH`, `SCHEMA_ID_MISMATCH`, `SCHEMA_VERSION_MISMATCH`, `SCHEMA_NOT_FOUND` |
| Shape/type | `TYPE_MISMATCH`, `MISSING_REQUIRED_MEMBER`, `UNKNOWN_MEMBER`, `DEPRECATED_MEMBER` |
| Scalar | `I64_RANGE`, `U64_RANGE`, `F64_RANGE`, `STRING_LENGTH`, `STRING_VALUE`, `BINARY_SIZE` |
| Container | `ARRAY_LENGTH`, `OBJECT_LENGTH` |
| Safety budget | `PATH_LIMIT`, `DEPTH_LIMIT`, `NODE_LIMIT` |

The result reports `nDiagnosticsRequired` even after caller storage fills,
`nDiagnosticsWritten`, error and warning counts, nodes visited, and
`bDiagnosticsTruncated`. Passing zero diagnostic capacity is a supported counting
pass. Warnings alone do not invalidate a document. Validation statuses are `OK`,
`INVALID_ARGUMENT`, `INVALID_SCHEMA`, `INVALID_DOCUMENT`, and
`SCHEMA_NOT_FOUND`
([validation implementation](../src/CypherCommon/Tier2/CypherCommon_Schema.cpp#L705-L768)).

#### 12.1.4 Registry

`schema_registry_t` stores borrowed descriptor pointers in caller-owned fixed
storage and performs no allocation. Registration validates the descriptor and
rejects a duplicate exact `(schema ID, version)` pair; different versions of one
ID may coexist. `SchemaRegistry_Find` and document validation use an exact pair.
`SchemaRegistry_FindLatest` returns the highest registered version only for tools
and migration discovery; it is never an automatic validation fallback. Registry
statuses are `OK`, `INVALID_ARGUMENT`, `INVALID_SCHEMA`, `DUPLICATE_SCHEMA`, and
`CAPACITY_EXCEEDED`
([registry contract](../src/CypherCommon/Tier2/CypherCommon_SchemaRegistry.h#L30-L81),
[implementation](../src/CypherCommon/Tier2/CypherCommon_SchemaRegistry.cpp#L66-L181)).

#### 12.1.5 Current generic-schema gaps

The implemented generic descriptor does not provide:

- default values or default insertion;
- normalization or type coercion;
- migration declarations or migration execution;
- resource-reference categories, extension checking, or dependency resolution;
- regular expressions or general identifier/path predicates;
- array element uniqueness or object-key pattern constraints;
- cross-field, cross-object, or cross-document constraints;
- custom validator callbacks;
- editor labels, descriptions, groups, units, widgets, or other presentation
  hints;
- per-node source ranges or a lossless syntax tree;
- a self-hosted `.cyschema` language or generated C/C++ bindings; or
- a headless standalone schema compiler/validator executable.

Domain decoders currently implement path, identifier, uniqueness, extension,
default, and cross-field policy after generic validation. These are gaps in the
generic foundation, not features that a schema descriptor silently supplies.

### 12.2 `.cyproject` — `cypher.project` version 1

**Implementation status:** the closed Tier2 schema and transactional typed
decoder are implemented and tested. No production Host or tool source currently
loads a `.cyproject`; file discovery, VFS I/O, and startup integration remain to
be connected. The decoder itself performs no file I/O.

```cykv
@cykv 1
@schema "cypher.project" 1

{
    id = "reap"
    name = "REAP"
    start_map = "maps/facility.cymap"
    search_paths = [
        "game",
        "engine",
        "mods/base",
    ]
}
```

The root is a closed object: unknown members are errors. Version 1 fields are:

| Path | CYKV type | Required/default | Exact constraints and meaning |
| --- | --- | --- | --- |
| `/id` | `STRING` | Required; no default | 1-64 bytes. First byte `a`-`z`; remaining bytes `a`-`z`, `0`-`9`, `_`, or `-`. Durable machine identity for tools, caches, and generated resources. |
| `/name` | `STRING` | Required; no default | 1-128 UTF-8 bytes. Human-readable display name; not persistent identity. |
| `/start_map` | `STRING` | Required; no default | 1-259 bytes. Relative canonical virtual-resource path with exact lowercase `.cymap` extension. |
| `/search_paths` | `ARRAY` | Optional; absence means zero search roots | If present, 1-64 elements. Order is retained and earlier entries have higher caller-defined mount priority. |
| `/search_paths/*` | `STRING` | One per array element | 1-259 bytes, canonical virtual path, exact duplicate paths forbidden. |

A canonical virtual path is nonempty, relative, lowercase printable ASCII, uses
forward slashes, and has no empty, `.` or `..` segment. It rejects backslashes,
whitespace, non-ASCII bytes, and `: * ? " < > |`. `start_map` additionally must
end in exact lowercase `.cymap`. These semantic checks are shared through
[`DataValidation_CheckCanonicalVirtualPath` and `DataValidation_CheckResourcePath`](../src/CypherCommon/Tier2/CypherCommon_DataValidation.cpp#L175-L240).

The static schema supplies type and byte/count bounds
([descriptor](../src/CypherCommon/Tier2/CypherCommon_ProjectSchema.cpp#L72-L105));
`ProjectManifest_Decode` then applies identifier grammar, canonical path,
extension, and duplicate-search-path checks
([decoder](../src/CypherCommon/Tier2/CypherCommon_ProjectManifest.cpp#L51-L157)).
It constructs a local result and modifies the caller's output only after every
check succeeds.

`project_manifest_view_t` is zero-copy. `id`, `name`, `startMap`, and every search
path borrow bytes from the parsed document, so that document must remain alive
and unchanged for the complete view lifetime. The fixed output contains at most
64 search-path views. Decode statuses are:

| Status | Meaning |
| --- | --- |
| `OK` | Validated and decoded successfully |
| `INVALID_ARGUMENT` | Null document/output or inconsistent diagnostic arguments |
| `INVALID_DOCUMENT` | Generic schema/header validation failed; inspect `validation` |
| `INVALID_PROJECT_ID` | `/id` violates stable identifier grammar |
| `INVALID_START_MAP` | `/start_map` is not a canonical `.cymap` resource path |
| `INVALID_SEARCH_PATH` | One search path is not canonical; `iSearchPath` identifies it |
| `DUPLICATE_SEARCH_PATH` | One path exactly repeats an earlier path; `iSearchPath` identifies it |
| `INTERNAL_ERROR` | A value could not be extracted after successful schema validation |

### 12.3 `.cysettings` — `cypher.settings` version 1

**Implementation status:** the closed Tier2 schema, compiled defaults, and
transactional owning decoder are implemented and tested. No production Host or
tool source currently discovers or loads a `.cysettings` file. Missing-file
policy and file I/O belong to the caller; `CypherSettings_Defaults` supplies the
fallback values.

```cykv
@cykv 1
@schema "cypher.settings" 1

{
    display = {
        width = 1920
        height = 1080
        mode = "borderless"
        vsync = true
    }
}
```

The root and `/display` objects are closed. Every field is optional:

| Path | CYKV type | Default | Exact constraints and meaning |
| --- | --- | --- | --- |
| `/display` | `OBJECT` | All compiled display defaults | Optional container; unknown members rejected |
| `/display/width` | `I64` | `1280` | Inclusive range 320-16384 pixels; `U64` is not accepted |
| `/display/height` | `I64` | `720` | Inclusive range 200-16384 pixels; `U64` is not accepted |
| `/display/mode` | `STRING` | `"windowed"` | Exact case-sensitive enum: `windowed`, `borderless`, or `fullscreen` |
| `/display/vsync` | `BOOL` | `true` | Requested presentation synchronization |

An empty root object is therefore a valid settings document and decodes to
1280x720, windowed mode, with VSync enabled. The schema is defined in
[CypherCommon_SettingsSchema.cpp](../src/CypherCommon/Tier2/CypherCommon_SettingsSchema.cpp#L79-L121).
The decoder starts from compiled defaults, applies validated optional overrides
to a local value, and commits once
([implementation](../src/CypherCommon/Tier2/CypherCommon_Settings.cpp#L99-L160)).
The returned `cypher_settings_t` owns all its state and remains valid after the
CYKV document is destroyed.

Settings decode statuses are:

| Status | Meaning |
| --- | --- |
| `OK` | Defaults and all present overrides decoded successfully |
| `INVALID_ARGUMENT` | Null document/output or inconsistent diagnostic arguments |
| `INVALID_DOCUMENT` | Generic schema/header validation failed; inspect `validation` |
| `INTERNAL_ERROR` | A value could not be extracted after successful schema validation |

### 12.4 `.cfg` and `.cycfg` command configuration

Command configuration is an ordered stream with side effects. It is deliberately
separate from CYKV: it has no `@cykv` or `@schema` header, no typed object tree,
no canonical document hash, and no Tier2 schema. `exec` in a command config is a
runtime file-loading command; it is not a CYKV include directive.

There are currently **two different configuration implementations**. Their
grammars and policy must not be described as one unified contract:

1. `CypherConfig` is the implementation used by the current engine Host.
2. `CypherCommon::Config_Load` is a newer caller-supplied text/command-system
   contract used by tests and benchmarks, but it is not wired into Host startup.

#### 12.4.1 Active Host `CypherConfig` grammar

The Host loads `config/default.cfg` followed by `config/autoexec.cfg`. Both are
currently requested as optional files by `Host_LoadStartupConfig`
([source](../src/CypherEngine/CypherHost/CypherHost.cpp#L1913-L1957)). This differs
from the convenience `Cfg_LoadDefault()` helper, which treats `default.cfg` as
required, while `Cfg_LoadAutoexec()` treats `autoexec.cfg` as optional
([source](../src/CypherConfig/CypherConfig.cpp#L219-L241)).

Each physical line is trimmed and processed independently. Empty lines and
comments succeed. `#` and `//` begin a comment anywhere outside double quotes;
comment markers inside quotes are data. The loader accepts LF, CRLF, and bare CR
as line separators. It rejects an embedded NUL before dispatch but performs no
UTF-8 validation.

The active implementation recognizes these forms:

```cfg
# full-line comment
// full-line comment

set  r_width  "1920"
seta r_height 1080
exec "config/controls.cfg"
some_registered_command argument
```

| Form | Exact active behavior |
| --- | --- |
| `set <name> <value>` | Requires exactly one nonempty value, either one unquoted token or one double-quoted span; trailing tokens fail; calls `Cvar_Set` |
| `seta <name> <value>` | Currently identical to `set`; it does **not** add or change an archive flag |
| `exec <path>` | Requires exactly one nonempty quoted or unquoted path; loads it as optional; trailing tokens fail |
| Other nonempty line | Delegated to the active command system as a regular command |

Active quoted `set` values and `exec` paths have no escape language. A `\"` does
not provide a supported embedded quote, and backslash sequences are copied
literally. Empty quoted values are rejected. The explicit `set`, `seta`, and
`exec` handlers are implemented in
[CypherConfig.cpp](../src/CypherConfig/CypherConfig.cpp#L243-L402).

Active limits are:

| Limit | Declared value | Effective maximum |
| --- | ---: | ---: |
| File storage | 64 KiB | 65,535 bytes; the implementation rejects size `>= 65,536` |
| Physical line buffer | 1,024 bytes including NUL | 1,023 bytes |
| `exec` path buffer | 260 bytes including NUL | 259 bytes |
| CVar-name buffer | 256 bytes including NUL | 255 bytes |
| Nested file loads | 8 active levels | Includes the outermost load |

The published limits are declared in
[CypherConfig.h](../src/CypherConfig/CypherConfig.h#L31-L60). Only an optional
`ERR_PATH_NOT_FOUND` is ignored. Invalid paths, denied opens, malformed content,
and other I/O failures remain errors.

File execution is not transactional. The loader continues after a failing line,
returns the first line error, and retains effects from every successful line
executed before or after it. `loadedOut` becomes true only when the complete file
finishes successfully (or immediately for a successfully opened empty file).
Active status values are `OK`, `ERR_NOT_INIT`, `ERR_IS_INIT`,
`ERR_INVALID_PATH`, `ERR_INVALID_LINE`, `ERR_FILE_OPEN_FAILED`,
`ERR_PARSE_FAILED`, `ERR_COMMAND_FAILED`, and `ERR_IO_ERROR`
([status declaration](../src/CypherConfig/CypherConfig_Error.h#L32-L47),
[file execution](../src/CypherConfig/CypherConfig.cpp#L84-L217)).

#### 12.4.2 Tier1 `CypherCommon::Config_Load`

The Tier1 API accepts a borrowed source name and complete borrowed text, a
caller-owned command system, and a caller command context. It performs no file
I/O and has no `exec` directive of its own. It splits LF, CRLF, or bare CR physical
lines; strips `#` and `//` outside single- or double-quoted spans; parses at most
64 arguments on a line of at most 4 KiB; and then resolves the first argument as
a registered command or ConVar.

A direct ConVar assignment is:

```cfg
game.title "Cypher \"Arena\""
```

It is not written as `set game.title ...`. Exactly two arguments are required for
a ConVar assignment. The ConVar value argument, quoted or unquoted, decodes
`\\`, `\"`, `\n`, `\r`, and `\t`; other backslash pairs remain literal. A command line executes only when
policy permits it. The implementation is in
[CypherCommon_Config.cpp](../src/CypherCommon/Tier1/CypherCommon_Config.cpp#L197-L327),
using the bounded
[ConCommand parser](../src/CypherCommon/Tier1/CypherCommon_ConCommand.h#L29-L87).

| Source flag | Meaning |
| --- | --- |
| `ALLOW_COMMANDS` | Permit registered command execution; enabled by default |
| `ALLOW_CHEATS` | Preserve caller cheat permission; never grants permission the caller lacks |
| `STOP_ON_ERROR` | Stop after the first rejected line; otherwise continue and retain the first error |
| `ARCHIVED_ONLY` | Permit only archived ConVars and commands marked safe for persistence |

The result reports the first or terminal error code, physical lines read,
commands/assignments executed, total errors, and the byte offset of the first
failure. Tier1-local status values are `OK`, `INVALID_ARGUMENT`, `PARSE_FAILED`,
`PERMISSION_DENIED`, and `WRITE_FAILED`; command-system errors can also be
returned in the general error code.

`Config_WriteArchivedConVars` streams only ConVars carrying
`CONVAR_FLAG_ARCHIVE`. It emits one reloadable `name value\n` line per value,
quotes and escapes strings, and reports `WRITE_FAILED` when the caller's sink
rejects a fragment. It does not write a CYKV document
([writer](../src/CypherCommon/Tier1/CypherCommon_Config.cpp#L108-L193)).

#### 12.4.3 Exact `.cycfg` status

`.cfg` is the extension used by the live startup paths and tests. `.cycfg` is
currently only a documented alternate extension in the
[format catalog](formats/FORMAT_CATALOG.md#foundation-and-configuration). There is
no `.cycfg`-specific parser, header, schema, default path, version, or production
call site. Both config loaders are extension-agnostic, so a caller may pass a
`.cycfg` path or source name and receive the same grammar selected by the API it
called. The extension alone cannot select between the active Host grammar and
the Tier1 grammar.

Until those implementations are unified or one is retired, documentation and
tools must name the exact API they target. A file valid for one path is not
necessarily valid for the other: the active Host expects `set`/`seta`, while the
Tier1 path expects direct ConVar assignment and has a distinct quoting, escaping,
permission, limit, diagnostic, and archive-writing contract.

## 13. CYRS Cooked-Resource Container

| Container property | Value |
| --- | --- |
| Magic | `CYRS` / FourCC `0x53525943` in little-endian storage |
| Container version | 1 |
| Byte order | Little-endian |
| Fixed header | 80 bytes |
| Chunk descriptor | 64 bytes |
| Chunk-count limit | 4,096 |
| Maximum declared chunk alignment | 1 MiB |
| Status | Implemented common reader/writer and domain use |

CYRS is the common envelope for cooked runtime resources. It identifies the
domain resource and version, locates ordered payload chunks, records optional
source/content identities, and gives every domain reader one bounded structural
validation pass. CYRS does not define the payload meaning of `CYSH`, `CYTX`,
`CYMT`, or future resource types.

The serialized layout is:

```text
80-byte CYRS header
N × 64-byte chunk descriptors
optional gaps/alignment padding
ordered payload chunks
optional trailing region accepted by the common V1 validator
```

Domain writers normally use zero-filled alignment gaps and place the final chunk
at the declared file end. The common V1 layout validator permits gaps and a
trailing unreferenced region; stricter domain readers can and do impose canonical
placement.

### 13.1 Header fields

Offsets are relative to byte zero of the complete file.

| Offset | Member | Type | Req. | Values/constraints | Meaning |
| ---: | --- | --- | --- | --- | --- |
| 0 | `magic` | `u32` FourCC | Yes | Exactly `CYRS` | Identifies the generic cooked envelope |
| 4 | `nContainerVersion` | `u32` | Yes | Exactly 1 | Version of this 80-byte/64-byte envelope contract |
| 8 | `cbHeader` | `u32` bytes | Yes | Exactly 80 | Fixed-header size |
| 12 | `resourceType` | `u32` FourCC | Yes | Nonzero; e.g. `CYSH`, `CYTX`, `CYMT` | Selects the domain reader |
| 16 | `nResourceVersion` | `u32` | Yes | Greater than zero | Version of the selected domain payload contract |
| 20 | `flags` | `u32` bitmask | Yes | Only bits 0–1 | Declares which optional header hashes are present |
| 24 | `nChunks` | `u32` | Yes | 1–4,096 | Number of following 64-byte descriptors |
| 28 | `nReserved` | `u32` | Yes | Exactly zero | Reserved for a future container version |
| 32 | `cbFile` | `u64` bytes | Yes | Exact input size; at least header plus descriptors | Complete declared file length |
| 40 | `iChunkTable` | `u64` byte offset | Yes | Exactly 80 in V1 | Absolute start of the descriptor table |
| 48 | `sourceHash.low` | `u64` | Conditional | Nonzero 128-bit hash iff flag bit 0 is set | Low half of authored-input/compiler identity |
| 56 | `sourceHash.high` | `u64` | Conditional | Paired with low half | High half of authored-input/compiler identity |
| 64 | `contentHash.low` | `u64` | Conditional | Nonzero 128-bit hash iff flag bit 1 is set | Low half of the post-header content seal |
| 72 | `contentHash.high` | `u64` | Conditional | Paired with low half | High half of the post-header content seal |

Header flags:

| Bit | Name | Meaning |
| ---: | --- | --- |
| — | `NONE = 0` | Neither optional header hash is present |
| 0 | `HAS_SOURCE_HASH` | `sourceHash` is present and valid |
| 1 | `HAS_CONTENT_HASH` | `contentHash` is present and valid |

Presence flags and hash validity are checked in both directions. A nonzero hash
without its flag and a flagged invalid/sentinel hash both reject the file.

The CYRS content hash covers every serialized byte beginning at offset 80:
descriptor table, gaps, padding, payloads, and any trailing region. It excludes
the fixed header so the header can store the resulting seal.

### 13.2 Chunk-descriptor fields

Offsets are relative to the beginning of one descriptor.

| Offset | Member | Type | Req. | Values/constraints | Meaning |
| ---: | --- | --- | --- | --- | --- |
| 0 | `chunkType` | `u32` FourCC | Yes | Nonzero, domain-defined | Identifies the payload kind, such as `SHMD` or `TXDT` |
| 4 | `codec` | `u32` enum | Yes | `NONE=0`, `LZ4=1`, `ZSTD=2` | Storage codec identity |
| 8 | `flags` | `u32` bitmask | Yes | Only bits 0–2 | Compression, optionality, and hash presence |
| 12 | `nAlignment` | `u32` bytes | Yes | Power of two, 1 through 1 MiB | Required absolute file-offset alignment |
| 16 | `iOffset` | `u64` byte offset | Yes | Aligned; first chunk at or after the full header/table prefix, later chunks at or after the prior stored end | Absolute start of stored payload |
| 24 | `cbStored` | `u64` bytes | Yes | Greater than zero and contained by `cbFile` | Bytes physically present in the file |
| 32 | `cbDecoded` | `u64` bytes | Yes | Greater than zero | Logical byte count after decompression |
| 40 | `nReserved` | `u64` | Yes | Exactly zero | Reserved for a future container version |
| 48 | `contentHash.low` | `u64` | Conditional | Valid iff flag bit 2 is set | Low half of the chunk payload identity |
| 56 | `contentHash.high` | `u64` | Conditional | Paired with low half | High half of the chunk payload identity |

Chunk flags:

| Bit | Name | Meaning |
| ---: | --- | --- |
| — | `NONE = 0` | Uncompressed, required chunk with no declared chunk hash |
| 0 | `COMPRESSED` | Stored and decoded representations differ |
| 1 | `OPTIONAL` | A domain reader may skip an unknown optional chunk |
| 2 | `HAS_CONTENT_HASH` | The descriptor hash is present |

`OPTIONAL` expresses generic envelope intent. The current CYSH, CYTX, and CYMT
domain readers require exact chunk counts, kinds, and ordering, so they reject
unknown chunks even when the optional bit is present.

Codec consistency rules:

- `NONE` requires `COMPRESSED` to be clear and `cbStored == cbDecoded`.
- `LZ4` or `ZSTD` requires `COMPRESSED` to be set.
- Codec identifiers are structurally recognized, but the common layer does not
  currently provide decompression.
- The header declaration currently describes the chunk hash as covering stored
  bytes, while the renderer-format document describes decoded bytes. Those are
  identical for every currently emitted `NONE` chunk. Compression must not ship
  until one authoritative semantic rule is frozen and tested.

### 13.3 Layout and validation rules

The common reader performs these checks before publishing either the header or
descriptors:

1. input contains the complete 80-byte header;
2. magic, container version, header size, chunk-table offset, and reserves match;
3. resource FourCC and resource version are nonzero;
4. no unknown header or chunk flag is present;
5. chunk count is within 1–4,096 and prefix-size arithmetic is safe;
6. declared `cbFile` exactly equals the input byte count;
7. every descriptor has a valid codec, size, power-of-two alignment, reserve, and
   hash-presence relationship;
8. the first payload begins at or after the full header/table prefix, and
   descriptors are ordered by absolute offset and never overlap;
9. each stored range is contained by `cbFile`;
10. the optional whole-content hash matches.

CYRS V1 does not itself require:

- a particular domain chunk set or order beyond physical offset order;
- a chunk to begin immediately after the table or previous chunk;
- zero gap or trailing bytes;
- unique chunk FourCCs;
- a chunk hash to be recomputed by the common reader;
- decompression;
- signatures, encryption, or authentication.

The selected domain reader owns those additional rules.

### 13.4 Worked structural example

The following is an annotated description, not a textual source format:

```text
CYRS header
  resourceType      = CYSH
  resourceVersion   = 3
  flags             = HAS_SOURCE_HASH | HAS_CONTENT_HASH
  nChunks           = 5
  iChunkTable       = 80

chunk[0] = SHMD, codec NONE, alignment 8
chunk[1] = SHRF, codec NONE, alignment 8
chunk[2] = SHST, codec NONE, alignment 1
chunk[3] = SHCD, codec NONE, alignment 4
chunk[4] = SHCD, codec NONE, alignment 4
```

The same envelope can contain a texture or material by changing the resource
FourCC/version and using that format's exact chunk contract.

### 13.5 Failure and publication behavior

`CookedResource_ReadLayout` decodes into local temporary state, validates the
complete envelope, then copies results into caller storage. Failures leave caller
outputs unchanged and report a stable status plus the first offending chunk when
known.

Stable result classes cover invalid arguments, insufficient output, truncation,
magic/version/header/type/flag errors, chunk-limit and descriptor errors, physical
ordering, file-size disagreement, and whole-content-hash mismatch.

### 13.6 Current gaps

- LZ4 and Zstandard have identifiers but no common decoder.
- Chunk-hash stored-versus-decoded semantics need one frozen rule before
  compression is implemented.
- Common validation permits unused gaps and a trailing unreferenced region.
- The common writer emits only the fixed header and table; each domain writer
  owns payload placement, zero padding, and final sealing.
- Content hashes detect corruption and identify cache inputs; they are not
  signatures and do not establish trust.
- No signature, encryption, signer identity, or rollback protection exists.


## 14. Shader Format

CypherEngine separates the authored shader recipe from the cooked runtime
program. Authors write a CYKV `.cyshader` document and ordinary GLSL stage
files. `CypherShaderCompiler` validates the recipe and the complete linked GLSL
program, then writes a `.cyshader_c` cooked resource. The cooked resource uses
the common CYRS envelope with resource FourCC `CYSH`.

The source schema describes logical material intent. Authors do not write OpenGL
uniform locations, texture units, descriptor indices, register numbers, chunk
indices, logical byte offsets, or content hashes. The compiler derives and
validates those values. This distinction is essential because the V2 source
schema can express some concepts that compiler version 5 deliberately refuses to
cook, while CYSH V3 can persist several binding kinds that the current source
compiler does not emit.

Primary implementations:

- [authored schema constants and limits](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.h);
- [authored schema graphs](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.cpp);
- [authored typed views and decoder result types](../src/CypherCommon/Formats/CypherCommon_RenderAsset.h);
- [authored semantic decoders](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp);
- [cooked shader ABI](../src/CypherCommon/Formats/CypherCommon_CookedShader.h);
- [cooked shader writer and reader](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp);
- [offline compiler](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp);
- [runtime resource loader](../src/CypherResource/CypherResource_RenderAssets.cpp);
- [OpenGL program consumer](../src/CypherRender/OpenGL/CypherRender_OpenGL_Shader.cpp).

### 14.1 Version and route matrix

| Layer | Current | Compatibility and route |
| --- | ---: | --- |
| Source extension | `.cyshader` | Exact lower-case extension |
| Cooked extension | `.cyshader_c` | Exact lower-case extension |
| CYKV language | 1 | Both source schema versions use `@cykv 1` |
| Schema ID | `cypher.shader` | Exact ID; other IDs reject |
| Shader source schema | 2 | Decoder and compiler accept V1 and V2 |
| Compiler API / implementation | 1 / 5 | Deterministic, thread-safe, validation and dry-run capable |
| CYRS container | 1 | Common 80-byte header and 64-byte chunk records |
| CYSH resource | 3 | Shared reader accepts V2 and V3 |
| SHMD metadata | 3 | SHMD V2 belongs to CYSH V2; SHMD V3 belongs to CYSH V3 |
| SHRF reflection | 1 | Present only in CYSH V3 |
| SHST string table | No inner version | Meaning is fixed by CYSH V3 |
| SHCD code payload | No inner version | Meaning comes from the stage record and code-format enum |

The route is selected by the exact source schema version:

| Authored document | Compiler output | Meaning |
| --- | --- | --- |
| `@schema "cypher.shader" 1` | CYSH V2 with SHMD V2 | Vertex and fragment GLSL, no logical interface |
| `@schema "cypher.shader" 2` | CYSH V3 with SHMD V3, SHRF V1, and SHST | Vertex and fragment GLSL plus a canonical typed logical interface |

There is no field-shape inference. An unknown source schema, CYSH resource
version, SHMD version, SHRF version, enum value, flag bit, chunk layout, or
reserved-field value is rejected. Source dispatch is implemented in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L2236-L2301),
and cooked dispatch is implemented in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L1968-L2024).

### 14.2 Common source-document rules

Every shader recipe begins with both exact headers:

```cykv
@cykv 1
@schema "cypher.shader" <1|2>
```

The compiler parses with the normal CYKV policy: comments, unquoted object keys,
and trailing commas are accepted; duplicate object keys are rejected. All fixed
shader objects are closed. A misspelled or unrecognized member therefore fails
schema validation instead of being ignored. Dynamic maps such as texture,
parameter, and feature maps accept authored member names, but validate every key
and value. When a dynamic map is present, it must contain at least one member.

The compiler applies these text limits before schema decoding:

| Input | Limit | Additional rule |
| --- | ---: | --- |
| `.cyshader` recipe | 1 MiB | Non-empty UTF-8, no embedded NUL |
| Vertex source | 16 MiB minus 1 byte | Non-empty UTF-8, no embedded NUL |
| Fragment source | 16 MiB minus 1 byte | Non-empty UTF-8, no embedded NUL |
| One include file | 1 MiB | Non-empty UTF-8, no embedded NUL |
| All unique includes combined | 8 MiB | Counted once per unique resolved file |

The spare stage byte is needed because SHCD includes one terminal NUL in its
16 MiB maximum. Compiler bounds are defined in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L57-L67),
and text validation is implemented in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L323-L376).

#### 14.2.1 Canonical paths

An authored stage path is a canonical virtual path of 1–259 UTF-8 bytes. It:

- is relative and does not begin with `/` or `\`;
- uses `/`, never `\`, as its separator;
- contains no uppercase ASCII;
- contains no empty, `.` or `..` segment;
- contains no whitespace or non-ASCII byte;
- contains none of `: * ? " < > |`;
- is not normalized or repaired by the decoder;
- uses a case-sensitive lower-case extension.

A vertex stage accepts `.vert` or `.glsl`. A fragment stage accepts `.frag` or
`.glsl`. The compiler request's input and output paths are separately limited to
4,095 bytes and must end in `.cyshader` and `.cyshader_c` respectively.
Canonical path rules are implemented in
[CypherCommon_DataValidation.cpp](../src/CypherCommon/Tier2/CypherCommon_DataValidation.cpp#L45-L93)
and
[CypherCommon_DataValidation.cpp](../src/CypherCommon/Tier2/CypherCommon_DataValidation.cpp#L175-L240).

#### 14.2.2 Identifier classes

Shader source uses two case-sensitive identifier classes:

| Class | First byte | Remaining bytes | Source maximum | Uses |
| --- | --- | --- | ---: | --- |
| ASCII identifier | `[A-Za-z_]` | `[A-Za-z0-9_]` | 64 bytes | Defines, entries, interface names, feature names |
| Stable identifier | `[a-z]` | `[a-z0-9_-]` | 64 bytes | Sampler presets and enum feature values/defaults |

The generic cooked format allows a logical binding name up to 127 bytes, but the
current source schemas deliberately expose only the narrower 64-byte authoring
limit. Identifier validation is implemented in
[CypherCommon_DataValidation.cpp](../src/CypherCommon/Tier2/CypherCommon_DataValidation.cpp#L115-L172).

Both source decoders return borrowed views into the parsed CYKV document. The
document must outlive the view. Decoding is transactional: structural schema
validation and all semantic checks complete before the caller-visible output is
published.

### 14.3 `.cyshader` source schema V1

V1 is the frozen compatibility recipe. It describes exactly one desktop GLSL
vertex/fragment program and an optional set of preprocessor symbols.

```cykv
@cykv 1
@schema "cypher.shader" 1

{
    language = "glsl"
    vertex = "shaders/world.vert"
    fragment = "shaders/world.frag"
    defines = ["CY_WORLD_PASS", "CY_FOG"]
}
```

| Field | CYKV type | Required | Default | Values and constraints |
| --- | --- | --- | --- | --- |
| `language` | string | Yes | — | Exact value `glsl` |
| `vertex` | string | Yes | — | Canonical `.vert` or `.glsl` virtual path, 1–259 bytes |
| `fragment` | string | Yes | — | Canonical `.frag` or `.glsl` virtual path, 1–259 bytes |
| `defines` | array of strings | No | Empty set | If present, 1–64 unique ASCII identifiers, each 1–64 bytes |

Define order is retained by the decoded view, although each name must be unique.
Each define is injected into both stages as `#define NAME 1`. V1 has no authored
entry field; both entries are `main`. It has no logical texture, sampler,
parameter, feature, range, default-value, or variant contract.

V1 is decoded by `RenderShaderSource_Decode` and compiles to CYSH V2. The exact
schema is in
[CypherCommon_RenderAssetSchema.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.cpp#L168-L189),
and semantic decoding is in
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L570-L694).

### 14.4 `.cyshader` source schema V2

V2 adds stage objects, a typed material-facing interface, authored feature
metadata, and a bounded static-variant declaration. This example uses only the
subset accepted by compiler version 5:

```cykv
@cykv 1
@schema "cypher.shader" 2

{
    language = "glsl"

    stages = {
        vertex = {
            source = "shaders/world.vert"
            entry = "main"
        }
        fragment = {
            source = "shaders/world.frag"
        }
    }

    defines = ["CY_WORLD_PASS"]

    interface = {
        textures = {
            base_color = {
                type = "texture2d"
                usage = "color"
                color_space = "srgb"
                required = true
            }
            normal_map = {
                type = "texture2d"
                usage = "normal"
                required = false
            }
        }

        parameters = {
            roughness = {
                type = "f32"
                default = 0.6
                minimum = 0
                maximum = 1
            }
            tint = {
                type = "color4"
                default = [1, 0.5, 0.25, 1]
            }
            model = {
                type = "mat4"
                required = true
            }
        }
    }

    variant_budget = 8u
}
```

#### 14.4.1 Root fields

| Field | CYKV type | Required | Default | Values and constraints |
| --- | --- | --- | --- | --- |
| `language` | string | Yes | — | Exact value `glsl` |
| `stages` | closed object | Yes | — | Required `vertex` and `fragment` objects; no other stage member |
| `defines` | array of strings | No | Empty set | If present, 1–64 unique ASCII identifiers |
| `interface` | closed object | No | No material interface | If present, contains 1–3 of `textures`, `samplers`, `parameters` |
| `features` | dynamic object | No | No features | If present, 1–32 named feature records |
| `variant_budget` | I64 or U64 | No | `64` | Inclusive range 1–1,024; F64 is not accepted |

An omitted interface produces a valid empty CYSH V3 interface. An authored
`interface = {}` is invalid because a present interface must contain at least one
category. Likewise, an authored category map must not be empty.

#### 14.4.2 Stage objects

Both `stages.vertex` and `stages.fragment` use this closed shape:

| Field | CYKV type | Required | Default | Values and constraints |
| --- | --- | --- | --- | --- |
| `source` | string | Yes | — | Canonical stage path, at most 259 bytes |
| `entry` | string | No | `main` | ASCII identifier, at most 64 bytes |

The schema can represent a non-`main` entry name, but compiler version 5 rejects
it because CYSH V3 has no persisted runtime entry-name field. Both entries must
currently resolve to the exact case-sensitive name `main`.

#### 14.4.3 Defines

`defines` has the same contract as V1. A present array contains 1–64 unique ASCII
identifiers. The compiler emits every item into both stage preambles as:

```glsl
#define NAME 1
```

The source format does not support macro values or per-stage define sets.

#### 14.4.4 Texture interface

`interface.textures` is an optional dynamic map of 1–32 entries. Each member name
is the logical GLSL combined-sampler name and must be an ASCII identifier.
Texture, sampler, and parameter names share one namespace and cannot collide.
Each texture value is a closed object:

| Field | CYKV type | Required | Default | Values and constraints |
| --- | --- | --- | --- | --- |
| `type` | string enum | Yes | — | `texture2d`, `texture_cube`, `texture2d_array`, `texture3d` |
| `usage` | string enum | Yes | — | `color`, `normal`, `data` |
| `color_space` | string enum | No | `srgb` for color; `linear` for normal/data | `srgb` or `linear` |
| `required` | bool | No | `true` | Persists as the REQUIRED binding flag |

A `normal` or `data` texture must be linear. Explicit `color_space = "srgb"` on
those usages is invalid. A color texture may explicitly request either sRGB or
linear. Type, required state, and logical name affect the cooked interface.
Usage and color-space policy are authoring semantics and do not appear in SHRF.

#### 14.4.5 Sampler interface

`interface.samplers` is an optional dynamic map of 1–16 entries. Names share the
texture/parameter namespace. Each value is a closed object:

| Field | CYKV type | Required | Default | Values and constraints |
| --- | --- | --- | --- | --- |
| `type` | string enum | Yes | — | `filtering` or `comparison` |
| `default` | string | No | No preset | Stable identifier naming a sampler preset |
| `required` | bool | No | `true` | Intended required-state contract |

The V2 decoder fully validates this shape. Compiler version 5 rejects every
non-empty sampler map because the source format does not yet version a
sampler-to-texture association and the OpenGL material subset uses combined
samplers. A valid sampler declaration is therefore schema-valid but not currently
cookable.

#### 14.4.6 Parameter interface

`interface.parameters` is an optional dynamic map of 1–64 entries. Names share
the texture/sampler namespace. Each value is a closed object:

| Field | CYKV type | Required | Default | Values and constraints |
| --- | --- | --- | --- | --- |
| `type` | string enum | Yes | — | `bool`, `i32`, `u32`, `f32`, `f32x2`, `f32x3`, `f32x4`, `color3`, `color4`, `mat3`, `mat4` |
| `default` | bool, number, or numeric array | No | No authored default | Must exactly match the declared type and shape |
| `minimum` | number | No | No lower bound | Legal only for `i32`, `u32`, or `f32` |
| `maximum` | number | No | No upper bound | Legal only for `i32`, `u32`, or `f32` |
| `required` | bool | No | `false` | Persists as the REQUIRED binding flag |

Exact default representations are:

| Declared type | Accepted default |
| --- | --- |
| `bool` | CYKV bool only |
| `i32` | I64 or U64 node in `[-2,147,483,648, 2,147,483,647]`; F64 is rejected |
| `u32` | Non-negative I64 or U64 node in `[0, 4,294,967,295]`; F64 is rejected |
| `f32` | Any finite CYKV number in `[-FLT_MAX, FLT_MAX]` |
| `f32x2` | Numeric array of exactly 2 finite F32-representable values |
| `f32x3`, `color3` | Numeric array of exactly 3 finite F32-representable values |
| `f32x4`, `color4` | Numeric array of exactly 4 finite F32-representable values |
| `mat3` | Numeric array of exactly 9 finite F32-representable values |
| `mat4` | Numeric array of exactly 16 finite F32-representable values |

Colors are not implicitly clamped to `[0,1]`. Vector, color, and matrix ranges
cannot be authored in schema V2. When `minimum` or `maximum` is present:

- `minimum <= maximum`;
- the bound must fit the declared scalar type;
- an `i32` or `u32` bound must be mathematically integral even when written as an
  F64 literal;
- an authored default must lie within every authored bound.

General schema-number validation rejects NaN and infinity. I64/U64 nodes used by
the broad number rule are limited to the exactly F64-representable integer range
of plus or minus `2^53` before narrower parameter checks run. Exact typed-value
normalization is implemented in
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L350-L448),
and parameter cross-field checks are in
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L1289-L1485).

`required = false` does not permit the GLSL uniform to be absent or inactive.
Every authored parameter and texture, required or optional, must currently appear
as an active linked GLSL resource. The flag describes material fulfillment, not
reflection optionality.

#### 14.4.7 Features and variant budget

`features` is an optional dynamic map of 1–32 entries. Feature names are ASCII
identifiers and need only be unique within the feature map; they do not share the
interface binding namespace. Each feature is a closed object:

| Field | CYKV type | Required | Default | Values and constraints |
| --- | --- | --- | --- | --- |
| `type` | string enum | Yes | — | `bool` or `enum` |
| `mode` | string enum | No | `static` | `static` or `dynamic` |
| `default` | bool or string | Conditional | `false` for bool | Bool requires bool when present; enum requires a stable identifier |
| `values` | array of strings | Conditional | — | Forbidden for bool; required for enum; 2–16 unique stable identifiers |

An enum default must name one of its values. A static bool contributes two values
to the static variant count. A static enum contributes its number of values. A
dynamic feature contributes no static multiplicity. The decoder starts with one
base variant and computes the Cartesian product of all static feature counts. It
rejects the first multiplication that would exceed `variant_budget`. The default
budget is 64 and the absolute maximum is 1,024.

Compiler version 5 rejects every non-empty feature map after schema and budget
validation. CYSH V3 has no variant table, variant key, stage-payload selection
record, or feature-default record. The compiler therefore refuses the recipe
instead of pretending that all declared permutations were cooked. Feature
semantics and the budget algorithm are implemented in
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L1487-L1648).

#### 14.4.8 V2 source capability versus compiler version 5

| Authored construct | Schema V2 | Compiler version 5 |
| --- | --- | --- |
| Vertex and fragment entry `main` | Valid | Supported |
| Alternate valid entry identifier | Valid | Rejected; no persisted entry field |
| No `interface` member | Valid | Supported; writes an empty V3 interface |
| Texture declarations | Valid | Supported when exact active GLSL combined samplers match |
| Parameter declarations | Valid | Supported when exact active GLSL plain uniforms match |
| Independent sampler declarations | Valid | Rejected; association is not versioned |
| No feature map | Valid | Supported |
| Static or dynamic feature declarations | Valid and budgeted | Rejected; CYSH V3 has no variant table |
| Vertex/fragment graphics pair | Required | Supported |
| Geometry, tessellation, compute, mesh, or ray stages | Not representable | Not supported |

The hard gates are implemented in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L2394-L2495).

### 14.5 GLSL compilation, includes, and reflection

#### 14.5.1 Language profile

Each root stage must declare one desktop core directive in the exact form:

```glsl
#version NNN core
```

A UTF-8 BOM, whitespace, `//` comments, and closed `/* ... */` comments may
precede it. No other directive or token may precede `#version`. Vertex and
fragment stages must declare the same version.

The cooked format recognizes desktop core GLSL versions 330, 400, 410, 420, 430,
440, 450, and 460. Compiler version 5 applies the selected target ceiling:

| Target | Maximum compiler-emitted GLSL |
| --- | ---: |
| macOS OpenGL | 410 core |
| Windows OpenGL | 450 core |
| Linux OpenGL | 450 core |

This produces an intentional distinction: the low-level CYSH writer and reader
can round-trip GLSL 460, and a capable runtime driver may accept it, but the
current source compiler does not emit 460 for any configured target. Directive
parsing is implemented in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L713-L940),
and target enforcement is in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L2535-L2639).

#### 14.5.2 Include contract

Only quoted, project-local GLSL includes are reproducible and supported:

```glsl
#include "lighting/common.glsl"
```

`#include <system>` is rejected. A quoted include is resolved relative to its
including file through the source VFS, normalized to a lower-case canonical
virtual path, and required to remain beneath the VFS source root. Absolute paths,
root escapes, invalid canonical paths, and non-`.glsl` include extensions reject
the compilation.

| Include limit | Value |
| --- | ---: |
| Nesting depth | 32 |
| Unique include files per shader program | 128 |
| Include requests per stage | 256 |
| One include file | 1 MiB |
| Combined unique include text | 8 MiB |
| Resolved path buffer | 4,095 bytes |

Repeated requests for one resolved file share the cached text and dependency
record. Every unique transitive include path and content hash participates in
source identity. Resolution and normalization are implemented in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L964-L1059),
and bounded loading/callback behavior is in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L1061-L1255).

#### 14.5.3 Compile and link pipeline

For both source versions, the compiler:

1. parses and validates the CYKV recipe;
2. reads both source stages through the source VFS;
3. validates matching desktop-core `#version` directives against the target;
4. creates the define preamble;
5. preprocesses each stage with glslang and the bounded VFS includer;
6. retains non-empty, valid UTF-8 preprocessed GLSL within the SHCD size limit;
7. parses each stage;
8. links one vertex/fragment program with cross-stage I/O validation;
9. for source V2, reflects the linked material-facing interface;
10. builds canonical CYSH bytes, validates all hashes/layout, and publishes one
    primary `application/x-cypher-shader` artifact.

Compiler warnings remain warnings unless the tool invocation requests
warnings-as-errors. A dry run performs parsing, GLSL validation, linking,
reflection, canonical cooking, and output validation without writing the
artifact. Preprocessing and parsing are implemented in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L1635-L1800),
and linking is in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L1802-L1878).

#### 14.5.4 V2 reflection contract

Source V2 asks glslang to generate OpenGL-semantics SPIR-V 1.0 for offline
reflection after the GLSL pair links. SPIRV-Cross examines active resources. The
SPIR-V is validation data only and is not persisted; SHCD remains preprocessed
GLSL.

The comparison is bidirectional:

- every active combined sampled image must have a same-name
  `interface.textures` declaration;
- every active plain uniform must have a same-name `interface.parameters`
  declaration;
- every authored texture and parameter must be active in at least one linked
  stage;
- logical kind, value type, texture dimension, and array shape must agree.

The accepted reflected material subset is:

- non-array float `sampler2D`, `sampler2DArray`, `sampler3D`, and `samplerCube`;
- non-array `bool`, `int`, `uint`, `float`, `vec2`, `vec3`, `vec4`, `mat3`, and
  `mat4` plain uniforms.

Descriptor arrays, plain-uniform arrays, shadow samplers, multisample samplers,
integer samplers, cube-array samplers, 1D samplers, unsupported dimensions, and
unsupported value shapes fail when they participate in the compared material
interface. Uniform blocks, storage buffers, storage images, and stage inputs and
outputs are not enumerated into the authored material-interface comparison in
this version. Engine-owned declarations in those categories may coexist, but
they receive no V2 source declaration and no compiler-emitted SHRF binding.
Reflection mapping is implemented in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L1286-L1585),
and the reverse authored-to-active check is in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L1587-L1633).

Physical reflected stage use is not persisted for material bindings. The
compiler records conservative vertex-plus-fragment visibility so independent
shader and material compilation reconstruct the same interface hash without
requiring driver reflection.

#### 14.5.5 Source identity

The compiler's source hash combines:

- compiler identity, including source schema and cooked resource version;
- canonical CYKV recipe hash, independent of insignificant formatting and member
  order;
- original vertex and fragment text hashes;
- V3 interface hash when compiling source V2;
- every unique include path and include-content hash;
- glslang toolchain identity;
- SPIRV-Cross reflection-toolchain identity for source V2;
- selected target and build configuration.

Changing any semantic input invalidates the cooked identity. Parameter defaults,
ranges, texture usage/color-space policy, and other authoring fields participate
in the canonical recipe/source hash even when they do not appear in the CYSH
interface ABI. Identity construction is implemented in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L2704-L2762).

### 14.6 CYSH common binary contract

CYSH is serialized field by field in little-endian byte order. Native C++ layout
and padding are never written. Chapter 13 defines the general CYRS envelope; this
section states the stricter values required by the shader reader.

#### 14.6.1 CYRS header as constrained by CYSH

| Offset | Type | Field | CYSH requirement |
| ---: | --- | --- | --- |
| 0 | `u32` | `magic` | FourCC `CYRS` |
| 4 | `u32` | `nContainerVersion` | 1 |
| 8 | `u32` | `cbHeader` | 80 |
| 12 | `u32` | `resourceType` | FourCC `CYSH` |
| 16 | `u32` | `nResourceVersion` | 2 or 3 |
| 20 | `u32` | `flags` | Mandatory `HAS_CONTENT_HASH`; optional `HAS_SOURCE_HASH`; no other bits |
| 24 | `u32` | `nChunks` | 3 for CYSH V2; 5 for CYSH V3 |
| 28 | `u32` | `nReserved` | 0 |
| 32 | `u64` | `cbFile` | Exact complete file size |
| 40 | `u64` | `iChunkTable` | 80 |
| 48 | `u64` | `sourceHash.low` | Valid only when `HAS_SOURCE_HASH` is set |
| 56 | `u64` | `sourceHash.high` | Valid only when `HAS_SOURCE_HASH` is set |
| 64 | `u64` | `contentHash.low` | Required whole-content hash |
| 72 | `u64` | `contentHash.high` | Required whole-content hash |

Resource flag values are:

| Value | Flag | Shader rule |
| ---: | --- | --- |
| `0x1` | `HAS_SOURCE_HASH` | Optional at the low-level writer; compiler outputs provide it |
| `0x2` | `HAS_CONTENT_HASH` | Mandatory |

The whole-content hash covers every serialized byte after the fixed 80-byte
header: the chunk table, deterministic padding, and all payloads. It therefore
seals physical layout as well as payload content.

#### 14.6.2 CYRS chunk record as constrained by CYSH

Each table entry is 64 bytes:

| Relative offset | Type | Field | CYSH requirement |
| ---: | --- | --- | --- |
| 0 | `u32` | `chunkType` | `SHMD`, `SHRF`, `SHST`, or `SHCD` in the version-specific position |
| 4 | `u32` | `codec` | `NONE = 0` |
| 8 | `u32` | `flags` | Exactly `HAS_CONTENT_HASH = 0x4` |
| 12 | `u32` | `nAlignment` | 8 for SHMD/SHRF, 1 for SHST, 4 for SHCD |
| 16 | `u64` | `iOffset` | Exact canonical aligned absolute offset |
| 24 | `u64` | `cbStored` | Exact nonzero payload bytes |
| 32 | `u64` | `cbDecoded` | Exactly equal to `cbStored` |
| 40 | `u64` | `nReserved` | 0 |
| 48 | `u64` | `contentHash.low` | Hash of exact stored payload |
| 56 | `u64` | `contentHash.high` | Hash of exact stored payload |

Shader chunks cannot be compressed, optional, unhashed, reordered, overlapping,
or followed by unreferenced trailing data. Every alignment gap is zero. The
shader reader verifies the generic envelope and whole-content hash first, then
checks every domain chunk, per-chunk hash, header, record, offset, zero gap, and
payload before publishing its borrowed view. Generic field serialization is in
[CypherCommon_CookedResource.cpp](../src/CypherCommon/Formats/CypherCommon_CookedResource.cpp#L130-L207),
and strict shader validation is in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L1968-L2402).

#### 14.6.3 Persisted program and stage enums

| Enum | Numeric value | Meaning |
| --- | ---: | --- |
| Backend `OPENGL` | 1 | Only current runtime backend |
| Program kind `GRAPHICS` | 1 | Linked vertex/fragment program |
| Stage `VERTEX` | 1 | Vertex stage |
| Stage `FRAGMENT` | 2 | Fragment stage |
| Code format `GLSL_UTF8` | 1 | UTF-8 GLSL with one included terminal NUL |
| Language profile `GLSL_CORE` | 1 | Desktop OpenGL core profile |
| Program flags `NONE` | 0 | Only accepted program flag value |
| Stage flags `NONE` | 0 | Only accepted stage flag value |

The cooked layer accepts GLSL language versions 330, 400, 410, 420, 430, 440,
450, and 460. The program kind always requires exactly one vertex and one fragment
record. Records are unique, sorted by numeric stage value, and point to the
canonical stage chunk indices.

#### 14.6.4 Common stage record

Both SHMD versions append two identical 24-byte stage records:

| Relative offset | Type | Field | Requirement |
| ---: | --- | --- | --- |
| 0 | `u32` | `stage` | `VERTEX = 1` then `FRAGMENT = 2` |
| 4 | `u32` | `codeFormat` | `GLSL_UTF8 = 1` |
| 8 | `u32` | `flags` | 0 |
| 12 | `u32` | `iCodeChunk` | 1/2 in V2; 3/4 in V3 |
| 16 | `u64` | `cbCode` | Exact SHCD size including NUL, 2 bytes through 16 MiB |

#### 14.6.5 SHCD payload

SHCD has no inner magic or version header. It stores one non-empty GLSL byte
sequence followed by exactly one NUL. The terminal NUL is included in `cbCode`,
`cbStored`, `cbDecoded`, and the chunk hash. An earlier embedded NUL, invalid
UTF-8, size disagreement, zero-length text, payload larger than 16 MiB, unknown
code format, or incorrect chunk index rejects the file. SHCD validation is in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L1081-L1106).

### 14.7 CYSH V2 compatibility layout

CYSH V2 contains exactly three chunks:

```text
chunk 0  SHMD  alignment 8  metadata version 2
chunk 1  SHCD  alignment 4  vertex GLSL
chunk 2  SHCD  alignment 4  fragment GLSL
```

The CYRS prefix is `80 + 3 * 64 = 272` bytes. It is already 8-byte aligned, so
SHMD begins at byte 272. SHMD is `40 + 2 * 24 = 88` bytes and ends at byte 360.
The vertex SHCD therefore starts at byte 360. The fragment SHCD starts at the
next 4-byte-aligned offset after the complete vertex payload. The file ends
exactly after the fragment payload.

#### 14.7.1 SHMD V2 header

SHMD's outer identity is the chunk FourCC. The payload begins with inner magic
`CSHD`:

| Offset | Type | Field | Required value |
| ---: | --- | --- | --- |
| 0 | `u32` | Magic | FourCC `CSHD` |
| 4 | `u32` | Metadata version | 2 |
| 8 | `u32` | Header size | 40 |
| 12 | `u32` | Backend | `OPENGL = 1` |
| 16 | `u32` | Program kind | `GRAPHICS = 1` |
| 20 | `u32` | Language profile | `GLSL_CORE = 1` |
| 24 | `u32` | Language version | One supported cooked GLSL version |
| 28 | `u32` | Program flags | 0 |
| 32 | `u32` | Stage count | 2 |
| 36 | `u32` | Reserved | 0 |
| 40 | 24 bytes | Vertex record | Common stage-record layout, code chunk 1 |
| 64 | 24 bytes | Fragment record | Common stage-record layout, code chunk 2 |

CYSH V2 contains no SHRF, SHST, interface hash, logical binding ID, persisted
parameter layout, default, range, usage, feature, or variant data. A successfully
read V2 view has `nBindings = 0`. The V1 authored compiler route continues to
write this format through `CookedShader_Write`. Canonical V2 layout construction
is in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L1109-L1175),
and writing is in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L1591-L1751).

### 14.8 CYSH V3 current layout

CYSH V3 contains exactly five chunks:

```text
chunk 0  SHMD  alignment 8  metadata version 3
chunk 1  SHRF  alignment 8  reflection version 1
chunk 2  SHST  alignment 1  canonical names
chunk 3  SHCD  alignment 4  vertex GLSL
chunk 4  SHCD  alignment 4  fragment GLSL
```

The CYRS prefix is `80 + 5 * 64 = 400` bytes. SHMD begins at byte 400 and is
`72 + 2 * 24 = 120` bytes, so SHRF begins at byte 520. For `N` bindings:

```text
SHRF size = 32 + 56 * N
SHST size = 1 + sum(binding_name_byte_length + 1)
```

SHST follows SHRF immediately because its alignment is one. The vertex SHCD
begins at the next 4-byte-aligned offset after SHST. The fragment SHCD begins at
the next 4-byte-aligned offset after the vertex payload. All gaps are zero and
the file ends exactly after the fragment payload.

#### 14.8.1 SHMD V3 header

| Offset | Type | Field | Required value |
| ---: | --- | --- | --- |
| 0 | `u32` | Magic | FourCC `CSHD` |
| 4 | `u32` | Metadata version | 3 |
| 8 | `u32` | Header size | 72 |
| 12 | `u32` | Backend | `OPENGL = 1` |
| 16 | `u32` | Program kind | `GRAPHICS = 1` |
| 20 | `u32` | Language profile | `GLSL_CORE = 1` |
| 24 | `u32` | Language version | One supported cooked GLSL version |
| 28 | `u32` | Program flags | 0 |
| 32 | `u32` | Stage count | 2 |
| 36 | `u32` | Binding count | 0–128 |
| 40 | `u32` | Reflection chunk index | 1 |
| 44 | `u32` | String chunk index | 2 |
| 48 | `u32` | String-table bytes | Exact SHST size, 1–65,536 |
| 52 | `u32` | Reserved | 0 |
| 56 | `u64` | Interface hash low | Valid non-sentinel hash |
| 64 | `u64` | Interface hash high | Valid non-sentinel hash |
| 72 | 24 bytes | Vertex record | Common stage-record layout, code chunk 3 |
| 96 | 24 bytes | Fragment record | Common stage-record layout, code chunk 4 |

Header serialization and validation are in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L644-L803).

#### 14.8.2 SHRF V1 header

SHRF begins with inner magic `CSRF` and has exact size `32 + 56 * N`:

| Offset | Type | Field | Required value |
| ---: | --- | --- | --- |
| 0 | `u32` | Magic | FourCC `CSRF` |
| 4 | `u32` | Reflection version | 1 |
| 8 | `u32` | Header size | 32 |
| 12 | `u32` | Binding-record size | 56 |
| 16 | `u32` | Binding count | Same as SHMD, 0–128 |
| 20 | `u32` | String-table bytes | Same as SHMD, 1–65,536 |
| 24 | `u32` | Reserved 0 | 0 |
| 28 | `u32` | Reserved 1 | 0 |

#### 14.8.3 SHRF binding record

Each binding record is exactly 56 bytes:

| Relative offset | Type | Field | Meaning |
| ---: | --- | --- | --- |
| 0 | `u32` | `iName` | Byte offset of the name in SHST |
| 4 | `u32` | `cchName` | Name bytes excluding NUL, 1–127 |
| 8 | `u32` | `kind` | Persisted binding-kind enum |
| 12 | `u32` | `valueType` | Persisted value-type enum or `NONE` |
| 16 | `u32` | `resourceType` | Persisted resource-type enum or `NONE` |
| 20 | `u32` | `nArrayElements` | 1–1,024 |
| 24 | `u32` | `stageMask` | Nonzero vertex/fragment subset |
| 28 | `u32` | `flags` | Known binding flags only |
| 32 | `u64` | `nLogicalBinding` | Recomputed stable name ID |
| 40 | `u32` | `iByteOffset` | Logical storage offset |
| 44 | `u32` | `cbByteSize` | Logical occupied size; zero for opaque resources |
| 48 | `u32` | Reserved 0 | 0 |
| 52 | `u32` | Reserved 1 | 0 |

Header and record serialization are implemented in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L806-L921).

#### 14.8.4 Persisted binding kinds

| Value | Kind | Required value/resource representation |
| ---: | --- | --- |
| 1 | `VALUE` | Valid value type, resource `NONE` |
| 2 | `SAMPLED_TEXTURE` | Value `NONE`, texture resource type |
| 3 | `SAMPLER` | Value `NONE`, sampler resource type |
| 4 | `UNIFORM_BUFFER` | Value `NONE`, UBO resource type |
| 5 | `STORAGE_BUFFER` | Value `NONE`, SSBO resource type |
| 6 | `STORAGE_IMAGE` | Value `NONE`, storage-image resource type |
| 7 | `VERTEX_INPUT` | Valid value type, resource `NONE`, vertex-only mask |
| 8 | `FRAGMENT_OUTPUT` | Valid value type, resource `NONE`, fragment-only mask |

The current source compiler emits only `VALUE` and `SAMPLED_TEXTURE`. The other
kinds are part of the versioned cooked ABI for lower-level or future producers.

#### 14.8.5 Persisted value types

| Value | Type | Raw bytes | Material alignment | Material storage bytes |
| ---: | --- | ---: | ---: | ---: |
| 0 | `NONE` | 0 | 0 | 0 |
| 1 | `BOOL` | 4 | 4 | 4 |
| 2 | `I32` | 4 | 4 | 4 |
| 3 | `U32` | 4 | 4 | 4 |
| 4 | `F32` | 4 | 4 | 4 |
| 5 | `F64` | 8 | 8 | 8 |
| 6 | `I32X2` | 8 | 8 | 8 |
| 7 | `I32X3` | 12 | 16 | 16 |
| 8 | `I32X4` | 16 | 16 | 16 |
| 9 | `U32X2` | 8 | 8 | 8 |
| 10 | `U32X3` | 12 | 16 | 16 |
| 11 | `U32X4` | 16 | 16 | 16 |
| 12 | `F32X2` | 8 | 8 | 8 |
| 13 | `F32X3` | 12 | 16 | 16 |
| 14 | `F32X4` | 16 | 16 | 16 |
| 15 | `F32X3X3` | 36 | 16 | 48 |
| 16 | `F32X4X4` | 64 | 16 | 64 |

The current V2 source compiler emits BOOL, I32, U32, F32, F32X2, F32X3,
F32X4, F32X3X3, and F32X4X4. Source `color3` maps to F32X3 and `color4` maps to
F32X4. F64 and signed/unsigned integer vectors are representable by CYSH V3 but
have no `.cyshader` V2 spelling.

#### 14.8.6 Persisted resource types

| Value | Resource type |
| ---: | --- |
| 0 | `NONE` |
| 1 | `TEXTURE_1D` |
| 2 | `TEXTURE_2D` |
| 3 | `TEXTURE_3D` |
| 4 | `TEXTURE_CUBE` |
| 5 | `TEXTURE_1D_ARRAY` |
| 6 | `TEXTURE_2D_ARRAY` |
| 7 | `TEXTURE_CUBE_ARRAY` |
| 8 | `SAMPLER` |
| 9 | `SAMPLER_COMPARISON` |
| 10 | `UNIFORM_BUFFER` |
| 11 | `STORAGE_BUFFER` |
| 12 | `STORAGE_IMAGE_1D` |
| 13 | `STORAGE_IMAGE_2D` |
| 14 | `STORAGE_IMAGE_3D` |

The current source compiler emits TEXTURE_2D, TEXTURE_3D, TEXTURE_CUBE, and
TEXTURE_2D_ARRAY for authored textures. All other entries remain cooked-format
capability.

#### 14.8.7 Stage masks and binding flags

| Value | Stage-mask bit |
| ---: | --- |
| `0x1` | Vertex |
| `0x2` | Fragment |
| `0x3` | All graphics stages |

A mask must be nonzero and contain no other bits. The current source compiler
persists `0x3` for every material texture and parameter even when reflection
finds use in only one stage.

| Value | Binding flag | Meaning |
| ---: | --- | --- |
| `0x1` | `REQUIRED` | Material/runtime must provide the binding |
| `0x2` | `MATERIAL` | Binding belongs to material-owned logical storage |
| `0x4` | `INSTANCE` | Binding belongs to instance-owned logical storage |
| `0x8` | `READ_ONLY` | Resource is logically read-only |

`MATERIAL` and `INSTANCE` are mutually exclusive. The current source compiler
emits MATERIAL on all authored textures and parameters, REQUIRED according to
the source field, and no INSTANCE or READ_ONLY bit.

The persisted enums and flags are declared in
[CypherCommon_CookedShader.h](../src/CypherCommon/Formats/CypherCommon_CookedShader.h#L71-L188).

#### 14.8.8 Binding validation by kind

Every binding must have a valid 1–127-byte ASCII name, a matching recomputed ID,
1–1,024 array elements, a valid nonzero stage mask, known flags, and a
non-overflowing `iByteOffset + cbByteSize` range. Additional rules are:

| Kind | Additional invariant |
| --- | --- |
| `VALUE` | `valueType` is non-NONE, `resourceType` is NONE, and byte size is at least raw element size times array count |
| Material `VALUE` | Exactly one element, exact canonical storage size, natural alignment, canonical packed offset |
| `VERTEX_INPUT` | VALUE representation and stage mask exactly vertex |
| `FRAGMENT_OUTPUT` | VALUE representation and stage mask exactly fragment |
| `SAMPLED_TEXTURE` | Texture resource type; value NONE; byte offset and size both zero |
| `SAMPLER` | SAMPLER or SAMPLER_COMPARISON resource; value NONE; byte offset and size both zero |
| `UNIFORM_BUFFER` | UBO resource; value NONE; nonzero byte size |
| `STORAGE_BUFFER` | SSBO resource; value NONE; nonzero byte size |
| `STORAGE_IMAGE` | Storage-image resource; value NONE; byte offset and size both zero |

These rules are enforced symmetrically by writer and reader in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L263-L368).

#### 14.8.9 Material-constant packing

The compiler maps authored parameters into one deterministic logical material
constant block. It sorts parameters by logical name, then aligns and appends each
value using the material alignment/storage columns in the value-type table.
Scalars use 4-byte alignment, X2 values use 8, and X3/X4/matrices use 16. X3 and
`color3` occupy 16 bytes, `mat3` occupies three padded 16-byte columns for 48
bytes, and `mat4` occupies 64 bytes.

The cooked writer sorts the complete binding list by name and independently
requires material VALUE offsets to reproduce the same packed sequence. There may
be alignment padding but no arbitrary gap between consecutive material values.
This catches an otherwise well-typed but ABI-incompatible constant layout.
Packing is constructed in
[CypherShaderCompiler.cpp](../src/CypherTools/CypherShaderCompiler/CypherShaderCompiler.cpp#L616-L710)
and validated in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L389-L481).

#### 14.8.10 Logical IDs, SHST, and canonical ordering

Bindings are sorted by bytewise name order. Names must be unique, and logical IDs
must be unique. The stable ID is FNV-1a 64 over:

```text
"cypher.shader.binding.v1:" + exact_name_bytes
```

Zero is reserved and is remapped to one. The interface-wide duplicate-ID check
also detects the extremely unlikely collision caused by that remap. ID generation
is implemented in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L1278-L1307).

SHST has no inner header. Byte zero is the canonical empty-string sentinel. Each
sorted binding name then appears exactly once as raw name bytes followed by NUL.
Records point to consecutive offsets; there are no holes, aliases, suffix-sharing
entries, or unreferenced trailing bytes. An empty interface has a 32-byte SHRF
header, zero records, and a one-byte SHST containing only NUL.

#### 14.8.11 Interface hash

The interface hash is computed from the exact canonical payloads:

```text
ContentHash_Combine(
    ContentHash_Data(exact SHRF payload),
    ContentHash_Data(exact SHST payload)
)
```

It covers names, IDs, kinds, types, resource shapes, array counts, visibility,
flags, byte offsets, byte sizes, record/header sizes, and reserved zeros. It does
not cover parameter defaults/ranges, texture usage/color space, feature metadata,
or variant budget because CYSH does not persist those fields. They still affect
the compiler's source hash through the canonical recipe.

The reader recomputes each chunk hash, recombines the interface hash, validates
every record, checks sorted order and consecutive string offsets, rejects
name/ID duplicates, and publishes bindings only after the whole interface
passes. SHRF/SHST construction and hashing are in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L924-L1051),
and strict read-side validation is in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L2125-L2329).

### 14.9 Limits summary

| Contract | Limit |
| --- | ---: |
| Authored stage path | 259 bytes |
| Authored identifier | 64 bytes |
| Defines | 64 |
| Authored textures | 32 |
| Authored samplers | 16 |
| Authored parameters | 64 |
| Authored features | 32 |
| Values per enum feature | 16 |
| Default static variant budget | 64 |
| Maximum static variant budget | 1,024 |
| Cooked stages | Exactly 2 |
| Cooked bindings | 128 |
| Cooked array elements per binding | 1,024 |
| Cooked binding name | 127 bytes |
| SHST payload | 64 KiB |
| SHCD payload per stage | 16 MiB including terminal NUL |
| Runtime resource-loader file cap | 33 MiB by default |

Source-schema maxima permit 112 interface declarations before compiler gates
(32 textures, 16 samplers, 64 parameters), within the 128-record cooked bound.
Because compiler version 5 rejects independent samplers, its current authored
maximum is 96 emitted bindings.

### 14.10 Reader, loader, and runtime behavior

`CookedShader_Read` accepts CYSH V2 and V3 through one transactional API. It
first validates CYRS bounds, ordering, flags, and whole-content hash. It then
validates the exact version-specific chunk count and order, SHMD, SHRF/SHST when
present, both SHCD payloads, every per-chunk hash, zero padding, exact final file
size, and all semantic records. It builds a local borrowed view and copies that
view to the caller only after every check succeeds. Failed input cannot publish a
partially decoded shader.

The resource subsystem accepts only `.cyshader_c`, reads the entire file into
owned blob storage, applies the default 33 MiB cap, runs `CookedShader_Read`, and
publishes the resource only on success. Stage bytes and V3 binding-name views
borrow that owned storage for the resource lifetime. Loader ownership and
publication are implemented in
[CypherResource_RenderAssets.cpp](../src/CypherResource/CypherResource_RenderAssets.cpp#L124-L208),
and the size cap is declared in
[CypherResource_RenderAssets.h](../src/CypherResource/CypherResource_RenderAssets.h#L34-L39).

The OpenGL backend accepts either resource version because both produce the same
validated program/stage view. Before creating a native program it requires:

- backend OPENGL and kind GRAPHICS;
- no program flags;
- a supported GLSL core language/version;
- exactly one vertex and one fragment stage;
- no stage flags and GLSL_UTF8 payloads;
- a runtime OpenGL shading-language version at least as high as the cooked
  requirement.

It then compiles both SHCD strings with the driver and links the native program.
Driver compile and link logs are bounded and reported as renderer errors. Runtime
validation and creation are implemented in
[CypherRender_OpenGL_Shader.cpp](../src/CypherRender/OpenGL/CypherRender_OpenGL_Shader.cpp#L90-L295).

The current OpenGL program-creation path does not consume SHRF/SHST to assign
uniform locations, texture units, buffer bindings, or material constant uploads.
V3 bindings remain available through `CookedShader_FindBinding` and
`CookedShader_FindBindingById`, but generalized runtime binding and hot-reload ABI
reconciliation are incomplete. Draw-specific code may bind a narrow known set;
that does not constitute implementation of the complete V3 logical interface.

The two checked-in authored shader assets currently use source schema V1:
[cube.cyshader](../assets/render_smoke/cube.cyshader) and
[tile_surface.cyshader](../assets/shaders/tile_surface.cyshader). V2 behavior is
exercised by the format and compiler conformance tests rather than by a shipped
asset recipe.

### 14.11 Conformance tests and companion reference

The most direct conformance suites are:

- [source schema and decoder tests](../tests/CypherCommon/Formats/CypherCommon_RenderAsset_Tests.cpp);
- [CYSH writer/reader, mutation, layout, hash, and transaction tests](../tests/CypherCommon/Formats/CypherCommon_CookedShader_Tests.cpp);
- [compiler, reflection, include, profile, determinism, and identity tests](../tests/CypherTools/CypherShaderCompiler/CypherShaderCompiler_Tests.cpp);
- [resource-loader lifetime and validation tests](../tests/CypherResource/CypherResource_RenderAssets_Tests.cpp);
- [OpenGL cooked-shader consumer tests](../tests/CypherRender/CypherRender_OpenGL_Shader_Tests.cpp).

Additional render-asset rationale and cross-format material/texture interaction
are documented in [Render Assets](formats/RENDER_ASSETS.md). Source and tests are
authoritative if this living manual and implementation ever disagree.

## 15. Texture Format

The texture pipeline has two independent version axes. `.cytex` is the authored
CYKV recipe; CYTX is the cooked binary resource. Source schema V1 remains accepted,
but both source versions now produce CYTX V2. CYTX V1 is a reader-only compatibility
format.

### 15.1 Version and implementation matrix

| Layer | Current | Compatibility and behavior |
| --- | ---: | --- |
| CYKV language | 1 | Exact `@cykv 1` required |
| `cypher.texture` source schema | 2 | Compiler accepts V1 and V2 |
| Texture compiler API / implementation | 1 / 4 | Deterministic, thread-safe, validate and dry-run capable |
| CYRS envelope | 1 | Shared cooked-resource envelope |
| CYTX resource | 2 | Reader accepts V1 and V2; all current writers emit V2 |
| TXMD metadata | 2 | Metadata version follows CYTX resource version |

The version constants and public limits are declared in
[CypherCommon_RenderAssetSchema.h](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.h#L40-L62),
[CypherCommon_CookedTexture.h](../src/CypherCommon/Formats/CypherCommon_CookedTexture.h#L33-L71),
and
[CypherTextureCompiler.h](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.h#L30-L31).

### 15.2 Shared `.cytex` source rules

Every source texture begins with one exact header:

```cykv
@cykv 1
@schema "cypher.texture" <1|2>
```

Language version, schema ID, and schema version are matched exactly before the
semantic tree is traversed. Recipe objects are closed: unknown or misspelled fields
are errors. Optional dynamic objects elsewhere in the render schemas are nonempty
when present. These rules are implemented in
[CypherCommon_Schema.cpp](../src/CypherCommon/Tier2/CypherCommon_Schema.cpp#L730-L765)
and
[CypherCommon_RenderAssetSchema.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.cpp#L119-L143).

All source paths are canonical virtual paths. They are relative lowercase ASCII,
use `/`, contain no empty, `.` or `..` segments, contain no platform syntax, and
use an exact lowercase extension. A path may contain at most 259 bytes. An ASCII
identifier is 1–64 bytes, starts with an ASCII letter or `_`, and continues with
letters, decimal digits or `_`; case is significant. A stable identifier is 1–64
bytes, starts with a lowercase ASCII letter, and continues with lowercase letters,
digits, `_` or `-`. The two identifier grammars are intentionally different. See
[CypherCommon_DataValidation.h](../src/CypherCommon/Tier2/CypherCommon_DataValidation.h#L48-L75).

Generic recipe numbers accept `i64`, `u64`, or finite `f64`. Integers used through
the generic numeric rule are bounded to the exactly representable binary64 interval:
signed `−9,007,199,254,740,992..+9,007,199,254,740,992`, unsigned
`0..9,007,199,254,740,992`. See
[CypherCommon_RenderAssetSchema.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.cpp#L43-L69).

### 15.3 `.cytex` V1 source contract

V1 is a compact single-image recipe.

| Field | Type | Req. | Default | Values and validation |
| --- | --- | --- | --- | --- |
| `source` | string | Yes | — | Canonical `.png`, `.jpg`, `.jpeg`, `.tga`, or `.exr` path; 1–259 bytes |
| `usage` | string enum | No | `"color"` | `"color"`, `"normal"`, `"data"` |
| `color_space` | string enum | No | `"srgb"`, except omitted non-color or EXR input becomes `"linear"` | `"srgb"`, `"linear"` |
| `generate_mips` | bool | No | `true` | Complete generated mip chain when true; base mip only when false |

The closed V1 schema is defined in
[CypherCommon_RenderAssetSchema.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.cpp#L191-L211),
and its initialized defaults are visible in
[CypherCommon_RenderAsset.h](../src/CypherCommon/Formats/CypherCommon_RenderAsset.h#L69-L87).

The decoder applies two color-space rules after structural validation:

- omitted `color_space` becomes linear when `usage` is `normal` or `data`;
- omitted `color_space` becomes linear for EXR even when usage is `color`;
- normal, data, and EXR inputs explicitly tagged sRGB are rejected.

Those rules and the V1 extension allowlist are implemented in
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L696-L796).

The compiler normalizes V1 to the common internal recipe. It derives alpha mode as
`straight` for color, `data` for data usage, and `none` for normal usage. It maps
`generate_mips` to mip mode `generate` or `none`. The conversion is in
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L184-L202).

The checked-in development textures use this schema. A complete example is
[grid.cytex](../assets/textures/dev/grid.cytex):

```cykv
@cykv 1
@schema "cypher.texture" 1
{
    source = "textures/dev/grid.png"
    usage = "color"
    color_space = "srgb"
    generate_mips = true
}
```

### 15.4 `.cytex` V2 source contract

V2 makes alpha, mip, output, and streaming intent explicit while retaining a
single source image.

#### 15.4.1 Root fields

| Field | Type | Req. | Default | Values and validation |
| --- | --- | --- | --- | --- |
| `source` | string | Yes | — | Canonical `.png`, `.jpg`, `.jpeg`, `.tga`, `.exr`, `.dds`, or `.ktx2`; 1–259 bytes |
| `type` | string enum | Yes | — | Currently only `"2d"` |
| `usage` | string enum | Yes | — | `"color"`, `"normal"`, `"data"` |
| `color_space` | string enum | Yes | — | `"srgb"` or `"linear"`; non-color and EXR require linear |
| `alpha` | closed object | No | `none`, cutoff 0.5, no dilation | See below |
| `mips` | closed object | No | generated box/clamp, no coverage preservation | See below |
| `output` | closed object | No | auto/balanced | If present it must contain one or two fields |
| `streaming` | closed object | No | disabled | Presence enables mip-streamed metadata |

The complete V2 object graph and enum allowlists are defined in
[CypherCommon_RenderAssetSchema.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.cpp#L414-L531).
Non-color and EXR validation is implemented in
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L1650-L1726).

#### 15.4.2 `alpha`

| Field | Type | Req. | Default | Values and validation |
| --- | --- | --- | --- | --- |
| `mode` | string enum | Yes, when object exists | — | `"none"`, `"straight"`, `"premultiplied"`, `"mask"`, `"data"` |
| `cutoff` | number | No | `0.5` | Inclusive `[0,1]`; may be authored only when mode is `mask` |
| `dilate_rgb` | bool | No | `false` | May be true only for `straight` or `mask` |

For `normal` and `data` usage, alpha mode may only be `none` or `data`. Omitting
the entire object does not infer V1 alpha semantics; it selects `none`. Defaults are
declared in
[CypherCommon_RenderAsset.h](../src/CypherCommon/Formats/CypherCommon_RenderAsset.h#L250-L262),
and combination checks are in
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L1728-L1772).

#### 15.4.3 `mips`

| Field | Type | Req. | Default | Values and validation |
| --- | --- | --- | --- | --- |
| `mode` | string enum | Yes, when object exists | — | `"generate"`, `"preserve"`, `"none"` |
| `filter` | string enum | No | `"box"` | `"box"`, `"kaiser"`, `"lanczos"` |
| `edge` | string enum | No | `"clamp"` | `"clamp"`, `"repeat"`, `"mirror"` |
| `preserve_alpha_coverage` | bool | No | `false` | True requires alpha mode `mask` |

`filter`, `edge`, and `preserve_alpha_coverage` may be authored only when mode is
`generate`. Mode `preserve` requires a `.dds` or `.ktx2` source. Omitting the
entire object selects `generate`, `box`, `clamp`, and false. The decoder checks are
in
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L1774-L1843).

#### 15.4.4 `output`

The object is closed and must contain at least one member when present.

| Field | Type | Req. | Default | Values |
| --- | --- | --- | --- | --- |
| `format` | string enum | No | `"auto"` | `"auto"`, `"uncompressed"` |
| `quality` | string enum | No | `"balanced"` | `"fast"`, `"balanced"`, `"production"` |

The schema bounds are in
[CypherCommon_RenderAssetSchema.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.cpp#L439-L502),
and initialized defaults are in
[CypherCommon_RenderAsset.h](../src/CypherCommon/Formats/CypherCommon_RenderAsset.h#L289-L307).

#### 15.4.5 `streaming`

| Field | Type | Req. | Default | Values and validation |
| --- | --- | --- | --- | --- |
| `class` | stable identifier | Yes | — | 1–64 bytes; used to derive a serialized scheduling priority |
| `resident_mips` | `i64` or `u64` integer | No | `1` | Inclusive `1..15`; cannot exceed the actual mip count |

The object’s presence enables streaming. Its absence produces a fully resident
texture. If mip mode is `none`, `resident_mips` must be exactly one. Schema and
semantic checks are in
[CypherCommon_RenderAssetSchema.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.cpp#L503-L522)
and
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L1897-L1938).

#### 15.4.6 V2 examples

This example is accepted by the current compiler:

```cykv
@cykv 1
@schema "cypher.texture" 2
{
    source = "textures/source/panel.png"
    type = "2d"
    usage = "color"
    color_space = "srgb"
    alpha = { mode = "mask" cutoff = 0.42 }
    mips = { mode = "generate" filter = "box" edge = "clamp" }
    output = { format = "uncompressed" quality = "production" }
    streaming = { class = "world" resident_mips = 2u }
}
```

The compiler test verifies that this recipe produces CYTX V2 with three mips,
the final two resident, mask cutoff `0.42f`, desktop target metadata, and priority
128:
[CypherTextureCompiler_Tests.cpp](../tests/CypherTools/CypherTextureCompiler/CypherTextureCompiler_Tests.cpp#L811-L870).

The source decoder also accepts schema features whose execution is deliberately
gated by the compiler. The full policy example using Kaiser, repeat, coverage
preservation, and RGB dilation is in
[CypherCommon_RenderAsset_Tests.cpp](../tests/CypherCommon/Formats/CypherCommon_RenderAsset_Tests.cpp#L488-L560).

### 15.5 Compiler pipeline and current gates

#### 15.5.1 Dispatch, bounded input, and importer

The compiler accepts source schema V1 or V2. Version 2 uses the V2 decoder;
everything else is sent through the V1 decoder so the standard exact-version
diagnostic remains authoritative. Recipe input is bounded to 1 MiB of valid UTF-8,
and source-image input/decoded storage is bounded to 512 MiB. Dispatch and limits
are implemented in
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L58-L62)
and
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L1138-L1233).

The active importer accepts PNG, JPEG, TGA, and finite RGBA EXR. Decoded images
must normalize to `RGBA8_UNORM` or `RGBA32_FLOAT`. Float components are explicitly
written little-endian before cooking. Import logic is in
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L557-L623),
and the user-facing accepted-format diagnostic is at
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L1352-L1366).

#### 15.5.2 Implemented and gated policies

The current implementation supports:

- `generate` with a complete box-filtered, clamped mip chain;
- `none`, producing only mip zero;
- uncompressed RGBA8 or RGBA32-float storage;
- straight, premultiplied, mask, none, and data alpha metadata;
- fully resident or coarse-tail mip-streamed metadata.

The source schema intentionally leads implementation. The compiler currently
rejects:

| Schema-valid request | Compiler result |
| --- | --- |
| `.dds` or `.ktx2` source | Unsupported preserved-container import |
| `mips.mode = "preserve"` | Unsupported preserved mip import |
| `filter = "kaiser"` or `"lanczos"` | Only box is implemented |
| `edge = "repeat"` or `"mirror"` | Only clamp is implemented |
| `preserve_alpha_coverage = true` | Coverage-preserving generation is absent |
| `dilate_rgb = true` | Transparent-pixel RGB dilation is absent |

The exact gate and diagnostics are in
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L232-L283).

`output.format = "auto"` currently resolves to the same uncompressed format as
`"uncompressed"`; there is no target compressor. `quality` still changes source
identity so future encoder behavior cannot silently reuse older output. Current
format selection is in
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L301-L317)
and
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L1463-L1476).

#### 15.5.3 Mip semantics

Generated mips are semantic rather than raw byte averages:

- sRGB RGB components are converted to linear, filtered, then encoded back;
- normal-map vectors are reconstructed and renormalized;
- straight-alpha color is temporarily premultiplied during filtering to prevent
  transparent-edge color bleed;
- RGBA32-float EXR data remains floating point.

The mip planner caps the chain at 15 levels and aggregate payload at 512 MiB.
See
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L625-L959).

#### 15.5.4 Target and streaming metadata

Target mapping is Windows/Linux → `DESKTOP`, macOS → `APPLE`, and unknown →
`PORTABLE`. Streaming class maps to a stable `0..255` scheduling hint:

| Class | Priority |
| --- | ---: |
| `critical` | 255 |
| `ui` | 224 |
| `character` | 192 |
| `world` | 128 |
| `effects` | 96 |
| `background` | 32 |
| Any other valid stable identifier | 128 |

The table and target mapping are fixed compiler behavior in
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L157-L181)
and
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L286-L299).
The compiler rejects `resident_mips` greater than the generated chain at
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L1398-L1413).

#### 15.5.5 Deterministic source identity

The CYTX `sourceHash` combines:

1. compiler/API identity;
2. exact CYKV language/schema header plus the canonicalized recipe tree;
3. source image bytes;
4. importer/toolchain identity;
5. target platform, architecture, and build-profile configuration.

Formatting and object-member order therefore do not change the recipe component,
while any relevant source, toolchain, or target change does. Construction is in
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L1423-L1451).
The compiler descriptor declares deterministic, thread-safe, validate, and dry-run
support in
[CypherTextureCompiler.cpp](../src/CypherTools/CypherTextureCompiler/CypherTextureCompiler.cpp#L1631-L1651).

### 15.6 Shared CYRS envelope

Both CYTX versions use the little-endian CYRS V1 envelope. The fixed header is 80
bytes and every chunk-table record is 64 bytes.

| CYRS header offset | Type | Field |
| ---: | --- | --- |
| 0 | `u32` | Magic `CYRS` |
| 4 | `u32` | Container version, `1` |
| 8 | `u32` | Header bytes, `80` |
| 12 | `u32` | Resource FourCC, `CYTX` |
| 16 | `u32` | CYTX resource version |
| 20 | `u32` | Header flags |
| 24 | `u32` | Chunk count |
| 28 | `u32` | Reserved, zero |
| 32 | `u64` | Exact file bytes |
| 40 | `u64` | Chunk-table offset, canonically `80` |
| 48 | `u64` | Source-hash low half |
| 56 | `u64` | Source-hash high half |
| 64 | `u64` | Content-hash low half |
| 72 | `u64` | Content-hash high half |

| Chunk-record offset | Type | Field |
| ---: | --- | --- |
| 0 | `u32` | Chunk FourCC |
| 4 | `u32` | Codec: NONE=0, LZ4=1, ZSTD=2 |
| 8 | `u32` | Chunk flags |
| 12 | `u32` | Power-of-two alignment |
| 16 | `u64` | Absolute payload offset |
| 24 | `u64` | Stored bytes |
| 32 | `u64` | Decoded bytes |
| 40 | `u64` | Reserved, zero |
| 48 | `u64` | Stored-content hash low half |
| 56 | `u64` | Stored-content hash high half |

CYRS header flags are `HAS_SOURCE_HASH=bit 0` and
`HAS_CONTENT_HASH=bit 1`. Chunk flags are `COMPRESSED=bit 0`,
`OPTIONAL=bit 1`, and `HAS_CONTENT_HASH=bit 2`; unknown bits are invalid. The
generic parser permits at most 4,096 chunks and at most 1 MiB alignment, while
CYTX and CYMT impose the smaller type-specific counts and alignments documented
below.

`sourceHash` is optional in the generic envelope but present in normal compiler
output. CYTX requires the whole-content hash and a content hash on every chunk.
The whole-content hash covers every serialized byte after the fixed 80-byte
header: table, deterministic padding, and payloads. Current CYTX requires codec
NONE even though CYRS reserves LZ4 and ZSTD. The generic contract is in
[CypherCommon_CookedResource.h](../src/CypherCommon/Formats/CypherCommon_CookedResource.h#L30-L142),
and explicit field serialization is in
[CypherCommon_CookedResource.cpp](../src/CypherCommon/Formats/CypherCommon_CookedResource.cpp#L130-L207).

### 15.7 CYTX serialized enums, flags, and limits

CYTX uses `TXMD` for metadata, `TXDT` for one subresource payload, and `CTEX`
inside TXMD. These and all structural maxima are declared in
[CypherCommon_CookedTexture.h](../src/CypherCommon/Formats/CypherCommon_CookedTexture.h#L33-L71).

#### 15.7.1 Texture enums

| Enum | Serialized values |
| --- | --- |
| Dimension | `TEXTURE_2D=1`, `TEXTURE_1D=2`, `TEXTURE_3D=3`, `TEXTURE_CUBE=4` |
| Usage | `COLOR=0`, `NORMAL=1`, `DATA=2` |
| Color space | `SRGB=0`, `LINEAR=1` |
| Legacy pixel format | `UNKNOWN=0`, `RGBA8_UNORM=1`, `RGBA8_SRGB=2`, `RGBA32_FLOAT=3` |
| Alpha | `AUTO=0` writer input only, `NONE=1`, `STRAIGHT=2`, `PREMULTIPLIED=3`, `MASK=4`, `DATA=5` |
| Target | `PORTABLE=1`, `DESKTOP=2`, `APPLE=3`, `MOBILE=4`, `WEB=5` |
| Residency | `FULLY_RESIDENT=1`, `MIP_STREAMED=2` |

`COOKED_TEXTURE_FLAG_GENERATED_MIPS` is bit 0; every other bit is currently
invalid. Numeric permanence and comments are in
[CypherCommon_CookedTexture.h](../src/CypherCommon/Formats/CypherCommon_CookedTexture.h#L73-L116).

#### 15.7.2 Storage-format values

CYTX V2 serializes `render_format_t` as a `u32`, although the shared enum has a
`u16` underlying type. The defined values are:

| Range | Exact values |
| --- | --- |
| Sentinels | `UNKNOWN=0x0000` is accepted only as writer input when a legacy pixel format can resolve it; neither `UNKNOWN` nor `INVALID=0xFFFF` is valid serialized storage data |
| 8-bit | `R8_UNORM=0x0100`, `R8_SNORM=0x0101`, `R8_UINT=0x0102`, `R8_SINT=0x0103`; `RG8_* = 0x0110..0x0113`; `RGB8_UNORM/SNORM/UINT/SINT/SRGB = 0x0120..0x0124`; `RGBA8_UNORM/SNORM/UINT/SINT/SRGB = 0x0130..0x0134`; `BGRA8_UNORM/SRGB = 0x0140..0x0141`; `RGB565_UNORM=0x0150`, `RGBA4_UNORM=0x0151`, `RGB5A1_UNORM=0x0152` |
| 16-bit | `R16_UNORM/SNORM/UINT/SINT/FLOAT = 0x0200..0x0204`; corresponding `RG16 = 0x0210..0x0214`, `RGB16 = 0x0220..0x0224`, `RGBA16 = 0x0230..0x0234` |
| 32-bit/packed | `R32_UINT/SINT/FLOAT = 0x0300..0x0302`; `RG32 = 0x0310..0x0312`; `RGB32 = 0x0320..0x0322`; `RGBA32 = 0x0330..0x0332`; `RGB10A2_UNORM=0x0380`, `RGB10A2_UINT=0x0381`, `R11G11B10_UFLOAT=0x0382`, `RGB9E5_UFLOAT=0x0383` |
| Depth/stencil | `D16_UNORM=0x0400`, `D24_UNORM_S8_UINT=0x0401`, `D32_FLOAT=0x0402`, `D32_FLOAT_S8_UINT=0x0403`, `S8_UINT=0x0404` |
| BC | `BC1_RGB_UNORM/SRGB=0x0500..0x0501`, `BC1_RGBA_UNORM/SRGB=0x0502..0x0503`, `BC2=0x0504..0x0505`, `BC3=0x0506..0x0507`, `BC4_UNORM/SNORM=0x0508..0x0509`, `BC5=0x050A..0x050B`, `BC6H_UFLOAT/SFLOAT=0x050C..0x050D`, `BC7_UNORM/SRGB=0x050E..0x050F` |
| ETC/EAC | `ETC2_RGB8_UNORM/SRGB=0x0600..0x0601`, `ETC2_RGB8A1_UNORM/SRGB=0x0602..0x0603`, `ETC2_RGBA8_UNORM/SRGB=0x0604..0x0605`, `EAC_R11_UNORM/SNORM=0x0606..0x0607`, `EAC_RG11_UNORM/SNORM=0x0608..0x0609` |
| ASTC | UNORM/SRGB pairs for `4x4`, `5x4`, `5x5`, `6x5`, `6x6`, `8x5`, `8x6`, `8x8`, `10x5`, `10x6`, `10x8`, `10x10`, `12x10`, `12x12`, consecutively `0x0700..0x071B` |

The authoritative enum is
[CypherCommon_RenderFormat.h](../src/CypherCommon/Formats/CypherCommon_RenderFormat.h#L58-L124).
The cooked writer/reader knows tight texel or compressed-block layouts for every
listed format, including BC, ETC/EAC, and ASTC; it does not perform compression.
That validation table is in
[CypherCommon_CookedTexture.cpp](../src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp#L1280-L1475).

#### 15.7.3 Hard limits

| Property | Maximum |
| --- | ---: |
| Width, height, or depth | 16,384 |
| Mip levels | 15 |
| Layers | 256 |
| Frames | 256 |
| Cube faces | 6 exactly for cube, 1 for other supported shapes |
| Total subresources | 1,024 |
| Aggregate encoded payload | 512 MiB |
| Streaming priority | 255 |
| TXMD alignment | 8 bytes |
| TXDT alignment | 16 bytes |

### 15.8 CYTX V1 binary layout

CYTX V1 is retained for reading existing cooked resources. There is no public or
production V1 writer; the conformance tests contain a
[private fixture encoder](../tests/CypherCommon/Formats/CypherCommon_CookedTexture_Tests.cpp#L61-L170).
A V1 file contains `TXMD` first and one `TXDT` per mip. All chunks must be
uncompressed and individually hashed.

#### 15.8.1 V1 TXMD header, 64 bytes

| Offset | Type | Field | Constraint |
| ---: | --- | --- | --- |
| 0 | `u32` | Metadata magic | `CTEX` |
| 4 | `u32` | Metadata version | `1` |
| 8 | `u32` | Header bytes | `64` |
| 12 | `u32` | Dimension | Serialized dimension enum |
| 16 | `u32` | Legacy pixel format | V1 legacy enum |
| 20 | `u32` | Usage | `0..2` |
| 24 | `u32` | Color space | `0..1` |
| 28 | `u32` | Texture flags | Known bits only |
| 32 | `u32` | Width | Validated extent |
| 36 | `u32` | Height | Validated extent |
| 40 | `u32` | Depth | Validated extent |
| 44 | `u32` | Layers | Effectively one in the V1 subresource model |
| 48 | `u32` | Faces | Effectively one; cube/multi-face cannot match V1 mip-only count |
| 52 | `u32` | Mip levels | `1..15` |
| 56 | `u32` | Reserved | Zero |
| 60 | `u32` | Reserved | Zero |

The exact field order and compatibility defaults are decoded in
[CypherCommon_CookedTexture.cpp](../src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp#L577-L639).

#### 15.8.2 V1 mip record, 32 bytes each

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | `u32` | Mip level |
| 4 | `u32` | Width |
| 8 | `u32` | Height |
| 12 | `u32` | Depth |
| 16 | `u32` | Tight row pitch |
| 20 | `u32` | Data chunk index |
| 24 | `u64` | Data bytes |

The reader converts each record to a V2-shaped subresource at frame/layer/face
zero and derives slice pitch from the format and mip extent. See
[CypherCommon_CookedTexture.cpp](../src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp#L720-L793).

V1 has no explicit alpha, target, frame, residency, streaming, slice-pitch, or
aggregate-byte fields. The reader supplies:

- `storageFormat` mapped from the legacy pixel format;
- alpha inferred as straight for alpha-bearing color, data for alpha-bearing
  data usage, otherwise none;
- cutoff `0.5`;
- target `PORTABLE`;
- one frame;
- every mip fully resident;
- streaming priority zero;
- resident and total byte counts derived from the validated mip payloads;
- one subresource per mip.

General subresource lookup therefore accepts only frame/layer/face zero for V1,
as documented in
[CypherCommon_CookedTexture.h](../src/CypherCommon/Formats/CypherCommon_CookedTexture.h#L353-L374).

### 15.9 CYTX V2 binary layout

CYTX V2 contains one `TXMD` followed by one independently hashed `TXDT` for every
subresource. Canonical subresource order is mip, then frame, then layer, then face.
All subresources in the resident coarse-mip tail are therefore contiguous at the
end of the data sequence. Each record’s `dataChunkIndex` is its zero-based record
index plus one. The order is part of the public contract in
[CypherCommon_CookedTexture.h](../src/CypherCommon/Formats/CypherCommon_CookedTexture.h#L180-L197).

#### 15.9.1 V2 TXMD header, 128 bytes

| Offset | Type | Field | Constraint |
| ---: | --- | --- | --- |
| 0 | `u32` | Metadata magic | `CTEX` |
| 4 | `u32` | Metadata version | `2` |
| 8 | `u32` | Header bytes | `128` |
| 12 | `u32` | Dimension | Serialized dimension enum |
| 16 | `u32` | Storage format | Valid `render_format_t` value |
| 20 | `u32` | Usage | `COLOR`, `NORMAL`, or `DATA` |
| 24 | `u32` | Color space | `SRGB` or `LINEAR` |
| 28 | `u32` | Alpha mode | `NONE..DATA`; AUTO is not serialized |
| 32 | `u32` | Target | `PORTABLE..WEB` |
| 36 | `u32` | Residency | Fully resident or mip streamed |
| 40 | `u32` | Texture flags | Known bits only |
| 44 | `u32` | Width | `1..16,384` |
| 48 | `u32` | Height | `1..16,384` |
| 52 | `u32` | Depth | `1..16,384` |
| 56 | `u32` | Layers | `1..256` |
| 60 | `u32` | Faces | Shape-dependent |
| 64 | `u32` | Frames | `1..256` |
| 68 | `u32` | Mip levels | `1..15`, no more than full chain |
| 72 | `u32` | Subresource count | Exact product `mips*frames*layers*faces` |
| 76 | `u32` | Resident mip levels | Full count or nonzero streamed tail |
| 80 | `u32` | First resident subresource | Derived canonical tail index |
| 84 | `u32` | Resident subresource count | Derived tail count |
| 88 | `u32` | Streaming priority | `0..255` |
| 92 | `f32` | Alpha cutoff | Finite `[0,1]`; exactly `0.5f` unless mask |
| 96 | `u64` | Resident data bytes | Exact sum of resident payloads |
| 104 | `u64` | Total data bytes | Exact sum of all payloads |
| 112 | `u64` | Reserved | Zero |
| 120 | `u64` | Reserved | Zero |

The serializer is
[CypherCommon_CookedTexture.cpp](../src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp#L519-L555).

#### 15.9.2 V2 subresource record, 64 bytes each

| Offset | Type | Field | Constraint |
| ---: | --- | --- | --- |
| 0 | `u32` | Mip level | Matches canonical record index |
| 4 | `u32` | Frame | Matches canonical record index |
| 8 | `u32` | Layer | Matches canonical record index |
| 12 | `u32` | Face | Matches canonical record index |
| 16 | `u32` | Width | Exact mip extent |
| 20 | `u32` | Height | Exact mip extent |
| 24 | `u32` | Depth | Exact mip extent |
| 28 | `u32` | Row pitch | Exact tight block-row bytes |
| 32 | `u32` | Slice pitch | Exact tight block-slice bytes |
| 36 | `u32` | Data chunk index | Record index + 1 |
| 40 | `u32` | Flags | Zero in V2 |
| 44 | `u32` | Reserved | Zero |
| 48 | `u64` | Data bytes | Exact block-derived payload bytes |
| 56 | `u64` | Reserved | Zero |

Serialization is in
[CypherCommon_CookedTexture.cpp](../src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp#L557-L575).

#### 15.9.3 Shape, layout, and semantic invariants

The reader and writer enforce:

- 1D has height=depth=faces=1;
- 2D has depth=faces=1;
- 3D has layers=faces=1;
- cube has square width/height, depth=1, and exactly six faces;
- generated-mips flag means the serialized chain is the complete chain to 1×1×1;
- the subresource count and resident-tail index/count are exact derived values;
- every extent, row pitch, slice pitch, and byte count is the tight size implied
  by the storage format’s texel or block geometry; row padding is forbidden;
- sRGB metadata must agree with the format’s sRGB variant;
- non-color usage is linear;
- an alpha-using mode requires a format with alpha;
- straight, premultiplied, and mask alpha require color usage;
- alpha cutoff is finite `[0,1]`, canonical positive zero, and `0.5f` outside mask mode.

Descriptor normalization is in
[CypherCommon_CookedTexture.cpp](../src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp#L157-L350),
and exact subresource derivation is in
[CypherCommon_CookedTexture.cpp](../src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp#L353-L483).

#### 15.9.4 Canonical physical layout and compatibility writing

TXMD is aligned to 8 bytes. Every TXDT is aligned to 16 bytes. Alignment gaps
must be zero and the final payload must end at the declared file size; extra tail
bytes are rejected. Every chunk is stored verbatim with `codec=NONE`, equal stored
and decoded sizes, and its own content hash. The writer seals the whole-content
hash after table, padding, and payload bytes are final. See
[CypherCommon_CookedTexture.cpp](../src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp#L1684-L1855)
and
[CypherCommon_CookedTexture.cpp](../src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp#L2091-L2202).

The legacy `CookedTexture_Write` entry point accepts one 2D mip chain, converts it
to explicit subresources, and delegates to `CookedTexture_WriteSubresources`.
It therefore emits CYTX V2. This behavior is explicit in
[CypherCommon_CookedTexture.h](../src/CypherCommon/Formats/CypherCommon_CookedTexture.h#L322-L353)
and
[CypherCommon_CookedTexture.cpp](../src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp#L1858-L1930).
The V1-source integration test asserts resource version 2 in
[CypherTextureCompiler_Tests.cpp](../tests/CypherTools/CypherTextureCompiler/CypherTextureCompiler_Tests.cpp#L533-L595).

### 15.10 Reader, resource loader, renderer, and editor behavior

`CookedTexture_Read` accepts CYTX V1 and V2 through one transactional API. It
validates resource type/version, chunk count, whole-content hash, TXMD structure,
every subresource record, every TXDT descriptor and content hash, canonical
ordering, zero padding, and exact final file size before publishing the borrowed
view. V1 permits at most 16 chunks; V2 at most 1,025. The implementation is in
[CypherCommon_CookedTexture.cpp](../src/CypherCommon/Formats/CypherCommon_CookedTexture.cpp#L1933-L2202).

The resource subsystem accepts `.cytex_c`, reads the entire file into owned blob
storage under a default 513 MiB cap, calls `CookedTexture_Read`, and exposes a
zero-copy borrowed view for the handle lifetime. It does not partially read the
resident tail. Loader limits and lifetime are documented in
[CypherResource_RenderAssets.h](../src/CypherResource/CypherResource_RenderAssets.h#L34-L39)
and
[CypherResource_RenderAssets.h](../src/CypherResource/CypherResource_RenderAssets.h#L154-L170);
loading is implemented in
[CypherResource_RenderAssets.cpp](../src/CypherResource/CypherResource_RenderAssets.cpp#L211-L268).

The renderer has a narrower immutable texture API: one RGBA8 2D base image,
exactly `width*height*4` bytes, with sRGB, repeat, and generate-mips Booleans. The
OpenGL backend uploads that base through `glTexImage2D` and asks the driver to
generate mips. It does not consume arbitrary CYTX subresources, float formats,
cube/array/3D shapes, block-compressed payloads, or streamed residency. See
[CypherRender_Texture.h](../src/CypherRender/CypherRender_Texture.h#L20-L51),
[CypherRender_Texture.cpp](../src/CypherRender/CypherRender_Texture.cpp#L70-L112),
and
[CypherRender_OpenGL_Texture.cpp](../src/CypherRender/OpenGL/CypherRender_OpenGL_Texture.cpp#L12-L79).

The Tile Editor preview is the current bridge from cooked data to this narrow
renderer path. It:

- reads the full cooked material and full cooked texture;
- accepts only color `RGBA8_UNORM` or `RGBA8_SRGB`;
- copies mip zero only;
- caps the cooked texture read at 96 MiB and the base image at 64 MiB;
- uploads the copied RGBA8 bytes through `R_CreateTexture2D`.

Decode restrictions are in
[CypherTileMaterialPreview.cpp](../src/CypherTools/CypherTileEditor/Core/CypherTileMaterialPreview.cpp#L51-L134),
and upload is in
[CypherTileMaterialPreview.cpp](../src/CypherTools/CypherTileEditor/Core/CypherTileMaterialPreview.cpp#L220-L258).

Picasso has an in-memory texture document, but its compile command explicitly
reports that `.cytex` authoring serialization is unavailable and writes no
artifact. See
[PicassoMainWindow.cpp](../src/CypherTools/Picasso/Gui/PicassoMainWindow.cpp#L2536-L2561).

The remaining texture integration work is therefore concrete:

- target BC/ETC/ASTC encoders and DDS/KTX2 preserved import;
- Kaiser/Lanczos, repeat/mirror, alpha-coverage, and dilation execution;
- CYTX chunk decompression if LZ4/ZSTD storage is adopted;
- partial resident-tail I/O and a streaming scheduler;
- a general CYTX-to-renderer upload path for all supported dimensions, formats,
  precomputed mips, layers, faces, and frames;
- Picasso `.cytex` recipe serialization/compiler integration.

### 15.11 Conformance tests and companion reference

The primary executable specifications are:

- [source-schema and decoder tests](../tests/CypherCommon/Formats/CypherCommon_RenderAsset_Tests.cpp);
- [CYTX writer/reader, V1 compatibility, mutation, layout, and deterministic-byte tests](../tests/CypherCommon/Formats/CypherCommon_CookedTexture_Tests.cpp);
- [compiler import, mip, target, streaming, determinism, and identity tests](../tests/CypherTools/CypherTextureCompiler/CypherTextureCompiler_Tests.cpp);
- [resource-loader ownership and validation tests](../tests/CypherResource/CypherResource_RenderAssets_Tests.cpp);
- [Tile Editor material-preview tests](../tests/CypherTools/CypherTileEditor/CypherTileMaterialPreview_Tests.cpp);
- [renderer texture tests](../tests/CypherRender/CypherRender_Texture_Tests.cpp).

Additional cross-format rationale is in
[Render Assets](formats/RENDER_ASSETS.md). Source and tests remain authoritative
if this living manual and implementation diverge.

## 16. Material Format

The material pipeline also has separate source and cooked versions. `.cymat` V1
is a loose shader-path, texture-map, and value-map recipe that compiles to CYMT
V1. `.cymat` V2 resolves inheritance and validates every binding against a typed
V2 shader source contract, then compiles to CYMT V2. The common cooked reader
accepts both CYMT generations.

### 16.1 Version and implementation matrix

| Layer | Current | Compatibility and behavior |
| --- | ---: | --- |
| CYKV language | 1 | Exact `@cykv 1` required |
| `cypher.material` source schema | 2 | Compiler accepts V1 and V2 |
| Material compiler API / implementation | 1 / 2 | Deterministic, thread-safe, validate and dry-run capable |
| CYRS envelope | 1 | Shared cooked-resource envelope |
| CYMT resource | 2 | V1 source writes V1; V2 source writes V2; reader accepts both |
| MTMD metadata | 2 | Metadata V1 for CYMT V1, metadata V2 for CYMT V2 |

The source and cooked version constants are in
[CypherCommon_RenderAssetSchema.h](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.h#L40-L62),
[CypherCommon_CookedMaterial.h](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.h#L43-L73),
and
[CypherMaterialCompiler.h](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.h#L30-L31).

### 16.2 Shared `.cymat` source rules

Every source material begins with:

```cykv
@cykv 1
@schema "cypher.material" <1|2>
```

The exact CYKV header, closed-object policy, 259-byte canonical-path limit,
64-byte ASCII/stable-identifier limits, finite-number rules, and exact-in-binary64
integer bounds described in Section 15.2 apply here as well. Material-specific
caps are 32 features, 32 texture bindings, 64 parameter values, V1 numeric vectors
of 2–4 components, V2 numeric arrays of 2–16 components, and UV set `0..7`:
[CypherCommon_RenderAssetSchema.h](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.h#L45-L62).

### 16.3 `.cymat` V1 source contract

| Field | Type | Req. | Default | Values and validation |
| --- | --- | --- | --- | --- |
| `shader` | string | Yes | — | Canonical `.cyshader` path, 1–259 bytes |
| `textures` | dynamic object | No | Empty | If present, 1–32 entries; key is ASCII identifier, value is canonical `.cytex` path |
| `parameters` | dynamic object | No | Empty | If present, 1–64 entries; key is ASCII identifier, value is bool, numeric scalar, or numeric array length 2–4 |

V1 parameter scalar syntax preserves source kind while parsing (`i64`, `u64`, or
`f64`), but the decoded V1 material view normalizes every numeric scalar/vector
component to `f64`. Boolean parameters remain Boolean. Source values and view
types are defined in
[CypherCommon_RenderAssetSchema.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.cpp#L213-L239)
and
[CypherCommon_RenderAsset.h](../src/CypherCommon/Formats/CypherCommon_RenderAsset.h#L89-L118).

Semantic decoding validates shader/texture extensions and identifier keys, then
publishes only after the entire document succeeds. It does not reject duplicate
dynamic-map names itself; the cooked writer later sorts and rejects duplicates.
See
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L799-L971)
and
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L308-L335).

The checked-in development materials use V1. A complete example is
[grid.cymat](../assets/materials/dev/grid.cymat):

```cykv
@cykv 1
@schema "cypher.material" 1
{
    shader = "shaders/tile_surface.cyshader"
    textures = { base_color = "textures/dev/grid.cytex" }
    parameters = {
        tint = [1.0, 1.0, 1.0, 1.0]
        uv_scale = [1.0, 1.0]
    }
}
```

#### 16.3.1 V1 compiler validation and identity

The V1 compiler reads the referenced `.cyshader` and every `.cytex` as source
text. It runs the V1 shader or V1 texture decoder and canonical-hashes each
dependency. It does not inspect a typed shader interface, prove that names exist,
check parameter types against reflection, or require a cooked dependency. The
dependency path is implemented in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L601-L755).

V1 output is CYMT V1. Its `sourceHash` combines compiler/API/schema/cooked-version
identity, canonical material recipe hash, canonical shader-recipe hash, and texture
recipe hashes in material source order. Cooking and identity are in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L2856-L2979).

### 16.4 `.cymat` V2 source contract

V2 represents partial override layers. Every root field is structurally optional,
but a layer must identify either `base` or `shader`. A fully resolved chain must
select a shader.

#### 16.4.1 Root fields

| Field | Type | Req. | Default | Values and validation |
| --- | --- | --- | --- | --- |
| `base` | string | Conditional | — | Canonical `.cymat`; enables inherited partial overrides and `null` removals |
| `shader` | string | Conditional | Inherited | Canonical `.cyshader`; required when no base, may replace inherited shader |
| `domain` | string enum | No | Inherited; `"surface"` if unset across the chain | `"surface"`, `"decal"`, `"ui"`, `"postprocess"`, `"particle"` |
| `features` | dynamic object | No | Inherited/empty | 1–32 operations; see below |
| `state` | closed object | No | Inherited/default state | 1–5 members; see below |
| `surface` | string | No | Inherited/absent | Canonical `.cysurface`; currently compiler-gated |
| `textures` | dynamic object | No | Inherited/empty | 1–32 operations; see below |
| `parameters` | dynamic object | No | Inherited/defaulted | 1–64 operations; see below |

If a layer authors both `surface` and `domain`, its domain must be `surface` or
`decal`. There is no source operation that removes an inherited surface. The root
schema is in
[CypherCommon_RenderAssetSchema.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.cpp#L533-L645),
and path/root combination checks are in
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L1941-L2050).

#### 16.4.2 `features`

Each key is an ASCII identifier naming a shader feature. Values are:

| Value | Meaning |
| --- | --- |
| `bool` | Set or override a Boolean feature |
| stable-identifier string | Set or override an enum feature value |
| `null` | Remove an inherited feature; legal only in a layer with `base` |

Duplicate names are rejected during V2 source decoding. String enum values use
the stable-identifier grammar, so lowercase hyphenated values are source-valid.
The decoder is in
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L2112-L2171).

#### 16.4.3 `state`

The state object is closed and nonempty when present.

| Field | Type | Req. | Resolved default | Values and merge behavior |
| --- | --- | --- | --- | --- |
| `alpha_mode` | string enum | No | `"opaque"` | `"opaque"`, `"mask"`, `"blend"`, `"additive"`; changing away from mask resets cutoff to 0.5 |
| `alpha_cutoff` | number | No | `0.5` | Inclusive `[0,1]`; requires mask in this layer or an inherited resolved mask |
| `two_sided` | bool | No | `false` | Partial override |
| `casts_shadows` | bool | No | `true` | Partial override |
| `receives_shadows` | bool | No | `true` | Partial override |

Defaults and authored-presence bits are in
[CypherCommon_RenderAsset.h](../src/CypherCommon/Formats/CypherCommon_RenderAsset.h#L336-L356).
Decode-time cutoff rules are in
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L2052-L2110),
and resolved merge behavior is in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L1069-L1104).

#### 16.4.4 `textures`

Each map key is an ASCII identifier naming a sampled-texture shader binding. A
value is either `null` or a closed, nonempty object.

| Field | Type | Req. | Default | Values and merge behavior |
| --- | --- | --- | --- | --- |
| `resource` | string | Conditional | Inherited | Canonical `.cytex`; required for a standalone/new binding without an inherited resource |
| `sampler` | stable identifier | No | Inherited/absent | Combined-binding sampler preset; does not name an independent shader sampler |
| `uv` | closed object | No | Inherited/default UV | Presence atomically replaces the entire inherited UV block |

`null` removes an inherited binding and is legal only in a layer with `base`.
Duplicate binding names are rejected. A partial object may inherit `resource`
from a base; if no resource exists after resolution, compilation fails.
An inherited sampler may be replaced, but the source format has no operation that
clears it while retaining the texture binding.

The nested `uv` object is closed and nonempty when present:

| UV field | Type | Req. | Default when `uv` is authored | Constraint |
| --- | --- | --- | --- | --- |
| `set` | `i64` or `u64` integer | No | `0` | Inclusive `0..7` |
| `scale` | numeric array | No | `[1,1]` | Exactly two finite components |
| `offset` | numeric array | No | `[0,0]` | Exactly two finite components |
| `rotation` | number | No | `0` | Finite generic number |

Because `uv` is atomic, authoring only `offset` resets inherited set, scale, and
rotation to these defaults. Source schema and decoder are in
[CypherCommon_RenderAssetSchema.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAssetSchema.cpp#L581-L617)
and
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L2173-L2302).
Atomic merge behavior is explicit in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L1138-L1178).

#### 16.4.5 `parameters`

Each key is an ASCII identifier naming a shader value binding. Values are:

| Source value | Decoded V2 kind | Constraint |
| --- | --- | --- |
| `bool` | BOOL | Exact Boolean |
| signed integer | I64 | Generic exact-in-binary64 bound |
| unsigned integer | U64 | Generic exact-in-binary64 bound |
| floating scalar | F64 | Finite |
| numeric array | F64_ARRAY | 2–16 finite numeric components |
| `null` | Removal operation | Legal only in a layer with `base` |

Duplicate parameter names are rejected. The source value representation is in
[CypherCommon_RenderAsset.h](../src/CypherCommon/Formats/CypherCommon_RenderAsset.h#L122-L137),
and map decoding is in
[CypherCommon_RenderAsset.cpp](../src/CypherCommon/Formats/CypherCommon_RenderAsset.cpp#L2304-L2352).

#### 16.4.6 V2 authoring example

This compiler-supported example demonstrates inheritance and atomic UV replacement:

```cykv
@cykv 1
@schema "cypher.material" 2
{
    base = "materials/templates/world_surface.cymat"
    state = {
        alpha_mode = "opaque"
        receives_shadows = false
    }
    textures = {
        albedo_texture = {
            sampler = "point_clamp"
            uv = { offset = [0.25, 0.5] }
        }
        detail_texture = null
    }
    parameters = {
        count = 6
        roughness = null
    }
}
```

In the exercised base chain, the final albedo retains its inherited resource but
gets sampler `point_clamp`; its authored UV block resets set to 0, scale to `[1,1]`,
and rotation to 0 while applying the new offset. The complete three-layer fixture
and cooked assertions are in
[CypherMaterialCompiler_Tests.cpp](../tests/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler_Tests.cpp#L573-L704).

The source decoder’s full example also shows features and surface syntax that is
valid source but currently gated at compile time:
[CypherCommon_RenderAsset_Tests.cpp](../tests/CypherCommon/Formats/CypherCommon_RenderAsset_Tests.cpp#L638-L716).

### 16.5 V2 inheritance and compiler contract

#### 16.5.1 Chain resolution

A V2 material may inherit only from V2 materials. The compiler rejects cycles and
allows at most 16 recipes total: the leaf plus at most 15 bases. It reads and
canonical-hashes every base, then applies layers from the oldest base toward the
leaf. Chain loading is implemented in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L791-L994).

Resolution behavior is:

- shader, surface, and explicitly authored domain replace inherited values;
- authored state members replace only those members;
- feature and parameter values replace by name; `null` removes by name;
- texture resource and sampler merge independently by binding;
- an authored UV object replaces the complete inherited UV object;
- a removed value that does not exist is harmless;
- final feature/texture/parameter counts remain capped at 32/32/64;
- the final result must select a shader and every surviving texture must have a
  resource.

The merge and oldest-to-leaf loop are in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L1060-L1277).

#### 16.5.2 Shader-source validation and interface reconstruction

The resolved shader must be a `.cyshader` source schema V2. The material compiler
does not open cooked CYSH. It parses the shader source and reconstructs the logical
interface that CYSH V3 would expose:

- sampled textures become `SAMPLED_TEXTURE` bindings;
- parameter types map to cooked shader value types;
- all graphics stages are assigned;
- required/material flags are reproduced;
- parameter names are sorted and packed by the shared value-type alignment rules;
- logical binding IDs and the interface hash use the shared cooked-shader helpers.

Source-to-cooked type mapping is:

| Shader source type | Cooked value type |
| --- | --- |
| `bool` | BOOL |
| `i32` | I32 |
| `u32` | U32 |
| `f32` | F32 |
| `f32x2` | F32X2 |
| `f32x3`, `color3` | F32X3 |
| `f32x4`, `color4` | F32X4 |
| `mat3` | F32X3X3 |
| `mat4` | F32X4X4 |

Interface reconstruction is in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L1280-L1467).

The current compiler rejects a referenced shader declaring any feature or any
independent sampler. Features require a versioned cooked variant table; independent
samplers require a material-to-sampler association that CYMT V2 does not encode.
These gates are in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L1469-L1641).

#### 16.5.3 Texture-binding validation

Every surviving texture reference must point to a `.cytex` source schema V2. The
compiler parses and canonical-hashes the recipe; it does not require cooked CYTX.
For every binding it then requires:

- a shader texture with the same ASCII name;
- a shader type of exactly `texture2d`;
- a texture recipe type of `2d`;
- exact usage equality;
- exact color-space equality.

Unknown material bindings and missing required shader bindings fail compilation.
Optional shader textures omitted by the material are not cooked into CYMT. Source
dependency loading is in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L1709-L1896),
and interface matching is in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L1899-L2031).

`sampler` remains an arbitrary preset identifier on a combined sampled-texture
binding. It does not link to an independently reflected CYSH sampler, as stated in
[CypherCommon_CookedMaterial.h](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.h#L98-L111).

There is one current grammar mismatch to avoid: source sampler and feature enum
values use stable identifiers and therefore allow `-`, while the CYMT V2 writer
validates these persisted strings as ASCII identifiers, which do not allow `-`.
Features are already compiler-gated, but a hyphenated sampler can pass source
decoding and fail during cooking. The cooked checks are in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L1632-L1697).

#### 16.5.4 Parameter validation and defaults

Unknown authored parameter names fail. For each shader parameter, the compiler
chooses the authored resolved value, otherwise the shader-declared default,
otherwise fails if required, otherwise omits it. Selection is implemented in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L2194-L2300).

Conversion is exact:

| Shader parameter | Accepted material value | Additional checks |
| --- | --- | --- |
| BOOL | BOOL only | Exact type |
| I32 | I64 or U64 integer | Inclusive signed 32-bit range |
| U32 | I64 or U64 integer | Nonnegative and at most `UINT32_MAX` |
| F32 | I64, U64, or F64 scalar | Finite and representable as finite `f32` |
| F32X2 | F64_ARRAY length 2 | Every component finite and `f32`-representable |
| F32X3 / COLOR3 | F64_ARRAY length 3 | Same |
| F32X4 / COLOR4 | F64_ARRAY length 4 | Same |
| MAT3 | F64_ARRAY length 9 | Same |
| MAT4 | F64_ARRAY length 16 | Same |

Shader-declared scalar minimum and maximum constraints are enforced after type and
range conversion. The complete converter is in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L2034-L2192).

#### 16.5.5 Current feature and surface gates

The source schema and CYMT V2 can represent resolved features and a surface path,
but the compiler currently rejects:

- **any feature operation in any layer**, including a feature added by a base and
  removed by a derived layer, because the chain records that an operation occurred;
- any resolved `surface`, because no versioned `.cysurface` compiler/runtime
  contract exists.

The gates are applied after inheritance resolution in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L2458-L2508).
Current compiler-created CYMT V2 therefore has no surface and zero feature records,
even though the binary format supports both. Construction is in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L2551-L2623).

#### 16.5.6 Bounded input and deterministic identity

Material recipes and every source dependency are bounded to 1 MiB UTF-8 text.
The compiler accepts only schema versions 1 and 2, uses a 259-byte tool path bound,
and caps a V2 chain at 16 recipes. Limits and version dispatch are in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L55-L60)
and
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L512-L550).

V2 `sourceHash` combines:

1. compiler/API/source-schema/CYMT-version identity;
2. canonical base-recipe hashes from oldest base to immediate base;
3. canonical leaf recipe hash;
4. canonical shader-source hash;
5. reconstructed shader interface hash;
6. canonical hashes of unique texture-source paths, deduplicated and sorted by
   virtual path.

The exact combination is in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L2303-L2405).
The compiler descriptor declares deterministic, thread-safe, validate, and dry-run
support in
[CypherMaterialCompiler.cpp](../src/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler.cpp#L3053-L3072).

### 16.6 CYMT chunks, enums, flags, and limits

CYMT uses the same CYRS V1 envelope and exact 80-byte header/64-byte chunk records
listed in Section 15.6, with resource FourCC `CYMT`. Both versions require
whole-content and per-chunk hashes. Current CYMT accepts only codec NONE despite
the generic envelope reserving LZ4 and ZSTD.

Type-specific chunks are:

| FourCC | Purpose | Alignment |
| --- | --- | ---: |
| `MTMD` | Fixed header and fixed-size records | 8 |
| `MTCD` | V2 packed material constants; omitted when empty | 16 |
| `MTST` | Canonical NUL-terminated strings | 1 |

The inner MTMD magic is `CMAT`. String table and constant data are independently
capped at 64 KiB. V1 uses a 48-byte header, 16-byte texture record, and 48-byte
parameter record. V2 uses a 128-byte header, 32-byte feature record, 64-byte
texture record, and 40-byte parameter record. Constants are declared in
[CypherCommon_CookedMaterial.h](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.h#L43-L73).

#### 16.6.1 Domains, alpha modes, feature types, and flags

| Enum/flags | Serialized values |
| --- | --- |
| Domain | `SURFACE=0`, `DECAL=1`, `UI=2`, `POSTPROCESS=3`, `PARTICLE=4` |
| Alpha mode | `OPAQUE=0`, `MASK=1`, `BLEND=2`, `ADDITIVE=3` |
| Feature value | `BOOL=1`, `ENUM=2` |
| V2 flags | two-sided bit0, casts-shadows bit1, receives-shadows bit2 |
| V1 flags | No bits permitted; value must be zero |

The source enum order is declared in
[CypherCommon_RenderAsset.h](../src/CypherCommon/Formats/CypherCommon_RenderAsset.h#L328-L350),
feature values and flags in
[CypherCommon_CookedMaterial.h](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.h#L75-L96),
and version-specific known flag sets in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L34-L39).

#### 16.6.2 Packed parameter type values

CYMT V2 uses the shared shader-value enum:

| Code | Type | Active bytes | Alignment | Occupied storage |
| ---: | --- | ---: | ---: | ---: |
| 0 | NONE, invalid for a parameter | 0 | 0 | 0 |
| 1 | BOOL | 4 | 4 | 4 |
| 2 | I32 | 4 | 4 | 4 |
| 3 | U32 | 4 | 4 | 4 |
| 4 | F32 | 4 | 4 | 4 |
| 5 | F64 | 8 | 8 | 8 |
| 6 | I32X2 | 8 | 8 | 8 |
| 7 | I32X3 | 12 | 16 | 16 |
| 8 | I32X4 | 16 | 16 | 16 |
| 9 | U32X2 | 8 | 8 | 8 |
| 10 | U32X3 | 12 | 16 | 16 |
| 11 | U32X4 | 16 | 16 | 16 |
| 12 | F32X2 | 8 | 8 | 8 |
| 13 | F32X3 | 12 | 16 | 16 |
| 14 | F32X4 | 16 | 16 | 16 |
| 15 | F32X3X3 | 36 | 16 | 48 |
| 16 | F32X4X4 | 64 | 16 | 64 |

The binary format can validate all listed non-NONE types. The current material
compiler emits BOOL, I32, U32, F32, F32X2/3/4, F32X3X3, and F32X4X4. Enum values
are in
[CypherCommon_CookedShader.h](../src/CypherCommon/Formats/CypherCommon_CookedShader.h#L116-L134),
with active-size, alignment, and occupied-size rules in
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L238-L260)
and
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L1309-L1350).

Logical binding IDs are FNV-1a64 over the ASCII bytes
`cypher.shader.binding.v1:` followed by the binding name. Hash zero is remapped to
one; zero remains the uninitialized sentinel. See
[CypherCommon_CookedShader.cpp](../src/CypherCommon/Formats/CypherCommon_CookedShader.cpp#L1278-L1306).

### 16.7 CYMT V1 binary layout

CYMT V1 contains exactly two chunks in this order:

```text
MTMD  fixed metadata and records
MTST  canonical string table
```

Both use codec NONE, equal stored/decoded sizes, and individual hashes. MTMD is
8-byte aligned; MTST follows immediately at alignment 1. Any prefix padding is
zero and no trailing bytes are permitted. Writer layout and sealing are in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L341-L378)
and
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L820-L931).

#### 16.7.1 V1 MTMD header, 48 bytes

| Offset | Type | Field | Constraint |
| ---: | --- | --- | --- |
| 0 | `u32` | Metadata magic | `CMAT` |
| 4 | `u32` | Metadata version | `1` |
| 8 | `u32` | Header bytes | `48` |
| 12 | `u32` | Flags | Zero |
| 16 | `u32` | Texture count | `0..32` |
| 20 | `u32` | Parameter count | `0..64` |
| 24 | `{u32,u32}` | Shader string offset and byte length | Canonical `.cyshader` |
| 32 | `u32` | String-table bytes | Exact MTST bytes, at most 64 KiB |
| 36 | `u32` | Reserved | Zero |
| 40 | `u32` | Reserved | Zero |
| 44 | `u32` | Reserved | Zero |

The explicit serializer is
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L402-L475).

#### 16.7.2 V1 texture record, 16 bytes each

| Offset | Type | Field |
| ---: | --- | --- |
| 0 | `{u32,u32}` | Binding-name string offset and length |
| 8 | `{u32,u32}` | `.cytex` path string offset and length |

#### 16.7.3 V1 parameter record, 48 bytes each

| Offset | Type | Field | Constraint |
| ---: | --- | --- | --- |
| 0 | `{u32,u32}` | Parameter-name string offset and length | ASCII identifier |
| 8 | `u32` | Parameter type | `BOOL=0`, `SCALAR=1`, `VECTOR=2` |
| 12 | `u32` | Component count | Bool=0, scalar=1, vector=2..4 |
| 16 | `f64` | Value 0 | Bool is exactly 0.0 or 1.0 |
| 24 | `f64` | Value 1 | Active for vectors as applicable |
| 32 | `f64` | Value 2 | Active for vectors as applicable |
| 40 | `f64` | Value 3 | Active for vectors as applicable |

All active numeric values are finite. Inactive slots are zero, and negative zero
is canonicalized to positive zero. Validation is in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L123-L177),
and record serialization is in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L478-L508).

#### 16.7.4 V1 canonical order and compatibility defaults

Texture records are sorted by binding name; parameter records by parameter name.
Duplicates are rejected. MTST traversal is exactly:

1. shader path;
2. for every sorted texture, binding name then texture path;
3. every sorted parameter name.

Each traversal entry is emitted separately in order, with a required terminal
NUL at the next exact offset. References may not alias; gaps, reordering, extra
strings, or a noncanonical record order are rejected.
Sorting and string emission are in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L180-L218)
and
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L511-L547).

When CYMT V1 is read into the shared view, V2-only fields retain their initialized
defaults: domain surface, alpha opaque, cutoff `0.5f`, no surface, zero features,
no interface/variant hashes, no packed constants, and no texture sampler/UV/logical
ID data. The V1 reader is in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L934-L1233),
and view defaults are in
[CypherCommon_CookedMaterial.h](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.h#L170-L234).

### 16.8 CYMT V2 binary layout

Chunk order depends only on whether packed constant data is empty:

```text
No parameters/constants:  MTMD, MTST
Nonempty constants:       MTMD, MTCD, MTST
```

All chunks are codec NONE and independently hashed; the whole CYRS payload area
is also hashed. MTMD is 8-byte aligned, MTCD 16-byte aligned, and MTST alignment
1. Gaps are zero and the file ends exactly at MTST. Layout is built in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L1880-L1939)
and checked by the reader in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L3256-L3299).

#### 16.8.1 V2 MTMD header, 128 bytes

| Offset | Type | Field | Constraint |
| ---: | --- | --- | --- |
| 0 | `u32` | Metadata magic | `CMAT` |
| 4 | `u32` | Metadata version | `2` |
| 8 | `u32` | Header bytes | `128` |
| 12 | `u32` | Material flags | Known V2 bits only |
| 16 | `u32` | Domain | `0..4` |
| 20 | `u32` | Alpha mode | `0..3` |
| 24 | `f64` | Alpha cutoff | Canonical promoted `f32`; finite `[0,1]`; exactly 0.5 unless mask |
| 32 | `u32` | Feature count | `0..32` |
| 36 | `u32` | Texture count | `0..32` |
| 40 | `u32` | Parameter count | `0..64` |
| 44 | `u32` | String-table bytes | Exact MTST size, at most 64 KiB |
| 48 | `u32` | Constant bytes | Exact MTCD size, at most 64 KiB |
| 52 | `u32` | Reserved | Zero |
| 56 | `{u32,u32}` | Shader path string ref | Canonical `.cyshader` |
| 64 | `{u32,u32}` | Optional surface path string ref | Canonical `.cysurface` when present; zero ref otherwise |
| 72 | `u64` | Shader interface hash low | Required valid hash |
| 80 | `u64` | Shader interface hash high | Required valid hash |
| 88 | `u64` | Variant hash low | Derived from canonical features |
| 96 | `u64` | Variant hash high | Derived from canonical features |
| 104 | `u32` | Feature-record byte offset in MTMD | Canonically 128 |
| 108 | `u32` | Texture-record byte offset in MTMD | Immediately after features |
| 112 | `u32` | Parameter-record byte offset in MTMD | Immediately after textures |
| 116 | `u32` | Reserved | Zero |
| 120 | `u64` | Reserved | Zero |

The serializer and derived record offsets are in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L2019-L2083).

#### 16.8.2 V2 feature record, 32 bytes each

| Offset | Type | Field | Constraint |
| ---: | --- | --- | --- |
| 0 | `{u32,u32}` | Feature-name string ref | ASCII identifier |
| 8 | `{u32,u32}` | Enum-value string ref | Present only for enum feature |
| 16 | `u32` | Feature type | BOOL=1 or ENUM=2 |
| 20 | `u32` | Boolean value | 0/1 for BOOL; zero for ENUM |
| 24 | `u64` | Reserved | Zero |

Cooked records contain resolved feature values only. Source inheritance/removal
operations are not persisted.

#### 16.8.3 V2 texture record, 64 bytes each

| Offset | Type | Field | Constraint |
| ---: | --- | --- | --- |
| 0 | `{u32,u32}` | Binding-name string ref | ASCII identifier |
| 8 | `{u32,u32}` | Texture-path string ref | Canonical `.cytex` |
| 16 | `{u32,u32}` | Optional sampler-preset string ref | ASCII identifier in cooked format |
| 24 | `u64` | Logical binding ID | Exact hash of binding name; nonzero |
| 32 | `u32` | UV set | `0..7` |
| 36 | `u32` | Reserved | Zero |
| 40 | `f32` | UV scale X | Finite, canonical |
| 44 | `f32` | UV scale Y | Finite, canonical |
| 48 | `f32` | UV offset X | Finite, canonical |
| 52 | `f32` | UV offset Y | Finite, canonical |
| 56 | `f32` | UV rotation | Finite, canonical |
| 60 | `u32` | Reserved | Zero |

#### 16.8.4 V2 parameter record, 40 bytes each

| Offset | Type | Field | Constraint |
| ---: | --- | --- | --- |
| 0 | `{u32,u32}` | Parameter-name string ref | ASCII identifier |
| 8 | `u64` | Logical binding ID | Exact hash of name; nonzero |
| 16 | `u32` | Shader value type | One valid non-NONE type from Section 16.6.2 |
| 20 | `u32` | Byte offset in MTCD | Exact aligned canonical offset |
| 24 | `u32` | Active value bytes | Exact type value size |
| 28 | `u32` | Occupied storage bytes | Exact type storage size including padding |
| 32 | `u64` | Reserved | Zero |

All three record serializers are contiguous in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L2085-L2138).

#### 16.8.5 MTST and MTCD

V2 string traversal is exactly:

1. shader path;
2. optional surface path;
3. each sorted feature name and, for enum features, enum value;
4. each sorted texture binding, texture path, and optional sampler preset;
5. each sorted parameter name.

All strings are NUL-terminated. The reference offsets and physical writer are in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L1953-L2017)
and
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L2141-L2195).

MTCD is fully zeroed before values are written. Scalars and vectors use explicit
little-endian 32- or 64-bit components. Three-component vectors occupy a 16-byte
slot with zero padding. MAT3 input is column-major and is emitted as three
16-byte columns, each containing three `f32` values and a zero pad. MAT4 occupies
64 contiguous bytes. Constant emission is in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L2197-L2297).

#### 16.8.6 Canonicalization and hashes

Before writing, CYMT V2:

- validates shader/surface/texture paths and all identifiers;
- validates each supplied logical ID against the shared hash function;
- narrows UV and non-F64 floating constants only when the result stays finite;
- canonicalizes negative zero to positive zero;
- forces non-mask cutoff to `0.5f` and stores the canonical `f32` value promoted
  into the header’s `f64` field;
- sorts features and parameters by name and textures by binding;
- rejects duplicate names and duplicate logical IDs across textures/parameters;
- requires the constant offsets to be the exact contiguous layout obtained by
  aligning sorted parameters, with no arbitrary holes or overlap;
- requires a valid shader interface hash;
- derives a variant hash from feature count plus every sorted feature name, type,
  Boolean value, and enum bytes, or verifies an explicitly supplied hash.

The canonicalizer is
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L1504-L1877),
and canonical feature hashing is in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L1449-L1501).
The deterministic-order/negative-zero test is
[CypherCommon_CookedMaterial_Tests.cpp](../tests/CypherCommon/Formats/CypherCommon_CookedMaterial_Tests.cpp#L606-L647).

### 16.9 Reader, resource loader, renderer, and editor behavior

`CookedMaterial_Read` first validates the common CYRS layout and dispatches by
resource version. Both V1 and V2 readers require the whole-content hash, codec-NONE
type-specific chunks, correct individual hashes, exact records, canonical strings,
sorted unique names, zero padding, exact chunk placement, and exact final file
size. The public dispatch is in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L3512-L3552).

V2 additionally verifies interface and variant hashes, all logical IDs, packed
constant type/size/alignment, exact parameter layout, zero padding inside values,
and canonical floating representation. It exposes allocation-free binary-search
lookup by sorted name and lookup by logical binding ID. Lookup implementations
begin at
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L3555).

The resource subsystem accepts `.cymat_c`, reads and owns the complete file under
a default 1 MiB cap, calls `CookedMaterial_Read`, and exposes borrowed views for
the handle lifetime. Texture paths and logical IDs remain metadata; the loader
does not load dependency textures or construct GPU bindings. Limits/lifetime are
in
[CypherResource_RenderAssets.h](../src/CypherResource/CypherResource_RenderAssets.h#L34-L39)
and
[CypherResource_RenderAssets.h](../src/CypherResource/CypherResource_RenderAssets.h#L154-L170),
and material loading is in
[CypherResource_RenderAssets.cpp](../src/CypherResource/CypherResource_RenderAssets.cpp#L270-L327).

No general renderer material object currently consumes CYMT V2 interface hashes,
logical IDs, variant hashes, packed constants, texture dependencies, UV transforms,
or render state. Draw and preview code bind narrow known data explicitly.

The Tile Editor’s shared preview adapter does use `CookedMaterial_Read`, so it can
open either resource version, but its material contract is deliberately narrow:

- shader must be `shaders/tile_surface.cyshader`;
- exactly one texture binding named `base_color` must exist;
- only parameters `tint` and `uv_scale` are accepted;
- those parameters must use the legacy V1 `VECTOR`, `nComponents`, and `values`
  fields;
- the referenced texture must satisfy the RGBA8/color restrictions in Section
  15.10.

CYMT V2 decodes parameters into `shaderType`, packed-data ranges, and typed value
arrays rather than populating the V1 legacy vector discriminator. A V2 material
with parameters therefore does not currently bridge into this preview subset.
The assumptions are in
[CypherTileMaterialPreview.cpp](../src/CypherTools/CypherTileEditor/Core/CypherTileMaterialPreview.cpp#L51-L134),
and V2 parameter decode is in
[CypherCommon_CookedMaterial.cpp](../src/CypherCommon/Formats/CypherCommon_CookedMaterial.cpp#L3095-L3151).

Picasso’s source material importer explicitly calls `RenderMaterialSource_Decode`,
the V1 decoder. It maps recognized texture semantics and deliberately skips all
shader parameters. It does not import V2 inheritance, state, features, UV blocks,
or typed parameters. See
[PicassoMaterialImport.cpp](../src/CypherTools/Picasso/Core/PicassoMaterialImport.cpp#L140-L217).

The remaining material integration work is therefore:

- a shader variant-table contract and feature selection;
- `.cysurface` compiler/runtime support;
- an independent sampler-association format revision if separate samplers are
  required;
- a dependency-owning runtime GPU material that checks interface hashes, resolves
  logical IDs, uploads MTCD, binds textures/samplers, and applies state;
- CYMT chunk decompression if compressed storage is adopted;
- a typed CYMT V2 bridge for Tile Editor preview;
- Picasso V2 material import/authoring support;
- reconciliation of source stable identifiers with cooked ASCII identifiers for
  sampler presets and feature enum values.

### 16.10 Conformance tests and companion reference

The primary executable specifications are:

- [source-schema, defaults, inheritance-operation, and decoder tests](../tests/CypherCommon/Formats/CypherCommon_RenderAsset_Tests.cpp);
- [CYMT V1/V2 writer/reader, mutation, layout, packing, hash, compatibility, and deterministic-byte tests](../tests/CypherCommon/Formats/CypherCommon_CookedMaterial_Tests.cpp);
- [material compiler dependency, inheritance, binding, parameter, determinism, and identity tests](../tests/CypherTools/CypherMaterialCompiler/CypherMaterialCompiler_Tests.cpp);
- [resource-loader ownership and validation tests](../tests/CypherResource/CypherResource_RenderAssets_Tests.cpp);
- [Picasso V1 material-import tests](../tests/CypherTools/Picasso/PicassoAuthoringCore_Tests.cpp);
- [Tile Editor material-preview tests](../tests/CypherTools/CypherTileEditor/CypherTileMaterialPreview_Tests.cpp).

Additional cross-format rationale is in
[Render Assets](formats/RENDER_ASSETS.md). Source and tests remain authoritative
if this living manual and implementation diverge.

## 17. Map Format

| Map property | Value |
| --- | --- |
| Extension | `.cymap` |
| CYKV language | 1 |
| Schema ID | `cypher.map` |
| Current writer | Schema V3 |
| Current reader | Schema V1, V2, and V3 |
| Cooked map | Not implemented |
| Producer/consumer | Qt Tile Editor and source-preview path |
| Object policy | Closed; unknown fields reject the document |

A `.cymap` is a sparse, editor-facing tile-map document. It stores active floor
cells rather than the dense in-memory grid. Empty grid positions are reconstructed
during loading. The format is suitable for the current blockout editor and preview
path; it is not yet the production World, streaming, collision, lighting, or
entity format.

### 17.1 Version history

| Schema | Reader | Writer | Additions |
| --- | --- | --- | --- |
| V1 | Supported | No | Base root, flat cells, player-spawn and door markers |
| V2 | Supported | No | Optional cell `shape` and `stair_steps` |
| V3 | Supported | Current | Optional material-slot-to-`.cymat` bindings |
| Unknown | Rejected | — | No inference from fields or file contents |

Loading V1 or V2 and saving upgrades the document to V3. The loader dispatches by
the exact `@schema` version and keeps the older closed field sets; a V1 document
cannot smuggle in V2 stair fields, and V1/V2 cannot contain V3 `materials`.

### 17.2 Root fields

Every file starts with:

```cykv
@cykv 1
@schema "cypher.map" <1|2|3>
```

| Field | Type | Req. | Default | Versions | Values/constraints | Meaning |
| --- | --- | --- | --- | --- | --- | --- |
| `map_id` | string | Yes | — | V1–V3 | Nonzero 36-character 8-4-4-4-12 hexadecimal UUID text; upper/lower case accepted, writer emits lower case | Stable authored map identity |
| `dimensions` | object | Yes | — | V1–V3 | Exactly `width` and `height` | Dense grid extent |
| `metrics` | object | Yes | — | V1–V3 | Exactly `cell_size` and `level_height` | Grid-to-world conversion |
| `cells` | array of cell objects | Yes | — | V1–V3 | At most 262,144 unique coordinates | Sparse active floor cells |
| `markers` | array of marker objects | Yes | — | V1–V3 | At most 4,096; IDs unique | Gameplay/editor markers |
| `materials` | array of binding objects | No | Empty | V3 only | At most 256; slots unique | Stable slot to material resource mapping |

Root objects are closed. A missing required field, unknown field, wrong CYKV type,
or duplicate object member rejects the document.

### 17.3 `dimensions` fields

| Field | Type | Req. | Range | Meaning |
| --- | --- | --- | --- | --- |
| `width` | `u64` | Yes | 1–1,024 | Number of grid columns |
| `height` | `u64` | Yes | 1–1,024 | Number of grid rows |

The product is at most 1,048,576 dense in-memory cells. Only active cells enter
the source array, which has the lower 262,144-record limit.

### 17.4 `metrics` fields

| Field | Type | Req. | Range/unit | Meaning |
| --- | --- | --- | --- | --- |
| `cell_size` | finite `f64` representable as `f32` | Yes | At least 0.20 world units | Width/depth of one grid cell |
| `level_height` | finite `f64` representable as `f32` | Yes | At least 0.25 world units | Vertical distance represented by one level |

Neither field has an authored default. New editor documents use 2.0 and 3.0,
respectively, but a source file must serialize both values.

### 17.5 Cell fields

Each `cells[]` element is a closed object.

| Field | Type | Req. | Default | Versions | Values/constraints | Meaning |
| --- | --- | --- | --- | --- | --- | --- |
| `x` | `u64` | Yes | — | V1–V3 | `0 <= x < width` | Grid column |
| `y` | `u64` | Yes | — | V1–V3 | `0 <= y < height` | Grid row |
| `floor_level` | `i64` | Yes | — | V1–V3 | Signed 16-bit range; stairs cannot use 32,767 | Floor elevation in level units |
| `wall_height_levels` | `u64` | Yes | — | V1–V3 | 1–65,535 | Wall height in level units |
| `material_slot` | `u64` | Yes | — | V1–V3 | 0–65,535 | Stable material slot |
| `flags` | `u64` bitmask | Yes | — | V1–V3 | Exactly 1 (`FLOOR`) | Declares an active floor cell |
| `shape` | string enum | No | `flat` | V2–V3 | See table below | Flat surface or cardinal stair |
| `stair_steps` | `u64` | No | 8 | V2–V3 | 2–32 | Treads used when generating stair geometry |

Cell shape values:

| Value | Meaning |
| --- | --- |
| `flat` | Constant-height floor |
| `stairs_north` | Rises one level toward negative grid Y |
| `stairs_east` | Rises one level toward positive grid X |
| `stairs_south` | Rises one level toward positive grid Y |
| `stairs_west` | Rises one level toward negative grid X |

Coordinates must be unique. The loader rejects a second record for the same
`(x,y)` pair. A sparse record always represents a floor; empty cells are omitted
rather than serialized with `flags = 0`.

### 17.6 Marker fields

All marker IDs share one uniqueness domain. Marker coordinates must lie inside the
declared dimensions.

Common fields:

| Field | Type | Req. | Values/constraints | Meaning |
| --- | --- | --- | --- | --- |
| `id` | string | Yes | Nonzero 36-character 8-4-4-4-12 hexadecimal UUID text; upper/lower case accepted, writer emits lower case; unique in `markers` | Stable marker identity |
| `kind` | string enum | Yes | `player_spawn` or `door` | Selects the exact closed record shape |
| `x` | `u64` | Yes | `0 <= x < width` | Marker grid column |
| `y` | `u64` | Yes | `0 <= y < height` | Marker grid row |

`player_spawn` adds exactly:

| Field | Type | Req. | Values/constraints | Meaning |
| --- | --- | --- | --- | --- |
| `yaw_degrees` | finite `f64` representable as `f32` | Yes | No normalization is performed | Initial facing yaw in degrees |

`door` adds exactly:

| Field | Type | Req. | Values/constraints | Meaning |
| --- | --- | --- | --- | --- |
| `side` | string enum | Yes | `north`, `east`, `south`, or `west` | Cell boundary occupied by the door |

A spawn cannot contain `side` and a door cannot contain `yaw_degrees`. Structural
loading permits zero or more player spawns so an incomplete editor document can
be saved. The separate gameplay validator requires exactly one usable spawn.

### 17.7 V3 material binding fields

Each `materials[]` element is a closed object:

| Field | Type | Req. | Values/constraints | Meaning |
| --- | --- | --- | --- | --- |
| `slot` | `u64` | Yes | 0–65,535; unique in the array | Value referenced by `cells[].material_slot` |
| `path` | string | Yes | 1–255 bytes; canonical lowercase relative virtual path ending in `.cymat` | Material source resource |

Canonical paths use `/`, contain no empty, `.`, or `..` segments, contain no
uppercase ASCII or backslashes, and are not absolute. They also reject all
whitespace, control bytes, DEL, non-ASCII bytes, and `: * ? " < > |`. A cell may
still use an unbound slot; the editor falls back to its
blockout/unknown-material presentation.

### 17.8 Complete V3 example

```cykv
@cykv 1
@schema "cypher.map" 3

{
  "map_id" = "90c1f58b-2aa5-4026-b5c0-8ac364a61655"

  "dimensions" = {
    "width" = 8u
    "height" = 6u
  }

  "metrics" = {
    "cell_size" = 2.0
    "level_height" = 3.0
  }

  "materials" = [
    {
      "slot" = 1u
      "path" = "materials/dev/grid.cymat"
    }
  ]

  "cells" = [
    {
      "x" = 2u
      "y" = 2u
      "floor_level" = 0
      "wall_height_levels" = 1u
      "material_slot" = 1u
      "flags" = 1u
    },
    {
      "x" = 3u
      "y" = 2u
      "floor_level" = 0
      "wall_height_levels" = 1u
      "material_slot" = 1u
      "flags" = 1u
      "shape" = "stairs_east"
      "stair_steps" = 8u
    }
  ]

  "markers" = [
    {
      "id" = "c679dd4a-35a2-4fe6-82fd-6536b89fcc18"
      "kind" = "player_spawn"
      "x" = 2u
      "y" = 2u
      "yaw_degrees" = 90.0
    },
    {
      "id" = "17f7645d-7dda-4f32-bcd2-f7f2d477cb57"
      "kind" = "door"
      "x" = 2u
      "y" = 2u
      "side" = "north"
    }
  ]
}
```

### 17.9 Limits

| Resource | Limit |
| --- | ---: |
| Source text | 64 MiB |
| Width | 1,024 |
| Height | 1,024 |
| Dense cells | 1,048,576 |
| Serialized active cells | 262,144 |
| Markers | 4,096 |
| Material bindings | 256 |
| Material path | 255 bytes |
| Stair steps | 2–32 |
| Serialization diagnostic field path | 95 bytes plus terminator |

General CYKV parser limits also apply.

### 17.10 Canonical persistence

The current writer always emits schema V3 with:

- two-space indentation and a final LF;
- root members in writer-defined order;
- active cells in row-major Y-then-X order;
- markers sorted by stable UUID;
- material bindings sorted by numeric slot;
- no records for canonical empty cells;
- omitted `materials` when there are no bindings;
- omitted `shape` and `stair_steps` only when shape is `flat` and steps are the
  default eight.

The writer validates the complete source document before replacing the output
text buffer. Loading also constructs temporary vectors and publishes the document
only after every field has passed.

### 17.11 Structural and gameplay validation

Structural serialization reports stable status classes for invalid arguments,
nonfresh destinations, uninitialized or invalid documents, limits, allocation,
CYKV parse/write failure, header mismatch, wrong root/type, missing/unknown fields,
range failures, invalid IDs, duplicate cells or marker IDs, unsupported markers,
invalid material paths, and duplicate material slots.

The separate gameplay validator checks:

- dimensions and metrics;
- cell storage and canonical empty values;
- active-cell limit and cell property/shape/step validity;
- marker-ID validity and uniqueness plus supported marker kinds;
- one player spawn;
- spawn bounds, supporting floor, and no spawn on stairs;
- door bounds, cardinal side, supporting floor, boundary placement, no door on
  stairs, and no duplicate door edge;
- material-binding count, validity, and slot uniqueness.

This split permits saving structurally valid work in progress while still showing
playability problems.

### 17.12 Current consumers and missing cooked format

Implemented:

- Qt-independent mutable document;
- bounded undo/redo;
- deterministic CYKV persistence;
- floors, exposed walls, cliffs, doors, and stair geometry;
- editor and standalone source preview;
- V3 material bindings and bounded preview dependency loading.

Missing:

- `.cymap_c` identity and binary layout;
- shared map compiler;
- production World loader;
- partition/streaming cells;
- collision, visibility, navigation, and lighting sections;
- general entities/components, script bindings, arbitrary brush/mesh placement;
- runtime migration policy beyond the source reader.

No binary fields for `.cymap_c` are listed because no such contract exists. The
name remains planned rather than implying an implementation.


## 18. Input Actions and User Bindings

**Status:** Proposal. Low-level keyboard, text, mouse-button, mouse-motion, and
mouse-wheel System events exist. The action runtime, source schemas, compiler,
`CYIN` reader/writer, gamepad event path, and user-binding store do not.

Recommended family:

| Layer | Identity | Lifecycle |
| --- | --- | --- |
| Project source | `.cyinput` / `cypher.input` V1 | Source-controlled developer data |
| Cooked runtime | `.cyinput_c` / `CYIN` V1 in CYRS V1 | Read-only packaged resource |
| User overrides | `.cybindings` / `cypher.input_bindings` V1 | Writable profile data; never cooked or packaged |

The main format is not named `.cykeymap` because it covers keyboard, mouse,
wheel, gamepad buttons and axes, chords, composites, contexts, processors,
accessibility, and user rebinding. A smaller `cypher.tool_keymap` may eventually
describe application/editor shortcuts.

All tables in this chapter are candidate V1 contracts. They do not become
accepted syntax until the first action runtime, compiler, and tests freeze them.

### 18.1 Ownership and data flow

```text
CypherSystem physical events and separate text/IME events
    -> CypherInput device state
    -> active contexts and resolved bindings
    -> named typed action frame
    -> gameplay, camera, UI, replay/network command builder

.cyinput -> CypherInputCompiler -> .cyinput_c / CYIN
.cybindings ----------------------^ sparse runtime overlay
```

Physical controls feed actions. UTF-8 text and IME composition remain a separate
UI stream. Runtime state such as held buttons and active context stacks is never
serialized as authored input data.

### 18.2 Candidate `.cyinput` root fields

Header:

```cykv
@cykv 1
@schema "cypher.input" 1
```

| Field | Type | Req. | Default | Values/constraints | Meaning |
| --- | --- | --- | --- | --- | --- |
| `map_id` | string identifier | Yes | — | Stable canonical project identity | Names the action-map contract |
| `actions` | object map | Yes | — | 1–512 unique dotted action names | Declares semantic input intents |
| `contexts` | object map | Yes | — | 1–64 unique dotted context names | Declares routing/conflict domains |
| `schemes` | object map | Yes | — | 1–16 unique scheme names | Declares compatible default bindings |

Source names are canonical lowercase dotted identifiers. Cooked tables may store
stable hashes for lookup, but tools retain complete names and reject collisions.

### 18.3 Candidate action fields

| Field | Type | Req. | Default | Values/constraints | Meaning |
| --- | --- | --- | --- | --- | --- |
| `type` | string enum | Yes | — | `digital`, `axis1d`, `axis2d` | Shape of the published action value |
| `display` | string | Yes | — | Localization key, at most 128 bytes | Controls-menu label reference |
| `rebindable` | Boolean | No | `true` | — | User may replace/add/remove bindings |
| `simulation` | Boolean | No | `false` | — | Included in deterministic action-frame sampling |
| `ui` | Boolean | No | `false` | — | Participates in UI navigation |
| `combine` | string enum | No | Type-defined | `latest`, `maximum_magnitude`, `sum_clamped`, `vector_normalized` | Combines simultaneous bindings |
| `clamp` | scalar or two-value array | No | Type-defined legal range | Finite; dimension matches action type | Final action-value range |

`digital` actions use inactive/active state and transitions. `axis1d` publishes
one signed scalar. `axis2d` publishes two signed components.

### 18.4 Candidate context fields

| Field | Type | Req. | Default | Values/constraints | Meaning |
| --- | --- | --- | --- | --- | --- |
| `priority` | integer | Yes | — | Stable bounded signed priority | Higher-priority active context resolves first |
| `consume` | Boolean | No | `true` | — | Handled controls stop at this context |
| `actions` | array of strings | Yes | — | Unique references to declared actions | Actions legal in the context |
| `exclusive_with` | array of strings | No | Empty | Declared contexts; symmetric validation required | Context combinations that cannot coexist |
| `parent` | string | No | None | Existing context; acyclic if admitted | Candidate policy reuse, not activation |

Runtime code decides which contexts are active. The file cannot execute logic or
read CVars to activate itself. V1 may omit `parent` if real use does not justify
context inheritance.

### 18.5 Candidate scheme and binding fields

Scheme fields:

| Field | Type | Req. | Default | Values/constraints | Meaning |
| --- | --- | --- | --- | --- | --- |
| `device_classes` | array of strings | Yes | — | Unique supported classes such as `keyboard`, `mouse`, `gamepad` | Devices compatible with the scheme |
| `bindings` | array of objects | Yes | — | At most 2,048; stable IDs unique | Default control-to-action mappings |

Binding fields:

| Field | Type | Req. | Default | Values/constraints | Meaning |
| --- | --- | --- | --- | --- | --- |
| `id` | string identifier | Yes | — | Stable, at most 64 bytes, unique in map | User-override and diagnostic identity |
| `action` | string | Yes | — | Declared action | Target semantic intent |
| `context` | string | Yes | — | Declared context containing the action | Routing/conflict domain |
| `control` | string | One of control/controls | — | Canonical token, at most 96 bytes | Single physical/logical control |
| `controls` | array of strings | One of control/controls | — | 1–4 compatible controls | Multi-axis/composite source |
| `trigger` | string enum or object | No | `down` | `down`, `press`, `release`, `repeat`, `threshold`, `hold`, `tap` | Transition/interaction rule |
| `chord` | array of strings | No | Empty | At most four controls | Modifiers that must be active |
| `composite` | object | No | None | Axis and finite signed contribution | Maps a scalar control into an axis component |
| `processors` | array of objects | No | Empty | Ordered, at most eight | Dead zone, curve, scale, inversion, clamp |
| `consume` | Boolean | No | Context policy | — | Binding-specific consumption override |
| `pass_through` | Boolean | No | `false` | Cannot contradict consumption policy | Allows lower-priority routing |
| `priority` | integer | No | 0 | Stable bounded value | Explicit resolution before stable-ID tie break |

Candidate composite fields are `axis = "x"|"y"` and a finite `scale`. Candidate
processor operations are scalar/vector scale, inversion, clamp, axial dead zone,
radial dead zone, normalized response curve, sensitivity, and composite
contribution. Processor order is semantic.

Trigger-specific threshold, hysteresis, hold duration, tap window, repeat delay,
and repeat rate must be finite, range-bounded fields in the final schema. Their
exact spellings and units remain open until runtime behavior is measured.

### 18.6 Canonical controls

Source files serialize names, never engine enum integers or native codes.

Examples:

```text
keyboard.w
keyboard.space
keyboard.left_shift
mouse.left
mouse.delta_x
mouse.delta_y
mouse.wheel_y
gamepad.south
gamepad.left_stick_x
gamepad.left_stick_y
gamepad.right_trigger
```

Rules:

- lowercase ASCII dotted namespace;
- stable across platforms;
- no localized display label or glyph path in identity;
- no SDL event number, native scancode, Qt enum, or device instance ID;
- aliases are tool input only and are rewritten to canonical names;
- characters such as `text.a` are invalid because text input is not a control.

### 18.7 Complete proposed source example

```cykv
@cykv 1
@schema "cypher.input" 1

{
  map_id = "reap.default"

  actions = {
    player.move = {
      type = "axis2d"
      display = "input.action.player_move"
      rebindable = true
      simulation = true
      combine = "sum_clamped"
    }
    player.jump = {
      type = "digital"
      display = "input.action.player_jump"
      rebindable = true
      simulation = true
    }
  }

  contexts = {
    gameplay = {
      priority = 100
      consume = true
      actions = ["player.move", "player.jump"]
    }
  }

  schemes = {
    keyboard_mouse = {
      device_classes = ["keyboard", "mouse"]
      bindings = [
        {
          id = "move.forward"
          action = "player.move"
          context = "gameplay"
          control = "keyboard.w"
          composite = { axis = "y" scale = 1.0 }
          trigger = "down"
        },
        {
          id = "jump.space"
          action = "player.jump"
          context = "gameplay"
          control = "keyboard.space"
          trigger = "press"
        }
      ]
    }
  }
}
```

### 18.8 Proposed `CYIN` V1 cooked resource

| Property | Candidate value |
| --- | --- |
| Extension | `.cyinput_c` |
| CYRS resource FourCC | `CYIN` |
| Resource version | 1 |
| Container | CYRS V1 |

Candidate chunks:

| Chunk | Alignment | Contents |
| --- | ---: | --- |
| `INMD` | 8 | Header and fixed action/context/scheme/binding records |
| `INST` | 1 | Canonical names, controls, and localization-key strings |
| `INPR` | 4 | Variable trigger, chord, composite, and processor records |

The exact record widths are intentionally unfrozen because no runtime access
pattern has been measured. The cooked contract must eventually persist:

- source/dependency and action-contract hashes;
- stable action/context/scheme/binding IDs and full collision-check names;
- action types and flags;
- context priority, consumption, exclusivity, and memberships;
- device-class compatibility;
- canonical controls;
- trigger, chord, composite, and ordered processor records;
- conflict metadata required by runtime resolution;
- localization keys or stable references.

It must not contain user overrides, transient devices, native handles/codes,
active contexts, held state, text composition, rumble commands, or network packet
layout.

### 18.9 Candidate `.cybindings` fields

Header:

```cykv
@cykv 1
@schema "cypher.input_bindings" 1
```

Root fields:

| Field | Type | Req. | Default | Meaning |
| --- | --- | --- | --- | --- |
| `input_map` | string | Yes | — | `.cyinput` map identity |
| `base_contract_hash` | 128-bit binary | Yes | — | Defaults generation against which overrides were authored |
| `profile` | string identifier | Yes | — | Writable user profile identity |
| `local_player` | unsigned integer | No | 0 | Optional local-player slot |
| `scheme` | string | No | Project/platform default | Selected default device scheme |
| `overrides` | array | No | Empty | Sparse replacements, additions, and tombstones |
| `calibration` | object | No | Empty | Per-device-class calibration |
| `preferred_glyphs` | string | No | Automatic | UI glyph-family preference |

Override record candidates:

| Field | Type | Req. | Meaning |
| --- | --- | --- | --- |
| `binding` | string | Yes for replacement/removal | Stable shipped binding identity |
| `action` | string | Required for added binding | Stable target action |
| `control` / `controls` | string/array | Required unless unbound | Replacement physical control |
| `unbound` | Boolean | No | Explicit tombstone; defaults false |
| `trigger` | string/object | No | User-compatible trigger override |
| `processors` | array | No | Sensitivity, inversion, dead-zone, or curve overrides |
| `device_profile` | string | No | Stable mapping GUID/capability profile, never instance ID |

Unknown or removed targets remain bounded orphan records for repair and downgrade.
They are ignored during evaluation rather than silently deleted. User files are
self-contained, reject includes/conditionals/macros, and use atomic replacement.

### 18.10 Merge and conflict rules

Merge order:

```text
compiled project defaults
  -> target/platform defaults
  -> device-class scheme
  -> accessibility preset
  -> user replacements/additions/unbind tombstones
  -> session-only device assignment
```

Conflicts are context-aware. Exact consuming bindings in contexts that may coexist
are errors unless sharing is explicit. Reuse across mutually exclusive contexts
is valid. Chord-prefix and overlapping analog thresholds require an explicit
policy. Runtime resolution is deterministic by context priority, explicit binding
priority, chord specificity, and stable binding identity; the final tie breaker
must not hide an authored conflict that should have failed compilation.

### 18.11 Proposed limits

| Resource | Maximum |
| --- | ---: |
| Actions | 512 |
| Contexts | 64 |
| Schemes | 16 |
| Bindings per scheme | 2,048 |
| Controls per chord/composite | 4 |
| Processors per binding | 8 |
| Triggers per binding | 4 |
| Identifier | 64 bytes |
| Control token | 96 bytes |
| Localization key | 128 bytes |
| Source document | 4 MiB |
| Cooked resource | 8 MiB |
| User override records | 2,048 |

These are design starting points, not implemented constants. The first REAP and
Tile Editor action sets must measure actual use before they are frozen.

### 18.12 Runtime requirements and current absence

The eventual runtime must clear or synthesize releases on focus loss, context
replacement, queue overflow, and device removal; clear relative motion; cancel
hold/tap/repeat/chord state; and publish a deterministic action-frame boundary.

No source or cooked field described in this chapter is currently accepted by a
parser/compiler. The complete proposal, implementation order, compiler checks,
accessibility rules, and acceptance criteria are in
[formats/INPUT_ACTIONS.md](formats/INPUT_ACTIONS.md).


## 19. Package Archive

| Archive property | Value |
| --- | --- |
| Extension | `.cypak` |
| Format name/label | Cypher Package Archive / `CYPACKAGE10` |
| Version | 10 |
| Byte order | Little-endian |
| Header | 136 bytes |
| File-entry record | 64 bytes |
| Default payload alignment | 16 bytes |
| Maximum stored path | 259 bytes plus terminator |
| Status | Reader, writer, and FileSystem mount implemented for uncompressed entries |

A package is a distribution container, not a CYRS resource. It maps canonical
virtual paths to stored byte ranges so FileSystem can serve packaged content using
the same lookup names as loose files.

### 19.1 Physical layout

```text
136-byte header
N × 64-byte file-entry records
packed NUL-terminated path string table
padding to writer-selected data alignment
stored payloads, each placed at that alignment
```

The writer computes the complete layout before opening its sibling staging file,
writes fixed-width little-endian records and zero alignment padding, flushes, and
atomically replaces the final archive.

### 19.2 V10 header fields

Offsets are relative to archive byte zero.

| Offset | Member | Type | Req. | Values/constraints | Meaning |
| ---: | --- | --- | --- | --- | --- |
| 0 | `magic[16]` | bytes | Yes | `CYPACKAGE` plus seven NUL bytes | Archive signature |
| 16 | `version` | `u32` | Yes | Exactly 10 | On-disk format generation |
| 20 | `nHeaderSize` | `u32` bytes | Yes | Exactly 136 | Fixed-header size |
| 24 | `endianTag` | `u32` | Yes | `0x12345678` | Detects wrong byte order/corruption |
| 28 | `flags` | `u32` bitmask | Yes | Supported bits described below | Archive feature declarations |
| 32 | `nArchiveSize` | `u64` bytes | Yes | At least the end of the declared data region | Complete declared archive length |
| 40 | `nFileCount` | `u64` | Yes | Narrowable to `u32` | Number of 64-byte index records |
| 48 | `nIndexOffset` | `u64` | Yes | At or after header; writer uses 136 | Absolute entry-table offset |
| 56 | `nIndexSize` | `u64` bytes | Yes | Exactly `nFileCount * 64` | Entry-table byte extent |
| 64 | `nStringTableOffset` | `u64` | Yes | At or after index end | Absolute path-table offset |
| 72 | `nStringTableSize` | `u64` bytes | Yes | Contains all declared paths and terminators | Path-table extent |
| 80 | `nDataOffset` | `u64` | Yes | At or after string-table end | Start of payload region |
| 88 | `nDataSize` | `u64` bytes | Yes | Range contained by `nArchiveSize` | Complete payload-region extent |
| 96 | `archiveHash` | `u64` | Conditional | Intended when archive-hash flag is set | Whole-archive integrity field |
| 104 | `reserved[4]` | four `u64` | Reserved | Writer emits zero; reader currently does not verify zero | Space for compatible extension |

Header flags:

| Bit | Name | Current behavior |
| ---: | --- | --- |
| — | `NONE = 0` | No optional archive feature is declared |
| 0 | `INDEX_SORTED` | Entries are strictly increasing by canonical path; enables binary search |
| 1 | `HAS_FILE_HASHES` | Writer marks entries with unpacked-content hashes |
| 2 | `HAS_ARCHIVE_HASH` | Reader recognizes the declaration; verification returns `ERR_NOT_IMPLEMENTED` |
| 3 | `COMPRESSED_INDEX` | Reserved in the enum but rejected by the V10 reader |

The reader validates ordered nonoverlapping header sections with overflow-checked
addition. `Pak_ValidateHeader` permits `nArchiveSize` to be larger than the end of
the declared data region. `Pak_OpenReader` separately requires `nArchiveSize` to
equal the physical file size, but a physically present trailing region outside
`nDataSize` can therefore remain unreferenced.

### 19.3 File-entry fields

Offsets are relative to one 64-byte index record.

| Offset | Member | Type | Req. | Values/constraints | Meaning |
| ---: | --- | --- | --- | --- | --- |
| 0 | `nPathOffset` | `u64` | Yes | References a contained string-table range | Start of canonical path |
| 8 | `nDataOffset` | `u64` | Yes | Stored range must lie inside data section | Absolute payload start |
| 16 | `nStoredSize` | `u64` bytes | Yes | Contained by data region | Physical payload bytes |
| 24 | `nUnpackedSize` | `u64` bytes | Yes | Equals stored size in current V10 support | Logical bytes after decompression |
| 32 | `modifiedTimeUtc` | `u64` | Yes | Source timestamp in current writer convention | Preserved source modification time |
| 40 | `contentHash` | `u64` | Physically present; semantically conditional | Current writer always serializes FNV-1a 64; valid and verified only when `HAS_HASH` is set | Integrity/cache identity |
| 48 | `nPathSize` | `u32` bytes | Yes | 1–259, excluding terminator | Exact path length |
| 52 | `szPathHash` | `u32` | Yes | FNV-1a of complete canonical path | Lookup accelerator; never replaces equality |
| 56 | `compression` | `u32` enum | Yes | Currently only `NONE=0` | Stored codec |
| 60 | `flags` | `u32` bitmask | Yes | `COMPRESSED` and/or `HAS_HASH` only | Entry feature declaration |

Compression enum values are `NONE=0`, `LZ4=1`, and `ZSTD=2`. The identifiers are
reserved, but writer and reader currently accept only `NONE`. An uncompressed
entry must have `COMPRESSED` clear and equal stored/unpacked sizes.

Entry flag values are:

| Bit | Name | Meaning |
| ---: | --- | --- |
| — | `NONE = 0` | Stored verbatim and no content hash is declared |
| 0 | `COMPRESSED` | Stored bytes require the declared decompressor before use |
| 1 | `HAS_HASH` | `contentHash` contains the FNV-1a 64 hash of unpacked bytes |

The reader validates every path reference, terminator, embedded-NUL absence,
canonical spelling, path hash, entry flags, compression consistency, and payload
containment before exposing archive lookup.

### 19.4 Canonical path contract

Writer input is normalized and serialized as:

- nonempty relative ASCII;
- lowercase;
- `/` separators;
- no leading slash or drive prefix;
- no `.` or `..` component;
- no empty serialized component;
- no bytes below 32, DEL, or non-ASCII bytes;
- none of `: " < > | space * ?`;
- at most 259 bytes.

Writer input may contain uppercase, backslashes, or repeated separators; they are
lowercased/collapsed. The on-disk reader requires the already-normalized spelling,
so a package cannot serialize an alias. Full string equality always follows a
path-hash match.

Duplicate canonical paths are always rejected, including when the writer's
`FAIL_ON_DUPLICATE` option is clear. Sorted indexes must be strictly increasing.
Unsorted archives receive a temporary sorting pass during validation solely to
detect duplicates.

### 19.5 Writer inputs and options

One source entry supplies:

| Input | Type | Meaning |
| --- | --- | --- |
| `szVirtualPath` | string | Package lookup identity to normalize |
| `szPhysicalPath` | host path | File read during archive construction |
| `compression` | enum | Per-file codec; currently must be `NONE` |
| `flags` | bitmask | Caller may request `HAS_HASH` only |

Writer configuration supplies the output path, flags, default compression, and
payload alignment. Current default flags request deterministic output, sorted
index, file hashes, and duplicate rejection.

Actual V10 behavior:

- `SORT_INDEX` controls ordering and the header sorted flag.
- `WRITE_HASHES` sets entry hash flags and persists FNV-1a payload hashes.
- duplicate rejection is unconditional.
- `DETERMINISTIC` currently has no behavioral branch.
- `defaultCompression` is validated but is not applied to entries.
- source modification timestamps are always serialized.
- arbitrary alignment is used by the writer without a persisted alignment field
  or a power-of-two validation rule; the default is 16.

### 19.6 Annotated archive example

This is a logical dump, not source text:

```text
header
  version            = 10
  flags              = INDEX_SORTED | HAS_FILE_HASHES
  nFileCount         = 2
  index               = [136, 264)
  string table        = [264, 317)
  data                = [320, archive end)

entry[0]
  path                = "materials/dev/grid.cymat_c"
  compression         = NONE
  flags               = HAS_HASH
  stored == unpacked

entry[1]
  path                = "textures/dev/grid.cytex_c"
  compression         = NONE
  flags               = HAS_HASH
  stored == unpacked
```

### 19.7 Lookup, reads, and verification

A sorted archive uses binary search by complete canonical path. An unsorted
archive uses the 32-bit path hash as an accelerator and verifies complete text to
avoid collision aliases.

`Pak_ReadFile` returns logical bytes; `Pak_ReadRawFileByIndex` exposes stored
bytes. They are equivalent for current uncompressed entries.

Verification flags cover:

- header;
- index;
- individual file hashes;
- whole-archive hash.

File-hash verification is implemented for raw/uncompressed payloads. Whole-archive
verification is explicitly unimplemented when a present archive hash is
requested.

### 19.8 Limits, trust, and V10 gaps

The fixed format limit is a 259-byte path. Several essential practical limits are
not yet part of V10:

- maximum archive bytes;
- maximum file count below the theoretical `u32` ceiling;
- maximum string-table allocation;
- maximum individual or aggregate unpacked bytes;
- maximum staging-memory use.

Additional current gaps:

- timestamps prevent byte-for-byte reproducibility when source modification time
  changes despite the deterministic writer flag;
- LZ4/Zstandard payload and compressed-index support are absent;
- archive hashing is not emitted or verified;
- the public memory-map open flag returns `ERR_NOT_IMPLEMENTED`; the reader
  currently allocates/loads the index and string table and reads payloads through
  standard file I/O;
- FNV hashes are noncryptographic and do not authenticate content;
- header/entry hash flags and zero-value fields need stronger cross-validation;
- reserved-zero, payload overlap, required alignment, zero-gap, trailing-data,
  and exact declared-data-end checks
  (`nArchiveSize == nDataOffset + nDataSize`) are incomplete;
- writer memory is proportional to total staged package contents;
- signing, encryption, signer identity, patch lineage, and rollback protection
  do not exist.

The writer is transactionally safe with respect to the destination: failure
removes only the staging file and leaves a previous package readable.


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

### 20.7 Contract state of every reserved or candidate family

The names in this table reserve discussion space, not a serialized contract.
Unless a row points to another chapter, it currently has **no accepted language
header, schema ID/version, required field set, default values, enum values,
binary FourCC, chunk layout, reader, writer, or compatibility promise**. Tools
must not create placeholder files merely because an extension appears here.

| Family | Intended ownership | Field-contract state | Next evidence required before fields are frozen |
| --- | --- | --- | --- |
| `.cysurface` | Physical/render surface semantics shared by materials, maps, audio, and gameplay | No fields frozen | A consuming surface service and material/map dependency contract |
| `.cymesh` | Cooked geometry and vertex/index streams | No fields frozen | Importer, renderer input contract, bounds/LOD policy, and target data |
| `.cyskel` | Skeleton hierarchy and bind pose | No fields frozen | Animation runtime hierarchy and retargeting policy |
| `.cyanim` | Animation clip/sample data | No fields frozen | Skeleton binding, events, root motion, compression, and sampling contract |
| `.cyanimgraph` | Animation states, transitions, blends, masks, and synchronization | No fields frozen | Runtime graph evaluator and parameter model |
| `.cyparticle` | Particle/VFX authoring graph and cooked evaluator | No fields frozen | VFX runtime, deterministic simulation policy, and renderer interface |
| `.cysnd` | Imported sample/stream recipe | No fields frozen | Audio decoder/streaming runtime and platform codec targets |
| `.cyaudioevent` | Layered sound-event behavior | Provisional name; no fields frozen | Event evaluator, attenuation, concurrency, and randomization policy |
| `.cymix` | Audio buses, sends, snapshots, and effects | Provisional name; no fields frozen | Mixer graph/runtime and effect plug-in policy |
| `.cyfont` | Font import, glyph coverage, atlas, and fallback data | No fields frozen | Text shaping/rendering stack and localization coverage rules |
| `.cyloc` | Locale-key catalog | No fields frozen | Localization service, placeholder/plural rules, and fallback policy |
| `.cycaption` | Timed subtitles and accessibility captions | No fields frozen | Playback clock, speaker/style metadata, and localization linkage |
| `.cyui` | UI layout/style data | No fields frozen | UI runtime ownership, layout model, and event/data-binding contract |
| `.cypostfx` | Post-processing profile and LUT dependencies | No fields frozen | Renderer post-processing graph and volume/transition model |
| `.cycine` | Cinematic sequence | No fields frozen | Timeline runtime, tracks, events, binding, and seek policy |
| `.cyscene` | World/scene composition | No fields frozen | World representation and entity/component persistence contract |
| `.cyprefab` | Reusable entity template | No fields frozen | Entity/component schema, override, inheritance, and identity policy |
| `.cyphys` | Collision/physics setup | No fields frozen | Physics backend boundary, cooking targets, layers, and material policy |
| `.cynav` | Navigation authoring/cooked data | No fields frozen | Navigation runtime, agent profiles, tile/update policy, and source geometry |
| `.cyflow` | Mission or gameplay logic graph | No fields frozen | Gameplay execution model, node ABI, persistence, and debugging contract |
| `.cyschema` | Data-authored schema definitions | No fields frozen | Bootstrapping/trust design and parity with the current C++ schema graph |
| `.cydata` | Schema-selected gameplay data | No fields frozen | First real gameplay consumer and exact schema IDs |
| `.cymanifest` | Resource, preload, package, or release manifests | Extension only; no shared fields | One exact schema ID/version per manifest purpose |
| `.cymod` | Mod identity, dependencies, mounts, compatibility, and permissions | No fields frozen | Mod loader, trust model, dependency resolution, and packaging policy |
| `.cyplugin` | Native/tool plug-in metadata | No fields frozen | Plug-in ABI/API, service registry, targets, permissions, and load phases |
| `.cyreplay` / `CYRP` | Generated deterministic replay/demo record | Identity candidate only | Simulation command contract, ticks, checkpoints, build/resource identity |
| `.cysave` / `CYSV` | Generated save/checkpoint/profile record | Identity candidate only | Persisted gameplay state, migrations, backup/recovery, and mod policy |
| `.cymap_c` | Cooked map runtime resource | Planned name only | Runtime world consumer and a deliberate source-to-runtime lowering design |
| `.cymap.user` | Per-user editor state beside a map | Possible internal format only | Tile Editor persistence needs and a user-state location/merge policy |

`.cyinput`, `.cyinput_c`/CYIN, and `.cybindings` have a more developed
proposal in [Input Actions and User Bindings](#18-input-actions-and-user-bindings).
Those example fields remain non-normative until implementation and tests freeze
the schema and binary versions.

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
