//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMemory/CypherMemory_Pool.cpp
//  Purpose: Implements the CypherMemory Memory Pool module.
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

#include "CypherMemory_Pool.h"
#include "CypherLog.h"

#include <cstring>
#include <limits>

namespace cypher::engine::memory
{

struct pool_free_node_t {
    pool_free_node_t *next{ nullptr };
};

namespace {

constexpr common::usize CYPHER_MEMORY_POOL_INVALID_SLOT_INDEX = std::numeric_limits<common::usize>::max();
constexpr common::usize CYPHER_MEMORY_POOL_ALLOCATION_BITS_PER_WORD = sizeof( common::u64 ) * 8u;

struct pool_layout_t {
    common::usize nSlotSize{ 0u };
    common::usize nSlotStride{ 0u };
    common::usize nSlotCount{ 0u };
    common::usize alignment{ CYPHER_MEMORY_DEFAULT_ALIGNMENT };
    common::usize nSlotBytes{ 0u };
    common::usize nMetadataOffset{ 0u };
    common::usize nMetadataBytes{ 0u };
    common::usize nBackingBytes{ 0u };
    common::usize nAllocationWordCount{ 0u };
};

common::usize CypherMemory_PoolMax( const common::usize a, const common::usize b )
{
    return a > b ? a : b;
}

bool CypherMemory_PoolAddChecked( const common::usize a, const common::usize b, common::usize &valueOut )
{
    const common::usize nMaxValue = std::numeric_limits<common::usize>::max();

    if ( a > nMaxValue - b ) {
        return false;
    }

    valueOut = a + b;
    return true;
}

bool CypherMemory_PoolMulChecked( const common::usize a, const common::usize b, common::usize &valueOut )
{
    const common::usize nMaxValue = std::numeric_limits<common::usize>::max();

    if ( a != 0u && b > nMaxValue / a ) {
        return false;
    }

    valueOut = a * b;
    return true;
}

bool CypherMemory_PoolAlignForwardChecked( const common::usize value,
                                           const common::usize alignment,
                                           common::usize &valueOut )
{
    if ( !CypherMemory_IsPowerOfTwo( alignment ) ) {
        return false;
    }

    const common::usize mask = alignment - 1u;
    const common::usize nMaxValue = std::numeric_limits<common::usize>::max();

    if ( value > nMaxValue - mask ) {
        return false;
    }

    valueOut = ( value + mask ) & ~mask;
    return true;
}

mem_error_t CypherMemory_PoolFailInit( pool_t &pool, const pool_desc_t &poolDesc, const mem_error_t error, const char *reason )
{
    pool.name = poolDesc.name;
    pool.lastError = error;

    LOG_ERROR( log::channel_t::MEMORY,
                      "pool '%s' init failed: %s.",
                      pool.name ? pool.name : "<unnamed>",
                      reason ? reason : CypherMemory_ErrorDesc( error ) );

    return error;
}

bool CypherMemory_PoolComputeLayout( const pool_desc_t &poolDesc, pool_layout_t &layoutOut, mem_error_t &errorOut )
{
    layoutOut = {};
    errorOut = mem_error_t::OK;

    if ( poolDesc.nSlotSize == 0u ) {
        errorOut = mem_error_t::ERR_INVALID_ARGUMENT;
        return false;
    }

    if ( poolDesc.nSlotCount == 0u ) {
        errorOut = mem_error_t::ERR_INVALID_CAPACITY;
        return false;
    }

    if ( !CypherMemory_IsPowerOfTwo( poolDesc.alignment ) ) {
        errorOut = mem_error_t::ERR_INVALID_ALIGNMENT;
        return false;
    }

    const common::usize nNodeAlignment = alignof( pool_free_node_t );
    const common::usize nNodeSize = sizeof( pool_free_node_t );
    const common::usize alignment = CypherMemory_PoolMax( poolDesc.alignment, nNodeAlignment );
    const common::usize nSlotPayloadSize = CypherMemory_PoolMax( poolDesc.nSlotSize, nNodeSize );

    common::usize nSlotStride = 0u;
    if ( !CypherMemory_PoolAlignForwardChecked( nSlotPayloadSize, alignment, nSlotStride ) ) {
        errorOut = mem_error_t::ERR_INTEGER_OVERFLOW;
        return false;
    }

    common::usize nSlotBytes = 0u;
    if ( !CypherMemory_PoolMulChecked( nSlotStride, poolDesc.nSlotCount, nSlotBytes ) ) {
        errorOut = mem_error_t::ERR_INTEGER_OVERFLOW;
        return false;
    }

    const common::usize bitsRounding = CYPHER_MEMORY_POOL_ALLOCATION_BITS_PER_WORD - 1u;
    common::usize nRoundedSlotCount = 0u;
    if ( !CypherMemory_PoolAddChecked( poolDesc.nSlotCount, bitsRounding, nRoundedSlotCount ) ) {
        errorOut = mem_error_t::ERR_INTEGER_OVERFLOW;
        return false;
    }

    const common::usize nAllocationWordCount = nRoundedSlotCount / CYPHER_MEMORY_POOL_ALLOCATION_BITS_PER_WORD;

    common::usize nMetadataBytes = 0u;
    if ( !CypherMemory_PoolMulChecked( nAllocationWordCount, sizeof( common::u64 ), nMetadataBytes ) ) {
        errorOut = mem_error_t::ERR_INTEGER_OVERFLOW;
        return false;
    }

    common::usize nMetadataOffset = 0u;
    if ( !CypherMemory_PoolAlignForwardChecked( nSlotBytes, alignof( common::u64 ), nMetadataOffset ) ) {
        errorOut = mem_error_t::ERR_INTEGER_OVERFLOW;
        return false;
    }

    common::usize nBackingBytes = 0u;
    if ( !CypherMemory_PoolAddChecked( nMetadataOffset, nMetadataBytes, nBackingBytes ) ) {
        errorOut = mem_error_t::ERR_INTEGER_OVERFLOW;
        return false;
    }

    layoutOut.nSlotSize = poolDesc.nSlotSize;
    layoutOut.nSlotStride = nSlotStride;
    layoutOut.nSlotCount = poolDesc.nSlotCount;
    layoutOut.alignment = alignment;
    layoutOut.nSlotBytes = nSlotBytes;
    layoutOut.nMetadataOffset = nMetadataOffset;
    layoutOut.nMetadataBytes = nMetadataBytes;
    layoutOut.nBackingBytes = nBackingBytes;
    layoutOut.nAllocationWordCount = nAllocationWordCount;

    return true;
}

void CypherMemory_PoolRecordOperationTrace( pool_t &pool,
                                            void *ptr,
                                            const common::usize nSlotIndex,
                                            const pool_operation_t operation,
                                            const mem_error_t error,
                                            const bool failed,
                                            const char *file,
                                            const char *function,
                                            const common::i32 line )
{
    const common::usize nTraceIndex = pool.nOperationTraceIndex % CYPHER_MEMORY_POOL_OPERATION_TRACE_COUNT;

    pool_operation_trace_t &trace = pool.pOperationTraces[nTraceIndex];
    trace.file = file;
    trace.function = function;
    trace.line = line;
    trace.ptr = ptr;
    trace.nSlotIndex = nSlotIndex;
    trace.operation = operation;
    trace.nOperationIndex = pool.nAllocationCount +
                            pool.nFreeOperationCount +
                            pool.nFailedAllocationCount +
                            pool.nFailedFreeCount;
    trace.error = error;
    trace.failed = failed;

    pool.nOperationTraceIndex = ( pool.nOperationTraceIndex + 1u ) % CYPHER_MEMORY_POOL_OPERATION_TRACE_COUNT;
    if ( pool.nOperationTraceCount < CYPHER_MEMORY_POOL_OPERATION_TRACE_COUNT ) {
        ++pool.nOperationTraceCount;
    }
}

common::byte *CypherMemory_PoolSlotPtr( const pool_t &pool, const common::usize nSlotIndex )
{
    return pool.base + ( nSlotIndex * pool.nSlotStride );
}

common::usize CypherMemory_PoolIndexUnchecked( const pool_t &pool, const void *ptr )
{
    const common::usize address = reinterpret_cast<common::usize>( ptr );
    const common::usize base = reinterpret_cast<common::usize>( pool.base );
    return ( address - base ) / pool.nSlotStride;
}

bool CypherMemory_PoolIsSlotAllocated( const pool_t &pool, const common::usize nSlotIndex )
{
    const common::usize nWordIndex = nSlotIndex / CYPHER_MEMORY_POOL_ALLOCATION_BITS_PER_WORD;
    const common::usize nBitIndex = nSlotIndex % CYPHER_MEMORY_POOL_ALLOCATION_BITS_PER_WORD;
    const common::u64 mask = common::u64{ 1u } << nBitIndex;

    return ( pool.allocationBits[nWordIndex] & mask ) != 0u;
}

void CypherMemory_PoolSetSlotAllocated( pool_t &pool, const common::usize nSlotIndex )
{
    const common::usize nWordIndex = nSlotIndex / CYPHER_MEMORY_POOL_ALLOCATION_BITS_PER_WORD;
    const common::usize nBitIndex = nSlotIndex % CYPHER_MEMORY_POOL_ALLOCATION_BITS_PER_WORD;
    const common::u64 mask = common::u64{ 1u } << nBitIndex;

    pool.allocationBits[nWordIndex] |= mask;
}

void CypherMemory_PoolClearSlotAllocated( pool_t &pool, const common::usize nSlotIndex )
{
    const common::usize nWordIndex = nSlotIndex / CYPHER_MEMORY_POOL_ALLOCATION_BITS_PER_WORD;
    const common::usize nBitIndex = nSlotIndex % CYPHER_MEMORY_POOL_ALLOCATION_BITS_PER_WORD;
    const common::u64 mask = common::u64{ 1u } << nBitIndex;

    pool.allocationBits[nWordIndex] &= ~mask;
}

void CypherMemory_PoolClearAllocationBits( pool_t &pool )
{
    if ( pool.allocationBits == nullptr || pool.nMetadataBytes == 0u ) {
        return;
    }

    std::memset( pool.allocationBits, 0, pool.nMetadataBytes );
}

void CypherMemory_PoolBuildFreeList( pool_t &pool )
{
    pool.freeList = nullptr;

    for ( common::usize nSlotIndex = pool.nSlotCount; nSlotIndex > 0u; --nSlotIndex ) {
        pool_free_node_t *node = reinterpret_cast<pool_free_node_t *>( CypherMemory_PoolSlotPtr( pool, nSlotIndex - 1u ) );
        node->next = pool.freeList;
        pool.freeList = node;
    }

    pool.nUsedCount = 0u;
    pool.nFreeCount = pool.nSlotCount;
}

void *CypherMemory_PoolAllocInternal( pool_t &pool,
                                      const common::usize nRequestedSize,
                                      const common::usize nRequestedAlignment,
                                      const bool pZeroMemory,
                                      const char *file,
                                      const char *function,
                                      const common::i32 line )
{
    if ( !pool.initialized ) {
        pool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        ++pool.nFailedAllocationCount;
        CypherMemory_PoolRecordOperationTrace( pool, nullptr, CYPHER_MEMORY_POOL_INVALID_SLOT_INDEX, pool_operation_t::POOL_OPERATION_ALLOC, pool.lastError, true, file, function, line );
        LOG_ERROR( log::channel_t::MEMORY, "pool allocation failed: pool is not initialized." );
        return nullptr;
    }

    if ( nRequestedSize == 0u ) {
        pool.lastError = mem_error_t::ERR_INVALID_ARGUMENT;
        ++pool.nFailedAllocationCount;
        CypherMemory_PoolRecordOperationTrace( pool, nullptr, CYPHER_MEMORY_POOL_INVALID_SLOT_INDEX, pool_operation_t::POOL_OPERATION_ALLOC, pool.lastError, true, file, function, line );
        LOG_ERROR( log::channel_t::MEMORY, "pool '%s' allocation failed: requested size is zero.", pool.name ? pool.name : "<unnamed>" );
        return nullptr;
    }

    if ( !CypherMemory_IsPowerOfTwo( nRequestedAlignment ) ) {
        pool.lastError = mem_error_t::ERR_INVALID_ALIGNMENT;
        ++pool.nFailedAllocationCount;
        CypherMemory_PoolRecordOperationTrace( pool, nullptr, CYPHER_MEMORY_POOL_INVALID_SLOT_INDEX, pool_operation_t::POOL_OPERATION_ALLOC, pool.lastError, true, file, function, line );
        LOG_ERROR( log::channel_t::MEMORY, "pool '%s' allocation failed: invalid alignment %zu.", pool.name ? pool.name : "<unnamed>", nRequestedAlignment );
        return nullptr;
    }

    if ( nRequestedSize > pool.nSlotSize ) {
        pool.lastError = mem_error_t::ERR_BUFFER_TOO_SMALL;
        ++pool.nFailedAllocationCount;
        CypherMemory_PoolRecordOperationTrace( pool, nullptr, CYPHER_MEMORY_POOL_INVALID_SLOT_INDEX, pool_operation_t::POOL_OPERATION_ALLOC, pool.lastError, true, file, function, line );
        LOG_ERROR( log::channel_t::MEMORY,
                          "pool '%s' allocation failed: requested=%zu, slot_size=%zu.",
                          pool.name ? pool.name : "<unnamed>",
                          nRequestedSize,
                          pool.nSlotSize );
        return nullptr;
    }

    if ( nRequestedAlignment > pool.alignment ) {
        pool.lastError = mem_error_t::ERR_INVALID_ALIGNMENT;
        ++pool.nFailedAllocationCount;
        CypherMemory_PoolRecordOperationTrace( pool, nullptr, CYPHER_MEMORY_POOL_INVALID_SLOT_INDEX, pool_operation_t::POOL_OPERATION_ALLOC, pool.lastError, true, file, function, line );
        LOG_ERROR( log::channel_t::MEMORY,
                          "pool '%s' allocation failed: requested alignment=%zu exceeds pool alignment=%zu.",
                          pool.name ? pool.name : "<unnamed>",
                          nRequestedAlignment,
                          pool.alignment );
        return nullptr;
    }

    if ( pool.freeList == nullptr || pool.nFreeCount == 0u ) {
        pool.lastError = mem_error_t::ERR_OUT_OF_MEMORY;
        ++pool.nFailedAllocationCount;
        CypherMemory_PoolRecordOperationTrace( pool, nullptr, CYPHER_MEMORY_POOL_INVALID_SLOT_INDEX, pool_operation_t::POOL_OPERATION_ALLOC, pool.lastError, true, file, function, line );
        LOG_ERROR( log::channel_t::MEMORY, "pool '%s' allocation failed: pool is full.", pool.name ? pool.name : "<unnamed>" );
        return nullptr;
    }

    pool_free_node_t *node = pool.freeList;
    pool.freeList = node->next;

    void *result = static_cast<void *>( node );
    const common::usize nSlotIndex = CypherMemory_PoolIndexUnchecked( pool, result );

    CypherMemory_PoolSetSlotAllocated( pool, nSlotIndex );

    ++pool.nUsedCount;
    --pool.nFreeCount;

    if ( pool.nUsedCount > pool.nPeakUsedCount ) {
        pool.nPeakUsedCount = pool.nUsedCount;
    }

    ++pool.nAllocationCount;
    pool.lastError = mem_error_t::OK;

    if ( pZeroMemory || ( pool.flags & CYPHER_MEMORY_POOL_FLAG_ZERO_ON_ALLOC ) != 0u ) {
        std::memset( result, 0, pool.nSlotStride );
    }

    CypherMemory_PoolRecordOperationTrace( pool, result, nSlotIndex, pool_operation_t::POOL_OPERATION_ALLOC, pool.lastError, false, file, function, line );

    return result;
}

}       // namespace

mem_error_t CypherMemory_PoolInit( pool_t &pool, const pool_desc_t &poolDesc )
{
    if ( pool.initialized ) {
        pool.lastError = mem_error_t::ERR_ALREADY_INITIALIZED;
        LOG_WARNING( log::channel_t::MEMORY, "pool '%s' is already initialized.", pool.name ? pool.name : "<unnamed>" );
        return mem_error_t::ERR_ALREADY_INITIALIZED;
    }

    pool_layout_t layout{};
    mem_error_t layoutError = mem_error_t::OK;
    if ( !CypherMemory_PoolComputeLayout( poolDesc, layout, layoutError ) ) {
        return CypherMemory_PoolFailInit( pool, poolDesc, layoutError, CypherMemory_ErrorDesc( layoutError ) );
    }

    void *memory = nullptr;

    switch ( poolDesc.backing ) {
    case pool_backing_t::POOL_ARENA:
        if ( poolDesc.arena == nullptr ) {
            return CypherMemory_PoolFailInit( pool, poolDesc, mem_error_t::ERR_INVALID_ARGUMENT, "arena pointer is required" );
        }

        if ( !CypherMemory_ArenaIsInitialized( *poolDesc.arena ) ) {
            return CypherMemory_PoolFailInit( pool, poolDesc, mem_error_t::ERR_NOT_INITIALIZED, "backing arena is not initialized" );
        }

        memory = CypherMemory_ArenaAlloc( *poolDesc.arena, layout.nBackingBytes, layout.alignment );
        if ( memory == nullptr ) {
            mem_error_t arenaError = CypherMemory_ArenaLastError( *poolDesc.arena );
            if ( arenaError == mem_error_t::OK ) {
                arenaError = mem_error_t::ERR_OUT_OF_MEMORY;
            }
            return CypherMemory_PoolFailInit( pool, poolDesc, arenaError, CypherMemory_ErrorDesc( arenaError ) );
        }
        break;

    case pool_backing_t::POOL_EXTERNAL_BUFFER:
        if ( poolDesc.pExternalBuffer == nullptr ) {
            return CypherMemory_PoolFailInit( pool, poolDesc, mem_error_t::ERR_EXTERNAL_BUFFER_REQUIRED, "external buffer is required" );
        }

        if ( poolDesc.nExternalBufferSize == 0u ) {
            return CypherMemory_PoolFailInit( pool, poolDesc, mem_error_t::ERR_BUFFER_TOO_SMALL, "external buffer size is zero" );
        }

        {
            const common::usize pExternalAddress = reinterpret_cast<common::usize>( poolDesc.pExternalBuffer );
            common::usize pAlignedAddress = 0u;

            if ( !CypherMemory_PoolAlignForwardChecked( pExternalAddress, layout.alignment, pAlignedAddress ) ) {
                return CypherMemory_PoolFailInit( pool, poolDesc, mem_error_t::ERR_INTEGER_OVERFLOW, "external buffer alignment overflow" );
            }

            const common::usize padding = pAlignedAddress - pExternalAddress;
            if ( padding > poolDesc.nExternalBufferSize ) {
                return CypherMemory_PoolFailInit( pool, poolDesc, mem_error_t::ERR_BUFFER_TOO_SMALL, "external buffer is too small after alignment padding" );
            }

            const common::usize nAvailableSize = poolDesc.nExternalBufferSize - padding;
            if ( nAvailableSize < layout.nBackingBytes ) {
                return CypherMemory_PoolFailInit( pool, poolDesc, mem_error_t::ERR_BUFFER_TOO_SMALL, "external buffer cannot fit pool slots and metadata" );
            }

            memory = reinterpret_cast<void *>( pAlignedAddress );
        }
        break;

    default:
        return CypherMemory_PoolFailInit( pool, poolDesc, mem_error_t::ERR_INVALID_ARGUMENT, "invalid backing type" );
    }

    pool = pool_t{};
    pool.name = poolDesc.name;
    pool.base = static_cast<common::byte *>( memory );
    pool.freeList = nullptr;
    pool.allocationBits = reinterpret_cast<common::u64 *>( pool.base + layout.nMetadataOffset );

    pool.nSlotSize = layout.nSlotSize;
    pool.nSlotStride = layout.nSlotStride;
    pool.nSlotCount = layout.nSlotCount;
    pool.alignment = layout.alignment;

    pool.nSlotBytes = layout.nSlotBytes;
    pool.nMetadataBytes = layout.nMetadataBytes;
    pool.nBackingBytes = layout.nBackingBytes;
    pool.nAllocationWordCount = layout.nAllocationWordCount;

    pool.nUsedCount = 0u;
    pool.nFreeCount = layout.nSlotCount;
    pool.nPeakUsedCount = 0u;

    pool.flags = poolDesc.flags;
    pool.backing = poolDesc.backing;
    pool.lastError = mem_error_t::OK;
    pool.initialized = true;

    CypherMemory_PoolClearAllocationBits( pool );
    CypherMemory_PoolBuildFreeList( pool );

    LOG_INFO( log::channel_t::MEMORY,
                     "pool '%s' initialized: slot_size=%zu, slot_stride=%zu, slot_count=%zu, backing_bytes=%zu.",
                     pool.name ? pool.name : "<unnamed>",
                     pool.nSlotSize,
                     pool.nSlotStride,
                     pool.nSlotCount,
                     pool.nBackingBytes );

    return mem_error_t::OK;
}

void CypherMemory_PoolShutdown( pool_t &pool )
{
    if ( !pool.initialized ) {
        return;
    }

    LOG_INFO( log::channel_t::MEMORY,
                     "pool '%s' shutdown: used=%zu, peak=%zu, allocations=%llu, frees=%llu, failed_alloc=%llu, failed_free=%llu.",
                     pool.name ? pool.name : "<unnamed>",
                     pool.nUsedCount,
                     pool.nPeakUsedCount,
                     static_cast<unsigned long long>( pool.nAllocationCount ),
                     static_cast<unsigned long long>( pool.nFreeOperationCount ),
                     static_cast<unsigned long long>( pool.nFailedAllocationCount ),
                     static_cast<unsigned long long>( pool.nFailedFreeCount ) );

    if ( ( pool.flags & CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_SHUTDOWN ) != 0u && pool.base != nullptr && pool.nBackingBytes > 0u ) {
        std::memset( pool.base, 0, pool.nBackingBytes );
    }

    pool = pool_t{};
}

pool_stats_t CypherMemory_PoolStats( const pool_t &pool )
{
    pool_stats_t stats{};

    stats.name = pool.name;
    stats.nSlotSize = pool.nSlotSize;
    stats.nSlotStride = pool.nSlotStride;
    stats.nSlotCount = pool.nSlotCount;
    stats.nUsedCount = pool.nUsedCount;
    stats.nFreeCount = pool.nFreeCount;
    stats.nPeakUsedCount = pool.nPeakUsedCount;
    stats.nSlotBytes = pool.nSlotBytes;
    stats.nMetadataBytes = pool.nMetadataBytes;
    stats.nBackingBytes = pool.nBackingBytes;
    stats.nAllocationCount = pool.nAllocationCount;
    stats.nFreeOperationCount = pool.nFreeOperationCount;
    stats.nFailedAllocationCount = pool.nFailedAllocationCount;
    stats.nFailedFreeCount = pool.nFailedFreeCount;

    return stats;
}

void CypherMemory_PoolResetCounters( pool_t &pool )
{
    if ( !pool.initialized ) {
        return;
    }

    pool.nPeakUsedCount = pool.nUsedCount;
    pool.nAllocationCount = 0u;
    pool.nFreeOperationCount = 0u;
    pool.nFailedAllocationCount = 0u;
    pool.nFailedFreeCount = 0u;
    pool.nOperationTraceIndex = 0u;
    pool.nOperationTraceCount = 0u;
    pool.lastError = mem_error_t::OK;
}

void CypherMemory_PoolReset( pool_t &pool )
{
    if ( !pool.initialized ) {
        return;
    }

    if ( ( pool.flags & CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_RESET ) != 0u && pool.base != nullptr && pool.nSlotBytes > 0u ) {
        std::memset( pool.base, 0, pool.nSlotBytes );
    }

    CypherMemory_PoolClearAllocationBits( pool );
    CypherMemory_PoolBuildFreeList( pool );

    pool.lastError = mem_error_t::OK;
    CypherMemory_PoolRecordOperationTrace( pool, nullptr, CYPHER_MEMORY_POOL_INVALID_SLOT_INDEX, pool_operation_t::POOL_OPERATION_RESET, pool.lastError, false, nullptr, nullptr, 0 );
}

void *CypherMemory_PoolAlloc( pool_t &pool )
{
    return CypherMemory_PoolAllocDebug( pool, nullptr, nullptr, 0 );
}

void *CypherMemory_PoolAllocDebug( pool_t &pool, const char *file, const char *function, common::i32 line )
{
    return CypherMemory_PoolAllocInternal( pool, pool.nSlotSize, pool.alignment, false, file, function, line );
}

void *CypherMemory_PoolAllocZero( pool_t &pool )
{
    return CypherMemory_PoolAllocZeroDebug( pool, nullptr, nullptr, 0 );
}

void *CypherMemory_PoolAllocZeroDebug( pool_t &pool, const char *file, const char *function, common::i32 line )
{
    return CypherMemory_PoolAllocInternal( pool, pool.nSlotSize, pool.alignment, true, file, function, line );
}

void *CypherMemory_PoolAllocSize( pool_t &pool, common::usize size, common::usize alignment )
{
    return CypherMemory_PoolAllocSizeDebug( pool, size, alignment, nullptr, nullptr, 0 );
}

void *CypherMemory_PoolAllocSizeDebug( pool_t &pool,
                                       common::usize size,
                                       common::usize alignment,
                                       const char *file,
                                       const char *function,
                                       common::i32 line )
{
    return CypherMemory_PoolAllocInternal( pool, size, alignment, false, file, function, line );
}

void *CypherMemory_PoolAllocSizeZero( pool_t &pool, common::usize size, common::usize alignment )
{
    return CypherMemory_PoolAllocSizeZeroDebug( pool, size, alignment, nullptr, nullptr, 0 );
}

void *CypherMemory_PoolAllocSizeZeroDebug( pool_t &pool,
                                           common::usize size,
                                           common::usize alignment,
                                           const char *file,
                                           const char *function,
                                           common::i32 line )
{
    return CypherMemory_PoolAllocInternal( pool, size, alignment, true, file, function, line );
}

mem_error_t CypherMemory_PoolFree( pool_t &pool, void *ptr )
{
    return CypherMemory_PoolFreeDebug( pool, ptr, nullptr, nullptr, 0 );
}

mem_error_t CypherMemory_PoolFreeDebug( pool_t &pool, void *ptr, const char *file, const char *function, common::i32 line )
{
    if ( !pool.initialized ) {
        pool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        ++pool.nFailedFreeCount;
        CypherMemory_PoolRecordOperationTrace( pool, ptr, CYPHER_MEMORY_POOL_INVALID_SLOT_INDEX, pool_operation_t::POOL_OPERATION_FREE, pool.lastError, true, file, function, line );
        LOG_ERROR( log::channel_t::MEMORY, "pool free failed: pool is not initialized." );
        return pool.lastError;
    }

    if ( ptr == nullptr ) {
        pool.lastError = mem_error_t::ERR_INVALID_POINTER;
        ++pool.nFailedFreeCount;
        CypherMemory_PoolRecordOperationTrace( pool, ptr, CYPHER_MEMORY_POOL_INVALID_SLOT_INDEX, pool_operation_t::POOL_OPERATION_FREE, pool.lastError, true, file, function, line );
        LOG_ERROR( log::channel_t::MEMORY, "pool '%s' free failed: pointer is null.", pool.name ? pool.name : "<unnamed>" );
        return pool.lastError;
    }

    if ( !CypherMemory_PoolOwnsSlot( pool, ptr ) ) {
        pool.lastError = mem_error_t::ERR_INVALID_POINTER;
        ++pool.nFailedFreeCount;
        CypherMemory_PoolRecordOperationTrace( pool, ptr, CYPHER_MEMORY_POOL_INVALID_SLOT_INDEX, pool_operation_t::POOL_OPERATION_FREE, pool.lastError, true, file, function, line );
        LOG_ERROR( log::channel_t::MEMORY, "pool '%s' free failed: pointer does not belong to a pool slot.", pool.name ? pool.name : "<unnamed>" );
        return pool.lastError;
    }

    const common::usize nSlotIndex = CypherMemory_PoolIndexUnchecked( pool, ptr );

    if ( !CypherMemory_PoolIsSlotAllocated( pool, nSlotIndex ) ) {
        pool.lastError = mem_error_t::ERR_DOUBLE_FREE;
        ++pool.nFailedFreeCount;
        CypherMemory_PoolRecordOperationTrace( pool, ptr, nSlotIndex, pool_operation_t::POOL_OPERATION_FREE, pool.lastError, true, file, function, line );
        LOG_ERROR( log::channel_t::MEMORY, "pool '%s' free failed: slot %zu is already free.", pool.name ? pool.name : "<unnamed>", nSlotIndex );
        return pool.lastError;
    }

    if ( ( pool.flags & CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_FREE ) != 0u ) {
        std::memset( ptr, 0, pool.nSlotStride );
    }

    pool_free_node_t *node = static_cast<pool_free_node_t *>( ptr );
    node->next = pool.freeList;
    pool.freeList = node;

    CypherMemory_PoolClearSlotAllocated( pool, nSlotIndex );

    --pool.nUsedCount;
    ++pool.nFreeCount;
    ++pool.nFreeOperationCount;
    pool.lastError = mem_error_t::OK;

    CypherMemory_PoolRecordOperationTrace( pool, ptr, nSlotIndex, pool_operation_t::POOL_OPERATION_FREE, pool.lastError, false, file, function, line );

    return pool.lastError;
}

bool CypherMemory_PoolContains( const pool_t &pool, const void *ptr )
{
    if ( !pool.initialized || pool.base == nullptr || ptr == nullptr ) {
        return false;
    }

    const common::usize address = reinterpret_cast<common::usize>( ptr );
    const common::usize base = reinterpret_cast<common::usize>( pool.base );

    if ( address < base ) {
        return false;
    }

    return ( address - base ) < pool.nSlotBytes;
}

bool CypherMemory_PoolOwnsSlot( const pool_t &pool, const void *ptr )
{
    if ( !CypherMemory_PoolContains( pool, ptr ) || pool.nSlotStride == 0u ) {
        return false;
    }

    const common::usize address = reinterpret_cast<common::usize>( ptr );
    const common::usize base = reinterpret_cast<common::usize>( pool.base );
    const common::usize offset = address - base;

    return ( offset % pool.nSlotStride ) == 0u;
}

bool CypherMemory_PoolIsInitialized( const pool_t &pool )
{
    return pool.initialized;
}

mem_error_t CypherMemory_PoolLastError( const pool_t &pool )
{
    return pool.lastError;
}

common::usize CypherMemory_PoolSlotIndex( const pool_t &pool, const void *ptr )
{
    if ( !CypherMemory_PoolOwnsSlot( pool, ptr ) ) {
        return CYPHER_MEMORY_POOL_INVALID_SLOT_INDEX;
    }

    return CypherMemory_PoolIndexUnchecked( pool, ptr );
}

common::usize CypherMemory_PoolUsedCount( const pool_t &pool )
{
    return pool.nUsedCount;
}

common::usize CypherMemory_PoolFreeCount( const pool_t &pool )
{
    return pool.nFreeCount;
}

common::usize CypherMemory_PoolCapacity( const pool_t &pool )
{
    return pool.nSlotCount;
}

common::f32 CypherMemory_PoolUsageRatio( const pool_t &pool )
{
    if ( pool.nSlotCount == 0u ) {
        return 0.0f;
    }

    return static_cast<common::f32>( pool.nUsedCount ) / static_cast<common::f32>( pool.nSlotCount );
}

const pool_operation_trace_t *CypherMemory_PoolOperationTraces( const pool_t &pool, common::usize &nOutCount )
{
    nOutCount = pool.nOperationTraceCount;
    return pool.pOperationTraces;
}

}       // namespace cypher::engine::memory
