//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherMemory/CypherMemory.h
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

/*
================
Memory Contract

The memory front end routes explicit allocation, reallocation, and free requests to the selected
backend. Memory allocated by one backend must be released through that same ownership domain.
================
*/

#ifndef CYPHER_ENGINE_MEMORY_H
#define CYPHER_ENGINE_MEMORY_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon.h"
#include "CypherMemory_Arena.h"
#include "CypherMemory_Pool.h"
#include "CypherMemory_Scratch.h"
#include "CypherMemory_Bucket.h"
#include "CypherMemory_Thread.h"

namespace cypher::engine::memory
{

enum class memory_tag_t : common::u8 {
    UNKNOWN = 0, // Allocation owner is not known.
    CORE,        // Host and engine-wide runtime state.
    SYSTEM,      // Platform, process, window, and device services.
    MEMORY,      // Allocator implementation metadata.
    FILESYSTEM,  // Mounts, paths, package readers, and file buffers.
    RESOURCE,    // Loaded resource records and decoded payloads.
    WORLD,       // Map, entity, and spatial world state.
    RENDER,      // Renderer frontend and backend allocations.
    AUDIO,       // Sound system state and decoded audio data.
    PHYSICS,     // Collision and simulation state.
    AI,          // Navigation and decision-system data.
    SCRIPT,      // Lua runtime and script-owned state.
    NETWORK,     // Transport, packet, channel, client, and server state.
    EDITOR,      // Editor-only documents and UI model data.
    TOOLS,       // Offline compiler and authoring-tool state.
    TEMP,        // Explicitly short-lived scratch work.
    COUNT        // Sentinel used to size per-tag tables.
};

struct memory_tag_stats_t {
    const char *name{ nullptr };                            // Static display name for the tag.
    common::usize used{ 0u };                               // Current attributed live bytes.
    common::usize nPeakUsed{ 0u };                          // Highest attributed live-byte count.
    common::u64 nAllocationCount{ 0u };                     // Successful attributed allocations.
    common::u64 nFailedAllocationCount{ 0u };               // Failed attributed allocation attempts.
};

struct memory_arena_config_t {
    const char *name{ nullptr };                            // Borrowed process-lifetime arena name.
    common::usize nReserveSize{ 0u };                       // Arena capacity or virtual reservation in bytes.
    common::usize initialCommit{ 0u };                      // Initial accessible prefix for virtual backing.
    common::u32 flags{ CYPHER_MEMORY_ARENA_FLAG_NONE };     // Arena allocation/reset policy bits.
    arena_backing_t backing{ arena_backing_t::ARENA_VIRTUAL_MEMORY }; // Backing strategy.
    memory_tag_t tag{ memory_tag_t::MEMORY };               // Attribution category for diagnostics.
};

struct memory_config_t {
    memory_arena_config_t permanentArena;                   // Process-lifetime engine state.
    memory_arena_config_t frameArena;                       // Cleared at the frame boundary.
    memory_arena_config_t scratchArena;                     // Marker-scoped temporary work.
    memory_arena_config_t resourceArena;                    // Loaded resource metadata and payloads.
    memory_arena_config_t worldArena;                       // Current map/world lifetime.
    memory_arena_config_t renderArena;                      // Renderer-owned runtime allocations.
    memory_arena_config_t editorArena;                      // Editor/tool document lifetime.
};

struct memory_stats_t {
    common::usize nTotalCapacity{ 0u };                     // Sum of configured arena capacities.
    common::usize totalCommitted{ 0u };                     // Sum of currently accessible backing bytes.
    common::usize nTotalUsed{ 0u };                         // Sum of current arena cursors.
    common::usize nPeakUsed{ 0u };                          // Highest aggregate usage observed by the system.

    arena_stats_t permanentStats{};                         // Process-lifetime arena snapshot.
    arena_stats_t frameStats{};                             // Per-frame arena snapshot.
    arena_stats_t scratchStats{};                           // Scratch arena snapshot.
    arena_stats_t resourceStats{};                          // Resource arena snapshot.
    arena_stats_t worldStats{};                             // World arena snapshot.
    arena_stats_t renderStats{};                            // Renderer arena snapshot.
    arena_stats_t editorStats{};                            // Editor arena snapshot.

    memory_tag_stats_t tagStats[static_cast<common::usize>( memory_tag_t::COUNT )]{}; // Per-owner totals.
};

struct memory_state_t {
    bool initialized{ false };                              // All configured arenas are live and accessible.

    memory_config_t config{};                               // Owned copy of the successful initialization policy.

    arena_t permanentArena{};                               // Process-lifetime arena state.
    arena_t frameArena{};                                   // Reset at BeginFrame.
    arena_t scratchArena{};                                 // Backing for nested scratch scopes.
    arena_t resourceArena{};                                // Resource-lifetime allocations.
    arena_t worldArena{};                                   // Current world/map allocations.
    arena_t renderArena{};                                  // Renderer-lifetime allocations.
    arena_t editorArena{};                                  // Editor/tool allocations.

    common::usize nTotalCapacity{ 0u };                     // Cached aggregate capacity.
    common::usize totalCommitted{ 0u };                     // Cached aggregate commit size.
    common::usize nTotalUsed{ 0u };                         // Cached aggregate current usage.
    common::usize nPeakUsed{ 0u };                          // Highest cached aggregate usage.
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
