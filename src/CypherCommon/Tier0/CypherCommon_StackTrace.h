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
#pragma once

/*
================
CypherCommon Stack Trace

Small placeholder API for future platform stack capture.
No allocation and no symbol loading at Tier0.
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
inline void ClearStackTrace( stack_trace_t &trace )
{
    trace.frame_count = 0u;
    for ( u32 i = 0u; i < CYPHER_STACK_TRACE_MAX_FRAMES; ++i ) {
        trace.frames[i].address = nullptr;
    }
}

// Captures stack frames when platform support is implemented.
inline u32 CaptureStackTrace( stack_trace_t &trace, u32 max_frames, u32 skip_frames )
{
    CYPHER_UNUSED( max_frames );
    CYPHER_UNUSED( skip_frames );
    ClearStackTrace( trace );
    return 0u;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_STACKTRACE_H
