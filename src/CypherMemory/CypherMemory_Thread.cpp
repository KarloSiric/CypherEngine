//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherMemory/CypherMemory_Thread.cpp
//  Purpose: Implements the CypherMemory Memory Thread module.
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
Thread Implementation Notes

Binding checks, allocator calls, and last-error publication share one critical
section. The mutex protects wrapper operations, not the lifetime of borrowed
backing storage or the caller's use of returned payloads.
================
*/

#include "CypherMemory_Thread.h"
#include "CypherLog.h"

namespace cypher::engine::memory
{

namespace {

void *Mem_ThreadSafeAllocFail( const char *szAllocatorName, const mem_error_t error, const char *reason )
{
    LOG_ERROR( log::channel_t::MEMORY,
                      "thread-safe allocator '%s' allocation failed: %s.",
                      szAllocatorName ? szAllocatorName : "<unnamed>",
                      reason ? reason : Mem_ErrorDesc( error ) );
    return nullptr;
}

}       // namespace

void Mem_MutexLock( memory_mutex_t &mutex )
{
    mutex.nativeMutex.lock();
}

void Mem_MutexUnlock( memory_mutex_t &mutex )
{
    mutex.nativeMutex.unlock();
}

mem_error_t Mem_ThreadSafeArenaBind( thread_safe_arena_t &threadSafeArena, arena_t &arena )
{
    const std::lock_guard<std::mutex> lock( threadSafeArena.mutex.nativeMutex );
    // Binding transfers no ownership; the arena must outlive the wrapper and all users.
    if ( threadSafeArena.initialized ) {
        threadSafeArena.lastError = mem_error_t::ERR_ALREADY_INITIALIZED;
        return threadSafeArena.lastError;
    }

    if ( !Mem_ArenaIsInitialized( arena ) ) {
        threadSafeArena.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return threadSafeArena.lastError;
    }

    threadSafeArena.arena = &arena;
    threadSafeArena.lastError = mem_error_t::OK;
    threadSafeArena.initialized = true;

    return threadSafeArena.lastError;
}

void Mem_ThreadSafeArenaUnbind( thread_safe_arena_t &threadSafeArena )
{
    // Serialize publication of the unbound state with in-flight wrapper operations.
    const std::lock_guard<std::mutex> lock( threadSafeArena.mutex.nativeMutex );
    threadSafeArena.arena = nullptr;
    threadSafeArena.lastError = mem_error_t::OK;
    threadSafeArena.initialized = false;
}

void *Mem_ThreadSafeArenaAlloc( thread_safe_arena_t &threadSafeArena,
                                         common::usize size,
                                         common::usize alignment )
{
    return Mem_ThreadSafeArenaAllocDebug( threadSafeArena, size, alignment, nullptr, nullptr, 0 );
}

void *Mem_ThreadSafeArenaAllocDebug( thread_safe_arena_t &threadSafeArena,
                                              common::usize size,
                                              common::usize alignment,
                                              const char *file,
                                              const char *function,
                                              common::i32 line )
{
    const std::lock_guard<std::mutex> lock( threadSafeArena.mutex.nativeMutex );
    if ( !threadSafeArena.initialized || threadSafeArena.arena == nullptr ) {
        threadSafeArena.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return Mem_ThreadSafeAllocFail( nullptr, threadSafeArena.lastError, "arena wrapper is not initialized" );
    }

    // Keep the allocator mutation and last-error snapshot in one critical section.
    void *memory = Mem_ArenaAllocDebug( *threadSafeArena.arena, size, alignment, file, function, line );
    threadSafeArena.lastError = Mem_ArenaLastError( *threadSafeArena.arena );

    return memory;
}

void *Mem_ThreadSafeArenaAllocZero( thread_safe_arena_t &threadSafeArena,
                                             common::usize size,
                                             common::usize alignment )
{
    return Mem_ThreadSafeArenaAllocZeroDebug( threadSafeArena, size, alignment, nullptr, nullptr, 0 );
}

void *Mem_ThreadSafeArenaAllocZeroDebug( thread_safe_arena_t &threadSafeArena,
                                                  common::usize size,
                                                  common::usize alignment,
                                                  const char *file,
                                                  const char *function,
                                                  common::i32 line )
{
    const std::lock_guard<std::mutex> lock( threadSafeArena.mutex.nativeMutex );
    if ( !threadSafeArena.initialized || threadSafeArena.arena == nullptr ) {
        threadSafeArena.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return Mem_ThreadSafeAllocFail( nullptr, threadSafeArena.lastError, "arena wrapper is not initialized" );
    }

    void *memory = Mem_ArenaAllocZeroDebug( *threadSafeArena.arena, size, alignment, file, function, line );
    threadSafeArena.lastError = Mem_ArenaLastError( *threadSafeArena.arena );

    return memory;
}

void Mem_ThreadSafeArenaReset( thread_safe_arena_t &threadSafeArena )
{
    const std::lock_guard<std::mutex> lock( threadSafeArena.mutex.nativeMutex );
    if ( !threadSafeArena.initialized || threadSafeArena.arena == nullptr ) {
        threadSafeArena.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return;
    }

    Mem_ArenaReset( *threadSafeArena.arena );
    threadSafeArena.lastError = Mem_ArenaLastError( *threadSafeArena.arena );
}

arena_stats_t Mem_ThreadSafeArenaStats( thread_safe_arena_t &threadSafeArena )
{
    const std::lock_guard<std::mutex> lock( threadSafeArena.mutex.nativeMutex );
    arena_stats_t stats{};

    if ( !threadSafeArena.initialized || threadSafeArena.arena == nullptr ) {
        threadSafeArena.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return stats;
    }

    stats = Mem_ArenaStats( *threadSafeArena.arena );
    threadSafeArena.lastError = Mem_ArenaLastError( *threadSafeArena.arena );

    return stats;
}

mem_error_t Mem_ThreadSafeArenaLastError( const thread_safe_arena_t &threadSafeArena )
{
    const std::lock_guard<std::mutex> lock( threadSafeArena.mutex.nativeMutex );
    return threadSafeArena.lastError;
}

mem_error_t Mem_ThreadSafePoolBind( thread_safe_pool_t &threadSafePool, pool_t &pool )
{
    const std::lock_guard<std::mutex> lock( threadSafePool.mutex.nativeMutex );
    // The wrapper serializes access but does not extend the pool's lifetime.
    if ( threadSafePool.initialized ) {
        threadSafePool.lastError = mem_error_t::ERR_ALREADY_INITIALIZED;
        return threadSafePool.lastError;
    }

    if ( !Mem_PoolIsInitialized( pool ) ) {
        threadSafePool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return threadSafePool.lastError;
    }

    threadSafePool.pool = &pool;
    threadSafePool.lastError = mem_error_t::OK;
    threadSafePool.initialized = true;

    return threadSafePool.lastError;
}

void Mem_ThreadSafePoolUnbind( thread_safe_pool_t &threadSafePool )
{
    // Pool free-list and bitmap state must change under the same lock.
    const std::lock_guard<std::mutex> lock( threadSafePool.mutex.nativeMutex );
    threadSafePool.pool = nullptr;
    threadSafePool.lastError = mem_error_t::OK;
    threadSafePool.initialized = false;
}

void *Mem_ThreadSafePoolAlloc( thread_safe_pool_t &threadSafePool )
{
    return Mem_ThreadSafePoolAllocDebug( threadSafePool, nullptr, nullptr, 0 );
}

void *Mem_ThreadSafePoolAllocDebug( thread_safe_pool_t &threadSafePool,
                                             const char *file,
                                             const char *function,
                                             common::i32 line )
{
    const std::lock_guard<std::mutex> lock( threadSafePool.mutex.nativeMutex );
    if ( !threadSafePool.initialized || threadSafePool.pool == nullptr ) {
        threadSafePool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return Mem_ThreadSafeAllocFail( nullptr, threadSafePool.lastError, "pool wrapper is not initialized" );
    }

    void *memory = Mem_PoolAllocDebug( *threadSafePool.pool, file, function, line );
    threadSafePool.lastError = Mem_PoolLastError( *threadSafePool.pool );

    return memory;
}

void *Mem_ThreadSafePoolAllocZero( thread_safe_pool_t &threadSafePool )
{
    return Mem_ThreadSafePoolAllocZeroDebug( threadSafePool, nullptr, nullptr, 0 );
}

void *Mem_ThreadSafePoolAllocZeroDebug( thread_safe_pool_t &threadSafePool,
                                                 const char *file,
                                                 const char *function,
                                                 common::i32 line )
{
    const std::lock_guard<std::mutex> lock( threadSafePool.mutex.nativeMutex );
    if ( !threadSafePool.initialized || threadSafePool.pool == nullptr ) {
        threadSafePool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return Mem_ThreadSafeAllocFail( nullptr, threadSafePool.lastError, "pool wrapper is not initialized" );
    }

    void *memory = Mem_PoolAllocZeroDebug( *threadSafePool.pool, file, function, line );
    threadSafePool.lastError = Mem_PoolLastError( *threadSafePool.pool );

    return memory;
}

mem_error_t Mem_ThreadSafePoolFree( thread_safe_pool_t &threadSafePool, void *ptr )
{
    return Mem_ThreadSafePoolFreeDebug( threadSafePool, ptr, nullptr, nullptr, 0 );
}

mem_error_t Mem_ThreadSafePoolFreeDebug( thread_safe_pool_t &threadSafePool,
                                                   void *ptr,
                                                   const char *file,
                                                   const char *function,
                                                   common::i32 line )
{
    const std::lock_guard<std::mutex> lock( threadSafePool.mutex.nativeMutex );
    if ( !threadSafePool.initialized || threadSafePool.pool == nullptr ) {
        threadSafePool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return threadSafePool.lastError;
    }

    threadSafePool.lastError = Mem_PoolFreeDebug( *threadSafePool.pool, ptr, file, function, line );

    return threadSafePool.lastError;
}

void Mem_ThreadSafePoolReset( thread_safe_pool_t &threadSafePool )
{
    const std::lock_guard<std::mutex> lock( threadSafePool.mutex.nativeMutex );
    if ( !threadSafePool.initialized || threadSafePool.pool == nullptr ) {
        threadSafePool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return;
    }

    Mem_PoolReset( *threadSafePool.pool );
    threadSafePool.lastError = Mem_PoolLastError( *threadSafePool.pool );
}

pool_stats_t Mem_ThreadSafePoolStats( thread_safe_pool_t &threadSafePool )
{
    const std::lock_guard<std::mutex> lock( threadSafePool.mutex.nativeMutex );
    pool_stats_t stats{};

    if ( !threadSafePool.initialized || threadSafePool.pool == nullptr ) {
        threadSafePool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return stats;
    }

    stats = Mem_PoolStats( *threadSafePool.pool );
    threadSafePool.lastError = Mem_PoolLastError( *threadSafePool.pool );

    return stats;
}

mem_error_t Mem_ThreadSafePoolLastError( const thread_safe_pool_t &threadSafePool )
{
    const std::lock_guard<std::mutex> lock( threadSafePool.mutex.nativeMutex );
    return threadSafePool.lastError;
}

mem_error_t Mem_ThreadSafeBucketBind( thread_safe_bucket_t &threadSafeBucket, bucket_t &bucket )
{
    const std::lock_guard<std::mutex> lock( threadSafeBucket.mutex.nativeMutex );
    // All class pools are protected as one bucket ownership domain.
    if ( threadSafeBucket.initialized ) {
        threadSafeBucket.lastError = mem_error_t::ERR_ALREADY_INITIALIZED;
        return threadSafeBucket.lastError;
    }

    if ( !Mem_BucketIsInitialized( bucket ) ) {
        threadSafeBucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return threadSafeBucket.lastError;
    }

    threadSafeBucket.bucket = &bucket;
    threadSafeBucket.lastError = mem_error_t::OK;
    threadSafeBucket.initialized = true;

    return threadSafeBucket.lastError;
}

void Mem_ThreadSafeBucketUnbind( thread_safe_bucket_t &threadSafeBucket )
{
    // Class selection and the selected pool allocation are one atomic wrapper operation.
    const std::lock_guard<std::mutex> lock( threadSafeBucket.mutex.nativeMutex );
    threadSafeBucket.bucket = nullptr;
    threadSafeBucket.lastError = mem_error_t::OK;
    threadSafeBucket.initialized = false;
}

void *Mem_ThreadSafeBucketAlloc( thread_safe_bucket_t &threadSafeBucket,
                                          common::usize size,
                                          common::usize alignment )
{
    return Mem_ThreadSafeBucketAllocDebug( threadSafeBucket, size, alignment, nullptr, nullptr, 0 );
}

void *Mem_ThreadSafeBucketAllocDebug( thread_safe_bucket_t &threadSafeBucket,
                                               common::usize size,
                                               common::usize alignment,
                                               const char *file,
                                               const char *function,
                                               common::i32 line )
{
    const std::lock_guard<std::mutex> lock( threadSafeBucket.mutex.nativeMutex );
    if ( !threadSafeBucket.initialized || threadSafeBucket.bucket == nullptr ) {
        threadSafeBucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return Mem_ThreadSafeAllocFail( nullptr, threadSafeBucket.lastError, "bucket wrapper is not initialized" );
    }

    void *memory = Mem_BucketAllocDebug( *threadSafeBucket.bucket, size, alignment, file, function, line );
    threadSafeBucket.lastError = Mem_BucketLastError( *threadSafeBucket.bucket );

    return memory;
}

void *Mem_ThreadSafeBucketAllocZero( thread_safe_bucket_t &threadSafeBucket,
                                              common::usize size,
                                              common::usize alignment )
{
    return Mem_ThreadSafeBucketAllocZeroDebug( threadSafeBucket, size, alignment, nullptr, nullptr, 0 );
}

void *Mem_ThreadSafeBucketAllocZeroDebug( thread_safe_bucket_t &threadSafeBucket,
                                                   common::usize size,
                                                   common::usize alignment,
                                                   const char *file,
                                                   const char *function,
                                                   common::i32 line )
{
    const std::lock_guard<std::mutex> lock( threadSafeBucket.mutex.nativeMutex );
    if ( !threadSafeBucket.initialized || threadSafeBucket.bucket == nullptr ) {
        threadSafeBucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return Mem_ThreadSafeAllocFail( nullptr, threadSafeBucket.lastError, "bucket wrapper is not initialized" );
    }

    void *memory = Mem_BucketAllocZeroDebug( *threadSafeBucket.bucket, size, alignment, file, function, line );
    threadSafeBucket.lastError = Mem_BucketLastError( *threadSafeBucket.bucket );

    return memory;
}

mem_error_t Mem_ThreadSafeBucketFree( thread_safe_bucket_t &threadSafeBucket, void *ptr )
{
    return Mem_ThreadSafeBucketFreeDebug( threadSafeBucket, ptr, nullptr, nullptr, 0 );
}

mem_error_t Mem_ThreadSafeBucketFreeDebug( thread_safe_bucket_t &threadSafeBucket,
                                                     void *ptr,
                                                     const char *file,
                                                     const char *function,
                                                     common::i32 line )
{
    const std::lock_guard<std::mutex> lock( threadSafeBucket.mutex.nativeMutex );
    if ( !threadSafeBucket.initialized || threadSafeBucket.bucket == nullptr ) {
        threadSafeBucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return threadSafeBucket.lastError;
    }

    threadSafeBucket.lastError = Mem_BucketFreeDebug( *threadSafeBucket.bucket, ptr, file, function, line );

    return threadSafeBucket.lastError;
}

void Mem_ThreadSafeBucketReset( thread_safe_bucket_t &threadSafeBucket )
{
    const std::lock_guard<std::mutex> lock( threadSafeBucket.mutex.nativeMutex );
    if ( !threadSafeBucket.initialized || threadSafeBucket.bucket == nullptr ) {
        threadSafeBucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return;
    }

    Mem_BucketReset( *threadSafeBucket.bucket );
    threadSafeBucket.lastError = Mem_BucketLastError( *threadSafeBucket.bucket );
}

bucket_stats_t Mem_ThreadSafeBucketStats( thread_safe_bucket_t &threadSafeBucket )
{
    const std::lock_guard<std::mutex> lock( threadSafeBucket.mutex.nativeMutex );
    bucket_stats_t stats{};

    if ( !threadSafeBucket.initialized || threadSafeBucket.bucket == nullptr ) {
        threadSafeBucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return stats;
    }

    stats = Mem_BucketStats( *threadSafeBucket.bucket );
    threadSafeBucket.lastError = Mem_BucketLastError( *threadSafeBucket.bucket );

    return stats;
}

mem_error_t Mem_ThreadSafeBucketLastError( const thread_safe_bucket_t &threadSafeBucket )
{
    const std::lock_guard<std::mutex> lock( threadSafeBucket.mutex.nativeMutex );
    return threadSafeBucket.lastError;
}

}       // namespace cypher::engine::memory
