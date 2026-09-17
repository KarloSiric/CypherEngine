//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileEditorIcons.h
//  Purpose: Declares the tile editor's platform-independent icon factory.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_ICONS_H
#define CYPHER_TOOLS_TILEEDITOR_ICONS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <QIcon>
#include <QStringView>
class QWidget;

namespace cypher::tools::tile_editor
{

enum class tile_editor_icon_tone_t : unsigned char
{
    STEEL,
    BLUE,
    TEAL,
    GOLD,
    GREEN,
    ORANGE,
    RED
};

// Semantic names keep QAction construction independent from resource paths.
// The same vocabulary can later be reused by Mason's tile-map workspace.
enum class tile_editor_icon_t : unsigned char
{
    NEW_MAP,
    OPEN,
    SAVE,
    UNDO,
    REDO,
    SELECT,
    PAINT,
    ERASE,
    RECTANGLE,
    STAMP,
    SPAWN,
    DOOR,
    MATERIAL,
    GRID,
    VALIDATE,
    BUILD,
    PREVIEW,
    FIT_VIEW,
    SETTINGS,
    SEARCH,
    CONSOLE,
    PAN,
    EYEDROPPER,
    LINE,
    FILL,
    FOUR_VIEWS,
    MATERIAL_LIBRARY,
    STOP,
    PLAY
};

// Creates normal, hover, selected, and disabled pixmaps from a bundled SVG.
// The logical name omits the resource prefix and .svg extension.
[[nodiscard]] QIcon CypherTileEditorIcon_Create(
    QStringView name,
    tile_editor_icon_tone_t tone = tile_editor_icon_tone_t::STEEL );

// Refresh existing action/button glyphs after a theme accent change.
void CypherTileEditorIcons_Refresh( QWidget *root );

// Uses the editor's default tone for the semantic icon family.
[[nodiscard]] QIcon CypherTileEditorIcon_Create( tile_editor_icon_t icon );

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_ICONS_H
