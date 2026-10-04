//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_CodeEditor.h
//  Purpose: Declares the editor's code editor: a plain-text editor with a
//           line-number gutter, current-line highlight, find bar, and syntax
//           highlighting for GLSL and CYKV, used where the editor shows or
//           edits source (the Database View's shader editor first).
//  Details: Colours come from the theme's code.* tokens and the font from
//           the "code" font token, so a theme recolours source the way it
//           recolours everything else. Tab inserts four spaces; Enter keeps
//           the line's indentation. Ctrl+F opens the find bar, F3 / Shift+F3
//           find again, Ctrl+G goes to a line.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_CODE_EDITOR_H
#define CYPHER_EDITOR_GUI_CODE_EDITOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Style.h"

#include <QString>

class QPlainTextEdit;
class QWidget;

namespace cypher::editor::gui
{

enum class editor_code_language_t : common::u8 { PLAIN = 0u, GLSL, CYKV };

// The returned widget holds the editor and its find bar; style must outlive it.
CYPHER_NODISCARD QWidget *EditorCodeEditor_Create( QWidget *pParent, const editor_style_t *pStyle, editor_code_language_t language );

// The text edit inside, for signals (modificationChanged, textChanged).
CYPHER_NODISCARD QPlainTextEdit *EditorCodeEditor_Text( QWidget *pEditor );
void EditorCodeEditor_SetLanguage( QWidget *pEditor, editor_code_language_t language );
// Replaces the text and marks it unmodified (a fresh load).
void EditorCodeEditor_SetText( QWidget *pEditor, const QString &text );
CYPHER_NODISCARD QString EditorCodeEditor_TextOf( QWidget *pEditor );
CYPHER_NODISCARD bool EditorCodeEditor_IsModified( QWidget *pEditor );
void EditorCodeEditor_SetModified( QWidget *pEditor, bool bModified );
void EditorCodeEditor_SetReadOnly( QWidget *pEditor, bool bReadOnly );
// Selects the next match after the cursor (wrapping); false when none.
CYPHER_NODISCARD bool EditorCodeEditor_Find( QWidget *pEditor, const QString &text, bool bBackward = false );
CYPHER_NODISCARD bool EditorCodeEditor_GoToLine( QWidget *pEditor, int line ); // 1-based.
// Rereads colours and font after a theme change.
void EditorCodeEditor_RefreshStyle( QWidget *pEditor );

// Highlight category at a position (tests and tooltips): "keyword", "type",
// "builtin", "number", "string", "comment", "preprocessor", "key", or "".
CYPHER_NODISCARD QString EditorCodeEditor_CategoryAt( QWidget *pEditor, int line, int column );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_CODE_EDITOR_H
