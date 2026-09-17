//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherMemory/CypherMemory_ScratchBucket_Tests.cpp
//  Purpose: Tests scratch lifetimes and runtime bucket routing and rollback.
//  Details: Protects borrowed arena ownership, scope errors, class exhaustion,
//           invalid frees, zero allocation, and partial initialization cleanup.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMemory_Bucket.h"
#include "CypherMemory_Scratch.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

using namespace cypher::engine::memory;
using cypher::engine::common::usize;

namespace
{

struct arena_guard_t {
    arena_t arena{};
    ~arena_guard_t() { Mem_ArenaShutdown( arena ); }
};

struct scratch_guard_t {
    scratch_scope_t scope{};
    ~scratch_guard_t()
    {
        if ( Mem_ScratchIsActive( scope ) ) {
            (void)Mem_ScratchEnd( scope );
        }
    }
};

struct bucket_guard_t {
    bucket_t bucket{};
    ~bucket_guard_t() { Mem_BucketShutdown( bucket ); }
};

arena_desc_t ExternalArenaDesc( void *buffer, usize capacity )
{
    arena_desc_t desc{};
    desc.name = "scratch-bucket-arena";
    desc.backing = arena_backing_t::ARENA_EXTERNAL_BUFFER;
    desc.pExternalBuffer = buffer;
    desc.capacity = capacity;
    return desc;
}

bucket_desc_t SmallBucketDesc( arena_t &arena )
{
    bucket_desc_t desc{};
    desc.name = "small-bucket-tests";
    desc.arena = &arena;
    desc.alignment = 16u;
    desc.nClassCount = 3u;
    desc.classes[0] = { 16u, 1u };
    desc.classes[1] = { 32u, 1u };
    desc.classes[2] = { 64u, 1u };
    return desc;
}

bool BytesEqual( const void *data, usize size, unsigned char value )
{
    const auto *bytes = static_cast<const unsigned char *>( data );
    return std::all_of( bytes, bytes + size,
                        [value]( unsigned char byte ) { return byte == value; } );
}

} // namespace

TEST_CASE( "Runtime scratch scopes nest and restore their own entry cursors",
           "[CypherMemory][Scratch]" )
{
    alignas( 64 ) std::array<unsigned char, 512> storage{};
    arena_guard_t parent{};
    arena_t &arena = parent.arena;
    REQUIRE( Mem_ArenaInit( arena, ExternalArenaDesc( storage.data(), storage.size() ) ) == mem_error_t::OK );
    void *persistent = Mem_ArenaAlloc( arena, 7u, 1u );
    REQUIRE( persistent != nullptr );
    std::memset( persistent, 0x23u, 7u );
    const usize parentUsed = Mem_ArenaUsed( arena );
    scratch_guard_t outer{};
    REQUIRE( Mem_ScratchBegin( outer.scope, arena, "outer" ) == mem_error_t::OK );
    REQUIRE( Mem_ScratchBegin( outer.scope, arena ) == mem_error_t::ERR_ALREADY_INITIALIZED );
    void *outerBytes = Mem_ScratchAlloc( outer.scope, 21u, 32u );
    REQUIRE( outerBytes != nullptr );
    REQUIRE( reinterpret_cast<usize>( outerBytes ) % 32u == 0u );
    std::memset( outerBytes, 0x47u, 21u );
    const usize outerUsed = Mem_ArenaUsed( arena );

    scratch_guard_t inner{};
    REQUIRE( Mem_ScratchBegin( inner.scope, arena, "inner" ) == mem_error_t::OK );
    void *innerBytes = Mem_ScratchAllocZero( inner.scope, 19u, 16u );
    REQUIRE( innerBytes != nullptr );
    REQUIRE( BytesEqual( innerBytes, 19u, 0u ) );
    std::memset( innerBytes, 0x89u, 19u );
    REQUIRE( Mem_ScratchAlloc( inner.scope, storage.size(), 1u ) == nullptr );
    REQUIRE( Mem_ScratchLastError( inner.scope ) == mem_error_t::ERR_OUT_OF_MEMORY );
    const scratch_stats_t innerStats = Mem_ScratchStats( inner.scope );
    REQUIRE( innerStats.active );
    REQUIRE( innerStats.nUsedAtBegin == outerUsed );
    REQUIRE( innerStats.nUsedSinceBegin == Mem_ArenaUsed( arena ) - outerUsed );
    REQUIRE( innerStats.nAllocationCountSinceBegin == 1u );
    REQUIRE( innerStats.nFailedAllocationCountSinceBegin == 1u );
    REQUIRE( Mem_ScratchStats( outer.scope ).nAllocationCountSinceBegin == 2u );
    REQUIRE( Mem_ScratchEnd( inner.scope ) == mem_error_t::OK );
    REQUIRE_FALSE( Mem_ScratchIsActive( inner.scope ) );
    REQUIRE( Mem_ArenaUsed( arena ) == outerUsed );
    REQUIRE( BytesEqual( outerBytes, 21u, 0x47u ) );
    REQUIRE( BytesEqual( persistent, 7u, 0x23u ) );
    REQUIRE( Mem_ScratchEnd( outer.scope ) == mem_error_t::OK );
    REQUIRE( Mem_ArenaUsed( arena ) == parentUsed );
    REQUIRE( Mem_ArenaIsInitialized( arena ) );
    REQUIRE( BytesEqual( persistent, 7u, 0x23u ) );
    REQUIRE( Mem_ScratchAlloc( outer.scope, 1u ) == nullptr );
    REQUIRE( Mem_ScratchLastError( outer.scope ) == mem_error_t::ERR_NOT_INITIALIZED );
    REQUIRE( Mem_ScratchEnd( outer.scope ) == mem_error_t::ERR_NOT_INITIALIZED );
}

TEST_CASE( "Runtime scratch end retains its error when the parent marker has been invalidated",
           "[CypherMemory][Scratch]" )
{
    alignas( 32 ) std::array<unsigned char, 256> storage{};
    arena_guard_t parent{};
    arena_t &arena = parent.arena;
    REQUIRE( Mem_ArenaInit( arena, ExternalArenaDesc( storage.data(), storage.size() ) ) == mem_error_t::OK );
    REQUIRE( Mem_ArenaAlloc( arena, 16u, 1u ) != nullptr );
    scratch_guard_t scratch{};
    REQUIRE( Mem_ScratchBegin( scratch.scope, arena, "invalidated-scratch" ) == mem_error_t::OK );
    REQUIRE( Mem_ScratchAlloc( scratch.scope, 8u ) != nullptr );
    Mem_ArenaReset( arena );
    REQUIRE( Mem_ScratchEnd( scratch.scope ) == mem_error_t::ERR_INVALID_MARKER );
    REQUIRE( Mem_ScratchLastError( scratch.scope ) == mem_error_t::ERR_INVALID_MARKER );
    REQUIRE_FALSE( Mem_ScratchIsActive( scratch.scope ) );
    REQUIRE_FALSE( Mem_ScratchStats( scratch.scope ).active );
    REQUIRE( std::strcmp( Mem_ScratchStats( scratch.scope ).name, "invalidated-scratch" ) == 0 );
    REQUIRE( Mem_ArenaUsed( arena ) == 0u );
    REQUIRE( Mem_ScratchBegin( scratch.scope, arena ) == mem_error_t::OK );
    REQUIRE( Mem_ScratchLastError( scratch.scope ) == mem_error_t::OK );
    REQUIRE( Mem_ScratchAlloc( scratch.scope, 8u ) != nullptr );
    REQUIRE( Mem_ScratchEnd( scratch.scope ) == mem_error_t::OK );
}

TEST_CASE( "Runtime scratch validates backing lifetime and guards diagnostic subtraction",
           "[CypherMemory][Scratch]" )
{
    alignas( 32 ) std::array<unsigned char, 256> storage{};
    arena_guard_t parent{};
    arena_t &arena = parent.arena;
    scratch_guard_t scratch{};
    REQUIRE( Mem_ScratchBegin( scratch.scope, arena ) == mem_error_t::ERR_NOT_INITIALIZED );
    REQUIRE_FALSE( Mem_ScratchIsActive( scratch.scope ) );
    REQUIRE( Mem_ScratchAlloc( scratch.scope, 8u ) == nullptr );
    REQUIRE( Mem_ArenaInit( arena, ExternalArenaDesc( storage.data(), storage.size() ) ) == mem_error_t::OK );
    void *prefix = Mem_ArenaAlloc( arena, 16u, 1u );
    REQUIRE( prefix != nullptr );
    std::memset( prefix, 0xB8u, 16u );
    REQUIRE( Mem_ArenaAlloc( arena, 0u ) == nullptr );
    REQUIRE( Mem_ScratchBegin( scratch.scope, arena ) == mem_error_t::OK );
    REQUIRE( Mem_ScratchAlloc( scratch.scope, 8u, 3u ) == nullptr );
    REQUIRE( Mem_ScratchLastError( scratch.scope ) == mem_error_t::ERR_INVALID_ALIGNMENT );
    const usize overflow = std::numeric_limits<usize>::max() / sizeof( usize ) + 1u;
    REQUIRE( Mem_ScratchAllocArray<usize>( scratch.scope, overflow ) == nullptr );
    REQUIRE( Mem_ScratchLastError( scratch.scope ) == mem_error_t::ERR_INTEGER_OVERFLOW );
    REQUIRE( Mem_ArenaUsed( arena ) == 16u );
    REQUIRE( BytesEqual( prefix, 16u, 0xB8u ) );

    Mem_ArenaResetCounters( arena );
    const scratch_stats_t resetStats = Mem_ScratchStats( scratch.scope );
    REQUIRE( resetStats.nAllocationCountSinceBegin == 0u );
    REQUIRE( resetStats.nFailedAllocationCountSinceBegin == 0u );
    REQUIRE( resetStats.nUsedSinceBegin == 0u );
    Mem_ArenaShutdown( arena );
    REQUIRE( Mem_ScratchAllocZero( scratch.scope, 8u ) == nullptr );
    REQUIRE( Mem_ScratchLastError( scratch.scope ) == mem_error_t::ERR_NOT_INITIALIZED );
    REQUIRE( Mem_ScratchEnd( scratch.scope ) == mem_error_t::ERR_NOT_INITIALIZED );
    REQUIRE( Mem_ScratchLastError( scratch.scope ) == mem_error_t::ERR_NOT_INITIALIZED );
    REQUIRE_FALSE( Mem_ScratchIsActive( scratch.scope ) );
}

TEST_CASE( "Runtime bucket initialization rolls back completed classes when a later class fails",
           "[CypherMemory][Bucket]" )
{
    alignas( 64 ) std::array<unsigned char, 512> storage{};
    storage.fill( 0xA5u );
    arena_guard_t parent{};
    arena_t &arena = parent.arena;
    REQUIRE( Mem_ArenaInit( arena, ExternalArenaDesc( storage.data() + 32u, 448u ) ) == mem_error_t::OK );
    void *prefix = Mem_ArenaAlloc( arena, 7u, 1u );
    REQUIRE( prefix != nullptr );
    std::memset( prefix, 0x29u, 7u );
    const arena_marker_t marker = Mem_ArenaGetMarker( arena );
    bucket_guard_t scope{};
    bucket_desc_t desc = SmallBucketDesc( arena );
    desc.flags = CYPHER_MEMORY_BUCKET_FLAG_CLEAR_ON_SHUTDOWN;
    mem_error_t expected = mem_error_t::ERR_INVALID_CAPACITY;
    SECTION( "Invalid later class" ) { desc.classes[1].nSlotCount = 0u; }
    SECTION( "Backing exhausted after the first class" ) {
        desc.classes[1].nSlotCount = 100u;
        expected = mem_error_t::ERR_OUT_OF_MEMORY;
    }
    SECTION( "Later class layout overflows" ) {
        desc.classes[1].nSlotSize = std::numeric_limits<usize>::max();
        expected = mem_error_t::ERR_INTEGER_OVERFLOW;
    }
    REQUIRE( Mem_BucketInit( scope.bucket, desc ) == expected );
    REQUIRE( Mem_BucketLastError( scope.bucket ) == expected );
    REQUIRE_FALSE( Mem_BucketIsInitialized( scope.bucket ) );
    REQUIRE( Mem_BucketStats( scope.bucket ).nClassCount == 0u );
    REQUIRE( Mem_BucketUsedCount( scope.bucket ) == 0u );
    REQUIRE( Mem_BucketFreeCount( scope.bucket ) == 0u );
    REQUIRE( Mem_ArenaUsed( arena ) == marker.used );
    REQUIRE( BytesEqual( prefix, 7u, 0x29u ) );
    REQUIRE( BytesEqual( storage.data(), 32u, 0xA5u ) );
    REQUIRE( BytesEqual( storage.data() + 480u, 32u, 0xA5u ) );

    // The entire rolled-back tail can still be acquired by the caller.
    const usize remaining = Mem_ArenaRemaining( arena );
    REQUIRE( Mem_ArenaAlloc( arena, remaining, 1u ) != nullptr );
    REQUIRE( Mem_ArenaRewind( arena, marker ) == mem_error_t::OK );
    REQUIRE( Mem_BucketInit( scope.bucket, SmallBucketDesc( arena ) ) == mem_error_t::OK );
    REQUIRE( Mem_BucketAlloc( scope.bucket, 16u ) != nullptr );
    REQUIRE( BytesEqual( prefix, 7u, 0x29u ) );
}

TEST_CASE( "Runtime bucket rejects invalid top-level descriptors without consuming its arena",
           "[CypherMemory][Bucket]" )
{
    alignas( 32 ) std::array<unsigned char, 512> storage{};
    storage.fill( 0x5Bu );
    arena_guard_t parent{};
    REQUIRE( Mem_ArenaInit( parent.arena, ExternalArenaDesc( storage.data(), storage.size() ) ) == mem_error_t::OK );
    arena_t unavailable{};
    bucket_guard_t scope{};
    bucket_desc_t desc = SmallBucketDesc( parent.arena );
    mem_error_t expected = mem_error_t::ERR_INVALID_CAPACITY;
    SECTION( "No classes" ) { desc.nClassCount = 0u; }
    SECTION( "Too many classes" ) { desc.nClassCount = CYPHER_MEMORY_BUCKET_MAX_CLASSES + 1u; }
    SECTION( "Missing parent" ) { desc.arena = nullptr; expected = mem_error_t::ERR_INVALID_ARGUMENT; }
    SECTION( "Uninitialized parent" ) { desc.arena = &unavailable; expected = mem_error_t::ERR_NOT_INITIALIZED; }
    SECTION( "Invalid alignment" ) { desc.alignment = 3u; expected = mem_error_t::ERR_INVALID_ALIGNMENT; }
    REQUIRE( Mem_BucketInit( scope.bucket, desc ) == expected );
    REQUIRE_FALSE( Mem_BucketIsInitialized( scope.bucket ) );
    REQUIRE( Mem_ArenaUsed( parent.arena ) == 0u );
    REQUIRE( BytesEqual( storage.data(), storage.size(), 0x5Bu ) );
}

TEST_CASE( "Runtime bucket routes best fit and falls back through larger classes before exhaustion",
           "[CypherMemory][Bucket]" )
{
    alignas( 32 ) std::array<unsigned char, 512> storage{};
    arena_guard_t parent{};
    REQUIRE( Mem_ArenaInit( parent.arena, ExternalArenaDesc( storage.data(), storage.size() ) ) == mem_error_t::OK );
    bucket_guard_t scope{};
    bucket_t &bucket = scope.bucket;
    const bucket_desc_t desc = SmallBucketDesc( parent.arena );
    REQUIRE( Mem_BucketInit( bucket, desc ) == mem_error_t::OK );
    for ( usize size : { 1u, 16u } ) { REQUIRE( Mem_BucketClassIndexForSize( bucket, size ) == 0u ); }
    for ( usize size : { 17u, 32u } ) { REQUIRE( Mem_BucketClassIndexForSize( bucket, size ) == 1u ); }
    for ( usize size : { 33u, 64u } ) { REQUIRE( Mem_BucketClassIndexForSize( bucket, size ) == 2u ); }
    REQUIRE( Mem_BucketClassIndexForSize( bucket, 65u ) == std::numeric_limits<usize>::max() );
    const usize sizes[] = { 16u, 16u, 33u };
    std::array<void *, 3> slots{};
    for ( usize i = 0u; i < slots.size(); ++i ) {
        slots[i] = Mem_BucketAlloc( bucket, sizes[i], 16u );
        REQUIRE( slots[i] != nullptr );
        REQUIRE( reinterpret_cast<usize>( slots[i] ) % 16u == 0u );
        REQUIRE( Mem_BucketOwnsSlot( bucket, slots[i] ) );
        REQUIRE( Mem_BucketStats( bucket ).classStats[i].nUsedCount == 1u );
        for ( usize j = 0u; j < i; ++j ) { REQUIRE( slots[i] != slots[j] ); }
        std::memset( slots[i], static_cast<int>( 0x60u + i ), sizes[i] );
    }
    const auto before = storage;
    REQUIRE( Mem_BucketInit( bucket, desc ) == mem_error_t::ERR_ALREADY_INITIALIZED );
    REQUIRE( storage == before );
    REQUIRE( Mem_BucketAlloc( bucket, 1u ) == nullptr );
    REQUIRE( Mem_BucketLastError( bucket ) == mem_error_t::ERR_OUT_OF_MEMORY );
    REQUIRE( Mem_BucketUsageRatio( bucket ) == 1.0f );
    REQUIRE( Mem_BucketClassIndexForSize( bucket, 16u ) == 0u );
    for ( usize i = 0u; i < slots.size(); ++i ) {
        REQUIRE( BytesEqual( slots[i], sizes[i], static_cast<unsigned char>( 0x60u + i ) ) );
    }
    REQUIRE( Mem_BucketFree( bucket, slots[0] ) == mem_error_t::OK );
    REQUIRE( Mem_BucketAlloc( bucket, 16u ) == slots[0] );
    REQUIRE( Mem_BucketUsedCount( bucket ) == 3u );
    REQUIRE( Mem_BucketFreeCount( bucket ) == 0u );
}

TEST_CASE( "Runtime bucket rejects incompatible requests and invalid frees while preserving live slots",
           "[CypherMemory][Bucket]" )
{
    alignas( 32 ) std::array<unsigned char, 512> storage{};
    arena_guard_t parent{};
    REQUIRE( Mem_ArenaInit( parent.arena, ExternalArenaDesc( storage.data(), storage.size() ) ) == mem_error_t::OK );
    void *foreign = Mem_ArenaAlloc( parent.arena, 16u );
    REQUIRE( foreign != nullptr );
    std::memset( foreign, 0x42u, 16u );
    bucket_guard_t scope{};
    bucket_t &bucket = scope.bucket;
    REQUIRE( Mem_BucketInit( bucket, SmallBucketDesc( parent.arena ) ) == mem_error_t::OK );
    void *small = Mem_BucketAlloc( bucket, 16u );
    void *large = Mem_BucketAlloc( bucket, 64u );
    REQUIRE( small != nullptr );
    REQUIRE( large != nullptr );
    std::memset( large, 0xDAu, 64u );
    struct request_t { usize size; usize alignment; mem_error_t error; };
    const request_t invalid[] = {
        { 0u, 16u, mem_error_t::ERR_INVALID_ARGUMENT },
        { 65u, 16u, mem_error_t::ERR_BUFFER_TOO_SMALL },
        { 8u, 3u, mem_error_t::ERR_INVALID_ALIGNMENT },
        { 8u, 32u, mem_error_t::ERR_BUFFER_TOO_SMALL }
    };
    for ( const request_t &request : invalid ) {
        REQUIRE( Mem_BucketAlloc( bucket, request.size, request.alignment ) == nullptr );
        REQUIRE( Mem_BucketLastError( bucket ) == request.error );
    }
    auto *interior = static_cast<unsigned char *>( large ) + 1u;
    REQUIRE( Mem_BucketContains( bucket, interior ) );
    REQUIRE_FALSE( Mem_BucketOwnsSlot( bucket, interior ) );
    REQUIRE_FALSE( Mem_BucketContains( bucket, foreign ) );
    REQUIRE( Mem_BucketFree( bucket, foreign ) == mem_error_t::ERR_INVALID_POINTER );
    REQUIRE( Mem_BucketFree( bucket, interior ) == mem_error_t::ERR_INVALID_POINTER );
    REQUIRE( Mem_BucketFree( bucket, nullptr ) == mem_error_t::ERR_INVALID_POINTER );
    REQUIRE( Mem_BucketFree( bucket, small ) == mem_error_t::OK );
    REQUIRE( Mem_BucketFree( bucket, small ) == mem_error_t::ERR_DOUBLE_FREE );
    bucket_stats_t stats = Mem_BucketStats( bucket );
    REQUIRE( stats.nUsedCount == 1u );
    REQUIRE( stats.nFreeCount == 2u );
    REQUIRE( stats.nPeakUsedCount == 2u );
    REQUIRE( stats.nAllocationCount == 2u );
    REQUIRE( stats.nFreeOperationCount == 1u );
    REQUIRE( stats.nFailedAllocationCount == 4u );
    REQUIRE( stats.nFailedFreeCount == 4u );
    REQUIRE( BytesEqual( large, 64u, 0xDAu ) );
    REQUIRE( BytesEqual( foreign, 16u, 0x42u ) );
    Mem_BucketResetCounters( bucket );
    stats = Mem_BucketStats( bucket );
    REQUIRE( stats.nUsedCount == 1u );
    REQUIRE( stats.nPeakUsedCount == 1u );
    REQUIRE( stats.nAllocationCount == 0u );
    REQUIRE( stats.nFreeOperationCount == 0u );
    REQUIRE( stats.nFailedAllocationCount == 0u );
    REQUIRE( stats.nFailedFreeCount == 0u );
    REQUIRE( BytesEqual( large, 64u, 0xDAu ) );
    REQUIRE( Mem_BucketAlloc( bucket, 16u ) == small );
}

TEST_CASE( "Runtime bucket zeroing and reset preserve borrowed arena ownership",
           "[CypherMemory][Bucket]" )
{
    alignas( 32 ) std::array<unsigned char, 1024> storage{};
    arena_guard_t parent{};
    arena_t &arena = parent.arena;
    REQUIRE( Mem_ArenaInit( arena, ExternalArenaDesc( storage.data(), storage.size() ) ) == mem_error_t::OK );
    void *prefix = Mem_ArenaAlloc( arena, 16u );
    REQUIRE( prefix != nullptr );
    std::memset( prefix, 0x38u, 16u );
    bucket_guard_t scope{};
    bucket_t &bucket = scope.bucket;
    bucket_desc_t desc = SmallBucketDesc( arena );
    desc.flags = CYPHER_MEMORY_BUCKET_FLAG_CLEAR_ON_SHUTDOWN;
    bool explicitZero = true;
    SECTION( "Explicit zero allocation" ) {}
    SECTION( "Bucket zero-on-allocation policy" ) {
        desc.flags |= CYPHER_MEMORY_BUCKET_FLAG_ZERO_ON_ALLOC;
        explicitZero = false;
    }
    REQUIRE( Mem_BucketInit( bucket, desc ) == mem_error_t::OK );
    void *suffix = Mem_ArenaAlloc( arena, 19u );
    REQUIRE( suffix != nullptr );
    std::memset( suffix, 0xAFu, 19u );
    const usize used = Mem_ArenaUsed( arena );
    std::array<void *, 3> slots{};
    for ( usize iteration = 0u; iteration < 3u; ++iteration ) {
        for ( usize i = 0u; i < slots.size(); ++i ) {
            const usize size = desc.classes[i].nSlotSize;
            slots[i] = explicitZero ? Mem_BucketAllocZero( bucket, size ) : Mem_BucketAlloc( bucket, size );
            REQUIRE( slots[i] != nullptr );
            REQUIRE( BytesEqual( slots[i], size, 0u ) );
            std::memset( slots[i], 0xCEu, size );
        }
        if ( iteration == 0u ) {
            for ( void *slot : slots ) { REQUIRE( Mem_BucketFree( bucket, slot ) == mem_error_t::OK ); }
        } else {
            Mem_BucketReset( bucket );
            REQUIRE( Mem_BucketUsedCount( bucket ) == 0u );
            REQUIRE( Mem_BucketFreeCount( bucket ) == 3u );
            REQUIRE( Mem_BucketFree( bucket, slots[0] ) == mem_error_t::ERR_DOUBLE_FREE );
        }
        REQUIRE( Mem_ArenaUsed( arena ) == used );
        REQUIRE( BytesEqual( prefix, 16u, 0x38u ) );
        REQUIRE( BytesEqual( suffix, 19u, 0xAFu ) );
    }
    Mem_BucketShutdown( bucket );
    REQUIRE_FALSE( Mem_BucketIsInitialized( bucket ) );
    REQUIRE( Mem_ArenaIsInitialized( arena ) );
    REQUIRE( Mem_ArenaUsed( arena ) == used );
    REQUIRE( BytesEqual( prefix, 16u, 0x38u ) );
    REQUIRE( BytesEqual( suffix, 19u, 0xAFu ) );
}
