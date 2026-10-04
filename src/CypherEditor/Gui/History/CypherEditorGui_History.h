//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_History.h
//  Purpose: Declares the reusable document Undo History panel.
//  Details: Rows represent retained history states, with the active state
//           and redo steps distinguished. The owning workspace rebuilds
//           its document views after the panel applies history callbacks.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_HISTORY_H
#define CYPHER_EDITOR_GUI_HISTORY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditor_History.h"
#include "CypherEditorGui_Style.h"

class QWidget;

namespace cypher::editor::gui
{

// Called after a panel-initiated undo, redo, or walk. APPLY_FAILED can
// leave a partially applied document, so consumers refresh for that too.
using editor_history_panel_applied_fn = void ( * )( void *pContext, editor_history_status_t status ) noexcept;

// Borrows history, style, and callback context; they outlive the panel.
// A null history creates a disabled panel until Bind attaches a document.
// Returns null when its history listener cannot be registered.
CYPHER_NODISCARD QWidget *EditorHistoryPanel_Create(
    QWidget *pParent,
    editor_history_t *pHistory,
    const editor_style_t *pStyle,
    editor_history_panel_applied_fn pfnApplied = nullptr,
    void *pContext = nullptr );

// Atomically changes the history subscription. A null history detaches.
// On listener-capacity failure the old document remains attached.
CYPHER_NODISCARD common::bool_t EditorHistoryPanel_Bind( QWidget *pPanel, editor_history_t *pHistory );

// History changes refresh automatically; call this after replacing the
// style's resolved values so icon tints and row colours follow the theme.
void EditorHistoryPanel_Refresh( QWidget *pPanel );

} // namespace cypher::editor::gui

#endif
