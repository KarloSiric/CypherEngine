//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Profile.h
//  Purpose: Declares CypherCommon Tier0 Profile support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
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
constexpr profile_token_t CY_PROFILE_INVALID_TOKEN = 0u;

enum profile_flags_t : flags32_t {
    PROFILE_FLAG_NONE = 0u,
    PROFILE_FLAG_CPU = CYPHER_BIT32( 0 ),
    PROFILE_FLAG_GPU = CYPHER_BIT32( 1 ),
    PROFILE_FLAG_TOOL = CYPHER_BIT32( 2 )
};

struct profile_zone_desc_t {
    const char *pszName;
    const char *pszCategory;
    source_location_t location;
    flags32_t flags;
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
    profile_event_type_t type;
    profile_token_t token;
    timer_tick_t nTimestampTicks;
    u64 nFrameIndex;
    thread_id_t nThreadId;
    profile_zone_desc_t zone;
    const char *pszCounterName;
    i64 nCounterValue;
};

using profile_sink_fn_t =
    void ( CYPHER_CALL * )( const profile_event_t &event, void *pUserData ) noexcept;

struct profile_state_t {
    u64 nFrameIndex;
    u64 nEmittedEventCount;
    u64 nDroppedReentrantEventCount;
    bool_t isEnabled;
    bool_t hasSink;
};

// Installs a synchronous event sink. Event string pointers are callback-lifetime.
// Replacing a sink does not wait for callbacks already in flight; the caller must
// keep the previous sink and user data alive until emitting threads are quiescent.
CYPHER_COMMON_API void Cy_ProfileSetSink(
    profile_sink_fn_t pSink,
    void *pUserData = nullptr ) noexcept;
CYPHER_COMMON_API void Cy_ProfileSetEnabled( bool_t isEnabled ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ProfileIsEnabled() noexcept;

[[nodiscard]] CYPHER_COMMON_API profile_token_t Cy_ProfileBeginZone(
    const profile_zone_desc_t *pDesc ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ProfileEndZone(
    profile_token_t token ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ProfileCounterAdd(
    const char *pszName,
    i64 value ) noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ProfileCounterSet(
    const char *pszName,
    i64 value ) noexcept;
[[nodiscard]] CYPHER_COMMON_API u64 Cy_ProfileFrameBegin() noexcept;
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_ProfileFrameEnd() noexcept;
[[nodiscard]] CYPHER_COMMON_API profile_state_t Cy_ProfileGetState() noexcept;

// Resets counters and token generation. Call only while profiler users are quiescent.
CYPHER_COMMON_API void Cy_ProfileResetState() noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_PROFILE_H
