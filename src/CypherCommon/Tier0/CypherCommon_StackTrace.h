//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_StackTrace.h
//  Purpose: Declares CypherCommon Tier0 StackTrace support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_STACKTRACE_H
#define CYPHER_COMMON_TIER0_STACKTRACE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Stack Trace

Raw platform stack capture for diagnostics, asserts, crash reports and tools.
Tier0 captures addresses only; symbol lookup and demangling belong above it.
================
*/

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Defines.h"

namespace cypher::common
{

constexpr u32 CYPHER_STACK_TRACE_MAX_FRAMES = 64u;

struct stack_frame_t {
    void *address;
};

struct stack_trace_t {
    stack_frame_t frames[CYPHER_STACK_TRACE_MAX_FRAMES];
    u32 frame_count;
};

// Resets a stack trace to an empty state.
void Cy_StackTraceClear( stack_trace_t *pTrace );

// Captures raw return addresses without resolving symbols.
u32 Cy_StackTraceCapture( stack_trace_t *pTrace, u32 cMaxFrames, u32 cSkipFrames );

// Returns the number of captured frames in the trace.
u32 Cy_StackTraceGetFrameCount( const stack_trace_t *pTrace );

// Returns a captured frame address or nullptr when the index is invalid.
void *Cy_StackTraceGetFrameAddress( const stack_trace_t *pTrace, u32 iFrame );

// Returns true when the trace has no captured frames.
bool_t Cy_StackTraceIsEmpty( const stack_trace_t *pTrace );

inline void ClearStackTrace( stack_trace_t &trace )
{
    Cy_StackTraceClear( &trace );
}

inline u32 CaptureStackTrace( stack_trace_t &trace, u32 max_frames, u32 skip_frames )
{
    return Cy_StackTraceCapture( &trace, max_frames, skip_frames );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_STACKTRACE_H
