//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherMemory/CypherMemory_Arena.cpp
//  Purpose: Implements the CypherMemory Memory Arena module.
//  Details: This file participates in the engine allocation layer for arenas, pools,
//           buckets, scratch memory, and diagnostics. Keep ownership and lifetime
//           rules explicit because allocator bugs corrupt everything above them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMemory_Arena.h"
#include "CypherLog.h"
#include "CypherSystem_Platform.h"

#include <cstdlib>          // for malloc func
#include <cstring>          // for string memset
#include <new>              // for operator new

namespace cypher::engine::memory
{

namespace {

/*
================
CypherMemory_AlignSizeForward
================
*/
common::usize CypherMemory_AlignSizeForward( const common::usize size, const common::usize alignment )
{
    if ( size == 0u || alignment == 0u ) {
        return size;
    }

    common::usize nAlignedSize = size;
    if ( !CypherMemory_AlignForwardChecked( size, alignment, nAlignedSize ) ) {
        return 0u;
    }

    return nAlignedSize;
}

/*
================
CypherMemory_ArenaRecordAllocationTrace
================
*/
void CypherMemory_ArenaRecordAllocationTrace(
    arena_t &arena,
    void *ptr,
    const common::usize size,
    const common::usize alignment,
    const char *file,
    const char *function,
    const common::i32 line,
    const mem_error_t error,
    const bool failed )
{
    const common::usize nTraceIndex = arena.nAllocationTraceIndex % CYPHER_MEMORY_ARENA_ALLOCATION_TRACE_COUNT;

    arena_allocation_trace_t &trace = arena.pAllocationTraces[nTraceIndex];
    trace.file = file;
    trace.function = function;
    trace.line = line;
    trace.ptr = ptr;
    trace.size = size;
    trace.alignment = alignment;
    trace.nUsedAfter = arena.used;
    trace.nAllocationIndex = arena.nAllocationCount + arena.nFailedAllocationCount + 1u;
    trace.error = error;
    trace.failed = failed;

    arena.nAllocationTraceIndex = ( arena.nAllocationTraceIndex + 1u ) % CYPHER_MEMORY_ARENA_ALLOCATION_TRACE_COUNT;
    if ( arena.nAllocationTraceCount < CYPHER_MEMORY_ARENA_ALLOCATION_TRACE_COUNT ) {
        ++arena.nAllocationTraceCount;
    }
}

/*
================
CypherMemory_ArenaDecommitToInitialCommit
================
*/
void CypherMemory_ArenaDecommitToInitialCommit( arena_t &arena )
{
    if ( arena.backing != arena_backing_t::ARENA_VIRTUAL_MEMORY ) {
        return;
    }

    if ( ( arena.flags & CYPHER_MEMORY_ARENA_FLAG_DECOMMIT_ON_RESET ) == 0u ) {
        return;
    }

    if ( arena.committed <= arena.initialCommit ) {
        return;
    }

    const common::usize nDecommitOffset = arena.initialCommit;
    const common::usize nDecommitSize = arena.committed - arena.initialCommit;
    const sys::sys_error_t decommitResult = sys::Sys_VirtualDecommit( arena.base + nDecommitOffset, nDecommitSize );

    if ( decommitResult != sys::sys_error_t::OK ) {
        arena.lastError = mem_error_t::ERR_MEMORY_DECOMMIT;
        LOG_ERROR( log::channel_t::MEMORY,
                          "arena '%s' reset failed to decommit %zu bytes.",
                          arena.name ? arena.name : "<unnamed>",
                          nDecommitSize );
        return;
    }

    arena.committed = arena.initialCommit;
}

}       // namespace

mem_error_t CypherMemory_ArenaInit( arena_t &arena, const arena_desc_t &arenaDesc )
{
    if ( arena.initialized ) {
        arena.lastError = mem_error_t::ERR_ALREADY_INITIALIZED;
        LOG_WARNING( log::channel_t::MEMORY, "arena '%s' is already initialized.", arena.name ? arena.name : "<unnamed>" );
        return mem_error_t::ERR_ALREADY_INITIALIZED;
    }
    if ( arenaDesc.capacity == 0u ) {
        arena.lastError = mem_error_t::ERR_INVALID_CAPACITY;
        LOG_ERROR( log::channel_t::MEMORY, "arena init failed for '%s': invalid capacity.", arenaDesc.name ? arenaDesc.name : "<unnamed>" );
        return mem_error_t::ERR_INVALID_CAPACITY;
    }

    void *memory = nullptr;
    common::usize capacity = arenaDesc.capacity;
    common::usize committed = arenaDesc.capacity;
    common::usize nPageSize = 0u;
    bool pOwnsMemory = true;

    switch ( arenaDesc.backing ) {
    case arena_backing_t::ARENA_HEAP:
        memory = ::operator new( capacity, std::nothrow );
        if ( memory == nullptr ) {
            arena.lastError = mem_error_t::ERR_MEMORY_ALLOCATION;
            LOG_ERROR( log::channel_t::MEMORY, "arena init failed for '%s': heap allocation of %zu bytes failed.", arenaDesc.name ? arenaDesc.name : "<unnamed>", capacity );
            return mem_error_t::ERR_MEMORY_ALLOCATION;
        }
        break;

    case arena_backing_t::ARENA_EXTERNAL_BUFFER:
        if ( arenaDesc.pExternalBuffer == nullptr ) {
            arena.lastError = mem_error_t::ERR_EXTERNAL_BUFFER_REQUIRED;
            LOG_ERROR( log::channel_t::MEMORY, "arena init failed for '%s': external buffer is required.", arenaDesc.name ? arenaDesc.name : "<unnamed>" );
            return mem_error_t::ERR_EXTERNAL_BUFFER_REQUIRED;
        }

        memory = arenaDesc.pExternalBuffer;
        pOwnsMemory = false;
        break;

    case arena_backing_t::ARENA_VIRTUAL_MEMORY:
        nPageSize = sys::Sys_VirtualPageSize();

        if ( nPageSize == 0u || !CypherMemory_IsPowerOfTwo( nPageSize ) ) {
            arena.lastError = mem_error_t::ERR_INVALID_ALIGNMENT;
            LOG_ERROR( log::channel_t::MEMORY, "arena init failed for '%s': invalid virtual page size %zu.", arenaDesc.name ? arenaDesc.name : "<unnamed>", nPageSize );
            return mem_error_t::ERR_INVALID_ALIGNMENT;
        }

        capacity = CypherMemory_AlignSizeForward( arenaDesc.capacity, nPageSize );

        if ( capacity == 0u ) {
            arena.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
            LOG_ERROR( log::channel_t::MEMORY, "arena init failed for '%s': virtual capacity alignment overflowed.", arenaDesc.name ? arenaDesc.name : "<unnamed>" );
            return mem_error_t::ERR_INTEGER_OVERFLOW;
        }

        committed = arenaDesc.initialCommit;
        if ( committed == 0u && ( arenaDesc.flags & CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC ) == 0u ) {
            committed = capacity;
        }

        if ( committed > capacity ) {
            arena.lastError = mem_error_t::ERR_INVALID_CAPACITY;
            LOG_ERROR( log::channel_t::MEMORY, "arena init failed for '%s': initial commit %zu exceeds capacity %zu.", arenaDesc.name ? arenaDesc.name : "<unnamed>", committed, capacity );
            return mem_error_t::ERR_INVALID_CAPACITY;
        }

        committed = CypherMemory_AlignSizeForward( committed, nPageSize );
        if ( committed == 0u && arenaDesc.initialCommit > 0u ) {
            arena.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
            LOG_ERROR( log::channel_t::MEMORY, "arena init failed for '%s': initial commit alignment overflowed.", arenaDesc.name ? arenaDesc.name : "<unnamed>" );
            return mem_error_t::ERR_INTEGER_OVERFLOW;
        }

        memory = sys::Sys_VirtualReserve( capacity );

        if ( memory == nullptr ) {
            arena.lastError = mem_error_t::ERR_MEMORY_RESERVE;
            LOG_ERROR( log::channel_t::MEMORY, "arena init failed for '%s': virtual reserve of %zu bytes failed.", arenaDesc.name ? arenaDesc.name : "<unnamed>", capacity );
            return mem_error_t::ERR_MEMORY_RESERVE;
        }

        if ( committed > 0u ) {
            const sys::sys_error_t commitResult = sys::Sys_VirtualCommit( memory, committed );
            if ( commitResult != sys::sys_error_t::OK ) {
                sys::Sys_VirtualRelease( memory, capacity );
                arena.lastError = mem_error_t::ERR_MEMORY_COMMIT;
                LOG_ERROR( log::channel_t::MEMORY, "arena init failed for '%s': virtual commit of %zu bytes failed.", arenaDesc.name ? arenaDesc.name : "<unnamed>", committed );
                return mem_error_t::ERR_MEMORY_COMMIT;
            }
        }
        break;

    default:
        arena.lastError = mem_error_t::ERR_INVALID_ARGUMENT;
        LOG_ERROR( log::channel_t::MEMORY, "arena init failed for '%s': invalid backing type.", arenaDesc.name ? arenaDesc.name : "<unnamed>" );
        return mem_error_t::ERR_INVALID_ARGUMENT;
    }

    arena.name = arenaDesc.name;
    arena.base = static_cast<common::byte *>( memory );

    arena.capacity = capacity;
    arena.used = 0u;
    arena.nPeakUsed = 0u;

    arena.committed = committed;
    arena.initialCommit = committed;
    arena.nPageSize = nPageSize;

    arena.nAllocationCount = 0u;
    arena.nFailedAllocationCount = 0u;

    arena.flags = arenaDesc.flags;
    arena.lastError = mem_error_t::OK;

    arena.backing = arenaDesc.backing;

    arena.initialized = true;
    arena.pOwnsMemory = pOwnsMemory;

    LOG_INFO( log::channel_t::MEMORY, "arena '%s' initialized: capacity=%zu bytes, committed=%zu bytes, backing=%u.", arena.name ? arena.name : "<unnamed>", arena.capacity, arena.committed, static_cast<common::u32>( arena.backing ) );

    return mem_error_t::OK;
}

void CypherMemory_ArenaShutdown( arena_t &arena )
{
    if ( !arena.initialized ) {
        return ;
    }

    LOG_INFO( log::channel_t::MEMORY, "arena '%s' shutdown: used=%zu bytes, peak=%zu bytes, allocations=%llu, failed=%llu.",
                     arena.name ? arena.name : "<unnamed>",
                     arena.used,
                     arena.nPeakUsed,
                     static_cast<unsigned long long>( arena.nAllocationCount ),
                     static_cast<unsigned long long>( arena.nFailedAllocationCount ) );

    if ( ( arena.flags & CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_SHUTDOWN ) != 0u && arena.base != nullptr && arena.used > 0u ) {
        std::memset( arena.base, 0, arena.used );
    }

    if ( arena.pOwnsMemory && arena.base != nullptr ) {
        if ( arena.backing == arena_backing_t::ARENA_VIRTUAL_MEMORY ) {
            const sys::sys_error_t releaseResult = sys::Sys_VirtualRelease( arena.base, arena.capacity );
            if ( releaseResult != sys::sys_error_t::OK ) {
                LOG_ERROR( log::channel_t::MEMORY, "arena '%s' virtual release failed during shutdown.", arena.name ? arena.name : "<unnamed>" );
            }
        } else {
            ::operator delete( arena.base );
        }
    }

    arena = arena_t{};
}

void CypherMemory_ArenaReset( arena_t &arena )
{
    // Only resets the arena memory does not free the memory itself.
    if ( !arena.initialized ) {
        return ;
    }

    if  ( ( arena.flags & CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_RESET ) != 0 ) {
        common::usize nClearSize = arena.used;
        if ( arena.backing == arena_backing_t::ARENA_VIRTUAL_MEMORY &&
             ( arena.flags & CYPHER_MEMORY_ARENA_FLAG_DECOMMIT_ON_RESET ) != 0u &&
             nClearSize > arena.initialCommit ) {
            nClearSize = arena.initialCommit;
        }

        if ( nClearSize > 0u ) {
            std::memset( arena.base, 0, nClearSize );
        }
    }

    CypherMemory_ArenaDecommitToInitialCommit( arena );

    arena.used = 0u;
    if ( arena.lastError != mem_error_t::ERR_MEMORY_DECOMMIT ) {
        arena.lastError = mem_error_t::OK;
    }
}

arena_stats_t CypherMemory_ArenaStats( const arena_t &arena )
{
    arena_stats_t stats{};

    stats.name = arena.name;
    stats.nAllocationCount = arena.nAllocationCount;
    stats.nFailedAllocationCount = arena.nFailedAllocationCount;
    stats.capacity = arena.capacity;
    stats.committed = arena.committed;
    stats.initialCommit = arena.initialCommit;
    stats.nPeakUsed = arena.nPeakUsed;
    stats.remaining = arena.used >= arena.capacity ? 0u : arena.capacity - arena.used;
    stats.used = arena.used;

    return stats;
}

void CypherMemory_ArenaResetCounters( arena_t &arena )
{
    if ( !arena.initialized ) {
        return ;
    }

    arena.nPeakUsed = arena.used;
    arena.nAllocationCount = 0u;
    arena.nFailedAllocationCount = 0u;
    arena.lastError = mem_error_t::OK;
}

void *CypherMemory_ArenaAlloc( arena_t &arena, common::usize size, common::usize alignment )
{
    return CypherMemory_ArenaAllocDebug( arena, size, alignment, nullptr, nullptr, 0 );
}

void *CypherMemory_ArenaAllocDebug(
    arena_t &arena,
    common::usize size,
    common::usize alignment,
    const char *file,
    const char *function,
    common::i32 line )
{
    if ( !arena.initialized ) {
        arena.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        ++arena.nFailedAllocationCount;
        CypherMemory_ArenaRecordAllocationTrace( arena, nullptr, size, alignment, file, function, line, arena.lastError, true );
        LOG_ERROR( log::channel_t::MEMORY, "arena allocation failed: arena is not initialized." );
        return nullptr;
    }
    if ( size == 0u ) {
        arena.lastError = mem_error_t::ERR_INVALID_ARGUMENT;
        ++arena.nFailedAllocationCount;
        CypherMemory_ArenaRecordAllocationTrace( arena, nullptr, size, alignment, file, function, line, arena.lastError, true );
        LOG_ERROR( log::channel_t::MEMORY, "arena '%s' allocation failed: requested size is zero.", arena.name ? arena.name : "<unnamed>" );
        return nullptr;
    }

    if ( !CypherMemory_IsPowerOfTwo( alignment ) ) {
        arena.lastError = mem_error_t::ERR_INVALID_ALIGNMENT;
        ++arena.nFailedAllocationCount;
        CypherMemory_ArenaRecordAllocationTrace( arena, nullptr, size, alignment, file, function, line, arena.lastError, true );
        LOG_ERROR( log::channel_t::MEMORY, "arena '%s' allocation failed: invalid alignment %zu.", arena.name ? arena.name : "<unnamed>", alignment );
        return nullptr;
    }

    common::usize pCurrentAddress = 0u;
    if ( !CypherMemory_AddSizeChecked( reinterpret_cast<common::usize>( arena.base ), arena.used, pCurrentAddress ) ) {
        arena.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
        ++arena.nFailedAllocationCount;
        CypherMemory_ArenaRecordAllocationTrace( arena, nullptr, size, alignment, file, function, line, arena.lastError, true );
        LOG_ERROR( log::channel_t::MEMORY, "arena '%s' allocation failed: current address calculation overflowed.", arena.name ? arena.name : "<unnamed>" );
        return nullptr;
    }

    common::usize pAlignedAddress = 0u;
    if ( !CypherMemory_AlignForwardChecked( pCurrentAddress, alignment, pAlignedAddress ) ) {
        arena.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
        ++arena.nFailedAllocationCount;
        CypherMemory_ArenaRecordAllocationTrace( arena, nullptr, size, alignment, file, function, line, arena.lastError, true );
        LOG_ERROR( log::channel_t::MEMORY, "arena '%s' allocation failed: alignment calculation overflowed.", arena.name ? arena.name : "<unnamed>" );
        return nullptr;
    }

    const common::usize padding = pAlignedAddress - pCurrentAddress;

    common::usize bUsedWithPadding = 0u;
    if ( !CypherMemory_AddSizeChecked( arena.used, padding, bUsedWithPadding ) ) {
        arena.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
        ++arena.nFailedAllocationCount;
        CypherMemory_ArenaRecordAllocationTrace( arena, nullptr, size, alignment, file, function, line, arena.lastError, true );
        LOG_ERROR( log::channel_t::MEMORY, "arena '%s' allocation failed: padding calculation overflowed.", arena.name ? arena.name : "<unnamed>" );
        return nullptr;
    }

    common::usize pNewAddress = 0u;
    if ( !CypherMemory_AddSizeChecked( bUsedWithPadding, size, pNewAddress ) ) {
        arena.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
        ++arena.nFailedAllocationCount;
        CypherMemory_ArenaRecordAllocationTrace( arena, nullptr, size, alignment, file, function, line, arena.lastError, true );
        LOG_ERROR( log::channel_t::MEMORY, "arena '%s' allocation failed: allocation end calculation overflowed.", arena.name ? arena.name : "<unnamed>" );
        return nullptr;
    }

    if ( pNewAddress > arena.capacity ) {
        arena.lastError = mem_error_t::ERR_OUT_OF_MEMORY;
        ++arena.nFailedAllocationCount;
        CypherMemory_ArenaRecordAllocationTrace( arena, nullptr, size, alignment, file, function, line, arena.lastError, true );
        LOG_ERROR( log::channel_t::MEMORY, "arena '%s' allocation failed: requested=%zu, alignment=%zu, used=%zu, capacity=%zu.", arena.name ? arena.name : "<unnamed>", size, alignment, arena.used, arena.capacity );
        return nullptr;
    }

    if ( arena.backing == arena_backing_t::ARENA_VIRTUAL_MEMORY && pNewAddress > arena.committed ) {
        if ( ( arena.flags & CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC ) == 0u ) {
            arena.lastError = mem_error_t::ERR_MEMORY_COMMIT;
            ++arena.nFailedAllocationCount;
            CypherMemory_ArenaRecordAllocationTrace( arena, nullptr, size, alignment, file, function, line, arena.lastError, true );
            LOG_ERROR( log::channel_t::MEMORY, "arena '%s' allocation failed: requested=%zu exceeds committed=%zu.", arena.name ? arena.name : "<unnamed>", pNewAddress, arena.committed );
            return nullptr;
        }

        common::usize commitTarget = CypherMemory_AlignSizeForward( pNewAddress, arena.nPageSize );
        if ( commitTarget == 0u ) {
            arena.lastError = mem_error_t::ERR_INTEGER_OVERFLOW;
            ++arena.nFailedAllocationCount;
            CypherMemory_ArenaRecordAllocationTrace( arena, nullptr, size, alignment, file, function, line, arena.lastError, true );
            LOG_ERROR( log::channel_t::MEMORY, "arena '%s' allocation failed: commit target calculation overflowed.", arena.name ? arena.name : "<unnamed>" );
            return nullptr;
        }

        if ( commitTarget > arena.capacity ) {
            commitTarget = arena.capacity;
        }

        const common::usize nCommitSize = commitTarget - arena.committed;
        const sys::sys_error_t commitResult = sys::Sys_VirtualCommit( arena.base + arena.committed, nCommitSize );

        if ( commitResult != sys::sys_error_t::OK ) {
            arena.lastError = mem_error_t::ERR_MEMORY_COMMIT;
            ++arena.nFailedAllocationCount;
            CypherMemory_ArenaRecordAllocationTrace( arena, nullptr, size, alignment, file, function, line, arena.lastError, true );
            LOG_ERROR( log::channel_t::MEMORY, "arena '%s' allocation failed: virtual commit of %zu bytes failed.", arena.name ? arena.name : "<unnamed>", nCommitSize );
            return nullptr;
        }

        arena.committed = commitTarget;
    }

    arena.used = pNewAddress;

    if ( arena.used > arena.nPeakUsed ) {
        arena.nPeakUsed = arena.used;
    }

    ++arena.nAllocationCount;
    arena.lastError = mem_error_t::OK;

    void *result = reinterpret_cast<void *>( pAlignedAddress );

    if ( ( arena.flags & CYPHER_MEMORY_ARENA_FLAG_ZERO_ON_ALLOC ) != 0 ) {
        std::memset( result, 0, size );
    }

    CypherMemory_ArenaRecordAllocationTrace( arena, result, size, alignment, file, function, line, arena.lastError, false );

    return result;
}

void *CypherMemory_ArenaAllocZero( arena_t &arena, common::usize size, common::usize alignment )
{
    return CypherMemory_ArenaAllocZeroDebug( arena, size, alignment, nullptr, nullptr, 0 );
}

void *CypherMemory_ArenaAllocZeroDebug(
    arena_t &arena,
    common::usize size,
    common::usize alignment,
    const char *file,
    const char *function,
    common::i32 line )
{
    void *memory = CypherMemory_ArenaAllocDebug( arena, size, alignment, file, function, line );

    if ( memory == nullptr ) {
        return nullptr;
    }
    std::memset( memory, 0, size );

    return memory;
}

common::usize CypherMemory_ArenaRemaining( const arena_t &arena )
{
    if ( !arena.initialized ) {
        return 0u;
    }

    if ( arena.used >= arena.capacity ) {
        return 0u;
    }

    return arena.capacity - arena.used;
}

const arena_allocation_trace_t *CypherMemory_ArenaAllocationTraces( const arena_t &arena, common::usize &nOutCount )
{
    nOutCount = arena.nAllocationTraceCount;
    return arena.pAllocationTraces;
}

arena_marker_t CypherMemory_ArenaGetMarker( const arena_t &arena )
{
    arena_marker_t marker{};

    if ( !arena.initialized ) {
        return marker;
    }

    marker.used = arena.used;
    return marker;
}

mem_error_t CypherMemory_ArenaRewind( arena_t &arena, arena_marker_t marker )
{
    if ( !arena.initialized ) {
        arena.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        LOG_ERROR( log::channel_t::MEMORY, "arena rewind failed: arena is not initialized." );
        return mem_error_t::ERR_NOT_INITIALIZED;
    }

    if ( marker.used > arena.used || marker.used > arena.capacity ) {
        arena.lastError = mem_error_t::ERR_INVALID_MARKER;
        LOG_ERROR( log::channel_t::MEMORY, "arena '%s' rewind failed: marker=%zu, used=%zu, capacity=%zu.", arena.name ? arena.name : "<unnamed>", marker.used, arena.used, arena.capacity );
        return mem_error_t::ERR_INVALID_MARKER;
    }

    if ( ( arena.flags & CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_RESET ) != 0u ) {
        std::memset( arena.base + marker.used,
                     0,
                     arena.used - marker.used );
    }

    arena.used = marker.used;
    arena.lastError = mem_error_t::OK;

    return mem_error_t::OK;
}

bool CypherMemory_ArenaContains( const arena_t &arena, const void *ptr )
{
    if ( !arena.initialized || arena.base == nullptr || ptr == nullptr ) {
        return false;
    }

    const common::usize address = reinterpret_cast<common::usize>( ptr );
    const common::usize base = reinterpret_cast<common::usize>( arena.base );

    if ( address < base ) {
        return false;
    }

    return ( address - base ) < arena.capacity;
}

mem_error_t CypherMemory_ArenaLastError( const arena_t &arena )
{
    return arena.lastError;
}

bool CypherMemory_ArenaIsInitialized( const arena_t &arena )
{
    return arena.initialized;
}

common::usize CypherMemory_ArenaUsed( const arena_t &arena )
{
    return arena.used;
}

common::f32 CypherMemory_ArenaUsageRatio( const arena_t &arena )
{
    if ( arena.capacity == 0u ) {
        return 0.0f;
    }
    return static_cast<common::f32>( arena.used ) / static_cast<common::f32>( arena.capacity );
}

common::usize CypherMemory_ArenaCapacity( const arena_t &arena )
{
    if ( !arena.initialized ) {
        return 0u;
    }

    return arena.capacity;
}

}       // namespace cypher::engine::memory
