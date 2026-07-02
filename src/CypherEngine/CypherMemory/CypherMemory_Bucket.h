//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherMemory/CypherMemory_Bucket.h
//  Purpose: Declares the CypherMemory Memory Bucket module.
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

#ifndef CYPHER_ENGINE_MEMORY_BUCKET_H
#define CYPHER_ENGINE_MEMORY_BUCKET_H

#pragma once

#include "CypherCommon.h"
#include "CypherMemory_Pool.h"

namespace cypher::engine::memory
{

constexpr common::usize CYPHER_MEMORY_BUCKET_MAX_CLASSES = 16u;
constexpr common::usize CYPHER_MEMORY_BUCKET_DEFAULT_CLASS_COUNT = 10u;

constexpr common::u32 CYPHER_MEMORY_BUCKET_FLAG_NONE              = 0u;
constexpr common::u32 CYPHER_MEMORY_BUCKET_FLAG_ZERO_ON_ALLOC     = 1u << 0u;
constexpr common::u32 CYPHER_MEMORY_BUCKET_FLAG_CLEAR_ON_FREE     = 1u << 1u;
constexpr common::u32 CYPHER_MEMORY_BUCKET_FLAG_CLEAR_ON_RESET    = 1u << 2u;
constexpr common::u32 CYPHER_MEMORY_BUCKET_FLAG_CLEAR_ON_SHUTDOWN = 1u << 3u;

struct bucket_class_desc_t {
    common::usize nSlotSize{ 0u };
    common::usize nSlotCount{ 0u };
};

struct bucket_desc_t {
    const char *name{ nullptr };
    arena_t *arena{ nullptr };

    common::usize alignment{ CYPHER_MEMORY_DEFAULT_ALIGNMENT };
    common::usize nClassCount{ 0u };
    bucket_class_desc_t classes[CYPHER_MEMORY_BUCKET_MAX_CLASSES]{};

    common::u32 flags{ CYPHER_MEMORY_BUCKET_FLAG_NONE };
};

struct bucket_class_t {
    pool_t pool{};
    common::usize nSlotSize{ 0u };
    common::usize nSlotCount{ 0u };
};

struct bucket_class_stats_t {
    common::usize nSlotSize{ 0u };
    common::usize nSlotCount{ 0u };
    common::usize nUsedCount{ 0u };
    common::usize nFreeCount{ 0u };
    common::usize nPeakUsedCount{ 0u };
    common::usize nBackingBytes{ 0u };
};

struct bucket_stats_t {
    const char *name{ nullptr };

    common::usize nClassCount{ 0u };
    common::usize nUsedCount{ 0u };
    common::usize nFreeCount{ 0u };
    common::usize nPeakUsedCount{ 0u };
    common::usize nBackingBytes{ 0u };

    common::u64 nAllocationCount{ 0u };
    common::u64 nFreeOperationCount{ 0u };
    common::u64 nFailedAllocationCount{ 0u };
    common::u64 nFailedFreeCount{ 0u };

    bucket_class_stats_t classStats[CYPHER_MEMORY_BUCKET_MAX_CLASSES]{};
};

struct bucket_t {
    const char *name{ nullptr };
    arena_t *arena{ nullptr };

    bucket_class_t classes[CYPHER_MEMORY_BUCKET_MAX_CLASSES]{};
    common::usize nClassCount{ 0u };
    common::usize alignment{ CYPHER_MEMORY_DEFAULT_ALIGNMENT };

    common::usize nPeakUsedCount{ 0u };

    common::u64 nAllocationCount{ 0u };
    common::u64 nFreeOperationCount{ 0u };
    common::u64 nFailedAllocationCount{ 0u };
    common::u64 nFailedFreeCount{ 0u };

    common::u32 flags{ CYPHER_MEMORY_BUCKET_FLAG_NONE };
    mem_error_t lastError{ mem_error_t::OK };
    bool initialized{ false };
};

using bucket_allocator_t = bucket_t;

bucket_desc_t CypherMemory_BucketDefaultDesc( arena_t &arena, const char *name = nullptr );

mem_error_t CypherMemory_BucketInit( bucket_t &bucket, const bucket_desc_t &bucketDesc );

void CypherMemory_BucketShutdown( bucket_t &bucket );

void CypherMemory_BucketReset( bucket_t &bucket );

void CypherMemory_BucketResetCounters( bucket_t &bucket );

bucket_stats_t CypherMemory_BucketStats( const bucket_t &bucket );

void *CypherMemory_BucketAlloc( bucket_t &bucket, common::usize size, common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *CypherMemory_BucketAllocDebug( bucket_t &bucket,
                                     common::usize size,
                                     common::usize alignment,
                                     const char *file,
                                     const char *function,
                                     common::i32 line );

void *CypherMemory_BucketAllocZero( bucket_t &bucket, common::usize size, common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *CypherMemory_BucketAllocZeroDebug( bucket_t &bucket,
                                         common::usize size,
                                         common::usize alignment,
                                         const char *file,
                                         const char *function,
                                         common::i32 line );

mem_error_t CypherMemory_BucketFree( bucket_t &bucket, void *ptr );

mem_error_t CypherMemory_BucketFreeDebug( bucket_t &bucket, void *ptr, const char *file, const char *function, common::i32 line );

bool CypherMemory_BucketContains( const bucket_t &bucket, const void *ptr );

bool CypherMemory_BucketOwnsSlot( const bucket_t &bucket, const void *ptr );

bool CypherMemory_BucketIsInitialized( const bucket_t &bucket );

mem_error_t CypherMemory_BucketLastError( const bucket_t &bucket );

common::usize CypherMemory_BucketClassIndexForSize( const bucket_t &bucket, common::usize size, common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

common::usize CypherMemory_BucketUsedCount( const bucket_t &bucket );

common::usize CypherMemory_BucketFreeCount( const bucket_t &bucket );

common::f32 CypherMemory_BucketUsageRatio( const bucket_t &bucket );

template <typename T>
T *CypherMemory_BucketAllocType( bucket_t &bucket )
{
    return static_cast<T *>( CypherMemory_BucketAlloc( bucket, sizeof( T ), alignof( T ) ) );
}

template <typename T>
T *CypherMemory_BucketAllocTypeDebug( bucket_t &bucket, const char *file, const char *function, common::i32 line )
{
    return static_cast<T *>( CypherMemory_BucketAllocDebug( bucket, sizeof( T ), alignof( T ), file, function, line ) );
}

template <typename T>
T *CypherMemory_BucketAllocTypeZero( bucket_t &bucket )
{
    return static_cast<T *>( CypherMemory_BucketAllocZero( bucket, sizeof( T ), alignof( T ) ) );
}

template <typename T>
T *CypherMemory_BucketAllocTypeZeroDebug( bucket_t &bucket, const char *file, const char *function, common::i32 line )
{
    return static_cast<T *>( CypherMemory_BucketAllocZeroDebug( bucket, sizeof( T ), alignof( T ), file, function, line ) );
}

}       // namespace cypher::engine::memory

#define CYPHER_MEMORY_BUCKET_ALLOC( BUCKET, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::CypherMemory_BucketAllocDebug( ( BUCKET ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_BUCKET_ALLOC_ZERO( BUCKET, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::CypherMemory_BucketAllocZeroDebug( ( BUCKET ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_BUCKET_ALLOC_TYPE( BUCKET, TYPE ) \
    ::cypher::engine::memory::CypherMemory_BucketAllocTypeDebug<TYPE>( ( BUCKET ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_BUCKET_ALLOC_TYPE_ZERO( BUCKET, TYPE ) \
    ::cypher::engine::memory::CypherMemory_BucketAllocTypeZeroDebug<TYPE>( ( BUCKET ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_BUCKET_FREE( BUCKET, PTR ) \
    ::cypher::engine::memory::CypherMemory_BucketFreeDebug( ( BUCKET ), ( PTR ), __FILE__, __func__, __LINE__ )

#endif // CYPHER_ENGINE_MEMORY_BUCKET_H
