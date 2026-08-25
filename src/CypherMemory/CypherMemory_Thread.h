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

/*
================
Thread Contract

Thread-local memory state borrows backing storage from the memory system and must be released
before thread exit. Cross-thread frees follow the owning allocator's synchronization policy.
================
*/

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
    SINGLE_THREAD = 0, // Caller guarantees no concurrent access.
    EXTERNAL_LOCK,     // Caller serializes operations around the allocator.
    INTERNAL_LOCK      // Wrapper acquires its mutex around every allocator operation.
};

struct memory_mutex_t {
    std::mutex nativeMutex{};                               // Platform-neutral C++ mutex owned by the wrapper.
};

struct thread_safe_arena_t {
    arena_t *arena{ nullptr };                              // Borrowed arena protected by mutex.
    memory_mutex_t mutex{};                                 // Serializes all operations through this wrapper.
    mem_error_t lastError{ mem_error_t::OK };               // Wrapper-level result of the latest operation.
    bool initialized{ false };                              // Binding is active and arena is valid.
};

struct thread_safe_pool_t {
    pool_t *pool{ nullptr };                                // Borrowed pool protected by mutex.
    memory_mutex_t mutex{};                                 // Serializes all operations through this wrapper.
    mem_error_t lastError{ mem_error_t::OK };               // Wrapper-level result of the latest operation.
    bool initialized{ false };                              // Binding is active and pool is valid.
};

struct thread_safe_bucket_t {
    bucket_t *bucket{ nullptr };                            // Borrowed bucket protected by mutex.
    memory_mutex_t mutex{};                                 // Serializes all operations through this wrapper.
    mem_error_t lastError{ mem_error_t::OK };               // Wrapper-level result of the latest operation.
    bool initialized{ false };                              // Binding is active and bucket is valid.
};

void Mem_MutexLock( memory_mutex_t &mutex );

void Mem_MutexUnlock( memory_mutex_t &mutex );

mem_error_t Mem_ThreadSafeArenaBind( thread_safe_arena_t &threadSafeArena, arena_t &arena );

void Mem_ThreadSafeArenaUnbind( thread_safe_arena_t &threadSafeArena );

void *Mem_ThreadSafeArenaAlloc( thread_safe_arena_t &threadSafeArena,
                                         common::usize size,
                                         common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *Mem_ThreadSafeArenaAllocDebug( thread_safe_arena_t &threadSafeArena,
                                              common::usize size,
                                              common::usize alignment,
                                              const char *file,
                                              const char *function,
                                              common::i32 line );

void *Mem_ThreadSafeArenaAllocZero( thread_safe_arena_t &threadSafeArena,
                                             common::usize size,
                                             common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *Mem_ThreadSafeArenaAllocZeroDebug( thread_safe_arena_t &threadSafeArena,
                                                  common::usize size,
                                                  common::usize alignment,
                                                  const char *file,
                                                  const char *function,
                                                  common::i32 line );

void Mem_ThreadSafeArenaReset( thread_safe_arena_t &threadSafeArena );

arena_stats_t Mem_ThreadSafeArenaStats( thread_safe_arena_t &threadSafeArena );

mem_error_t Mem_ThreadSafeArenaLastError( const thread_safe_arena_t &threadSafeArena );

mem_error_t Mem_ThreadSafePoolBind( thread_safe_pool_t &threadSafePool, pool_t &pool );

void Mem_ThreadSafePoolUnbind( thread_safe_pool_t &threadSafePool );

void *Mem_ThreadSafePoolAlloc( thread_safe_pool_t &threadSafePool );

void *Mem_ThreadSafePoolAllocDebug( thread_safe_pool_t &threadSafePool,
                                             const char *file,
                                             const char *function,
                                             common::i32 line );

void *Mem_ThreadSafePoolAllocZero( thread_safe_pool_t &threadSafePool );

void *Mem_ThreadSafePoolAllocZeroDebug( thread_safe_pool_t &threadSafePool,
                                                 const char *file,
                                                 const char *function,
                                                 common::i32 line );

mem_error_t Mem_ThreadSafePoolFree( thread_safe_pool_t &threadSafePool, void *ptr );

mem_error_t Mem_ThreadSafePoolFreeDebug( thread_safe_pool_t &threadSafePool,
                                                   void *ptr,
                                                   const char *file,
                                                   const char *function,
                                                   common::i32 line );

void Mem_ThreadSafePoolReset( thread_safe_pool_t &threadSafePool );

pool_stats_t Mem_ThreadSafePoolStats( thread_safe_pool_t &threadSafePool );

mem_error_t Mem_ThreadSafePoolLastError( const thread_safe_pool_t &threadSafePool );

mem_error_t Mem_ThreadSafeBucketBind( thread_safe_bucket_t &threadSafeBucket, bucket_t &bucket );

void Mem_ThreadSafeBucketUnbind( thread_safe_bucket_t &threadSafeBucket );

void *Mem_ThreadSafeBucketAlloc( thread_safe_bucket_t &threadSafeBucket,
                                          common::usize size,
                                          common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *Mem_ThreadSafeBucketAllocDebug( thread_safe_bucket_t &threadSafeBucket,
                                               common::usize size,
                                               common::usize alignment,
                                               const char *file,
                                               const char *function,
                                               common::i32 line );

void *Mem_ThreadSafeBucketAllocZero( thread_safe_bucket_t &threadSafeBucket,
                                              common::usize size,
                                              common::usize alignment = CYPHER_MEMORY_DEFAULT_ALIGNMENT );

void *Mem_ThreadSafeBucketAllocZeroDebug( thread_safe_bucket_t &threadSafeBucket,
                                                   common::usize size,
                                                   common::usize alignment,
                                                   const char *file,
                                                   const char *function,
                                                   common::i32 line );

mem_error_t Mem_ThreadSafeBucketFree( thread_safe_bucket_t &threadSafeBucket, void *ptr );

mem_error_t Mem_ThreadSafeBucketFreeDebug( thread_safe_bucket_t &threadSafeBucket,
                                                     void *ptr,
                                                     const char *file,
                                                     const char *function,
                                                     common::i32 line );

void Mem_ThreadSafeBucketReset( thread_safe_bucket_t &threadSafeBucket );

bucket_stats_t Mem_ThreadSafeBucketStats( thread_safe_bucket_t &threadSafeBucket );

mem_error_t Mem_ThreadSafeBucketLastError( const thread_safe_bucket_t &threadSafeBucket );

}       // namespace cypher::engine::memory

#define CYPHER_MEMORY_THREAD_SAFE_ARENA_ALLOC( THREAD_SAFE_ARENA, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::Mem_ThreadSafeArenaAllocDebug( ( THREAD_SAFE_ARENA ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_ARENA_ALLOC_ZERO( THREAD_SAFE_ARENA, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::Mem_ThreadSafeArenaAllocZeroDebug( ( THREAD_SAFE_ARENA ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_POOL_ALLOC( THREAD_SAFE_POOL ) \
    ::cypher::engine::memory::Mem_ThreadSafePoolAllocDebug( ( THREAD_SAFE_POOL ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_POOL_ALLOC_ZERO( THREAD_SAFE_POOL ) \
    ::cypher::engine::memory::Mem_ThreadSafePoolAllocZeroDebug( ( THREAD_SAFE_POOL ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_POOL_FREE( THREAD_SAFE_POOL, PTR ) \
    ::cypher::engine::memory::Mem_ThreadSafePoolFreeDebug( ( THREAD_SAFE_POOL ), ( PTR ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_BUCKET_ALLOC( THREAD_SAFE_BUCKET, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::Mem_ThreadSafeBucketAllocDebug( ( THREAD_SAFE_BUCKET ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_BUCKET_ALLOC_ZERO( THREAD_SAFE_BUCKET, SIZE, ALIGNMENT ) \
    ::cypher::engine::memory::Mem_ThreadSafeBucketAllocZeroDebug( ( THREAD_SAFE_BUCKET ), ( SIZE ), ( ALIGNMENT ), __FILE__, __func__, __LINE__ )

#define CYPHER_MEMORY_THREAD_SAFE_BUCKET_FREE( THREAD_SAFE_BUCKET, PTR ) \
    ::cypher::engine::memory::Mem_ThreadSafeBucketFreeDebug( ( THREAD_SAFE_BUCKET ), ( PTR ), __FILE__, __func__, __LINE__ )

#endif // CYPHER_ENGINE_MEMORY_THREAD_H
