//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileEditorTheme.h
//  Purpose: Declares the standalone tile editor's Qt visual theme.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_THEME_H
#define CYPHER_TOOLS_TILEEDITOR_THEME_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

class QApplication;

namespace cypher::tools::tile_editor
{

struct tile_editor_preferences_t;
void CypherTileEditorTheme_Apply( QApplication &application );
void CypherTileEditorTheme_Apply(
    QApplication &application, const tile_editor_preferences_t &preferences );

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_THEME_H
