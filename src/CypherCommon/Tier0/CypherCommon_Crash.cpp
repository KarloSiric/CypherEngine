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

#include <atomic>
#include <cstdio>

namespace cypher::common
{
namespace
{

std::atomic<crash_handler_t> g_crashHandler{ nullptr };
thread_local bool_t g_crashHandlingReport = CY_FALSE;

void Crash_WriteFallback( const crash_info_t &info ) noexcept
{
    std::fprintf( stderr,
                  "[Fatal] %s (%s:%u:%s)\n",
                  info.pReason,
                  info.location.pFile,
                  static_cast<unsigned int>( info.location.line ),
                  info.location.pFunction );
    std::fflush( stderr );
}

} // namespace

void Cy_CrashSetHandler( crash_handler_t pHandler ) noexcept
{
    g_crashHandler.store( pHandler, std::memory_order_release );
}

crash_handler_t Cy_CrashGetHandler() noexcept
{
    return g_crashHandler.load( std::memory_order_acquire );
}

void Cy_CrashReport( const char *pReason, source_location_t location ) noexcept
{
    crash_info_t info{};
    info.pReason = pReason != nullptr && pReason[0] != '\0'
        ? pReason
        : "Unknown fatal error";
    info.location.pFile = location.pFile != nullptr && location.pFile[0] != '\0'
        ? location.pFile
        : "<unknown file>";
    info.location.pFunction = location.pFunction != nullptr && location.pFunction[0] != '\0'
        ? location.pFunction
        : "<unknown function>";
    info.location.line = location.line;
    info.location.column = location.column;

    if ( g_crashHandlingReport ) {
        Crash_WriteFallback( info );
        return;
    }

    g_crashHandlingReport = CY_TRUE;
    const crash_handler_t pHandler = Cy_CrashGetHandler();
    if ( pHandler != nullptr ) {
        pHandler( info );
    } else {
        Crash_WriteFallback( info );
    }
    g_crashHandlingReport = CY_FALSE;
}

[[noreturn]] void Cy_CrashTrigger( const char *pReason, source_location_t location ) noexcept
{
    Cy_CrashReport( pReason, location );
    CY_TRAP();
}

} // namespace cypher::common
