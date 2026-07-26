//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Debug.cpp
//  Purpose: Implements CypherCommon Tier0 debug support.
//  Details: Provides platform debugger detection, debugger interruption, and
//           unconditional process termination without logging or allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-07-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Debug.h"

#include <cstdlib>

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <Windows.h>
    #include <intrin.h>
#elif CYPHER_PLATFORM_LINUX
    #include <csignal>
    #include <cstring>
    #include <fcntl.h>
    #include <unistd.h>
#elif CYPHER_PLATFORM_MACOS
    #include <csignal>
    #include <sys/proc.h>
    #include <sys/sysctl.h>
    #include <sys/types.h>
    #include <unistd.h>
#endif

namespace cypher::common
{

bool_t Cy_DebuggerIsAttached()
{
    #if CYPHER_PLATFORM_WINDOWS
        return ::IsDebuggerPresent() != FALSE;
    #elif CYPHER_PLATFORM_LINUX
        const int fd = ::open( "/proc/self/status", O_RDONLY | O_CLOEXEC );
        if ( fd < 0 ) {
            return false;
        }

        char status[4096];
        const ssize_t bytesRead = ::read( fd, status, sizeof( status ) - 1u );
        ::close( fd );

        if ( bytesRead <= 0 ) {
            return false;
        }
        status[bytesRead] = '\0';
        const char *pTracer = std::strstr( status, "TracerPid:" );
        if ( pTracer == nullptr ) {
            return false;
        }
        pTracer += sizeof( "TracerPid:" ) - 1u;
        while ( *pTracer == ' ' || *pTracer == '\t' ) {
            ++pTracer;
        }

        return *pTracer >= '1' && *pTracer <= '9';
    #elif CYPHER_PLATFORM_MACOS
        int query[4] = {
            CTL_KERN,
            KERN_PROC,
            KERN_PROC_PID,
            static_cast<int>( ::getpid() )
        };
        struct kinfo_proc processInfo = {};
        size_t processInfoSize = sizeof( processInfo );
        const int result = ::sysctl( query, 4u, &processInfo, &processInfoSize, nullptr, 0u );
        if ( result != 0 ) {
            return false;
        }

        return ( processInfo.kp_proc.p_flag & P_TRACED ) != 0;
    #endif
}

void Cy_DebugBreak()
{
    #if CYPHER_COMPILER_MSVC
        __debugbreak();
    #elif CYPHER_COMPILER_CLANG
        __builtin_debugtrap();
    #elif CYPHER_COMPILER_GCC
        std::raise( SIGTRAP );
    #else
        #error "Unsupported compiler for Cy_DebugBreak."
    #endif
}

[[noreturn]] void Cy_DebugTrap()
{
    std::abort();
}

} // namespace cypher::common
