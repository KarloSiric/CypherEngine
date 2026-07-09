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

#include "CypherCommon_Process.h"
#include "CypherCommon_StackTrace.h"
#include "CypherCommon_Thread.h"

#include <cstdio>
#include <mutex>

namespace cypher::common
{
namespace
{

std::mutex g_minidumpMutex;
char g_minidumpOutputPath[1024] = {};

const char *Minidump_SelectPath( const minidump_info_t &info )
{
    if ( info.pOutputPath != nullptr && info.pOutputPath[0] != '\0' ) {
        return info.pOutputPath;
    }

    if ( g_minidumpOutputPath[0] != '\0' ) {
        return g_minidumpOutputPath;
    }

    return "CypherEngine.minidump.txt";
}

} // namespace

bool_t Minidump_Write( const minidump_info_t &info )
{
    std::lock_guard<std::mutex> lock( g_minidumpMutex );

    const char *pPath = Minidump_SelectPath( info );
    FILE *pFile = std::fopen( pPath, "w" );
    if ( pFile == nullptr ) {
        return CY_FALSE;
    }

    stack_trace_t trace{};
    const u32 cFrames = Cy_StackTraceCapture( &trace, CYPHER_STACK_TRACE_MAX_FRAMES, 1u );

    std::fprintf( pFile, "CypherEngine diagnostic dump\n" );
    std::fprintf( pFile, "format=cypher-text-dump\n" );
    std::fprintf( pFile, "application=%s\n", info.pApplicationName != nullptr ? info.pApplicationName : "" );
    std::fprintf( pFile, "version=%s\n", info.pVersion != nullptr ? info.pVersion : "" );
    std::fprintf( pFile, "process_id=%llu\n", static_cast<unsigned long long>( Process_GetCurrentId() ) );
    std::fprintf( pFile, "thread_id_hash=%llu\n", static_cast<unsigned long long>( Cy_ThreadGetCurrentIdHash() ) );
    std::fprintf( pFile, "stack_frame_count=%u\n", cFrames );
    for ( u32 iFrame = 0u; iFrame < cFrames; ++iFrame ) {
        std::fprintf( pFile, "stack_frame[%u]=%p\n", iFrame, Cy_StackTraceGetFrameAddress( &trace, iFrame ) );
    }
    std::fclose( pFile );
    return CY_TRUE;
}

void Minidump_SetOutputPath( const char *pPath )
{
    std::lock_guard<std::mutex> lock( g_minidumpMutex );

    usize i = 0u;
    const char *pSrc = pPath != nullptr ? pPath : "";
    for ( ; i + 1u < sizeof( g_minidumpOutputPath ) && pSrc[i] != '\0'; ++i ) {
        g_minidumpOutputPath[i] = pSrc[i];
    }
    g_minidumpOutputPath[i] = '\0';
}

} // namespace cypher::common
