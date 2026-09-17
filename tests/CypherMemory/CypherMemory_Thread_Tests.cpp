//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherMemory/CypherMemory_Thread_Tests.cpp
//  Purpose: Tests synchronized wrappers over borrowed runtime allocators.
//  Details: Protects binding transitions, allocation uniqueness, diagnostic
//           snapshots, and operations racing with unbind. Backing storage and
//           allocator owners remain alive until all worker threads have joined.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMemory_Thread.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

using namespace cypher::engine::memory;
using cypher::engine::common::usize;

namespace
{

struct arena_scope_t {
    arena_t arena{};
    ~arena_scope_t() { Mem_ArenaShutdown( arena ); }
};

struct pool_scope_t {
    pool_t pool{};
    ~pool_scope_t() { Mem_PoolShutdown( pool ); }
};

struct bucket_scope_t {
    bucket_t bucket{};
    ~bucket_scope_t() { Mem_BucketShutdown( bucket ); }
};

void InitArena( arena_t &arena, void *storage, usize cbStorage )
{
    arena_desc_t desc{};
    desc.name = "thread-wrapper-arena";
    desc.capacity = cbStorage;
    desc.pExternalBuffer = storage;
    desc.backing = arena_backing_t::ARENA_EXTERNAL_BUFFER;
    REQUIRE( Mem_ArenaInit( arena, desc ) == mem_error_t::OK );
}

void InitPool( pool_t &pool, void *storage, usize cbStorage, usize cSlots )
{
    pool_desc_t desc{};
    desc.name = "thread-wrapper-pool";
    desc.pExternalBuffer = storage;
    desc.nExternalBufferSize = cbStorage;
    desc.nSlotSize = 32u;
    desc.nSlotCount = cSlots;
    desc.alignment = 16u;
    desc.backing = pool_backing_t::POOL_EXTERNAL_BUFFER;
    REQUIRE( Mem_PoolInit( pool, desc ) == mem_error_t::OK );
}

void InitBucket( bucket_t &bucket, arena_t &arena, usize cSlots )
{
    bucket_desc_t desc{};
    desc.name = "thread-wrapper-bucket";
    desc.arena = &arena;
    desc.alignment = 16u;
    desc.nClassCount = 1u;
    desc.classes[0] = { 32u, cSlots };
    REQUIRE( Mem_BucketInit( bucket, desc ) == mem_error_t::OK );
}

template <typename operation_t>
void RunWorkers( usize cWorkers, operation_t operation )
{
    std::atomic<bool> start{ false };
    std::vector<std::thread> workers;
    workers.reserve( cWorkers );
    for ( usize iWorker = 0u; iWorker < cWorkers; ++iWorker ) {
        workers.emplace_back( [&, iWorker]() {
            while ( !start.load( std::memory_order_acquire ) ) {
                std::this_thread::yield();
            }
            operation( iWorker );
        } );
    }
    start.store( true, std::memory_order_release );
    for ( std::thread &worker : workers ) {
        worker.join();
    }
}

template <usize Count>
void CheckUniqueSlots( const std::array<void *, Count> &slots, usize cbSlot )
{
    std::array<usize, Count> addresses{};
    for ( usize iSlot = 0u; iSlot < Count; ++iSlot ) {
        REQUIRE( slots[iSlot] != nullptr );
        addresses[iSlot] = reinterpret_cast<usize>( slots[iSlot] );
        REQUIRE( addresses[iSlot] % 16u == 0u );
    }
    std::sort( addresses.begin(), addresses.end() );
    for ( usize iSlot = 1u; iSlot < Count; ++iSlot ) {
        REQUIRE( addresses[iSlot] - addresses[iSlot - 1u] >= cbSlot );
    }
}

template <typename allocate_t, typename free_t>
bool ConcurrentFreeResultsMatch( allocate_t allocate, free_t free )
{
    std::atomic<bool> failed{ false };
    RunWorkers( 2u, [&]( usize iWorker ) {
        for ( usize i = 0u; i < 256u; ++i ) {
            if ( iWorker == 0u ) {
                void *pPayload = allocate();
                if ( pPayload == nullptr || free( pPayload ) != mem_error_t::OK ) {
                    failed.store( true );
                }
            } else if ( free( nullptr ) != mem_error_t::ERR_INVALID_POINTER ) {
                failed.store( true );
            }
        }
    } );
    return !failed.load();
}

} // namespace

TEST_CASE( "Arena wrapper binding preserves the active borrowed allocator",
           "[CypherMemory][Thread]" )
{
    alignas( 64 ) std::array<unsigned char, 1024> firstStorage{};
    alignas( 64 ) std::array<unsigned char, 1024> secondStorage{};
    arena_scope_t first{};
    arena_scope_t second{};
    thread_safe_arena_t wrapper{};
    REQUIRE( Mem_ThreadSafeArenaBind( wrapper, first.arena ) ==
             mem_error_t::ERR_NOT_INITIALIZED );
    InitArena( first.arena, firstStorage.data(), firstStorage.size() );
    InitArena( second.arena, secondStorage.data(), secondStorage.size() );
    REQUIRE( Mem_ThreadSafeArenaBind( wrapper, first.arena ) == mem_error_t::OK );
    REQUIRE( Mem_ThreadSafeArenaBind( wrapper, first.arena ) ==
             mem_error_t::ERR_ALREADY_INITIALIZED );
    REQUIRE( Mem_ThreadSafeArenaBind( wrapper, second.arena ) ==
             mem_error_t::ERR_ALREADY_INITIALIZED );
    REQUIRE( wrapper.arena == &first.arena );
    REQUIRE( Mem_ThreadSafeArenaAlloc( wrapper, 32u, 16u ) != nullptr );
    Mem_ThreadSafeArenaUnbind( wrapper );
    Mem_ThreadSafeArenaUnbind( wrapper );
    REQUIRE( Mem_ThreadSafeArenaLastError( wrapper ) == mem_error_t::OK );
    REQUIRE( Mem_ThreadSafeArenaAllocZero( wrapper, 32u, 16u ) == nullptr );
    REQUIRE( Mem_ThreadSafeArenaLastError( wrapper ) == mem_error_t::ERR_NOT_INITIALIZED );
    Mem_ThreadSafeArenaReset( wrapper );
    REQUIRE( Mem_ThreadSafeArenaStats( wrapper ).capacity == 0u );
    REQUIRE( Mem_ArenaStats( first.arena ).used == 32u );
    REQUIRE( Mem_ThreadSafeArenaBind( wrapper, second.arena ) == mem_error_t::OK );
    REQUIRE( Mem_ThreadSafeArenaAllocZero( wrapper, 32u, 16u ) == secondStorage.data() );
    Mem_ThreadSafeArenaUnbind( wrapper );
}

TEST_CASE( "Pool and bucket wrappers retain outstanding allocations across unbind",
           "[CypherMemory][Thread]" )
{
    alignas( 64 ) std::array<unsigned char, 4096> storage{};
    SECTION( "Pool" )
    {
        pool_scope_t owner{};
        thread_safe_pool_t wrapper{};
        REQUIRE( Mem_ThreadSafePoolBind( wrapper, owner.pool ) ==
                 mem_error_t::ERR_NOT_INITIALIZED );
        InitPool( owner.pool, storage.data(), storage.size(), 16u );
        REQUIRE( Mem_ThreadSafePoolBind( wrapper, owner.pool ) == mem_error_t::OK );
        REQUIRE( Mem_ThreadSafePoolBind( wrapper, owner.pool ) ==
                 mem_error_t::ERR_ALREADY_INITIALIZED );
        void *pPayload = Mem_ThreadSafePoolAllocZero( wrapper );
        REQUIRE( pPayload != nullptr );
        Mem_ThreadSafePoolUnbind( wrapper );
        REQUIRE( Mem_ThreadSafePoolFree( wrapper, pPayload ) == mem_error_t::ERR_NOT_INITIALIZED );
        REQUIRE( Mem_ThreadSafePoolAlloc( wrapper ) == nullptr );
        Mem_ThreadSafePoolReset( wrapper );
        REQUIRE( Mem_ThreadSafePoolStats( wrapper ).nSlotCount == 0u );
        REQUIRE( Mem_PoolStats( owner.pool ).nUsedCount == 1u );
        Mem_ThreadSafePoolUnbind( wrapper );
        REQUIRE( Mem_ThreadSafePoolBind( wrapper, owner.pool ) == mem_error_t::OK );
        REQUIRE( Mem_ThreadSafePoolFree( wrapper, pPayload ) == mem_error_t::OK );
        REQUIRE( Mem_ThreadSafePoolLastError( wrapper ) == mem_error_t::OK );
        Mem_ThreadSafePoolUnbind( wrapper );
    }
    SECTION( "Bucket" )
    {
        arena_scope_t arena{};
        bucket_scope_t owner{};
        thread_safe_bucket_t wrapper{};
        REQUIRE( Mem_ThreadSafeBucketBind( wrapper, owner.bucket ) ==
                 mem_error_t::ERR_NOT_INITIALIZED );
        InitArena( arena.arena, storage.data(), storage.size() );
        InitBucket( owner.bucket, arena.arena, 16u );
        REQUIRE( Mem_ThreadSafeBucketBind( wrapper, owner.bucket ) == mem_error_t::OK );
        REQUIRE( Mem_ThreadSafeBucketBind( wrapper, owner.bucket ) ==
                 mem_error_t::ERR_ALREADY_INITIALIZED );
        void *pPayload = Mem_ThreadSafeBucketAllocZero( wrapper, 24u, 16u );
        REQUIRE( pPayload != nullptr );
        Mem_ThreadSafeBucketUnbind( wrapper );
        REQUIRE( Mem_ThreadSafeBucketFree( wrapper, pPayload ) == mem_error_t::ERR_NOT_INITIALIZED );
        REQUIRE( Mem_ThreadSafeBucketAlloc( wrapper, 24u, 16u ) == nullptr );
        Mem_ThreadSafeBucketReset( wrapper );
        REQUIRE( Mem_ThreadSafeBucketStats( wrapper ).nClassCount == 0u );
        REQUIRE( Mem_BucketStats( owner.bucket ).nUsedCount == 1u );
        Mem_ThreadSafeBucketUnbind( wrapper );
        REQUIRE( Mem_ThreadSafeBucketBind( wrapper, owner.bucket ) == mem_error_t::OK );
        REQUIRE( Mem_ThreadSafeBucketFree( wrapper, pPayload ) == mem_error_t::OK );
        REQUIRE( Mem_ThreadSafeBucketLastError( wrapper ) == mem_error_t::OK );
        Mem_ThreadSafeBucketUnbind( wrapper );
    }
}

TEST_CASE( "Concurrent arena allocations have unique aligned payloads",
           "[CypherMemory][Thread]" )
{
    constexpr usize cWorkers = 4u;
    constexpr usize cPerWorker = 64u;
    constexpr usize cbPayload = 32u;
    alignas( 64 ) std::array<unsigned char, cWorkers * cPerWorker * cbPayload> storage{};
    arena_scope_t owner{};
    InitArena( owner.arena, storage.data(), storage.size() );
    thread_safe_arena_t wrapper{};
    REQUIRE( Mem_ThreadSafeArenaBind( wrapper, owner.arena ) == mem_error_t::OK );
    std::array<void *, cWorkers * cPerWorker> allocations{};
    std::atomic<bool> failed{ false };
    RunWorkers( cWorkers, [&]( usize iWorker ) {
        for ( usize i = 0u; i < cPerWorker; ++i ) {
            void *pPayload = i % 2u == 0u
                ? Mem_ThreadSafeArenaAlloc( wrapper, cbPayload, 16u )
                : Mem_ThreadSafeArenaAllocZero( wrapper, cbPayload, 16u );
            allocations[iWorker * cPerWorker + i] = pPayload;
            if ( pPayload == nullptr ) {
                failed.store( true );
                continue;
            }
            std::memset( pPayload, static_cast<int>( iWorker + 1u ), cbPayload );
            const arena_stats_t stats = Mem_ThreadSafeArenaStats( wrapper );
            if ( stats.used > stats.capacity || stats.used + stats.remaining != stats.capacity ) {
                failed.store( true );
            }
            (void)Mem_ThreadSafeArenaLastError( wrapper );
        }
    } );
    REQUIRE_FALSE( failed.load() );
    CheckUniqueSlots( allocations, cbPayload );
    for ( usize i = 0u; i < allocations.size(); ++i ) {
        const auto *bytes = static_cast<const unsigned char *>( allocations[i] );
        const auto expected = static_cast<unsigned char>( i / cPerWorker + 1u );
        REQUIRE( std::all_of( bytes, bytes + cbPayload,
                             [expected]( unsigned char value ) { return value == expected; } ) );
    }
    REQUIRE( Mem_ThreadSafeArenaStats( wrapper ).nAllocationCount == allocations.size() );
    Mem_ThreadSafeArenaReset( wrapper );
    REQUIRE( Mem_ThreadSafeArenaStats( wrapper ).used == 0u );
    Mem_ThreadSafeArenaUnbind( wrapper );
}

TEST_CASE( "Concurrent pool allocation and cross-thread frees preserve every slot",
           "[CypherMemory][Thread]" )
{
    constexpr usize cWorkers = 4u;
    constexpr usize cPerWorker = 64u;
    constexpr usize cSlots = cWorkers * cPerWorker;
    alignas( 64 ) std::array<unsigned char, 16384> storage{};
    pool_scope_t owner{};
    InitPool( owner.pool, storage.data(), storage.size(), cSlots );
    thread_safe_pool_t wrapper{};
    REQUIRE( Mem_ThreadSafePoolBind( wrapper, owner.pool ) == mem_error_t::OK );
    std::array<void *, cSlots> allocations{};
    RunWorkers( cWorkers, [&]( usize iWorker ) {
        for ( usize i = 0u; i < cPerWorker; ++i ) {
            allocations[iWorker * cPerWorker + i] = i % 2u == 0u
                ? Mem_ThreadSafePoolAlloc( wrapper )
                : Mem_ThreadSafePoolAllocZero( wrapper );
            (void)Mem_ThreadSafePoolStats( wrapper );
            (void)Mem_ThreadSafePoolLastError( wrapper );
        }
    } );
    CheckUniqueSlots( allocations, 32u );
    REQUIRE( Mem_ThreadSafePoolStats( wrapper ).nUsedCount == cSlots );
    std::atomic<bool> failed{ false };
    RunWorkers( cWorkers, [&]( usize iWorker ) {
        const usize iOtherWorker = ( iWorker + 1u ) % cWorkers;
        for ( usize i = 0u; i < cPerWorker; ++i ) {
            if ( Mem_ThreadSafePoolFree( wrapper, allocations[iOtherWorker * cPerWorker + i] ) !=
                 mem_error_t::OK ) {
                failed.store( true );
            }
        }
    } );
    REQUIRE_FALSE( failed.load() );
    const pool_stats_t stats = Mem_ThreadSafePoolStats( wrapper );
    REQUIRE( stats.nUsedCount == 0u );
    REQUIRE( stats.nFreeCount == cSlots );
    REQUIRE( stats.nFreeOperationCount == cSlots );
    Mem_ThreadSafePoolUnbind( wrapper );
}

TEST_CASE( "Arena operations serialize with unbind and rebinding",
           "[CypherMemory][Thread][Contention]" )
{
    constexpr usize cIterations = 512u;
    alignas( 64 ) std::array<unsigned char, 32768> storage{};
    arena_scope_t owner{};
    InitArena( owner.arena, storage.data(), storage.size() );
    thread_safe_arena_t wrapper{};
    REQUIRE( Mem_ThreadSafeArenaBind( wrapper, owner.arena ) == mem_error_t::OK );
    std::atomic<bool> failed{ false };
    RunWorkers( 3u, [&]( usize iWorker ) {
        for ( usize i = 0u; i < cIterations; ++i ) {
            if ( iWorker == 0u ) {
                // Returned bytes are deliberately not used across reset.
                (void)Mem_ThreadSafeArenaAlloc( wrapper, 16u, 16u );
                (void)Mem_ThreadSafeArenaAllocZero( wrapper, 16u, 16u );
                Mem_ThreadSafeArenaReset( wrapper );
            } else if ( iWorker == 1u ) {
                Mem_ThreadSafeArenaUnbind( wrapper );
                if ( Mem_ThreadSafeArenaBind( wrapper, owner.arena ) != mem_error_t::OK ) {
                    failed.store( true );
                }
            } else {
                const arena_stats_t stats = Mem_ThreadSafeArenaStats( wrapper );
                if ( stats.used > stats.capacity || stats.used + stats.remaining != stats.capacity ) {
                    failed.store( true );
                }
                (void)Mem_ThreadSafeArenaLastError( wrapper );
            }
        }
    } );
    REQUIRE_FALSE( failed.load() );
    Mem_ThreadSafeArenaReset( wrapper );
    REQUIRE( Mem_ThreadSafeArenaAlloc( wrapper, 32u, 16u ) != nullptr );
    Mem_ThreadSafeArenaUnbind( wrapper );
    REQUIRE( Mem_ThreadSafeArenaAlloc( wrapper, 16u, 16u ) == nullptr );
}

TEST_CASE( "Concurrent frees return their own result rather than another operation's error",
           "[CypherMemory][Thread][Contention]" )
{
    alignas( 64 ) std::array<unsigned char, 4096> storage{};
    SECTION( "Pool" )
    {
        pool_scope_t owner{};
        InitPool( owner.pool, storage.data(), storage.size(), 4u );
        thread_safe_pool_t wrapper{};
        REQUIRE( Mem_ThreadSafePoolBind( wrapper, owner.pool ) == mem_error_t::OK );
        REQUIRE( ConcurrentFreeResultsMatch(
            [&]() { return Mem_ThreadSafePoolAlloc( wrapper ); },
            [&]( void *pPayload ) { return Mem_ThreadSafePoolFree( wrapper, pPayload ); } ) );
        REQUIRE( Mem_ThreadSafePoolStats( wrapper ).nUsedCount == 0u );
        Mem_ThreadSafePoolUnbind( wrapper );
    }
    SECTION( "Bucket" )
    {
        arena_scope_t arena{};
        bucket_scope_t owner{};
        InitArena( arena.arena, storage.data(), storage.size() );
        InitBucket( owner.bucket, arena.arena, 4u );
        thread_safe_bucket_t wrapper{};
        REQUIRE( Mem_ThreadSafeBucketBind( wrapper, owner.bucket ) == mem_error_t::OK );
        REQUIRE( ConcurrentFreeResultsMatch(
            [&]() { return Mem_ThreadSafeBucketAlloc( wrapper, 24u, 16u ); },
            [&]( void *pPayload ) { return Mem_ThreadSafeBucketFree( wrapper, pPayload ); } ) );
        REQUIRE( Mem_ThreadSafeBucketStats( wrapper ).nUsedCount == 0u );
        Mem_ThreadSafeBucketUnbind( wrapper );
    }
}

TEST_CASE( "Pool and bucket operations serialize with unbind and rebinding",
           "[CypherMemory][Thread][Contention]" )
{
    constexpr usize cIterations = 512u;
    alignas( 64 ) std::array<unsigned char, 32768> storage{};
    std::atomic<bool> failed{ false };
    SECTION( "Pool" )
    {
        pool_scope_t owner{};
        InitPool( owner.pool, storage.data(), storage.size(), cIterations );
        thread_safe_pool_t wrapper{};
        REQUIRE( Mem_ThreadSafePoolBind( wrapper, owner.pool ) == mem_error_t::OK );
        RunWorkers( 3u, [&]( usize iWorker ) {
            for ( usize i = 0u; i < cIterations; ++i ) {
                if ( iWorker == 0u ) {
                    void *pPayload = i % 2u == 0u
                        ? Mem_ThreadSafePoolAlloc( wrapper )
                        : Mem_ThreadSafePoolAllocZero( wrapper );
                    if ( pPayload != nullptr ) {
                        const mem_error_t result = Mem_ThreadSafePoolFree( wrapper, pPayload );
                        if ( result != mem_error_t::OK && result != mem_error_t::ERR_NOT_INITIALIZED ) {
                            failed.store( true );
                        }
                    }
                } else if ( iWorker == 1u ) {
                    Mem_ThreadSafePoolUnbind( wrapper );
                    if ( Mem_ThreadSafePoolBind( wrapper, owner.pool ) != mem_error_t::OK ) {
                        failed.store( true );
                    }
                } else {
                    const pool_stats_t stats = Mem_ThreadSafePoolStats( wrapper );
                    if ( stats.nUsedCount + stats.nFreeCount != stats.nSlotCount ) {
                        failed.store( true );
                    }
                    (void)Mem_ThreadSafePoolLastError( wrapper );
                }
            }
        } );
        // A free rejected while unbound leaves a valid outstanding allocation.
        // No worker uses any payload now, so reset may reclaim those slots.
        Mem_ThreadSafePoolReset( wrapper );
        REQUIRE( Mem_ThreadSafePoolStats( wrapper ).nFreeCount == cIterations );
        Mem_ThreadSafePoolUnbind( wrapper );
    }
    SECTION( "Bucket" )
    {
        arena_scope_t arena{};
        bucket_scope_t owner{};
        InitArena( arena.arena, storage.data(), storage.size() );
        InitBucket( owner.bucket, arena.arena, cIterations );
        thread_safe_bucket_t wrapper{};
        REQUIRE( Mem_ThreadSafeBucketBind( wrapper, owner.bucket ) == mem_error_t::OK );
        RunWorkers( 3u, [&]( usize iWorker ) {
            for ( usize i = 0u; i < cIterations; ++i ) {
                if ( iWorker == 0u ) {
                    void *pPayload = i % 2u == 0u
                        ? Mem_ThreadSafeBucketAlloc( wrapper, 24u, 16u )
                        : Mem_ThreadSafeBucketAllocZero( wrapper, 24u, 16u );
                    if ( pPayload != nullptr ) {
                        const mem_error_t result = Mem_ThreadSafeBucketFree( wrapper, pPayload );
                        if ( result != mem_error_t::OK && result != mem_error_t::ERR_NOT_INITIALIZED ) {
                            failed.store( true );
                        }
                    }
                } else if ( iWorker == 1u ) {
                    Mem_ThreadSafeBucketUnbind( wrapper );
                    if ( Mem_ThreadSafeBucketBind( wrapper, owner.bucket ) != mem_error_t::OK ) {
                        failed.store( true );
                    }
                } else {
                    const bucket_stats_t stats = Mem_ThreadSafeBucketStats( wrapper );
                    if ( stats.nClassCount > 1u ||
                         ( stats.nClassCount == 1u &&
                           stats.nUsedCount + stats.nFreeCount != cIterations ) ) {
                        failed.store( true );
                    }
                    (void)Mem_ThreadSafeBucketLastError( wrapper );
                }
            }
        } );
        Mem_ThreadSafeBucketReset( wrapper );
        REQUIRE( Mem_ThreadSafeBucketStats( wrapper ).nFreeCount == cIterations );
        Mem_ThreadSafeBucketUnbind( wrapper );
    }
    REQUIRE_FALSE( failed.load() );
}
