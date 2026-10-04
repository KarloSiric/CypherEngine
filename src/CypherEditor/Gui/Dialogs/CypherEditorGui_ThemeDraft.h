//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_ThemeDraft.h
//  Purpose: Declares the working copy every theme-editing surface shares
//           (the Theme Editor and the Appearance page of Settings): a
//           complete copy of the active theme, edited token by token,
//           previewed live, and saved as a complete theme.
//  Details: The draft is a root theme (no base): "auto" passes on to a
//           base (CYTHEME.md 3), so a base-relative draft could never set a
//           derived colour the base pins back to its formula. A snapshot of
//           the opening theme is kept for per-token revert and for marking
//           changed tokens. Values the user sets are always written, even
//           when equal to what the formula gives, so a pinned colour stays
//           pinned when its inputs change. Tokens no loaded module
//           registers are carried through unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-09-30 (extracted from the Theme Editor)
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_THEME_DRAFT_H
#define CYPHER_EDITOR_GUI_THEME_DRAFT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Application.h"

#include <QColor>
#include <QString>

#include <vector>

class QApplication;

namespace cypher::editor::gui
{

// One token's value in one theme.
struct theme_value_t {
    common::u32 rgba{ 0u };
    bool bFormula{ false }; // COLOR: follows its formula ("auto").
    theme_font_t font{};    // family borrows the theme document.
    common::f64 flMetric{ 0.0 };
    common::string_view_t choice{};
};

struct editor_theme_draft_t {
    editor_theme_draft_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( editor_theme_draft_t );
    ~editor_theme_draft_t() noexcept;

    editor_gui_t *pGui{ nullptr };
    QApplication *pApplication{ nullptr };
    common::settings_document_t snapshot{}; // The opening (or last saved) theme, complete.
    common::settings_document_t draft{};    // What is edited; a root theme.
    std::vector<const theme_token_t *> order{}; // Framework catalogue order, then other tokens by ID.
    QString previousId{};                   // The theme to go back to.
    QString builtinId{};
    bool bActive{ false };
};

// Starts from the active theme. False when the framework is not running.
CYPHER_NODISCARD bool EditorThemeDraft_Begin( editor_theme_draft_t *pDraft, editor_gui_t *pGui, QApplication *pApplication );
// Puts the previous theme back if the draft is being previewed, and frees.
void EditorThemeDraft_End( editor_theme_draft_t *pDraft );
// Starts over from the theme active now (after another theme was selected).
void EditorThemeDraft_Rebase( editor_theme_draft_t *pDraft );

CYPHER_NODISCARD const theme_token_t *EditorThemeDraft_Token( const editor_theme_draft_t *pDraft, const QString &id );
CYPHER_NODISCARD theme_value_t EditorThemeDraft_Value( const editor_theme_draft_t *pDraft, const theme_token_t &token, bool bSnapshot = false );
CYPHER_NODISCARD bool EditorThemeDraft_IsChanged( const editor_theme_draft_t *pDraft, const theme_token_t &token );
CYPHER_NODISCARD bool EditorThemeDraft_HasChanges( const editor_theme_draft_t *pDraft );

// Edits apply to the draft and preview through the whole editor at once
// unless bPreview is false (a colour dialog coalescing drags).
void EditorThemeDraft_Write( editor_theme_draft_t *pDraft, const theme_token_t &token, const theme_value_t &value, bool bPreview = true );
void EditorThemeDraft_SetColor( editor_theme_draft_t *pDraft, const theme_token_t &token, const QColor &color, bool bPreview = true );
// Derived colours only: follow the formula again.
void EditorThemeDraft_SetFormula( editor_theme_draft_t *pDraft, const theme_token_t &token );
void EditorThemeDraft_RevertToken( editor_theme_draft_t *pDraft, const theme_token_t &token );
void EditorThemeDraft_RevertAll( editor_theme_draft_t *pDraft );

void EditorThemeDraft_Preview( editor_theme_draft_t *pDraft );
void EditorThemeDraft_RestorePrevious( editor_theme_draft_t *pDraft );
CYPHER_NODISCARD bool EditorThemeDraft_IsPreviewing( const editor_theme_draft_t *pDraft );

// Saves the draft as a complete root theme `<folder>/<id>.cytheme`, loads
// and selects it, and records it in editor.ui.theme. Returns the file, or
// empty with a reason in pErrorOut.
CYPHER_NODISCARD QString EditorThemeDraft_Save(
    editor_theme_draft_t *pDraft,
    const QString &id,
    const QString &name,
    const QString &author,
    const QString &description,
    const QString &folder,
    QString *pErrorOut );

// The draft as complete theme text under an identity, for export.
CYPHER_NODISCARD QString EditorThemeDraft_Text( const editor_theme_draft_t *pDraft, const QString &id, const QString &name, const QString &author,
                                                const QString &description );

CYPHER_NODISCARD QColor EditorThemeDraft_ToColor( common::u32 rgba );
CYPHER_NODISCARD QString EditorThemeDraft_ColorText( common::u32 rgba );
// The formula as CYTHEME.md 4 writes it ("mix(ui.background, ui.text, 0.2)").
CYPHER_NODISCARD QString EditorThemeDraft_FormulaText( const theme_derive_t &derive );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_THEME_DRAFT_H
