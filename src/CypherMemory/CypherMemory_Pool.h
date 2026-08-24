//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherMemory/CypherMemory_Pool.h
//  Purpose: Declares the CypherMemory Memory Pool module.
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

#ifndef CYPHER_ENGINE_MEMORY_POOL_H
#define CYPHER_ENGINE_MEMORY_POOL_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon.h"
#include "CypherMemory_Arena.h"

namespace cypher::engine::memory
{

/*
================
Pool Constants
================
*/
constexpr common::u32 CYPHER_MEMORY_POOL_FLAG_NONE              = 0u;       // Preserve slot bytes across operations.
constexpr common::u32 CYPHER_MEMORY_POOL_FLAG_ZERO_ON_ALLOC     = 1u << 0u; // Return zero-filled payloads.
constexpr common::u32 CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_FREE     = 1u << 1u; // Scrub payload bytes before reuse.
constexpr common::u32 CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_RESET    = 1u << 2u; // Scrub every slot during bulk reset.
constexpr common::u32 CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_SHUTDOWN = 1u << 3u; // Scrub managed backing before shutdown.

constexpr common::usize CYPHER_MEMORY_POOL_OPERATION_TRACE_COUNT = 64u;

enum class pool_backing_t : common::u8 {
    POOL_ARENA = 0,      // Pool borrows one allocation carved from an arena.
    POOL_EXTERNAL_BUFFER // Pool manages caller-owned storage without releasing it.
};

enum class pool_operation_t : common::u8 {
    POOL_OPERATION_ALLOC = 0, // Slot allocation attempt.
    POOL_OPERATION_FREE,      // Slot release attempt.
    POOL_OPERATION_RESET      // Bulk return of every slot to the free list.
};

/*
================
Pool Description

Creation request for a fixed-block allocator. A pool never owns an arena or
external buffer; it only manages slots inside memory provided by them.
================
*/
struct pool_desc_t {
    const char *name{ nullptr };                            // Borrowed diagnostic name; must outlive the pool.

    arena_t *arena{ nullptr };                              // Borrowed backing arena for POOL_ARENA.
    void *pExternalBuffer{ nullptr };                       // Caller-owned backing base for EXTERNAL_BUFFER.
    common::usize nExternalBufferSize{ 0u };                // Usable external backing extent in bytes.

    common::usize nSlotSize{ 0u };                          // Maximum caller payload bytes per slot.
    common::usize nSlotCount{ 0u };                         // Fixed number of independently allocatable slots.
    common::usize alignment{ CYPHER_MEMORY_DEFAULT_ALIGNMENT }; // Alignment of every returned slot.

    common::u32 flags{ CYPHER_MEMORY_POOL_FLAG_NONE };      // CYPHER_MEMORY_POOL_FLAG_* policy bits.
    pool_backing_t backing{ pool_backing_t::POOL_ARENA };   // Selects which backing descriptor is active.
};

struct pool_free_node_t;

/*
================
Pool Operation Trace

Small ring buffer for recent pool operations. This is diagnostic data only.
================
*/
struct pool_operation_trace_t {
    const char *file{ nullptr };                            // Borrowed source filename for the operation.
    const char *function{ nullptr };                        // Borrowed source function name.
    common::i32 line{ 0 };                                  // One-based source line, or zero when unavailable.

    void *ptr{ nullptr };                                   // Slot address involved in the operation.
    common::usize nSlotIndex{ 0u };                         // Pool-relative slot index when known.

    common::u64 nOperationIndex{ 0u };                      // Monotonic ordering number for traces.
    pool_operation_t operation{ pool_operation_t::POOL_OPERATION_ALLOC }; // Operation category.
    mem_error_t error{ mem_error_t::OK };                   // Result captured at the call boundary.
    bool failed{ false };                                   // True when the requested operation did not complete.
};

/*
================
Pool Stats

Snapshot of fixed-block pool usage.
================
*/
struct pool_stats_t {
    const char *name{ nullptr };                            // Borrowed pool diagnostic name.

    common::usize nSlotSize{ 0u };                          // Caller-visible payload bytes per slot.
    common::usize nSlotStride{ 0u };                        // Aligned distance between adjacent slots.
    common::usize nSlotCount{ 0u };                         // Total slots in the pool.
    common::usize nUsedCount{ 0u };                         // Slots currently checked out.
    common::usize nFreeCount{ 0u };                         // Slots currently on the free list.
    common::usize nPeakUsedCount{ 0u };                     // Highest simultaneous used count.

    common::usize nSlotBytes{ 0u };                         // Bytes occupied by all slot strides.
    common::usize nMetadataBytes{ 0u };                     // Bytes occupied by the allocation bitmap.
    common::usize nBackingBytes{ 0u };                      // Complete managed backing size.

    common::u64 nAllocationCount{ 0u };                     // Successful slot allocations.
    common::u64 nFreeOperationCount{ 0u };                  // Successful slot releases.
    common::u64 nFailedAllocationCount{ 0u };               // Rejected or exhausted allocations.
    common::u64 nFailedFreeCount{ 0u };                     // Invalid, foreign, or duplicate frees.
};

/*
================
Pool

Fixed-block allocator state. The free list is stored inside free slots, while
the allocation bitmap detects invalid frees and double frees.
================
*/
struct pool_t {
    const char *name{ nullptr };                            // Borrowed diagnostic name.

    common::byte *base{ nullptr };                          // First slot in the managed backing region.
    pool_free_node_t *freeList{ nullptr };                  // Intrusive list whose nodes occupy free slots.
    common::u64 *allocationBits{ nullptr };                 // One liveness bit per slot for free validation.

    common::usize nSlotSize{ 0u };                          // Caller-visible payload bytes per slot.
    common::usize nSlotStride{ 0u };                        // Aligned byte distance between slots.
    common::usize nSlotCount{ 0u };                         // Fixed capacity of the pool.
    common::usize alignment{ CYPHER_MEMORY_DEFAULT_ALIGNMENT }; // Guaranteed address alignment.

    common::usize nSlotBytes{ 0u };                         // nSlotStride * nSlotCount.
    common::usize nMetadataBytes{ 0u };                     // Allocation-bitmap bytes after alignment.
    common::usize nBackingBytes{ 0u };                      // Total bytes borrowed from the backing source.
    common::usize nAllocationWordCount{ 0u };               // Number of valid u64 words in allocationBits.

    common::usize nUsedCount{ 0u };                         // Live checked-out slots.
    common::usize nFreeCount{ 0u };                         // Slots available through freeList.
    common::usize nPeakUsedCount{ 0u };                     // Highest observed nUsedCount.

    common::u64 nAllocationCount{ 0u };                     // Successful allocations since counter reset.
    common::u64 nFreeOperationCount{ 0u };                  // Successful frees since counter reset.
    common::u64 nFailedAllocationCount{ 0u };               // Failed allocations since counter reset.
    common::u64 nFailedFreeCount{ 0u };                     // Failed frees since counter reset.

    common::u32 flags{ CYPHER_MEMORY_POOL_FLAG_NONE };      // Active clearing/zeroing policy bits.
    pool_backing_t backing{ pool_backing_t::POOL_ARENA };   // Origin of the managed backing block.
    mem_error_t lastError{ mem_error_t::OK };               // Result of the latest mutating operation.

    pool_operation_trace_t pOperationTraces[CYPHER_MEMORY_POOL_OPERATION_TRACE_COUNT]{}; // Recent-operation ring.
    common::usize nOperationTraceIndex{ 0u };               // Slot overwritten by the next trace.
    common::usize nOperationTraceCount{ 0u };               // Valid records, capped at ring capacity.

    bool initialized{ false };                              // Pool invariants and backing are ready for use.
};

using pool_allocator_t = pool_t;

mem_error_t CypherMemory_PoolInit( pool_t &pool, const pool_desc_t &poolDesc );

void CypherMemory_PoolShutdown( pool_t &pool );

pool_stats_t CypherMemory_PoolStats( const pool_t &pool );

void CypherMemory_PoolResetCounters( pool_t &pool );

void CypherMemory_PoolReset( pool_t &pool );

void *CypherMemory_PoolAlloc( pool_t &pool );

void *CypherMemory_PoolAllocDebug( pool_t &pool, const char *file, const char *function, common::i32 line );

void *CypherMemory_PoolAllocZero( pool_t &pool );

void *CypherMemory_PoolAllocZeroDebug( pool_t &pool, const char *file, const char *function, common::i32 line );

void *CypherMemory_PoolAllocSize( pool_t &pool, common::usize size, common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *CypherMemory_PoolAllocSizeDebug( pool_t &pool,
                                       common::usize size,
                                       common::usize alignment,
                                       const char *file,
                                       const char *function,
                                       common::i32 line );

void *CypherMemory_PoolAllocSizeZero( pool_t &pool, common::usize size, common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *CypherMemory_PoolAllocSizeZeroDebug( pool_t &pool,
                                           common::usize size,
                                           common::usize alignment,
                                           const char *file,
                                           const char *function,
                                           common::i32 line );

mem_error_t CypherMemory_PoolFree( pool_t &pool, void *ptr );

mem_error_t CypherMemory_PoolFreeDebug( pool_t &pool, void *ptr, const char *file, const char *function, common::i32 line );

bool CypherMemory_PoolContains( const pool_t &pool, const void *ptr );

bool CypherMemory_PoolOwnsSlot( const pool_t &pool, const void *ptr );

bool CypherMemory_PoolIsInitialized( const pool_t &pool );

mem_error_t CypherMemory_PoolLastError( const pool_t &pool );

common::usize CypherMemory_PoolSlotIndex( const pool_t &pool, const void *ptr );

common::usize CypherMemory_PoolUsedCount( const pool_t &pool );

common::usize CypherMemory_PoolFreeCount( const pool_t &pool );

common::usize CypherMemory_PoolCapacity( const pool_t &pool );

common::f32 CypherMemory_PoolUsageRatio( const pool_t &pool );

const pool_operation_trace_t *CypherMemory_PoolOperationTraces( const pool_t &pool, common::usize &nOutCount );

template <typename T>
T *CypherMemory_PoolAllocType( pool_t &pool )
{
    return static_cast<T *>( CypherMemory_PoolAllocSize( pool, sizeof( T ), alignof( T ) ) );
}

template <typename T>
T *CypherMemory_PoolAllocTypeDebug( pool_t &pool, const char *file, const char *function, common::i32 line )
{
    return static_cast<T *>( CypherMemory_PoolAllocSizeDebug( pool, sizeof( T ), alignof( T ), file, function, line ) );
}

template <typename T>
T *CypherMemory_PoolAllocTypeZero( pool_t &pool )
{
    return static_cast<T *>( CypherMemory_PoolAllocSizeZero( pool, sizeof( T ), alignof( T ) ) );
}

template <typename T>
T *CypherMemory_PoolAllocTypeZeroDebug( pool_t &pool, const char *file, const char *function, common::i32 line )
{
    return static_cast<T *>( CypherMemory_PoolAllocSizeZeroDebug( pool, sizeof( T ), alignof( T ), file, function, line ) );
}

}       // namespace cypher::engine::memory

#define CYPHER_MEMORY_POOL_ALLOC( POOL ) \
    ::cypher::engine::memory::CypherMemory_PoolAllocDebug( ( POOL ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_POOL_ALLOC_ZERO( POOL ) \
    ::cypher::engine::memory::CypherMemory_PoolAllocZeroDebug( ( POOL ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_POOL_ALLOC_SIZE( POOL, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::CypherMemory_PoolAllocSizeDebug( ( POOL ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_POOL_ALLOC_SIZE_ZERO( POOL, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::CypherMemory_PoolAllocSizeZeroDebug( ( POOL ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_POOL_ALLOC_TYPE( POOL, TYPE ) \
    ::cypher::engine::memory::CypherMemory_PoolAllocTypeDebug<TYPE>( ( POOL ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_POOL_ALLOC_TYPE_ZERO( POOL, TYPE ) \
    ::cypher::engine::memory::CypherMemory_PoolAllocTypeZeroDebug<TYPE>( ( POOL ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_POOL_FREE( POOL, PTR ) \
    ::cypher::engine::memory::CypherMemory_PoolFreeDebug( ( POOL ), ( PTR ), __FILE__, __func__, __LINE__ )

#endif // CYPHER_ENGINE_MEMORY_POOL_H
