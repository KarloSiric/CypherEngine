//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_AssetWindow.h
//  Purpose: Declares the rich Asset Browser content, available as a
//           standalone pop-up or an embedded central editor pane.
//  Details: Tabs: All, one per asset kind, Used in Map, and Selection (what
//           the selected objects use, from the application). A filter with
//           the editor's fuzzy matcher, saved searches, List / Grid / Tree
//           views over one model, a thumbnail size slider, Asset Types and
//           Content Roots filters (Hammer's Asset Types and Mods), and a
//           Sources toggle. Grid tiles carry small status badges: in the
//           map, a source file, missing. The footer counts what is visible;
//           Accept (or double-click, or Enter) hands the selected path to
//           the caller.
//
//           The catalogue and thumbnails come from the asset browser panel
//           (CypherEditorGui_AssetBrowser), which owns the scan and its
//           image cache, so the window and the panel never disagree and
//           nothing is scanned twice. The window refreshes when shown.
//
//           Two ways to use it: browse (Accept calls the window's accept
//           callback, kept open) and pick (one kind, the current value
//           selected; Accept calls the pick callback once and closes).
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_ASSET_WINDOW_H
#define CYPHER_EDITOR_GUI_ASSET_WINDOW_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Application.h"
#include "CypherEditor_AssetCatalog.h"

#include <QDialog>
#include <QString>
#include <QStringList>

class QWidget;

namespace cypher::editor::gui
{

enum editor_asset_window_tab_t : int {
    ASSET_WINDOW_TAB_ALL = 0,
    ASSET_WINDOW_TAB_FIRST_KIND = 1, // + editor_asset_kind_t
    ASSET_WINDOW_TAB_USED = ASSET_WINDOW_TAB_FIRST_KIND + static_cast<int>( editor_asset_kind_t::COUNT ),
    ASSET_WINDOW_TAB_SELECTION,
    ASSET_WINDOW_TAB_COUNT
};

constexpr editor_asset_window_tab_t EditorAssetWindow_KindTab( editor_asset_kind_t kind ) noexcept
{
    return static_cast<editor_asset_window_tab_t>( ASSET_WINDOW_TAB_FIRST_KIND + static_cast<int>( kind ) );
}

enum class editor_asset_window_view_t : common::u8 { LIST = 0u, GRID, TREE };

// The asset the user accepted: its virtual path and kind (COUNT when the
// path is not in the catalogue, such as a missing asset from Used in Map).
using editor_asset_accept_fn = void ( * )( void *pContext, const QString &path, editor_asset_kind_t kind );
// Asset paths the current selection uses, for the Selection tab.
using editor_asset_selection_fn = QStringList ( * )( void *pContext );

// pBrowser is the asset browser panel whose catalogue and thumbnails the
// window shows; pGui and pBrowser must outlive the window.
CYPHER_NODISCARD QDialog *EditorAssetWindow_Create( QWidget *pParent, editor_gui_t *pGui, QWidget *pBrowser );

// The same browser content in a parent-owned editor pane. pParent is
// required; pGui and pBrowser must outlive it. It stays open on Accept and
// cannot be turned into a modal picker. The operations below accept either
// the standalone dialog or this embedded widget.
CYPHER_NODISCARD QWidget *EditorAssetWindow_CreateEmbedded( QWidget *pParent, editor_gui_t *pGui, QWidget *pBrowser );

void EditorAssetWindow_SetAccept( QWidget *pWindow, editor_asset_accept_fn pfnAccept, void *pContext );
void EditorAssetWindow_SetSelectionSource( QWidget *pWindow, editor_asset_selection_fn pfnSelection, void *pContext );
void EditorAssetWindow_SetUsed( QWidget *pWindow, const QStringList &paths );

// Shows the standalone window on the kind's tab with current selected.
// Accept calls pfnPick once and closes; closing without accepting calls
// nothing. Ignored by embedded widgets.
void EditorAssetWindow_Pick( QWidget *pWindow, editor_asset_kind_t kind, const QString &current, editor_asset_accept_fn pfnPick, void *pContext );
CYPHER_NODISCARD bool EditorAssetWindow_IsPicking( QWidget *pWindow );

// Reads the catalogue again (after a rescan) and rebuilds the view.
void EditorAssetWindow_Refresh( QWidget *pWindow );

void EditorAssetWindow_SetTab( QWidget *pWindow, int tab );
CYPHER_NODISCARD int EditorAssetWindow_Tab( QWidget *pWindow );
void EditorAssetWindow_SetFilter( QWidget *pWindow, const QString &text );
void EditorAssetWindow_SetView( QWidget *pWindow, editor_asset_window_view_t view );
CYPHER_NODISCARD editor_asset_window_view_t EditorAssetWindow_View( QWidget *pWindow );
// Asset Types on the All tab; EDITOR_ASSET_KIND_ALL shows every kind.
void EditorAssetWindow_SetKindMask( QWidget *pWindow, common::u32 kindMask );
void EditorAssetWindow_SetSources( QWidget *pWindow, bool bShow );
// Content roots by index (Hammer's Mods); all are shown by default.
void EditorAssetWindow_SetRootVisible( QWidget *pWindow, int iRoot, bool bVisible );

// Automation and tests.
CYPHER_NODISCARD QStringList EditorAssetWindow_Visible( QWidget *pWindow ); // Virtual paths, in view order.
CYPHER_NODISCARD bool EditorAssetWindow_Select( QWidget *pWindow, const QString &path );
CYPHER_NODISCARD QString EditorAssetWindow_Selected( QWidget *pWindow );
CYPHER_NODISCARD bool EditorAssetWindow_Accept( QWidget *pWindow ); // False with nothing acceptable selected.
// Keyboard focus to the results, selecting the first when none is (Enter
// in a search field that feeds the window).
void EditorAssetWindow_FocusResults( QWidget *pWindow );
CYPHER_NODISCARD QString EditorAssetWindow_Status( QWidget *pWindow ); // "12 Assets Visible"
CYPHER_NODISCARD bool EditorAssetWindow_HasThumbnail( QWidget *pWindow, const QString &path );
void EditorAssetWindow_LoadThumbnails( QWidget *pWindow ); // Every queued thumbnail now.

// Saved searches remember the tab, filter, Asset Types, and Sources under a
// name, in the user's settings (editor.assets.saved_searches).
CYPHER_NODISCARD bool EditorAssetWindow_SaveSearch( QWidget *pWindow, const QString &name );
CYPHER_NODISCARD bool EditorAssetWindow_LoadSearch( QWidget *pWindow, const QString &name );
CYPHER_NODISCARD bool EditorAssetWindow_DeleteSearch( QWidget *pWindow, const QString &name );
CYPHER_NODISCARD QStringList EditorAssetWindow_SavedSearches( QWidget *pWindow );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_ASSET_WINDOW_H
