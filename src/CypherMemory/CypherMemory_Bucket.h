//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherMemory/CypherMemory_Bucket.h
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

/*
================
Bucket Contract

Pool and bucket allocators trade generality for predictable size classes and reuse. Allocation
metadata must always identify the owning pool before a block is returned or freed.
================
*/

#ifndef CYPHER_ENGINE_MEMORY_BUCKET_H
#define CYPHER_ENGINE_MEMORY_BUCKET_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon.h"
#include "CypherMemory_Pool.h"

namespace cypher::engine::memory
{

constexpr common::usize CYPHER_MEMORY_BUCKET_MAX_CLASSES = 16u;
constexpr common::usize CYPHER_MEMORY_BUCKET_DEFAULT_CLASS_COUNT = 10u;

constexpr common::u32 CYPHER_MEMORY_BUCKET_FLAG_NONE              = 0u;       // Preserve class-slot bytes across operations.
constexpr common::u32 CYPHER_MEMORY_BUCKET_FLAG_ZERO_ON_ALLOC     = 1u << 0u; // Return zero-filled payloads.
constexpr common::u32 CYPHER_MEMORY_BUCKET_FLAG_CLEAR_ON_FREE     = 1u << 1u; // Scrub a slot before class reuse.
constexpr common::u32 CYPHER_MEMORY_BUCKET_FLAG_CLEAR_ON_RESET    = 1u << 2u; // Scrub all class pools during reset.
constexpr common::u32 CYPHER_MEMORY_BUCKET_FLAG_CLEAR_ON_SHUTDOWN = 1u << 3u; // Scrub all class backing before shutdown.

struct bucket_class_desc_t {
    common::usize nSlotSize{ 0u };                          // Largest request served by this size class.
    common::usize nSlotCount{ 0u };                         // Fixed number of pool slots assigned to the class.
};

struct bucket_desc_t {
    const char *name{ nullptr };                            // Borrowed diagnostic name; must outlive the bucket.
    arena_t *arena{ nullptr };                              // Borrowed arena from which all class pools are carved.

    common::usize alignment{ CYPHER_MEMORY_DEFAULT_ALIGNMENT }; // Minimum alignment guaranteed by every class.
    common::usize nClassCount{ 0u };                        // Valid prefix length in classes.
    bucket_class_desc_t classes[CYPHER_MEMORY_BUCKET_MAX_CLASSES]{}; // Ascending size-class definitions.

    common::u32 flags{ CYPHER_MEMORY_BUCKET_FLAG_NONE };    // Clearing/zeroing policy propagated to class pools.
};

struct bucket_class_t {
    pool_t pool{};                                          // Fixed-block allocator serving this class.
    common::usize nSlotSize{ 0u };                          // Maximum request size routed to pool.
    common::usize nSlotCount{ 0u };                         // Configured capacity retained for diagnostics.
};

struct bucket_class_stats_t {
    common::usize nSlotSize{ 0u };                          // Payload size served by this class.
    common::usize nSlotCount{ 0u };                         // Total class capacity.
    common::usize nUsedCount{ 0u };                         // Slots currently checked out.
    common::usize nFreeCount{ 0u };                         // Slots currently available.
    common::usize nPeakUsedCount{ 0u };                     // Highest simultaneous usage.
    common::usize nBackingBytes{ 0u };                      // Bytes consumed by this class pool and metadata.
};

struct bucket_stats_t {
    const char *name{ nullptr };                            // Borrowed bucket diagnostic name.

    common::usize nClassCount{ 0u };                        // Number of active class summaries.
    common::usize nUsedCount{ 0u };                         // Aggregate live slots across all classes.
    common::usize nFreeCount{ 0u };                         // Aggregate available slots across all classes.
    common::usize nPeakUsedCount{ 0u };                     // Highest aggregate live-slot count.
    common::usize nBackingBytes{ 0u };                      // Aggregate bytes reserved by class pools.

    common::u64 nAllocationCount{ 0u };                     // Successful allocations routed to classes.
    common::u64 nFreeOperationCount{ 0u };                  // Successful frees routed to classes.
    common::u64 nFailedAllocationCount{ 0u };               // Oversized, exhausted, or invalid allocations.
    common::u64 nFailedFreeCount{ 0u };                     // Foreign or duplicate free attempts.

    bucket_class_stats_t classStats[CYPHER_MEMORY_BUCKET_MAX_CLASSES]{}; // Per-class valid prefix.
};

struct bucket_t {
    const char *name{ nullptr };                            // Borrowed diagnostic name.
    arena_t *arena{ nullptr };                              // Borrowed backing arena; bucket never destroys it.

    bucket_class_t classes[CYPHER_MEMORY_BUCKET_MAX_CLASSES]{}; // Ascending active size classes.
    common::usize nClassCount{ 0u };                        // Valid prefix length in classes.
    common::usize alignment{ CYPHER_MEMORY_DEFAULT_ALIGNMENT }; // Maximum supported request alignment.

    common::usize nPeakUsedCount{ 0u };                     // Highest aggregate live-slot count.

    common::u64 nAllocationCount{ 0u };                     // Successful allocations since counter reset.
    common::u64 nFreeOperationCount{ 0u };                  // Successful frees since counter reset.
    common::u64 nFailedAllocationCount{ 0u };               // Failed allocations since counter reset.
    common::u64 nFailedFreeCount{ 0u };                     // Failed frees since counter reset.

    common::u32 flags{ CYPHER_MEMORY_BUCKET_FLAG_NONE };    // Active bucket clearing/zeroing policy.
    mem_error_t lastError{ mem_error_t::OK };               // Result of the latest mutating operation.
    bool initialized{ false };                              // Class pools and invariants are ready for use.
};

using bucket_allocator_t = bucket_t;

bucket_desc_t Mem_BucketDefaultDesc( arena_t &arena, const char *name = nullptr );

mem_error_t Mem_BucketInit( bucket_t &bucket, const bucket_desc_t &bucketDesc );

void Mem_BucketShutdown( bucket_t &bucket );

void Mem_BucketReset( bucket_t &bucket );

void Mem_BucketResetCounters( bucket_t &bucket );

bucket_stats_t Mem_BucketStats( const bucket_t &bucket );

void *Mem_BucketAlloc( bucket_t &bucket, common::usize size, common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *Mem_BucketAllocDebug( bucket_t &bucket,
                                     common::usize size,
                                     common::usize alignment,
                                     const char *file,
                                     const char *function,
                                     common::i32 line );

void *Mem_BucketAllocZero( bucket_t &bucket, common::usize size, common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *Mem_BucketAllocZeroDebug( bucket_t &bucket,
                                         common::usize size,
                                         common::usize alignment,
                                         const char *file,
                                         const char *function,
                                         common::i32 line );

mem_error_t Mem_BucketFree( bucket_t &bucket, void *ptr );

mem_error_t Mem_BucketFreeDebug( bucket_t &bucket, void *ptr, const char *file, const char *function, common::i32 line );

bool Mem_BucketContains( const bucket_t &bucket, const void *ptr );

bool Mem_BucketOwnsSlot( const bucket_t &bucket, const void *ptr );

bool Mem_BucketIsInitialized( const bucket_t &bucket );

mem_error_t Mem_BucketLastError( const bucket_t &bucket );

common::usize Mem_BucketClassIndexForSize( const bucket_t &bucket, common::usize size, common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

common::usize Mem_BucketUsedCount( const bucket_t &bucket );

common::usize Mem_BucketFreeCount( const bucket_t &bucket );

common::f32 Mem_BucketUsageRatio( const bucket_t &bucket );

template <typename T>
T *Mem_BucketAllocType( bucket_t &bucket )
{
    return static_cast<T *>( Mem_BucketAlloc( bucket, sizeof( T ), alignof( T ) ) );
}

template <typename T>
T *Mem_BucketAllocTypeDebug( bucket_t &bucket, const char *file, const char *function, common::i32 line )
{
    return static_cast<T *>( Mem_BucketAllocDebug( bucket, sizeof( T ), alignof( T ), file, function, line ) );
}

template <typename T>
T *Mem_BucketAllocTypeZero( bucket_t &bucket )
{
    return static_cast<T *>( Mem_BucketAllocZero( bucket, sizeof( T ), alignof( T ) ) );
}

template <typename T>
T *Mem_BucketAllocTypeZeroDebug( bucket_t &bucket, const char *file, const char *function, common::i32 line )
{
    return static_cast<T *>( Mem_BucketAllocZeroDebug( bucket, sizeof( T ), alignof( T ), file, function, line ) );
}

}       // namespace cypher::engine::memory

#define CYPHER_MEMORY_BUCKET_ALLOC( BUCKET, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::Mem_BucketAllocDebug( ( BUCKET ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_BUCKET_ALLOC_ZERO( BUCKET, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::Mem_BucketAllocZeroDebug( ( BUCKET ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_BUCKET_ALLOC_TYPE( BUCKET, TYPE ) \
    ::cypher::engine::memory::Mem_BucketAllocTypeDebug<TYPE>( ( BUCKET ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_BUCKET_ALLOC_TYPE_ZERO( BUCKET, TYPE ) \
    ::cypher::engine::memory::Mem_BucketAllocTypeZeroDebug<TYPE>( ( BUCKET ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_BUCKET_FREE( BUCKET, PTR ) \
    ::cypher::engine::memory::Mem_BucketFreeDebug( ( BUCKET ), ( PTR ), __FILE__, __func__, __LINE__ )

#endif // CYPHER_ENGINE_MEMORY_BUCKET_H
