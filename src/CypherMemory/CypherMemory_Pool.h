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
constexpr common::u32 CYPHER_MEMORY_POOL_FLAG_NONE              = 0u;
constexpr common::u32 CYPHER_MEMORY_POOL_FLAG_ZERO_ON_ALLOC     = 1u << 0u;
constexpr common::u32 CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_FREE     = 1u << 1u;
constexpr common::u32 CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_RESET    = 1u << 2u;
constexpr common::u32 CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_SHUTDOWN = 1u << 3u;

constexpr common::usize CYPHER_MEMORY_POOL_OPERATION_TRACE_COUNT = 64u;

enum class pool_backing_t : common::u8 {
    POOL_ARENA = 0,
    POOL_EXTERNAL_BUFFER
};

enum class pool_operation_t : common::u8 {
    POOL_OPERATION_ALLOC = 0,
    POOL_OPERATION_FREE,
    POOL_OPERATION_RESET
};

/*
================
Pool Description

Creation request for a fixed-block allocator. A pool never owns an arena or
external buffer; it only manages slots inside memory provided by them.
================
*/
struct pool_desc_t {
    const char *name{ nullptr };

    arena_t *arena{ nullptr };
    void *pExternalBuffer{ nullptr };
    common::usize nExternalBufferSize{ 0u };

    common::usize nSlotSize{ 0u };
    common::usize nSlotCount{ 0u };
    common::usize alignment{ CYPHER_MEMORY_DEFAULT_ALIGNMENT };

    common::u32 flags{ CYPHER_MEMORY_POOL_FLAG_NONE };
    pool_backing_t backing{ pool_backing_t::POOL_ARENA };
};

struct pool_free_node_t;

/*
================
Pool Operation Trace

Small ring buffer for recent pool operations. This is diagnostic data only.
================
*/
struct pool_operation_trace_t {
    const char *file{ nullptr };
    const char *function{ nullptr };
    common::i32 line{ 0 };

    void *ptr{ nullptr };
    common::usize nSlotIndex{ 0u };

    common::u64 nOperationIndex{ 0u };
    pool_operation_t operation{ pool_operation_t::POOL_OPERATION_ALLOC };
    mem_error_t error{ mem_error_t::OK };
    bool failed{ false };
};

/*
================
Pool Stats

Snapshot of fixed-block pool usage.
================
*/
struct pool_stats_t {
    const char *name{ nullptr };

    common::usize nSlotSize{ 0u };
    common::usize nSlotStride{ 0u };
    common::usize nSlotCount{ 0u };
    common::usize nUsedCount{ 0u };
    common::usize nFreeCount{ 0u };
    common::usize nPeakUsedCount{ 0u };

    common::usize nSlotBytes{ 0u };
    common::usize nMetadataBytes{ 0u };
    common::usize nBackingBytes{ 0u };

    common::u64 nAllocationCount{ 0u };
    common::u64 nFreeOperationCount{ 0u };
    common::u64 nFailedAllocationCount{ 0u };
    common::u64 nFailedFreeCount{ 0u };
};

/*
================
Pool

Fixed-block allocator state. The free list is stored inside free slots, while
the allocation bitmap detects invalid frees and double frees.
================
*/
struct pool_t {
    const char *name{ nullptr };

    common::byte *base{ nullptr };
    pool_free_node_t *freeList{ nullptr };
    common::u64 *allocationBits{ nullptr };

    common::usize nSlotSize{ 0u };
    common::usize nSlotStride{ 0u };
    common::usize nSlotCount{ 0u };
    common::usize alignment{ CYPHER_MEMORY_DEFAULT_ALIGNMENT };

    common::usize nSlotBytes{ 0u };
    common::usize nMetadataBytes{ 0u };
    common::usize nBackingBytes{ 0u };
    common::usize nAllocationWordCount{ 0u };

    common::usize nUsedCount{ 0u };
    common::usize nFreeCount{ 0u };
    common::usize nPeakUsedCount{ 0u };

    common::u64 nAllocationCount{ 0u };
    common::u64 nFreeOperationCount{ 0u };
    common::u64 nFailedAllocationCount{ 0u };
    common::u64 nFailedFreeCount{ 0u };

    common::u32 flags{ CYPHER_MEMORY_POOL_FLAG_NONE };
    pool_backing_t backing{ pool_backing_t::POOL_ARENA };
    mem_error_t lastError{ mem_error_t::OK };

    pool_operation_trace_t pOperationTraces[CYPHER_MEMORY_POOL_OPERATION_TRACE_COUNT]{};
    common::usize nOperationTraceIndex{ 0u };
    common::usize nOperationTraceCount{ 0u };

    bool initialized{ false };
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
