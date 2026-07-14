//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMemory/CypherMemory.h
//  Purpose: Declares the CypherMemory Memory module.
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

#ifndef CYPHER_ENGINE_MEMORY_H
#define CYPHER_ENGINE_MEMORY_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon.h"
#include "CypherMemory_Arena.h"
#include "CypherMemory_Pool.h"
#include "CypherMemory_Scratch.h"
#include "CypherMemory_Bucket.h"
#include "CypherMemory_Thread.h"

namespace cypher::engine::memory
{

enum class memory_tag_t : common::u8 {
    UNKNOWN = 0,
    CORE,
    SYSTEM,
    MEMORY,
    FILESYSTEM,
    RESOURCE,
    WORLD,
    RENDER,
    AUDIO,
    PHYSICS,
    AI,
    SCRIPT,
    NETWORK,
    EDITOR,
    TOOLS,
    TEMP,
    COUNT
};

struct memory_tag_stats_t {
    const char *name{ nullptr };
    common::usize used{ 0u };
    common::usize nPeakUsed{ 0u };
    common::u64 nAllocationCount{ 0u };
    common::u64 nFailedAllocationCount{ 0u };
};

struct memory_arena_config_t {
    const char *name{ nullptr };
    common::usize nReserveSize{ 0u };
    common::usize initialCommit{ 0u };
    common::u32 flags{ CYPHER_MEMORY_ARENA_FLAG_NONE };
    arena_backing_t backing{ arena_backing_t::ARENA_VIRTUAL_MEMORY };
    memory_tag_t tag{ memory_tag_t::MEMORY };
};

struct memory_config_t {
    memory_arena_config_t permanentArena;
    memory_arena_config_t frameArena;
    memory_arena_config_t scratchArena;
    memory_arena_config_t resourceArena;
    memory_arena_config_t worldArena;
    memory_arena_config_t renderArena;
    memory_arena_config_t editorArena;
};

struct memory_stats_t {
    common::usize nTotalCapacity{ 0u };
    common::usize totalCommitted{ 0u };
    common::usize nTotalUsed{ 0u };
    common::usize nPeakUsed{ 0u };

    arena_stats_t permanentStats{};
    arena_stats_t frameStats{};
    arena_stats_t scratchStats{};
    arena_stats_t resourceStats{};
    arena_stats_t worldStats{};
    arena_stats_t renderStats{};
    arena_stats_t editorStats{};

    memory_tag_stats_t tagStats[static_cast<common::usize>( memory_tag_t::COUNT )]{};
};

struct memory_state_t {
    bool initialized{ false };

    memory_config_t config{};

    arena_t permanentArena{};
    arena_t frameArena{};
    arena_t scratchArena{};
    arena_t resourceArena{};
    arena_t worldArena{};
    arena_t renderArena{};
    arena_t editorArena{};

    common::usize nTotalCapacity{ 0u };
    common::usize totalCommitted{ 0u };
    common::usize nTotalUsed{ 0u };
    common::usize nPeakUsed{ 0u };
};

memory_config_t CypherMemory_DefaultConfig();

mem_error_t CypherMemory_Init( const memory_config_t &config );

void CypherMemory_Shutdown();

void CypherMemory_BeginFrame();

void CypherMemory_EndFrame();

bool CypherMemory_IsInitialized();

memory_stats_t CypherMemory_Stats();

const char *CypherMemory_TagName( memory_tag_t tag );

arena_t &CypherMemory_PermanentArena();

arena_t &CypherMemory_FrameArena();

arena_t &CypherMemory_ScratchArena();

arena_t &CypherMemory_ResourceArena();

arena_t &CypherMemory_WorldArena();

arena_t &CypherMemory_RenderArena();

arena_t &CypherMemory_EditorArena();

}       // namespace cypher::engine::memory

#endif // CYPHER_ENGINE_MEMORY_H
