//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMason_MainWindow.h
//  Purpose: Declares Mason's main window: the editor framework, the Map
//           workspace, and the menus, toolbars, tool strip, panels, and
//           status bar that tie them together.
//  Details: Mason is the only code that lists every workspace (ADR 0008).
//           It owns the application-level commands - file, edit, view,
//           help - and routes them to the active workspace; the workspace
//           owns its own `map.*` commands. With one workspace so far the
//           routing is direct.
//
//           MASON_FLAG_HEADLESS turns every dialog into a log line so the
//           window can be driven by tests on the offscreen platform.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_MASON_MAIN_WINDOW_H
#define CYPHER_MASON_MAIN_WINDOW_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include "CypherEditorGui_Application.h"

#include <QStringList>

class QApplication;
class QMainWindow;

namespace cypher::mason
{

inline constexpr const char *MASON_DEFAULT_LAYOUT_RESOURCE = ":/cypher/editor/layouts/mason_default.cylayout";
inline constexpr const char *MASON_PANEL_VIEWS = "mason.views";
inline constexpr const char *MASON_PANEL_CONSOLE = "console";
inline constexpr const char *MASON_PANEL_OUTPUT = "output";
inline constexpr const char *MASON_PANEL_PROBLEMS = "problems";
inline constexpr const char *MASON_PANEL_ASSETS = "assets";
inline constexpr const char *MASON_PANEL_HISTORY = "map.history";
inline constexpr const char *MASON_PANEL_COMMAND_HISTORY = "command_history";

enum mason_flags_t : common::u32 {
    MASON_FLAG_NONE = 0u,
    MASON_FLAG_HEADLESS = 1u << 0u // No dialogs: questions take their safe answer, errors go to the log.
};

struct mason_t;
using mason_startup_callback_t = void ( * )( void *pContext, const editor::gui::editor_gui_t &gui, const char *pStage );

// Builds the editor and its window (not shown). Null on failure, with the
// reason in the log.
CYPHER_NODISCARD mason_t *Mason_Create( QApplication *pApplication, const common::allocator_t *pAllocator, common::u32 flags,
                                      mason_startup_callback_t pfnStartup = nullptr, void *pStartupContext = nullptr );
void Mason_Destroy( mason_t *pMason );

CYPHER_NODISCARD QMainWindow *Mason_Window( mason_t *pMason );
CYPHER_NODISCARD editor::gui::editor_gui_t *Mason_Gui( mason_t *pMason );
CYPHER_NODISCARD editor::map::map_workspace_t *Mason_MapWorkspace( mason_t *pMason );

// Opens a map, reporting failure in the log, the status bar, and - unless
// headless - a message box.
CYPHER_NODISCARD bool Mason_OpenMap( mason_t *pMason, const QString &path );

// Recent maps, newest first (File > Open Recent and the welcome window).
CYPHER_NODISCARD QStringList Mason_RecentMaps( mason_t *pMason );

// The welcome window (TrenchBroom's): New Map, Open Map, recent maps. Runs
// the choice. False when headless (nothing is shown).
bool Mason_ShowWelcome( mason_t *pMason );

// Status bar text, joined, for tests.
CYPHER_NODISCARD QString Mason_StatusText( mason_t *pMason );

} // namespace cypher::mason

#endif // CYPHER_MASON_MAIN_WINDOW_H
