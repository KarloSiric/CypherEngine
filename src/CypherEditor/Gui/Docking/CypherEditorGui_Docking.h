//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Docking.h
//  Purpose: Declares the dock manager: panels registered by ID, and layouts
//           (`.cylayout`, EditorCore) shown on and captured from an editor
//           window through the Qt Advanced Docking System.
//  Details: Layouts stay docking-library independent (ADR 0008); only this
//           file and its source know ADS. Any layout tree is shown exactly:
//           nested splits in either direction, tab groups anywhere, and
//           floating windows holding whole trees.
//
//           The central panel (the viewports in Mason) is ADS's central
//           widget: always present, never closed, floated, or tabbed with
//           other panels. Every other panel is a dock widget created on
//           first use through its factory and kept alive while closed, so a
//           panel keeps its state across layout changes. Panel IDs the
//           layout names but nobody registered (a plugin that is not
//           loaded) are skipped and counted.
//
//           The docking library is a pinned submodule (thirdparty/qtads,
//           LGPL-2.1) built as a shared library.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - Moved from QDockWidget onto the Qt Advanced Docking System on
//    2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_DOCKING_H
#define CYPHER_EDITOR_GUI_DOCKING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditor_Layout.h"

#include <QHash>
#include <QString>

class QMainWindow;
class QWidget;

namespace ads
{
class CDockManager;
class CDockWidget;
} // namespace ads

namespace cypher::editor::gui
{

using editor_panel_create_fn = QWidget *( * )( void *pContext, QWidget *pParent );

struct editor_panel_desc_t {
    const char *pId{ nullptr };    // "console", "map.outliner"; static lifetime.
    const char *pTitle{ nullptr }; // Tab title.
    editor_panel_create_fn pfnCreate{ nullptr };
    void *pContext{ nullptr };
};

struct editor_docking_t {
    QMainWindow *pWindow{ nullptr };
    ads::CDockManager *pManager{ nullptr };           // The window's central widget; owned by the window.
    QString centralId{};                              // Panel shown as the central widget.
    QHash<QString, editor_panel_desc_t> panels{};     // Registered panels.
    QHash<QString, ads::CDockWidget *> docks{};       // Created docks (not the central one); owned by the manager.
    ads::CDockWidget *pCentralDock{ nullptr };        // Holds the central panel.
    QWidget *pCentral{ nullptr };                     // The central panel's widget.
    bool bUppercaseTitles{ true };                    // "OUTLINER": the TileEditor's dock titles. Set before panels open.
};

// Installs a dock manager as the window's central widget. The window keeps
// its menu bar, toolbars, and status bar around it.
void EditorDocking_Init( editor_docking_t *pDocking, QMainWindow *pWindow, const char *pCentralPanelId );

// Registers panels; a duplicate ID replaces nothing and returns false.
CYPHER_NODISCARD bool EditorDocking_RegisterPanels(
    editor_docking_t *pDocking,
    const editor_panel_desc_t *pPanels,
    common::usize nPanels );

// The dock for a panel, created (closed, unplaced) on first use. Null for an
// unregistered ID or the central panel.
CYPHER_NODISCARD ads::CDockWidget *EditorDocking_Dock( editor_docking_t *pDocking, const QString &id );

// Shows a layout: panels it names are placed and opened, every other panel
// is closed. Returns the number of panel IDs skipped because nobody
// registered them.
common::usize EditorDocking_ApplyLayout( editor_docking_t *pDocking, const layout_t &layout );

// Captures the current arrangement into an initialized layout (cleared
// first). False on allocation failure.
CYPHER_NODISCARD bool EditorDocking_CaptureLayout( editor_docking_t *pDocking, layout_t *pLayout );

// Opens or closes a panel; a panel that was never placed opens to the right
// of the central panel.
void EditorDocking_SetPanelVisible( editor_docking_t *pDocking, const QString &id, bool bVisible );

CYPHER_NODISCARD bool EditorDocking_IsPanelVisible( const editor_docking_t *pDocking, const QString &id );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_DOCKING_H
