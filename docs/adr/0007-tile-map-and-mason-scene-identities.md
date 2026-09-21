# ADR 0007: Tile Map and Mason Scene Identities

**Status:** Accepted
**Date:** 2026-09-18

## Context

CypherTileEditor already reads and writes a stable, bounded source family:

```text
.cymap
@cykv 1
@schema "cypher.map" 1, 2, or 3
```

The current writer emits schema version 3. The document is a dense tile grid
with dimensions, metrics, cells, markers, and material bindings. Its deliberately
small model is useful for rapid blockouts and test maps.

Mason needs a different authored document model. A production three-dimensional
scene must eventually represent arbitrary editable topology, hierarchy, layers,
entities and components, prefabs, lights, triggers, audio, environment data, and
compiler metadata. Extending `cypher.map` until it also means that unrelated
scene graph would make one extension select two validators, two editors, and two
compilers. Content inspection would then be required before basic file routing.

The repository already reserves `.cyscene` for Mason Scene/World authoring and
`CypherSceneCompiler`, so no new format name is required.

## Decision

The two authored formats have permanently distinct identities:

| Purpose | Extension | Schema | Owner | Current state |
| --- | --- | --- | --- | --- |
| Tile construction | `.cymap` | `cypher.map` | CypherTileEditor | V1-V3 readable; V3 written |
| General 3D world authoring | `.cyscene` | `cypher.scene` | Mason | V1 planned, fields not frozen |

They may share the CYKV parser, schema registry, source diagnostics, editor
geometry library, compiler utilities, and a validated compiler-facing world
snapshot. They do not share a source schema, editor document model, version
history, validator, or extension-based compiler registration.

Mason may provide an explicit **Convert Tile Map to Scene** command. That command
creates a new `.cyscene`, records the source `.cymap` as provenance, converts tile
cells and markers deterministically, and leaves the input unchanged. Once the
scene gains arbitrary edits, it is not required to round-trip back to tiles.

TileEditor rejects `.cyscene`. Mason may list `.cymap` as an import type, but it
must not open a tile map as if it were already a Mason scene or save scene data
over the source. Tools validate the extension and `@schema` pair exactly; they do
not guess a dialect from fields inside the root object.

Cooked identities remain separate while their runtime consumers are unproven:

```text
.cymap   -> future tile-map cooker, if a tile runtime product is required
.cyscene -> CypherSceneCompiler -> .cyscene_c -> CypherWorld
```

No cooked FourCC or binary payload is frozen by this decision. A cooked format is
admitted only when it has a writer, bounded reader, runtime consumer, compatibility
rules, and tests.

## Consequences

- Existing `.cymap` V1-V3 fixtures and user documents keep their exact meaning.
- TileEditor remains a small grid tool instead of becoming a second Mason.
- Mason can evolve a professional scene schema without inheriting tile-grid
  compatibility constraints.
- Extension routing remains deterministic for the asset browser, command line,
  file associations, source control, and compiler registry.
- Shared editor geometry code does not imply a shared persisted document.
- Project manifests will eventually need an explicit startup resource type before
  `.cyscene` can replace a TileEditor map as the game startup world.

## Rejected alternatives

### Reuse `.cymap` for both documents

Rejected because one suffix would identify unrelated schemas and compiler paths.
Schema sniffing would move ambiguity into every editor, build tool, and project
manifest consumer.

### Rename existing tile maps to `.cytilemap`

Rejected because `.cymap` already has implemented readers, a V3 writer, fixtures,
tests, and project-manifest validation. The repository already has the unused and
more accurate `.cyscene` reservation for Mason.

### Store Mason topology in a future `cypher.map` version

Rejected because the change would be a different document model rather than an
incremental tile-map schema revision. Explicit conversion provides a reviewable
boundary and preserves both tools' strengths.
