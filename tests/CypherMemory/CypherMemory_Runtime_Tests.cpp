//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherMemory/CypherMemory_Runtime_Tests.cpp
//  Purpose: Tests global memory frontend ownership, rollback, and accounting.
//  Details: Small lifetime arenas verify routing, frame boundaries, and tag
//           snapshots without reserving the production default memory budgets.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMemory.h"
#include "CypherSystem_Public.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstring>

using namespace cypher::engine::memory;
using cypher::engine::common::usize;

namespace
{

struct memory_scope_t {
    memory_scope_t() { Mem_Shutdown(); }
    ~memory_scope_t() { Mem_Shutdown(); }
};

std::array<arena_t *, 7> RuntimeArenas()
{
    return { &Mem_PermanentArena(), &Mem_FrameArena(), &Mem_ScratchArena(),
             &Mem_ResourceArena(), &Mem_WorldArena(), &Mem_RenderArena(), &Mem_EditorArena() };
}

std::array<memory_arena_config_t *, 7> ArenaConfigs( memory_config_t &config )
{
    return { &config.permanentArena, &config.frameArena, &config.scratchArena,
             &config.resourceArena, &config.worldArena, &config.renderArena, &config.editorArena };
}

std::array<arena_stats_t, 7> ArenaSnapshots( const memory_stats_t &stats )
{
    return { stats.permanentStats, stats.frameStats, stats.scratchStats,
             stats.resourceStats, stats.worldStats, stats.renderStats, stats.editorStats };
}

memory_config_t SmallHeapConfig()
{
    memory_config_t config = Mem_DefaultConfig();
    const auto arenas = ArenaConfigs( config );
    for ( usize i = 0u; i < arenas.size(); ++i ) {
        arenas[i]->nReserveSize = 512u * ( i + 1u );
        arenas[i]->initialCommit = 0u;
        arenas[i]->backing = arena_backing_t::ARENA_HEAP;
        arenas[i]->flags = CYPHER_MEMORY_ARENA_FLAG_NONE;
    }
    config.frameArena.flags = CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_RESET;
    return config;
}

bool BytesEqual( const void *data, usize size, unsigned char value )
{
    const auto *bytes = static_cast<const unsigned char *>( data );
    return std::all_of( bytes, bytes + size,
                        [value]( unsigned char byte ) { return byte == value; } );
}

void RequireStoppedMemory()
{
    REQUIRE_FALSE( Mem_IsInitialized() );
    for ( const arena_t *arena : RuntimeArenas() ) {
        REQUIRE_FALSE( Mem_ArenaIsInitialized( *arena ) );
        REQUIRE( arena->base == nullptr );
        REQUIRE( Mem_ArenaCapacity( *arena ) == 0u );
    }
    const memory_stats_t stats = Mem_Stats();
    REQUIRE( stats.nTotalCapacity == 0u );
    REQUIRE( stats.totalCommitted == 0u );
    REQUIRE( stats.nTotalUsed == 0u );
    REQUIRE( stats.nPeakUsed == 0u );
    for ( const memory_tag_stats_t &tag : stats.tagStats ) {
        REQUIRE( tag.used == 0u );
        REQUIRE( tag.nPeakUsed == 0u );
        REQUIRE( tag.nAllocationCount == 0u );
        REQUIRE( tag.nFailedAllocationCount == 0u );
    }
}

} // namespace

TEST_CASE( "Global memory owns distinct lifetime arenas and preserves a live initialization",
           "[CypherMemory][Runtime]" )
{
    memory_scope_t scope;
    Mem_BeginFrame();
    Mem_EndFrame();
    Mem_Shutdown();
    RequireStoppedMemory();

    memory_config_t config = SmallHeapConfig();
    REQUIRE( Mem_Init( config ) == mem_error_t::OK );
    REQUIRE( Mem_IsInitialized() );
    const auto arenas = RuntimeArenas();
    const auto configs = ArenaConfigs( config );
    std::array<void *, 7> live{};
    usize totalCapacity = 0u;
    usize totalUsed = 0u;
    for ( usize i = 0u; i < arenas.size(); ++i ) {
        CAPTURE( i );
        REQUIRE( Mem_ArenaIsInitialized( *arenas[i] ) );
        REQUIRE( arenas[i]->backing == arena_backing_t::ARENA_HEAP );
        REQUIRE( arenas[i]->flags == configs[i]->flags );
        REQUIRE( arenas[i]->capacity == configs[i]->nReserveSize );
        REQUIRE( arenas[i]->committed == configs[i]->nReserveSize );
        REQUIRE( std::strcmp( arenas[i]->name, configs[i]->name ) == 0 );
        for ( usize previous = 0u; previous < i; ++previous ) {
            REQUIRE( arenas[i] != arenas[previous] );
            REQUIRE( arenas[i]->base != arenas[previous]->base );
        }
        const usize size = 7u * ( i + 1u );
        live[i] = Mem_ArenaAlloc( *arenas[i], size, 1u );
        REQUIRE( live[i] != nullptr );
        std::memset( live[i], static_cast<int>( i + 1u ), size );
        totalCapacity += configs[i]->nReserveSize;
        totalUsed += size;
    }

    const memory_stats_t stats = Mem_Stats();
    REQUIRE( stats.nTotalCapacity == totalCapacity );
    REQUIRE( stats.totalCommitted == totalCapacity );
    REQUIRE( stats.nTotalUsed == totalUsed );
    REQUIRE( stats.nPeakUsed == totalUsed );
    const auto snapshots = ArenaSnapshots( stats );
    for ( usize i = 0u; i < arenas.size(); ++i ) {
        CAPTURE( i );
        REQUIRE( snapshots[i].capacity == configs[i]->nReserveSize );
        REQUIRE( snapshots[i].used == 7u * ( i + 1u ) );
        REQUIRE( snapshots[i].nAllocationCount == 1u );
        REQUIRE( std::strcmp( snapshots[i].name, configs[i]->name ) == 0 );
    }

    memory_config_t replacement = config;
    replacement.permanentArena.nReserveSize = 0u;
    REQUIRE( Mem_Init( replacement ) == mem_error_t::ERR_ALREADY_INITIALIZED );
    REQUIRE( Mem_IsInitialized() );
    REQUIRE( Mem_Stats().nTotalUsed == totalUsed );
    for ( usize i = 0u; i < arenas.size(); ++i ) {
        REQUIRE( arenas[i]->base == live[i] );
        REQUIRE( BytesEqual( live[i], 7u * ( i + 1u ), static_cast<unsigned char>( i + 1u ) ) );
    }

    Mem_Shutdown();
    RequireStoppedMemory();
    REQUIRE( Mem_Init( config ) == mem_error_t::OK );
    REQUIRE( Mem_Stats().nTotalUsed == 0u );
    REQUIRE( Mem_Stats().nPeakUsed == 0u );
}

TEST_CASE( "Global memory rolls back every initialized prefix and permits retry",
           "[CypherMemory][Runtime]" )
{
    memory_scope_t scope;
    for ( usize failingArena = 0u; failingArena < RuntimeArenas().size(); ++failingArena ) {
        CAPTURE( failingArena );
        memory_config_t config = SmallHeapConfig();
        const auto configs = ArenaConfigs( config );
        const usize capacity = configs[failingArena]->nReserveSize;
        configs[failingArena]->nReserveSize = 0u;
        REQUIRE( Mem_Init( config ) == mem_error_t::ERR_INVALID_CAPACITY );
        RequireStoppedMemory();

        configs[failingArena]->nReserveSize = capacity;
        REQUIRE( Mem_Init( config ) == mem_error_t::OK );
        for ( arena_t *arena : RuntimeArenas() ) {
            REQUIRE( Mem_ArenaAlloc( *arena, 8u, 1u ) != nullptr );
        }
        Mem_Shutdown();
        RequireStoppedMemory();
    }
}

TEST_CASE( "Global memory forwards backing policy and rejects unsupported external backing",
           "[CypherMemory][Runtime]" )
{
    memory_scope_t scope;
    memory_config_t config = SmallHeapConfig();
    // The frontend has no external-buffer field, so it must unwind this descriptor failure.
    config.editorArena.backing = arena_backing_t::ARENA_EXTERNAL_BUFFER;
    REQUIRE( Mem_Init( config ) == mem_error_t::ERR_EXTERNAL_BUFFER_REQUIRED );
    RequireStoppedMemory();

    const usize page = cypher::engine::sys::Sys_VirtualPageSize();
    REQUIRE( page > 0u );
    config.editorArena.backing = arena_backing_t::ARENA_HEAP;
    config.frameArena.backing = arena_backing_t::ARENA_VIRTUAL_MEMORY;
    config.frameArena.nReserveSize = 4u * page;
    config.frameArena.initialCommit = page;
    config.frameArena.flags = CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC |
                              CYPHER_MEMORY_ARENA_FLAG_DECOMMIT_ON_RESET;
    REQUIRE( Mem_Init( config ) == mem_error_t::OK );
    const memory_stats_t before = Mem_Stats();
    REQUIRE( before.frameStats.capacity == 4u * page );
    REQUIRE( before.frameStats.committed == page );
    REQUIRE( before.frameStats.initialCommit == page );
    void *frame = Mem_ArenaAlloc( Mem_FrameArena(), page + 1u, 1u );
    REQUIRE( frame != nullptr );
    std::memset( frame, 0x46u, page + 1u );
    const memory_stats_t grown = Mem_Stats();
    REQUIRE( grown.frameStats.committed == 2u * page );
    REQUIRE( grown.totalCommitted == before.totalCommitted + page );
    REQUIRE( grown.nTotalCapacity == before.nTotalCapacity );
    Mem_BeginFrame();
    const memory_stats_t reset = Mem_Stats();
    REQUIRE( reset.frameStats.used == 0u );
    REQUIRE( reset.frameStats.committed == page );
    REQUIRE( reset.totalCommitted == before.totalCommitted );
}

TEST_CASE( "Global frame boundaries invalidate only frame storage and reset only frame diagnostics",
           "[CypherMemory][Runtime]" )
{
    memory_scope_t scope;
    REQUIRE( Mem_Init( SmallHeapConfig() ) == mem_error_t::OK );
    const auto arenas = RuntimeArenas();
    std::array<void *, 7> live{};
    for ( usize i = 0u; i < arenas.size(); ++i ) {
        live[i] = Mem_ArenaAlloc( *arenas[i], 32u, 1u );
        REQUIRE( live[i] != nullptr );
        std::memset( live[i], 0x51, 32u );
        REQUIRE( Mem_ArenaAlloc( *arenas[i], arenas[i]->capacity, 1u ) == nullptr );
    }
    REQUIRE( Mem_Stats().nTotalUsed == 7u * 32u );

    Mem_EndFrame();
    for ( usize i = 0u; i < arenas.size(); ++i ) {
        CAPTURE( i );
        const arena_stats_t stats = Mem_ArenaStats( *arenas[i] );
        REQUIRE( stats.used == 32u );
        REQUIRE( stats.nPeakUsed == 32u );
        REQUIRE( stats.nAllocationCount == ( i == 1u ? 0u : 1u ) );
        REQUIRE( stats.nFailedAllocationCount == ( i == 1u ? 0u : 1u ) );
        REQUIRE( BytesEqual( live[i], 32u, 0x51u ) );
    }

    Mem_BeginFrame();
    REQUIRE( Mem_ArenaUsed( Mem_FrameArena() ) == 0u );
    REQUIRE( Mem_Stats().nTotalUsed == 6u * 32u );
    REQUIRE( Mem_Stats().nPeakUsed == 7u * 32u );
    for ( usize i = 0u; i < arenas.size(); ++i ) {
        if ( i == 1u ) {
            continue;
        }
        REQUIRE( Mem_ArenaUsed( *arenas[i] ) == 32u );
        REQUIRE( BytesEqual( live[i], 32u, 0x51u ) );
    }
    // Reacquire the released range before inspecting the clear-on-reset policy.
    void *reused = Mem_ArenaAlloc( Mem_FrameArena(), 32u, 1u );
    REQUIRE( reused == live[1] );
    REQUIRE( BytesEqual( reused, 32u, 0u ) );
}

TEST_CASE( "Global memory attributes arena usage and failures using its copied configuration",
           "[CypherMemory][Runtime]" )
{
    memory_scope_t scope;
    memory_config_t config = SmallHeapConfig();
    config.resourceArena.tag = memory_tag_t::FILESYSTEM;
    REQUIRE( Mem_Init( config ) == mem_error_t::OK );
    config.resourceArena.tag = memory_tag_t::AUDIO;

    REQUIRE( Mem_ArenaAlloc( Mem_FrameArena(), 20u, 1u ) != nullptr );
    REQUIRE( Mem_ArenaAlloc( Mem_ScratchArena(), 30u, 1u ) != nullptr );
    REQUIRE( Mem_ArenaAlloc( Mem_ScratchArena(), 10u, 1u ) != nullptr );
    REQUIRE( Mem_ArenaAlloc( Mem_ResourceArena(), 11u, 1u ) != nullptr );
    REQUIRE( Mem_ArenaAlloc( Mem_PermanentArena(), 7u, 1u ) != nullptr );
    REQUIRE( Mem_ArenaAlloc( Mem_FrameArena(), Mem_ArenaCapacity( Mem_FrameArena() ), 1u ) == nullptr );
    REQUIRE( Mem_ArenaAlloc( Mem_ScratchArena(), 1u, 3u ) == nullptr );
    const memory_stats_t stats = Mem_Stats();
    const memory_tag_stats_t &temp = stats.tagStats[static_cast<usize>( memory_tag_t::TEMP )];
    REQUIRE( std::strcmp( temp.name, "TEMP" ) == 0 );
    REQUIRE( temp.used == 60u );
    REQUIRE( temp.nAllocationCount == 3u );
    REQUIRE( temp.nFailedAllocationCount == 2u );
    const memory_tag_stats_t &files = stats.tagStats[static_cast<usize>( memory_tag_t::FILESYSTEM )];
    REQUIRE( std::strcmp( files.name, "FILESYSTEM" ) == 0 );
    REQUIRE( files.used == 11u );
    REQUIRE( files.nAllocationCount == 1u );
    REQUIRE( stats.tagStats[static_cast<usize>( memory_tag_t::AUDIO )].used == 0u );
    REQUIRE( stats.tagStats[static_cast<usize>( memory_tag_t::CORE )].used == 7u );

    usize attributedUsed = 0u;
    for ( const memory_tag_stats_t &tag : stats.tagStats ) {
        attributedUsed += tag.used;
    }
    REQUIRE( attributedUsed == stats.nTotalUsed );
    REQUIRE( attributedUsed == 78u );
    Mem_EndFrame();
    const memory_tag_stats_t tempAfterEnd = Mem_Stats().tagStats[static_cast<usize>( memory_tag_t::TEMP )];
    REQUIRE( tempAfterEnd.used == 60u );
    REQUIRE( tempAfterEnd.nAllocationCount == 2u );
    REQUIRE( tempAfterEnd.nFailedAllocationCount == 1u );
}

TEST_CASE( "Global memory distinguishes sampled aggregate peaks from summed arena peaks",
           "[CypherMemory][Runtime]" )
{
    memory_scope_t scope;
    REQUIRE( Mem_Init( SmallHeapConfig() ) == mem_error_t::OK );
    REQUIRE( Mem_ArenaAlloc( Mem_FrameArena(), 100u, 1u ) != nullptr );
    REQUIRE( Mem_Stats().nPeakUsed == 100u );
    Mem_BeginFrame();
    REQUIRE( Mem_ArenaAlloc( Mem_ScratchArena(), 200u, 1u ) != nullptr );
    const memory_stats_t stats = Mem_Stats();
    REQUIRE( stats.nTotalUsed == 200u );
    REQUIRE( stats.nPeakUsed == 200u );
    const memory_tag_stats_t &temp = stats.tagStats[static_cast<usize>( memory_tag_t::TEMP )];
    REQUIRE( temp.used == 200u );
    REQUIRE( temp.nPeakUsed == 300u );

    // An allocation released between snapshots contributes only to its arena high-water mark.
    Mem_ArenaReset( Mem_ScratchArena() );
    REQUIRE( Mem_ArenaAlloc( Mem_FrameArena(), 400u, 1u ) != nullptr );
    Mem_BeginFrame();
    const memory_stats_t afterUnsampledUsage = Mem_Stats();
    REQUIRE( afterUnsampledUsage.nTotalUsed == 0u );
    REQUIRE( afterUnsampledUsage.nPeakUsed == 200u );
    REQUIRE( afterUnsampledUsage.frameStats.nPeakUsed == 400u );
    REQUIRE( afterUnsampledUsage.tagStats[static_cast<usize>( memory_tag_t::TEMP )].nPeakUsed == 600u );
    Mem_EndFrame();
    const memory_stats_t afterCounterReset = Mem_Stats();
    REQUIRE( afterCounterReset.nPeakUsed == 200u );
    REQUIRE( afterCounterReset.tagStats[static_cast<usize>( memory_tag_t::TEMP )].nPeakUsed == 200u );
}
