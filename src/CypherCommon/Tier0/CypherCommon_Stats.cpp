//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Stats.cpp
//  Purpose: Implements CypherCommon Tier0 named runtime statistics.
//  Details: Stats are used by memory, VFS, pak loading, renderer diagnostics,
//           command-line tools, and future Mason panels.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Stats.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace cypher::common
{
namespace
{

struct stat_record_t {
    stat_desc_t desc{};
    stat_value_t value{};
};

std::mutex g_statsMutex;
std::unordered_map<std::string, stat_record_t> g_stats;

stat_value_t Stats_MakeValue( stat_value_type_t type )
{
    stat_value_t value{};
    value.type = type;
    value.u64Value = 0u;
    return value;
}

stat_record_t *Stats_FindLocked( const char *pName )
{
    if ( pName == nullptr || pName[0] == '\0' ) {
        return nullptr;
    }

    auto it = g_stats.find( pName );
    if ( it != g_stats.end() ) {
        return &it->second;
    }

    return nullptr;
}

} // namespace

void Cy_StatsRegister( const stat_desc_t &desc )
{
    if ( desc.pName == nullptr || desc.pName[0] == '\0' ) {
        return;
    }

    std::lock_guard<std::mutex> lock( g_statsMutex );
    stat_record_t *pExisting = Stats_FindLocked( desc.pName );
    if ( pExisting != nullptr ) {
        pExisting->desc = desc;
        pExisting->value.type = desc.type;
        return;
    }

    stat_record_t record{};
    record.desc = desc;
    record.value = Stats_MakeValue( desc.type );
    g_stats.emplace( std::string( desc.pName ), record );
}

void Cy_StatsSetI64( const char *pName, i64 value )
{
    std::lock_guard<std::mutex> lock( g_statsMutex );
    stat_record_t *pRecord = Stats_FindLocked( pName );
    if ( pRecord == nullptr ) {
        return;
    }

    pRecord->value.type = stat_value_type_t::I64;
    pRecord->value.i64Value = value;
}

void Cy_StatsSetU64( const char *pName, u64 value )
{
    std::lock_guard<std::mutex> lock( g_statsMutex );
    stat_record_t *pRecord = Stats_FindLocked( pName );
    if ( pRecord == nullptr ) {
        return;
    }

    pRecord->value.type = stat_value_type_t::U64;
    pRecord->value.u64Value = value;
}

void Cy_StatsSetF64( const char *pName, f64 value )
{
    std::lock_guard<std::mutex> lock( g_statsMutex );
    stat_record_t *pRecord = Stats_FindLocked( pName );
    if ( pRecord == nullptr ) {
        return;
    }

    pRecord->value.type = stat_value_type_t::F64;
    pRecord->value.f64Value = value;
}

bool_t Cy_StatsGet( const char *pName, stat_value_t *pOutValue )
{
    if ( pOutValue == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( g_statsMutex );
    stat_record_t *pRecord = Stats_FindLocked( pName );
    if ( pRecord == nullptr ) {
        *pOutValue = {};
        return CY_FALSE;
    }

    *pOutValue = pRecord->value;
    return CY_TRUE;
}

void Cy_StatsReset()
{
    std::lock_guard<std::mutex> lock( g_statsMutex );
    for ( auto &pair : g_stats ) {
        pair.second.value = Stats_MakeValue( pair.second.desc.type );
    }
}

} // namespace cypher::common
