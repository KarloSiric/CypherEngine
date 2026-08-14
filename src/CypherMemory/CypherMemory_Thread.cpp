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

#include "CypherMemory_Thread.h"
#include "CypherLog.h"

namespace cypher::engine::memory
{

namespace {

void *CypherMemory_ThreadSafeAllocFail( const char *szAllocatorName, const mem_error_t error, const char *reason )
{
    LOG_ERROR( log::channel_t::MEMORY,
                      "thread-safe allocator '%s' allocation failed: %s.",
                      szAllocatorName ? szAllocatorName : "<unnamed>",
                      reason ? reason : CypherMemory_ErrorDesc( error ) );
    return nullptr;
}

}       // namespace

void CypherMemory_MutexLock( memory_mutex_t &mutex )
{
    mutex.nativeMutex.lock();
}

void CypherMemory_MutexUnlock( memory_mutex_t &mutex )
{
    mutex.nativeMutex.unlock();
}

mem_error_t CypherMemory_ThreadSafeArenaBind( thread_safe_arena_t &threadSafeArena, arena_t &arena )
{
    if ( threadSafeArena.initialized ) {
        threadSafeArena.lastError = mem_error_t::ERR_ALREADY_INITIALIZED;
        return threadSafeArena.lastError;
    }

    if ( !CypherMemory_ArenaIsInitialized( arena ) ) {
        threadSafeArena.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return threadSafeArena.lastError;
    }

    threadSafeArena.arena = &arena;
    threadSafeArena.lastError = mem_error_t::OK;
    threadSafeArena.initialized = true;

    return threadSafeArena.lastError;
}

void CypherMemory_ThreadSafeArenaUnbind( thread_safe_arena_t &threadSafeArena )
{
    CypherMemory_MutexLock( threadSafeArena.mutex );
    threadSafeArena.arena = nullptr;
    threadSafeArena.lastError = mem_error_t::OK;
    threadSafeArena.initialized = false;
    CypherMemory_MutexUnlock( threadSafeArena.mutex );
}

void *CypherMemory_ThreadSafeArenaAlloc( thread_safe_arena_t &threadSafeArena,
                                         common::usize size,
                                         common::usize alignment )
{
    return CypherMemory_ThreadSafeArenaAllocDebug( threadSafeArena, size, alignment, nullptr, nullptr, 0 );
}

void *CypherMemory_ThreadSafeArenaAllocDebug( thread_safe_arena_t &threadSafeArena,
                                              common::usize size,
                                              common::usize alignment,
                                              const char *file,
                                              const char *function,
                                              common::i32 line )
{
    if ( !threadSafeArena.initialized || threadSafeArena.arena == nullptr ) {
        threadSafeArena.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return CypherMemory_ThreadSafeAllocFail( nullptr, threadSafeArena.lastError, "arena wrapper is not initialized" );
    }

    CypherMemory_MutexLock( threadSafeArena.mutex );
    void *memory = CypherMemory_ArenaAllocDebug( *threadSafeArena.arena, size, alignment, file, function, line );
    threadSafeArena.lastError = CypherMemory_ArenaLastError( *threadSafeArena.arena );
    CypherMemory_MutexUnlock( threadSafeArena.mutex );

    return memory;
}

void *CypherMemory_ThreadSafeArenaAllocZero( thread_safe_arena_t &threadSafeArena,
                                             common::usize size,
                                             common::usize alignment )
{
    return CypherMemory_ThreadSafeArenaAllocZeroDebug( threadSafeArena, size, alignment, nullptr, nullptr, 0 );
}

void *CypherMemory_ThreadSafeArenaAllocZeroDebug( thread_safe_arena_t &threadSafeArena,
                                                  common::usize size,
                                                  common::usize alignment,
                                                  const char *file,
                                                  const char *function,
                                                  common::i32 line )
{
    if ( !threadSafeArena.initialized || threadSafeArena.arena == nullptr ) {
        threadSafeArena.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return CypherMemory_ThreadSafeAllocFail( nullptr, threadSafeArena.lastError, "arena wrapper is not initialized" );
    }

    CypherMemory_MutexLock( threadSafeArena.mutex );
    void *memory = CypherMemory_ArenaAllocZeroDebug( *threadSafeArena.arena, size, alignment, file, function, line );
    threadSafeArena.lastError = CypherMemory_ArenaLastError( *threadSafeArena.arena );
    CypherMemory_MutexUnlock( threadSafeArena.mutex );

    return memory;
}

void CypherMemory_ThreadSafeArenaReset( thread_safe_arena_t &threadSafeArena )
{
    if ( !threadSafeArena.initialized || threadSafeArena.arena == nullptr ) {
        threadSafeArena.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return;
    }

    CypherMemory_MutexLock( threadSafeArena.mutex );
    CypherMemory_ArenaReset( *threadSafeArena.arena );
    threadSafeArena.lastError = CypherMemory_ArenaLastError( *threadSafeArena.arena );
    CypherMemory_MutexUnlock( threadSafeArena.mutex );
}

arena_stats_t CypherMemory_ThreadSafeArenaStats( thread_safe_arena_t &threadSafeArena )
{
    arena_stats_t stats{};

    if ( !threadSafeArena.initialized || threadSafeArena.arena == nullptr ) {
        threadSafeArena.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return stats;
    }

    CypherMemory_MutexLock( threadSafeArena.mutex );
    stats = CypherMemory_ArenaStats( *threadSafeArena.arena );
    threadSafeArena.lastError = CypherMemory_ArenaLastError( *threadSafeArena.arena );
    CypherMemory_MutexUnlock( threadSafeArena.mutex );

    return stats;
}

mem_error_t CypherMemory_ThreadSafeArenaLastError( const thread_safe_arena_t &threadSafeArena )
{
    return threadSafeArena.lastError;
}

mem_error_t CypherMemory_ThreadSafePoolBind( thread_safe_pool_t &threadSafePool, pool_t &pool )
{
    if ( threadSafePool.initialized ) {
        threadSafePool.lastError = mem_error_t::ERR_ALREADY_INITIALIZED;
        return threadSafePool.lastError;
    }

    if ( !CypherMemory_PoolIsInitialized( pool ) ) {
        threadSafePool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return threadSafePool.lastError;
    }

    threadSafePool.pool = &pool;
    threadSafePool.lastError = mem_error_t::OK;
    threadSafePool.initialized = true;

    return threadSafePool.lastError;
}

void CypherMemory_ThreadSafePoolUnbind( thread_safe_pool_t &threadSafePool )
{
    CypherMemory_MutexLock( threadSafePool.mutex );
    threadSafePool.pool = nullptr;
    threadSafePool.lastError = mem_error_t::OK;
    threadSafePool.initialized = false;
    CypherMemory_MutexUnlock( threadSafePool.mutex );
}

void *CypherMemory_ThreadSafePoolAlloc( thread_safe_pool_t &threadSafePool )
{
    return CypherMemory_ThreadSafePoolAllocDebug( threadSafePool, nullptr, nullptr, 0 );
}

void *CypherMemory_ThreadSafePoolAllocDebug( thread_safe_pool_t &threadSafePool,
                                             const char *file,
                                             const char *function,
                                             common::i32 line )
{
    if ( !threadSafePool.initialized || threadSafePool.pool == nullptr ) {
        threadSafePool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return CypherMemory_ThreadSafeAllocFail( nullptr, threadSafePool.lastError, "pool wrapper is not initialized" );
    }

    CypherMemory_MutexLock( threadSafePool.mutex );
    void *memory = CypherMemory_PoolAllocDebug( *threadSafePool.pool, file, function, line );
    threadSafePool.lastError = CypherMemory_PoolLastError( *threadSafePool.pool );
    CypherMemory_MutexUnlock( threadSafePool.mutex );

    return memory;
}

void *CypherMemory_ThreadSafePoolAllocZero( thread_safe_pool_t &threadSafePool )
{
    return CypherMemory_ThreadSafePoolAllocZeroDebug( threadSafePool, nullptr, nullptr, 0 );
}

void *CypherMemory_ThreadSafePoolAllocZeroDebug( thread_safe_pool_t &threadSafePool,
                                                 const char *file,
                                                 const char *function,
                                                 common::i32 line )
{
    if ( !threadSafePool.initialized || threadSafePool.pool == nullptr ) {
        threadSafePool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return CypherMemory_ThreadSafeAllocFail( nullptr, threadSafePool.lastError, "pool wrapper is not initialized" );
    }

    CypherMemory_MutexLock( threadSafePool.mutex );
    void *memory = CypherMemory_PoolAllocZeroDebug( *threadSafePool.pool, file, function, line );
    threadSafePool.lastError = CypherMemory_PoolLastError( *threadSafePool.pool );
    CypherMemory_MutexUnlock( threadSafePool.mutex );

    return memory;
}

mem_error_t CypherMemory_ThreadSafePoolFree( thread_safe_pool_t &threadSafePool, void *ptr )
{
    return CypherMemory_ThreadSafePoolFreeDebug( threadSafePool, ptr, nullptr, nullptr, 0 );
}

mem_error_t CypherMemory_ThreadSafePoolFreeDebug( thread_safe_pool_t &threadSafePool,
                                                   void *ptr,
                                                   const char *file,
                                                   const char *function,
                                                   common::i32 line )
{
    if ( !threadSafePool.initialized || threadSafePool.pool == nullptr ) {
        threadSafePool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return threadSafePool.lastError;
    }

    CypherMemory_MutexLock( threadSafePool.mutex );
    threadSafePool.lastError = CypherMemory_PoolFreeDebug( *threadSafePool.pool, ptr, file, function, line );
    CypherMemory_MutexUnlock( threadSafePool.mutex );

    return threadSafePool.lastError;
}

void CypherMemory_ThreadSafePoolReset( thread_safe_pool_t &threadSafePool )
{
    if ( !threadSafePool.initialized || threadSafePool.pool == nullptr ) {
        threadSafePool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return;
    }

    CypherMemory_MutexLock( threadSafePool.mutex );
    CypherMemory_PoolReset( *threadSafePool.pool );
    threadSafePool.lastError = CypherMemory_PoolLastError( *threadSafePool.pool );
    CypherMemory_MutexUnlock( threadSafePool.mutex );
}

pool_stats_t CypherMemory_ThreadSafePoolStats( thread_safe_pool_t &threadSafePool )
{
    pool_stats_t stats{};

    if ( !threadSafePool.initialized || threadSafePool.pool == nullptr ) {
        threadSafePool.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return stats;
    }

    CypherMemory_MutexLock( threadSafePool.mutex );
    stats = CypherMemory_PoolStats( *threadSafePool.pool );
    threadSafePool.lastError = CypherMemory_PoolLastError( *threadSafePool.pool );
    CypherMemory_MutexUnlock( threadSafePool.mutex );

    return stats;
}

mem_error_t CypherMemory_ThreadSafePoolLastError( const thread_safe_pool_t &threadSafePool )
{
    return threadSafePool.lastError;
}

mem_error_t CypherMemory_ThreadSafeBucketBind( thread_safe_bucket_t &threadSafeBucket, bucket_t &bucket )
{
    if ( threadSafeBucket.initialized ) {
        threadSafeBucket.lastError = mem_error_t::ERR_ALREADY_INITIALIZED;
        return threadSafeBucket.lastError;
    }

    if ( !CypherMemory_BucketIsInitialized( bucket ) ) {
        threadSafeBucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return threadSafeBucket.lastError;
    }

    threadSafeBucket.bucket = &bucket;
    threadSafeBucket.lastError = mem_error_t::OK;
    threadSafeBucket.initialized = true;

    return threadSafeBucket.lastError;
}

void CypherMemory_ThreadSafeBucketUnbind( thread_safe_bucket_t &threadSafeBucket )
{
    CypherMemory_MutexLock( threadSafeBucket.mutex );
    threadSafeBucket.bucket = nullptr;
    threadSafeBucket.lastError = mem_error_t::OK;
    threadSafeBucket.initialized = false;
    CypherMemory_MutexUnlock( threadSafeBucket.mutex );
}

void *CypherMemory_ThreadSafeBucketAlloc( thread_safe_bucket_t &threadSafeBucket,
                                          common::usize size,
                                          common::usize alignment )
{
    return CypherMemory_ThreadSafeBucketAllocDebug( threadSafeBucket, size, alignment, nullptr, nullptr, 0 );
}

void *CypherMemory_ThreadSafeBucketAllocDebug( thread_safe_bucket_t &threadSafeBucket,
                                               common::usize size,
                                               common::usize alignment,
                                               const char *file,
                                               const char *function,
                                               common::i32 line )
{
    if ( !threadSafeBucket.initialized || threadSafeBucket.bucket == nullptr ) {
        threadSafeBucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return CypherMemory_ThreadSafeAllocFail( nullptr, threadSafeBucket.lastError, "bucket wrapper is not initialized" );
    }

    CypherMemory_MutexLock( threadSafeBucket.mutex );
    void *memory = CypherMemory_BucketAllocDebug( *threadSafeBucket.bucket, size, alignment, file, function, line );
    threadSafeBucket.lastError = CypherMemory_BucketLastError( *threadSafeBucket.bucket );
    CypherMemory_MutexUnlock( threadSafeBucket.mutex );

    return memory;
}

void *CypherMemory_ThreadSafeBucketAllocZero( thread_safe_bucket_t &threadSafeBucket,
                                              common::usize size,
                                              common::usize alignment )
{
    return CypherMemory_ThreadSafeBucketAllocZeroDebug( threadSafeBucket, size, alignment, nullptr, nullptr, 0 );
}

void *CypherMemory_ThreadSafeBucketAllocZeroDebug( thread_safe_bucket_t &threadSafeBucket,
                                                   common::usize size,
                                                   common::usize alignment,
                                                   const char *file,
                                                   const char *function,
                                                   common::i32 line )
{
    if ( !threadSafeBucket.initialized || threadSafeBucket.bucket == nullptr ) {
        threadSafeBucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return CypherMemory_ThreadSafeAllocFail( nullptr, threadSafeBucket.lastError, "bucket wrapper is not initialized" );
    }

    CypherMemory_MutexLock( threadSafeBucket.mutex );
    void *memory = CypherMemory_BucketAllocZeroDebug( *threadSafeBucket.bucket, size, alignment, file, function, line );
    threadSafeBucket.lastError = CypherMemory_BucketLastError( *threadSafeBucket.bucket );
    CypherMemory_MutexUnlock( threadSafeBucket.mutex );

    return memory;
}

mem_error_t CypherMemory_ThreadSafeBucketFree( thread_safe_bucket_t &threadSafeBucket, void *ptr )
{
    return CypherMemory_ThreadSafeBucketFreeDebug( threadSafeBucket, ptr, nullptr, nullptr, 0 );
}

mem_error_t CypherMemory_ThreadSafeBucketFreeDebug( thread_safe_bucket_t &threadSafeBucket,
                                                     void *ptr,
                                                     const char *file,
                                                     const char *function,
                                                     common::i32 line )
{
    if ( !threadSafeBucket.initialized || threadSafeBucket.bucket == nullptr ) {
        threadSafeBucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return threadSafeBucket.lastError;
    }

    CypherMemory_MutexLock( threadSafeBucket.mutex );
    threadSafeBucket.lastError = CypherMemory_BucketFreeDebug( *threadSafeBucket.bucket, ptr, file, function, line );
    CypherMemory_MutexUnlock( threadSafeBucket.mutex );

    return threadSafeBucket.lastError;
}

void CypherMemory_ThreadSafeBucketReset( thread_safe_bucket_t &threadSafeBucket )
{
    if ( !threadSafeBucket.initialized || threadSafeBucket.bucket == nullptr ) {
        threadSafeBucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return;
    }

    CypherMemory_MutexLock( threadSafeBucket.mutex );
    CypherMemory_BucketReset( *threadSafeBucket.bucket );
    threadSafeBucket.lastError = CypherMemory_BucketLastError( *threadSafeBucket.bucket );
    CypherMemory_MutexUnlock( threadSafeBucket.mutex );
}

bucket_stats_t CypherMemory_ThreadSafeBucketStats( thread_safe_bucket_t &threadSafeBucket )
{
    bucket_stats_t stats{};

    if ( !threadSafeBucket.initialized || threadSafeBucket.bucket == nullptr ) {
        threadSafeBucket.lastError = mem_error_t::ERR_NOT_INITIALIZED;
        return stats;
    }

    CypherMemory_MutexLock( threadSafeBucket.mutex );
    stats = CypherMemory_BucketStats( *threadSafeBucket.bucket );
    threadSafeBucket.lastError = CypherMemory_BucketLastError( *threadSafeBucket.bucket );
    CypherMemory_MutexUnlock( threadSafeBucket.mutex );

    return stats;
}

mem_error_t CypherMemory_ThreadSafeBucketLastError( const thread_safe_bucket_t &threadSafeBucket )
{
    return threadSafeBucket.lastError;
}

}       // namespace cypher::engine::memory
