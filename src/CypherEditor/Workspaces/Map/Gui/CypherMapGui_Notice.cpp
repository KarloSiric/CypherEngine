//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Notice.cpp
//  Purpose: Implements the map editor's status bar notices.
//
//  History:
//  - Created by Karlo Siric on 2026-10-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Notice.h"

#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <unordered_map>

namespace cypher::editor::map
{
using namespace common;

namespace
{
struct notice_state_t {
    map_notice_sink_t sink;
    map_notice_t lastKind{ map_notice_t::INFO };
    QString last;
};

std::unordered_map<const map_workspace_t *, notice_state_t> g_notices;
} // namespace

void MapNotice_Post( const map_workspace_t *pWorkspace, map_notice_t kind, const QString &text ) noexcept
{
    if ( pWorkspace == nullptr || text.isEmpty() ) { return; }
    const QByteArray utf8 = text.toUtf8();
    const auto level = kind == map_notice_t::ERROR ? log_level_t::Error : kind == map_notice_t::WARNING ? log_level_t::Warning : log_level_t::Info;
    Cy_LogWriteAt( level, log_channel_t::Editor, utf8.constData(), CY_SOURCE_LOCATION );
    auto &state = g_notices[pWorkspace];
    state.last = text; state.lastKind = kind;
    if ( state.sink ) { state.sink( kind, text ); }
}

void MapNotice_SetSink( const map_workspace_t *pWorkspace, map_notice_sink_t sink ) noexcept
{
    if ( pWorkspace == nullptr ) { return; }
    if ( !sink ) { g_notices.erase( pWorkspace ); return; }
    g_notices[pWorkspace].sink = std::move( sink );
}

QString MapNotice_Last( const map_workspace_t *pWorkspace, map_notice_t *pKindOut ) noexcept
{
    const auto found = g_notices.find( pWorkspace );
    if ( found == g_notices.end() ) { return QString(); }
    if ( pKindOut != nullptr ) { *pKindOut = found->second.lastKind; }
    return found->second.last;
}

} // namespace cypher::editor::map
