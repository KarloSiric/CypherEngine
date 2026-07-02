//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMemory/CypherMemory_Bucket.cpp
//  Purpose: Implements the CypherMemory Memory Bucket module.
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

#include "CypherMemory_Bucket.h"
#include "CypherLog.h"

#include <cstring>
#include <limits>

namespace cypher::engine::memory
{

namespace {

constexpr common::usize CYPHER_MEMORY_BUCKET_INVALID_CLASS_INDEX = std::numeric_limits<common::usize>::max();

common::u32 CypherMemory_BucketPoolFlags( const common::u32 nBucketFlags )
{
    common::u32 nPoolFlags = CYPHER_MEMORY_POOL_FLAG_NONE;

    if ( ( nBucketFlags & CYPHER_MEMORY_BUCKET_FLAG_ZERO_ON_ALLOC ) != 0u ) {
        nPoolFlags |= CYPHER_MEMORY_POOL_FLAG_ZERO_ON_ALLOC;
    }

    if ( ( nBucketFlags & CYPHER_MEMORY_BUCKET_FLAG_CLEAR_ON_FREE ) != 0u ) {
        nPoolFlags |= CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_FREE;
    }

    if ( ( nBucketFlags & CYPHER_MEMORY_BUCKET_FLAG_CLEAR_ON_RESET ) != 0u ) {
        nPoolFlags |= CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_RESET;
    }

    if ( ( nBucketFlags & CYPHER_MEMORY_BUCKET_FLAG_CLEAR_ON_SHUTDOWN ) != 0u ) {
        nPoolFlags |= CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_SHUTDOWN;
    }

    return nPoolFlags;
}

mem_error_t CypherMemory_BucketFailInit( bucket_t &bucket,
                                          const bucket_desc_t &bucketDesc,
                                          const mem_error_t error,
                                          const char *reason )
{
    bucket.name = bucketDesc.name;
    bucket.lastError = error;

    LOG_ERROR( log::channel_t::MEMORY,
                      "bucket '%s' init failed: %s.",
                      bucket.name ? bucket.name : "<unnamed>",
                      reason ? reason : CypherMemory_ErrorDesc( error ) );

    return error;
}

common::usize CypherMemory_BucketFindBestClass( const bucket_t &bucket,
                                                const common::usize size,
                                                const common::usize alignment,
                                                const bool nRequireFreeSlot )
{
    common::usize nBestIndex = CYPHER_MEMORY_BUCKET_INVALID_CLASS_INDEX;
    common::usize nBestSlotSize = std::numeric_limits<common::usize>::max();

    for ( common::usize nClassIndex = 0u; nClassIndex < bucket.nClassCount; ++nClassIndex ) {
        const bucket_class_t &bucketClass = bucket.classes[nClassIndex];

        if ( bucketClass.nSlotSize < size ) {
            continue;
        }

        if ( bucketClass.pool.alignment < alignment ) {
            continue;
        }

        if ( nRequireFreeSlot && CypherMemory_PoolFreeCount( bucketClass.pool ) == 0u ) {
            continue;
        }

        if ( bucketClass.nSlotSize < nBestSlotSize ) {
            nBestIndex = nClassIndex;
            nBestSlotSize = bucketClass.nSlotSize;
        }
    }

    return nBestIndex;
}

bool CypherMemory_BucketHasCompatibleClass( const bucket_t &bucket,
                                            const common::usize size,
                                            const common::usize alignment )
{
    return CypherMemory_BucketFindBestClass( bucket, size, alignment, false ) != CYPHER_MEMORY_BUCKET_INVALID_CLASS_INDEX;
}

void CypherMemory_BucketRefreshPeak( bucket_t &bucket )
{
    const common::usize nUsedCount = CypherMemory_BucketUsedCount( bucket );

    if ( nUsedCount > bucket.nPeakUsedCount ) {
        bucket.nPeakUsedCount = nUsedCount;
    }
}

void *CypherMemory_BucketFailAlloc( bucket_t &bucket,
                                    const mem_error_t error,
                                    const char *reason )
{
    bucket.lastError = error;
    ++bucket.nFailedAllocationCount;

    LOG_ERROR( log::channel_t::MEMORY,
                      "bucket '%s' allocation failed: %s.",
                      bucket.name ? bucket.name : "<unnamed>",
                      reason ? reason : CypherMemory_ErrorDesc( error ) );

    return nullptr;
}

}       // namespace

bucket_desc_t CypherMemory_BucketDefaultDesc( arena_t &arena, const char *name )
{
    bucket_desc_t desc{};

    desc.name = name;
    desc.arena = &arena;
    desc.alignment = 16u;
    desc.nClassCount = CYPHER_MEMORY_BUCKET_DEFAULT_CLASS_COUNT;
    desc.flags = CYPHER_MEMORY_BUCKET_FLAG_NONE;

    desc.classes[0] = bucket_class_desc_t{ 16u, 1024u };
    desc.classes[1] = bucket_class_desc_t{ 32u, 1024u };
    desc.classes[2] = bucket_class_desc_t{ 64u, 1024u };
    desc.classes[3] = bucket_class_desc_t{ 128u, 512u };
    desc.classes[4] = bucket_class_desc_t{ 256u, 256u };
    desc.classes[5] = bucket_class_desc_t{ 512u, 128u };
    desc.classes[6] = bucket_class_desc_t{ 1024u, 64u };
    desc.classes[7] = bucket_class_desc_t{ 2048u, 32u };
    desc.classes[8] = bucket_class_desc_t{ 4096u, 16u };
    desc.classes[9] = bucket_class_desc_t{ 8192u, 8u };

    return desc;
}

mem_error_t CypherMemory_BucketInit( bucket_t &bucket, const bucket_desc_t &bucketDesc )
{
    if ( bucket.initialized ) {
        bucket.lastError = mem_error_t::ERR_ALREADY_INITIALIZED;
        LOG_WARNING( log::channel_t::MEMORY, "bucket '%s' is already initialized.", bucket.name ? bucket.name : "<unnamed>" );
        return bucket.lastError;
    }

    if ( bucketDesc.arena == nullptr ) {
        return CypherMemory_BucketFailInit( bucket, bucketDesc, mem_error_t::ERR_INVALID_ARGUMENT, "arena pointer is required" );
    }

    if ( !CypherMemory_ArenaIsInitialized( *bucketDesc.arena ) ) {
        return CypherMemory_BucketFailInit( bucket, bucketDesc, mem_error_t::ERR_NOT_INITIALIZED, "backing arena is not initialized" );
    }

    if ( bucketDesc.nClassCount == 0u || bucketDesc.nClassCount > CYPHER_MEMORY_BUCKET_MAX_CLASSES ) {
        return CypherMemory_BucketFailInit( bucket, bucketDesc, mem_error_t::ERR_INVALID_CAPACITY, "invalid bucket class count" );
    }

    if ( !CypherMemory_IsPowerOfTwo( bucketDesc.alignment ) ) {
        return CypherMemory_BucketFailInit( bucket, bucketDesc, mem_error_t::ERR_INVALID_ALIGNMENT, "invalid bucket alignment" );
    }

    const arena_marker_t initMarker = CypherMemory_ArenaGetMarker( *bucketDesc.arena );

    bucket = bucket_t{};
    bucket.name = bucketDesc.name;
    bucket.arena = bucketDesc.arena;
    bucket.nClassCount = bucketDesc.nClassCount;
    bucket.alignment = bucketDesc.alignment;
    bucket.flags = bucketDesc.flags;

    const common::u32 nPoolFlags = CypherMemory_BucketPoolFlags( bucketDesc.flags );

    for ( common::usize nClassIndex = 0u; nClassIndex < bucketDesc.nClassCount; ++nClassIndex ) {
        const bucket_class_desc_t &classDesc = bucketDesc.classes[nClassIndex];

        if ( classDesc.nSlotSize == 0u || classDesc.nSlotCount == 0u ) {
            for ( common::usize nShutdownIndex = 0u; nShutdownIndex < nClassIndex; ++nShutdownIndex ) {
                CypherMemory_PoolShutdown( bucket.classes[nShutdownIndex].pool );
            }
            CypherMemory_ArenaRewind( *bucketDesc.arena, initMarker );
            bucket = bucket_t{};
            return CypherMemory_BucketFailInit( bucket, bucketDesc, mem_error_t::ERR_INVALID_CAPACITY, "invalid bucket class configuration" );
        }

        bucket_class_t &bucketClass = bucket.classes[nClassIndex];
        bucketClass.nSlotSize = classDesc.nSlotSize;
        bucketClass.nSlotCount = classDesc.nSlotCount;

        pool_desc_t poolDesc{};
        poolDesc.name = bucketDesc.name;
        poolDesc.arena = bucketDesc.arena;
        poolDesc.nSlotSize = classDesc.nSlotSize;
        poolDesc.nSlotCount = classDesc.nSlotCount;
        poolDesc.alignment = bucketDesc.alignment;
        poolDesc.flags = nPoolFlags;
        poolDesc.backing = pool_backing_t::POOL_ARENA;

        const mem_error_t poolResult = CypherMemory_PoolInit( bucketClass.pool, poolDesc );
        if ( poolResult != mem_error_t::OK ) {
            for ( common::usize nShutdownIndex = 0u; nShutdownIndex < nClassIndex; ++nShutdownIndex ) {
                CypherMemory_PoolShutdown( bucket.classes[nShutdownIndex].pool );
            }
            CypherMemory_ArenaRewind( *bucketDesc.arena, initMarker );
            bucket = bucket_t{};
            return CypherMemory_BucketFailInit( bucket, bucketDesc, poolResult, CypherMemory_ErrorDesc( poolResult ) );
        }
    }

    bucket.initialized = true;
    bucket.lastError = mem_error_t::OK;

    LOG_INFO( log::channel_t::MEMORY,
                     "bucket '%s' initialized: classes=%zu, alignment=%zu, backing_bytes=%zu.",
                     bucket.name ? bucket.name : "<unnamed>",
                     bucket.nClassCount,
                     bucket.alignment,
                     CypherMemory_BucketStats( bucket ).nBackingBytes );

    return mem_error_t::OK;
}

void CypherMemory_BucketShutdown( bucket_t &bucket )
{
    if ( !bucket.initialized ) {
        return;
    }

    LOG_INFO( log::channel_t::MEMORY,
                     "bucket '%s' shutdown: used=%zu, peak=%zu, allocations=%llu, frees=%llu, failed_alloc=%llu, failed_free=%llu.",
                     bucket.name ? bucket.name : "<unnamed>",
                     CypherMemory_BucketUsedCount( bucket ),
                     bucket.nPeakUsedCount,
                     static_cast<unsigned long long>( bucket.nAllocationCount ),
                     static_cast<unsigned long long>( bucket.nFreeOperationCount ),
                     static_cast<unsigned long long>( bucket.nFailedAllocationCount ),
                     static_cast<unsigned long long>( bucket.nFailedFreeCount ) );

    for ( common::usize nClassIndex = 0u; nClassIndex < bucket.nClassCount; ++nClassIndex ) {
        CypherMemory_PoolShutdown( bucket.classes[nClassIndex].pool );
    }

    bucket = bucket_t{};
}

void CypherMemory_BucketReset( bucket_t &bucket )
{
    if ( !bucket.initialized ) {
        return;
    }

    for ( common::usize nClassIndex = 0u; nClassIndex < bucket.nClassCount; ++nClassIndex ) {
        CypherMemory_PoolReset( bucket.classes[nClassIndex].pool );
    }

    bucket.lastError = mem_error_t::OK;
}

void CypherMemory_BucketResetCounters( bucket_t &bucket )
{
    if ( !bucket.initialized ) {
        return;
    }

    bucket.nPeakUsedCount = CypherMemory_BucketUsedCount( bucket );
    bucket.nAllocationCount = 0u;
    bucket.nFreeOperationCount = 0u;
    bucket.nFailedAllocationCount = 0u;
    bucket.nFailedFreeCount = 0u;
    bucket.lastError = mem_error_t::OK;

    for ( common::usize nClassIndex = 0u; nClassIndex < bucket.nClassCount; ++nClassIndex ) {
        CypherMemory_PoolResetCounters( bucket.classes[nClassIndex].pool );
    }
}

bucket_stats_t CypherMemory_BucketStats( const bucket_t &bucket )
{
    bucket_stats_t stats{};

    stats.name = bucket.name;
    stats.nClassCount = bucket.nClassCount;
    stats.nPeakUsedCount = bucket.nPeakUsedCount;
    stats.nAllocationCount = bucket.nAllocationCount;
    stats.nFreeOperationCount = bucket.nFreeOperationCount;
    stats.nFailedAllocationCount = bucket.nFailedAllocationCount;
    stats.nFailedFreeCount = bucket.nFailedFreeCount;

    for ( common::usize nClassIndex = 0u; nClassIndex < bucket.nClassCount; ++nClassIndex ) {
        const pool_stats_t poolStats = CypherMemory_PoolStats( bucket.classes[nClassIndex].pool );
        bucket_class_stats_t &classStats = stats.classStats[nClassIndex];

        classStats.nSlotSize = poolStats.nSlotSize;
        classStats.nSlotCount = poolStats.nSlotCount;
        classStats.nUsedCount = poolStats.nUsedCount;
        classStats.nFreeCount = poolStats.nFreeCount;
        classStats.nPeakUsedCount = poolStats.nPeakUsedCount;
        classStats.nBackingBytes = poolStats.nBackingBytes;

        stats.nUsedCount += poolStats.nUsedCount;
        stats.nFreeCount += poolStats.nFreeCount;
        stats.nBackingBytes += poolStats.nBackingBytes;
    }

    return stats;
}

void *CypherMemory_BucketAlloc( bucket_t &bucket, common::usize size, common::usize alignment )
{
    return CypherMemory_BucketAllocDebug( bucket, size, alignment, nullptr, nullptr, 0 );
}

void *CypherMemory_BucketAllocDebug( bucket_t &bucket,
                                     common::usize size,
                                     common::usize alignment,
                                     const char *file,
                                     const char *function,
                                     common::i32 line )
{
    if ( !bucket.initialized ) {
        return CypherMemory_BucketFailAlloc( bucket, mem_error_t::ERR_NOT_INITIALIZED, "bucket is not initialized" );
    }

    if ( size == 0u ) {
        return CypherMemory_BucketFailAlloc( bucket, mem_error_t::ERR_INVALID_ARGUMENT, "requested size is zero" );
    }

    if ( !CypherMemory_IsPowerOfTwo( alignment ) ) {
        return CypherMemory_BucketFailAlloc( bucket, mem_error_t::ERR_INVALID_ALIGNMENT, "requested alignment is invalid" );
    }

    if ( !CypherMemory_BucketHasCompatibleClass( bucket, size, alignment ) ) {
        return CypherMemory_BucketFailAlloc( bucket, mem_error_t::ERR_BUFFER_TOO_SMALL, "no bucket class can satisfy the request" );
    }

    const common::usize nClassIndex = CypherMemory_BucketFindBestClass( bucket, size, alignment, true );
    if ( nClassIndex == CYPHER_MEMORY_BUCKET_INVALID_CLASS_INDEX ) {
        return CypherMemory_BucketFailAlloc( bucket, mem_error_t::ERR_OUT_OF_MEMORY, "all compatible bucket classes are full" );
    }

    void *memory = CypherMemory_PoolAllocSizeDebug( bucket.classes[nClassIndex].pool, size, alignment, file, function, line );
    bucket.lastError = CypherMemory_PoolLastError( bucket.classes[nClassIndex].pool );

    if ( memory == nullptr ) {
        ++bucket.nFailedAllocationCount;
        return nullptr;
    }

    ++bucket.nAllocationCount;
    CypherMemory_BucketRefreshPeak( bucket );

    return memory;
}

void *CypherMemory_BucketAllocZero( bucket_t &bucket, common::usize size, common::usize alignment )
{
    return CypherMemory_BucketAllocZeroDebug( bucket, size, alignment, nullptr, nullptr, 0 );
}

void *CypherMemory_BucketAllocZeroDebug( bucket_t &bucket,
                                         common::usize size,
                                         common::usize alignment,
                                         const char *file,
                                         const char *function,
                                         common::i32 line )
{
    void *memory = CypherMemory_BucketAllocDebug( bucket, size, alignment, file, function, line );

    if ( memory == nullptr ) {
        return nullptr;
    }

    for ( common::usize nClassIndex = 0u; nClassIndex < bucket.nClassCount; ++nClassIndex ) {
        const pool_t &pool = bucket.classes[nClassIndex].pool;
        if ( CypherMemory_PoolOwnsSlot( pool, memory ) ) {
            std::memset( memory, 0, pool.nSlotStride );
            break;
        }
    }

    return memory;
}

mem_error_t CypherMemory_BucketFree( bucket_t &bucket, void *ptr )
{
    return CypherMemory_BucketFreeDebug( bucket, ptr, nullptr, nullptr, 0 );
}

mem_error_t CypherMemory_BucketFreeDebug( bucket_t &bucket, void *ptr, const char *file, const char *function, common::i32 line )
{
    if ( !bucket.initialized ) {
        bucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        ++bucket.nFailedFreeCount;
        return bucket.lastError;
    }

    if ( ptr == nullptr ) {
        bucket.lastError = mem_error_t::ERR_INVALID_POINTER;
        ++bucket.nFailedFreeCount;
        return bucket.lastError;
    }

    for ( common::usize nClassIndex = 0u; nClassIndex < bucket.nClassCount; ++nClassIndex ) {
        pool_t &pool = bucket.classes[nClassIndex].pool;
        if ( !CypherMemory_PoolOwnsSlot( pool, ptr ) ) {
            continue;
        }

        const mem_error_t freeResult = CypherMemory_PoolFreeDebug( pool, ptr, file, function, line );
        bucket.lastError = freeResult;

        if ( freeResult == mem_error_t::OK ) {
            ++bucket.nFreeOperationCount;
        } else {
            ++bucket.nFailedFreeCount;
        }

        return freeResult;
    }

    bucket.lastError = mem_error_t::ERR_INVALID_POINTER;
    ++bucket.nFailedFreeCount;
    LOG_ERROR( log::channel_t::MEMORY,
                      "bucket '%s' free failed: pointer does not belong to any bucket class.",
                      bucket.name ? bucket.name : "<unnamed>" );

    return bucket.lastError;
}

bool CypherMemory_BucketContains( const bucket_t &bucket, const void *ptr )
{
    if ( !bucket.initialized || ptr == nullptr ) {
        return false;
    }

    for ( common::usize nClassIndex = 0u; nClassIndex < bucket.nClassCount; ++nClassIndex ) {
        if ( CypherMemory_PoolContains( bucket.classes[nClassIndex].pool, ptr ) ) {
            return true;
        }
    }

    return false;
}

bool CypherMemory_BucketOwnsSlot( const bucket_t &bucket, const void *ptr )
{
    if ( !bucket.initialized || ptr == nullptr ) {
        return false;
    }

    for ( common::usize nClassIndex = 0u; nClassIndex < bucket.nClassCount; ++nClassIndex ) {
        if ( CypherMemory_PoolOwnsSlot( bucket.classes[nClassIndex].pool, ptr ) ) {
            return true;
        }
    }

    return false;
}

bool CypherMemory_BucketIsInitialized( const bucket_t &bucket )
{
    return bucket.initialized;
}

mem_error_t CypherMemory_BucketLastError( const bucket_t &bucket )
{
    return bucket.lastError;
}

common::usize CypherMemory_BucketClassIndexForSize( const bucket_t &bucket, common::usize size, common::usize alignment )
{
    if ( !bucket.initialized || size == 0u || !CypherMemory_IsPowerOfTwo( alignment ) ) {
        return CYPHER_MEMORY_BUCKET_INVALID_CLASS_INDEX;
    }

    return CypherMemory_BucketFindBestClass( bucket, size, alignment, false );
}

common::usize CypherMemory_BucketUsedCount( const bucket_t &bucket )
{
    common::usize nUsedCount = 0u;

    for ( common::usize nClassIndex = 0u; nClassIndex < bucket.nClassCount; ++nClassIndex ) {
        nUsedCount += CypherMemory_PoolUsedCount( bucket.classes[nClassIndex].pool );
    }

    return nUsedCount;
}

common::usize CypherMemory_BucketFreeCount( const bucket_t &bucket )
{
    common::usize nFreeCount = 0u;

    for ( common::usize nClassIndex = 0u; nClassIndex < bucket.nClassCount; ++nClassIndex ) {
        nFreeCount += CypherMemory_PoolFreeCount( bucket.classes[nClassIndex].pool );
    }

    return nFreeCount;
}

common::f32 CypherMemory_BucketUsageRatio( const bucket_t &bucket )
{
    const common::usize nUsedCount = CypherMemory_BucketUsedCount( bucket );
    const common::usize nFreeCount = CypherMemory_BucketFreeCount( bucket );
    const common::usize nTotalCount = nUsedCount + nFreeCount;

    if ( nTotalCount == 0u ) {
        return 0.0f;
    }

    return static_cast<common::f32>( nUsedCount ) / static_cast<common::f32>( nTotalCount );
}

}       // namespace cypher::engine::memory
