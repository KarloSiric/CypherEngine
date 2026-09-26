# ADR 0008: Editor Framework and Workspace Layout

**Status:** Accepted
**Date:** 2026-09-25

## Context

Mason is planned as one editor shell hosting many workspaces: map, material,
shader, particle, model, entity, prefab, physics, script, sequence, and more
(see [map_authoring_and_mason.md](../map_authoring_and_mason.md)). Before any of
them exists, the editor needs a frontend foundation: look and themes, a theme
editor, fonts, settings, editor config, game paths, an editor virtual file
system, a console, layouts, context menus, and the tool sidebar.

Two Qt tools already exist and each carries its own copy of that foundation:

- CypherTileEditor: `CypherTileEditorTheme`, `CypherTileEditorUserThemes`
  (`.cytheme`), `CypherTileEditorConfig` (`editor.ini`),
  `CypherTileEditorSettingsDialog` with a data-driven field table
  (`CypherTileEditorPreferenceFields.h`), `CypherTileConsole`,
  `CypherTileEditorIcons`, `CypherTileViewportColors`, `Resources/editor.qss`.
- Picasso: `PicassoTheme`, `PicassoConsole`, `PicassoIcons`.

A third copy inside Mason would make the look, settings, and console drift
three ways.

### What the Sandbox lineage shows

Researched on 2026-09-25. Evidence is labelled; CryEngine 1 material is
study-only under [reference_policy.md](../reference_policy.md), so only file
and folder names were read, never implementation.

**CryEngine 1 Sandbox** (verified from the file listing of the mirror cited in
[cryengine1_subsystem_research.md](../cryengine1_subsystem_research.md)): one
MFC executable, 752 code files, 208 of them flat in the root - terrain,
vegetation, Track View, every viewport, tool, panel, and dialog side by side.
Only a few domains had folders: `Objects/` (base object, object manager, class
registry, object types, gizmos, layers), `Controls/` (reusable widgets including
the property grid and rollup bar), `Util/` (including the property-variable
system and a file-change monitor), `Commands/`, `Undo/`, `Brush/`, and the
DataBase View libraries. Those libraries share one strong pattern: materials,
particles, prefabs, music, AI behaviours, and entity prototypes each provide a
Manager, Library, Item, and Dialog over one generic library base.

**CRYENGINE 5 Sandbox** (verified from Crytek's
[Sandbox programming documentation](https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756813/pages/26875248)):
the Qt rewrite restructured the monolith into packages:

- EditorInterface - public abstract interfaces only;
- EditorCommon - reusable widgets and utilities, plus some complete systems;
- Sandbox - the executable and level editor; it instantiates the systems and
  loads plugins, and no other package may depend on it;
- Plugins - one self-contained feature each, depending only on
  EditorInterface and EditorCommon; new functionality is a plugin by default.

Every tool receives framework services: an editor-window base (layouts,
docking, menus, unsaved-changes handling), an asset system (asset type plus
asset editor), a command system in which every action is a `module.command`
that menus, toolbars, shortcuts, and the console are built from, preference
pages declared by the code that owns them, personalization (UI state
remembered implicitly, globally or per project), one stylesheet with a palette
helper, and monochrome icons tinted at runtime. Crytek's own preference
guidance warns that too many options make tools harder to use and prefers
personalization over explicit settings.

**O3DE** (a Lumberyard/CryEngine descendant, Apache-2.0 OR MIT; verified folder
listing of [o3de/o3de](https://github.com/o3de/o3de)): `AzQtComponents` holds
styled widgets, a style manager, a stylesheet preprocessor, docking, title
bars, notifications, and fonts; `AzToolsFramework` holds the action manager
(actions, menus, toolbars, hotkeys), undo, prefabs, viewport selection,
manipulators, the outliner, and the property editor; the dark theme's colours
live in a JSON variables file that is preprocessed into the stylesheet.

## Decision

### Layout

```text
src/CypherEditor/
  Geometry/      Cypher::EditorGeometry   Qt-free authoring geometry (ADR 0005)
  Core/          Cypher::EditorCore       Qt-free editor framework
    Commands/        command registry (module.command), arguments, shortcut table
    Undo/            document-agnostic transactions and history
    Selection/       selection sets shared by panels and viewports
    Settings/        preference pages registered by their owners; persistence
    Personalization/ implicitly remembered UI state, global and per project
    Themes/          theme data: colour tokens, viewport colours, fonts
    Layouts/         dock layout trees (.cylayout), independent of the docking library
    Project/         projects, game paths, editor config, recent files
    Vfs/             editor mount profiles over CypherFileSystem
    Console/         console model over CypherLog, CypherCommand, CypherCVar
    Assets/          asset types, asset database, dependencies, libraries
    Notifications/   bounded user-facing notifications and error reports
    Scripting/       Python plugin host: plugin discovery, manifests, bindings
                     to Commands, Settings, Selection, and Assets
  Gui/           Cypher::EditorGui        Qt framework shared by every Qt tool
    Application/     application bootstrap and main-window shell
    Style/           theme to stylesheet, icon tinting, font application
    Docking/         dock manager, saved layouts, docked or floating browsers
    Actions/         menus, toolbars, context menus, sidebar built from Commands
    Widgets/         property grid, search and filter, typed field editors
    Panels/          console, log, notifications, history, settings, theme editor,
                     DataBase View
    Viewport/        viewport host, camera controllers, grid
    Scripting/       plugin panels, Python console
  Workspaces/    one folder per feature
    <Feature>/       Map, Material, Shader, Particle, Model, Entity, Prefab,
      Core/            Physics, Script, Sequence, ...
      Gui/             Qt-free core, Qt front end, one registration entry
  App/
    Mason/       the Mason executable; the only code that lists every workspace
  Resources/     monochrome icons, default themes, fonts, stylesheet templates
```

Mapping to the researched structure: `Core` corresponds to EditorInterface plus
the non-UI half of EditorCommon; `Gui` to the UI half of EditorCommon and to
`AzQtComponents`; `Workspaces` to Sandbox plugins; `App/Mason` to the Sandbox
executable.

### Dependency rules

```text
Cypher::CommonTier1, Math, FileSystem, Log, Command, CVar, Config
        |
Cypher::EditorCore            (no Qt)        Cypher::EditorGeometry
        |                                            |
Cypher::EditorGui             (Qt)                   |
        |                                            |
Workspaces/<F>/Core (no Qt) -- EditorCore, EditorGeometry, engine libraries
Workspaces/<F>/Gui  (Qt)    -- its Core, EditorGui
        |
App/Mason                   -- everything; nothing depends on it
```

- `Core` and every workspace `Core/` never include Qt and are testable
  headlessly, like Geometry.
- A workspace never includes another workspace. Cross-feature interaction goes
  through Core: commands, asset identifiers, selection, and notifications.
  (Sandbox permits plugin-to-plugin dependencies; this rule is stricter until a
  concrete need is shown.)
- Nothing depends on `App/Mason`. If a workspace needs something Mason owns,
  that thing moves into Core or Gui.
- Engine runtime services are reused, never re-implemented: the editor VFS is
  a mount profile over `CypherFileSystem`, the console routes to
  `CypherCommand`/`CypherCVar` and reads `CypherLog`.

### Registration

Workspaces are static libraries. Each exposes one registration descriptor - a
struct of identity data and function pointers, the stable boundary that
[function_pointer_policy.md](../function_pointer_policy.md) allows for plugin
entry points - and `App/Mason` lists the descriptors it links. There is no
dynamic plugin loading and no virtual plugin interface; the CryEngine research
already rejected a DLL per module as the default. Dynamic loading can be added
later behind the same descriptor if third-party extension becomes a real need.

### Python plugins

Added 2026-09-25. C++ workspaces stay static libraries; users extend the
editor at runtime with Python plugins, through the same contracts C++ code
uses:

- a plugin is a folder with a `.cyplugin` manifest (identity, version, required
  editor API version, entry module) and its Python modules;
- plugins register commands, which gives them menu, toolbar, context-menu,
  sidebar, shortcut, and console entries automatically; they add panels built
  from framework widgets and register settings pages and theme tokens;
- plugins change documents only by running commands, so every plugin edit is
  validated, undoable, and logged - the same surface MCP automation uses;
- plugins load from built-in, user, project, and workspace locations; a
  project- or workspace-supplied plugin needs the user's explicit trust before
  it first runs, because it executes with the editor's rights;
- the Python runtime is embedded by `Core/Scripting` (bindings, no Qt) and
  `Gui/Scripting` (panels, Python console). Adding the interpreter as a
  dependency is a separate reviewed step.

### Framework contracts

- **Commands first.** Every user action is registered once as a
  `module.command` with its label, icon, and default shortcut. Menus, toolbars,
  context menus, the tool sidebar, shortcut bindings, and the console are all
  generated from the registry, so every action is also scriptable.
- **Settings are registered by their owners.** Each module declares its
  preference pages as a data table (section path, key, label, type, default,
  limits), generalising TileEditor's existing field table. The settings dialog
  is generated and searchable. There is no central preferences struct.
- **Personalization is separate from settings.** Layouts, splitter positions,
  expanded categories, recents, and favourites are remembered automatically,
  globally or per project, and never appear as settings.
- **Themes are data.** A theme is a set of named tokens (interface colours,
  viewport line and grid colours, axis colours, fonts, sizes). Modules register
  the tokens they use with defaults; the theme editor lists every registered
  token; `Gui/Style` generates the stylesheet from the tokens, so an edited
  theme applies live. User themes keep the `.cytheme` suffix.
- **Icons are monochrome** and tinted from theme tokens at runtime.
- Qt requires `QWidget`/`QObject` subclassing; that inheritance stays inside
  `Gui` and workspace `Gui/` leaf code. Framework contracts are structs and
  free functions.

### Existing tools

- TileEditor's theme, user themes, config, settings dialog, console, icons,
  and viewport colours are lifted into `EditorCore`/`EditorGui`, generalised,
  and TileEditor links the framework instead. The TileEditor look becomes the
  default editor-wide theme. Picasso follows.
- TileEditor and Picasso stay separate executables under `src/CypherTools/`.
- The empty `src/CypherTools/Mason/` placeholder moves to
  `src/CypherEditor/App/Mason/`.

### Folder policy

A folder in this layout is created when its first real code lands, with its
tests. No README-only or `.gitkeep` placeholder folders are created from this
ADR; the layout above is the plan of record.

## First slice

The frontend foundation, in order:

1. `Core/`: Commands, Settings, Themes, Project (game paths, editor config),
   Vfs, Console - each with contract tests.
2. `Gui/`: Application, Style, Docking, Actions, Panels (console, settings,
   theme editor) - built by extracting and generalising the TileEditor code.
3. `App/Mason`: a main window with the TileEditor look, menus and sidebar
   generated from Commands, saved layouts, the console, and the settings and
   theme editors.
4. TileEditor migrated onto the framework; Picasso afterwards.

Content workspaces (Map first) start after this slice.

## Consequences

- One look, one settings system, one console, and one command registry for
  Mason, TileEditor, and Picasso.
- A feature lives in one workspace folder; adding a feature does not touch
  other features.
- Framework contracts are testable without a display.
- Registration tables cost a little boilerplate per workspace compared with
  implicit static registration.
- The TileEditor migration is a real refactor of working code and must land in
  steps that keep TileEditor building and behaving the same.

## Rejected alternatives

### Layer-first layout

`Core/`, `Gui/`, and `App/` at the top with every feature spread across them
(`Core/Material`, `Gui/Material`). Rejected because one feature would live in
several folders and the framework would have no visible boundary from feature
code - the same drift that produced CryEngine 1's flat editor root.

### Dynamic plugins with a virtual plugin interface

Sandbox's model. Rejected as the default: static targets are simpler, and the
C++ ABI across a DLL boundary is fragile. The registration descriptor keeps the
option open.

### One central preferences struct

TileEditor's current model (one struct, 66 fields). Rejected because every
workspace would edit one shared file, and the settings dialog could not be
generated per module.

### Copying the TileEditor foundation into Mason

Rejected because three copies of the look, settings, and console would drift.

### Keeping Mason under `src/CypherTools/`

Rejected so that all editor code lives under `src/CypherEditor/`; standalone
tools that are not part of the editor stay in `src/CypherTools/`.
