//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Purpose: Portable, color-only user themes for CypherTileEditor.
//////////////////////////////////////////////////////////////////////////
#pragma once

#include "CypherTileEditorSettingsDialog.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace cypher::tools::tile_editor
{

struct tile_editor_user_theme_t {
    QString name{};
    QString path{};
    tile_editor_preferences_t colors{};
};

// User themes deliberately contain only semantic colors. Applying a theme must
// never change navigation, shortcuts, layout, display options, or map defaults.
void TileEditorTheme_CopyColors(
    tile_editor_preferences_t &destination,
    const tile_editor_preferences_t &source );

QString TileEditorUserThemes_DefaultDirectory();
QString TileEditorUserThemes_PathForName(
    const QString &directory,
    const QString &name );

bool TileEditorUserTheme_Load(
    const QString &path,
    tile_editor_user_theme_t &outTheme,
    QString &error );

bool TileEditorUserTheme_Save(
    const QString &path,
    const QString &name,
    const tile_editor_preferences_t &preferences,
    QString &error );

QList<tile_editor_user_theme_t> TileEditorUserThemes_Discover(
    const QString &directory,
    QStringList *pErrors = nullptr );

} // namespace cypher::tools::tile_editor
