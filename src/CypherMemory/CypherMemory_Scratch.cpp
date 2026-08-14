//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherMemory/CypherMemory_Scratch.cpp
//  Purpose: Implements the CypherMemory Memory Scratch module.
//  Details: This file participates in the engine allocation layer for arenas, pools,
//           buckets, scratch memory, and diagnostics. Keep ownership and lifetime
//           rules explicit because allocator bugs corrupt everything above them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMemory_Scratch.h"
#include "CypherLog.h"

namespace cypher::engine::memory
{

namespace {

void *CypherMemory_ScratchFailAlloc( scratch_scope_t &scope,
                                     const mem_error_t error,
                                     const char *reason )
{
    scope.lastError = error;

    LOG_ERROR( log::channel_t::MEMORY,
                      "scratch scope '%s' allocation failed: %s.",
                      scope.name ? scope.name : "<unnamed>",
                      reason ? reason : CypherMemory_ErrorDesc( error ) );

    return nullptr;
}

}       // namespace

mem_error_t CypherMemory_ScratchBegin( scratch_scope_t &scope, arena_t &arena, const char *name )
{
    if ( scope.active ) {
        scope.lastError = mem_error_t::ERR_ALREADY_INITIALIZED;
        LOG_WARNING( log::channel_t::MEMORY,
                            "scratch scope '%s' begin failed: scope is already active.",
                            scope.name ? scope.name : "<unnamed>" );
        return scope.lastError;
    }

    if ( !CypherMemory_ArenaIsInitialized( arena ) ) {
        scope = {};
        scope.name = name;
        scope.arena = &arena;
        scope.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        LOG_ERROR( log::channel_t::MEMORY,
                          "scratch scope '%s' begin failed: backing arena is not initialized.",
                          name ? name : "<unnamed>" );
        return scope.lastError;
    }

    scope = {};
    scope.name = name;
    scope.arena = &arena;
    scope.marker = CypherMemory_ArenaGetMarker( arena );
    scope.nUsedAtBegin = arena.used;
    scope.nAllocationCountAtBegin = arena.nAllocationCount;
    scope.nFailedAllocationCountAtBegin = arena.nFailedAllocationCount;
    scope.lastError = mem_error_t::OK;
    scope.active = true;

    return mem_error_t::OK;
}

mem_error_t CypherMemory_ScratchEnd( scratch_scope_t &scope )
{
    if ( !scope.active ) {
        scope.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return scope.lastError;
    }

    if ( scope.arena == nullptr || !CypherMemory_ArenaIsInitialized( *scope.arena ) ) {
        scope.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        scope.active = false;
        return scope.lastError;
    }

    const mem_error_t rewindResult = CypherMemory_ArenaRewind( *scope.arena, scope.marker );
    scope.lastError = rewindResult;

    const char *name = scope.name;
    scope = {};
    scope.name = name;

    return rewindResult;
}

scratch_stats_t CypherMemory_ScratchStats( const scratch_scope_t &scope )
{
    scratch_stats_t stats{};

    stats.name = scope.name;
    stats.nUsedAtBegin = scope.nUsedAtBegin;
    stats.active = scope.active;

    if ( !scope.active || scope.arena == nullptr || !CypherMemory_ArenaIsInitialized( *scope.arena ) ) {
        return stats;
    }

    stats.nUsedCurrent = scope.arena->used;
    stats.nUsedSinceBegin = stats.nUsedCurrent >= scope.nUsedAtBegin ? stats.nUsedCurrent - scope.nUsedAtBegin : 0u;
    stats.capacity = scope.arena->capacity;
    stats.nAllocationCountSinceBegin = scope.arena->nAllocationCount >= scope.nAllocationCountAtBegin
                                             ? scope.arena->nAllocationCount - scope.nAllocationCountAtBegin
                                             : 0u;
    stats.nFailedAllocationCountSinceBegin = scope.arena->nFailedAllocationCount >= scope.nFailedAllocationCountAtBegin
                                                    ? scope.arena->nFailedAllocationCount - scope.nFailedAllocationCountAtBegin
                                                    : 0u;

    return stats;
}

void *CypherMemory_ScratchAlloc( scratch_scope_t &scope, common::usize size, common::usize alignment )
{
    return CypherMemory_ScratchAllocDebug( scope, size, alignment, nullptr, nullptr, 0 );
}

void *CypherMemory_ScratchAllocDebug( scratch_scope_t &scope,
                                      common::usize size,
                                      common::usize alignment,
                                      const char *file,
                                      const char *function,
                                      common::i32 line )
{
    if ( !scope.active ) {
        return CypherMemory_ScratchFailAlloc( scope, mem_error_t::ERR_NOT_INITIALIZED, "scope is not active" );
    }

    if ( scope.arena == nullptr || !CypherMemory_ArenaIsInitialized( *scope.arena ) ) {
        return CypherMemory_ScratchFailAlloc( scope, mem_error_t::ERR_NOT_INITIALIZED, "backing arena is not initialized" );
    }

    void *memory = CypherMemory_ArenaAllocDebug( *scope.arena, size, alignment, file, function, line );
    scope.lastError = CypherMemory_ArenaLastError( *scope.arena );

    return memory;
}

void *CypherMemory_ScratchAllocZero( scratch_scope_t &scope, common::usize size, common::usize alignment )
{
    return CypherMemory_ScratchAllocZeroDebug( scope, size, alignment, nullptr, nullptr, 0 );
}

void *CypherMemory_ScratchAllocZeroDebug( scratch_scope_t &scope,
                                          common::usize size,
                                          common::usize alignment,
                                          const char *file,
                                          const char *function,
                                          common::i32 line )
{
    if ( !scope.active ) {
        return CypherMemory_ScratchFailAlloc( scope, mem_error_t::ERR_NOT_INITIALIZED, "scope is not active" );
    }

    if ( scope.arena == nullptr || !CypherMemory_ArenaIsInitialized( *scope.arena ) ) {
        return CypherMemory_ScratchFailAlloc( scope, mem_error_t::ERR_NOT_INITIALIZED, "backing arena is not initialized" );
    }

    void *memory = CypherMemory_ArenaAllocZeroDebug( *scope.arena, size, alignment, file, function, line );
    scope.lastError = CypherMemory_ArenaLastError( *scope.arena );

    return memory;
}

bool CypherMemory_ScratchIsActive( const scratch_scope_t &scope )
{
    return scope.active;
}

mem_error_t CypherMemory_ScratchLastError( const scratch_scope_t &scope )
{
    return scope.lastError;
}

}       // namespace cypher::engine::memory
