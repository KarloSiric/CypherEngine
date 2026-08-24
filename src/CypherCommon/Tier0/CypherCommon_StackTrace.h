//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_StackTrace.h
//  Purpose: Captures bounded raw return addresses for diagnostics and crash reports.
//  Details: Tier0 does not symbolize or demangle frames; POSIX capture is not safe
//           from an asynchronous signal handler.
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

Raw synchronous stack capture for diagnostics, asserts, crash reports and tools.
Tier0 captures addresses only; symbol lookup and demangling belong above it.
The POSIX backend is not async-signal-safe and must not be called directly from
an operating-system signal handler.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Defines.h"

namespace cypher::common
{

constexpr u32 CYPHER_STACK_TRACE_MAX_FRAMES = 64u; // Fixed storage keeps capture allocation-free.

struct stack_frame_t {
    void *address{ nullptr }; // Raw instruction address; no ownership or symbol metadata.
};

struct stack_trace_t {
    stack_frame_t frames[CYPHER_STACK_TRACE_MAX_FRAMES]{}; // Oldest entries beyond frame_count are ignored.
    u32 frame_count{ 0u };                                 // Number of valid entries from frames[0].
};

CYPHER_COMMON_API void Cy_StackTraceClear( stack_trace_t *pTrace ) noexcept;

// Captures raw return addresses without resolving symbols.
CYPHER_NODISCARD CYPHER_COMMON_API u32 Cy_StackTraceCapture(
    stack_trace_t *pTrace,
    u32 cMaxFrames,
    u32 cSkipFrames ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API u32 Cy_StackTraceGetFrameCount(
    const stack_trace_t *pTrace ) noexcept;

// Returns a captured frame address or nullptr when the index is invalid.
CYPHER_NODISCARD CYPHER_COMMON_API void *Cy_StackTraceGetFrameAddress(
    const stack_trace_t *pTrace,
    u32 iFrame ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_StackTraceIsEmpty(
    const stack_trace_t *pTrace ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_STACKTRACE_H
