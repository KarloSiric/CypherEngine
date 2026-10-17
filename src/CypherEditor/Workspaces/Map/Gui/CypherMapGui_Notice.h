//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Notice.h
//  Purpose: Declares the one-line notices the map editor posts for the
//           window's status bar (Hammer 5's coloured bar: green when a map
//           is saved or a material applied, red when an action could not
//           run, with the reason).
//  Details: Posting logs the text and hands it to the sink the window
//           installed for the workspace. Without a sink (tests, headless)
//           the last notice is still kept, so it can be checked.
//
//  History:
//  - Created by Karlo Siric on 2026-10-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_GUI_NOTICE_H
#define CYPHER_EDITOR_MAP_GUI_NOTICE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMapGui_Workspace.h"

#include <functional>

#include <QString>

namespace cypher::editor::map
{

enum class map_notice_t : common::u8 { INFO = 0u, SUCCESS, WARNING, ERROR };

using map_notice_sink_t = std::function<void( map_notice_t, const QString & )>;

// Shows `text` in the window's status bar and logs it.
void MapNotice_Post( const map_workspace_t *pWorkspace, map_notice_t kind, const QString &text ) noexcept;

// The window's handler for the workspace's notices; an empty sink removes it.
void MapNotice_SetSink( const map_workspace_t *pWorkspace, map_notice_sink_t sink ) noexcept;

// The last notice posted for the workspace (empty when none).
CYPHER_NODISCARD QString MapNotice_Last( const map_workspace_t *pWorkspace, map_notice_t *pKindOut = nullptr ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_GUI_NOTICE_H
