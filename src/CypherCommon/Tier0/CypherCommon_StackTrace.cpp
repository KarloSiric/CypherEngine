//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_StackTrace.cpp
//  Purpose: Implements CypherCommon Tier0 raw stack trace capture.
//  Details: This module captures instruction addresses only. Symbol lookup,
//           demangling, and crash-report formatting belong in higher diagnostic
//           layers so Tier0 stays allocation-light and platform focused.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StackTrace.h"

#include "CypherCommon_Platform.h"

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#elif CYPHER_PLATFORM_POSIX
    #include <execinfo.h>
#endif

namespace cypher::common
{

namespace
{

constexpr u32 CYPHER_STACK_TRACE_CAPTURE_LIMIT = 128u;

constexpr u32 ClampCaptureCount( u32 cMaxFrames ) noexcept
{
    return cMaxFrames < CYPHER_STACK_TRACE_MAX_FRAMES ? cMaxFrames : CYPHER_STACK_TRACE_MAX_FRAMES;
}

} // namespace

void Cy_StackTraceClear( stack_trace_t *pTrace ) noexcept
{
    if ( pTrace == nullptr ) {
        return;
    }

    pTrace->frame_count = 0u;
    for ( u32 iFrame = 0u; iFrame < CYPHER_STACK_TRACE_MAX_FRAMES; ++iFrame ) {
        pTrace->frames[iFrame].address = nullptr;
    }
}

u32 Cy_StackTraceCapture( stack_trace_t *pTrace, u32 cMaxFrames, u32 cSkipFrames ) noexcept
{
    if ( pTrace == nullptr ) {
        return 0u;
    }

    Cy_StackTraceClear( pTrace );

    const u32 cOutputMax = ClampCaptureCount( cMaxFrames );
    if ( cOutputMax == 0u ) {
        return 0u;
    }

    // One internal capture frame is always skipped in addition to caller input.
    if ( cSkipFrames >= CYPHER_STACK_TRACE_CAPTURE_LIMIT - 1u ) {
        return 0u;
    }

#if CYPHER_PLATFORM_WINDOWS
    void *pCaptured[CYPHER_STACK_TRACE_MAX_FRAMES] = {};
    const USHORT cCaptured = CaptureStackBackTrace(
        static_cast<DWORD>( cSkipFrames + 1u ),
        static_cast<DWORD>( cOutputMax ),
        pCaptured,
        nullptr );

    for ( USHORT iFrame = 0u; iFrame < cCaptured; ++iFrame ) {
        pTrace->frames[iFrame].address = pCaptured[iFrame];
    }

    pTrace->frame_count = static_cast<u32>( cCaptured );
    return pTrace->frame_count;
#elif CYPHER_PLATFORM_POSIX
    void *pCaptured[CYPHER_STACK_TRACE_CAPTURE_LIMIT] = {};
    const u32 cAvailableAfterSkip = CYPHER_STACK_TRACE_CAPTURE_LIMIT - cSkipFrames - 1u;
    const u32 cRequestedOutput = cOutputMax < cAvailableAfterSkip
        ? cOutputMax
        : cAvailableAfterSkip;
    const u32 cRequested = cRequestedOutput + cSkipFrames + 1u;

    const int cCaptured = ::backtrace( pCaptured, static_cast<int>( cRequested ) );
    if ( cCaptured <= 0 ) {
        return 0u;
    }

    const u32 iFirstFrame = cSkipFrames + 1u;
    if ( iFirstFrame >= static_cast<u32>( cCaptured ) ) {
        return 0u;
    }

    u32 cOutput = 0u;
    for ( u32 iFrame = iFirstFrame; iFrame < static_cast<u32>( cCaptured ) && cOutput < cOutputMax; ++iFrame ) {
        pTrace->frames[cOutput].address = pCaptured[iFrame];
        ++cOutput;
    }

    pTrace->frame_count = cOutput;
    return cOutput;
#else
    return 0u;
#endif
}

u32 Cy_StackTraceGetFrameCount( const stack_trace_t *pTrace ) noexcept
{
    if ( pTrace == nullptr ) {
        return 0u;
    }

    return pTrace->frame_count;
}

void *Cy_StackTraceGetFrameAddress( const stack_trace_t *pTrace, u32 iFrame ) noexcept
{
    if ( pTrace == nullptr || iFrame >= pTrace->frame_count || iFrame >= CYPHER_STACK_TRACE_MAX_FRAMES ) {
        return nullptr;
    }

    return pTrace->frames[iFrame].address;
}

bool_t Cy_StackTraceIsEmpty( const stack_trace_t *pTrace ) noexcept
{
    return Cy_StackTraceGetFrameCount( pTrace ) == 0u;
}

} // namespace cypher::common
