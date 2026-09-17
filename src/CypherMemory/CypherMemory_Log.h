//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherMemory/CypherMemory_Log.h
//  Purpose: Provides private formatting helpers for memory diagnostics.
//  Details: Memory logs use named backing modes and bounded IEC byte counts so
//           startup diagnostics remain readable without depending on Common Tier1.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_MEMORY_LOG_H
#define CYPHER_ENGINE_MEMORY_LOG_H
#pragma once

#include "CypherMemory_Arena.h"

#include <cstdio>
#include <limits>

namespace cypher::engine::memory::detail
{

inline const char *Mem_LogArenaBackingName( const arena_backing_t backing )
{
    switch ( backing ) {
    case arena_backing_t::ARENA_HEAP:
        return "heap";
    case arena_backing_t::ARENA_EXTERNAL_BUFFER:
        return "external buffer";
    case arena_backing_t::ARENA_VIRTUAL_MEMORY:
        return "virtual memory";
    default:
        return "unknown";
    }
}

template <common::usize Capacity>
void Mem_LogFormatByteCount( const common::usize bytes, char ( &textOut )[Capacity] )
{
    static_assert( Capacity > 0u );

    constexpr const char *units[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
    constexpr common::usize cUnits = sizeof( units ) / sizeof( units[0] );

    common::usize unitIndex = 0u;
    common::usize divisor = 1u;
    while ( unitIndex + 1u < cUnits && bytes / divisor >= 1024u ) {
        if ( divisor > std::numeric_limits<common::usize>::max() / 1024u ) {
            break;
        }
        divisor *= 1024u;
        ++unitIndex;
    }

    int written = 0;
    if ( unitIndex == 0u ) {
        written = std::snprintf( textOut, Capacity, "%zu B", bytes );
    } else {
        const common::usize whole = bytes / divisor;
        const common::usize remainder = bytes % divisor;
        const int precision = remainder == 0u ? 0 : ( whole >= 10u ? 1 : 2 );
        const double value = static_cast<double>( bytes ) / static_cast<double>( divisor );
        written = std::snprintf( textOut, Capacity, "%.*f %s", precision, value, units[unitIndex] );
    }

    if ( written < 0 || static_cast<common::usize>( written ) >= Capacity ) {
        textOut[0] = '\0';
    }
}

} // namespace cypher::engine::memory::detail

#endif // CYPHER_ENGINE_MEMORY_LOG_H
