//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Minidump.cpp
//  Purpose: Implements CypherCommon Tier0 portable diagnostic dump output.
//  Details: This writes deterministic text diagnostics with process, thread, and
//           raw stack information. Native OS minidump backends can be added later
//           without changing the public Tier0 call site.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Minidump.h"

#include "CypherCommon_Annotations.h"
#include "CypherCommon_Platform.h"
#include "CypherCommon_Process.h"
#include "CypherCommon_StackTrace.h"
#include "CypherCommon_Thread.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace cypher::common
{
namespace
{

// Path updates and dump writes use separate locks: callers may change the next
// output directory without allowing two crash reports to write one file at once.

constexpr usize CYPHER_MINIDUMP_PATH_CAPACITY = 1024u;
constexpr char CYPHER_MINIDUMP_DEFAULT_PATH[] = "CypherEngine.crash.txt";

std::mutex g_minidumpPathMutex;
std::mutex g_minidumpWriteMutex;
char g_minidumpOutputPath[1024] = {};

bool_t Minidump_CopyPath( char *pDest, usize cchDest, const char *pSource ) noexcept
{
    if ( pDest == nullptr || cchDest == 0u || pSource == nullptr ) {
        return CY_FALSE;
    }

    const usize cchSource = std::strlen( pSource );
    if ( cchSource >= cchDest ) {
        pDest[0] = '\0';
        return CY_FALSE;
    }

    std::memcpy( pDest, pSource, cchSource + 1u );
    return CY_TRUE;
}

bool_t Minidump_Printf( FILE *pFile, CY_PRINTF_FORMAT_STRING const char *pFormat, ... )
    CY_PRINTF_LIKE( 2, 3 );

bool_t Minidump_Printf( FILE *pFile, const char *pFormat, ... )
{
    std::va_list args;
    va_start( args, pFormat );
    const int cchWritten = std::vfprintf( pFile, pFormat, args );
    va_end( args );
    return cchWritten >= 0;
}

#if CYPHER_PLATFORM_WINDOWS
FILE *Minidump_OpenUtf8Path(
    const char *pszPath,
    minidump_result_t &result ) noexcept
{
    wchar_t wszPath[CYPHER_MINIDUMP_PATH_CAPACITY] = {};
    if ( ::MultiByteToWideChar(
             CP_UTF8,
             MB_ERR_INVALID_CHARS,
             pszPath,
             -1,
             wszPath,
             static_cast<int>( CYPHER_ARRAY_COUNT( wszPath ) ) ) <= 0 ) {
        result = minidump_result_t::InvalidArgument;
        return nullptr;
    }

    FILE *pFile = ::_wfopen( wszPath, L"w" );
    result = pFile != nullptr
        ? minidump_result_t::Ok
        : minidump_result_t::OpenFailed;
    return pFile;
}
#endif

} // namespace

minidump_result_t Cy_MinidumpWrite( const minidump_info_t &info ) noexcept
{
    // Minidump support is deliberately best-effort. Failure reporting must not
    // allocate recursively or turn an original crash into another unhandled one.
    char szPath[CYPHER_MINIDUMP_PATH_CAPACITY] = {};
    if ( info.pOutputPath != nullptr && info.pOutputPath[0] != '\0' ) {
        if ( !Minidump_CopyPath( szPath, CYPHER_ARRAY_COUNT( szPath ), info.pOutputPath ) ) {
            return minidump_result_t::PathTooLong;
        }
    } else {
        const usize cchPath =
            Cy_MinidumpGetOutputPath( szPath, CYPHER_ARRAY_COUNT( szPath ) );
        if ( cchPath >= CYPHER_ARRAY_COUNT( szPath ) ) {
            return minidump_result_t::PathTooLong;
        }
    }

    std::unique_lock<std::mutex> writeLock( g_minidumpWriteMutex, std::try_to_lock );
    if ( !writeLock.owns_lock() ) {
        return minidump_result_t::Busy;
    }

    minidump_result_t openResult = minidump_result_t::OpenFailed;
#if CYPHER_PLATFORM_WINDOWS
    FILE *pFile = Minidump_OpenUtf8Path( szPath, openResult );
#else
    FILE *pFile = std::fopen( szPath, "w" );
    if ( pFile != nullptr ) {
        openResult = minidump_result_t::Ok;
    }
#endif
    if ( pFile == nullptr ) {
        return openResult;
    }

    stack_trace_t trace{};
    const u32 cFrames = Cy_StackTraceCapture( &trace, info.maxFrames, info.skipFrames );

    const char *pApplication = info.pApplicationName != nullptr ? info.pApplicationName : "";
    const char *pVersion = info.pVersion != nullptr ? info.pVersion : "";
    const char *pReason = info.pReason != nullptr ? info.pReason : "";
    const char *pSourceFile = info.location.pFile != nullptr ? info.location.pFile : "";
    const char *pSourceFunction = info.location.pFunction != nullptr ? info.location.pFunction : "";

    bool_t bWriteSucceeded = CY_TRUE;
    bWriteSucceeded &= Minidump_Printf( pFile, "CypherEngine diagnostic dump\n" );
    bWriteSucceeded &= Minidump_Printf( pFile, "format=cypher-text-dump\n" );
    bWriteSucceeded &= Minidump_Printf( pFile, "format_version=1\n" );
    bWriteSucceeded &= Minidump_Printf( pFile, "application=%s\n", pApplication );
    bWriteSucceeded &= Minidump_Printf( pFile, "version=%s\n", pVersion );
    bWriteSucceeded &= Minidump_Printf( pFile, "reason=%s\n", pReason );
    bWriteSucceeded &= Minidump_Printf( pFile, "source_file=%s\n", pSourceFile );
    bWriteSucceeded &= Minidump_Printf(
        pFile,
        "source_line=%u\n",
        static_cast<unsigned int>( info.location.line ) );
    bWriteSucceeded &= Minidump_Printf( pFile, "source_function=%s\n", pSourceFunction );
    bWriteSucceeded &= Minidump_Printf(
        pFile,
        "process_id=%llu\n",
        static_cast<unsigned long long>( Cy_ProcessGetCurrentId() ) );
    bWriteSucceeded &= Minidump_Printf(
        pFile,
        "thread_id_hash=%llu\n",
        static_cast<unsigned long long>( Cy_ThreadGetCurrentIdHash() ) );
    bWriteSucceeded &= Minidump_Printf(
        pFile,
        "stack_frame_count=%u\n",
        static_cast<unsigned int>( cFrames ) );
    for ( u32 iFrame = 0u; iFrame < cFrames; ++iFrame ) {
        bWriteSucceeded &= Minidump_Printf(
            pFile,
            "stack_frame[%u]=%p\n",
            static_cast<unsigned int>( iFrame ),
            Cy_StackTraceGetFrameAddress( &trace, iFrame ) );
    }

    if ( std::fclose( pFile ) != 0 ) {
        bWriteSucceeded = CY_FALSE;
    }

    return bWriteSucceeded
        ? minidump_result_t::Ok
        : minidump_result_t::WriteFailed;
}

minidump_result_t Cy_MinidumpSetOutputPath( const char *pPath ) noexcept
{
    const char *pSource = pPath != nullptr ? pPath : "";
    if ( std::strlen( pSource ) >= CYPHER_MINIDUMP_PATH_CAPACITY ) {
        return minidump_result_t::PathTooLong;
    }

    std::lock_guard<std::mutex> lock( g_minidumpPathMutex );
    if ( !Minidump_CopyPath(
             g_minidumpOutputPath,
             CYPHER_ARRAY_COUNT( g_minidumpOutputPath ),
             pSource ) ) {
        return minidump_result_t::PathTooLong;
    }
    return minidump_result_t::Ok;
}

usize Cy_MinidumpGetOutputPath( char *pDest, usize cchDest ) noexcept
{
    std::lock_guard<std::mutex> lock( g_minidumpPathMutex );
    const char *pSource = g_minidumpOutputPath[0] != '\0'
        ? g_minidumpOutputPath
        : CYPHER_MINIDUMP_DEFAULT_PATH;
    const usize cchRequired = std::strlen( pSource );

    if ( pDest != nullptr && cchDest != 0u ) {
        const usize cchCopy = cchRequired < cchDest - 1u
            ? cchRequired
            : cchDest - 1u;
        std::memcpy( pDest, pSource, cchCopy );
        pDest[cchCopy] = '\0';
    }

    return cchRequired;
}

const char *Cy_MinidumpResultName( minidump_result_t result ) noexcept
{
    switch ( result ) {
        case minidump_result_t::Ok: return "Ok";
        case minidump_result_t::InvalidArgument: return "InvalidArgument";
        case minidump_result_t::PathTooLong: return "PathTooLong";
        case minidump_result_t::Busy: return "Busy";
        case minidump_result_t::OpenFailed: return "OpenFailed";
        case minidump_result_t::WriteFailed: return "WriteFailed";
        case minidump_result_t::Count:
            break;
    }

    return "Unknown";
}

} // namespace cypher::common
