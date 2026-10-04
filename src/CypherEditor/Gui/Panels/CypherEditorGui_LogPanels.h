//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_LogPanels.h
//  Purpose: Declares the Problems and Output panels, two more views of the
//           editor's log store beside the Console.
//  Details: Problems lists warnings and errors as a table - description,
//           channel, source location, time - the list an IDE shows after a
//           build. Output shows plain output per source (Editor, Build,
//           Game, Engine), the way a compiler's or game's output reads,
//           without the console's level and channel columns.
//
//  History:
//  - Created by Karlo Siric on 2026-09-30
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_LOG_PANELS_H
#define CYPHER_EDITOR_GUI_LOG_PANELS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Log.h"
#include "CypherEditorGui_Style.h"

#include <QStringList>

class QWidget;

namespace cypher::editor::gui
{

CYPHER_NODISCARD QWidget *EditorProblems_Create( QWidget *pParent, const editor_style_t *pStyle, editor_log_t *pLog );
// Visible rows, for tests: "WARN Gui message".
CYPHER_NODISCARD QStringList EditorProblems_Rows( QWidget *pPanel );

CYPHER_NODISCARD QWidget *EditorOutput_Create( QWidget *pParent, const editor_style_t *pStyle, editor_log_t *pLog );
// "All", "Editor", "Build", "Game", "Engine".
void EditorOutput_SetSource( QWidget *pPanel, const QString &source );
CYPHER_NODISCARD QString EditorOutput_Text( QWidget *pPanel );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_LOG_PANELS_H
