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

#include "CypherMemory.h"
#include "CypherLog.h"

namespace {

cypher::engine::memory::memory_state_t s_Memory{};

cypher::engine::memory::arena_desc_t CypherMemory_MakeArenaDesc( const cypher::engine::memory::memory_arena_config_t &arenaConfig )
{
    cypher::engine::memory::arena_desc_t desc{};

    desc.name = arenaConfig.name;
    desc.capacity = arenaConfig.nReserveSize;
    desc.initialCommit = arenaConfig.initialCommit;
    desc.flags = arenaConfig.flags;
    desc.backing = arenaConfig.backing;

    return desc;
}

cypher::engine::memory::mem_error_t CypherMemory_InitArena(
    cypher::engine::memory::arena_t &arena,
    const cypher::engine::memory::memory_arena_config_t &arenaConfig )
{
    return cypher::engine::memory::CypherMemory_ArenaInit(
        arena,
        CypherMemory_MakeArenaDesc( arenaConfig ) );
}

void CypherMemory_ShutdownInitializedArenas()
{
    cypher::engine::memory::CypherMemory_ArenaShutdown( s_Memory.editorArena );
    cypher::engine::memory::CypherMemory_ArenaShutdown( s_Memory.renderArena );
    cypher::engine::memory::CypherMemory_ArenaShutdown( s_Memory.worldArena );
    cypher::engine::memory::CypherMemory_ArenaShutdown( s_Memory.resourceArena );
    cypher::engine::memory::CypherMemory_ArenaShutdown( s_Memory.scratchArena );
    cypher::engine::memory::CypherMemory_ArenaShutdown( s_Memory.frameArena );
    cypher::engine::memory::CypherMemory_ArenaShutdown( s_Memory.permanentArena );
}

void CypherMemory_AddArenaStats(
    const cypher::engine::memory::arena_t &arena,
    cypher::engine::memory::memory_stats_t &stats,
    const cypher::engine::memory::memory_tag_t tag )
{
    const cypher::engine::common::usize nTagIndex = static_cast<cypher::engine::common::usize>( tag );

    stats.nTotalCapacity += arena.capacity;
    stats.totalCommitted += arena.committed;
    stats.nTotalUsed += arena.used;

    if ( arena.nPeakUsed > stats.nPeakUsed ) {
        stats.nPeakUsed = arena.nPeakUsed;
    }

    if ( nTagIndex < static_cast<cypher::engine::common::usize>( cypher::engine::memory::memory_tag_t::COUNT ) ) {
        stats.tagStats[nTagIndex].name = cypher::engine::memory::CypherMemory_TagName( tag );
        stats.tagStats[nTagIndex].used += arena.used;
        stats.tagStats[nTagIndex].nPeakUsed += arena.nPeakUsed;
        stats.tagStats[nTagIndex].nAllocationCount += arena.nAllocationCount;
        stats.tagStats[nTagIndex].nFailedAllocationCount += arena.nFailedAllocationCount;
    }
}

}       // namespace

namespace cypher::engine::memory
{

memory_config_t CypherMemory_DefaultConfig()
{
    memory_config_t config{};

    config.permanentArena = memory_arena_config_t{
        "PermanentArena",
        CypherMemory_Megabytes( 256u ),
        CypherMemory_Megabytes( 16u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::CORE
    };

    config.frameArena = memory_arena_config_t{
        "FrameArena",
        CypherMemory_Megabytes( 64u ),
        CypherMemory_Megabytes( 8u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC | CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_RESET | CYPHER_MEMORY_ARENA_FLAG_DECOMMIT_ON_RESET,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::TEMP
    };

    config.scratchArena = memory_arena_config_t{
        "ScratchArena",
        CypherMemory_Megabytes( 128u ),
        CypherMemory_Megabytes( 8u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC | CYPHER_MEMORY_ARENA_FLAG_DECOMMIT_ON_RESET,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::TEMP
    };

    config.resourceArena = memory_arena_config_t{
        "ResourceArena",
        CypherMemory_Gigabytes( 2u ),
        CypherMemory_Megabytes( 64u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::RESOURCE
    };

    config.worldArena = memory_arena_config_t{
        "WorldArena",
        CypherMemory_Gigabytes( 1u ),
        CypherMemory_Megabytes( 64u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::WORLD
    };

    config.renderArena = memory_arena_config_t{
        "RenderArena",
        CypherMemory_Megabytes( 512u ),
        CypherMemory_Megabytes( 32u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::RENDER
    };

    config.editorArena = memory_arena_config_t{
        "EditorArena",
        CypherMemory_Megabytes( 512u ),
        CypherMemory_Megabytes( 32u ),
        CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC,
        arena_backing_t::ARENA_VIRTUAL_MEMORY,
        memory_tag_t::EDITOR
    };

    return config;
}

mem_error_t CypherMemory_Init( const memory_config_t &config )
{
    if ( s_Memory.initialized ) {
        LOG_WARNING( log::channel_t::MEMORY, "memory system is already initialized." );
        return mem_error_t::ERR_ALREADY_INITIALIZED;
    }

    s_Memory = {};
    s_Memory.config = config;

    mem_error_t result = CypherMemory_InitArena( s_Memory.permanentArena, config.permanentArena );
    if ( result != mem_error_t::OK ) {
        CypherMemory_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    result = CypherMemory_InitArena( s_Memory.frameArena, config.frameArena );
    if ( result != mem_error_t::OK ) {
        CypherMemory_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    result = CypherMemory_InitArena( s_Memory.scratchArena, config.scratchArena );
    if ( result != mem_error_t::OK ) {
        CypherMemory_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    result = CypherMemory_InitArena( s_Memory.resourceArena, config.resourceArena );
    if ( result != mem_error_t::OK ) {
        CypherMemory_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    result = CypherMemory_InitArena( s_Memory.worldArena, config.worldArena );
    if ( result != mem_error_t::OK ) {
        CypherMemory_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    result = CypherMemory_InitArena( s_Memory.renderArena, config.renderArena );
    if ( result != mem_error_t::OK ) {
        CypherMemory_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    result = CypherMemory_InitArena( s_Memory.editorArena, config.editorArena );
    if ( result != mem_error_t::OK ) {
        CypherMemory_ShutdownInitializedArenas();
        s_Memory = {};
        return result;
    }

    s_Memory.initialized = true;

    const memory_stats_t stats = CypherMemory_Stats();
    LOG_INFO( log::channel_t::MEMORY,
                     "memory system initialized: capacity=%zu bytes, committed=%zu bytes.",
                     stats.nTotalCapacity,
                     stats.totalCommitted );

    return mem_error_t::OK;
}

void CypherMemory_Shutdown()
{
    if ( !s_Memory.initialized ) {
        return;
    }

    const memory_stats_t stats = CypherMemory_Stats();
    LOG_INFO( log::channel_t::MEMORY,
                     "memory system shutdown: used=%zu bytes, peak=%zu bytes.",
                     stats.nTotalUsed,
                     stats.nPeakUsed );

    CypherMemory_ShutdownInitializedArenas();
    s_Memory = {};
}

void CypherMemory_BeginFrame()
{
    if ( !s_Memory.initialized ) {
        return;
    }

    CypherMemory_ArenaReset( s_Memory.frameArena );
}

void CypherMemory_EndFrame()
{
    if ( !s_Memory.initialized ) {
        return;
    }

    CypherMemory_ArenaResetCounters( s_Memory.frameArena );
}

bool CypherMemory_IsInitialized()
{
    return s_Memory.initialized;
}

memory_stats_t CypherMemory_Stats()
{
    memory_stats_t stats{};

    stats.permanentStats = CypherMemory_ArenaStats( s_Memory.permanentArena );
    stats.frameStats = CypherMemory_ArenaStats( s_Memory.frameArena );
    stats.scratchStats = CypherMemory_ArenaStats( s_Memory.scratchArena );
    stats.resourceStats = CypherMemory_ArenaStats( s_Memory.resourceArena );
    stats.worldStats = CypherMemory_ArenaStats( s_Memory.worldArena );
    stats.renderStats = CypherMemory_ArenaStats( s_Memory.renderArena );
    stats.editorStats = CypherMemory_ArenaStats( s_Memory.editorArena );

    CypherMemory_AddArenaStats( s_Memory.permanentArena, stats, s_Memory.config.permanentArena.tag );
    CypherMemory_AddArenaStats( s_Memory.frameArena, stats, s_Memory.config.frameArena.tag );
    CypherMemory_AddArenaStats( s_Memory.scratchArena, stats, s_Memory.config.scratchArena.tag );
    CypherMemory_AddArenaStats( s_Memory.resourceArena, stats, s_Memory.config.resourceArena.tag );
    CypherMemory_AddArenaStats( s_Memory.worldArena, stats, s_Memory.config.worldArena.tag );
    CypherMemory_AddArenaStats( s_Memory.renderArena, stats, s_Memory.config.renderArena.tag );
    CypherMemory_AddArenaStats( s_Memory.editorArena, stats, s_Memory.config.editorArena.tag );

    s_Memory.nTotalCapacity = stats.nTotalCapacity;
    s_Memory.totalCommitted = stats.totalCommitted;
    s_Memory.nTotalUsed = stats.nTotalUsed;

    if ( stats.nTotalUsed > s_Memory.nPeakUsed ) {
        s_Memory.nPeakUsed = stats.nTotalUsed;
    }

    stats.nPeakUsed = s_Memory.nPeakUsed;

    return stats;
}

const char *CypherMemory_TagName( const memory_tag_t tag )
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

arena_t &CypherMemory_PermanentArena()
{
    return s_Memory.permanentArena;
}

arena_t &CypherMemory_FrameArena()
{
    return s_Memory.frameArena;
}

arena_t &CypherMemory_ScratchArena()
{
    return s_Memory.scratchArena;
}

arena_t &CypherMemory_ResourceArena()
{
    return s_Memory.resourceArena;
}

arena_t &CypherMemory_WorldArena()
{
    return s_Memory.worldArena;
}

arena_t &CypherMemory_RenderArena()
{
    return s_Memory.renderArena;
}

arena_t &CypherMemory_EditorArena()
{
    return s_Memory.editorArena;
}

}       // namespace cypher::engine::memory
