<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYKV.md
//  Purpose: Defines version 1 of the Cypher KeyValues text format.
//  Details: This specification fixes the lexical grammar, document structure,
//           scalar semantics, canonical representation, limits, diagnostics,
//           versioning, and boundary between CYKV and schema validation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Cypher KeyValues 1 Specification

## Status

This document is the normative draft specification for Cypher KeyValues version
1, abbreviated `CYKV 1`. Implementations must not describe themselves as CYKV 1
conforming until the conformance tests derived from this document pass.

The terms **must**, **must not**, **required**, **should**, **should not**, and
**may** describe requirements with their usual standards-document meanings.

## Purpose

CYKV is CypherEngine's bounded, typed, hierarchical source-data format. It is
designed for data that must be:

- readable and writable by humans
- deterministic in source control and build pipelines
- validated against an explicit domain schema
- processed by both command-line tools and Mason
- converted into specialized cooked runtime resources

CYKV is not a gameplay scripting language, package archive, database, or
universal runtime binary representation.

## Terminology Decision

`CYKV` is the single name for the language, semantic data model, parser, and
writer family. Older planning documents use `CYDF` for the serialized language
and `CypherKeyValues` for its API. That split is retired because two names for
one contract create needless ambiguity.

Domain files retain meaningful extensions such as `.cymap`, `.cymat`, and
`.cyprefab`. Their `@schema` directive states what the CYKV document means.

## Source And Cooked Data

A CYKV source file is not normally loaded as the final shipping representation:

```text
CYKV source document
    -> parse
    -> schema validation
    -> domain compilation
    -> specialized cooked binary resource
```

For example, `facility.cymap` is a CYKV source document governed by the
`cypher.map` schema. Its compiled runtime representation is `facility.cymap_c`.
The cooked file is free to use a domain-specific chunk layout and magic value.

## Encoding

A conforming CYKV 1 document must satisfy all of these rules:

- The source encoding is UTF-8 without a byte-order mark.
- Unicode scalar values must use their shortest valid UTF-8 encoding.
- Surrogate code points and values above `U+10FFFF` are invalid.
- Input may use LF or CRLF line endings.
- A bare CR is invalid.
- Canonical output always uses LF.
- The first byte of the file must be `@`; comments, whitespace, and a UTF-8 BOM
  are not allowed before the language header.

Byte offsets are zero-based. Diagnostic line and column numbers are one-based.
Columns count decoded Unicode scalar values, not display cells.

## Document Header

Every conforming document starts with two header lines:

```cykv
@cykv 1
@schema "cypher.project" 1
```

The language header selects the CYKV grammar version. The schema header selects
a separately versioned domain contract.

```ebnf
document         = language-header, line-end,
                   schema-header, line-end,
                   trivia, object, trivia, end-of-input ;

language-header  = "@cykv", horizontal-space, decimal-version ;
schema-header    = "@schema", horizontal-space, string,
                   horizontal-space, decimal-version ;
decimal-version  = nonzero-digit, { decimal-digit } ;
schema-id        = schema-component, ".", schema-component,
                   { ".", schema-component } ;
schema-component = lowercase-letter,
                   { lowercase-letter | decimal-digit | "_" | "-" } ;
```

Header requirements:

- `@cykv` must be the first token in the file.
- `@schema` must be the next header line.
- The CYKV language version and schema version must be positive decimal `u32`
  values without signs, separators, or leading zeroes.
- The schema identifier must be a non-empty normal string.
- Schema identifiers must match `schema-id`; examples include `cypher.map` and
  `cypher.audio.event`.
- Unknown CYKV major versions must be rejected.
- An unavailable schema or schema version is a schema-layer error, not a syntax
  error.
- Other `@` directives are reserved and invalid in CYKV 1.

## Whitespace And Comments

Whitespace separates tokens but is otherwise insignificant.

```ebnf
horizontal-space = " " | "\t" ;
line-end          = "\n" | "\r\n" ;
whitespace        = horizontal-space | line-end ;
trivia            = { whitespace | line-comment | block-comment } ;
required-trivia   = whitespace | line-comment | block-comment,
                    { whitespace | line-comment | block-comment } ;
line-comment      = "//", { non-line-end-byte } ;
block-comment     = "/*", { block-comment | block-comment-byte }, "*/" ;
```

CYKV 1 supports:

- `//` line comments
- `/* ... */` block comments
- nested block comments
- trailing comments after a value

CYKV 1 does not support `#` comments or HTML-style comments. Comments are trivia:
the semantic tree does not retain them. A future Mason lossless syntax tree may
retain comments and formatting without changing semantic CYKV behavior.

## Values

CYKV 1 has these semantic value types:

- `null`
- boolean
- signed 64-bit integer
- unsigned 64-bit integer
- finite 64-bit floating-point number
- UTF-8 string
- binary block
- object
- array

```ebnf
value = "null"
      | "true"
      | "false"
      | signed-integer
      | unsigned-integer
      | floating-point
      | string
      | multiline-string
      | binary
      | object
      | array ;
```

CYKV performs no implicit string-to-number, number-to-boolean, or signed-to-
unsigned coercion. Domain schemas may define controlled conversions during an
explicit migration, but parsing itself does not.

## Objects

An object is a collection of named members:

```ebnf
object       = "{", trivia,
               [ member, { required-trivia, member }, trivia ],
               "}" ;
member       = key, trivia, "=", trivia, value ;
key          = bare-key | string ;
bare-key     = key-start, { key-body } ;
key-start    = ASCII-letter | "_" ;
key-body     = ASCII-letter | decimal-digit | "_" | "." | "-" ;
```

Object rules:

- The root value must be an object.
- Object members are separated by at least one whitespace or comment token.
- Commas and semicolons between object members are invalid.
- Keys are case-sensitive.
- Duplicate keys are always invalid.
- Empty keys are invalid.
- Quoted keys may contain UTF-8 characters and characters unavailable to bare
  keys.
- Implementations preserve source member order for authoring and diagnostics.
- Object order has no domain meaning unless a schema explicitly says otherwise.

## Arrays

Arrays are ordered values separated by commas:

```ebnf
array = "[", trivia,
        [ value, trivia, { ",", trivia, value, trivia }, [ ",", trivia ] ],
        "]" ;
```

A single trailing comma is permitted. Missing commas, doubled commas, and a
leading comma are invalid.

## Normal Strings

Normal strings use double quotes:

```cykv
name = "security_terminal"
path = "materials/facility/wall_01.cymat"
```

Single-quoted strings are invalid. Unescaped line endings and control characters
from `U+0000` through `U+001F` are invalid.

CYKV 1 recognizes these escapes:

| Escape | Meaning |
| --- | --- |
| `\"` | quotation mark |
| `\\` | reverse solidus |
| `\/` | solidus |
| `\b` | backspace |
| `\f` | form feed |
| `\n` | line feed |
| `\r` | carriage return |
| `\t` | horizontal tab |
| `\uHHHH` | one Unicode code unit |
| `\UHHHHHHHH` | one Unicode scalar value |

Two `\u` escapes may form one valid UTF-16 surrogate pair. Isolated surrogates,
invalid scalar values, `\x`, octal escapes, and unknown escapes are invalid.
Decoded CYKV strings must not contain `U+0000`.

## Multiline Strings

Multiline strings use triple double quotes:

```cykv
description = """
    Emergency lighting is active.
    Proceed to the lower facility.
    """
```

Rules:

1. The opening `"""` must be followed immediately by a line ending.
2. The closing `"""` must be the first non-horizontal-whitespace token on its
   line and must be followed only by horizontal whitespace and a line ending or
   end of input.
3. The horizontal whitespace before the closing delimiter defines the margin.
4. Every non-empty content line must begin with exactly that margin. The margin
   is removed from every content line.
5. Up to the margin width is removed from an otherwise blank content line.
6. The line ending after the opening delimiter and the line ending immediately
   before the closing delimiter are not part of the value.
7. Remaining line endings are normalized to LF.
8. Normal string escapes are interpreted inside multiline strings.

A content line with less indentation than the closing margin is invalid. This
rule makes indentation deterministic and prevents editor formatting from
silently changing the stored value.

## Integers

An unsuffixed integer is an `i64`. An unsigned integer has a lowercase `u`
suffix.

```cykv
health = 100
offset = -64
mask = 0xff00u
large_id = 18446744073709551615u
```

Supported bases:

- decimal: no prefix
- hexadecimal: `0x`
- octal: `0o`
- binary: `0b`

Rules:

- An optional `+` or `-` may precede a signed integer.
- An unsigned integer cannot have a sign.
- `_` separators are allowed only between two valid digits.
- Decimal integers other than zero must not begin with `0`.
- A base prefix must be followed by at least one valid digit.
- A base-prefixed integer cannot contain digits invalid for that base.
- An unsuffixed value must fit exactly in `i64`.
- A suffixed value must fit exactly in `u64`.
- Overflow and underflow are parse errors.

## Floating-Point Numbers

Floating-point values are finite IEEE-754 binary64 values. A number is floating
point when it contains a decimal point or decimal exponent.

```cykv
volume = 0.75
distance = 1.0e+3
exposure = -2.5
```

Rules:

- A decimal point must have at least one digit on each side.
- The whole-number component must not contain redundant leading zeroes.
- An exponent must contain at least one digit.
- `_` is allowed only between digits within one numeric component.
- Base prefixes and unsigned suffixes are invalid on floating-point values.
- `NaN`, `Inf`, `Infinity`, and overflow to infinity are invalid.
- Underflow that rounds a nonzero literal to zero is invalid.
- Negative zero is valid and must retain its sign in the semantic value.

## Binary Blocks

Binary data uses an explicit hexadecimal string:

```cykv
signature = hex"89504e470d0a1a0a"
```

The payload must contain an even number of ASCII hexadecimal digits. Whitespace,
separators, escapes, and non-ASCII characters inside the payload are invalid.
Canonical output uses lowercase hexadecimal digits. Large bulk data should be a
separate resource rather than embedded in CYKV.

## Complete Example

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

## Semantic Document Model

Parsing a conforming document produces:

- CYKV language version
- schema identifier
- schema version
- one root object
- owned typed values
- an exact source location for syntax failures

The semantic tree does not preserve comments, exact whitespace, quote choice, or
original numeric spelling, and CYKV 1 semantic nodes do not retain source ranges.
Mason eventually requires a separate lossless syntax tree and source map for
comment-preserving edits and node-level diagnostics. The lossless tree lowers
into the same semantic document used by validators and compilers.

## Schema Boundary

The CYKV parser validates language syntax and primitive value ranges. It does not
know what a map, material, particle, or project means.

The Tier2 schema system validates:

- required and optional members
- expected value types
- defaults
- numeric and length ranges
- enumerations
- nested structures
- resource-reference categories
- array limits
- deprecated members
- schema versions and migrations
- editor-facing labels and hints

Domain-specific invariants remain with domain validators. For example, checking
that an entity reference names an entity in the same map belongs to the map
compiler, not the generic CYKV parser.

CYKV 1 has no textual include or import directive. Composition uses explicit
resource references, prefab references, submaps, or schema-defined inheritance.
This keeps dependency discovery, cycle detection, cooker invalidation, and VFS
resolution explicit.

## Resource Limits

A parser must accept caller-provided limits and fail before exceeding them. At a
minimum, limits cover:

- input byte count
- maximum token byte count
- nesting depth
- total node count
- total decoded string and binary bytes
- object member count
- array element count
- comment nesting depth
- diagnostic count

No failure may leave a partially replaced destination document. Parsing is
transactional: success replaces the destination; failure preserves it.

## Diagnostics

Every syntax failure reports at least:

- stable error code
- byte offset
- one-based line and column
- concise description

Schema diagnostics additionally report:

- schema identifier and version
- logical member path
- expected constraint
- actual type or value
- source range when available

Tools may collect multiple recoverable schema errors. The runtime parser may stop
after the first syntax error. Error descriptions are diagnostic text and must not
be used as stable programmatic identifiers.

## Canonical Representation

Canonical CYKV output exists for hashing, caching, reproducible builds, and
tests. It must:

- emit `@cykv 1` followed by the document's exact schema header
- use LF line endings
- sort object members by unsigned UTF-8 key bytes
- preserve array order
- quote every object key
- escape strings using the shortest allowed spelling
- emit lowercase hexadecimal binary payloads
- emit signed integers without a `+`
- emit unsigned integers with a lowercase `u`
- emit finite floating-point values in the shortest round-trippable decimal form
- emit one ASCII space between adjacent compact object members
- emit no comments or insignificant trailing whitespace
- end without an additional newline

Canonicalization is semantic, not lossless. Two source documents with different
comments or formatting may have identical canonical output.

## Versioning

The number after `@cykv` is the language major version. A breaking grammar or
semantic change requires a new number. CYKV 1 readers reject unknown language
versions rather than guessing.

The schema version is independent. Schema migrations operate on parsed semantic
documents and are explicit ordered transformations. A migration must never be
performed silently during a read-only validation operation.

Cooked binary formats have their own magic values and versions. The current
generic `CYKV` packed tree is useful for tests and generic interchange, but it is
not a substitute for domain-specific cooked formats.

## Security Requirements

Implementations must treat all source as untrusted, including editor and mod
content. They must:

- use checked arithmetic for lengths and allocations
- validate UTF-8 and numeric ranges before storing values
- enforce configured limits before allocation or recursion
- reject duplicate keys before ambiguous replacement occurs
- reject malformed escape sequences and unterminated comments or strings
- avoid recursion beyond the configured depth
- preserve the destination document on failure
- never execute commands, scripts, or includes while parsing CYKV

Cryptographic signatures, encryption, package trust, and multiplayer content
policy are separate layers built on canonical or cooked bytes.

## Non-Goals For Version 1

CYKV 1 deliberately excludes:

- executable expressions
- macros
- conditionals
- textual includes and imports
- anchors or aliases
- implicit type coercion
- custom user-defined scalar literals
- dates and calendar values
- non-finite floating-point values
- preserving comments in the semantic tree
- direct serialization of pointers or compiler ABI structures

These exclusions keep the first contract deterministic, bounded, and suitable
for both runtime tooling and long-lived asset source.
