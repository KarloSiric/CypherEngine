//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_SettingControl.h
//  Purpose: Declares a control bound to one registered setting: a check
//           box, spin box, text field, or choice list that shows the
//           effective value and writes the user scope.
//  Details: Tool option panels (brush shape, clip mode, texture scale) are
//           settings, so the same value shows in the settings dialog, the
//           panel, and the settings file, and changing it anywhere updates
//           every control through the registry's listeners.
//
//  History:
//  - Created by Karlo Siric on 2026-09-30
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_SETTING_CONTROL_H
#define CYPHER_EDITOR_GUI_SETTING_CONTROL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherEditor_SettingsRegistry.h"

#include <QString>

class QWidget;

namespace cypher::editor::gui
{

// Null when the path is not registered. The control's object name is the
// path; its tooltip is the setting's description.
CYPHER_NODISCARD QWidget *EditorSettingControl_Create( QWidget *pParent, settings_registry_t *pRegistry, const char *pPath );

// The label a form shows beside the control.
CYPHER_NODISCARD QString EditorSettingControl_Label( const settings_registry_t *pRegistry, const char *pPath );

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_SETTING_CONTROL_H
