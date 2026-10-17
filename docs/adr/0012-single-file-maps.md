# ADR 0012: Single-File Maps

**Status:** Accepted
**Date:** 2026-10-12
**Supersedes:** "The full map is chunked" in
[ADR 0009](0009-editor-and-map-file-identities.md), and "Root plus chunk
documents, discovered by directory" and "Deterministic chunk assignment" in
[ADR 0011](0011-map-source-format.md). The rest of ADR 0011 (map-owned
readable geometry, IDs, tolerance, entities, compact layout, `info`) stands.

## Context

`cypher.map` V10 split a map into a root `.cymap` and a folder of
`.cymapchunk` files, one per layer and spatial cell, with their own schema
(`cypher.map_chunk`). In use, the split worked against the format's first
goal - a file a person opens and sees the level in:

- Opening the `.cymap` showed identity and settings and nothing of the
  level. Reading a map meant finding a folder, knowing the cell naming, and
  reassembling the level from many files in your head.
- Every tool that touched maps (Mason, the asset browser, the map compiler,
  autosave, Save As) had to discover, stage, rename, and delete a set of
  files, and track which of them a document "owned" across undo and failed
  saves. Most of the save code and its tests existed for that bookkeeping.
- A second schema, its own problem codes (foreign map, wrong path, invalid
  cell, two files for one cell), and a cell grid setting existed only to
  serve the split.

The split's reasons were parser budgets for very large maps, merging in
version control, live reload of one part, and damage isolation.

## Decision

**A map is one file.** `cypher.map` V11 holds the whole level: identity,
shared tables, settings, and then every layer with its objects nested inside
it in sections by kind (`entities`, `brushes`, ... `prefabs`) - the same
sections a V10 chunk had. The field-level contract is
[CYMAP.md](../formats/CYMAP.md).

- **Objects nest in their layers** rather than carrying a `layer` member in
  flat, map-wide sections. The file then reads the way the editor is
  organised (Layers panel, Outliner); a layer reads, reviews, and copies as
  one block; and the reader hands each layer to the editor without sorting.
  TrenchBroom writes layers the same way. The cost - moving an object to
  another layer moves its lines rather than changing one - is paid on a rare
  operation.
- **Order and layout stay deterministic**: small shared tables first, the
  level last, objects sorted by ID within their section, short records on
  one line. The layout width grows from 200 to 208 columns so every record
  that fit on one line in a V10 chunk still does despite the two extra
  nesting levels.
- **Summaries live on layers only.** Each layer has an `info` block (counts
  and bounds) for readers. The map has none: a whole-map summary would
  change with every edit and make any two people's changes conflict on one
  line. Tools that want totals add up the layers.
- **The header is the magic and version.** `@cykv 1` must be the file's
  first bytes and `@schema "cypher.map" 11` the next line, so a tool tells a
  map and its version from the first bytes before reading anything else.
  Maps written in CYKV 2 by hand are read; Mason saves resolved CYKV 1 until
  CYKV gains lossless saving.
- **Bounds are the map's own.** The map parses with its own limits - 128 MiB
  of text and 8,388,608 values, about 66,000 box brushes - instead of the
  settings loader's 16 MiB / 2^20. They are bounds against corrupt or
  hostile files, not design targets; the writer refuses what the reader would
  reject, and the reader checks the file size before reading.
- **V10 is retired, not migrated on load.** Mason and the map compiler read
  V11 only; a V10 root is refused with `VERSION_UNSUPPORTED` naming the
  version. The repository's maps were converted once, in place, when V11
  landed. Keeping a V10 reader would keep the whole chunk machinery alive
  for files that no longer exist.

In memory nothing about editing changes: each layer's sections are kept as
their own tree (a layer store) beside the geometry document, exactly as each
chunk's tree was. Only persistence changed: `MapDocument_Load` takes the
file's text, `MapDocument_Save` produces it, and `MapDocument_BuildTree`
gives views (the Properties panel's "Map file") the tree a save would write
without saving.

## Consequences

- **Merging** is line-based within one file. Two people editing different
  layers, or different objects in one layer, change different lines; the
  shared tables and settings change rarely. Concurrent edits to one layer
  can both touch that layer's `info` line, which is ignored on read and
  recomputed on save, so either side resolves it.
- **Live reload** reloads the file. Damage isolation is per object and per
  section: an unreadable object or a section that is not a list is kept as
  written and reported; a syntax error refuses the open and names its line,
  instead of silently loading part of a level.
- **Saving** writes `<file>.tmp` and replaces the map atomically. There is
  no chunk inventory, no deletion of emptied files, no recovery bookkeeping
  after a partly published save, and no folder beside the map.
- **Large maps** load as one parse. The limits allow levels far beyond the
  current target (arena-scale maps are tens of kilobytes to a few
  megabytes); a level that approaches them should become several maps.
- The cell grid (`cell_size`, the `editor.map.cell_size` setting) is gone.
- Old `.cymapchunk` folders left beside converted maps are ignored by every
  tool and can be deleted.

## Rejected alternatives

- **Keep chunks, add an index to the root** so opening the root lists the
  level: still many files and the same bookkeeping, and the index conflicts
  on every parallel change and goes stale when files move (already rejected
  in ADR 0011).
- **Flat sections with a `layer` member on each object**: a layer change is
  one line, but the file no longer reads by layer, entity-owned geometry
  would need a rule for its `layer` member, and the reader must sort every
  object into layers.
- **Migrate V10 on load**: convenient for old files, but it keeps two
  readers and all of the chunk code for maps that were converted once.
