//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherMemory/CypherMemory_Arena_Tests.cpp
//  Purpose: Tests runtime linear arena ownership, rollback, and page lifetimes.
//  Details: Uses small real allocations and caller-owned guards to protect
//           marker bounds, failure rollback, clearing policies, and diagnostics.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMemory_Arena.h"
#include "CypherSystem_Public.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

using namespace cypher::engine::memory;
using cypher::engine::common::usize;

namespace
{

struct arena_scope_t {
    arena_t arena{};

    ~arena_scope_t()
    {
        Mem_ArenaShutdown( arena );
    }
};

arena_desc_t ExternalArenaDesc( void *buffer, usize capacity )
{
    arena_desc_t desc{};
    desc.name = "runtime-arena-tests";
    desc.backing = arena_backing_t::ARENA_EXTERNAL_BUFFER;
    desc.pExternalBuffer = buffer;
    desc.capacity = capacity;
    return desc;
}

bool BytesEqual( const void *data, usize size, unsigned char value )
{
    const auto *bytes = static_cast<const unsigned char *>( data );
    return std::all_of( bytes, bytes + size,
                        [value]( unsigned char byte ) { return byte == value; } );
}

} // namespace

TEST_CASE( "Runtime arena aligns borrowed allocations without touching surrounding storage",
           "[CypherMemory][Arena]" )
{
    alignas( 64 ) std::array<unsigned char, 512> storage{};
    storage.fill( 0xA5u );
    arena_scope_t scope{};
    arena_t &arena = scope.arena;
    const arena_desc_t desc = ExternalArenaDesc( storage.data() + 3u, 400u );
    REQUIRE( Mem_ArenaInit( arena, desc ) == mem_error_t::OK );
    REQUIRE( BytesEqual( storage.data(), storage.size(), 0xA5u ) );
    REQUIRE( Mem_ArenaContains( arena, storage.data() + 3u ) );
    REQUIRE( Mem_ArenaContains( arena, storage.data() + 402u ) );
    REQUIRE_FALSE( Mem_ArenaContains( arena, storage.data() + 2u ) );
    REQUIRE_FALSE( Mem_ArenaContains( arena, storage.data() + 403u ) );
    REQUIRE_FALSE( Mem_ArenaContains( arena, nullptr ) );

    struct request_t { usize size; usize alignment; };
    const request_t requests[] = { { 3u, 1u }, { 7u, 16u }, { 17u, 64u }, { 1u, 8u }, { 9u, 32u } };
    struct allocation_t { void *ptr; usize size; unsigned char value; };
    std::array<allocation_t, 5> live{};
    const usize base = reinterpret_cast<usize>( desc.pExternalBuffer );
    usize previousEnd = base;
    for ( usize i = 0u; i < live.size(); ++i ) {
        const request_t request = requests[i];
        void *ptr = Mem_ArenaAlloc( arena, request.size, request.alignment );
        REQUIRE( ptr != nullptr );
        const usize address = reinterpret_cast<usize>( ptr );
        REQUIRE( address % request.alignment == 0u );
        REQUIRE( address >= previousEnd );
        REQUIRE( address - base <= desc.capacity - request.size );
        const unsigned char value = static_cast<unsigned char>( i + 1u );
        live[i] = { ptr, request.size, value };
        std::memset( ptr, value, request.size );
        previousEnd = address + request.size;
        REQUIRE( Mem_ArenaUsed( arena ) == previousEnd - base );
    }

    const usize remaining = Mem_ArenaRemaining( arena );
    REQUIRE( remaining > 0u );
    void *tail = Mem_ArenaAlloc( arena, remaining, 1u );
    REQUIRE( tail != nullptr );
    std::memset( tail, 0x74u, remaining );
    REQUIRE( Mem_ArenaUsed( arena ) == desc.capacity );
    REQUIRE( Mem_ArenaRemaining( arena ) == 0u );
    REQUIRE( Mem_ArenaUsageRatio( arena ) == 1.0f );
    const auto fullStorage = storage;
    REQUIRE( Mem_ArenaAlloc( arena, 1u, 1u ) == nullptr );
    REQUIRE( Mem_ArenaLastError( arena ) == mem_error_t::ERR_OUT_OF_MEMORY );
    REQUIRE( storage == fullStorage );
    for ( const allocation_t &allocation : live ) {
        REQUIRE( BytesEqual( allocation.ptr, allocation.size, allocation.value ) );
    }
    REQUIRE( BytesEqual( storage.data(), 3u, 0xA5u ) );
    REQUIRE( BytesEqual( storage.data() + 403u, storage.size() - 403u, 0xA5u ) );

    Mem_ArenaShutdown( arena );
    REQUIRE_FALSE( Mem_ArenaIsInitialized( arena ) );
    REQUIRE( Mem_ArenaCapacity( arena ) == 0u );
    REQUIRE( storage == fullStorage );
    storage.fill( 0x39u );
    REQUIRE( Mem_ArenaInit( arena, desc ) == mem_error_t::OK );
    REQUIRE( Mem_ArenaAlloc( arena, 32u, 1u ) != nullptr );
}

TEST_CASE( "Runtime arena rejects invalid descriptors before acquiring or mutating backing",
           "[CypherMemory][Arena]" )
{
    alignas( 64 ) std::array<unsigned char, 256> storage{};
    storage.fill( 0x6Du );
    arena_scope_t scope{};
    arena_t &arena = scope.arena;
    const arena_desc_t valid = ExternalArenaDesc( storage.data(), storage.size() );
    arena_desc_t invalid = valid;
    mem_error_t expected = mem_error_t::ERR_INVALID_ARGUMENT;

    SECTION( "Zero capacity" ) {
        invalid.capacity = 0u;
        expected = mem_error_t::ERR_INVALID_CAPACITY;
    }
    SECTION( "Missing external buffer" ) {
        invalid.pExternalBuffer = nullptr;
        expected = mem_error_t::ERR_EXTERNAL_BUFFER_REQUIRED;
    }
    SECTION( "Invalid backing kind" ) {
        invalid.backing = static_cast<arena_backing_t>( 255u );
    }
    SECTION( "Virtual capacity rounding overflow" ) {
        invalid.backing = arena_backing_t::ARENA_VIRTUAL_MEMORY;
        invalid.capacity = std::numeric_limits<usize>::max();
        expected = mem_error_t::ERR_INTEGER_OVERFLOW;
    }
    SECTION( "Initial commit exceeds virtual reservation" ) {
        const usize page = cypher::engine::sys::Sys_VirtualPageSize();
        REQUIRE( page > 0u );
        invalid.backing = arena_backing_t::ARENA_VIRTUAL_MEMORY;
        invalid.capacity = page;
        invalid.initialCommit = page + 1u;
        expected = mem_error_t::ERR_INVALID_CAPACITY;
    }

    REQUIRE( Mem_ArenaInit( arena, invalid ) == expected );
    REQUIRE( Mem_ArenaLastError( arena ) == expected );
    REQUIRE_FALSE( Mem_ArenaIsInitialized( arena ) );
    REQUIRE( Mem_ArenaCapacity( arena ) == 0u );
    REQUIRE( BytesEqual( storage.data(), storage.size(), 0x6Du ) );
    REQUIRE( Mem_ArenaInit( arena, valid ) == mem_error_t::OK );
    REQUIRE( Mem_ArenaAlloc( arena, 1u ) != nullptr );
}

TEST_CASE( "Runtime arena rejects invalid and overflowing requests without losing live bytes",
           "[CypherMemory][Arena]" )
{
    alignas( 64 ) std::array<unsigned char, 256> storage{};
    arena_scope_t scope{};
    arena_t &arena = scope.arena;
    const arena_desc_t desc = ExternalArenaDesc( storage.data(), storage.size() );
    REQUIRE( Mem_ArenaInit( arena, desc ) == mem_error_t::OK );
    void *live = Mem_ArenaAlloc( arena, 16u, 1u );
    REQUIRE( live != nullptr );
    std::memset( live, 0x87u, 16u );
    const auto before = storage;
    REQUIRE( Mem_ArenaInit( arena, desc ) == mem_error_t::ERR_ALREADY_INITIALIZED );

    struct request_t { usize size; usize alignment; mem_error_t error; };
    const request_t requests[] = {
        { 0u, 1u, mem_error_t::ERR_INVALID_ARGUMENT },
        { 8u, 0u, mem_error_t::ERR_INVALID_ALIGNMENT },
        { 8u, 3u, mem_error_t::ERR_INVALID_ALIGNMENT },
        { 256u, 1u, mem_error_t::ERR_OUT_OF_MEMORY },
        { std::numeric_limits<usize>::max(), 1u, mem_error_t::ERR_INTEGER_OVERFLOW }
    };
    for ( const request_t &request : requests ) {
        CAPTURE( request.size, request.alignment );
        REQUIRE( Mem_ArenaAlloc( arena, request.size, request.alignment ) == nullptr );
        REQUIRE( Mem_ArenaLastError( arena ) == request.error );
        REQUIRE( Mem_ArenaUsed( arena ) == 16u );
        REQUIRE( Mem_ArenaRemaining( arena ) == storage.size() - 16u );
        REQUIRE( storage == before );
    }

    const usize overflowingCount = std::numeric_limits<usize>::max() / sizeof( usize ) + 1u;
    REQUIRE( Mem_ArenaAllocArray<usize>( arena, overflowingCount ) == nullptr );
    REQUIRE( Mem_ArenaLastError( arena ) == mem_error_t::ERR_INTEGER_OVERFLOW );
    REQUIRE( Mem_ArenaAllocArrayZero<usize>( arena, overflowingCount ) == nullptr );
    REQUIRE( Mem_ArenaLastError( arena ) == mem_error_t::ERR_INTEGER_OVERFLOW );
    REQUIRE( Mem_ArenaUsed( arena ) == 16u );
    REQUIRE( storage == before );
    REQUIRE( Mem_ArenaStats( arena ).nAllocationCount == 1u );
    REQUIRE( Mem_ArenaStats( arena ).nFailedAllocationCount == 7u );
    REQUIRE( Mem_ArenaAlloc( arena, 1u, 1u ) != nullptr );
    REQUIRE( Mem_ArenaLastError( arena ) == mem_error_t::OK );
    REQUIRE( BytesEqual( live, 16u, 0x87u ) );
}

TEST_CASE( "Runtime arena markers bound rewind and preserve earlier allocations",
           "[CypherMemory][Arena]" )
{
    alignas( 32 ) std::array<unsigned char, 256> storage{};
    arena_scope_t scope{};
    arena_t &arena = scope.arena;
    arena_desc_t desc = ExternalArenaDesc( storage.data(), storage.size() );
    SECTION( "Rewind preserves released bytes by default" ) {}
    SECTION( "Clear-on-reset also clears the rewound range" ) {
        desc.flags = CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_RESET;
    }
    REQUIRE( Mem_ArenaInit( arena, desc ) == mem_error_t::OK );
    void *prefix = Mem_ArenaAlloc( arena, 16u, 1u );
    REQUIRE( prefix != nullptr );
    std::memset( prefix, 0x28u, 16u );
    const arena_marker_t prefixMarker = Mem_ArenaGetMarker( arena );
    void *suffix = Mem_ArenaAlloc( arena, 48u, 1u );
    REQUIRE( suffix != nullptr );
    std::memset( suffix, 0x97u, 48u );
    const arena_marker_t laterMarker = Mem_ArenaGetMarker( arena );
    const auto before = storage;
    REQUIRE( Mem_ArenaRewind( arena, { laterMarker.used + 1u } ) == mem_error_t::ERR_INVALID_MARKER );
    REQUIRE( Mem_ArenaRewind( arena, { storage.size() + 1u } ) == mem_error_t::ERR_INVALID_MARKER );
    REQUIRE( Mem_ArenaUsed( arena ) == laterMarker.used );
    REQUIRE( storage == before );
    REQUIRE( Mem_ArenaRewind( arena, laterMarker ) == mem_error_t::OK );
    REQUIRE( Mem_ArenaRewind( arena, prefixMarker ) == mem_error_t::OK );
    REQUIRE( Mem_ArenaUsed( arena ) == prefixMarker.used );
    REQUIRE( Mem_ArenaStats( arena ).nPeakUsed == laterMarker.used );
    REQUIRE( BytesEqual( prefix, 16u, 0x28u ) );
    REQUIRE( Mem_ArenaRewind( arena, laterMarker ) == mem_error_t::ERR_INVALID_MARKER );

    // Inspect released bytes only after reacquiring the same linear range.
    void *reused = Mem_ArenaAlloc( arena, 48u, 1u );
    REQUIRE( reused == suffix );
    const unsigned char expected = desc.flags == CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_RESET ? 0u : 0x97u;
    REQUIRE( BytesEqual( reused, 48u, expected ) );
    REQUIRE( BytesEqual( prefix, 16u, 0x28u ) );
    Mem_ArenaReset( arena );
    REQUIRE( Mem_ArenaUsed( arena ) == 0u );
    REQUIRE( Mem_ArenaRewind( arena, prefixMarker ) == mem_error_t::ERR_INVALID_MARKER );
    REQUIRE( Mem_ArenaRewind( arena, {} ) == mem_error_t::OK );
}

TEST_CASE( "Runtime arena diagnostics begin fresh at initialization and counter reset",
           "[CypherMemory][Arena]" )
{
    alignas( 32 ) std::array<unsigned char, 256> storage{};
    arena_scope_t scope{};
    arena_t &arena = scope.arena;
    REQUIRE( Mem_ArenaAlloc( arena, 8u ) == nullptr );
    REQUIRE( Mem_ArenaLastError( arena ) == mem_error_t::ERR_NOT_INITIALIZED );
    REQUIRE( Mem_ArenaRewind( arena, {} ) == mem_error_t::ERR_NOT_INITIALIZED );
    usize traceCount = 0u;
    const arena_allocation_trace_t *traces = Mem_ArenaAllocationTraces( arena, traceCount );
    REQUIRE( traceCount == 1u );
    REQUIRE( traces[0].failed );
    REQUIRE( traces[0].nAllocationIndex == 1u );

    REQUIRE( Mem_ArenaInit( arena, ExternalArenaDesc( storage.data(), storage.size() ) ) == mem_error_t::OK );
    (void)Mem_ArenaAllocationTraces( arena, traceCount );
    REQUIRE( traceCount == 0u );
    REQUIRE( Mem_ArenaStats( arena ).nFailedAllocationCount == 0u );
    void *live = Mem_ArenaAllocDebug( arena, 16u, 1u, "arena-test", "allocate", 42 );
    REQUIRE( live != nullptr );
    std::memset( live, 0x64u, 16u );
    REQUIRE( Mem_ArenaAllocDebug( arena, 0u, 1u, "arena-test", "reject", 43 ) == nullptr );
    traces = Mem_ArenaAllocationTraces( arena, traceCount );
    REQUIRE( traceCount == 2u );
    REQUIRE( traces[0].ptr == live );
    REQUIRE( traces[0].nAllocationIndex == 1u );
    REQUIRE( traces[0].nUsedAfter == 16u );
    REQUIRE( traces[0].line == 42 );
    REQUIRE( std::strcmp( traces[0].file, "arena-test" ) == 0 );
    REQUIRE_FALSE( traces[0].failed );
    REQUIRE( traces[1].nAllocationIndex == 2u );
    REQUIRE( traces[1].error == mem_error_t::ERR_INVALID_ARGUMENT );
    REQUIRE( traces[1].failed );

    REQUIRE( Mem_ArenaAlloc( arena, 32u, 1u ) != nullptr );
    REQUIRE( Mem_ArenaRewind( arena, { 16u } ) == mem_error_t::OK );
    REQUIRE( Mem_ArenaStats( arena ).nPeakUsed == 48u );
    Mem_ArenaResetCounters( arena );
    const arena_stats_t reset = Mem_ArenaStats( arena );
    REQUIRE( reset.used == 16u );
    REQUIRE( reset.nPeakUsed == 16u );
    REQUIRE( reset.nAllocationCount == 0u );
    REQUIRE( reset.nFailedAllocationCount == 0u );
    REQUIRE( Mem_ArenaLastError( arena ) == mem_error_t::OK );
    (void)Mem_ArenaAllocationTraces( arena, traceCount );
    REQUIRE( traceCount == 0u );
    REQUIRE( BytesEqual( live, 16u, 0x64u ) );

    REQUIRE( Mem_ArenaAlloc( arena, 1u, 1u ) != nullptr );
    traces = Mem_ArenaAllocationTraces( arena, traceCount );
    REQUIRE( traceCount == 1u );
    REQUIRE( traces[0].nAllocationIndex == 1u );
    Mem_ArenaReset( arena );
    REQUIRE( Mem_ArenaUsed( arena ) == 0u );
    REQUIRE( Mem_ArenaStats( arena ).nAllocationCount == 1u );
    REQUIRE( Mem_ArenaStats( arena ).nPeakUsed == 17u );
}

TEST_CASE( "Runtime arena reset and shutdown clearing respect borrowed buffer boundaries",
           "[CypherMemory][Arena]" )
{
    alignas( 32 ) std::array<unsigned char, 128> storage{};
    storage.fill( 0xA5u );
    arena_scope_t scope{};
    arena_t &arena = scope.arena;
    arena_desc_t desc = ExternalArenaDesc( storage.data() + 16u, 96u );
    bool shutdown = false;
    SECTION( "Reset retains payload bytes by default" ) {}
    SECTION( "Reset clears used bytes" ) { desc.flags = CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_RESET; }
    SECTION( "Shutdown clears used bytes" ) {
        desc.flags = CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_SHUTDOWN;
        shutdown = true;
    }
    REQUIRE( Mem_ArenaInit( arena, desc ) == mem_error_t::OK );
    void *live = Mem_ArenaAlloc( arena, 24u, 1u );
    REQUIRE( live != nullptr );
    std::memset( live, 0x57u, 24u );
    if ( shutdown ) {
        Mem_ArenaShutdown( arena );
        REQUIRE_FALSE( Mem_ArenaIsInitialized( arena ) );
    } else {
        Mem_ArenaReset( arena );
        REQUIRE( Mem_ArenaIsInitialized( arena ) );
        REQUIRE( Mem_ArenaUsed( arena ) == 0u );
        REQUIRE( Mem_ArenaRemaining( arena ) == desc.capacity );
    }
    // These reads use the caller-owned array, whose lifetime does not end with the arena.
    const unsigned char expected = desc.flags == CYPHER_MEMORY_ARENA_FLAG_NONE ? 0x57u : 0u;
    REQUIRE( BytesEqual( storage.data() + 16u, 24u, expected ) );
    REQUIRE( BytesEqual( storage.data(), 16u, 0xA5u ) );
    REQUIRE( BytesEqual( storage.data() + 40u, storage.size() - 40u, 0xA5u ) );
}

TEST_CASE( "Runtime arena zero allocation clears only the requested live payload",
           "[CypherMemory][Arena]" )
{
    alignas( 64 ) std::array<unsigned char, 256> storage{};
    storage.fill( 0xA5u );
    arena_scope_t scope{};
    arena_t &arena = scope.arena;
    arena_desc_t desc = ExternalArenaDesc( storage.data() + 3u, 200u );
    SECTION( "Explicit zero allocation" ) {}
    SECTION( "Zero-on-allocation policy" ) { desc.flags = CYPHER_MEMORY_ARENA_FLAG_ZERO_ON_ALLOC; }
    REQUIRE( Mem_ArenaInit( arena, desc ) == mem_error_t::OK );
    for ( usize iteration = 0u; iteration < 3u; ++iteration ) {
        void *ptr = desc.flags == CYPHER_MEMORY_ARENA_FLAG_NONE
            ? Mem_ArenaAllocZero( arena, 13u, 32u ) : Mem_ArenaAlloc( arena, 13u, 32u );
        REQUIRE( ptr != nullptr );
        REQUIRE( reinterpret_cast<usize>( ptr ) % 32u == 0u );
        const usize offset = reinterpret_cast<usize>( ptr ) - reinterpret_cast<usize>( storage.data() );
        REQUIRE( offset <= storage.size() - 13u );
        REQUIRE( BytesEqual( ptr, 13u, 0u ) );
        REQUIRE( BytesEqual( storage.data(), offset, 0xA5u ) );
        REQUIRE( BytesEqual( storage.data() + offset + 13u, storage.size() - offset - 13u, 0xA5u ) );
        std::memset( ptr, 0xDCu, 13u );
        Mem_ArenaReset( arena );
    }
}

TEST_CASE( "Runtime arena heap backing supports aligned use and fresh lifetimes after shutdown",
           "[CypherMemory][Arena]" )
{
    arena_scope_t scope{};
    arena_t &arena = scope.arena;
    arena_desc_t desc{};
    desc.name = "heap-arena-tests";
    desc.capacity = 512u;
    desc.flags = CYPHER_MEMORY_ARENA_FLAG_ZERO_ON_ALLOC | CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_SHUTDOWN;
    for ( usize lifetime = 0u; lifetime < 3u; ++lifetime ) {
        REQUIRE( Mem_ArenaInit( arena, desc ) == mem_error_t::OK );
        REQUIRE( Mem_ArenaStats( arena ).committed == desc.capacity );
        REQUIRE( Mem_ArenaUsed( arena ) == 0u );
        void *live = Mem_ArenaAlloc( arena, 73u, 64u );
        REQUIRE( live != nullptr );
        REQUIRE( reinterpret_cast<usize>( live ) % 64u == 0u );
        REQUIRE( BytesEqual( live, 73u, 0u ) );
        std::memset( live, 0xCDu, 73u );
        REQUIRE( Mem_ArenaInit( arena, desc ) == mem_error_t::ERR_ALREADY_INITIALIZED );
        REQUIRE( BytesEqual( live, 73u, 0xCDu ) );
        Mem_ArenaShutdown( arena );
        REQUIRE_FALSE( Mem_ArenaIsInitialized( arena ) );
        REQUIRE( Mem_ArenaUsed( arena ) == 0u );
        REQUIRE( Mem_ArenaCapacity( arena ) == 0u );
        REQUIRE( Mem_ArenaStats( arena ).committed == 0u );
        REQUIRE_FALSE( Mem_ArenaContains( arena, live ) );
        Mem_ArenaShutdown( arena );
    }
}

TEST_CASE( "Runtime virtual arena grows commitment and restores its initial commit floor on reset",
           "[CypherMemory][Arena][VirtualMemory]" )
{
    const usize page = cypher::engine::sys::Sys_VirtualPageSize();
    REQUIRE( page > 0u );
    REQUIRE( Mem_IsPowerOfTwo( page ) );
    arena_scope_t scope{};
    arena_t &arena = scope.arena;
    arena_desc_t desc{};
    desc.name = "growing-virtual-arena-tests";
    desc.backing = arena_backing_t::ARENA_VIRTUAL_MEMORY;
    desc.capacity = 4u * page - 1u;
    desc.flags = CYPHER_MEMORY_ARENA_FLAG_GROW_COMMIT_ON_ALLOC |
                 CYPHER_MEMORY_ARENA_FLAG_DECOMMIT_ON_RESET |
                 CYPHER_MEMORY_ARENA_FLAG_CLEAR_ON_RESET;
    usize initialCommit = 0u;
    SECTION( "Start fully reserved and uncommitted" ) {}
    SECTION( "Round an initial commit request upward to whole pages" ) {
        desc.initialCommit = page + 1u;
        initialCommit = 2u * page;
    }
    REQUIRE( Mem_ArenaInit( arena, desc ) == mem_error_t::OK );
    REQUIRE( Mem_ArenaCapacity( arena ) == 4u * page );
    REQUIRE( Mem_ArenaStats( arena ).committed == initialCommit );
    REQUIRE( Mem_ArenaStats( arena ).initialCommit == initialCommit );
    for ( usize iteration = 0u; iteration < 3u; ++iteration ) {
        const usize size = 2u * page + 17u;
        void *live = Mem_ArenaAlloc( arena, size, page );
        REQUIRE( live != nullptr );
        REQUIRE( reinterpret_cast<usize>( live ) % page == 0u );
        REQUIRE( Mem_ArenaStats( arena ).committed == 3u * page );
        REQUIRE( Mem_ArenaUsed( arena ) == size );
        if ( iteration > 0u ) {
            REQUIRE( BytesEqual( live, size, 0u ) );
        }
        std::memset( live, 0xBAu, size );
        REQUIRE( BytesEqual( live, size, 0xBAu ) );
        // Seed a historical diagnostic; this does not inject an OS failure.
        // A later successful reset must report its own result.
        arena.lastError = mem_error_t::ERR_MEMORY_DECOMMIT;
        Mem_ArenaReset( arena );
        REQUIRE( Mem_ArenaLastError( arena ) == mem_error_t::OK );
        REQUIRE( Mem_ArenaUsed( arena ) == 0u );
        REQUIRE( Mem_ArenaStats( arena ).committed == initialCommit );
        REQUIRE( Mem_ArenaRemaining( arena ) == 4u * page );
        // No reads through the invalidated/decommitted pointer after reset.
        arena.lastError = mem_error_t::ERR_MEMORY_DECOMMIT;
        Mem_ArenaReset( arena );
        REQUIRE( Mem_ArenaLastError( arena ) == mem_error_t::OK );
        REQUIRE( Mem_ArenaUsed( arena ) == 0u );
        REQUIRE( Mem_ArenaStats( arena ).committed == initialCommit );
    }
    Mem_ArenaShutdown( arena );
    REQUIRE_FALSE( Mem_ArenaIsInitialized( arena ) );
    REQUIRE( Mem_ArenaStats( arena ).committed == 0u );
}

TEST_CASE( "Runtime virtual arena enforces a fixed commit limit and defaults to full commitment",
           "[CypherMemory][Arena][VirtualMemory]" )
{
    const usize page = cypher::engine::sys::Sys_VirtualPageSize();
    REQUIRE( page > 0u );
    arena_scope_t scope{};
    arena_t &arena = scope.arena;
    arena_desc_t desc{};
    desc.name = "fixed-virtual-arena-tests";
    desc.backing = arena_backing_t::ARENA_VIRTUAL_MEMORY;
    desc.capacity = 2u * page;
    usize committed = 2u * page;
    SECTION( "No initial limit commits the full reservation" ) {}
    SECTION( "Explicit initial commitment is a hard limit without growth" ) {
        desc.initialCommit = 1u;
        committed = page;
    }
    REQUIRE( Mem_ArenaInit( arena, desc ) == mem_error_t::OK );
    REQUIRE( Mem_ArenaStats( arena ).committed == committed );
    void *live = Mem_ArenaAlloc( arena, committed, page );
    REQUIRE( live != nullptr );
    std::memset( live, 0x4Eu, committed );
    const arena_stats_t before = Mem_ArenaStats( arena );
    REQUIRE( Mem_ArenaAlloc( arena, 1u, 1u ) == nullptr );
    const mem_error_t expected = committed == desc.capacity ? mem_error_t::ERR_OUT_OF_MEMORY : mem_error_t::ERR_MEMORY_COMMIT;
    REQUIRE( Mem_ArenaLastError( arena ) == expected );
    REQUIRE( Mem_ArenaStats( arena ).used == before.used );
    REQUIRE( Mem_ArenaStats( arena ).committed == before.committed );
    REQUIRE( Mem_ArenaStats( arena ).nAllocationCount == before.nAllocationCount );
    REQUIRE( Mem_ArenaStats( arena ).nFailedAllocationCount == before.nFailedAllocationCount + 1u );
    REQUIRE( BytesEqual( live, committed, 0x4Eu ) );
    Mem_ArenaReset( arena );
    REQUIRE( Mem_ArenaLastError( arena ) == mem_error_t::OK );
    REQUIRE( Mem_ArenaStats( arena ).committed == committed );
    REQUIRE( Mem_ArenaAlloc( arena, committed, page ) != nullptr );
}
