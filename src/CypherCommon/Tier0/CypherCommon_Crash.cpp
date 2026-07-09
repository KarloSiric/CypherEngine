//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Crash.cpp
//  Purpose: Implements CypherCommon Tier0 crash reporting hooks.
//  Details: Crash hooks are intentionally tiny so higher layers can install
//           richer reporting without creating low-level dependencies.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Crash.h"

#include "CypherCommon_Debug.h"

#include <cstdio>
#include <mutex>

namespace cypher::common
{
namespace
{

std::mutex g_crashMutex;
crash_handler_t g_crashHandler = nullptr;

} // namespace

void Crash_SetHandler( crash_handler_t handler )
{
    std::lock_guard<std::mutex> lock( g_crashMutex );
    g_crashHandler = handler;
}

void Crash_ReportFatal( const char *pReason, const char *pFile, i32 line )
{
    std::lock_guard<std::mutex> lock( g_crashMutex );

    if ( g_crashHandler != nullptr ) {
        g_crashHandler( pReason, pFile, line );
        return;
    }

    std::fprintf( stderr,
                  "[Fatal] %s (%s:%d)\n",
                  pReason != nullptr ? pReason : "Unknown fatal error",
                  pFile != nullptr ? pFile : "unknown",
                  line );
}

void Crash_Trigger( const char *pReason )
{
    Crash_ReportFatal( pReason, __FILE__, __LINE__ );
    CYPHER_TRAP();
}

} // namespace cypher::common
