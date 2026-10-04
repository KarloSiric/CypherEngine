# ADR 0011: Map Source Format

**Status:** Accepted
**Date:** 2026-09-27, revised 2026-09-29 (see "Revision 1")

## Context

ADR 0009 fixed the map's identity - `.cymap`, schema `cypher.map` from V10,
one format for every game, chunked - and left the chunk layout to be
specified before the first writer. The map must be human-readable, handle very
large levels, merge in version control, travel between workspaces and peers,
and carry every piece of geometry the editor can author. The Mason
design ([map_authoring_and_mason.md](../map_authoring_and_mason.md)) requires
stable IDs, entities with properties for lights, sounds, and triggers, external
asset references, and personal state kept out of the shared file.

## Decision

The field-level contract is [CYMAP.md](../formats/CYMAP.md).

### Root plus chunk documents, discovered by directory

A map is one root document (`cypher.map` V10) and chunk documents
(`cypher.map_chunk` V10, `.cymapchunk`) in a directory named after the root.
Each chunk holds one layer's objects in one spatial cell. Chunks are found by
scanning the directory; the root does not index them. Two people who add
chunks therefore never conflict on the root, and a chunk can be added,
removed, or restored as a file.

### Deterministic chunk assignment

An object's chunk follows from its layer and position (`floor(p / cell_size)`
per cut axis), so every editor writes the same chunk set. Objects without a
position live in the layer's `global` chunk; owned geometry follows its
entity.

### IDs: document-local u64, shared with geometry

Every object ID is a nonzero u64 unique within the map, the same space the
Geometry library uses for its source IDs; the root records `next_id` so no ID
is ever reused. Copying objects between maps or workspaces remaps IDs
explicitly, as geometry fragments already do. 128-bit GUIDs were considered:
they avoid collisions when two branches add objects independently, but they
would give geometry and map objects two identity schemes and roughly triple
the size of every reference. Concurrent editing of one map by several people
is a later feature; it will allocate disjoint ID blocks per session, which fits
this scheme.

### Source only

No derived data is read: no chunk index, hashes, dependency lists, or bounds
caches. Tools recompute them, so source files never go stale and never
conflict over caches. The one exception is `info` (Revision 1): summaries
written for people and ignored on read. Geometry is text, never binary
blobs, which would not be reviewable or mergeable.

### Nothing is lost

Readers keep unknown members at every level and write them back. An object
that cannot be read is kept verbatim and reported. A chunk that cannot be
parsed is left untouched on disk and saving it is blocked. Duplicate IDs open
the map read-only.

### Entities carry gameplay

Lights, sounds, triggers, spawn points, and models are entities: a class from
the game profile, typed properties, and outputs wired to target names, as in
Source's entity I/O. Brush entities contain the geometry they own
(Revision 1). Game-specific
variation lives in the game profile (`.cygame`, see
[CYGAME.md](../formats/CYGAME.md)) and in `settings.game`, never
in forks of the map schema.

### Readable conventions

Positions are world-space f64; rotations are `angles` in degrees (pitch, yaw,
roll), the values designers type; writers use a fixed member order and sort
objects by ID so unchanged maps save byte-identically.

## Revision 1 (2026-09-29): a map that reads as a level

The first draft embedded the Geometry library's own document serialization
in each chunk. That kept the code small but produced text no designer could
read: separate `normal_x`/`normal_y`/`normal_z`/`d` members, faces pointing
into a separate surface list by index, mesh positions as long flat arrays,
materials as numbers, 17-digit reals, and 8.5 KB per box brush. The goal is a
file a person can open and see the level in, so:

- **The map owns its geometry representation.** Brushes are faces with
  `plane = [ nx, ny, nz, distance ]`, a material path, and the texture
  mapping inline; meshes are `[x, y, z]` vertices and faces of vertex
  indices; patches are control grids row by row; terrains are height rows
  and paint-weight rows. The codec converts through the Geometry library's
  public construction and description API, so the map format - a promise to
  every level ever saved - no longer depends on the library's internal file
  format.
- **Brush entities contain their brushes**, as Hammer's VMF nests solids in
  entities: a trigger's volume sits inside the trigger.
- **Materials are paths on every face.** A chunk is self-contained and a
  text search for a material finds every use; the number the geometry
  library needs is an in-memory detail.
- **`info` annotations** (bounds, counts) on objects and chunks are written
  for readers and never read, so hand-editing one has no effect. The root has
  none: it would change with every edit and conflict constantly.
- **Compact layout and one spelling per value**: bare keys, shortest reals,
  short records on one line, sibling records laid out alike (one line per
  brush face), and format-defined members always written in their type. A
  box brush is now 1,269 bytes.
- **Hand editing is a first-class use.** Objects and parts written without
  IDs get fresh ones on load; duplicate IDs open read-only until "Reassign
  Duplicate IDs" reloads with fresh IDs for the later copies; unknown members
  are kept down to individual faces.
- **New sections** for what a level contains beyond entities and geometry:
  terrain paint, shapes (paths, rails, rivers), foliage (compact per-model
  instance lists placed per instance), notes, selection sets, cordons, and
  retained mesh modifiers - the last resolving the persistence gate recorded
  in the Geometry Modifiers README.
- **The writer refuses what the reader would reject**, including the
  reader's value budget (1,000,000 values per chunk), which dense brush work
  reaches before the text limit.

## Consequences

- The map reader/writer is an in-memory library (Map workspace core): callers
  pass the root text and chunk texts, and receive texts to write, so the
  editor, the scene compiler, and tests share it.
- ADR 0009's sentence about binary blobs is corrected to text arrays.
- The Geometry library's document serialization remains its own format for
  clipboard and tooling; maps do not use it.
- The scene compiler reads the same format; `.cymap_c` is specified with it.

## Rejected alternatives

- **One file per map** (Hammer's VMF): simple, but large maps exceed parser
  budgets and every edit touches the one file.
- **One file per object** (as some engines do for world partitioning): best
  for merges, but tens of thousands of tiny files for an arena-scale map and
  slow directory operations.
- **A chunk index in the root**: faster discovery, but it conflicts on every
  parallel chunk addition and goes stale when files move.
