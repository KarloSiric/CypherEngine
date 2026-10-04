<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYGAME.md
//  Purpose: Specifies game profiles (`.cygame`, cypher.game V1): what one
//           game adds to the engine's map format - its entity classes with
//           their properties, inputs, outputs, and editor look, its
//           game-specific map settings, and editor defaults.
//  Details: Identity in ADR 0009. The editor's equivalent of a per-game
//           entity definition file (Hammer's FGD, Radiant's entity defs),
//           in CYKV. Source and tests are authoritative if they diverge.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27 (split from EDITOR_FORMATS.md,
//    with helpers, property extras, includes, and the REAP example)
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Game Profiles (`.cygame`, V1)

## 1. What it is for

The engine does not know what a player start, a wave spawner, or a door is;
each game decides. The game profile is where a game says so:

- **The map stores instances.** "An `info_player_start` at (128, -64, 16)
  facing east, team red" is map data ([CYMAP.md](CYMAP.md)).
- **The game profile stores definitions.** "`info_player_start` is a point
  entity; it has a `team` property with choices any/red/blue defaulting to
  any; it fires `on_spawn`; draw it as a green 32 x 32 x 72 box with the
  player model; list it under Info" is game profile data.

With the profile, Mason can offer the entity browser, place entities with
their defaults, show a properties panel with the right editor for each value
(dropdowns, colour pickers, asset pickers, target pickers), draw helpers
(light radius, spot cone, path links), validate maps (a trigger whose target
matches nothing, a wave number out of range), and autocomplete entity I/O.
Without it, entities are still loaded, kept, and saved, but shown as generic
magenta boxes with raw key/value editing.

One profile per game (`game/reap.cygame`); the project names it
([CYPROJECT.md](CYPROJECT.md)) and every map records its `game` and
`game_version`. A large game splits its classes over several files with
`includes`.

## 2. Document

```cykv
@cykv 1
@schema "cypher.game" 1
{
    id = "reap"
    name = "REAP"
    version = 3u
    units_per_meter = 39.37
    includes = [ "game/entities/logic.cygame", "game/entities/enemies.cygame" ]

    editor = {
        default_point_class = "info_player_start"
        default_solid_class = "func_detail"
        categories = [ "Info", "Waves", "Enemies", "Lights", "Triggers", "Doors", "Props", "Logic" ]
        sprites = "materials/editor/sprites"
    }

    map_settings = [
        { key = "wave_count" type = "integer" default = 10 min = 1 max = 100 label = "Waves" group = "Waves" },
        { key = "wave_intermission" type = "real" default = 8.0 min = 0.0 max = 120.0 unit = "seconds" label = "Intermission" group = "Waves" },
        { key = "difficulty" type = "choice" default = "normal" choices = [ "easy", "normal", "hard", "nightmare" ] }
    ]

    classes = [
        {
            name = "targetable"
            kind = "abstract"
            properties = [
                { key = "start_disabled" type = "bool" default = false label = "Start Disabled" group = "Logic" }
            ]
            inputs = [ { name = "enable" }, { name = "disable" }, { name = "toggle" } ]
        },
        {
            name = "info_player_start"
            kind = "point"
            label = "Player Start"
            category = "Info"
            description = "Where a player spawns at the start of the match."
            editor = { size = [ -16, -16, 0, 16, 16, 72 ] color = "#40c040" model = "models/player/reaper.cymodel" helpers = [ { type = "arrow" } ] }
            properties = [
                { key = "team" type = "choice" default = "any" choices = [ "any", "red", "blue" ] label = "Team" },
                { key = "priority" type = "integer" default = 0 min = 0 max = 100 label = "Priority" description = "Higher starts are used first." }
            ]
            outputs = [ { name = "on_spawn" description = "A player spawned here." } ]
        },
        {
            name = "info_wave_start"
            kind = "point"
            base = [ "targetable" ]
            label = "Wave Start"
            category = "Waves"
            description = "Begins a wave: enables its spawn group and starts the countdown."
            editor = { size = [ -24, -24, -24, 24, 24, 24 ] color = "#e05050" icon = "materials/editor/sprites/wave.cymat" }
            properties = [
                { key = "wave" type = "integer" default = 1 min = 1 max = 100 label = "Wave" },
                { key = "spawn_group" type = "string" default = "" label = "Spawn Group" description = "Enemy spawns with this group name activate." },
                { key = "delay" type = "real" default = 0.0 min = 0.0 max = 600.0 unit = "seconds" label = "Delay" },
                { key = "announce" type = "asset" default = "" extensions = [ ".cysnd" ] label = "Announcer Line" }
            ]
            inputs = [ { name = "start" }, { name = "abort" } ]
            outputs = [ { name = "on_started" }, { name = "on_cleared" }, { name = "on_failed" } ]
        },
        {
            name = "info_enemy_spawn"
            kind = "point"
            base = [ "targetable" ]
            label = "Enemy Spawn"
            category = "Enemies"
            editor = { size = [ -16, -16, 0, 16, 16, 64 ] color = "#c05030" model_from = "enemy" helpers = [ { type = "arrow" } ] }
            properties = [
                { key = "enemy" type = "choice" default = "grunt" choices = [ "grunt", "stalker", "brute", "sniper" ] label = "Enemy" },
                { key = "spawn_group" type = "string" default = "" label = "Spawn Group" },
                { key = "count" type = "integer" default = 1 min = 1 max = 64 label = "Count" },
                { key = "flags" type = "flags" default = [ "face_player" ] choices = [ "face_player", "silent", "no_drop", "boss" ] label = "Flags" }
            ]
            outputs = [ { name = "on_spawned" }, { name = "on_all_dead" } ]
        },
        {
            name = "light"
            kind = "point"
            base = [ "targetable" ]
            label = "Point Light"
            category = "Lights"
            editor = { size = [ -8, -8, -8, 8, 8, 8 ] color_from = "color" icon = "materials/editor/sprites/light.cymat" helpers = [ { type = "radius" property = "range" } ] }
            properties = [
                { key = "color" type = "color" default = "#ffffff" label = "Colour" },
                { key = "intensity" type = "real" default = 300.0 min = 0.0 max = 100000.0 unit = "candela" label = "Intensity" },
                { key = "range" type = "real" default = 512.0 min = 1.0 max = 65536.0 unit = "units" label = "Range" },
                { key = "cast_shadows" type = "bool" default = true label = "Cast Shadows" }
            ]
            inputs = [ { name = "turn_on" }, { name = "turn_off" }, { name = "set_color" type = "string" } ]
        },
        {
            name = "trigger_once"
            kind = "solid"
            base = [ "targetable" ]
            label = "Trigger Once"
            category = "Triggers"
            editor = { color = "#e0a030" material = "materials/editor/trigger.cymat" }
            properties = [
                { key = "filter" type = "choice" default = "players" choices = [ "players", "enemies", "everything" ] label = "Filter" }
            ]
            outputs = [ { name = "on_trigger" }, { name = "on_start_touch" } ]
        },
        {
            name = "func_door"
            kind = "solid"
            base = [ "targetable" ]
            label = "Door"
            category = "Doors"
            editor = { color = "#8090ff" helpers = [ { type = "move_direction" property = "move_dir" distance = "lip" } ] }
            properties = [
                { key = "move_dir" type = "angles" default = [ 0.0, 90.0, 0.0 ] label = "Move Direction" },
                { key = "speed" type = "real" default = 200.0 min = 1.0 max = 10000.0 unit = "units/s" label = "Speed" },
                { key = "lip" type = "real" default = 8.0 label = "Lip" },
                { key = "wait" type = "real" default = 3.0 min = -1.0 unit = "seconds" label = "Close Delay" description = "-1 stays open." },
                { key = "open_sound" type = "asset" default = "" extensions = [ ".cysnd" ] label = "Open Sound" },
                { key = "locked" type = "bool" default = false label = "Locked" }
            ]
            inputs = [ { name = "open" }, { name = "close" }, { name = "lock" }, { name = "unlock" } ]
            outputs = [ { name = "on_open" }, { name = "on_close" }, { name = "on_locked_use" } ]
        },
        {
            name = "prop_static"
            kind = "point"
            label = "Static Prop"
            category = "Props"
            editor = { model_from = "model" size_from = "model" }
            properties = [
                { key = "model" type = "asset" default = "" extensions = [ ".cymodel" ] label = "Model" required = true },
                { key = "skin" type = "integer" default = 0 min = 0 label = "Skin" },
                { key = "solid" type = "choice" default = "vphysics" choices = [ "none", "bounds", "vphysics" ] label = "Collision" },
                { key = "fade_distance" type = "real" default = 0.0 min = 0.0 unit = "units" label = "Fade Distance" }
            ]
        },
        {
            name = "logic_relay"
            kind = "point"
            base = [ "targetable" ]
            label = "Logic Relay"
            category = "Logic"
            editor = { size = [ -8, -8, -8, 8, 8, 8 ] icon = "materials/editor/sprites/relay.cymat" }
            inputs = [ { name = "trigger" } ]
            outputs = [ { name = "on_trigger" } ]
        }
    ]
}
```

## 3. Members

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | string | yes | stable identifier; what projects and maps reference |
| `name` | string | yes | display name |
| `version` | u64 | yes | bumped when classes or properties change incompatibly; maps record it and migrate through it |
| `units_per_meter` | f64 | no | default for new maps |
| `includes` | array | no | canonical virtual paths of more `.cygame` files whose `classes` and `map_settings` join this profile; included files need no `id`, `name`, or `version` and may include others (16 deep, no cycles) |
| `editor` | object | no | editor defaults (below) |
| `map_settings` | array | no | property definitions for the map's `settings.game` section |
| `classes` | array | no | entity class definitions (below) |
| `migrations` | array | no | renames applied to maps saved against an older `version` (section 6) |

**`editor`:** `default_point_class`, `default_solid_class`, `categories`
(entity browser order; unlisted categories follow alphabetically),
`sprites` (folder of entity sprites).

## 4. Classes

| Member | Rules |
| --- | --- |
| `name` | lower-case identifier, unique across the profile and its includes |
| `kind` | `point` (has an origin), `solid` (owns brushes: triggers, doors, detail), `abstract` (a base only, never placed) |
| `base` | classes whose properties, inputs, and outputs are inherited in order, a later definition overriding an earlier one by key |
| `label`, `category`, `description` | entity browser and help text |
| `editor` | how the class looks in the editor (below) |
| `properties` | property definitions |
| `inputs`, `outputs` | `name`, optional `type` (`void` by default, or `bool`, `integer`, `real`, `string`, `target`, `vector`) and `description` |

**`editor`:** `size` (`[minx, miny, minz, maxx, maxy, maxz]` box for point
entities without a model), `color` or `color_from` (a colour property),
`model` or `model_from` (an asset property), `size_from` (take the box from
that model), `icon` (sprite material drawn in the views), `material` (face
material shown on the class's brushes, e.g. trigger stripes), `helpers`:

| Helper `type` | Draws | Parameters |
| --- | --- | --- |
| `arrow` | facing direction from `angles` | - |
| `radius` | sphere | `property` (a real) |
| `cone` | spot cone | `angle` (a real property), `range` (a real property) |
| `line_to` | line to the entity named by a property | `property` (a target) |
| `move_direction` | door and platform travel | `property` (angles), `distance` (a real property) |
| `box` | box of a size property | `property` (a vector) |

## 5. Property definitions

| Member | Rules |
| --- | --- |
| `key` | lower-case identifier, unique in the class after inheritance |
| `type` | one of the types below |
| `default` | value of that type; new entities are created with every default written out |
| `label`, `description`, `group` | properties panel text and section |
| `min`, `max` | numeric limits |
| `choices` | allowed strings for `choice` and `flags` |
| `extensions` | allowed asset file types, e.g. `[ ".cymat" ]` |
| `unit` | display unit (`units`, `seconds`, `degrees`, ...) |
| `required` | a map is flagged when the value is empty |
| `readonly`, `hidden` | editor presentation only |
| `target_classes` | for `target`, `object`, and `objects`: classes the referenced entities should belong to |

| Type | Value in maps | Editor |
| --- | --- | --- |
| `bool` | Boolean | checkbox |
| `integer` | integer | spin box, `min`/`max` |
| `real` | number | spin box, `min`/`max`, `unit` |
| `string` | string | text field |
| `choice` | string | dropdown of `choices` |
| `flags` | array of strings | checkboxes of `choices` |
| `color` | `"#rrggbb"` | colour picker |
| `vector` | `[x, y, z]` | three fields |
| `angles` | `[pitch, yaw, roll]` | three fields and a view picker |
| `asset` | canonical virtual path | asset picker filtered by `extensions` |
| `target` | string | entity-name picker with autocomplete |
| `object` | u64 | picker for any map object by ID (an entity, brush, shape, ...) |
| `objects` | array of u64 | multi-object picker |
| `shape` | u64 | picker for a shape (patrol routes, rails, rivers) |
| `faces` | array of u64 | brush-face picker in the views (overlays, decals projected onto faces) |

`target` names an entity by its target name, so several entities can answer
and renaming the target reconnects nothing silently. `object`, `objects`,
`shape`, and `faces` name map IDs, so they follow the object through renames
and are written with the `u` suffix like every ID; a reference to an ID the
map no longer has is reported by the validator and kept.

## 6. Migrations

```cykv
migrations = [
    { version = 3u rename_class = { from = "info_wave" to = "info_wave_start" } },
    { version = 3u rename_property = { class = "info_enemy_spawn" from = "type" to = "enemy" } }
]
```

When a map saved against version 2 opens with version 3, the listed renames
apply in order, the change is reported, and the map records version 3 on its
next save.

## 7. Validation

Decoding is tolerant: an unusable class or property definition is skipped and
reported; a base cycle or a missing base is reported and the class keeps what
resolves. Duplicate class names across includes are reported and the first
definition wins.

## 8. Limits

4,096 classes, 256 properties, 128 inputs, and 128 outputs per class, base
chains 16 deep, includes 16 deep, 16 MiB per file.
