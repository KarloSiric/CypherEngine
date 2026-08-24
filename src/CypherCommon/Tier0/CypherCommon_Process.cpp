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

//-----------------------------------------------------------------------------
// Process identity and termination
//
// Exit and abort are intentionally separate: exit performs normal runtime
// teardown, while abort is reserved for states that cannot continue safely.
//-----------------------------------------------------------------------------
namespace
{

struct process_path_t {
    char szPath[CY_PROCESS_PATH_MAX];
};

process_path_t Process_QueryExecutablePath() noexcept
{
    process_path_t result = {};

#if CYPHER_PLATFORM_WINDOWS
    wchar_t wszPath[CY_PROCESS_PATH_MAX] = {};
    const DWORD cchWritten = ::GetModuleFileNameW(
        nullptr,
        wszPath,
        static_cast<DWORD>( CY_PROCESS_PATH_MAX ) );
    if ( cchWritten == 0u || cchWritten >= CY_PROCESS_PATH_MAX ) {
        return result;
    }

    const int cchUtf8 = ::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        wszPath,
        static_cast<int>( cchWritten ),
        result.szPath,
        static_cast<int>( CY_PROCESS_PATH_MAX - 1u ),
        nullptr,
        nullptr );
    if ( cchUtf8 <= 0 ) {
        result.szPath[0] = '\0';
        return result;
    }
    result.szPath[static_cast<usize>( cchUtf8 )] = '\0';
#elif CYPHER_PLATFORM_MACOS
    u32 cchPath = static_cast<u32>( CY_PROCESS_PATH_MAX );
    if ( ::_NSGetExecutablePath( result.szPath, &cchPath ) != 0 ) {
        result.szPath[0] = '\0';
    }
#elif CYPHER_PLATFORM_LINUX
    const ssize_t cchWritten = ::readlink(
        "/proc/self/exe",
        result.szPath,
        CY_PROCESS_PATH_MAX - 1u );
    if ( cchWritten > 0 &&
         static_cast<usize>( cchWritten ) < CY_PROCESS_PATH_MAX - 1u ) {
        result.szPath[static_cast<usize>( cchWritten )] = '\0';
    } else {
        result.szPath[0] = '\0';
    }
#endif

    return result;
}

const process_path_t &Process_GetCachedExecutablePath() noexcept
{
    static const process_path_t path = Process_QueryExecutablePath();
    return path;
}

} // namespace

process_id_t Cy_ProcessGetCurrentId() noexcept
{
#if CYPHER_PLATFORM_WINDOWS
    return static_cast<process_id_t>( ::GetCurrentProcessId() );
#else
    return static_cast<process_id_t>( ::getpid() );
#endif
}

const char *Cy_ProcessGetExecutablePath() noexcept
{
    return Process_GetCachedExecutablePath().szPath;
}

[[noreturn]] void Cy_ProcessExit( i32 nExitCode ) noexcept
{
    std::exit( nExitCode );
}

} // namespace cypher::common
