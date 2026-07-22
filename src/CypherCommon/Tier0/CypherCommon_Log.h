//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Log.h
//  Purpose: Declares CypherCommon Tier0 Log support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_LOG_H
#define CYPHER_COMMON_TIER0_LOG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Log

Low-level logging declarations used by Common diagnostics before higher engine
logging layers are available.
================
*/

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Error.h"
#include "CypherCommon_SourceLocation.h"

namespace cypher::common
{

enum class log_level_t : u8 {
    Trace = 0u,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

enum class log_channel_t : u16 {
    Common = 0u,
    Memory,
    FileSystem,
    Pak,
    Render,
    Audio,
    Network,
    Physics,
    Game,
    Tools,
    Editor
};

struct log_record_t {
    log_level_t level;
    log_channel_t channel;
    error_code_t errorCode;
    source_location_t location;
    const char *pMessage;
};

using log_callback_t = void ( * )( const log_record_t &record, void *pUserData );

void Cy_LogWrite( log_level_t level, log_channel_t channel, const char *pMessage );
void Cy_LogWriteError( log_level_t level, log_channel_t channel, error_code_t errorCode, const char *pMessage );
void Cy_LogSetCallback( log_callback_t pCallback, void *pUserData );
const char *Cy_LogLevelName( log_level_t level );
const char *Cy_LogChannelName( log_channel_t channel );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_LOG_H
