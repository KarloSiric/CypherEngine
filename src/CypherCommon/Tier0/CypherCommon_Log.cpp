//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Log.cpp
//  Purpose: Implements CypherCommon Tier0 low-level logging.
//  Details: This layer provides callback-based logging before full engine logging
//           exists. Higher systems can install a callback and route records.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Log.h"

#include <cstdio>
#include <mutex>

namespace cypher::common
{
namespace
{

std::mutex g_logMutex;
log_callback_t g_logCallback = nullptr;
void *g_logUserData = nullptr;

} // namespace

void Cy_LogWrite( log_level_t level, log_channel_t channel, const char *pMessage )
{
    Cy_LogWriteError( level, channel, 0u, pMessage );
}

void Cy_LogWriteError( log_level_t level, log_channel_t channel, error_code_t errorCode, const char *pMessage )
{
    log_record_t record{};
    record.level = level;
    record.channel = channel;
    record.errorCode = errorCode;
    record.location = {};
    record.pMessage = pMessage != nullptr ? pMessage : "";

    std::lock_guard<std::mutex> lock( g_logMutex );
    if ( g_logCallback != nullptr ) {
        g_logCallback( record, g_logUserData );
        return;
    }

    std::fprintf( stderr, "[%s][%s] %s\n", Cy_LogLevelName( level ), Cy_LogChannelName( channel ), record.pMessage );
}

void Cy_LogSetCallback( log_callback_t pCallback, void *pUserData )
{
    std::lock_guard<std::mutex> lock( g_logMutex );
    g_logCallback = pCallback;
    g_logUserData = pUserData;
}

const char *Cy_LogLevelName( log_level_t level )
{
    switch ( level ) {
        case log_level_t::Trace: return "Trace";
        case log_level_t::Debug: return "Debug";
        case log_level_t::Info: return "Info";
        case log_level_t::Warning: return "Warning";
        case log_level_t::Error: return "Error";
        case log_level_t::Fatal: return "Fatal";
    }

    return "Unknown";
}

const char *Cy_LogChannelName( log_channel_t channel )
{
    switch ( channel ) {
        case log_channel_t::Common: return "Common";
        case log_channel_t::Memory: return "Memory";
        case log_channel_t::FileSystem: return "FileSystem";
        case log_channel_t::Pak: return "Pak";
        case log_channel_t::Render: return "Render";
        case log_channel_t::Audio: return "Audio";
        case log_channel_t::Network: return "Network";
        case log_channel_t::Physics: return "Physics";
        case log_channel_t::Game: return "Game";
        case log_channel_t::Tools: return "Tools";
        case log_channel_t::Editor: return "Editor";
    }

    return "Unknown";
}

} // namespace cypher::common
