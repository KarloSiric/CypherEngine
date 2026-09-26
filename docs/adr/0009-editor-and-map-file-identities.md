# ADR 0009: Editor and Map File Identities

**Status:** Accepted
**Date:** 2026-09-25
**Supersedes:** the naming decisions of
[ADR 0007](0007-tile-map-and-mason-scene-identities.md). ADR 0007's core
decision still stands: tile maps and full maps are separate document models
with separate compilers, joined only by explicit conversion.

## Context

ADR 0007 kept `.cymap` for TileEditor grids and reserved `.cyscene` for the
Mason map. The project owner has decided the reverse naming: the full map - the
large document Mason edits - is `.cymap` with schema `cypher.map`, and tile maps
become `.cytilemap` with schema `cypher.tilemap`. A hand-written
`assets/tilemaps/test1.cytilemap` already uses the new tile identity.

The editor frontend ([ADR 0008](0008-editor-framework-and-workspace-layout.md))
also needs persistent settings, themes, fonts, keyboard shortcuts, dock
layouts, projects, and workspaces. Workspaces must later support networking -
peers browsing and copying each other's maps - and MCP automation, where an AI
agent works in a developer's workspace.

Existing state: `cypher.project` V1 and `cypher.settings` V1 are implemented in
CypherCommon Tier2 with tests but are not loaded by any product. `.cycfg` is a
documented alias of `.cfg`, the command/CVar script family. TileEditor writes
`.cytheme` and `editor.ini` as Qt INI files.

## Decision

### File identities

| Extension | Schema | Owner | Holds |
| --- | --- | --- | --- |
| `.cymap` | `cypher.map` V10 and later | Mason map workspace | The full map: root document plus chunk documents |
| `.cytilemap` | `cypher.tilemap` V1 | CypherTileEditor | Tile maps (content model of legacy `cypher.map` V3) |
| `.cyproject` | `cypher.project` V2 | CypherCommon Tier2 | Project identity, content roots, start map, team settings |
| `.cyworkspace` | `cypher.workspace` V1 | EditorCore | One developer's working area on one machine |
| `.cysettings` | `cypher.settings` V2 | CypherCommon Tier2 | Sectioned settings (engine and editor) |
| `.cytheme` | `cypher.theme` V1 | EditorCore | A complete editor colour scheme: every UI, viewport, grid, gizmo, and console colour, plus fonts and metrics |
| `.cykeymap` | `cypher.editor_keymap` V1 | EditorCore | Editor shortcuts: editor command to key chords per editor context |
| `.cylayout` | `cypher.layout` V1 | EditorCore | Named dock layouts |
| `.cyfont` | `cypher.font` V1 | CypherCommon Tier2 | Font family definition shared by editor and runtime |

Unchanged: `.cfg`/`.cycfg` remain command/CVar scripts, never settings.

### Editor keymaps and game input are separate families

| | Editor shortcuts | Game input |
| --- | --- | --- |
| Files | `.cykeymap` | `.cyinput` (project action map and defaults), `.cybindings` (player overrides) |
| Schema | `cypher.editor_keymap` | `cypher.input`, `cypher.input_bindings` ([Input Actions](../formats/INPUT_ACTIONS.md)) |
| Binds | editor commands (`file.save`, `map.tool.clip`) per editor context | gameplay actions (`jump`, `fire`) per input context |
| Devices | keyboard chords and chord sequences | keyboard, mouse, wheel, gamepads, axes, composites |
| Read by | the editor's command system only | the runtime Input system only |

Distinct schema IDs mean each loader rejects the other family by its header.
The editor's Play mode runs the game with the game's own bindings; the editor
never reinterprets them as editor shortcuts, and a game never loads a
`.cykeymap`.
`.cyscene` is released; it may be reused later for map chunk or sub-scene files
when the map schema is designed.

### `cypher.map` version lineage

`cypher.map` V1, V2, and V3 are the legacy tile-map lineage. They are never
written again. TileEditor reads them through an explicit legacy import and
saves `.cytilemap`. The full map starts at `cypher.map` **V10**; V4-V9 are
never assigned. A reader decides what a file is from its `@schema` header
alone:

```text
@schema "cypher.map" 1..3   legacy tile map -> TileEditor import
@schema "cypher.map" 4..9   never written   -> rejected
@schema "cypher.map" 10..   full map        -> map reader
@schema "cypher.tilemap" 1  tile map        -> TileEditor
```

No reader guesses a document's kind from its content. For CYKV text formats
the `@cykv` and `@schema` lines are the file's magic and version; versions
then increase by one within a lineage. Cooked binaries keep the existing
convention of a four-character magic plus version inside a CYRS container
(as `CYSH`, `CYTX`, and `CYMT` do); `.cymap_c` receives its magic, starting at
version 10 to match, when the map compiler is specified.

### One map format for every game

`.cymap` is the engine's standard map format regardless of the game. Games do
not fork it. A map names the game profile it targets; the game supplies its
definitions (entity classes, their key/value schemas, game-specific map
settings) and validates its own sections of the map, in the way a per-game
entity definition file extends one shared level format. Engine-owned sections
stay identical across games, so tools, compilers, and the editor read every
game's maps. When a game's definitions change, the map keeps values it no
longer recognises and reports them rather than dropping them.

### The full map is chunked

A large map cannot be one text document: the CYKV parser's default policy
bounds a document at 64 MiB and about one million values, a single file loads
serially, and it cannot be merged. A `.cymap` is therefore a root document
(identity, game profile, map settings, layer and chunk index) plus chunk
documents that parse independently, with bulk arrays stored as binary blobs
and every object keyed by a stable ID in deterministic order. The chunk layout
is specified with the map schema; no single-document map loader is written.

### Scopes

| Scope | File | Location | Shared |
| --- | --- | --- | --- |
| Built-in | compiled defaults, bundled `.cytheme`/`.cykeymap`/`.cylayout` | editor resources | - |
| User | `editor.cysettings`, user themes, keymaps, layouts, recent workspaces | per-user config directory | no |
| Project | `<id>.cyproject` | project root | committed |
| Workspace | `<name>.cyworkspace` | workspace directory | never committed to the project |

A value resolves from the most specific scope that holds a valid value:
workspace, then project, then user, then built-in. The settings UI shows the
scope each value comes from and where an edit is saved.

A **project** is what the game is: identity, the game profile it builds for,
content roots, start map, default map settings, and settings the team shares.
Game-specific settings live in sections named by the game profile, so one
project file format serves every game. A **workspace** is where one developer works: which
project, an optional workspace content root, personal settings overrides, and
remembered state (open maps, layout, camera bookmarks). When a workspace has a
content root, the editor VFS mounts it above the project's content roots, so
work-in-progress maps saved there overlay the project until promoted; without
one, the workspace edits the project content directly.

### Persistence guarantees

These apply to every document above except the map formats:

- **Sparse.** Only values that differ from their inherited value are written,
  so improved defaults reach users while their own choices stay.
- **Per-value fallback.** An invalid or out-of-range value falls back for that
  value alone and produces a warning; the rest of the document still applies.
- **Unknown members are preserved** and written back, so a newer build's data
  survives a save by an older build.
- **Explicit migrations** carry renamed or restructured values forward.
- **Atomic save** (write a temporary file, then replace) keeping one `.bak`.
  Changes save when made.
- **A file that cannot be parsed is never lost.** It is first copied to
  `.broken`; the editor then loads the `.bak`, or, without a usable backup,
  runs that scope on inherited values, reports the error, and does not write
  the scope until the user repairs, restores, or accepts the overwrite.

Only an unparseable file or a wrong schema identity rejects a whole document.
This deliberately replaces `cypher.settings` V1's closed root, where one
unknown member failed the entire file.

### Themes cover everything

A theme is an open set of named tokens. Every colour, font, and metric the
editor draws with - panels, text, accents, viewport backgrounds, grid lines,
selection, gizmo axes, brush and mesh wireframes, console message kinds - is a
registered token with a built-in default, and a `.cytheme` may override any of
them. A theme names a base theme and stores only its differences, so a user
theme stays valid when new tokens are added.

### Networking and automation (planned, not frozen)

- `cypher.workspace` V1 carries a stable workspace `id` (UUID), a display
  `name`, and an `owner`, so a peer or an agent can address a workspace.
- The member names `sharing` and `automation` are reserved for later workspace
  versions. Planned meaning: `sharing` publishes chosen workspace paths
  read-only to peers; pulling a map copies it into the receiving workspace,
  either keeping its identity or as a copy with remapped IDs. `automation`
  grants an MCP agent an explicit allow-list of command modules for that
  workspace.
- Every automated or remote edit enters through the editor command system, so
  it is validated, undoable, and logged like a user's edit.

Field-level contracts: [Editor And Settings Formats](../formats/EDITOR_FORMATS.md).

## Consequences

- TileEditor, its tests, fixtures, the project manifest documentation, and the
  format catalog move to the new tile identity; existing `cypher.map` V1-V3
  files keep opening through the legacy import.
- `cypher.project` V2's `start_map` names a full `.cymap`; V1 documents remain
  readable.
- Settings-family decoders become tolerant and preserving, which changes
  `cypher.settings` behaviour and its tests.
- Map chunking must be designed before the first full-map writer exists.

## Rejected alternatives

### A new schema ID for the full map

For example `cypher.scene`, `cypher.world`, or `cypher.level`. Rejected by the
project owner: the map is `cypher.map`. Starting at V10 removes the ambiguity a
fresh ID would otherwise avoid.

### Recognising legacy tile maps by content

Rejected: the format catalog forbids byte-shape guessing, and a V1 header
would be ambiguous.

### `.cycfg` for settings

Rejected: `.cycfg` is the command-script grammar; settings need a typed,
sectioned document.

### Personal state inside `.cyproject`

Rejected: every layout change would modify a committed file, and several
developers or agents working on one project would overwrite each other's
state.
