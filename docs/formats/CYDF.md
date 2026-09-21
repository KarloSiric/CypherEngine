<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYDF.md
//  Purpose: Defines the generic Cypher Data File authoring profile.
//  Details: This document assigns `.cydf` to schema-selected generic CYKV data
//           without creating a second grammar, parser, or universal cooked file.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Cypher Data File Profile

## Status

`CYDF` is the accepted generic source-document profile for CypherEngine. Its file
extension is `.cydf`.

CYDF is not a second serialization language, parser, semantic tree, schema
system, or cooked binary format. A CYDF file is a CYKV document whose exact
`@schema` header supplies its domain meaning. The normative language contracts
are [CYKV 1](CYKV.md) and [CYKV 2](CYKV_2.md).

The profile and extension are accepted, but no generic CYDF dispatcher,
gameplay-data schemas, domain decoders, universal cooker, or runtime resource is
implemented at the 2026-09-18 snapshot. The existing parser can parse a CYDF
file only when a caller deliberately loads it as CYKV. Tier1 supports complete
CYKV 1 parsing plus the accepted CYKV 2 typed-definition, namespaced-include,
exact-schema base-composition, resolved-writer, and canonical-hash path. Tier2
schema opt-in, domain dispatch, complete provenance/manifests, and compiler
integration remain unavailable.

## 1. Purpose

CYDF gives schema-selected records a stable generic authoring extension when the
data does not justify its own file family. Suitable examples include:

- weapon, item, pickup, damage, and status-effect definitions;
- enemy archetypes and AI tuning records;
- wave tables and encounter parameters;
- player movement or game-balance profiles;
- difficulty and game-mode rules;
- tag, response, surface-property, and lookup tables;
- import, build, test, or tool profiles with an exact owning schema; and
- small reusable value modules imported by CYKV 2 documents.

The extension answers “this is generic schema-selected Cypher data.” The schema
answers what the data actually means.

## 2. Required Document Identity

Every `.cydf` file is a complete CYKV document and must contain both language
and schema headers:

```cykv
@cykv 2
@schema "cypher.gameplay.weapon" 1

{
    id = "rifle"
    magazine_size = 30
    damage = 24
}
```

CYDF has no independent numeric format version. Its compatibility identity is:

```text
CYKV language version + exact schema ID + exact schema version
```

New CYDF content should use CYKV 2 only within the capability implemented by its
consumer. The Tier1 source resolver supports typed constants, namespaced
imports, and base composition; no current CYDF domain consumer selects that
path automatically. A schema may permit CYKV 1 for documents that do not need
version-2 facilities. Readers dispatch by the declared CYKV version and never
infer it from the `.cydf` extension.

A schema ID must be domain-specific, for example:

- `cypher.gameplay.weapon`;
- `cypher.gameplay.wave_table`;
- `cypher.balance.difficulty`;
- `cypher.data.combat_constants`; or
- `cypher.tool.import_profile`.

Generic IDs such as `cypher.cydf`, `cypher.data`, or `cypher.document` are not
sufficient domain contracts. They do not tell a validator which members, types,
limits, or semantics apply.

## 3. Language and Schema Rules

A CYDF file follows its declared CYKV specification without exceptions:

- UTF-8, headers, comments, values, duplicate-key policy, and canonical output
  come from CYKV;
- the root is an object;
- the exact schema descriptor validates all domain members and constraints;
- a typed domain decoder enforces invariants that do not belong in the generic
  schema layer; and
- unknown language or schema versions fail rather than selecting a “latest”
  contract.

For `@cykv 2`, `#define`, `#include`, `#base`, references, dependency limits,
provenance, composition, and hashing follow [CYKV_2.md](CYKV_2.md). A CYDF file
used as an include module may declare either supported CYKV version, must resolve
to an object, and exposes reference-compatible object paths for
`$namespace.member` access. A bare `$namespace` reference copies that entire root
object. Only a CYKV 2 source may itself contain dependency directives or typed
definitions.

The `.cydf` extension never relaxes a schema. Two CYDF files with different
schema IDs are different domain formats even though they share a file extension
and parser.

## 4. Producer, Owner, and Consumer

Every CYDF schema must name:

- the authoring tool or human workflow that produces it;
- the subsystem that owns its semantics;
- the validator and typed decoder;
- the compiler, if source is cooked;
- the runtime or tool consumer;
- path, dependency, and trust policy;
- version and migration behavior; and
- resource, collection, and allocation limits.

The generic CYKV layer owns syntax and typed-tree construction. It does not own
weapon balance, wave spawning, AI behavior, import settings, or other domain
semantics.

## 5. Source and Cooked Data

CYDF is authored source. It does not define a universal `.cydf_c` file, FourCC,
runtime ABI, or memory image.

Each real schema chooses one of these deliberate routes:

1. validate and consume CYDF directly in a tool or low-frequency runtime path;
2. compile it into an existing domain resource;
3. compile it into a new domain-specific CYRS resource after that format passes
   the normal admission process; or
4. use a future self-identifying generic packed document when that contract is
   specified and implemented.

The current CYKV generic tree pack is not automatically a cooked CYDF resource.
Its version-1 layout omits the CYKV language/schema header and dependency graph,
so a caller cannot treat it as a self-contained CYDF document.

A specialized extension becomes justified when a domain needs distinct source
workflow, import semantics, compiler behavior, runtime streaming, binary layout,
editor, or compatibility policy. Merely having a different noun is not enough.

## 6. When CYDF Must Not Be Used

CYDF must not replace established or specialized families such as:

- `.cymap` for the implemented TileEditor map schemas;
- `.cyscene` for the reserved Mason scene/world contract;
- `.cyshader`, `.cytex`, and `.cymat` for renderer source recipes;
- `.cyprefab`, `.cyphys`, `.cynav`, or `.cyflow` once their domain contracts
  require specialized tools and compilation;
- `.cfg` or `.cycfg` command streams;
- `.cypak` package archives;
- save games, replays, recovery journals, logs, reports, or crash dumps; or
- arbitrary scripts or executable gameplay logic.

CYDF also must not become a catch-all for data with no owner, schema, consumer,
limits, or migration policy. The format-admission questions still apply.

## 7. Relationship to the Planned `.cydata` Name

Earlier planning documents proposed `.cydata` and `.cydata_c` for generic typed
gameplay data. Neither identity was implemented or frozen. CYDF replaces that
proposal:

- new generic schema-selected source uses `.cydf`;
- `.cydata` is withdrawn and must not be introduced as an alias;
- no compatibility reader or migration is required because no shipped reader or
  authored contract exists; and
- `.cydata_c` is also withdrawn; a schema selects its deliberate cooked route as
  described above.

Maintaining both extensions would create two names for the same responsibility
and make tool dispatch, documentation, and user expectations ambiguous.

## 8. Relationship to Valve VDF and KeyValues

The name and responsibility boundary are informed by Valve's VDF convention:

- [Valve Developer Community: VDF](https://developer.valvesoftware.com/wiki/VDF)
- [Valve Developer Community: KeyValues](https://developer.valvesoftware.com/wiki/KeyValues)
- [Valve Developer Community: KeyValues3](https://developer.valvesoftware.com/wiki/KeyValues3)
- [Valve Source SDK 2013 `KeyValues.h`](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/public/tier1/KeyValues.h)
- [Valve Source SDK 2013 `KeyValues.cpp`](https://github.com/ValveSoftware/source-sdk-2013/blob/b8cfb12c0e083a2ef5b2f9f9b50f3902fa034474/src/tier1/KeyValues.cpp)

The `.vdf` extension is a use convention for Valve's classic KeyValues family
rather than a separate general grammar. CYDF follows the useful architectural
idea that one structured language can carry several exact domain schemas. It is
not VDF-compatible and does not copy Valve parsing, duplicate-key, include,
conditional, or binary behavior.

Valve KeyValues3 is a distinct Source 2 family rather than a replacement grammar
selected by the `.vdf` extension. CYDF similarly names a usage profile, while
its actual language generation remains the explicit `@cykv` header.

Cypher-specific guarantees remain authoritative: duplicate object keys are
invalid, types are explicit, versions are mandatory, CYKV 2 includes are
namespaced, paths resolve through the bounded VFS policy, and every domain has an
exact schema and ownership contract.

## 9. Example Generic Profiles

### 9.1 Difficulty profile

```cykv
@cykv 2
@schema "cypher.balance.difficulty" 1

#define BASE_HEALTH_SCALE 1.0

{
    id = "normal"
    enemy_health_scale = $BASE_HEALTH_SCALE
    enemy_damage_scale = 1.0
    spawn_budget_scale = 1.0
}
```

### 9.2 Shared constant module

```cykv
@cykv 2
@schema "cypher.data.combat_constants" 1

{
    DEFAULT_DAMAGE = 24
    CRITICAL_MULTIPLIER = 2.0
    DEFAULT_TAGS = ["direct", "bullet",]
}
```

The second document may be imported with:

```cykv
#include "shared/combat_constants.cydf" as combat
```

It does not append those members to the importing root. Authors use explicit
references such as `$combat.DEFAULT_DAMAGE`.

## 10. Current Capability and Next Gate

| Layer | Status |
| --- | --- |
| `.cydf` profile and responsibility | Accepted |
| CYKV 1 parser usable by an explicit caller | Implemented |
| CYKV 2 parser, source resolver, resolved writer, and canonical hash | Implemented in Tier1 |
| Dedicated CYDF file dispatch | Not implemented |
| Generic gameplay CYDF schemas | Not implemented |
| CYDF include/base resolution through an explicit CYKV source call | Implemented in Tier1 |
| Node provenance, deterministic dependency manifest, and resolution hash | Not implemented |
| Universal CYDF cooker/runtime binary | Deliberately undefined |
| `.cydata` / `.cydata_c` | Withdrawn proposal |

The first implementation gate is one real domain schema with a producer,
validator, typed decoder, consumer, limits, examples, and tests. The preferred
first exercise is a small tool or gameplay-data schema that proves CYKV 2
resolution without coupling adoption to renderer or world-runtime work. That
consumer must define how dependency edges enter validation, diagnostics, and
its build-cache identity; the current generic profile does not supply a cooker
or registry automatically.
