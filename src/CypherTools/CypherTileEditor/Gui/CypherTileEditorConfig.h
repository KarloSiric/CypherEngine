//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code
// Copyright (c) 2026 Karlo Siric. All rights reserved.
// Purpose: Human-editable editor preferences, separate from native dock state.
//////////////////////////////////////////////////////////////////////////
#pragma once

#include "CypherTileEditorSettingsDialog.h"

class QSettings;

namespace cypher::tools::tile_editor
{
QString TileEditorConfig_DefaultPath();

// Missing files succeed without changing preferences. Present fields override the
// supplied baseline; invalid files fail without partially changing that baseline.
bool TileEditorConfig_Load(
    const QString &path, tile_editor_preferences_t &inOutPreferences, QString &error );

// Writes a complete schema-1 file atomically. Existing comments are regenerated;
// dock placement, recent documents, and other native workspace state stay separate.
bool TileEditorConfig_Save(
    const QString &path, const tile_editor_preferences_t &preferences, QString &error );

// One-time migration for profiles created while linked 2D navigation was the
// default. A completed migration never overrides a later explicit user choice.
bool TileEditorConfig_MigrateIndependentOrthographicCameras(
    QSettings &nativeSettings,
    const QString &configurationPath,
    tile_editor_preferences_t &inOutPreferences,
    bool &outMigrated,
    QString &error );
} // namespace cypher::tools::tile_editor
