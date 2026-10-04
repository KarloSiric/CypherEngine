//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_DatabaseView.h
//  Purpose: Declares the shared Content Library for asset recipes, file
//           metadata, and authored instances in the open map. Standalone
//           and embedded views share one catalogue and map record source.
//  Details: Materials: Material Settings (shader, base, domain, surface),
//           Opacity and State, Texture Maps and Shader Params. Slots and
//           parameters come from the material and from its shader's declared
//           interface (schema 2), so a parameter edits with the control its
//           declared type calls for: a checkbox, a slider within its bounds,
//           a colour, a vector; one the material leaves at the shader's
//           default can be overridden and returned to it. Shader and texture
//           rows have the folder button Sandbox has: it opens that asset in
//           its own tab. Shaders: the recipe's fields and a code editor for
//           the recipe and each stage (GLSL highlighting), New Shader, Save,
//           Reload, External Edit, and the materials that use it. Textures:
//           a zoomable preview with channel views, the recipe's fields, and
//           the materials that use it. Entities: the open map's entities by
//           class with every key, origin and rotation included. Select in
//           Map opens the authoritative, undoable object inspector. Triggers,
//           scripts and sequences filter those same instances. Models, maps,
//           fonts and animations show file metadata. Prefabs, particles and
//           sounds expose their recipes as an editable tree.
//
//           Recipes are read and written through the CYKV settings store,
//           at the schema version they were written in; Save writes the
//           file, Revert reads it again. The asset browser panel supplies
//           the catalogue and content roots.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_DATABASE_VIEW_H
#define CYPHER_EDITOR_GUI_DATABASE_VIEW_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Application.h"
#include "CypherEditor_AssetCatalog.h"

#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"

#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

class QWidget;

namespace cypher::editor::gui
{

enum editor_database_tab_t : int {
    DATABASE_TAB_MATERIALS = 0,
    DATABASE_TAB_SHADERS,
    DATABASE_TAB_TEXTURES,
    DATABASE_TAB_ENTITIES,
    DATABASE_TAB_PREFABS,
    DATABASE_TAB_PARTICLES,
    DATABASE_TAB_SOUNDS,
    // Append categories to retain existing tab identities.
    DATABASE_TAB_MODELS,
    DATABASE_TAB_TRIGGERS,
    DATABASE_TAB_SCRIPTS,
    DATABASE_TAB_SEQUENCES,
    DATABASE_TAB_MAPS,
    DATABASE_TAB_FONTS,
    DATABASE_TAB_ANIMATIONS,
    DATABASE_TAB_COUNT
};

// Open-map instances, shared by Entities/Triggers/Scripts/Sequences. These
// categories are projections of authored records, not creation definitions.
// Triggers use the trigger_ class prefix; Scripts/Sequences use explicit
// class/property conventions until game-profile categories are decoded.
struct editor_database_entity_t {
    common::u64 id{ 0u };
    QString className{};
    QString name{};
};
using editor_database_entities_fn = QVector<editor_database_entity_t> ( * )( void *pContext );
// The entity's record (its keys, origin, angles, outputs); null when gone.
using editor_database_record_fn = const common::key_value_t *( * )( void *pContext, common::u64 id );
using editor_database_go_to_fn = void ( * )( void *pContext, common::u64 id );

// pGui and pBrowser (the asset browser panel) must outlive the window.
CYPHER_NODISCARD QDialog *EditorDatabaseView_Create( QWidget *pParent, editor_gui_t *pGui, QWidget *pBrowser );

// Parent-owned editor content; pParent is required and pGui / pBrowser
// must outlive it. Retain the widget when switching pane modes to retain
// unsaved edits. Refresh updates libraries and borrowed map/file details;
// it does not reload editable recipe buffers.
// shaderOnly fixes the content to the real shader recipe / source editor
// and hides the other tabs. Operations below accept either factory result.
CYPHER_NODISCARD QWidget *EditorDatabaseView_CreateEmbedded( QWidget *pParent, editor_gui_t *pGui, QWidget *pBrowser,
                                                            int initialTab = DATABASE_TAB_MATERIALS, bool shaderOnly = false );

void EditorDatabaseView_SetEntitySource( QWidget *pView, editor_database_entities_fn pfnEntities, editor_database_record_fn pfnRecord,
                                         editor_database_go_to_fn pfnGoTo, void *pContext );

// Opens a catalogued asset in its category. Models/maps/fonts/animations
// currently show file metadata; their specialized editors are separate work.
// False for other kinds and paths the catalogue does not have.
CYPHER_NODISCARD bool EditorDatabaseView_Open( QWidget *pView, const QString &path );
CYPHER_NODISCARD bool EditorDatabaseView_OpenEntity( QWidget *pView, common::u64 id );
// Reads the catalogue and entities again.
void EditorDatabaseView_Refresh( QWidget *pView );

void EditorDatabaseView_SetTab( QWidget *pView, int tab );
CYPHER_NODISCARD int EditorDatabaseView_Tab( QWidget *pView );
// The asset (or "#id" entity) open in the current tab; empty when none.
CYPHER_NODISCARD QString EditorDatabaseView_Current( QWidget *pView );
// Library entries of the current tab, in tree order.
CYPHER_NODISCARD QStringList EditorDatabaseView_Library( QWidget *pView );

// The current tab's property grid as "Group/Name=value" rows; and setting
// one by "Group/Name" as the user would (text for paths and enums, "true" /
// "false", numbers, "r g b a" for vectors and colours).
CYPHER_NODISCARD QStringList EditorDatabaseView_Properties( QWidget *pView );
CYPHER_NODISCARD bool EditorDatabaseView_SetProperty( QWidget *pView, const QString &groupAndName, const QString &value );
// Returns a parameter or texture the material overrides to its shader's
// default (removes it from the recipe).
CYPHER_NODISCARD bool EditorDatabaseView_ResetProperty( QWidget *pView, const QString &groupAndName );
// Clicks the folder button of a row: opens that asset in its tab.
CYPHER_NODISCARD bool EditorDatabaseView_OpenRow( QWidget *pView, const QString &groupAndName );

CYPHER_NODISCARD bool EditorDatabaseView_IsModified( QWidget *pView );
CYPHER_NODISCARD bool EditorDatabaseView_Save( QWidget *pView );
CYPHER_NODISCARD bool EditorDatabaseView_Revert( QWidget *pView );

// Shaders: the code editors (0 the recipe, then one per stage), a new
// shader (recipe and stages from a template under the first content root),
// and the materials that use the current shader or texture.
CYPHER_NODISCARD QWidget *EditorDatabaseView_CodeEditor( QWidget *pView, int index );
CYPHER_NODISCARD int EditorDatabaseView_CodeEditorCount( QWidget *pView );
CYPHER_NODISCARD bool EditorDatabaseView_NewShader( QWidget *pView, const QString &name, QString *pPathOut = nullptr );
CYPHER_NODISCARD QStringList EditorDatabaseView_UsedBy( QWidget *pView );
// Textures: "rgb", "r", "g", "b", "a".
void EditorDatabaseView_SetChannel( QWidget *pView, const QString &channel );
CYPHER_NODISCARD QImage EditorDatabaseView_PreviewImage( QWidget *pView );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_DATABASE_VIEW_H
