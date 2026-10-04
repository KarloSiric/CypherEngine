//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMason_Welcome.h
//  Purpose: Declares Mason's welcome window: the Mason logo, New Map,
//           Open Map, and the recent maps, after TrenchBroom's.
//  Details: Shown at start when no map was named on the command line and
//           editor.ui.show_welcome is on; Help > Welcome brings it back. It
//           only reports the choice; Mason does the opening, so the same
//           checks (unsaved changes, errors) apply as from the File menu.
//
//  History:
//  - Created by Karlo Siric on 2026-10-02
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_MASON_WELCOME_H
#define CYPHER_MASON_WELCOME_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditorGui_Style.h"

#include <QStringList>

class QDialog;
class QWidget;

namespace cypher::mason
{

enum class mason_welcome_choice_t : int {
    NONE = 0, // Closed.
    NEW_MAP,
    BROWSE,   // Open Map...: show the file dialog.
    OPEN      // A recent map; MasonWelcome_ChosenFile names it.
};

CYPHER_NODISCARD QDialog *MasonWelcome_Create( QWidget *pParent, const editor::gui::editor_style_t &style, const QStringList &recentMaps,
                                              bool bShowAtStartup );

CYPHER_NODISCARD mason_welcome_choice_t MasonWelcome_Choice( QDialog *pWelcome );
CYPHER_NODISCARD QString MasonWelcome_ChosenFile( QDialog *pWelcome );
CYPHER_NODISCARD bool MasonWelcome_ShowAtStartup( QDialog *pWelcome );

// Automation and tests.
CYPHER_NODISCARD QStringList MasonWelcome_Rows( QDialog *pWelcome ); // "name\tfolder" per recent map.
void MasonWelcome_Choose( QDialog *pWelcome, mason_welcome_choice_t choice, int iRecent = -1 );

} // namespace cypher::mason

#endif // CYPHER_MASON_WELCOME_H
