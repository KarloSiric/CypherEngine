<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYKV_2.md
//  Purpose: Defines the accepted version 2 expansion of Cypher KeyValues.
//  Details: This specification freezes bounded typed constants, namespaced
//           includes, missing-value base composition, dependency resolution,
//           provenance, and resolved-document identity without adding scripting.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Cypher KeyValues 2 Specification

## Status

This document is the normative language contract for the first accepted
expansion of Cypher KeyValues version 2, abbreviated `CYKV 2`.

The contract and its first Tier1 implementation are accepted. At the 2026-09-18
repository snapshot, the implementation includes:

- `@cykv 2` parsing;
- immutable typed `#define` declarations and `$NAME` references;
- callback-driven `#include` and `#base` dependency resolution;
- namespaced object-member references, including nested object traversal;
- exact-schema, missing-value base composition;
- bounded graph traversal, cycle checks, stable errors, and transactional
  destination preservation; and
- text writing and canonical semantic hashing of resolved CYKV 2 documents.

The implementation does not yet retain node-level provenance or a lossless
directive syntax tree, emit a complete dependency manifest or resolution hash,
serialize a self-contained CYKV 2 binary document, or opt Tier2 schemas and
resource compilers into CYKV 2. The capability table in
[Implementation Plan and Conformance](#17-implementation-plan-and-conformance)
is authoritative for this snapshot.

CYKV 1 remains frozen and is specified in [CYKV.md](CYKV.md). Generic `.cydf`
documents are the [CYDF profile](CYDF.md); CYDF is not another language or
parser. Candidate features beyond this accepted subset remain in
[CYKV_2_PROPOSAL.md](CYKV_2_PROPOSAL.md).

The terms **must**, **must not**, **required**, **should**, **should not**, and
**may** describe requirements with their usual standards-document meanings.

## Contents

1. [Scope](#1-scope)
2. [Compatibility and Processing Model](#2-compatibility-and-processing-model)
3. [Document Structure and Grammar](#3-document-structure-and-grammar)
4. [Symbols, References, and Value Semantics](#4-symbols-references-and-value-semantics)
5. [`#define` Typed Constants](#5-define-typed-constants)
6. [`#include` Namespaced Imports](#6-include-namespaced-imports)
7. [`#base` Missing-Value Composition](#7-base-missing-value-composition)
8. [Virtual Paths and Dependency Resolution](#8-virtual-paths-and-dependency-resolution)
9. [Resolution Order and Transactionality](#9-resolution-order-and-transactionality)
10. [Provenance and Diagnostics](#10-provenance-and-diagnostics)
11. [Required Limits](#11-required-limits)
12. [Canonicalization, Hashes, and Dependency Records](#12-canonicalization-hashes-and-dependency-records)
13. [Schema and Tool Integration](#13-schema-and-tool-integration)
14. [Errors](#14-errors)
15. [Examples](#15-examples)
16. [Excluded and Deferred Features](#16-excluded-and-deferred-features)
17. [Implementation Plan and Conformance](#17-implementation-plan-and-conformance)
18. [Valve Research Boundary](#18-valve-research-boundary)

## 1. Scope

CYKV 2 adds exactly three source directives and two reference forms to the CYKV
1 typed data model:

- `#define NAME <value>` declares one immutable typed constant;
- `#include "path" as namespace` imports another resolved document root behind
  an explicit namespace;
- `#base "path"` fills members missing from the local root object;
- `$NAME` references a local typed constant or, when `NAME` is an include alias,
  copies the complete imported root object; and
- `$namespace.member` references a member of an imported root object; additional
  `.member` segments traverse nested objects.

These facilities solve repeated values, shared data modules, and controlled
object composition. They do not create a general preprocessor. Resolution works
on parsed typed values; it never pastes source tokens.

CYKV 2 keeps the CYKV 1 semantic value kinds unchanged:

- `null`;
- Boolean;
- signed 64-bit integer;
- unsigned 64-bit integer;
- finite binary64 floating point;
- UTF-8 string;
- binary block;
- object; and
- array.

CYKV 2 does not accept non-finite floating point, exact-width numeric suffixes,
conditionals, expressions, or tagged user types. A schema continues to define
domain meanings such as resource reference, color, vector, duration, and UUID.

## 2. Compatibility and Processing Model

Every CYKV 2 document begins with:

```cykv
@cykv 2
@schema "example.schema" 1
```

Language and schema versions remain independent. A schema explicitly declares
which CYKV language versions it accepts. Moving a document from CYKV 1 to CYKV 2
does not automatically change its schema version, although the schema owner may
choose a migration when the new composition behavior changes domain meaning.

A CYKV 1 reader must reject `@cykv 2` as unsupported. A CYKV 2 implementation
must use an explicit version dispatch and must not reinterpret a CYKV 1 document
with CYKV 2 rules. CYKV 1 never recognizes `#define`, `#include`, `#base`, or a
`$` reference.

The complete CYKV 2 source pipeline is:

```text
root UTF-8 source
    -> dependency-directive scan
    -> callback-driven canonical VFS dependency discovery
    -> recursive dependency parse and resolution
    -> typed constant and import-reference expansion
    -> ordered #base missing-value composition
    -> effective CYKV semantic document
    -> optional future provenance/dependency-manifest layers
    -> exact schema validation
    -> domain decoder or compiler
```

The effective semantic document contains only ordinary CYKV values. The current
resolver scans dependency directives, replaces their source bytes with
whitespace to preserve later parser locations, seeds include aliases as immutable
typed definitions, parses local definitions and the root, and finally applies
bases. It does not retain a lossless directive tree or node origins. Those remain
required toolchain work.

## 3. Document Structure and Grammar

### 3.1 Preamble and root

CYKV 2 preserves the CYKV 1 encoding, whitespace, comments, strings, binary
blocks, numbers, object, array, duplicate-key, and effective-root rules. The only
grammar additions are the directive preamble and value references.

```ebnf
document-v2       = language-header-v2, line-end,
                    schema-header, line-end,
                    trivia,
                    { dependency-directive, line-end, trivia },
                    { define-directive, trivia },
                    root-value-v2, trivia, end-of-input ;

root-value-v2     = object-v2 | reference ;

language-header-v2 = "@cykv", horizontal-space, "2" ;

dependency-directive = include-directive | base-directive ;

define-directive  = "#define", required-horizontal-space,
                    identifier, required-horizontal-space,
                    value-v2 ;

include-directive = "#include", required-horizontal-space,
                    string, required-horizontal-space,
                    "as", required-horizontal-space,
                    identifier ;

base-directive    = "#base", required-horizontal-space,
                    string ;

value-v2          = cykv-1-scalar
                  | object-v2
                  | array-v2
                  | reference ;

reference         = "$", identifier
                  | "$", identifier, ".", member-identifier,
                    { ".", member-identifier } ;

identifier        = identifier-start, { identifier-body } ;
identifier-start  = ASCII-letter | "_" ;
identifier-body   = ASCII-letter | decimal-digit | "_" ;

member-identifier = identifier-start,
                    { ASCII-letter | decimal-digit | "_" | "-" } ;
```

`object-v2` and `array-v2` are the CYKV 1 object and array productions with
`value-v2` accepted wherever CYKV 1 accepts a value. `cykv-1-scalar` denotes the
unchanged CYKV 1 scalar grammar. The schema header grammar is unchanged.

Every dependency directive occupies one physical line. Horizontal whitespace
and a `//` comment may follow it; other tokens and trailing block comments are
invalid. Dependency paths are normal quoted strings without escape sequences.
Dependency directives must precede every local `#define`.

A `#define` initializer is one syntactically complete value and may contain a
multiline object, array, or string. It must be separated from the next definition
or root by CYKV trivia. Newlines are the canonical presentation, but definition
parsing is token-delimited rather than line-delimited.

When `root-value-v2` is a reference, its expanded value must be an object. A
scalar or array root remains invalid.

### 3.2 Placement

Directives may appear only after the complete `@schema` line and before the root
value. All includes and bases form the first preamble group; all local definitions
form the second. Comments and whitespace may appear between directives. A
directive is
invalid:

- before either required header;
- inside an object, array, string, or another directive value;
- after the root object;
- in place of an object key; or
- in a CYKV 1 document.

Unknown `#` directives are errors. `#` does not begin a comment.

### 3.3 Names and case

Directive identifiers and imported member references are ASCII and
case-sensitive. Uppercase snake case is recommended for local constants, while
lowercase snake case is recommended for namespaces. These spelling styles do
not affect validity.

A qualified reference traverses object members from its first symbol. Each
member segment must match `member-identifier`; `-` is permitted after the first
character. A dot is always a traversal separator. Keys containing `.`, `/`,
whitespace, Unicode, or another unsupported character cannot be selected by this
reference form. Arrays cannot be indexed. An included document intended as a
value module should expose stable reference-compatible keys.

## 4. Symbols, References, and Value Semantics

One document preamble owns a single symbol table shared by constant names and
include namespace names. A name must be unique across both categories. Object
keys and schema fields occupy data positions rather than this symbol table and
may use the same spelling. Imported members always require namespace
qualification and do not become local symbols.

Symbols have document-local scope:

- a constant is visible only in the document that declares it;
- an include namespace is visible only in the importing document;
- declarations inside an included or base document do not leak to its parent;
- a base contributes only its resolved root values; and
- an included document exposes members of its resolved root through its alias.

The resolver resolves every dependency directive before parsing the local
definition group. Include aliases therefore exist when the first local
`#define` is parsed. Local definitions use declaration-before-use: a definition
may reference any include alias and any earlier local definition, but not a
later local definition. The root follows the complete preamble and may reference
any symbol declared there. Include aliases and local definitions share the
`nMaxDefinitions` budget.

Member traversal is available from any object-valued symbol, whether the symbol
is an include alias or a local definition. Each segment selects one object
member. Traversal through a scalar or array, or selection of a missing member,
is an undefined-reference error. A bare include alias is also a valid typed
reference and copies the complete imported root object as one value.

A reference occupies exactly one value position. It cannot create a key,
directive, partial number, string fragment, path, or token. These are invalid:

```cykv
{ $KEY = 1 }
{ name = "prefix-$NAME" }
#include $PATH as common
```

Resolution replaces a reference with the referenced immutable typed value. An
implementation may deep-copy values or share immutable storage, but observable
value semantics must be identical. No reference node may remain in the effective
semantic document.

The accepted toolchain design requires every substituted node to retain origin
information for both its definition and use site. The current semantic-tree
implementation expands values without retaining node-level origins; see
[Provenance and Diagnostics](#10-provenance-and-diagnostics).

## 5. `#define` Typed Constants

### 5.1 Form

```cykv
#define MAX_PLAYERS 8u
#define DEFAULT_TINT [1.0, 1.0, 1.0, 1.0]
#define DEFAULT_STATE {
    depth_test = true
    blend = "opaque"
}
```

A definition binds one name to one complete CYKV 2 value. Scalars, arrays,
objects, and binary blocks are all permitted. The value becomes immutable after
resolution.

### 5.2 Expansion

```cykv
@cykv 2
@schema "cypher.gameplay.rules" 1

#define STARTING_LIVES 3
#define SAFE_LIMITS {
    min_players = 1
    max_players = 8
}

{
    lives = $STARTING_LIVES
    limits = $SAFE_LIMITS
}
```

Expansion is typed. `$STARTING_LIVES` becomes an `i64`; `$SAFE_LIMITS` becomes
an object. A schema validates the expanded values exactly as if they had been
written at each use site.

Redefinition is invalid even when both definitions have equal values. There is
no `#undef`, conditional redefinition, command-line definition injection, or
implicit override. A constant may refer to earlier constants and earlier
include members, but recursive expansion is always an error.

### 5.3 Meaning of “macro” in CYKV

CYKV documentation may describe `#define` as an object-like macro for
familiarity. Normatively it is an immutable typed constant. It has no parameters
and performs no textual substitution. CYKV has no function-like macros,
variadics, stringification, token concatenation, or generated identifiers.

## 6. `#include` Namespaced Imports

### 6.1 Form and target

```cykv
#include "shared/combat_constants.cydf" as combat

{
    default_damage = $combat.DEFAULT_DAMAGE
    damage_curve = $combat.DEFAULT_CURVE
}
```

The resolver loads the path as a complete supported CYKV document, resolves that
document transactionally, and binds its resolved root object to `combat`. A
CYKV 1 dependency cannot itself contain version-2 directives; a CYKV 2
dependency may. The target must:

- declare a supported CYKV language version;
- have a valid `@schema` identity and version;
- resolve successfully under the same VFS and resolver policy;
- have an object root; and
- expose every referenced member path through reference-compatible object keys.

An include target may use a different schema ID or schema version from the
importing document. The importing domain schema may impose a narrower allowlist,
but the language resolver does not require include-schema equality.

The language does not require the `.cydf` extension, although `.cydf` is the
normal generic value-module profile. A domain schema may allow or forbid include
targets by extension, schema identity, or schema version.

### 6.2 Import behavior

`#include` imports a parsed typed root. It does not:

- paste source text;
- append target root members to the importing root;
- merge duplicate names;
- make target constants or namespaces visible;
- inherit the target schema into the importing document; or
- execute the target as a command or script.

`$combat.DEFAULT_DAMAGE` selects one member from the resolved imported root.
`$combat.palette.accent` traverses nested object members. A missing member or an
attempt to traverse through a scalar or array is an error. The selected member
may itself be any CYKV value, including an array or object, and is substituted
as one immutable value. Bare `$combat` substitutes the complete imported root
object, which is useful when a field expects the reusable record as a whole.

Two include directives may resolve the same canonical document under different
unique namespaces. Implementations may share immutable resolved storage, but
must count both logical dependency edges and report both to an installed edge
sink. A namespace cannot be redeclared.

### 6.3 Difference from Valve `#include`

Valve's classic KeyValues loader parses included content and appends its
top-level keys after the current list. CYKV deliberately does not copy that
behavior. Namespaces keep data origin visible, preserve the local root shape,
and avoid duplicate-key and directive-position ambiguity.

## 7. `#base` Missing-Value Composition

### 7.1 Form and compatibility

```cykv
#base "materials/common/world_surface.cymat"
```

A base is a complete resolved document using a supported CYKV language version.
The local and base roots must both be objects. Their schema IDs and schema
versions must match exactly; their CYKV language versions may differ. A later
schema migration layer may explicitly transform a base before composition, but
version guessing and implicit “latest” selection are forbidden.

A schema may forbid `#base`. A domain that already owns inheritance, such as an
implemented material-schema `base` field, retains that behavior until an
explicit schema migration adopts this language directive. One document must not
apply two incompatible inheritance systems to the same data.

### 7.2 Merge algorithm

The operation `fill_missing(destination, base, path)` is defined as follows:

1. If a base member is absent from the destination object, copy the complete
   base member subtree into the destination. The future origin layer also copies
   its provenance chain.
2. If both corresponding members are objects, recurse by base member source
   order.
3. If both corresponding members have the same non-object CYKV value kind, keep
   the destination value as a whole.
4. If the corresponding value kinds differ, fail with a base type-mismatch
   diagnostic at that path.

Arrays are whole values. Two arrays at the same path do not concatenate or merge
by index; the destination array wins. Scalars are whole values. `i64`, `u64`,
and `f64` are different kinds, so a numeric-kind disagreement is a type mismatch
even when the mathematical values would compare equal.

Effective insertion order is deterministic: existing local members retain their
order, then each missing member is appended when encountered in base member
order, with bases visited in directive source order. Canonical output still sorts
object keys under the normal CYKV canonical rules. Base-member matching is
exact-case, even when a caller supplied a generic destination document whose
ordinary lookup policy is case-insensitive.

Local values always have highest priority.

### 7.3 Multiple bases

Base directives are applied in source order to the already expanded local root:

```cykv
#base "defaults/primary.cydf"
#base "defaults/fallback.cydf"
```

The priority order is:

```text
local root > earlier base > later base
```

The first base fills every location still missing from local data. The second
base fills locations still missing after the first, and so on. When corresponding
objects exist, a later base may still supply object members missing from both the
local object and earlier bases. It never replaces a value already supplied.

This earlier-base priority follows the natural missing-only behavior of Valve's
classic KeyValues implementation. It is intentionally documented because a
reverse-order overlay produces different results.

### 7.4 Isolation

A base document resolves its own constants, imports, and bases first. Its
directive symbols never enter the child scope. Every direct and transitive base
is a dependency of the final document. Missing bases, schema mismatches, type
mismatches, and cycles fail the whole operation.

## 8. Virtual Paths and Dependency Resolution

Directive operands are authored virtual paths, never native operating-system
paths or URLs. A path literal must:

- be a non-empty UTF-8 normal string;
- use `/` separators;
- be relative to the including document's canonical virtual directory;
- contain no leading `/`, drive prefix, or UNC prefix;
- contain no C0 control byte, `DEL`, or backslash;
- use no quoted-string escapes; and
- be at most 259 UTF-8 bytes.

The authored spelling may contain relative `.` or `..` components. The loader
callback owns their interpretation. It must resolve mounts, relative paths, and
symbolic links under the caller's policy, then return a normalized,
root-confined canonical virtual path. That returned identity must use `/`, must
contain no unresolved `.` or `..` component, must not escape the active root,
must contain no C0 control byte, `DEL`, or backslash, and must also fit the
259-byte default path budget.

The loader resolves the operand relative to the document containing the
directive, not the process current working directory and not necessarily the
root document. The language does not perform network fetches, environment
variable substitution, home-directory expansion, or platform search. A caller
must enforce any further mount, symlink, or trust policy in its loader callback.

The resolver forms one graph containing both include and base edges. Node
identity is the canonical virtual path under the active source-mount policy.
Before descending an edge, a depth-first resolver marks nodes as unvisited,
visiting, or complete. An edge to a visiting node is a cycle and fails the
operation. An edge to a complete node may reuse its immutable resolved result.
The current result identifies the failing source and edge; rendering the complete
ordered cycle is part of the future dependency-chain provenance layer.

The source API receives the requesting source's canonical virtual path and the
authored referenced spelling. It returns borrowed source text, its canonical
virtual identity, and a loader-owned handle that is later passed to the matching
release callback. The current `KeyValue_ParseSource` API requires matching open
and release callbacks even when a particular root has no dependency edges. One
edge under one parent and resolver context must resolve
consistently, and one canonical identity must denote the same source bytes for
the lifetime of a resolver operation. Two
different parent directories may legitimately resolve the same relative spelling
to different canonical virtual paths, and each edge records its parent and final
identity. Physical checkout roots never become source identity and never appear
in portable dependency manifests.

## 9. Resolution Order and Transactionality

A conforming resolver performs these logical steps:

1. Validate the root canonical identity, callback contract, graph limits, and
   aggregate source-byte budget. Validate the root's input limit,
   UTF-8/NUL/line-ending rules, and complete two-line header before any callback.
   A dependency's incoming open and edge-sink calls necessarily obtain that
   source; validate its bytes and headers before scanning or opening any of its
   outgoing dependencies.
2. For a CYKV 2 source, scan the dependency-directive group before the first
   `#define`. Enforce comment policy and nesting limits while scanning, then
   validate each directive's exact line grammar and path literal.
3. Walk dependency directives in source order. Ask the loader callback for a
   canonical source, report the edge to the optional dependency sink, and
   recursively resolve every unique source under one cycle and limit policy.
4. Bind each resolved include root to its alias. Replace only the scanned
   dependency-directive bytes with whitespace so the ordinary parser retains
   source byte/line locations.
5. Parse local `#define` declarations and the local root with the include aliases
   pre-seeded. The parser expands typed references and enforces
   declaration-before-use for local definitions.
6. Apply resolved bases to the parsed local root in directive source order using
   the missing-value algorithm.
7. Verify that the effective tree contains no directives or reference nodes and
   remains within parser and merge limits.
8. Publish the effective semantic document only after every step succeeds.

The optional dependency sink is called once for every edge, including repeated
edges to an already resolved canonical source. It receives requesting canonical
path, dependency canonical path, and include/base kind. Rejecting an edge fails
resolution with `DEPENDENCY_SINK_FAILED`.

Implementations may interleave I/O and parsing or memoize completed dependencies,
but observable behavior and diagnostics must match this order. Exact schema
validation and domain decoding follow successful language resolution; they are
not currently performed by `KeyValue_ParseSource` itself.

Failure is transactional. The resolver does not replace an existing destination
document with partial output. A caller building dependency metadata, hashes, or
live resources must apply the same publish-after-success rule; a hot reload
failure retains the previous valid resource.

## 10. Provenance and Diagnostics

The accepted toolchain design requires every effective node to be traceable to
an authored origin. A future tool-capable origin record contains:

- canonical virtual source-file identity;
- source start and end byte offsets and line/column locations;
- the dependency edge through which the source was reached;
- a constant definition location when produced by `$NAME`;
- a constant or imported-reference use location;
- an include declaration and imported member name when applicable; and
- a base declaration and merge path when supplied by composition.

An implementation may intern repeated chains. It may discard tool provenance
after validation when constructing a compact runtime resource, but it must retain
enough information to issue the resolution diagnostic before discarding it.

The current source resolver does not implement node-level provenance or a full
dependency-chain object. Its bounded result records the first error with the
source status, nested parser result when applicable, loader status when
applicable, dependency kind, directive or parser location, unique-source/edge/
byte counters, and bounded source/reference path strings. The optional edge sink
lets callers observe the graph, but it is not a durable manifest and does not
report node origins. Diagnostic path storage replaces C0 and `DEL` bytes with
`?`, preventing malformed callback data from injecting terminal control bytes.
`errorReferencedPath` retains the authored directive operand associated with the
failure, including for a cycle; it is empty when no directive operand applies.
On `PARSE_FAILED`, `parseResult` is the nested parser diagnostic. On success, its
node and data counters describe the final effective root after base composition.

A completed tool diagnostic should report a dependency error in this order:

1. root document and initiating location;
2. each include or base edge in traversal order;
3. failing canonical virtual path and exact source location;
4. logical member path or symbol, when known;
5. stable error category; and
6. a bounded human-readable explanation.

For a substituted value that fails schema validation, tools should report the
use site first and the definition/import origin second. For a base-supplied
value, tools should report the effective logical path and base origin.

CYKV semantic documents still do not preserve formatting or comments. Mason
requires a separate lossless syntax tree that maps into the same effective
semantic document and provenance model.

## 11. Required Limits

Every resolver operation accepts explicit finite limits. General-purpose Cypher
tools use these implemented defaults:

| Limit | Default |
| --- | ---: |
| Bytes in one source document | 64 MiB |
| Aggregate bytes across unique source documents | 64 MiB |
| Maximum dependency depth below the root | 16 (17 source levels, depths 0 through 16) |
| Unique source documents, including the root | 64 |
| Include and base edges | 256 |
| Include aliases plus local `#define` declarations per source | 256 |
| Object/array nesting depth per parsed source | 128 |
| Semantic nodes per parsed/resolved source | 1,048,576 |
| Direct members in one container | 1,048,576 |
| Nested block-comment depth | 64 |
| Decoded names, strings, binary, and live definition data per source | 64 MiB |
| Canonical virtual path bytes | 259 |
| Diagnostic path buffer, including terminator | 1,024 bytes |

The public resolver additionally enforces hard graph ceilings before allocating
scratch arrays or entering recursive traversal: dependency depth may not exceed
64 edges below the root, unique-source capacity may not exceed 1,024, and edge
capacity may not exceed 8,192. Values above those ceilings are
`INVALID_ARGUMENT`, including overflow-scale values such as `CY_USIZE_MAX`.

The underlying CYKV parser's input, container, comment-depth, string-data, and
tree-depth limits apply independently to every source document. Definition
storage, imported aliases, expanded copies, and base copies count against the
relevant parser or resolved-document budgets. The implemented
`nMaxDefinitions` parser option defaults to 256 and covers the combined alias
and local-definition symbol table for a source.

Base composition preflights the copied subtree's node/data cost, destination
container count, and effective merged-tree depth before allocating the clone.
Limit failure preserves the caller's previously published destination document.

A domain compiler may lower a limit. Raising a default must be an explicit
caller decision and must remain at or below implementation hard ceilings. Shared
immutable subtrees do not exempt a document from logical expanded-node and
reference-use limits; otherwise aliasing could bypass resource budgets.

Limit exhaustion is a stable error, not permission to publish a partial result.
The current API returns the first failure rather than retaining a diagnostic
list. Source/reference diagnostic paths are bounded by the 1,024-byte buffers;
the normal 259-byte path policy keeps them complete.

## 12. Canonicalization, Hashes, and Dependency Records

### 12.1 Effective semantic form

Canonical semantic output is produced after all references and bases resolve.
It contains:

- `@cykv 2`;
- the exact schema ID and version;
- the effective root object; and
- no directives, symbols, references, comments, or provenance records.

Object members are sorted, arrays retain order, and scalars use the CYKV 1
canonical rules. Two sources with different constants, include layouts, or base
graphs can therefore have the same effective semantic hash when they produce the
same header and root value.

The CYKV canonical semantic hash remains XXH3-128 with seed zero over the exact
canonical semantic bytes. The `@cykv 2` header prevents a version-1 document
from sharing the same hashed byte stream.

The current text writer and canonical hash accept a successfully resolved CYKV
2 document. They serialize or hash the effective tree. Dependency directives,
definitions, references, comments, and source layout have already been expanded
or discarded and cannot be reconstructed by that writer.

### 12.2 Dependency manifest (accepted next layer)

Semantic equality is not sufficient for incremental builds. The completed
toolchain must also emit a deterministic dependency manifest. It records the
root and every edge in
stable depth-first, directive-source order. Each source record contains:

- canonical virtual path;
- CYKV language version;
- schema ID and schema version;
- source-content digest and its algorithm ID;
- effective semantic hash; and
- resolution status.

Each edge record contains:

- parent and child source-record indices;
- edge kind: include or base;
- directive source location;
- authored path spelling;
- canonical resolved virtual path;
- include namespace when applicable; and
- directive ordinal within the parent document.

Native physical paths, modification times, inode numbers, process locale, and
directory enumeration order must not enter the portable manifest.

This manifest is not implemented in the current source resolver. The optional
dependency callback reports edges during resolution, but it does not retain
source digests, headers, locations, ordinals, or a deterministic serialized
manifest.

### 12.3 Resolution identity (accepted next layer)

When a resource compiler adopts CYKV 2 dependencies, its source identity must
include, with unambiguous
length-prefixing and a versioned domain tag:

- the effective semantic hash;
- the complete deterministic dependency manifest;
- raw content digests for the root and dependencies;
- CYKV resolver-policy version and active limits;
- exact schema and migration versions;
- build-context inputs actually consumed by the domain compiler; and
- compiler/toolchain identity required by that resource family.

This rule ensures that edits to included or base files invalidate dependents,
even when the final value happens to remain equal. The exact binary encoding and
hash algorithm for the broader compiler source identity belong to the versioned
resource-build contract; they are not silently inferred from the semantic hash.

A future authoring writer may preserve the original preamble through a lossless
syntax tree. The implemented semantic writer intentionally emits only the
resolved effective document. The broader dependency/resolution identity is not
yet implemented.

## 13. Schema and Tool Integration

The implemented language resolver validates directive syntax, callback-returned
canonical path shape, reference resolution, dependency cycles and limits, base
shape, and exact base schema compatibility. The caller's loader enforces mount,
symlink, and trust policy. A domain schema decides:

- whether CYKV 2 is accepted;
- whether `#include` or `#base` is permitted at all;
- which included schema IDs and versions are allowed;
- which fields may receive imported objects or arrays;
- whether generic base composition conflicts with domain inheritance; and
- all ordinary member, type, range, path, and cross-field constraints.

Schema validation runs on the effective semantic tree. Defaults, migrations, and
deprecation handling do not mutate dependency source files. Existing Tier2
schema descriptors and resource compilers have not yet opted into CYKV 2 source
resolution.

CYDF uses this language as a generic source-document profile. Domain extensions
such as `.cymat`, `.cyshader`, `.cytex`, `.cymap`, and `.cyscene` retain their
own schema, decoder, compiler, version, and runtime contracts. A shared parser
does not make those formats interchangeable.

The current generic CYKV binary pack does not store a complete language/schema
header or dependency graph. It is therefore not a self-contained CYKV 2 cooked
representation. A future packed representation must version and serialize the
effective header and required identity metadata before tools may advertise it as
a complete resolved document.

## 14. Errors

A conforming implementation must preserve stable failure categories. The current
source API reports:

| `key_value_source_status_t` | Meaning |
| --- | --- |
| `INVALID_ARGUMENT` | Options, callback, root source, root path, or destination is invalid |
| `PATH_LIMIT` | An authored or canonical path exceeds policy |
| `INVALID_PATH` | A path is empty, absolute where relative is required, noncanonical when returned by the loader, or escapes its root |
| `INVALID_DIRECTIVE` | A pre-definition `#` directive has an unknown dependency keyword or violates include/base line grammar; directives after the first `#define` surface as nested parser syntax failures |
| `OPEN_FAILED` | The loader returned `NOT_FOUND`, `ACCESS_DENIED`, or `IO_ERROR` |
| `DEPENDENCY_CYCLE` | The unified include/base graph reaches a source already resolving |
| `DEPENDENCY_DEPTH_LIMIT` | Transitive source depth exceeds policy |
| `SOURCE_LIMIT` | Unique-source count exceeds policy |
| `EDGE_LIMIT` | Include/base edge count exceeds policy |
| `SOURCE_BYTE_LIMIT` | Aggregate bytes of unique sources exceed policy |
| `DUPLICATE_ALIAS` | Two direct includes bind the same alias |
| `SCHEMA_MISMATCH` | A base schema ID or version differs from the local document |
| `MERGE_CONFLICT` | Corresponding base and destination values have different kinds |
| `DEPENDENCY_SINK_FAILED` | The optional edge callback rejected a dependency |
| `PARSE_FAILED` | A source failed ordinary CYKV parsing; inspect the nested parser result |
| `OUT_OF_MEMORY` | Transactional source, dependency, alias, or merge storage could not be allocated |

The nested parser distinguishes unsupported versions, malformed headers and
schemas, duplicate keys, ordinary limits, duplicate definitions,
undefined/self/forward/member references, and the definition budget. A
duplicate include alias is a source error; an alias/local-definition collision
is reported by the parser as a duplicate definition.

Missing includes and bases are never warnings. Unknown references never become
`null`. A type mismatch never falls back to string conversion or numeric
coercion.

## 15. Examples

### 15.1 Shared generic data module

`data/shared/combat_constants.cydf`:

```cykv
@cykv 2
@schema "cypher.data.combat_constants" 1

#define BASE_DAMAGE 24

{
    DEFAULT_DAMAGE = $BASE_DAMAGE
    DEFAULT_CURVE = [1.0, 1.25, 1.5,]
    DAMAGE_TAGS = ["bullet", "direct",]
}
```

`data/rifle.cydf`:

```cykv
@cykv 2
@schema "cypher.gameplay.weapon" 1

#include "shared/combat_constants.cydf" as combat
#define MAGAZINE_SIZE 30

{
    id = "rifle"
    damage = $combat.DEFAULT_DAMAGE
    damage_curve = $combat.DEFAULT_CURVE
    magazine_size = $MAGAZINE_SIZE
}
```

### 15.2 Earlier-base priority

`primary.cydf` resolves to:

```cykv
{
    movement = {
        speed = 9.0
    }
    lives = 3
}
```

`fallback.cydf` resolves to:

```cykv
{
    movement = {
        speed = 5.0
        acceleration = 20.0
    }
    lives = 1
    respawn = true
}
```

The local root:

```cykv
@cykv 2
@schema "cypher.gameplay.mode" 1

#base "primary.cydf"
#base "fallback.cydf"

{
    movement = {
        friction = 6.0
    }
}
```

resolves to the semantic value:

```cykv
{
    movement = {
        friction = 6.0
        speed = 9.0
        acceleration = 20.0
    }
    lives = 3
    respawn = true
}
```

The local `friction` wins, the earlier base supplies `speed` and `lives`, and the
later base supplies only values still missing.

### 15.3 Invalid textual macro expectations

All of these are invalid CYKV 2:

```cykv
#define MAKE_PAIR(a, b) [a, b]
#define PREFIX weapon_
#define JOIN(a, b) a ## b
#define FIELD damage

{
    $FIELD = 24
    id = "$PREFIXrifle"
}
```

## 16. Excluded and Deferred Features

The accepted first expansion deliberately excludes:

- function-like or variadic macros;
- token pasting, stringification, textual replacement, and generated keys;
- `#undef`, redefinition, and command-line macro injection;
- conditional directives such as `#if`, `#when`, or platform tags;
- expressions, arithmetic, comparisons, functions, and string interpolation;
- environment, CVar, clock, random, hardware, and process-state lookups;
- transparent include append behavior;
- wildcard, directory, URL, or package discovery includes;
- optional/missing dependency fallbacks;
- array concatenation or index-wise base merging;
- configurable per-file base precedence;
- array indexing and quoted, computed, or dynamic member paths;
- non-finite floats and exact-width numeric node kinds;
- tagged semantic scalar syntax;
- pointer or ABI-structure serialization; and
- preservation of comments in the semantic tree.

Conditionals, non-finite values, exact-width literals, tagged values, a complete
binary generation, and a lossless editor tree remain separate proposals. They
require their own accepted specification changes and tests; `@cykv 2` does not
implicitly authorize them.

## 17. Implementation Plan and Conformance

### 17.1 Implemented today

The repository currently implements:

- CYKV 1 typed values, document headers, parser, writer, canonical semantic hash,
  generic tree pack, Tier2 schemas, and current domain integrations;
- explicit parser recognition of language versions 1 and 2;
- a CYKV 2 top-level `#define NAME <typed value>` preamble;
- `$NAME` and nested `$symbol.member.child` traversal in value positions,
  including definition values and an object-valued root reference;
- declaration-before-use, unique definition names, immutable deep-copy expansion,
  and rejection of self/forward/unknown references;
- `nMaxDefinitions` with a default of 256 and the stable parser statuses
  `DUPLICATE_DEFINITION`, `UNDEFINED_DEFINITION`, and `DEFINITION_LIMIT`;
- a callback-driven canonical-VFS source resolver for namespaced `#include` and
  exact-schema `#base`;
- one bounded include/base graph with canonical-path deduplication, cycle,
  depth, source, edge, aggregate-byte, and path checks;
- an optional per-edge dependency sink;
- local-over-base, earlier-base-over-later-base, recursive-object merge semantics
  with whole arrays/scalars and explicit type conflicts;
- accounting of private definitions, include aliases, expanded copies, and base
  copies under parser and resolved-document budgets;
- CYKV 2 text writing and canonical semantic hashing of the resolved effective
  document; and
- whole-document transactional failure behavior for parser and resolver paths.

The implementation rejects function-like macros, references in key position,
conditionals, token operations, and dependency directives after the first local
definition. The generic binary pack does not yet provide a self-contained CYKV
2 identity.

### 17.2 Remaining implementation work

The next implementation layers are:

1. bounded per-node provenance and complete include/base chain diagnostics;
2. a loss-aware authoring tree that retains directives, comments, and reference
   origins without changing the semantic tree contract;
3. deterministic dependency manifests, raw source digests, and versioned
   resolution identities for incremental builds;
4. explicit Tier2 schema opt-in and domain rules for includes and bases;
5. CLI validation and dependency-inspection output;
6. one low-risk real CYDF schema, producer, decoder, and consumer;
7. a self-identifying generic packed representation only if a real consumer
   justifies it; and
8. broader fuzz, path-policy, provenance, cache-invalidation, and transactional
   conformance coverage.

Specialized resource compilers should adopt CYKV 2 only after their schemas and
cache identities account for resolved dependencies. Initial adoption should use
one low-risk CYDF schema before migrating specialized asset formats.

### 17.3 Minimum conformance cases

A conforming test suite covers at least:

- every directive spelling and placement failure;
- scalar, array, object, binary, and imported-member expansion;
- symbol collision, forward reference, missing member, and constant cycles;
- relative path normalization, mount escape, symlink escape, and missing files;
- direct, indirect, mixed include/base, and depth-limit cycles;
- local, earlier-base, and later-base precedence at nested object paths;
- scalar, numeric-kind, array/object, and object/scalar mismatch behavior;
- schema identity/version mismatches;
- every resolver limit at boundary and boundary-plus-one;
- deterministic dependency order independent of native directory order;
- semantic-hash equality for equivalent effective values;
- source-identity change when a dependency or edge changes;
- complete provenance at reference and base use sites; and
- preservation of the previous destination after every failure class.

## 18. Valve Research Boundary

The accepted design is informed by these sources:

- [Valve Developer Community: VDF](https://developer.valvesoftware.com/wiki/VDF)
- [Valve Developer Community: KeyValues](https://developer.valvesoftware.com/wiki/KeyValues)
- [Valve Developer Community: KeyValues3](https://developer.valvesoftware.com/wiki/KeyValues3)
- [Valve Source SDK 2013 `KeyValues.h`](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/public/tier1/KeyValues.h)
- [Valve Source SDK 2013 `KeyValues.cpp`](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/tier1/KeyValues.cpp)

Documented Valve behavior establishes useful reference points: VDF files use the
classic KeyValues family; KeyValues supports quoted and unquoted tokens,
duplicate keys, conditional records, `#include`, and `#base`; `#include` appends
loaded top-level records; and `#base` recursively supplies missing records while
local values win. In the pinned SDK implementation, multiple bases naturally
give earlier bases priority because each one fills only values still missing.

Valve KeyValues does not define a file-language `#define` macro facility. Its
loader recognizes `#include` and `#base`, not C-style object/function macros.
CYKV's typed `#define` is therefore a Cypher-owned feature, not a claim of Valve
compatibility.

KeyValues3 is a later Source 2 serialization family with its own text header,
encoding/format identities, value grammar, and binary encodings. It is not a
version flag for classic KeyValues1/VDF, and classic `#include`/`#base` behavior
must not be inferred for KV3. CYKV 2 is likewise an independent Cypher language
generation; it does not claim source or binary compatibility with either Valve
family.

CYKV also deliberately differs from classic KeyValues: duplicate object keys
remain invalid, includes are typed and namespaced instead of appended, paths use
the Cypher VFS security contract, all recursive work is bounded, and provenance
and deterministic dependency identity are required. The sources are design
references, not a grammar, binary-layout, or source-code import.
