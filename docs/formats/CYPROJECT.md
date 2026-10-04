<!--
//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: docs/formats/CYPROJECT.md
//  Purpose: Specifies projects (`.cyproject`, cypher.project V3): a game's
//           identity, content roots, game profile, team settings, editor
//           resources, build and cook profiles, and run configurations.
//  Details: Identity and scopes in ADR 0009. Shared by the runtime (mounts,
//           start map) and the editor (everything else); decoded by
//           CypherCommon Tier2. Source and tests are authoritative if they
//           diverge.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27 (split from EDITOR_FORMATS.md,
//    V3 with engine, paths, languages, editor, build, cook, run)
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////
-->

# Projects (`.cyproject`, V3)

A project is one game: REAP is `reap.cyproject`. It says where the game's
content lives and in what order it is mounted, which game profile defines
its entities, what the team agrees on (settings, layouts, keymaps, themes,
validation rules), how maps are compiled and packages cooked, and how to
launch the game to test a map. It is committed with the game.

## 1. File

`<project root>/<id>.cyproject`. Paths inside are either canonical virtual
paths (lower-case, forward slashes, relative to the mounted content) or,
where marked native, paths relative to the project root.

## 2. Document

```cykv
@cykv 1
@schema "cypher.project" 3
{
    id = "reap"
    name = "REAP"
    game = "reap"
    game_profile = "game/reap.cygame"
    engine = { min_version = "0.1.0" }
    start_map = "maps/facility.cymap"
    maps_path = "maps"
    search_paths = [ "game", "engine" ]

    paths = {
        build = "build"
        cache = ".cache"
        packages = "packages"
    }
    languages = { default = "en" supported = [ "en", "de", "hr" ] }

    settings = {
        editor = { grid = { size = 16 } map = { default_material = "materials/dev/dev_grid.cymat" } }
    }
    map_defaults = { gravity = 800.0 sky = "materials/sky/dusk.cymat" }

    editor = {
        layouts = "editor/layouts"
        keymaps = "editor/keymaps"
        themes = "editor/themes"
        plugins = "editor/plugins"
        templates = { map = "editor/templates/empty.cymap" }
        validation = { "entity.missing_target" = "error" "brush.tiny" = "warning" "material.missing" = "error" }
    }

    build = {
        default = "full"
        profiles = [
            {
                id = "fast"
                name = "Fast (no lighting)"
                steps = [
                    { tool = "cypher_map_compiler" arguments = [ "${map}", "--output", "${build}/maps" ] },
                    { tool = "cypher_navigation_compiler" arguments = [ "${build}/maps/${map_name}.cyscene" ] enabled = false }
                ]
            },
            {
                id = "full"
                name = "Full"
                steps = [
                    { tool = "cypher_map_compiler" arguments = [ "${map}", "--output", "${build}/maps", "--optimize" ] },
                    { tool = "cypher_navigation_compiler" arguments = [ "${build}/maps/${map_name}.cyscene" ] }
                ]
            }
        ]
    }

    cook = {
        platforms = [ "macos", "windows", "linux" ]
        profiles = [ { id = "release" name = "Release" compression = "zstd" strip_editor_data = true } ]
    }

    run = {
        default = "play"
        configurations = [
            { id = "play" name = "Play" executable = "${build}/bin/reap" arguments = [ "+map", "${map_name}" ] working_directory = "." },
            { id = "play_windowed" name = "Play (windowed)" executable = "${build}/bin/reap" arguments = [ "+map", "${map_name}", "-windowed", "-width", "1600", "-height", "900" ] working_directory = "." }
        ]
    }
}
```

## 3. Members

| Member | Type | Required | Rules |
| --- | --- | --- | --- |
| `id` | string | yes | stable identifier, 1-64 bytes |
| `name` | string | yes | 1-128 bytes |
| `game` | string | no | stable identifier of the game profile, 1-64 bytes; must equal the profile's `id` |
| `game_profile` | string | no | canonical virtual path of the `.cygame` ([CYGAME.md](CYGAME.md)); default `game/<game>.cygame` |
| `engine` | object | no | `min_version` (`major.minor.patch`): the editor refuses to save with an older engine and warns on open |
| `start_map` | string | no | canonical virtual path ending `.cymap` |
| `maps_path` | string | no | canonical virtual path; default `maps` |
| `search_paths` | array | no | 1-64 unique canonical virtual mount roots, highest priority first |
| `paths` | object | no | native, relative to the project root: `build` (compiler output, default `build`), `cache` (derived data, default `.cache`), `packages` (cooked packages, default `packages`) |
| `languages` | object | no | `default` language tag, `supported` tags (BCP 47 subset: `en`, `pt-br`) |
| `settings` | object | no | team settings scope ([CYSETTINGS.md](CYSETTINGS.md)); resolves between workspace and user |
| `map_defaults` | object | no | `settings.map` values for new maps ([CYMAP.md](CYMAP.md)) |
| `editor` | object | no | team editor resources (below) |
| `build` | object | no | map build profiles (below) |
| `cook` | object | no | packaging platforms and profiles |
| `run` | object | no | how to launch the game to test (below) |

**`editor`:** `layouts`, `keymaps`, `themes`, `plugins` (canonical virtual
folders holding team files of those kinds; defaults `editor/layouts`, ...),
`templates.map` (the map new maps copy), `validation` (rule ID to
`off` / `info` / `warning` / `error`; rules not listed use their built-in
severity).

**`build`:** `default` (profile ID) and `profiles`: `id`, `name`, `steps`.
A step runs one tool: `tool` (a registered Cypher tool name or a native
executable path), `arguments` (strings), optional `enabled` (default true),
`working_directory` (native), `environment` (object of strings). Steps run in
order and stop at the first failure; output streams to the console.

**`cook`:** `platforms` (`macos`, `windows`, `linux`), `profiles` (`id`,
`name`, and profile options owned by the cook tool; unknown options kept).

**`run`:** `default` (configuration ID) and `configurations`: `id`, `name`,
`executable` (native), `arguments`, optional `working_directory` (native),
`environment`.

**Variables** in build steps and run configurations: `${project}` (project
root), `${build}`, `${cache}`, `${packages}`, `${map}` (native path of the
current map's root file), `${map_name}` (map file name without extension),
`${map_virtual}` (its canonical virtual path), `${platform}`. Unknown
variables are an error reported before anything runs.

## 4. Validation

Identity, start map, content roots, and game fields are validated strictly
(`INVALID_PROJECT_ID`, `INVALID_START_MAP`, `INVALID_SEARCH_PATH`,
`DUPLICATE_SEARCH_PATH`, `INVALID_GAME`, `INVALID_MAPS_PATH`,
`UNSUPPORTED_VERSION`) because a wrong mount set loads the wrong game data.
`editor`, `build`, `cook`, and `run` are read tolerantly: an unusable profile,
step, or configuration is skipped and reported, and the rest works. The root
is open; game-specific sections are named by the game profile.

## 5. Limits

64 search paths, 16 languages, 64 build profiles with 32 steps each, 64 run
configurations, 256 arguments per step, 16 MiB per file.

## 6. Versions

V1 had `id`, `name`, `start_map` (required), and `search_paths`. V2
(2026-09-25) added `game`, `maps_path`, `settings`, `map_defaults` and made
`start_map` optional. V3 adds `game_profile`, `engine`, `paths`,
`languages`, `editor`, `build`, `cook`, and `run`. V3 reads V1 and V2;
saving writes V3.
