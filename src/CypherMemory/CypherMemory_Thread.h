//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherMemory/CypherMemory_Thread.h
//  Purpose: Declares the CypherMemory Memory Thread module.
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

#ifndef CYPHER_ENGINE_MEMORY_THREAD_H
#define CYPHER_ENGINE_MEMORY_THREAD_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon.h"
#include "CypherMemory_Arena.h"
#include "CypherMemory_Bucket.h"
#include "CypherMemory_Pool.h"

#include <mutex>

namespace cypher::engine::memory
{

enum class memory_thread_policy_t : common::u8 {
    SINGLE_THREAD = 0,
    EXTERNAL_LOCK,
    INTERNAL_LOCK
};

struct memory_mutex_t {
    std::mutex nativeMutex{};
};

struct thread_safe_arena_t {
    arena_t *arena{ nullptr };
    memory_mutex_t mutex{};
    mem_error_t lastError{ mem_error_t::OK };
    bool initialized{ false };
};

struct thread_safe_pool_t {
    pool_t *pool{ nullptr };
    memory_mutex_t mutex{};
    mem_error_t lastError{ mem_error_t::OK };
    bool initialized{ false };
};

struct thread_safe_bucket_t {
    bucket_t *bucket{ nullptr };
    memory_mutex_t mutex{};
    mem_error_t lastError{ mem_error_t::OK };
    bool initialized{ false };
};

void CypherMemory_MutexLock( memory_mutex_t &mutex );

void CypherMemory_MutexUnlock( memory_mutex_t &mutex );

mem_error_t CypherMemory_ThreadSafeArenaBind( thread_safe_arena_t &threadSafeArena, arena_t &arena );

void CypherMemory_ThreadSafeArenaUnbind( thread_safe_arena_t &threadSafeArena );

void *CypherMemory_ThreadSafeArenaAlloc( thread_safe_arena_t &threadSafeArena,
                                         common::usize size,
                                         common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *CypherMemory_ThreadSafeArenaAllocDebug( thread_safe_arena_t &threadSafeArena,
                                              common::usize size,
                                              common::usize alignment,
                                              const char *file,
                                              const char *function,
                                              common::i32 line );

void *CypherMemory_ThreadSafeArenaAllocZero( thread_safe_arena_t &threadSafeArena,
                                             common::usize size,
                                             common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *CypherMemory_ThreadSafeArenaAllocZeroDebug( thread_safe_arena_t &threadSafeArena,
                                                  common::usize size,
                                                  common::usize alignment,
                                                  const char *file,
                                                  const char *function,
                                                  common::i32 line );

void CypherMemory_ThreadSafeArenaReset( thread_safe_arena_t &threadSafeArena );

arena_stats_t CypherMemory_ThreadSafeArenaStats( thread_safe_arena_t &threadSafeArena );

mem_error_t CypherMemory_ThreadSafeArenaLastError( const thread_safe_arena_t &threadSafeArena );

mem_error_t CypherMemory_ThreadSafePoolBind( thread_safe_pool_t &threadSafePool, pool_t &pool );

void CypherMemory_ThreadSafePoolUnbind( thread_safe_pool_t &threadSafePool );

void *CypherMemory_ThreadSafePoolAlloc( thread_safe_pool_t &threadSafePool );

void *CypherMemory_ThreadSafePoolAllocDebug( thread_safe_pool_t &threadSafePool,
                                             const char *file,
                                             const char *function,
                                             common::i32 line );

void *CypherMemory_ThreadSafePoolAllocZero( thread_safe_pool_t &threadSafePool );

void *CypherMemory_ThreadSafePoolAllocZeroDebug( thread_safe_pool_t &threadSafePool,
                                                 const char *file,
                                                 const char *function,
                                                 common::i32 line );

mem_error_t CypherMemory_ThreadSafePoolFree( thread_safe_pool_t &threadSafePool, void *ptr );

mem_error_t CypherMemory_ThreadSafePoolFreeDebug( thread_safe_pool_t &threadSafePool,
                                                   void *ptr,
                                                   const char *file,
                                                   const char *function,
                                                   common::i32 line );

void CypherMemory_ThreadSafePoolReset( thread_safe_pool_t &threadSafePool );

pool_stats_t CypherMemory_ThreadSafePoolStats( thread_safe_pool_t &threadSafePool );

mem_error_t CypherMemory_ThreadSafePoolLastError( const thread_safe_pool_t &threadSafePool );

mem_error_t CypherMemory_ThreadSafeBucketBind( thread_safe_bucket_t &threadSafeBucket, bucket_t &bucket );

void CypherMemory_ThreadSafeBucketUnbind( thread_safe_bucket_t &threadSafeBucket );

void *CypherMemory_ThreadSafeBucketAlloc( thread_safe_bucket_t &threadSafeBucket,
                                          common::usize size,
                                          common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *CypherMemory_ThreadSafeBucketAllocDebug( thread_safe_bucket_t &threadSafeBucket,
                                               common::usize size,
                                               common::usize alignment,
                                               const char *file,
                                               const char *function,
                                               common::i32 line );

void *CypherMemory_ThreadSafeBucketAllocZero( thread_safe_bucket_t &threadSafeBucket,
                                              common::usize size,
                                              common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *CypherMemory_ThreadSafeBucketAllocZeroDebug( thread_safe_bucket_t &threadSafeBucket,
                                                   common::usize size,
                                                   common::usize alignment,
                                                   const char *file,
                                                   const char *function,
                                                   common::i32 line );

mem_error_t CypherMemory_ThreadSafeBucketFree( thread_safe_bucket_t &threadSafeBucket, void *ptr );

mem_error_t CypherMemory_ThreadSafeBucketFreeDebug( thread_safe_bucket_t &threadSafeBucket,
                                                     void *ptr,
                                                     const char *file,
                                                     const char *function,
                                                     common::i32 line );

void CypherMemory_ThreadSafeBucketReset( thread_safe_bucket_t &threadSafeBucket );

bucket_stats_t CypherMemory_ThreadSafeBucketStats( thread_safe_bucket_t &threadSafeBucket );

mem_error_t CypherMemory_ThreadSafeBucketLastError( const thread_safe_bucket_t &threadSafeBucket );

}       // namespace cypher::engine::memory

#define CYPHER_MEMORY_THREAD_SAFE_ARENA_ALLOC( THREAD_SAFE_ARENA, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::CypherMemory_ThreadSafeArenaAllocDebug( ( THREAD_SAFE_ARENA ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_ARENA_ALLOC_ZERO( THREAD_SAFE_ARENA, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::CypherMemory_ThreadSafeArenaAllocZeroDebug( ( THREAD_SAFE_ARENA ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_POOL_ALLOC( THREAD_SAFE_POOL ) \
    ::cypher::engine::memory::CypherMemory_ThreadSafePoolAllocDebug( ( THREAD_SAFE_POOL ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_POOL_ALLOC_ZERO( THREAD_SAFE_POOL ) \
    ::cypher::engine::memory::CypherMemory_ThreadSafePoolAllocZeroDebug( ( THREAD_SAFE_POOL ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_POOL_FREE( THREAD_SAFE_POOL, PTR ) \
    ::cypher::engine::memory::CypherMemory_ThreadSafePoolFreeDebug( ( THREAD_SAFE_POOL ), ( PTR ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_BUCKET_ALLOC( THREAD_SAFE_BUCKET, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::CypherMemory_ThreadSafeBucketAllocDebug( ( THREAD_SAFE_BUCKET ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_BUCKET_ALLOC_ZERO( THREAD_SAFE_BUCKET, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::CypherMemory_ThreadSafeBucketAllocZeroDebug( ( THREAD_SAFE_BUCKET ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_BUCKET_FREE( THREAD_SAFE_BUCKET, PTR ) \
    ::cypher::engine::memory::CypherMemory_ThreadSafeBucketFreeDebug( ( THREAD_SAFE_BUCKET ), ( PTR ), __FILE__, __func__, __LINE__ )

#endif // CYPHER_ENGINE_MEMORY_THREAD_H
