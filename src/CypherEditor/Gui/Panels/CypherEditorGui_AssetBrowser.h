//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_AssetBrowser.h
//  Purpose: Declares the asset browser panel, after Hammer 5's: every asset
//           under the content roots as thumbnails or a list, by kind tab,
//           folder, and search, with a "Used in Map" tab for what the open
//           document names (missing assets flagged).
//  Details: The catalogue is the Qt-free CypherEditor_AssetCatalog; this
//           panel walks the roots, shows the catalogue, and draws
//           thumbnails: a material shows its base-colour texture's image, a
//           texture recipe its source image, an image itself, anything else
//           its kind's icon. Thumbnails load a few at a time as items come
//           into view, so a large project opens at once. Activating materials
//           calls the application (Mason changes Active Material); texture,
//           shader and map activation opens a read-only file inspector.
//           Grid previews default to 144 logical pixels, with a 48-256 px
//           slider and presets. Size and list/grid mode follow user settings.
//
//  History:
//  - Created by Karlo Siric on 2026-10-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_ASSET_BROWSER_H
#define CYPHER_EDITOR_GUI_ASSET_BROWSER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Application.h"
#include "CypherEditor_AssetCatalog.h"

#include <QImage>
#include <QStringList>

class QWidget;

namespace cypher::editor::gui
{

// Tabs: All, one per asset kind (in editor_asset_kind_t order), then the
// open document's assets.
enum editor_asset_tab_t : int {
    ASSET_TAB_ALL = 0,
    ASSET_TAB_FIRST_KIND = 1, // + editor_asset_kind_t
    ASSET_TAB_USED = ASSET_TAB_FIRST_KIND + static_cast<int>( editor_asset_kind_t::COUNT ),
    ASSET_TAB_COUNT
};

constexpr editor_asset_tab_t EditorAssetBrowser_KindTab( editor_asset_kind_t kind ) noexcept
{
    return static_cast<editor_asset_tab_t>( ASSET_TAB_FIRST_KIND + static_cast<int>( kind ) );
}

// Called on double-click or Enter with the asset's virtual path, except for
// texture, map and shader assets, which open their read-only inspector.
using editor_asset_activate_fn = void ( * )( void *pContext, const QString &path, editor_asset_kind_t kind );
// The shared catalogue owner can refresh dependent browsers after each
// completed scan, including root changes and a browser's Rescan button.
// The callback/context must outlive the browser and must not rescan it.
using editor_asset_rescan_fn = void ( * )( void *pContext );

// pGui must outlive the panel.
CYPHER_NODISCARD QWidget *EditorAssetBrowser_Create( QWidget *pParent, editor_gui_t *pGui );

// Content folders, highest priority first (the mount order); rescans.
void EditorAssetBrowser_SetRoots( QWidget *pBrowser, const QStringList &roots );
CYPHER_NODISCARD QStringList EditorAssetBrowser_Roots( QWidget *pBrowser );
void EditorAssetBrowser_Rescan( QWidget *pBrowser );
void EditorAssetBrowser_SetRescanCallback( QWidget *pBrowser, editor_asset_rescan_fn pfnRescan, void *pContext );

// What the open document uses, for the Used tab; title names it ("Used in
// Map"). Paths no root has are listed as missing.
void EditorAssetBrowser_SetUsed( QWidget *pBrowser, const QString &title, const QStringList &paths );

void EditorAssetBrowser_SetActivate( QWidget *pBrowser, editor_asset_activate_fn pfnActivate, void *pContext );

void EditorAssetBrowser_SetTab( QWidget *pBrowser, editor_asset_tab_t tab );
void EditorAssetBrowser_SetSearch( QWidget *pBrowser, const QString &text );
// Main-window quick search: resets the kind tab and folder restriction,
// retains Sources visibility, and optionally selects/focuses the first result.
// It never activates an asset. SetSearch above keeps the current filters.
void EditorAssetBrowser_Search( QWidget *pBrowser, const QString &text, bool focusFirstResult = false );
// Folder filter ("materials/blockout"); empty for every folder.
void EditorAssetBrowser_SetFolder( QWidget *pBrowser, const QString &folder );
void EditorAssetBrowser_SetSources( QWidget *pBrowser, bool bShow );

// Automation and tests.
CYPHER_NODISCARD QStringList EditorAssetBrowser_Visible( QWidget *pBrowser ); // Virtual paths, in view order.
CYPHER_NODISCARD QStringList EditorAssetBrowser_Missing( QWidget *pBrowser ); // Used paths no root has.
CYPHER_NODISCARD const editor_asset_catalog_t *EditorAssetBrowser_Catalog( QWidget *pBrowser );
void EditorAssetBrowser_Activate( QWidget *pBrowser, const QString &path );
// Opens a modeless read-only inspector without applying a material or
// opening a map. Parent-owned; returned pointer is borrowed and invalidated
// by closing it, inspecting a different asset, or destroying the browser.
// Returns nullptr when the virtual path is absent from the catalogue.
CYPHER_NODISCARD QWidget *EditorAssetBrowser_Preview( QWidget *pBrowser, const QString &path );
// Loads every queued thumbnail now (screenshots, tests); true when the
// asset has an image thumbnail rather than its kind's icon.
void EditorAssetBrowser_LoadThumbnails( QWidget *pBrowser );
CYPHER_NODISCARD bool EditorAssetBrowser_HasImage( QWidget *pBrowser, const QString &path );
// The asset's thumbnail image, decoded now if it was not yet; null when it
// has none (a sound, a material without textures, a missing file).
CYPHER_NODISCARD QImage EditorAssetBrowser_Image( QWidget *pBrowser, const QString &path );
CYPHER_NODISCARD QString EditorAssetBrowser_Status( QWidget *pBrowser );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_ASSET_BROWSER_H
