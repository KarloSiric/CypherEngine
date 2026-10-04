//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Section.h
//  Purpose: Declares collapsible panel sections, Hammer 5's Tool Properties
//           style: a bold centred title strip with a -/+ marker that folds
//           the body away.
//  Details: Long tool panels (texture state, modify texture, operations)
//           stay usable because each group folds independently; clicking
//           the title strip toggles it.
//
//  History:
//  - Created by Karlo Siric on 2026-09-30
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_SECTION_H
#define CYPHER_EDITOR_GUI_SECTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier0/CypherCommon_Annotations.h"

#include <QString>

class QWidget;

namespace cypher::editor::gui
{

// Wraps pBody (reparented) under a title strip. Styled through
// QWidget#EditorSection, #EditorSectionHeader, and #EditorSectionBody.
CYPHER_NODISCARD QWidget *EditorSection_Create( QWidget *pParent, const QString &title, QWidget *pBody, bool bExpanded = true );

void EditorSection_SetExpanded( QWidget *pSection, bool bExpanded );
CYPHER_NODISCARD bool EditorSection_IsExpanded( const QWidget *pSection );
CYPHER_NODISCARD QWidget *EditorSection_Body( const QWidget *pSection );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_SECTION_H
