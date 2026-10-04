//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditorGui_Log.h
//  Purpose: Declares the editor's log store: every record the process logs
//           from the first line of initialization, kept with its time,
//           level, channel, and source, for the Console, Output, and
//           Problems panels.
//  Details: One store per editor, installed as the Tier0 log sink when the
//           framework starts, so the console shows the whole start-up
//           sequence even though it is created later - an in-game
//           console's boot log. The previous sink (the terminal) keeps
//           receiving everything.
//
//           Records may arrive on any thread; they are appended on the GUI
//           thread (immediately when logged there, queued otherwise), so
//           listeners run on the GUI thread only.
//
//  History:
//  - Created by Karlo Siric on 2026-09-30
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GUI_LOG_H
#define CYPHER_EDITOR_GUI_LOG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <QElapsedTimer>
#include <QString>
#include <QVector>

namespace cypher::editor::gui
{

inline constexpr int EDITOR_LOG_MAX_ENTRIES = 20000;     // Oldest records drop beyond this.
inline constexpr common::usize EDITOR_LOG_MAX_LISTENERS = 16u;

struct editor_log_entry_t {
    qint64 msTime{ 0 };                                              // Since the store started.
    common::log_level_t level{ common::log_level_t::Info };
    common::log_channel_t channel{ common::log_channel_t::Common };
    QString message{};
    QString file{};                                                  // Source file, when the call site gave one.
    int line{ 0 };
    bool bCommand{ false };                                          // An echoed console command line.
};

// entry is null when the store was cleared.
using editor_log_listener_fn = void ( * )( void *pContext, const editor_log_entry_t *pEntry ) noexcept;

struct editor_log_listener_t {
    editor_log_listener_fn pfnChanged{ nullptr };
    void *pContext{ nullptr };
};

struct editor_log_t {
    QVector<editor_log_entry_t> entries{};
    common::usize nDropped{ 0u };
    common::usize counts[static_cast<common::usize>( common::log_level_t::Count )]{};
    editor_log_listener_t listeners[EDITOR_LOG_MAX_LISTENERS]{};
    common::usize nListeners{ 0u };
    QElapsedTimer clock{};
    common::log_callback_t pfnPrevious{ nullptr };
    void *pPreviousUserData{ nullptr };
    bool bInstalled{ false };
};

void EditorLog_Init( editor_log_t *pLog );

// Becomes the process log sink (chaining the one before it); Uninstall
// restores that one. Install and uninstall in reverse order.
void EditorLog_Install( editor_log_t *pLog );
void EditorLog_Uninstall( editor_log_t *pLog );

// Appends on the calling (GUI) thread.
void EditorLog_Append( editor_log_t *pLog, common::log_level_t level, common::log_channel_t channel, const QString &message,
                       const char *pFile = nullptr, int line = 0, bool bCommand = false );

void EditorLog_Clear( editor_log_t *pLog );
CYPHER_NODISCARD common::usize EditorLog_Count( const editor_log_t *pLog, common::log_level_t level ) noexcept;
// Warnings and above (errors, fatals) since the last clear.
CYPHER_NODISCARD common::usize EditorLog_ProblemCount( const editor_log_t *pLog ) noexcept;

CYPHER_NODISCARD bool EditorLog_AddListener( editor_log_t *pLog, editor_log_listener_fn pfnChanged, void *pContext ) noexcept;
void EditorLog_RemoveListener( editor_log_t *pLog, editor_log_listener_fn pfnChanged, void *pContext ) noexcept;

// "INFO", "WARN", "ERROR": the short level names the panels show.
CYPHER_NODISCARD const char *EditorLog_LevelTag( common::log_level_t level ) noexcept;

} // namespace cypher::editor::gui

#endif // CYPHER_EDITOR_GUI_LOG_H
