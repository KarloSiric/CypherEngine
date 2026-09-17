//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherMemory/CypherMemory_Pool_Tests.cpp
//  Purpose: Tests the runtime fixed-slot allocator over borrowed backing.
//  Details: Protects buffer boundaries, alignment, capacity, rejected operations,
//           reset, zero allocation, and live payload integrity across slot reuse.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMemory_Pool.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

using namespace cypher::engine::memory;
using cypher::engine::common::u32;
using cypher::engine::common::usize;

namespace
{

struct pool_scope_t {
    pool_t pool{};

    ~pool_scope_t()
    {
        Mem_PoolShutdown( pool );
    }
};

struct arena_scope_t {
    arena_t arena{};

    ~arena_scope_t()
    {
        Mem_ArenaShutdown( arena );
    }
};

pool_desc_t ExternalPoolDesc( void *buffer, usize bufferSize, usize slotSize,
                              usize slotCount, usize alignment = 16u )
{
    pool_desc_t desc{};
    desc.name = "runtime-pool-tests";
    desc.pExternalBuffer = buffer;
    desc.nExternalBufferSize = bufferSize;
    desc.nSlotSize = slotSize;
    desc.nSlotCount = slotCount;
    desc.alignment = alignment;
    desc.backing = pool_backing_t::POOL_EXTERNAL_BUFFER;
    return desc;
}

bool BytesEqual( const void *data, usize size, unsigned char value )
{
    const auto *bytes = static_cast<const unsigned char *>( data );
    return std::all_of( bytes, bytes + size,
                        [value]( unsigned char byte ) { return byte == value; } );
}

struct live_slot_t {
    void *ptr{ nullptr };
    unsigned char value{ 0u };
};

template <usize Count>
bool LivePayloadsMatch( const std::array<live_slot_t, Count> &slots, usize slotSize )
{
    for ( const live_slot_t &slot : slots ) {
        if ( slot.ptr != nullptr && !BytesEqual( slot.ptr, slotSize, slot.value ) ) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE( "Runtime pool aligns inside borrowed storage and preserves surrounding bytes",
           "[CypherMemory][Pool]" )
{
    alignas( 64 ) std::array<unsigned char, 1024> storage{};
    storage.fill( 0xA5u );
    pool_scope_t scope{};
    pool_t &pool = scope.pool;
    pool_desc_t desc = ExternalPoolDesc( storage.data() + 3u, 960u, 23u, 7u, 32u );

    SECTION( "Ordinary shutdown preserves caller storage" ) {}
    SECTION( "Clearing shutdown scrubs only managed backing" )
    {
        desc.flags = CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_SHUTDOWN;
    }

    REQUIRE( Mem_PoolInit( pool, desc ) == mem_error_t::OK );
    REQUIRE( Mem_PoolIsInitialized( pool ) );
    REQUIRE( Mem_PoolCapacity( pool ) == 7u );
    std::array<void *, 7> slots{};
    usize firstAddress = std::numeric_limits<usize>::max();
    for ( usize i = 0u; i < slots.size(); ++i ) {
        slots[i] = Mem_PoolAlloc( pool );
        REQUIRE( slots[i] != nullptr );
        const usize address = reinterpret_cast<usize>( slots[i] );
        const usize externalAddress = reinterpret_cast<usize>( desc.pExternalBuffer );
        REQUIRE( address >= externalAddress );
        REQUIRE( address - externalAddress <= desc.nExternalBufferSize - desc.nSlotSize );
        REQUIRE( address % desc.alignment == 0u );
        REQUIRE( Mem_PoolContains( pool, slots[i] ) );
        REQUIRE( Mem_PoolOwnsSlot( pool, slots[i] ) );
        for ( usize j = 0u; j < i; ++j ) {
            REQUIRE( slots[i] != slots[j] );
        }
        firstAddress = std::min( firstAddress, address );
        std::memset( slots[i], static_cast<int>( i + 1u ), desc.nSlotSize );
    }

    REQUIRE( Mem_PoolAlloc( pool ) == nullptr );
    REQUIRE( Mem_PoolLastError( pool ) == mem_error_t::ERR_OUT_OF_MEMORY );
    REQUIRE( Mem_PoolUsedCount( pool ) == slots.size() );
    REQUIRE( Mem_PoolFreeCount( pool ) == 0u );
    REQUIRE( Mem_PoolUsageRatio( pool ) == 1.0f );
    for ( usize i = 0u; i < slots.size(); ++i ) {
        REQUIRE( BytesEqual( slots[i], desc.nSlotSize, static_cast<unsigned char>( i + 1u ) ) );
        REQUIRE( Mem_PoolFree( pool, slots[i] ) == mem_error_t::OK );
    }

    // Refill to prove every released slot remains usable, without requiring a free-list order.
    for ( usize i = 0u; i < slots.size(); ++i ) {
        slots[i] = Mem_PoolAlloc( pool );
        REQUIRE( slots[i] != nullptr );
        for ( usize j = 0u; j < i; ++j ) {
            REQUIRE( slots[i] != slots[j] );
        }
    }
    REQUIRE( Mem_PoolAlloc( pool ) == nullptr );

    const pool_stats_t stats = Mem_PoolStats( pool );
    const usize managedBegin = firstAddress - reinterpret_cast<usize>( storage.data() );
    REQUIRE( managedBegin >= 3u );
    REQUIRE( managedBegin <= 963u );
    REQUIRE( stats.nBackingBytes <= 963u - managedBegin );
    const usize managedEnd = managedBegin + stats.nBackingBytes;
    REQUIRE( BytesEqual( storage.data(), managedBegin, 0xA5u ) );
    REQUIRE( BytesEqual( storage.data() + managedEnd, storage.size() - managedEnd, 0xA5u ) );
    const auto beforeShutdown = storage;

    Mem_PoolShutdown( pool );
    REQUIRE_FALSE( Mem_PoolIsInitialized( pool ) );
    REQUIRE( Mem_PoolCapacity( pool ) == 0u );
    REQUIRE( Mem_PoolUsedCount( pool ) == 0u );
    if ( desc.flags == CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_SHUTDOWN ) {
        REQUIRE( BytesEqual( storage.data() + managedBegin, stats.nBackingBytes, 0u ) );
        REQUIRE( BytesEqual( storage.data(), managedBegin, 0xA5u ) );
        REQUIRE( BytesEqual( storage.data() + managedEnd, storage.size() - managedEnd, 0xA5u ) );
    } else {
        REQUIRE( storage == beforeShutdown );
    }

    // The caller still owns the array and may reuse it after shutdown.
    storage.fill( 0x39u );
    REQUIRE( Mem_PoolInit( pool, desc ) == mem_error_t::OK );
    REQUIRE( Mem_PoolAlloc( pool ) != nullptr );
}

TEST_CASE( "Runtime pool rejects invalid frees without damaging live slots or capacity",
           "[CypherMemory][Pool]" )
{
    alignas( 32 ) std::array<unsigned char, 512> storage{};
    pool_scope_t scope{};
    pool_t &pool = scope.pool;
    REQUIRE( Mem_PoolInit( pool, ExternalPoolDesc( storage.data(), storage.size(), 32u, 4u ) ) == mem_error_t::OK );
    std::array<void *, 4> slots{};
    for ( usize i = 0u; i < slots.size(); ++i ) {
        slots[i] = Mem_PoolAlloc( pool );
        REQUIRE( slots[i] != nullptr );
        std::memset( slots[i], static_cast<int>( 0x40u + i ), 32u );
    }

    unsigned char foreign[32]{};
    auto *interior = static_cast<unsigned char *>( slots[0] ) + 1u;
    REQUIRE( Mem_PoolContains( pool, interior ) );
    REQUIRE_FALSE( Mem_PoolOwnsSlot( pool, interior ) );
    REQUIRE_FALSE( Mem_PoolContains( pool, foreign ) );
    REQUIRE( Mem_PoolFree( pool, foreign ) == mem_error_t::ERR_INVALID_POINTER );
    REQUIRE( Mem_PoolFree( pool, interior ) == mem_error_t::ERR_INVALID_POINTER );
    REQUIRE( Mem_PoolFree( pool, nullptr ) == mem_error_t::ERR_INVALID_POINTER );
    REQUIRE( Mem_PoolFree( pool, slots[1] ) == mem_error_t::OK );
    REQUIRE( Mem_PoolFree( pool, slots[1] ) == mem_error_t::ERR_DOUBLE_FREE );
    REQUIRE( Mem_PoolLastError( pool ) == mem_error_t::ERR_DOUBLE_FREE );

    const pool_stats_t stats = Mem_PoolStats( pool );
    REQUIRE( stats.nUsedCount == 3u );
    REQUIRE( stats.nFreeCount == 1u );
    REQUIRE( stats.nFailedFreeCount == 4u );
    REQUIRE( stats.nFreeOperationCount == 1u );
    for ( usize i : { 0u, 2u, 3u } ) {
        REQUIRE( BytesEqual( slots[i], 32u, static_cast<unsigned char>( 0x40u + i ) ) );
    }
    // Exactly one slot is free, so its identity is independent of allocation order.
    REQUIRE( Mem_PoolAlloc( pool ) == slots[1] );
    REQUIRE( Mem_PoolAlloc( pool ) == nullptr );
    REQUIRE( Mem_PoolUsedCount( pool ) == 4u );
    for ( void *slot : slots ) {
        REQUIRE( Mem_PoolFree( pool, slot ) == mem_error_t::OK );
    }
    REQUIRE( Mem_PoolFreeCount( pool ) == 4u );
}

TEST_CASE( "Runtime pool borrows an arena without rewinding it or damaging adjacent allocations",
           "[CypherMemory][Pool][Arena]" )
{
    alignas( 64 ) std::array<unsigned char, 1024> storage{};
    storage.fill( 0xA5u );
    arena_scope_t parent{};
    arena_t &arena = parent.arena;
    arena_desc_t arenaDesc{};
    arenaDesc.name = "runtime-pool-parent";
    arenaDesc.backing = arena_backing_t::ARENA_EXTERNAL_BUFFER;
    arenaDesc.pExternalBuffer = storage.data() + 64u;
    arenaDesc.capacity = storage.size() - 128u;
    REQUIRE( Mem_ArenaInit( arena, arenaDesc ) == mem_error_t::OK );
    void *beforePool = Mem_ArenaAlloc( arena, 13u );
    REQUIRE( beforePool != nullptr );
    std::memset( beforePool, 0x31u, 13u );

    // Declared after its parent so failure unwinding shuts down the pool first.
    pool_scope_t scope{};
    pool_desc_t desc{};
    desc.name = "arena-backed-runtime-pool";
    desc.arena = &arena;
    desc.nSlotSize = 32u;
    desc.nSlotCount = 4u;
    desc.alignment = 64u;
    desc.flags = CYPHER_MEMORY_POOL_FLAG_CLEAR_ON_SHUTDOWN;
    REQUIRE( Mem_PoolInit( scope.pool, desc ) == mem_error_t::OK );
    for ( usize i = 0u; i < desc.nSlotCount; ++i ) {
        void *slot = Mem_PoolAlloc( scope.pool );
        REQUIRE( slot != nullptr );
        REQUIRE( reinterpret_cast<usize>( slot ) % desc.alignment == 0u );
        std::memset( slot, 0x57u, desc.nSlotSize );
    }

    void *afterPool = Mem_ArenaAlloc( arena, 37u );
    REQUIRE( afterPool != nullptr );
    std::memset( afterPool, 0x93u, 37u );
    const usize remaining = Mem_ArenaStats( arena ).remaining;
    REQUIRE( remaining > 0u );
    void *tail = Mem_ArenaAlloc( arena, remaining, 1u );
    REQUIRE( tail != nullptr );
    std::memset( tail, 0xE2u, remaining );
    const arena_stats_t full = Mem_ArenaStats( arena );
    REQUIRE( full.remaining == 0u );
    const auto beforeFailure = storage;

    pool_scope_t rejected{};
    REQUIRE( Mem_PoolInit( rejected.pool, desc ) == mem_error_t::ERR_OUT_OF_MEMORY );
    REQUIRE_FALSE( Mem_PoolIsInitialized( rejected.pool ) );
    REQUIRE( Mem_ArenaStats( arena ).used == full.used );
    REQUIRE( Mem_ArenaStats( arena ).nAllocationCount == full.nAllocationCount );
    REQUIRE( Mem_ArenaStats( arena ).nFailedAllocationCount == full.nFailedAllocationCount + 1u );
    REQUIRE( storage == beforeFailure );

    Mem_PoolShutdown( scope.pool );
    REQUIRE_FALSE( Mem_PoolIsInitialized( scope.pool ) );
    REQUIRE( Mem_ArenaIsInitialized( arena ) );
    REQUIRE( Mem_ArenaStats( arena ).used == full.used );
    REQUIRE( Mem_ArenaStats( arena ).capacity == full.capacity );
    REQUIRE( BytesEqual( beforePool, 13u, 0x31u ) );
    REQUIRE( BytesEqual( afterPool, 37u, 0x93u ) );
    REQUIRE( BytesEqual( tail, remaining, 0xE2u ) );
    REQUIRE( BytesEqual( storage.data(), 64u, 0xA5u ) );
    REQUIRE( BytesEqual( storage.data() + storage.size() - 64u, 64u, 0xA5u ) );
}

TEST_CASE( "Runtime pool descriptor failures leave borrowed storage untouched and allow retry",
           "[CypherMemory][Pool]" )
{
    alignas( 64 ) std::array<unsigned char, 512> storage{};
    storage.fill( 0x6Du );
    pool_scope_t scope{};
    pool_t &pool = scope.pool;
    const pool_desc_t valid = ExternalPoolDesc( storage.data(), storage.size(), 32u, 4u );
    pool_desc_t invalid = valid;
    mem_error_t expected = mem_error_t::ERR_INVALID_ARGUMENT;

    SECTION( "Zero payload" ) { invalid.nSlotSize = 0u; }
    SECTION( "Zero slots" ) {
        invalid.nSlotCount = 0u;
        expected = mem_error_t::ERR_INVALID_CAPACITY;
    }
    SECTION( "Zero alignment" ) {
        invalid.alignment = 0u;
        expected = mem_error_t::ERR_INVALID_ALIGNMENT;
    }
    SECTION( "Non-power-of-two alignment" ) {
        invalid.alignment = 3u;
        expected = mem_error_t::ERR_INVALID_ALIGNMENT;
    }
    SECTION( "Missing external storage" ) {
        invalid.pExternalBuffer = nullptr;
        expected = mem_error_t::ERR_EXTERNAL_BUFFER_REQUIRED;
    }
    SECTION( "Empty external storage" ) {
        invalid.nExternalBufferSize = 0u;
        expected = mem_error_t::ERR_BUFFER_TOO_SMALL;
    }
    SECTION( "Storage consumed by alignment padding" ) {
        invalid.pExternalBuffer = storage.data() + 1u;
        invalid.nExternalBufferSize = 1u;
        invalid.alignment = 64u;
        expected = mem_error_t::ERR_BUFFER_TOO_SMALL;
    }
    SECTION( "Storage fits payloads but lacks bookkeeping space" ) {
        invalid.nExternalBufferSize = invalid.nSlotSize * invalid.nSlotCount;
        expected = mem_error_t::ERR_BUFFER_TOO_SMALL;
    }
    SECTION( "Payload alignment overflow" ) {
        invalid.nSlotSize = std::numeric_limits<usize>::max();
        expected = mem_error_t::ERR_INTEGER_OVERFLOW;
    }
    SECTION( "Slot count multiplication overflow" ) {
        invalid.nSlotCount = std::numeric_limits<usize>::max() / invalid.nSlotSize + 1u;
        expected = mem_error_t::ERR_INTEGER_OVERFLOW;
    }
    SECTION( "Backing size overflow after slot storage" ) {
        invalid.nSlotSize = std::numeric_limits<usize>::max() - 7u;
        invalid.nSlotCount = 1u;
        invalid.alignment = 8u;
        expected = mem_error_t::ERR_INTEGER_OVERFLOW;
    }
    SECTION( "Invalid backing selection" ) {
        invalid.backing = static_cast<pool_backing_t>( 255u );
    }

    REQUIRE( Mem_PoolInit( pool, invalid ) == expected );
    REQUIRE( Mem_PoolLastError( pool ) == expected );
    REQUIRE_FALSE( Mem_PoolIsInitialized( pool ) );
    REQUIRE( Mem_PoolCapacity( pool ) == 0u );
    REQUIRE( Mem_PoolUsedCount( pool ) == 0u );
    REQUIRE( BytesEqual( storage.data(), storage.size(), 0x6Du ) );
    REQUIRE( Mem_PoolInit( pool, valid ) == mem_error_t::OK );
    REQUIRE( Mem_PoolAlloc( pool ) != nullptr );
}

TEST_CASE( "Runtime pool rejects incompatible allocations and repeated initialization transactionally",
           "[CypherMemory][Pool]" )
{
    alignas( 32 ) std::array<unsigned char, 512> storage{};
    pool_scope_t scope{};
    pool_t &pool = scope.pool;
    const pool_desc_t desc = ExternalPoolDesc( storage.data(), storage.size(), 24u, 2u, 32u );
    REQUIRE( Mem_PoolInit( pool, desc ) == mem_error_t::OK );
    void *live = Mem_PoolAlloc( pool );
    REQUIRE( live != nullptr );
    std::memset( live, 0x5Cu, 24u );
    const auto beforeRejectedInit = storage;
    REQUIRE( Mem_PoolInit( pool, desc ) == mem_error_t::ERR_ALREADY_INITIALIZED );
    REQUIRE( storage == beforeRejectedInit );

    struct rejected_request_t { usize size; usize alignment; mem_error_t error; };
    const rejected_request_t requests[] = {
        { 0u, 16u, mem_error_t::ERR_INVALID_ARGUMENT },
        { 25u, 16u, mem_error_t::ERR_BUFFER_TOO_SMALL },
        { 8u, 0u, mem_error_t::ERR_INVALID_ALIGNMENT },
        { 8u, 3u, mem_error_t::ERR_INVALID_ALIGNMENT },
        { 8u, 64u, mem_error_t::ERR_INVALID_ALIGNMENT }
    };
    for ( const rejected_request_t &request : requests ) {
        CAPTURE( request.size, request.alignment );
        REQUIRE( Mem_PoolAllocSize( pool, request.size, request.alignment ) == nullptr );
        REQUIRE( Mem_PoolLastError( pool ) == request.error );
        REQUIRE( Mem_PoolUsedCount( pool ) == 1u );
        REQUIRE( Mem_PoolFreeCount( pool ) == 1u );
        REQUIRE( BytesEqual( live, 24u, 0x5Cu ) );
    }
    const pool_stats_t stats = Mem_PoolStats( pool );
    REQUIRE( stats.nAllocationCount == 1u );
    REQUIRE( stats.nFailedAllocationCount == 5u );
    void *second = Mem_PoolAllocSize( pool, 24u, 32u );
    REQUIRE( second != nullptr );
    REQUIRE( second != live );
    REQUIRE( Mem_PoolLastError( pool ) == mem_error_t::OK );
    REQUIRE( Mem_PoolAlloc( pool ) == nullptr );
    REQUIRE( BytesEqual( live, 24u, 0x5Cu ) );
}

TEST_CASE( "Runtime pool counter reset preserves allocations and bulk reset restores capacity",
           "[CypherMemory][Pool]" )
{
    alignas( 32 ) std::array<unsigned char, 512> storage{};
    pool_scope_t scope{};
    pool_t &pool = scope.pool;
    REQUIRE( Mem_PoolInit( pool, ExternalPoolDesc( storage.data(), storage.size(), 32u, 4u ) ) == mem_error_t::OK );
    std::array<void *, 3> slots{};
    for ( void *&slot : slots ) {
        slot = Mem_PoolAlloc( pool );
        REQUIRE( slot != nullptr );
        std::memset( slot, 0x72u, 32u );
    }
    REQUIRE( Mem_PoolFree( pool, slots[1] ) == mem_error_t::OK );
    REQUIRE( Mem_PoolAllocSize( pool, 33u ) == nullptr );
    REQUIRE( Mem_PoolFree( pool, nullptr ) == mem_error_t::ERR_INVALID_POINTER );
    pool_stats_t stats = Mem_PoolStats( pool );
    REQUIRE( stats.nAllocationCount == 3u );
    REQUIRE( stats.nFreeOperationCount == 1u );
    REQUIRE( stats.nFailedAllocationCount == 1u );
    REQUIRE( stats.nFailedFreeCount == 1u );
    REQUIRE( stats.nPeakUsedCount == 3u );

    Mem_PoolResetCounters( pool );
    stats = Mem_PoolStats( pool );
    REQUIRE( stats.nUsedCount == 2u );
    REQUIRE( stats.nFreeCount == 2u );
    REQUIRE( stats.nPeakUsedCount == 2u );
    REQUIRE( stats.nAllocationCount == 0u );
    REQUIRE( stats.nFreeOperationCount == 0u );
    REQUIRE( stats.nFailedAllocationCount == 0u );
    REQUIRE( stats.nFailedFreeCount == 0u );
    REQUIRE( Mem_PoolUsageRatio( pool ) == 0.5f );
    REQUIRE( BytesEqual( slots[0], 32u, 0x72u ) );
    REQUIRE( BytesEqual( slots[2], 32u, 0x72u ) );

    REQUIRE( Mem_PoolAlloc( pool ) != nullptr );
    Mem_PoolReset( pool );
    stats = Mem_PoolStats( pool );
    REQUIRE( stats.nUsedCount == 0u );
    REQUIRE( stats.nFreeCount == 4u );
    REQUIRE( stats.nAllocationCount == 1u );
    REQUIRE( stats.nFreeOperationCount == 0u );
    REQUIRE( stats.nPeakUsedCount == 3u );
    REQUIRE( Mem_PoolUsageRatio( pool ) == 0.0f );
    REQUIRE( Mem_PoolFree( pool, slots[0] ) == mem_error_t::ERR_DOUBLE_FREE );

    std::array<void *, 4> afterReset{};
    for ( usize i = 0u; i < afterReset.size(); ++i ) {
        afterReset[i] = Mem_PoolAlloc( pool );
        REQUIRE( afterReset[i] != nullptr );
        for ( usize j = 0u; j < i; ++j ) {
            REQUIRE( afterReset[i] != afterReset[j] );
        }
    }
    REQUIRE( Mem_PoolAlloc( pool ) == nullptr );
}

TEST_CASE( "Runtime pool zero allocation clears reused live payloads",
           "[CypherMemory][Pool]" )
{
    alignas( 32 ) std::array<unsigned char, 256> storage{};
    storage.fill( 0xA5u );
    pool_scope_t scope{};
    pool_t &pool = scope.pool;
    pool_desc_t desc = ExternalPoolDesc( storage.data(), storage.size(), 32u, 1u );
    usize requestedSize = desc.nSlotSize;
    bool explicitZero = true;
    SECTION( "Explicit whole-slot zero allocation" ) {}
    SECTION( "Explicit sized zero allocation" ) { requestedSize = 13u; }
    SECTION( "Zero-on-allocation policy" ) {
        desc.flags = CYPHER_MEMORY_POOL_FLAG_ZERO_ON_ALLOC;
        explicitZero = false;
    }

    REQUIRE( Mem_PoolInit( pool, desc ) == mem_error_t::OK );
    for ( usize iteration = 0u; iteration < 3u; ++iteration ) {
        void *slot = !explicitZero ? Mem_PoolAlloc( pool ) :
            requestedSize == desc.nSlotSize ? Mem_PoolAllocZero( pool ) :
            Mem_PoolAllocSizeZero( pool, requestedSize );
        REQUIRE( slot != nullptr );
        REQUIRE( BytesEqual( slot, requestedSize, 0u ) );
        std::memset( slot, 0xD7u, requestedSize );
        REQUIRE( Mem_PoolFree( pool, slot ) == mem_error_t::OK );
    }
}

TEST_CASE( "Runtime pool bitmap boundaries and mixed reuse preserve the live payload model",
           "[CypherMemory][Pool]" )
{
    constexpr usize slotCount = 130u;
    constexpr usize slotSize = 24u;
    alignas( 32 ) std::array<unsigned char, 8192> storage{};
    pool_scope_t scope{};
    pool_t &pool = scope.pool;
    REQUIRE( Mem_PoolInit( pool, ExternalPoolDesc( storage.data(), storage.size(), slotSize, slotCount ) ) == mem_error_t::OK );
    std::array<live_slot_t, slotCount> model{};
    usize liveCount = 0u;
    u32 generation = 0u;

    auto allocate = [&]() {
        void *slot = Mem_PoolAlloc( pool );
        REQUIRE( slot != nullptr );
        const usize index = Mem_PoolSlotIndex( pool, slot );
        REQUIRE( index < model.size() );
        REQUIRE( model[index].ptr == nullptr );
        for ( const live_slot_t &live : model ) {
            REQUIRE( live.ptr != slot );
        }
        const unsigned char value = static_cast<unsigned char>( ++generation % 251u + 1u );
        model[index] = { slot, value };
        std::memset( slot, value, slotSize );
        ++liveCount;
    };
    auto release = [&]( usize index ) {
        REQUIRE( model[index].ptr != nullptr );
        REQUIRE( BytesEqual( model[index].ptr, slotSize, model[index].value ) );
        REQUIRE( Mem_PoolFree( pool, model[index].ptr ) == mem_error_t::OK );
        model[index] = {};
        --liveCount;
    };

    for ( usize i = 0u; i < slotCount; ++i ) {
        allocate();
    }
    REQUIRE( Mem_PoolAlloc( pool ) == nullptr );
    REQUIRE( LivePayloadsMatch( model, slotSize ) );
    for ( usize index : { 0u, 63u, 64u, 127u, 128u, 129u } ) {
        CAPTURE( index );
        void *released = model[index].ptr;
        release( index );
        REQUIRE( Mem_PoolFree( pool, released ) == mem_error_t::ERR_DOUBLE_FREE );
        REQUIRE( LivePayloadsMatch( model, slotSize ) );
    }
    while ( liveCount < slotCount ) {
        allocate();
    }

    // A fixed seed gives repeatable churn; expected liveness never reads allocator metadata.
    u32 random = 0xC0FFEEu;
    for ( usize step = 0u; step < 384u; ++step ) {
        CAPTURE( step );
        random = random * 1664525u + 1013904223u;
        const usize index = random % slotCount;
        if ( model[index].ptr != nullptr ) {
            release( index );
        } else {
            allocate();
        }
        REQUIRE( Mem_PoolUsedCount( pool ) == liveCount );
        REQUIRE( Mem_PoolFreeCount( pool ) == slotCount - liveCount );
        REQUIRE( LivePayloadsMatch( model, slotSize ) );
    }
    for ( usize i = 0u; i < slotCount; ++i ) {
        if ( model[i].ptr != nullptr ) {
            release( i );
        }
    }
    REQUIRE( Mem_PoolUsedCount( pool ) == 0u );
    REQUIRE( Mem_PoolFreeCount( pool ) == slotCount );
    REQUIRE( Mem_PoolStats( pool ).nPeakUsedCount == slotCount );
    for ( usize i = 0u; i < slotCount; ++i ) {
        allocate();
    }
    REQUIRE( Mem_PoolAlloc( pool ) == nullptr );
    REQUIRE( LivePayloadsMatch( model, slotSize ) );
}
