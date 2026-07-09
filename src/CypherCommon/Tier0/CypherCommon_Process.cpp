//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Process.cpp
//  Purpose: Implements CypherCommon Tier0 process helpers.
//  Details: Process helpers provide process identity and executable path data
//           used by logs, crash reports, tools, and editor diagnostics.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Process.h"

#include "CypherCommon_Platform.h"

#include <cstdlib>

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif CYPHER_PLATFORM_MACOS
    #include <mach-o/dyld.h>
    #include <unistd.h>
#elif CYPHER_PLATFORM_LINUX
    #include <unistd.h>
#endif

namespace cypher::common
{

process_id_t Process_GetCurrentId()
{
#if CYPHER_PLATFORM_WINDOWS
    return static_cast<process_id_t>( ::GetCurrentProcessId() );
#else
    return static_cast<process_id_t>( ::getpid() );
#endif
}

const char *Process_GetExecutablePath()
{
    static char szPath[4096] = {};
    if ( szPath[0] != '\0' ) {
        return szPath;
    }

#if CYPHER_PLATFORM_WINDOWS
    const DWORD cchWritten = ::GetModuleFileNameA( nullptr, szPath, static_cast<DWORD>( sizeof( szPath ) ) );
    if ( cchWritten == 0u || cchWritten >= sizeof( szPath ) ) {
        szPath[0] = '\0';
    }
#elif CYPHER_PLATFORM_MACOS
    u32 cchPath = static_cast<u32>( sizeof( szPath ) );
    if ( _NSGetExecutablePath( szPath, &cchPath ) != 0 ) {
        szPath[0] = '\0';
    }
#elif CYPHER_PLATFORM_LINUX
    const ssize_t cchWritten = ::readlink( "/proc/self/exe", szPath, sizeof( szPath ) - 1u );
    if ( cchWritten > 0 ) {
        szPath[cchWritten] = '\0';
    } else {
        szPath[0] = '\0';
    }
#endif

    return szPath;
}

void Process_Exit( i32 exit_code )
{
    std::exit( exit_code );
}

} // namespace cypher::common
