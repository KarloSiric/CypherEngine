//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Profile.cpp
//  Purpose: Implements the allocation-free Tier0 profiling event spine.
//  Details: Runtime code emits timestamped synchronous events to a replaceable
//           sink. This layer owns no UI, trace file, heap map, or profiler backend.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//  - Reworked into an event sink on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Profile.h"

#include "CypherCommon_Stats.h"

#include <atomic>
#include <mutex>

namespace cypher::common
{
namespace
{

// The sequence counter makes the sink function and user pointer one coherent
// snapshot for readers. Odd values mean a writer is replacing the pair.

std::mutex g_profileSinkWriteMutex;
std::atomic<profile_sink_fn_t> g_profileSink = nullptr;
std::atomic<void *> g_profileSinkUserData = nullptr;
std::atomic<u64> g_profileSinkSequence = 0u;
std::atomic_bool g_profileEnabled = false;
std::atomic<u64> g_profileNextToken = 1u;
std::atomic<u64> g_profileFrameIndex = 0u;
std::atomic<u64> g_profileEmittedEventCount = 0u;
std::atomic<u64> g_profileDroppedReentrantEventCount = 0u;
thread_local bool_t g_isInsideProfileSink = CY_FALSE;

struct profile_sink_snapshot_t {
    profile_sink_fn_t pSink; // Callback and context must come from one revision.
    void *pUserData;
};

profile_sink_snapshot_t Profile_GetSinkSnapshot() noexcept
{
    profile_sink_snapshot_t snapshot{};
    // This is a small sequence-lock read. Writers make the sequence odd while
    // replacing the pair; readers retry if a write overlapped their snapshot.
    for ( ;; ) {
        const u64 nBegin =
            g_profileSinkSequence.load( std::memory_order_acquire );
        if ( ( nBegin & 1u ) != 0u ) {
            continue;
        }

        snapshot.pSink = g_profileSink.load( std::memory_order_acquire );
        snapshot.pUserData =
            g_profileSinkUserData.load( std::memory_order_acquire );

        const u64 nEnd =
            g_profileSinkSequence.load( std::memory_order_acquire );
        if ( nBegin == nEnd ) {
            return snapshot;
        }
    }
}

bool_t Profile_Emit( const profile_event_t &event ) noexcept
{
    // A sink may log or profile internally. Drop that nested event instead of
    // recursing into user code until the thread stack is exhausted.
    if ( g_isInsideProfileSink ) {
        g_profileDroppedReentrantEventCount.fetch_add(
            1u,
            std::memory_order_relaxed );
        return CY_FALSE;
    }

    const profile_sink_snapshot_t sink = Profile_GetSinkSnapshot();
    if ( sink.pSink == nullptr ) {
        return CY_FALSE;
    }

    g_isInsideProfileSink = CY_TRUE;
    sink.pSink( event, sink.pUserData );
    g_isInsideProfileSink = CY_FALSE;
    g_profileEmittedEventCount.fetch_add( 1u, std::memory_order_relaxed );
    return CY_TRUE;
}

profile_event_t Profile_MakeEvent( profile_event_type_t type ) noexcept
{
    profile_event_t event{};
    event.type = type;
    event.nTimestampTicks = Cy_TimerNowTicks();
    event.nFrameIndex =
        g_profileFrameIndex.load( std::memory_order_relaxed );
    event.nThreadId = Cy_ThreadGetCurrentId();
    return event;
}

bool_t Profile_GetOrRegisterCounter(
    const char *pszName,
    stat_id_t &outId ) noexcept
{
    outId = Cy_StatsFind( pszName );
    if ( outId != CY_STAT_ID_INVALID ) {
        return CY_TRUE;
    }

    const stat_desc_t desc{
        pszName,
        "Profile",
        "Profile counter",
        stat_value_type_t::I64
    };
    return Cy_StatsRegister( desc, &outId );
}

} // namespace

void Cy_ProfileSetSink(
    profile_sink_fn_t pSink,
    void *pUserData ) noexcept
{
    try {
        std::lock_guard<std::mutex> lock( g_profileSinkWriteMutex );
        // Odd marks the pair unstable. Publishing the final even value makes the
        // new callback and user pointer visible as one logical update.
        g_profileSinkSequence.fetch_add( 1u, std::memory_order_acq_rel );
        g_profileSinkUserData.store( pUserData, std::memory_order_release );
        g_profileSink.store( pSink, std::memory_order_release );
        g_profileSinkSequence.fetch_add( 1u, std::memory_order_release );
    } catch ( ... ) {
        return;
    }
}

void Cy_ProfileSetEnabled( bool_t isEnabled ) noexcept
{
    g_profileEnabled.store( isEnabled, std::memory_order_release );
}

bool_t Cy_ProfileIsEnabled() noexcept
{
    return g_profileEnabled.load( std::memory_order_acquire );
}

profile_token_t Cy_ProfileBeginZone(
    const profile_zone_desc_t *pDesc ) noexcept
{
    if ( !Cy_ProfileIsEnabled() ||
         pDesc == nullptr ||
         pDesc->pszName == nullptr ||
         pDesc->pszName[0] == '\0' ||
         g_profileSink.load( std::memory_order_acquire ) == nullptr ) {
        return CY_PROFILE_INVALID_TOKEN;
    }

    // Zero is reserved as the invalid token. Skip it if the monotonic counter
    // wraps after an extremely long process lifetime.
    profile_token_t token =
        g_profileNextToken.fetch_add( 1u, std::memory_order_relaxed );
    if ( token == CY_PROFILE_INVALID_TOKEN ) {
        token = g_profileNextToken.fetch_add( 1u, std::memory_order_relaxed );
        if ( token == CY_PROFILE_INVALID_TOKEN ) {
            return CY_PROFILE_INVALID_TOKEN;
        }
    }

    profile_event_t event = Profile_MakeEvent( profile_event_type_t::ZoneBegin );
    event.token = token;
    event.zone = *pDesc;
    return Profile_Emit( event ) ? token : CY_PROFILE_INVALID_TOKEN;
}

bool_t Cy_ProfileEndZone( profile_token_t token ) noexcept
{
    if ( !Cy_ProfileIsEnabled() || token == CY_PROFILE_INVALID_TOKEN ) {
        return CY_FALSE;
    }

    profile_event_t event = Profile_MakeEvent( profile_event_type_t::ZoneEnd );
    event.token = token;
    return Profile_Emit( event );
}

bool_t Cy_ProfileCounterAdd( const char *pszName, i64 value ) noexcept
{
    if ( !Cy_ProfileIsEnabled() || pszName == nullptr || pszName[0] == '\0' ) {
        return CY_FALSE;
    }

    stat_id_t id = CY_STAT_ID_INVALID;
    if ( !Profile_GetOrRegisterCounter( pszName, id ) ||
         !Cy_StatsAddI64( id, value ) ) {
        return CY_FALSE;
    }

    // CounterAdd events carry the post-add absolute value. Consumers can rebuild
    // graphs even if an earlier event was dropped by their own transport.
    stat_value_t counter{};
    if ( !Cy_StatsGet( id, &counter ) ) {
        return CY_FALSE;
    }

    profile_event_t event = Profile_MakeEvent( profile_event_type_t::CounterAdd );
    event.pszCounterName = pszName;
    event.nCounterValue = counter.i64Value;
    static_cast<void>( Profile_Emit( event ) );
    return CY_TRUE;
}

bool_t Cy_ProfileCounterSet( const char *pszName, i64 value ) noexcept
{
    if ( !Cy_ProfileIsEnabled() || pszName == nullptr || pszName[0] == '\0' ) {
        return CY_FALSE;
    }

    stat_id_t id = CY_STAT_ID_INVALID;
    if ( !Profile_GetOrRegisterCounter( pszName, id ) ||
         !Cy_StatsSetI64( id, value ) ) {
        return CY_FALSE;
    }

    profile_event_t event = Profile_MakeEvent( profile_event_type_t::CounterSet );
    event.pszCounterName = pszName;
    event.nCounterValue = value;
    static_cast<void>( Profile_Emit( event ) );
    return CY_TRUE;
}

u64 Cy_ProfileFrameBegin() noexcept
{
    if ( !Cy_ProfileIsEnabled() ) {
        return 0u;
    }

    const u64 nFrameIndex =
        g_profileFrameIndex.fetch_add( 1u, std::memory_order_relaxed ) + 1u;
    profile_event_t event = Profile_MakeEvent( profile_event_type_t::FrameBegin );
    event.nFrameIndex = nFrameIndex;
    static_cast<void>( Profile_Emit( event ) );
    return nFrameIndex;
}

bool_t Cy_ProfileFrameEnd() noexcept
{
    if ( !Cy_ProfileIsEnabled() ) {
        return CY_FALSE;
    }
    const profile_event_t event =
        Profile_MakeEvent( profile_event_type_t::FrameEnd );
    return Profile_Emit( event );
}

profile_state_t Cy_ProfileGetState() noexcept
{
    return {
        g_profileFrameIndex.load( std::memory_order_relaxed ),
        g_profileEmittedEventCount.load( std::memory_order_relaxed ),
        g_profileDroppedReentrantEventCount.load( std::memory_order_relaxed ),
        g_profileEnabled.load( std::memory_order_acquire ),
        g_profileSink.load( std::memory_order_acquire ) != nullptr
    };
}

void Cy_ProfileResetState() noexcept
{
    // Resetting while producers run can reuse tokens and move frame indices
    // backwards. The public contract therefore requires a quiescent profiler.
    g_profileNextToken.store( 1u, std::memory_order_relaxed );
    g_profileFrameIndex.store( 0u, std::memory_order_relaxed );
    g_profileEmittedEventCount.store( 0u, std::memory_order_relaxed );
    g_profileDroppedReentrantEventCount.store( 0u, std::memory_order_relaxed );
}

} // namespace cypher::common
