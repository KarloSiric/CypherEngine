# Cypher Editor Core

`Cypher::EditorCore` is the Qt-free editor framework described in
[ADR 0008](../../../docs/adr/0008-editor-framework-and-workspace-layout.md). It
holds the editor's data and rules; `Cypher::EditorGui` (Qt) and the workspaces
build on it. Nothing here may include Qt, so everything is testable headlessly
(`tests/CypherEditor/Core`).

Headers are included by basename. File formats are specified in
[Editor And Settings Formats](../../../docs/formats/EDITOR_FORMATS.md) and their
identities in [ADR 0009](../../../docs/adr/0009-editor-and-map-file-identities.md).

| Folder | Holds |
| --- | --- |
| [Settings/](Settings) | `CypherEditor_SettingsFile`: loading and saving settings-family files - backups, `.broken` copies of damaged files, blocked saves until the user decides |
| [Themes/](Themes) | `CypherEditor_Theme`: `.cytheme` token registry, base chains, colour/font/metric resolution, sparse editing, audits for the theme editor |
| [Commands/](Commands) | `CypherEditor_Keymap`: key chords and `.cykeymap` editor shortcuts - lookup through base keymaps, overrides, conflicts, editing |
| [Layouts/](Layouts) | `CypherEditor_Layout`: `.cylayout` dock trees, independent of the docking library |
| [Project/](Project) | `CypherEditor_Workspace`: `.cyworkspace` decoding and creation, and the workspace/project/user settings scope stack |

The generic settings store (`CypherCommon_SettingsDocument`), `.cysettings`,
`.cyproject`, and `.cyfont` live in CypherCommon Tier2 because the runtime
reads them too.

Planned next, per ADR 0008: the command registry itself (`Commands/`),
`Vfs/`, `Console/`, `Assets/`, `Notifications/`, `Selection/`, `Undo/`,
`Personalization/`, and `Scripting/` for Python plugins. Each folder appears
when its first code and tests land.
