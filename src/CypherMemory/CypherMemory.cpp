//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherMemory/CypherMemory.cpp
//  Purpose: Implements the CypherMemory Memory module.
//  Details: This file participates in the engine allocation layer for arenas, pools,
//           buckets, scratch memory, and diagnostics. Keep ownership and lifetime
//           rules explicit because allocator bugs corrupt everything above them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Memory Implementation Notes

The memory front end owns seven global lifetime arenas. Initialization acquires their backing
transactionally; accessors expose each arena for explicit allocation, and shutdown releases it.
Statistics aggregate current arena snapshots without intercepting individual allocations.
================
*/

#include "CypherMemory.h"
#include "CypherMemory_Log.h"
#include "CypherLog.h"

#include <cstdio>      // Bounded policy-list formatting.
#include <cstring>     // strlen for bounded policy-list appends.

namespace {

cypher::engine::memory::memory_state_t s_Memory{};

#define MEM_LOG_FIELD( LABEL, FORMAT, ... )                                                        \
    LOG_INFO( cypher::engine::log::channel_t::MEMORY, "  %-24s : " FORMAT, ( LABEL ) __VA_OPT__( , ) __VA_ARGS__ )

#define MEM_LOG_DETAIL( LABEL, FORMAT, ... )                                                       \
    LOG_INFO( cypher::engine::log::channel_t::MEMORY, "    %-22s : " FORMAT, ( LABEL ) __VA_OPT__( , ) __VA_ARGS__ )

cypher::engine::memory::arena_desc_t Mem_MakeArenaDesc( const cypher::engine::memory::memory_arena_config_t &arenaConfig )
{
    cypher::engine::memory::arena_desc_t desc{};

    desc.name = arenaConfig.name;
    desc.capacity = arenaConfig.nReserveSize;
    desc.initialCommit = arenaConfig.initialCommit;
    desc.flags = arenaConfig.flags;
    desc.backing = arenaConfig.backing;

    return desc;
}

cypher::engine::memory::mem_error_t Mem_InitArena(
    cypher::engine::memory::arena_t &arena,
    const cypher::engine::memory::memory_arena_config_t &arenaConfig )
{
    return cypher::engine::memory::Mem_ArenaInit(
        arena,
        Mem_MakeArenaDesc( arenaConfig ) );
}

void Mem_ShutdownInitializedArenas()
{
    // Release in reverse initialization order so later arenas cannot outlive dependencies.
    cypher::engine::memory::Mem_ArenaShutdown( s_Memory.editorArena );
    cypher::engine::memory::Mem_ArenaShutdown( s_Memory.renderArena );
    cypher::engine::memory::Mem_ArenaShutdown( s_Memory.worldArena );
    cypher::engine::memory::Mem_ArenaShutdown( s_Memory.resourceArena );
    cypher::engine::memory::Mem_ArenaShutdown( s_Memory.scratchArena );
    cypher::engine::memory::Mem_ArenaShutdown( s_Memory.frameArena );
    cypher::engine::memory::Mem_ArenaShutdown( s_Memory.permanentArena );
}

void Mem_AddArenaStats(
    const cypher::engine::memory::arena_t &arena,
    cypher::engine::memory::memory_stats_t &stats,
    const cypher::engine::memory::memory_tag_t tag )
{
    const cypher::engine::common::usize nTagIndex = static_cast<cypher::engine::common::usize>( tag );

    // An attribution tag may own multiple arenas, so tag statistics accumulate.
    stats.nTotalCapacity += arena.capacity;
    stats.totalCommitted += arena.committed;
    stats.nTotalUsed += arena.used;

    if ( arena.nPeakUsed > stats.nPeakUsed ) {
        stats.nPeakUsed = arena.nPeakUsed;
    }

    if ( nTagIndex < static_cast<cypher::engine::common::usize>( cypher::engine::memory::memory_tag_t::COUNT ) ) {
        stats.tagStats[nTagIndex].name = cypher::engine::memory::Mem_TagName( tag );
        stats.tagStats[nTagIndex].used += arena.used;
        stats.tagStats[nTagIndex].nPeakUsed += arena.nPeakUsed;
        stats.tagStats[nTagIndex].nAllocationCount += arena.nAllocationCount;
        stats.tagStats[nTagIndex].nFailedAllocationCount += arena.nFailedAllocationCount;
    }
}

template <cypher::engine::common::usize Capacity>
void Mem_AppendPolicy( char ( &text )[Capacity], const char *policy )
{
    if ( policy == nullptr || policy[0] == '\0' ) {
        return;
    }

    const cypher::engine::common::usize length = std::strlen( text );
    if ( length >= Capacity - 1u ) {
        return;
    }

    const int written = std::snprintf(
        text + length,
        Capacity - length,
        "%s%s",
        length == 0u ? "" : " | ",
        policy );
    if ( written < 0 ) {
        text[length] = '\0';
    }
}

void Mem_FormatArenaPolicies( const cypher::engine::common::u32 flags, char ( &textOut )[160] )
{
    using namespace cypher::engine::memory;

    textOut[0] = '\0';
    if ( ( flags & CYPHER_MEMORY_ARENA_FLAG_ZERO_ON_ALLOC ) != 0u ) {
        Mem_AppendPolicy( textOut, "zero on allocation" );
    }
    if ( ( flags & CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_RESET ) != 0u ) {
        Mem_AppendPolicy( textOut, "clear on reset" );
    }
    if ( ( flags & CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_SHUTDOWN ) != 0u ) {
        Mem_AppendPolicy( textOut, "clear on shutdown" );
    }
    if ( ( flags & CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC ) != 0u ) {
        Mem_AppendPolicy( textOut, "grow commit on allocation" );
    }
    if ( ( flags & CYPHER_MEMORY_ARENA_FLAG_DECOMMIT_ON_RESET ) != 0u ) {
        Mem_AppendPolicy( textOut, "decommit on reset" );
    }
    if ( textOut[0] == '\0' ) {
        std::snprintf( textOut, sizeof( textOut ), "none" );
    }
}

void Mem_LogArenaStartupRecord(
    const cypher::engine::common::u32 index,
    const char *lifetime,
    const cypher::engine::memory::memory_arena_config_t &config,
    const cypher::engine::memory::arena_t &arena )
{
    using namespace cypher::engine;

    char capacityText[32]{};
    char initialCommitText[32]{};
    char committedText[32]{};
    char pageSizeText[32]{};
    char policies[160]{};
    memory::detail::Mem_LogFormatByteCount( arena.capacity, capacityText );
    memory::detail::Mem_LogFormatByteCount( arena.initialCommit, initialCommitText );
    memory::detail::Mem_LogFormatByteCount( arena.committed, committedText );
    memory::detail::Mem_LogFormatByteCount( arena.nPageSize, pageSizeText );
    Mem_FormatArenaPolicies( arena.flags, policies );

    if ( arena.backing == memory::arena_backing_t::ARENA_VIRTUAL_MEMORY ) {
        LOG_INFO(
            log::channel_t::MEMORY,
            "  [%u] %-20s : %s lifetime  |  tag %s  |  %s  |  owns backing %s",
            index,
            arena.name ? arena.name : "<unnamed>",
            lifetime ? lifetime : "unknown",
            memory::Mem_TagName( config.tag ),
            memory::detail::Mem_LogArenaBackingName( arena.backing ),
            arena.pOwnsMemory ? "yes" : "no" );
        LOG_INFO(
            log::channel_t::MEMORY,
            "       %-20s : %s (%zu bytes)",
            "Reserved",
            capacityText,
            arena.capacity );
        LOG_INFO(
            log::channel_t::MEMORY,
            "       %-20s : initial %s (%zu bytes)  |  current %s (%zu bytes)",
            "Committed",
            initialCommitText,
            arena.initialCommit,
            committedText,
            arena.committed );
        LOG_INFO(
            log::channel_t::MEMORY,
            "       %-20s : page %s  |  flags 0x%08x",
            "Mapping",
            pageSizeText,
            arena.flags );
        LOG_INFO(
            log::channel_t::MEMORY,
            "       %-20s : %s",
            "Policies",
            policies );
        return;
    }

    const char *capacityKind = arena.backing == memory::arena_backing_t::ARENA_HEAP
        ? "allocated"
        : "capacity";
    LOG_INFO(
        log::channel_t::MEMORY,
        "  [%u] %-20s : %s lifetime  |  tag %s  |  %s  |  owns backing %s",
        index,
        arena.name ? arena.name : "<unnamed>",
        lifetime ? lifetime : "unknown",
        memory::Mem_TagName( config.tag ),
        memory::detail::Mem_LogArenaBackingName( arena.backing ),
        arena.pOwnsMemory ? "yes" : "no" );
    LOG_INFO(
        log::channel_t::MEMORY,
        "       %-20s : %s %s (%zu bytes)",
        "Capacity",
        capacityKind,
        capacityText,
        arena.capacity );
    LOG_INFO(
        log::channel_t::MEMORY,
        "       %-20s : flags 0x%08x",
        "Mapping",
        arena.flags );
    LOG_INFO(
        log::channel_t::MEMORY,
        "       %-20s : %s",
        "Policies",
        policies );
}

void Mem_LogStartupReport()
{
    using namespace cypher::engine;

    struct arena_report_t {
        const char *lifetime;
        const memory::memory_arena_config_t *config;
        const memory::arena_t *arena;
    };

    const arena_report_t arenas[] = {
        { "process", &s_Memory.config.permanentArena, &s_Memory.permanentArena },
        { "frame", &s_Memory.config.frameArena, &s_Memory.frameArena },
        { "scoped", &s_Memory.config.scratchArena, &s_Memory.scratchArena },
        { "resource", &s_Memory.config.resourceArena, &s_Memory.resourceArena },
        { "world", &s_Memory.config.worldArena, &s_Memory.worldArena },
        { "renderer", &s_Memory.config.renderArena, &s_Memory.renderArena },
        { "editor", &s_Memory.config.editorArena, &s_Memory.editorArena }
    };

    char pageSizeText[32]{};
    memory::detail::Mem_LogFormatByteCount( s_Memory.permanentArena.nPageSize, pageSizeText );
    MEM_LOG_FIELD(
        "State",
        "READY  |  segmented arenas  |  %zu arenas",
        sizeof( arenas ) / sizeof( arenas[0] ) );
    MEM_LOG_DETAIL(
        "Allocator defaults",
        "%zu-byte alignment  |  %s virtual page  |  %zu trace entries",
        memory::CYPHER_MEMORY_DEFAULT_ALIGNMENT,
        pageSizeText,
        memory::CYPHER_MEMORY_ARENA_ALLOCATION_TRACE_COUNT );
    LOG_INFO(
        log::channel_t::MEMORY,
        "  Global arena layout" );

    for ( common::u32 index = 0u; index < static_cast<common::u32>( sizeof( arenas ) / sizeof( arenas[0] ) ); ++index ) {
        Mem_LogArenaStartupRecord(
            index,
            arenas[index].lifetime,
            *arenas[index].config,
            *arenas[index].arena );
    }

    const memory::memory_stats_t stats = memory::Mem_Stats();
    char capacityText[32]{};
    char committedText[32]{};
    char usedText[32]{};
    memory::detail::Mem_LogFormatByteCount( stats.nTotalCapacity, capacityText );
    memory::detail::Mem_LogFormatByteCount( stats.totalCommitted, committedText );
    memory::detail::Mem_LogFormatByteCount( stats.nTotalUsed, usedText );
    MEM_LOG_FIELD(
        "Memory budget",
        "%s reserved  |  %s committed  |  %s used",
        capacityText,
        committedText,
        usedText );
    MEM_LOG_DETAIL(
        "Exact bytes",
        "reserved %zu  |  committed %zu  |  used %zu",
        stats.nTotalCapacity,
        stats.totalCommitted,
        stats.nTotalUsed );
}

}       // namespace

namespace cypher::engine::memory
{

memory_config_t Mem_DefaultConfig()
{
    memory_config_t config{};

    // Virtual reservations establish lifetime budgets without committing every byte up front.
    config.permanentArena = memory_arena_config_t{
        "PermanentArena",
        Mem_Megabytes( 256u ),
        Mem_Megabytes( 16u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::CORE
    };

    config.frameArena = memory_arena_config_t{
        "FrameArena",
        Mem_Megabytes( 64u ),
        Mem_Megabytes( 8u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC | CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_RESET | CYPHER_MEMORY_ARENA_FLAG_DECOMMIT_ON_RESET,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::TEMP
    };

    config.scratchArena = memory_arena_config_t{
        "ScratchArena",
        Mem_Megabytes( 128u ),
        Mem_Megabytes( 8u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC | CYPHER_MEMORY_ARENA_FLAG_DECOMMIT_ON_RESET,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::TEMP
    };

    config.resourceArena = memory_arena_config_t{
        "ResourceArena",
        Mem_Gigabytes( 2u ),
        Mem_Megabytes( 64u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::RESOURCE
    };

    config.worldArena = memory_arena_config_t{
        "WorldArena",
        Mem_Gigabytes( 1u ),
        Mem_Megabytes( 64u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::WORLD
    };

    config.renderArena = memory_arena_config_t{
        "RenderArena",
        Mem_Megabytes( 512u ),
        Mem_Megabytes( 32u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::RENDER
    };

    config.editorArena = memory_arena_config_t{
        "EditorArena",
        Mem_Megabytes( 512u ),
        Mem_Megabytes( 32u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::EDITOR
    };

    return config;
}

mem_error_t Mem_Init( const memory_config_t &config )
{
    if ( s_Memory.initialized ) {
        LOG_WARNING( log::channel_t::MEMORY, "memory system is already initialized." );
        return mem_error_t::ERR_ALREADY_INITIALIZED;
    }

    // Arena startup is transactional; every failure unwinds the initialized prefix.
    s_Memory = {};
    s_Memory.config = config;

    mem_error_t result = Mem_InitArena( s_Memory.permanentArena, config.permanentArena );
    if ( result != mem_error_t::OK ) {
        Mem_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    result = Mem_InitArena( s_Memory.frameArena, config.frameArena );
    if ( result != mem_error_t::OK ) {
        Mem_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    result = Mem_InitArena( s_Memory.scratchArena, config.scratchArena );
    if ( result != mem_error_t::OK ) {
        Mem_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    result = Mem_InitArena( s_Memory.resourceArena, config.resourceArena );
    if ( result != mem_error_t::OK ) {
        Mem_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    result = Mem_InitArena( s_Memory.worldArena, config.worldArena );
    if ( result != mem_error_t::OK ) {
        Mem_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    result = Mem_InitArena( s_Memory.renderArena, config.renderArena );
    if ( result != mem_error_t::OK ) {
        Mem_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    result = Mem_InitArena( s_Memory.editorArena, config.editorArena );
    if ( result != mem_error_t::OK ) {
        Mem_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    s_Memory.initialized = true;

    Mem_LogStartupReport();

    return mem_error_t::OK;
}

void Mem_Shutdown()
{
    if ( !s_Memory.initialized ) {
        return;
    }

    const memory_stats_t stats = Mem_Stats();
    char usedText[32]{};
    char peakText[32]{};
    detail::Mem_LogFormatByteCount( stats.nTotalUsed, usedText );
    detail::Mem_LogFormatByteCount( stats.nPeakUsed, peakText );
    LOG_INFO( log::channel_t::MEMORY,
              "memory shutdown: used=%s, peak=%s.",
              usedText,
              peakText );

    Mem_ShutdownInitializedArenas();
    s_Memory = {};
}

void Mem_BeginFrame()
{
    if ( !s_Memory.initialized ) {
        return;
    }

    // Frame allocations are invalid from this point; callers must not retain their pointers.
    Mem_ArenaReset( s_Memory.frameArena );
}

void Mem_EndFrame()
{
    if ( !s_Memory.initialized ) {
        return;
    }

    // Preserve live usage while starting a fresh diagnostics window for the next frame.
    Mem_ArenaResetCounters( s_Memory.frameArena );
}

bool Mem_IsInitialized()
{
    return s_Memory.initialized;
}

memory_stats_t Mem_Stats()
{
    // Build a coherent snapshot from each lifetime arena before updating aggregate peaks.
    memory_stats_t stats{};

    stats.permanentStats = Mem_ArenaStats( s_Memory.permanentArena );
    stats.frameStats = Mem_ArenaStats( s_Memory.frameArena );
    stats.scratchStats = Mem_ArenaStats( s_Memory.scratchArena );
    stats.resourceStats = Mem_ArenaStats( s_Memory.resourceArena );
    stats.worldStats = Mem_ArenaStats( s_Memory.worldArena );
    stats.renderStats = Mem_ArenaStats( s_Memory.renderArena );
    stats.editorStats = Mem_ArenaStats( s_Memory.editorArena );

    Mem_AddArenaStats( s_Memory.permanentArena, stats, s_Memory.config.permanentArena.tag );
    Mem_AddArenaStats( s_Memory.frameArena, stats, s_Memory.config.frameArena.tag );
    Mem_AddArenaStats( s_Memory.scratchArena, stats, s_Memory.config.scratchArena.tag );
    Mem_AddArenaStats( s_Memory.resourceArena, stats, s_Memory.config.resourceArena.tag );
    Mem_AddArenaStats( s_Memory.worldArena, stats, s_Memory.config.worldArena.tag );
    Mem_AddArenaStats( s_Memory.renderArena, stats, s_Memory.config.renderArena.tag );
    Mem_AddArenaStats( s_Memory.editorArena, stats, s_Memory.config.editorArena.tag );

    s_Memory.nTotalCapacity = stats.nTotalCapacity;
    s_Memory.totalCommitted = stats.totalCommitted;
    s_Memory.nTotalUsed = stats.nTotalUsed;

    if ( stats.nTotalUsed > s_Memory.nPeakUsed ) {
        s_Memory.nPeakUsed = stats.nTotalUsed;
    }

    stats.nPeakUsed = s_Memory.nPeakUsed;

    return stats;
}

const char *Mem_TagName( const memory_tag_t tag )
{
    switch ( tag ) {
        case memory_tag_t::UNKNOWN:    return "UNKNOWN";
        case memory_tag_t::CORE:       return "CORE";
        case memory_tag_t::SYSTEM:     return "SYSTEM";
        case memory_tag_t::MEMORY:     return "MEMORY";
        case memory_tag_t::FILESYSTEM: return "FILESYSTEM";
        case memory_tag_t::RESOURCE:   return "RESOURCE";
        case memory_tag_t::WORLD:      return "WORLD";
        case memory_tag_t::RENDER:     return "RENDER";
        case memory_tag_t::AUDIO:      return "AUDIO";
        case memory_tag_t::PHYSICS:    return "PHYSICS";
        case memory_tag_t::AI:         return "AI";
        case memory_tag_t::SCRIPT:     return "SCRIPT";
        case memory_tag_t::NETWORK:    return "NETWORK";
        case memory_tag_t::EDITOR:     return "EDITOR";
        case memory_tag_t::TOOLS:      return "TOOLS";
        case memory_tag_t::TEMP:       return "TEMP";
        case memory_tag_t::COUNT:      return "COUNT";
        default:                       return "UNKNOWN";
    }
}

arena_t &Mem_PermanentArena()
{
    return s_Memory.permanentArena;
}

arena_t &Mem_FrameArena()
{
    return s_Memory.frameArena;
}

arena_t &Mem_ScratchArena()
{
    return s_Memory.scratchArena;
}

arena_t &Mem_ResourceArena()
{
    return s_Memory.resourceArena;
}

arena_t &Mem_WorldArena()
{
    return s_Memory.worldArena;
}

arena_t &Mem_RenderArena()
{
    return s_Memory.renderArena;
}

arena_t &Mem_EditorArena()
{
    return s_Memory.editorArena;
}

}       // namespace cypher::engine::memory
