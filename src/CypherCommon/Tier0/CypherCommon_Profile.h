//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Profile.h
//  Purpose: Defines synchronous profiling events for zones, counters, and frames.
//  Details: Event strings are borrowed for the sink call; the sink is responsible
//           for copying any metadata it needs after the callback returns.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_PROFILE_H
#define CYPHER_COMMON_TIER0_PROFILE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Profile

Profiling zone and counter declarations used to measure engine runtime,
renderer, VFS, tools and editor performance.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Defines.h"
#include "CypherCommon_SourceLocation.h"
#include "CypherCommon_Thread.h"
#include "CypherCommon_Timer.h"

namespace cypher::common
{

using profile_token_t = u64;
constexpr profile_token_t CY_PROFILE_INVALID_TOKEN = 0u; // Valid zone tokens begin at one.

enum profile_flags_t : flags32_t {
    PROFILE_FLAG_NONE = 0u,
    PROFILE_FLAG_CPU = CYPHER_BIT32( 0 ),  // CPU-side timed work.
    PROFILE_FLAG_GPU = CYPHER_BIT32( 1 ),  // GPU work submitted by a renderer backend.
    PROFILE_FLAG_TOOL = CYPHER_BIT32( 2 )  // Offline or editor operation.
};

struct profile_zone_desc_t {
    const char *pszName;        // Borrowed zone name valid during event delivery.
    const char *pszCategory;    // Borrowed grouping name valid during event delivery.
    source_location_t location; // Source of the instrumented zone.
    flags32_t flags;            // Bitwise combination of profile_flags_t.
};

enum class profile_event_type_t : u8 {
    ZoneBegin = 0u,
    ZoneEnd,
    CounterAdd,
    CounterSet,
    FrameBegin,
    FrameEnd
};

struct profile_event_t {
    profile_event_type_t type;    // Selects which payload fields are meaningful.
    profile_token_t token;        // Matches begin/end events; zero for non-zone events.
    timer_tick_t nTimestampTicks; // Monotonic Tier0 timer sample.
    u64 nFrameIndex;              // Current profiler frame sequence.
    thread_id_t nThreadId;        // Producer thread that emitted the event.
    profile_zone_desc_t zone;     // Valid for zone begin/end events.
    const char *pszCounterName;   // Borrowed name for counter events; null otherwise.
    i64 nCounterValue;            // Delta or absolute value selected by event type.
};

using profile_sink_fn_t =
    void ( CYPHER_CALL * )( const profile_event_t &event, void *pUserData ) noexcept;

struct profile_state_t {
    u64 nFrameIndex;                 // Current profiler frame sequence.
    u64 nEmittedEventCount;          // Events successfully delivered to the sink.
    u64 nDroppedReentrantEventCount; // Events suppressed while already inside the sink.
    bool_t isEnabled;                // Global event-production gate.
    bool_t hasSink;                  // A process-wide sink is currently installed.
};

// Installs a synchronous event sink. Event string pointers are callback-lifetime.
// Replacing a sink does not wait for callbacks already in flight; the caller must
// keep the previous sink and user data alive until emitting threads are quiescent.
CYPHER_COMMON_API void Cy_ProfileSetSink(
    profile_sink_fn_t pSink,
    void *pUserData = nullptr ) noexcept;
CYPHER_COMMON_API void Cy_ProfileSetEnabled( bool_t isEnabled ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ProfileIsEnabled() noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API profile_token_t Cy_ProfileBeginZone(
    const profile_zone_desc_t *pDesc ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ProfileEndZone(
    profile_token_t token ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ProfileCounterAdd(
    const char *pszName,
    i64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ProfileCounterSet(
    const char *pszName,
    i64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API u64 Cy_ProfileFrameBegin() noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_ProfileFrameEnd() noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API profile_state_t Cy_ProfileGetState() noexcept;

// Resets counters and token generation. Call only while profiler users are quiescent.
CYPHER_COMMON_API void Cy_ProfileResetState() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PROFILE_H
