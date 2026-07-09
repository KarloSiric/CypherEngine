//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Profile.cpp
//  Purpose: Implements CypherCommon Tier0 profiling counters.
//  Details: This is the low-level profiling spine. It records active zone tokens,
//           frame index, and named counters without owning a UI/exporter yet.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Profile.h"

#include "CypherCommon_Atomic.h"
#include "CypherCommon_Stats.h"
#include "CypherCommon_Timer.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace cypher::common
{
namespace
{

struct profile_zone_record_t {
    profile_zone_desc_t desc{};
    timer_tick_t nStartTicks = 0u;
};

std::mutex g_profileMutex;
std::unordered_map<profile_token_t, profile_zone_record_t> g_profileZones;
std::unordered_map<std::string, i64> g_profileCounters;
atomic_u64_t g_profileNextToken{ 1u };
atomic_u64_t g_profileFrameIndex{ 0u };

void Profile_PublishCounter( const char *pName, i64 value )
{
    stat_desc_t desc{};
    desc.pName = pName;
    desc.pCategory = "Profile";
    desc.pDescription = "Profile counter";
    desc.type = stat_value_type_t::I64;
    Cy_StatsRegister( desc );
    Cy_StatsSetI64( pName, value );
}

} // namespace

profile_token_t Cy_ProfileBeginZone( const profile_zone_desc_t &desc )
{
    const profile_token_t token = Cy_AtomicFetchAdd( &g_profileNextToken, static_cast<u64>( 1u ), CY_MEMORY_ORDER_RELAXED );
    if ( token == 0u ) {
        return 0u;
    }

    profile_zone_record_t record{};
    record.desc = desc;
    record.nStartTicks = Timer_NowTicks();

    std::lock_guard<std::mutex> lock( g_profileMutex );
    g_profileZones[token] = record;
    return token;
}

void Cy_ProfileEndZone( profile_token_t token )
{
    if ( token == 0u ) {
        return;
    }

    std::lock_guard<std::mutex> lock( g_profileMutex );
    g_profileZones.erase( token );
}

void Cy_ProfileCounterAdd( const char *pName, i64 value )
{
    if ( pName == nullptr || pName[0] == '\0' ) {
        return;
    }

    i64 nNewValue = 0;
    {
        std::lock_guard<std::mutex> lock( g_profileMutex );
        nNewValue = ( g_profileCounters[std::string( pName )] += value );
    }

    Profile_PublishCounter( pName, nNewValue );
}

void Cy_ProfileCounterSet( const char *pName, i64 value )
{
    if ( pName == nullptr || pName[0] == '\0' ) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock( g_profileMutex );
        g_profileCounters[std::string( pName )] = value;
    }

    Profile_PublishCounter( pName, value );
}

void Cy_ProfileFrameBegin()
{
    Cy_AtomicFetchAdd( &g_profileFrameIndex, static_cast<u64>( 1u ), CY_MEMORY_ORDER_RELAXED );
}

void Cy_ProfileFrameEnd()
{
}

} // namespace cypher::common
