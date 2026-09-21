<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYKV_2_PROPOSAL.md
//  Purpose: Proposes a bounded second generation of the Cypher KeyValues language.
//  Details: This document separates researched design direction from the implemented
//           CYKV 1 contract. It defines candidate include, constant, conditional,
//           numeric, diagnostic, hashing, and migration behavior before code exists.
//
//  History:
//  - Created by Karlo Siric on 2026-09-17
//  - Marked the accepted first CYKV 2 subset as superseding candidate syntax on
//    2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Cypher KeyValues 2 Design Proposal

**Status:** Research proposal for features beyond the accepted first CYKV 2
subset. The normative subset is now [CYKV_2.md](CYKV_2.md). Tier1 implements its
typed definitions, namespaced includes, exact-schema bases, bounded source
resolver, resolved writer, and canonical hash. Schema registry opt-in, complete
provenance/manifests, a self-contained binary pack, and resource-compiler
adoption remain unavailable as detailed by the normative specification.

The earlier `@const`, `@include ... from`, and `@base` spellings in this research
record are superseded and must not be implemented. The accepted spellings are
`#define NAME <typed value>`, `#include "path" as namespace`, and
`#base "path"`, with `$NAME` and `$namespace.member` references. The accepted
base precedence is local values first, then earlier bases, then later bases.
This document preserves rejected alternatives as design history; it is not a
second CYKV 2 grammar.

**Current implementation:** [CYKV 1](CYKV.md) and the accepted Tier1 subset in
[CYKV 2](CYKV_2.md)

**Normative accepted contract:** [CYKV 2](CYKV_2.md)

**Schema system:** [CYKV Schemas](CYKV_SCHEMAS.md)

**Engine manual:** [CypherEngine Reference Manual](../CYPHERENGINE_REFERENCE_MANUAL.md)

## Table of Contents

1. [Purpose](#1-purpose)
2. [Compatibility Rule](#2-compatibility-rule)
3. [Design Principles](#3-design-principles)
4. [Research Lessons](#4-research-lessons)
5. [Proposed Document Header](#5-proposed-document-header)
6. [Include System](#6-include-system)
7. [Typed Constants](#7-typed-constants)
8. [Build Conditionals](#8-build-conditionals)
9. [Core and Semantic Data Types](#9-core-and-semantic-data-types)
10. [Non-finite Floating Point](#10-non-finite-floating-point)
11. [Object Composition](#11-object-composition)
12. [Source Locations and Diagnostics](#12-source-locations-and-diagnostics)
13. [Canonicalization and Hashing](#13-canonicalization-and-hashing)
14. [Resource and Security Limits](#14-resource-and-security-limits)
15. [Binary Representation](#15-binary-representation)
16. [Schema and Migration Integration](#16-schema-and-migration-integration)
17. [Candidate Grammar](#17-candidate-grammar)
18. [Examples](#18-examples)
19. [Features Deliberately Excluded](#19-features-deliberately-excluded)
20. [Implementation Sequence](#20-implementation-sequence)
21. [Acceptance Criteria](#21-acceptance-criteria)
22. [Open Decisions](#22-open-decisions)

## 1. Purpose

CYKV 1 already supplies a bounded typed tree, explicit document and schema
versions, comments, exact integers, finite floating point, UTF-8 strings, binary
values, arrays, objects, canonical writing, and semantic document hashing.

CYKV 2 should solve the next authoring problems without turning the data language
into a general programming language:

- reuse shared fragments without copying them;
- define repeated typed constants without token substitution;
- select explicitly declared target-dependent data during an offline build;
- preserve precise numeric intent when a schema needs a narrower scalar type;
- represent non-finite values only where a schema has an exceptional need;
- report failures through include and expansion chains;
- make every dependency and build-context input visible to deterministic hashing;
- carry complete document identity into a future binary representation.

CYKV 2 is not required to replace CYKV 1 content. Both language versions may
coexist, and every source schema must state which language versions it accepts.

## 2. Compatibility Rule

CYKV 1 syntax and semantics remain frozen. New syntax must begin with:

```cykv
@cykv 2
@schema "example.schema" 1
```

A CYKV 1 reader must reject that document as an unsupported language version. A
CYKV 2 reader must dispatch through an explicit CYKV 1 compatibility path when it
loads a version 1 document. It must never reinterpret version 1 text using version
2 rules.

A language-version increase does not automatically increase a source schema
version. A schema owner decides whether the new language changes the semantic
contract enough to require a schema generation. Build tools include both numbers
in source identity.

## 3. Design Principles

### 3.1 Bounded by construction

Every recursive or multiplicative operation has a configured limit: include
files, include depth, constant expansion depth, expanded nodes, aggregate bytes,
conditional branches, diagnostics, and dependency records.

### 3.2 Deterministic from declared inputs

The result may depend only on:

- the root document;
- resolved dependency documents;
- an explicit build-context object supplied by the caller;
- a versioned expansion policy.

It may not depend implicitly on environment variables, wall-clock time, current
working directory, random values, process state, host locale, or arbitrary files.

### 3.3 Typed tree expansion

Includes and constants produce CYKV values. They do not paste raw tokens into the
lexer. Typed expansion prevents a constant from manufacturing an unmatched brace,
a directive, a partial number, or another key name.

### 3.4 Clear ownership

The CYKV layer owns syntax, expansion, dependency discovery, canonical values,
and source locations. A source schema owns domain meanings such as color, path,
UUID, transform, material parameter, and action binding.

### 3.5 Fail closed

Unknown directives, unknown condition variables, include cycles, duplicate merge
results, invalid casts, non-canonical paths, limit exhaustion, and unsupported
non-finite values are errors. Silent fallback makes authored data difficult to
review and unsafe to cache.

### 3.6 Transactional publication

Parsing, dependency loading, expansion, schema validation, and migration build a
temporary result. A caller sees a new document only after the whole operation
succeeds.

## 4. Research Lessons

Primary research sources:

- [Valve Source SDK 2013 `KeyValues.h`](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/public/tier1/KeyValues.h)
- [Valve Source SDK 2013 `KeyValues.cpp`](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/tier1/KeyValues.cpp)
- [Valve Data Model interface and encoding/format versions](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/public/datamodel/idatamodel.h)
- [Valve Data Model attribute types](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/public/datamodel/dmattributetypes.h)

Valve's classic KeyValues implementation demonstrates several useful ideas:

- `#include` loads sibling content relative to the current resource;
- `#base` recursively supplies missing keys while local values win;
- platform conditionals select records at load time;
- the runtime tree can distinguish strings, integers, floating point, unsigned
  64-bit values, colors, and subkey collections;
- text and binary encodings serve different consumers.

It also demonstrates behaviors Cypher should make more explicit:

- include and base semantics must be specified independently;
- dependency cycles and aggregate work need hard limits;
- condition variables must be supplied by the caller instead of reading hidden
  process state;
- inferred text types should not change because of platform library parsing;
- duplicate-key policy must be explicit;
- binary data must never serialize native pointers;
- diagnostics must retain the complete dependency chain;
- string tables and token buffers need engine-owned capacity policies;
- binary readers require the same hostile-input discipline as text readers.

Cypher should adopt the responsibility boundaries, not Valve's syntax or binary
layout. CYKV remains an independent format.

## 5. Proposed Document Header

> **Superseded candidate syntax:** The accepted directive and reference grammar
> is defined in [CYKV_2.md](CYKV_2.md). The `@...` forms below are retained only
> to explain the design path and must not be implemented.

A complete source document keeps the two required header directives:

```cykv
@cykv 2
@schema "cypher.material" 3
```

Optional dependency and constant directives follow the header and precede the
root value:

```cykv
@cykv 2
@schema "cypher.material" 3

@base "materials/common/world_surface.cymat"
@const MAX_DETAIL_DISTANCE 2048f32
@include common from "materials/common/constants.cykv"

{
    detail_distance = MAX_DETAIL_DISTANCE
    fallback_tint = common.DEFAULT_TINT
}
```

The exact identifier-reference spelling remains an open grammar decision. The
examples in this proposal use bare constant references only where no object key
or scalar literal could be ambiguous.

A reusable fragment uses an explicit fragment header and cannot masquerade as a
complete schema document:

```cykv
@cykv 2
@fragment "cypher.constants" 1

{
    DEFAULT_TINT = [1f32, 1f32, 1f32, 1f32]
}
```

A full schema document and a fragment are distinct inputs. A source compiler may
allow only declared fragment IDs in each include position.

## 6. Include System

> **Superseded spelling:** Accepted includes use
> `#include "path" as namespace` and `$namespace.member`. The semantics in the
> normative specification replace this section wherever they differ.

### 6.1 Proposed form

```cykv
@include render_defaults from "config/render/defaults.cykv"
```

The directive loads one CYKV 2 fragment and binds its root value to the namespace
`render_defaults`. The consuming document then references values explicitly:

```cykv
{
    clear_color = render_defaults.CLEAR_COLOR
}
```

This namespaced model is preferred over transparent text inclusion because a
reviewer can see where imported data originates and local object shape remains
obvious.

A schema may additionally define a field that accepts an included object value:

```cykv
{
    state = @value "materials/common/opaque_state.cykv"
}
```

`@value` is a candidate spelling, not an accepted directive. If adopted, it loads
the complete root value at that exact expression location and does not merge
objects implicitly.

### 6.2 Path resolution

An include path must be:

- UTF-8;
- relative to the including document's canonical virtual directory;
- normalized with `/` separators;
- free of empty, `.` and `..` components;
- free of an absolute root or drive prefix;
- within a mounted source root allowed by the invoking tool;
- at most 259 bytes after canonicalization.

Physical symlink resolution must not escape the authorized source root. Tools
record both the authored include spelling and resolved canonical virtual path for
diagnostics.

### 6.3 Dependency graph

The loader records one edge per include. It rejects:

- direct recursion;
- indirect cycles;
- duplicate namespace names;
- the same path resolving to incompatible expected fragment identities;
- unsupported language or fragment versions;
- path aliases that resolve inconsistently under the active VFS policy.

Loading the same fragment more than once may share immutable parsed storage, but
its dependency edge appears for each logical use.

### 6.4 Diagnostics

An error includes:

1. root source path and location;
2. ordered include chain;
3. failing source path and location;
4. directive or field being expanded;
5. stable status code;
6. bounded human-readable explanation.

Example:

```text
CYKV_INCLUDE_CYCLE: material includes form a cycle
  materials/arena/wall.cymat:12:1
  -> materials/common/base.cymat:4:1
  -> materials/arena/wall.cymat:12:1
```

### 6.5 Include limits

Proposed defaults:

| Limit | Proposed default |
| --- | ---: |
| Include depth | 16 |
| Unique files | 64 |
| Include edges | 256 |
| Bytes per dependency | 16 MiB |
| Aggregate source bytes | 64 MiB |
| Expanded nodes | 1,048,576 |

A format compiler may lower these limits. Increasing a limit must remain an
explicit caller decision.

## 7. Typed Constants

> **Superseded spelling:** Accepted constants use `#define NAME <typed value>`
> and `$NAME`. The `@const` examples below are historical candidates.

### 7.1 Goal

Constants remove repeated values without permitting arbitrary token generation.
They are immutable CYKV values.

```cykv
@const DEFAULT_FOV 70f32
@const WHITE [1f32, 1f32, 1f32, 1f32]
@const MAX_PLAYERS 8u32
```

A constant may contain a scalar, array, object, or binary value. Its expansion is
a deep immutable value copy or a shared immutable reference with value semantics.

### 7.2 Scoping

- A root document's constants are private to that document.
- Included fragments expose only values intentionally present in the fragment
  root or an explicit export list if one is added later.
- Namespaces prevent imported name collisions.
- A local definition cannot silently override an imported definition.
- Constants are resolved without textual order dependence unless the grammar
  explicitly adopts declaration-before-use.

### 7.3 Expansion rules

- Constant references expand after syntax parsing and before schema validation.
- Constants can refer only to earlier constants or to fully resolved imported
  values under the initial proposal.
- Recursive references are errors.
- The expansion graph has a bounded depth and edge count.
- Expanded values retain both use-site and definition-site locations.

### 7.4 Deliberate exclusions

The initial system has no:

- function-like macros;
- variadic macros;
- token concatenation;
- stringification;
- arbitrary key-name generation;
- recursive macro substitution;
- host environment lookup;
- shell command execution.

These features would turn a reviewable data document into a preprocessor program.

## 8. Build Conditionals

Conditionals are useful for target-specific authored data but can destroy cache
correctness when they read hidden state. A CYKV 2 conditional may inspect only a
caller-supplied immutable build context.

Candidate syntax:

```cykv
@when target.platform == "macos" {
    shader_profile = "glsl410"
}
@else {
    shader_profile = "glsl450"
}
```

Candidate context properties:

- `target.platform`
- `target.architecture`
- `target.graphics_api`
- `build.profile`
- explicitly registered project feature flags

Every property name, type, and value participates in source identity when read by
a document. Unknown properties are errors even in branches that would otherwise
be skipped, unless the grammar defines a separate feature-query operation.

The first implementation should support only:

- Boolean values;
- equality and inequality;
- logical `and`, `or`, and `not`;
- parentheses;
- membership in a literal array if a demonstrated need exists.

It should not support arithmetic, file tests, regex, function calls, time, random,
environment variables, CVars, or runtime hardware queries.

A schema may forbid conditionals entirely. Gameplay state, network contracts, and
security policy should usually remain explicit fields instead of preprocessed
branches.

## 9. Core and Semantic Data Types

### 9.1 Existing CYKV 1 core

CYKV 1 already distinguishes:

- null;
- Boolean;
- signed 64-bit integer;
- unsigned 64-bit integer;
- finite 64-bit floating point;
- UTF-8 string;
- binary;
- object;
- array.

CYKV 2 should not duplicate schema-level types merely because an engine uses them.

### 9.2 Candidate exact-width numeric literals

Some assets need source identity to distinguish an `f32` value from an `f64`
value or an `i16` index from an `i64` integer. Candidate suffixes are:

```cykv
-7i8
-400i16
-200000i32
-9000000000i64
255u8
65535u16
4000000000u32
9000000000u64
0.5f32
0.5f64
```

If accepted, exact-width types become real node types in the model and binary
pack. They are not comments for the schema to reinterpret. Writers emit the
canonical suffix, and conversions require explicit schema rules.

A simpler alternative keeps the existing i64/u64/f64 core and adds schema-owned
numeric-width validation. That alternative avoids increasing the core node-kind
matrix. The implementation should choose only after shader constants, mesh
metadata, network schemas, and other real consumers demonstrate the need.

### 9.3 Semantic types remain in schemas

These should initially remain validated schema meanings over core values:

- UUID;
- canonical virtual path;
- resource reference;
- color and color space;
- vector, quaternion, and matrix;
- angle and distance units;
- duration;
- enum and bit flags;
- hash and content identity;
- input key, button, axis, and chord;
- entity/component identifier.

Keeping them in schemas prevents CYKV from becoming engine-domain-specific and
allows each format to define required units, ranges, and normalization.

### 9.4 Explicit tagged values

If many schemas need the same semantic type and canonicalization, a future tagged
value may be considered:

```cykv
spawn_id = @uuid "26cb6fa5-18c1-4478-a60e-b8ab1c4e8170"
texture = @resource "textures/world/wall.cytex"
```

Tagged values require a versioned registry and must not invoke arbitrary code.
They are deferred until repeated schema implementations prove the benefit.

## 10. Non-finite Floating Point

NaN and infinity should not become ordinary accepted values throughout the
engine. They break common ordering assumptions, propagate through transforms and
physics, complicate equality, and can make otherwise deterministic reductions
platform-sensitive.

CYKV 2 may represent these spellings at the language level:

```cykv
nan
+inf
-inf
```

The default schema policy rejects all three. A field must explicitly allow one or
more non-finite categories.

Canonical rules:

- every NaN spelling maps to one semantic quiet-NaN category;
- NaN sign and payload are not preserved;
- the canonical text spelling is `nan`;
- positive and negative infinity remain distinct;
- a semantic hash uses fixed domain tags, not host floating-point object bytes;
- binary encoding uses defined bits or dedicated value tags, never native
  serialization;
- `-0.0` follows the existing canonical-zero policy unless a schema opts into
  signed-zero preservation for a demonstrated reason.

Suitable uses are narrow diagnostic, scientific, or import-preservation cases.
Transforms, dimensions, colors, timing, physics, animation, render state, and
networked gameplay fields should continue to require finite values.

## 11. Object Composition

> **Superseded spelling and precedence:** Accepted composition uses `#base` and
> priority `local > earlier base > later base`. The later-base overlay candidate
> below was rejected.

Generic object inheritance is powerful but easy to misunderstand. Valve classic
KeyValues separates include append behavior from base fallback behavior. CYKV
should preserve that conceptual separation.

The proposed `@base` directive requests recursive missing-value inheritance from
another complete document with a schema explicitly accepted by the active format:

```cykv
@base "materials/common/world_surface.cymat"
```

The initial rules are:

- local values override every base;
- the rejected candidate gave later bases priority over earlier bases;
- objects merge recursively;
- arrays replace as a whole;
- a scalar/object or incompatible scalar-type disagreement is an error;
- missing required bases fail the complete operation;
- base cycles are errors with the complete cycle in the diagnostic;
- every base becomes a compiler dependency and hash input;
- a schema may forbid `@base` or replace it with a domain-owned inheritance field;
- one format must not combine generic `@base` with another incompatible
  inheritance mechanism.

Material V2 already owns a `base` field with tested resolution semantics. It
should retain that contract until a later material schema deliberately migrates
to a generic language directive. CYKV does not silently reinterpret existing
schema fields.

If a generic composition operator is later accepted, it should be an expression
with one fixed policy:

```cykv
state = @merge_missing(local_state, common.DEFAULT_STATE)
```

Candidate rules:

- local scalar wins over base scalar;
- two objects merge recursively;
- arrays replace as a whole unless a separate operator is chosen;
- type disagreement is an error;
- duplicate local keys remain an error;
- every resulting node retains provenance;
- merge depth and result-node count are bounded.

A format must not combine generic merge with an incompatible format-specific
inheritance model.

## 12. Source Locations and Diagnostics

CYKV 2 should optionally retain a compact origin record for every expanded node:

- canonical source-file ID;
- start byte, line, and column;
- end byte, line, and column;
- include/import edge;
- constant-definition origin;
- constant-use origin;
- composition origin when applicable.

The normal runtime tree may omit this table after validation. Tools and editors
can request it for error display, go-to-definition, hover help, refactoring, and
incremental rebuild diagnostics.

Diagnostics should provide:

- stable machine-readable code;
- severity;
- primary location;
- bounded related locations;
- schema field path;
- include and expansion chain;
- expected and actual type/value summaries;
- optional repair hint;
- no raw native pointer or unbounded source echo.

## 13. Canonicalization and Hashing

CYKV 2 needs two related identities.

### 13.1 Authored document identity

Covers:

- language and schema or fragment identity;
- root syntax tree values before dependency expansion;
- dependency directive paths and namespaces;
- constant definitions;
- conditional syntax;
- canonical source spelling where syntax itself has meaning.

This identity is useful for editor dirty tracking and dependency caches.

### 13.2 Resolved semantic identity

Covers:

- language and schema identity;
- completely expanded typed root value;
- canonical dependency virtual paths;
- each consumed dependency's semantic identity;
- every build-context property read during conditional evaluation;
- expansion-policy version.

Object members remain bytewise name sorted for semantic hashing. Array order
remains significant. Non-finite values, if allowed, use fixed semantic tags.

Tools should record a dependency list alongside the hash. A hash alone cannot
explain why a rebuild happened.

## 14. Resource and Security Limits

Proposed top-level defaults:

| Resource | Default limit |
| --- | ---: |
| Root source bytes | 64 MiB |
| Aggregate dependency bytes | 64 MiB |
| Include depth | 16 |
| Unique include files | 64 |
| Include edges | 256 |
| Conditional nesting | 32 |
| Constants | 256 |
| Constant dependency edges | 1,024 |
| Constant expansion depth | 32 |
| Parsed nodes | 1,048,576 |
| Resolved nodes | 1,048,576 |
| Diagnostics retained | 256 |
| Related locations per diagnostic | 16 |

Further requirements:

- use overflow-checked size arithmetic;
- reject embedded NUL in text and paths;
- validate UTF-8 before interning;
- cap intern tables;
- do not resolve network URLs;
- do not execute code during parsing or expansion;
- do not disclose physical paths when diagnostics cross a trust boundary;
- keep the previous valid asset live after a failed hot reload;
- fuzz both unexpanded and resolved paths;
- test include cycles, alias paths, hostile sizes, and expansion bombs.

## 15. Binary Representation

A CYKV 2 binary document must preserve complete identity. The current CYKV binary
pack stores only a root tree and therefore cannot stand alone as a schema document.

A future binary header should include:

- binary magic;
- binary format version;
- byte order marker;
- header and total size;
- CYKV language version;
- document kind: schema document or fragment;
- schema/fragment identifier and version;
- flags;
- node count;
- string and binary data sizes;
- dependency count;
- canonical semantic hash;
- optional source hash;
- explicit section offsets and sizes.

It must never serialize:

- native pointers;
- `sizeof`-dependent structures;
- host enum layouts;
- host byte order;
- unordered hash-table iteration;
- unresolved include directives;
- process-specific string-table IDs.

Whether dependency provenance belongs in the runtime binary or a separate build
manifest remains an open decision.

## 16. Schema and Migration Integration

Expansion occurs before ordinary schema validation:

```text
lex and parse root
  -> load and validate dependency headers
  -> parse dependencies
  -> resolve include graph
  -> resolve constants
  -> evaluate declared build conditionals
  -> produce resolved typed tree
  -> select exact schema ID/version
  -> structural schema validation
  -> typed semantic decode
  -> optional format migration
```

A migration transforms one already validated schema generation into another
explicit generation. It is not a fallback parser. Every migration has:

- exact input and output IDs/versions;
- implementation version;
- deterministic behavior;
- diagnostics;
- loss report;
- tests and golden outputs.

Editors should normally preserve the original source generation until the user
chooses to upgrade. Command-line tools may expose `validate`, `format`, `expand`,
`dependencies`, and `migrate` operations.

## 17. Candidate Grammar

> **Superseded for accepted directives:** The normative grammar in
> [CYKV_2.md](CYKV_2.md) replaces the include, base, constant, and reference
> productions below. Unaccepted candidate features remain research only.

This fragment is illustrative EBNF, not a normative grammar:

```ebnf
document        = language-header, schema-header, { directive }, root-value ;
fragment        = language-header, fragment-header, { directive }, root-value ;

directive       = include-directive
                | base-directive
                | const-directive
                | conditional-directive ;

include-directive = "@include", identifier, "from", string ;
base-directive    = "@base", string ;
const-directive  = "@const", identifier, value ;

conditional-directive
                = "@when", condition, object,
                  { "@else_when", condition, object },
                  [ "@else", object ] ;

condition       = condition-or ;
condition-or    = condition-and, { "or", condition-and } ;
condition-and   = condition-not, { "and", condition-not } ;
condition-not   = [ "not" ], condition-primary ;
condition-primary
                = context-reference, ( "==" | "!=" ), scalar
                | "(", condition, ")" ;

value           = null | boolean | integer | float | string | binary
                | array | object | constant-reference | imported-reference ;
```

The final grammar must specify whitespace, comments, token boundaries, Unicode,
reserved words, identifier normalization, numeric ranges, error recovery, and
all ambiguity cases.

## 18. Examples

### 18.1 Shared input constants

```cykv
@cykv 2
@fragment "cypher.input.constants" 1

{
    DEFAULT_MOUSE_SENSITIVITY = 0.08f32
    DEFAULT_STICK_DEAD_ZONE = 0.15f32
}
```

```cykv
@cykv 2
@schema "cypher.input" 1

@include input_defaults from "input/common/constants.cykv"

{
    contexts = {
        gameplay = {
            mouse_sensitivity = input_defaults.DEFAULT_MOUSE_SENSITIVITY
            stick_dead_zone = input_defaults.DEFAULT_STICK_DEAD_ZONE
        }
    }
}
```

### 18.2 Target-dependent texture policy

```cykv
@cykv 2
@schema "cypher.texture" 3

{
    source = "textures/world/panel.png"
    type = "2d"
    usage = "color"
    color_space = "srgb"

    @when target.platform == "macos" {
        output = { format = "bc7" }
    }
    @else_when target.platform == "ios" {
        output = { format = "astc_6x6" }
    }
    @else {
        output = { format = "auto" }
    }
}
```

This example is only a language illustration. Those texture output encoders do
not currently exist.

### 18.3 Schema-gated non-finite value

```cykv
@cykv 2
@schema "cypher.test.numeric" 1

{
    expected_import_result = nan
}
```

A normal transform or material schema would reject the same value.

## 19. Features Deliberately Excluded

CYKV 2 should remain data. The following are outside its intended scope:

- loops;
- user-defined functions;
- mutable variables;
- arbitrary arithmetic;
- reflection over schema definitions;
- file existence queries;
- directory enumeration;
- network access;
- shell commands;
- dynamic libraries;
- runtime CVar reads;
- date/time reads;
- random generation;
- unconstrained regular expressions;
- general template programming;
- implicit object inheritance;
- exception-driven partial recovery that publishes incomplete data.

If authoring requires substantial computation, a dedicated importer or compiler
should generate a validated CYKV document or cooked resource.

## 20. Implementation Sequence

1. Correct the known CYKV 1 specification/parser disagreements and freeze the
   resulting conformance suite.
2. Add per-node source ranges behind an optional parse flag.
3. Introduce a dependency-loader interface with canonical VFS paths, hard limits,
   cycle detection, and include-chain diagnostics.
4. Implement namespaced fragment includes with no macros or conditionals.
5. Extend semantic hashing and compiler dependency records to cover fragments.
6. Add immutable typed constants and provenance.
7. Decide exact-width numeric node types from concrete compiler needs.
8. Add a caller-owned build-context interface and minimal condition grammar.
9. Add opt-in non-finite value categories and canonical hashing.
10. Define a self-identifying binary generation only after the resolved document
    model is stable.
11. Add formatter, expansion viewer, dependency graph, and migration commands.
12. Fuzz every stage and qualify deterministic golden outputs on supported hosts.

Each step should ship with a real format consumer. Avoid implementing every
proposal as one parser rewrite.

## 21. Acceptance Criteria

A CYKV 2 feature is ready only when it has:

- normative grammar and semantic wording;
- explicit version dispatch;
- hard resource limits;
- stable diagnostic codes and source chains;
- canonical writer behavior;
- semantic hash behavior;
- dependency tracking;
- transactional failure behavior;
- hostile-input tests;
- cycle and expansion-limit tests;
- deterministic golden files;
- formatter behavior;
- one real source-format consumer;
- migration and compatibility documentation.

CYKV 2 as a language is complete only when all accepted features also have a
self-identifying binary representation or an explicit decision that no binary
form is needed.

## 22. Open Decisions

The following decisions remain intentionally unresolved:

- whether exact-width numeric values belong in the core tree or schemas;
- final constant-reference syntax;
- whether fragments export their root object directly or use an export list;
- whether expression-position includes are needed after namespaced imports;
- whether generic object merge should exist at all;
- whether conditionals may produce arbitrary values or object members only;
- whether comments and formatting need a lossless editor syntax tree;
- how incremental reparsing identifies unchanged included nodes;
- whether dependency provenance belongs in the binary document;
- whether non-finite values warrant a language feature before a real schema needs
  them.

These questions should be answered by tests and consumers, then recorded in an
architecture decision record before implementation freezes CYKV 2.
