<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYKV_SCHEMAS.md
//  Purpose: Defines the Tier2 schema architecture used with CYKV documents.
//  Details: This document records descriptor ownership, validation behavior,
//           diagnostics, registry lookup, configuration policy, and the initial
//           project and user-settings contracts.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# CYKV Schema Architecture

## Status

The Tier2 static schema foundation, project manifest decoder, and user-settings
decoder described here are implemented. This remains the first schema layer,
not the final reflection or schema-authoring system.

The normative language grammar remains in [CYKV.md](CYKV.md). Tier2 never changes
whether a CYKV token or document is syntactically valid.

## Dependency Boundary

```text
CYKV UTF-8 source
    -> Tier1 lexer/parser
    -> owned semantic document
    -> Tier2 exact schema lookup
    -> bounded schema validation
    -> domain decoder/compiler
    -> cooked runtime resource
```

`CypherCommonTier2` is a separate static library that publicly depends on
`CypherCommonTier1`. Tier1 does not depend on schemas or domain formats.

## Descriptor Model

A `schema_descriptor_t` contains:

- a canonical schema ID such as `cypher.project`
- a positive schema version
- one immutable root rule

Rules can constrain:

- exact allowed CYKV value types
- required, optional, and deprecated object members
- unknown-member policy
- array element rule and element-count range
- string byte-length range and allowed-value set
- binary byte-size range
- signed integer range
- unsigned integer range
- finite floating-point range

Descriptors are immutable and do not own memory. Static descriptors can be
shared by the runtime, CLI tools, tests, and Mason. Recursive rule references are
allowed; descriptor checking bounds its own traversal and rejects malformed
ranges, duplicate members, invalid masks, and invalid schema identities.

## Registry Model

`schema_registry_t` uses caller-provided pointer storage. Registration performs
descriptor validation and rejects an existing exact `(schema ID, version)` pair.
Different versions of one schema may coexist.

Lookup is exact. The registry never silently selects the newest version and does
not migrate a document during validation.

## Validation

Validation is read-only and allocation-free. The caller supplies:

- validation depth and node limits
- an optional diagnostic array and its capacity
- policy for reporting deprecated members

The result reports required and written diagnostic counts, error and warning
counts, nodes visited, and whether diagnostic output was truncated. A caller may
pass zero diagnostic capacity to count failures without collecting records.

Validation does not mutate values, insert defaults, resolve resources, execute
scripts, or compile domain data.

## Diagnostic Paths

Diagnostics use a bounded JSON Pointer-style path:

```text
/
/display/width
/search_paths/2
```

Member bytes are escaped as follows:

- `~` becomes `~0`
- `/` becomes `~1`
- ASCII control bytes become `~xHH`

The root path is `/`. A path that exceeds `CY_SCHEMA_MAX_PATH` produces a stable
`PATH_LIMIT` error rather than truncating into an ambiguous path.

The current CYKV semantic tree does not retain a source range per node. Syntax
failures still have exact byte, line, and column locations. Mason will eventually
add a lossless syntax tree/source map so schema diagnostics can also point to an
authoring range.

## Initial Project Schema

The first registered contract is:

```cykv
@cykv 1
@schema "cypher.project" 1

{
    id = "reap"
    name = "REAP"
    start_map = "maps/facility.cymap"
    search_paths = ["game", "engine", "mods/base",]
}
```

Version 1 currently defines:

| Path | Type | Requirement | Constraint |
| --- | --- | --- | --- |
| `/id` | string | required | 1-64 bytes; lowercase project identifier |
| `/name` | string | required | 1-128 UTF-8 bytes |
| `/start_map` | string | required | canonical virtual path to `.cymap` |
| `/search_paths` | array | optional | 1-64 strings |
| `/search_paths/*` | string | element | unique canonical virtual path, 1-1024 bytes |

Unknown root project members are rejected. This contract is deliberately small;
new project concerns should be added only when a real consumer exists. The `id`
must begin with `a-z`; remaining bytes may be `a-z`, `0-9`, `_`, or `-`.
`ProjectManifest_Decode` enforces these semantic rules after structural schema
validation. Its returned strings borrow the source document, which must remain
alive and unchanged while the view is used.

Display mode and other writable preferences do not belong in a shared project
manifest. This keeps `cypher.project` deterministic and suitable for source
control.

## User Settings Schema

Local user and machine preferences use a separate contract:

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

Version 1 defines:

| Path | Type | Requirement | Constraint |
| --- | --- | --- | --- |
| `/display` | object | optional | unknown members rejected |
| `/display/width` | i64 | optional | 320-16384 |
| `/display/height` | i64 | optional | 200-16384 |
| `/display/mode` | string | optional | `windowed`, `borderless`, or `fullscreen` |
| `/display/vsync` | bool | optional | exact boolean |

Every field is optional. `CypherSettings_Decode` starts from compiled defaults
of 1280x720, windowed mode, and VSync enabled, then transactionally applies the
validated overrides. The result owns its values and does not borrow the source
document. A missing settings file is handled by the host by using
`CypherSettings_Defaults`; Tier2 does not perform file I/O.

## Configuration Layers

Structured data and command execution remain separate:

1. compiled engine defaults
2. validated, source-controlled `cypher.project` identity and resource roots
3. validated, writable `cypher.settings` user and machine preferences
4. optional `.cfg` command/CVar scripts
5. command-line overrides

CYKV describes typed state. A `.cfg` file executes a constrained ordered command
stream. Neither format should absorb the other's responsibility.

## Deferred Work

These features are intentionally not claimed by the current Tier2 foundation:

- a self-hosted `.cyschema` authoring language
- generated C/C++ bindings
- schema-level default insertion and generic normalization
- schema migration execution
- resource-reference resolution
- reflection metadata and editor presentation hints
- cross-document and domain-specific invariants
- lossless syntax trees and node source maps
- a headless `cykv` or `cyschemac` executable

The next schema should be added only with its first real consumer. Map, material,
entity, and asset schemas must follow their runtime contracts rather than being
invented in isolation.
