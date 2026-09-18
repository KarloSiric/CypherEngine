//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Scratch_Tests.cpp
//  Purpose: Verifies bounded geometry-operation scratch storage.
//  Details: Covers local and fallback acquisition, budgets, alignment, typed
//           overflow, transactional failure, marker rewind, and canonical release.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Scratch.h"

#include <catch2/catch_test_macros.hpp>

#include <new>

namespace cypher::editor::geometry
{

namespace
{

struct scratch_allocator_state_t {
    u32 cAllocateCalls{ 0u };
    u32 cFreeCalls{ 0u };
    bool_t bFailAllocation{ common::CY_FALSE };
};

usize EffectiveAlignment( usize nAlignment ) noexcept
{
    return nAlignment < common::CY_ALLOCATOR_DEFAULT_ALIGNMENT
        ? common::CY_ALLOCATOR_DEFAULT_ALIGNMENT
        : nAlignment;
}

void *ScratchAllocate(
    void *pUserData,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<scratch_allocator_state_t *>( pUserData );
    ++pState->cAllocateCalls;
    if ( pState->bFailAllocation ) {
        return nullptr;
    }

    return ::operator new(
        cbSize,
        static_cast<std::align_val_t>( EffectiveAlignment( nAlignment ) ),
        std::nothrow );
}

void ScratchFree(
    void *pUserData,
    void *pMemory,
    usize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<scratch_allocator_state_t *>( pUserData );
    ++pState->cFreeCalls;
    ::operator delete(
        pMemory,
        static_cast<std::align_val_t>( EffectiveAlignment( nAlignment ) ) );
}

allocator_t MakeScratchAllocator( scratch_allocator_state_t *pState ) noexcept
{
    return { ScratchAllocate, nullptr, ScratchFree, pState };
}

geometry_scratch_stats_t QueryStats( const geometry_scratch_t &scratch )
{
    geometry_scratch_stats_t stats{};
    REQUIRE( GeometryScratch_QueryStats( &scratch, &stats ) ==
             geometry_status_t::OK );
    return stats;
}

} // namespace

TEST_CASE( "geometry scratch uses aligned local storage first",
           "[editor][geometry][core][scratch]" )
{
    alignas( 64 ) common::byte local[256]{};
    scratch_allocator_state_t allocatorState{};
    const allocator_t allocator = MakeScratchAllocator( &allocatorState );
    geometry_scratch_t scratch{};

    const geometry_scratch_desc_t desc{
        { local + 1u, sizeof( local ) - 1u },
        &allocator,
        128u,
        192u,
        32u
    };
    REQUIRE( GeometryScratch_Acquire( &scratch, desc ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryScratch_IsValid( &scratch ) );
    REQUIRE( GeometryScratch_IsInitialized( &scratch ) );

    const geometry_scratch_stats_t stats = QueryStats( scratch );
    REQUIRE( stats.cbBudget == 192u );
    REQUIRE( stats.cbCapacity == 128u );
    REQUIRE( stats.bUsesLocalStorage );
    REQUIRE_FALSE( stats.bUsesFallbackAllocation );
    REQUIRE( allocatorState.cAllocateCalls == 0u );

    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "geometry scratch falls back and releases allocator ownership",
           "[editor][geometry][core][scratch]" )
{
    common::byte local[8]{};
    scratch_allocator_state_t allocatorState{};
    const allocator_t allocator = MakeScratchAllocator( &allocatorState );
    geometry_scratch_t scratch{};

    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { { local, sizeof( local ) }, &allocator, 256u, 256u, 64u } ) ==
             geometry_status_t::OK );
    const geometry_scratch_stats_t stats = QueryStats( scratch );
    REQUIRE_FALSE( stats.bUsesLocalStorage );
    REQUIRE( stats.bUsesFallbackAllocation );
    REQUIRE( allocatorState.cAllocateCalls == 1u );
    REQUIRE( allocatorState.cFreeCalls == 0u );

    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
    REQUIRE( allocatorState.cFreeCalls == 1u );
    REQUIRE( GeometryScratch_IsValid( &scratch ) );
    REQUIRE_FALSE( GeometryScratch_IsInitialized( &scratch ) );
}

TEST_CASE( "geometry scratch rejects over-budget acquisition before allocation",
           "[editor][geometry][core][scratch]" )
{
    scratch_allocator_state_t allocatorState{};
    const allocator_t allocator = MakeScratchAllocator( &allocatorState );
    geometry_scratch_t scratch{};

    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { {}, &allocator, 257u, 256u, 16u } ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE( allocatorState.cAllocateCalls == 0u );
    REQUIRE( GeometryScratch_IsValid( &scratch ) );
    REQUIRE_FALSE( GeometryScratch_IsInitialized( &scratch ) );
}

TEST_CASE( "geometry scratch fallback failure leaves canonical empty state",
           "[editor][geometry][core][scratch]" )
{
    scratch_allocator_state_t allocatorState{};
    allocatorState.bFailAllocation = common::CY_TRUE;
    const allocator_t allocator = MakeScratchAllocator( &allocatorState );
    common::byte local[8]{};
    geometry_scratch_t scratch{};

    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { { local, sizeof( local ) }, &allocator, 128u, 128u, 16u } ) ==
             geometry_status_t::ALLOCATION_FAILED );
    REQUIRE( allocatorState.cAllocateCalls == 1u );
    REQUIRE( allocatorState.cFreeCalls == 0u );
    REQUIRE( GeometryScratch_IsValid( &scratch ) );
    REQUIRE_FALSE( GeometryScratch_IsInitialized( &scratch ) );
}

TEST_CASE( "geometry scratch allocations align and failures preserve cursor",
           "[editor][geometry][core][scratch]" )
{
    alignas( 64 ) common::byte local[96]{};
    geometry_scratch_t scratch{};
    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { { local, sizeof( local ) }, nullptr, 96u, 160u, 64u } ) ==
             geometry_status_t::OK );

    void *pFirst = nullptr;
    REQUIRE( GeometryScratch_Allocate( &scratch, 7u, 32u, &pFirst ) ==
             geometry_status_t::OK );
    REQUIRE( pFirst != nullptr );
    REQUIRE( common::Cy_AlignIsPointerAligned( pFirst, 32u ) );

    const geometry_scratch_stats_t before = QueryStats( scratch );
    void *pFailed = reinterpret_cast<void *>( 1u );
    REQUIRE( GeometryScratch_Allocate( &scratch, 96u, 16u, &pFailed ) ==
             geometry_status_t::INSUFFICIENT_CAPACITY );
    REQUIRE( pFailed == nullptr );
    const geometry_scratch_stats_t after = QueryStats( scratch );
    REQUIRE( after.cbUsed == before.cbUsed );
    REQUIRE( after.cbHighWater == before.cbHighWater );

    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "geometry scratch mark rewinds storage while retaining high water",
           "[editor][geometry][core][scratch]" )
{
    alignas( 32 ) common::byte local[256]{};
    geometry_scratch_t scratch{};
    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { { local, sizeof( local ) }, nullptr, 256u, 256u, 32u } ) ==
             geometry_status_t::OK );

    void *pPermanentForOperation = nullptr;
    REQUIRE( GeometryScratch_Allocate(
                 &scratch,
                 16u,
                 16u,
                 &pPermanentForOperation ) == geometry_status_t::OK );

    geometry_scratch_mark_t mark{};
    REQUIRE( GeometryScratch_Mark( &scratch, &mark ) == geometry_status_t::OK );

    void *pTemporary = nullptr;
    REQUIRE( GeometryScratch_Allocate( &scratch, 48u, 32u, &pTemporary ) ==
             geometry_status_t::OK );
    const geometry_scratch_stats_t peak = QueryStats( scratch );
    REQUIRE( peak.cbUsed == peak.cbHighWater );

    REQUIRE( GeometryScratch_Rewind( &scratch, mark ) == geometry_status_t::OK );
    const geometry_scratch_stats_t rewound = QueryStats( scratch );
    REQUIRE( rewound.cbUsed == mark.iOffset );
    REQUIRE( rewound.cbHighWater == peak.cbHighWater );

    void *pReused = nullptr;
    REQUIRE( GeometryScratch_Allocate( &scratch, 48u, 32u, &pReused ) ==
             geometry_status_t::OK );
    REQUIRE( pReused == pTemporary );

    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "geometry scratch typed arrays reject overflow transactionally",
           "[editor][geometry][core][scratch]" )
{
    alignas( 32 ) common::byte local[256]{};
    geometry_scratch_t scratch{};
    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { { local, sizeof( local ) }, nullptr, 256u, 256u, 32u } ) ==
             geometry_status_t::OK );

    u64 *pValues = nullptr;
    REQUIRE( GeometryScratch_AllocateArrayStorage(
                 &scratch,
                 8u,
                 &pValues ) == geometry_status_t::OK );
    REQUIRE( pValues != nullptr );
    REQUIRE( common::Cy_AlignIsPointerAligned( pValues, alignof( u64 ) ) );

    const geometry_scratch_stats_t before = QueryStats( scratch );
    u64 *pOverflow = reinterpret_cast<u64 *>( 1u );
    REQUIRE( GeometryScratch_AllocateArrayStorage(
                 &scratch,
                 common::CY_USIZE_MAX,
                 &pOverflow ) == geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE( pOverflow == nullptr );
    const geometry_scratch_stats_t after = QueryStats( scratch );
    REQUIRE( after.cbUsed == before.cbUsed );
    REQUIRE( after.cbHighWater == before.cbHighWater );

    u64 *pMisaligned = reinterpret_cast<u64 *>( 1u );
    REQUIRE( GeometryScratch_AllocateArrayStorage(
                 &scratch,
                 1u,
                 &pMisaligned,
                 1u ) == geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( pMisaligned == nullptr );

    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "geometry scratch release is canonical and idempotent",
           "[editor][geometry][core][scratch]" )
{
    alignas( 16 ) common::byte local[64]{};
    geometry_scratch_t scratch{};
    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { { local, sizeof( local ) }, nullptr, 64u, 64u, 16u } ) ==
             geometry_status_t::OK );

    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
    REQUIRE( GeometryScratch_IsValid( &scratch ) );
    REQUIRE_FALSE( GeometryScratch_IsInitialized( &scratch ) );
    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
    REQUIRE( GeometryScratch_IsValid( &scratch ) );

    geometry_scratch_stats_t stats{ .cbBudget = 7u };
    REQUIRE( GeometryScratch_QueryStats( &scratch, &stats ) ==
             geometry_status_t::NOT_INITIALIZED );
    REQUIRE( stats.cbBudget == 0u );
}

TEST_CASE( "geometry scratch rejects foreign and stale marks",
           "[editor][geometry][core][scratch]" )
{
    alignas( 32 ) common::byte firstStorage[128]{};
    alignas( 32 ) common::byte secondStorage[128]{};
    geometry_scratch_t first{};
    geometry_scratch_t second{};

    REQUIRE( GeometryScratch_Acquire(
                 &first,
                 { { firstStorage, sizeof( firstStorage ) },
                   nullptr,
                   sizeof( firstStorage ),
                   sizeof( firstStorage ),
                   32u } ) == geometry_status_t::OK );
    REQUIRE( GeometryScratch_Acquire(
                 &second,
                 { { secondStorage, sizeof( secondStorage ) },
                   nullptr,
                   sizeof( secondStorage ),
                   sizeof( secondStorage ),
                   32u } ) == geometry_status_t::OK );

    geometry_scratch_mark_t firstMark{};
    REQUIRE( GeometryScratch_Mark( &first, &firstMark ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryScratch_Rewind( &second, firstMark ) ==
             geometry_status_t::INVALID_ARGUMENT );

    const u64 firstSerial = first.nAcquisitionSerial;
    REQUIRE( GeometryScratch_Release( &first ) == geometry_status_t::OK );
    REQUIRE( first.nAcquisitionSerial == firstSerial );
    REQUIRE( GeometryScratch_Acquire(
                 &first,
                 { { firstStorage, sizeof( firstStorage ) },
                   nullptr,
                   sizeof( firstStorage ),
                   sizeof( firstStorage ),
                   32u } ) == geometry_status_t::OK );
    REQUIRE( first.nAcquisitionSerial == firstSerial + 1u );
    REQUIRE( GeometryScratch_Rewind( &first, firstMark ) ==
             geometry_status_t::INVALID_ARGUMENT );

    REQUIRE( GeometryScratch_Release( &first ) == geometry_status_t::OK );
    REQUIRE( GeometryScratch_Release( &second ) == geometry_status_t::OK );
}

TEST_CASE( "geometry scratch markers are one-shot and strictly LIFO",
           "[editor][geometry][core][scratch]" )
{
    alignas( 32 ) common::byte storage[256]{};
    geometry_scratch_t scratch{};
    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { { storage, sizeof( storage ) },
                   nullptr,
                   sizeof( storage ),
                   sizeof( storage ),
                   32u } ) == geometry_status_t::OK );

    geometry_scratch_mark_t outer{};
    REQUIRE( GeometryScratch_Mark( &scratch, &outer ) ==
             geometry_status_t::OK );
    void *pOuterAllocation = nullptr;
    REQUIRE( GeometryScratch_Allocate(
                 &scratch,
                 16u,
                 16u,
                 &pOuterAllocation ) == geometry_status_t::OK );

    geometry_scratch_mark_t inner{};
    REQUIRE( GeometryScratch_Mark( &scratch, &inner ) ==
             geometry_status_t::OK );
    void *pInnerAllocation = nullptr;
    REQUIRE( GeometryScratch_Allocate(
                 &scratch,
                 32u,
                 16u,
                 &pInnerAllocation ) == geometry_status_t::OK );

    const geometry_scratch_stats_t beforeRejectedRewind = QueryStats( scratch );
    REQUIRE( GeometryScratch_Rewind( &scratch, outer ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( QueryStats( scratch ).cbUsed == beforeRejectedRewind.cbUsed );

    REQUIRE( GeometryScratch_Rewind( &scratch, inner ) ==
             geometry_status_t::OK );
    REQUIRE( GeometryScratch_Rewind( &scratch, inner ) ==
             geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( GeometryScratch_Rewind( &scratch, outer ) ==
             geometry_status_t::OK );

    void *pRegrown = nullptr;
    REQUIRE( GeometryScratch_Allocate(
                 &scratch,
                 64u,
                 16u,
                 &pRegrown ) == geometry_status_t::OK );
    REQUIRE( GeometryScratch_Rewind( &scratch, inner ) ==
             geometry_status_t::INVALID_ARGUMENT );

    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "geometry scratch freezes typed allocation status precedence",
           "[editor][geometry][core][scratch]" )
{
    u64 *pValues = reinterpret_cast<u64 *>( 1u );
    REQUIRE( GeometryScratch_AllocateArrayStorage(
                 static_cast<geometry_scratch_t *>( nullptr ),
                 common::CY_USIZE_MAX,
                 &pValues,
                 1u ) == geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( pValues == nullptr );

    geometry_scratch_t scratch{};
    pValues = reinterpret_cast<u64 *>( 1u );
    REQUIRE( GeometryScratch_AllocateArrayStorage(
                 &scratch,
                 common::CY_USIZE_MAX,
                 &pValues ) == geometry_status_t::NOT_INITIALIZED );
    REQUIRE( pValues == nullptr );

    alignas( 32 ) common::byte storage[128]{};
    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { { storage, sizeof( storage ) },
                   nullptr,
                   sizeof( storage ),
                   sizeof( storage ),
                   32u } ) == geometry_status_t::OK );
    pValues = reinterpret_cast<u64 *>( 1u );
    REQUIRE( GeometryScratch_AllocateArrayStorage(
                 &scratch,
                 common::CY_USIZE_MAX,
                 &pValues,
                 3u ) == geometry_status_t::INVALID_ARGUMENT );
    REQUIRE( pValues == nullptr );
    REQUIRE( GeometryScratch_AllocateArrayStorage(
                 &scratch,
                 common::CY_USIZE_MAX,
                 &pValues ) == geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE( pValues == nullptr );
    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
}

TEST_CASE( "geometry scratch rejects exhausted serials and corrupt alignment metadata",
           "[editor][geometry][core][scratch]" )
{
    alignas( 32 ) common::byte storage[128]{};
    geometry_scratch_t scratch{};
    scratch.nAcquisitionSerial = common::CY_U64_MAX - 1u;
    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { { storage, sizeof( storage ) },
                   nullptr,
                   sizeof( storage ),
                   sizeof( storage ),
                   32u } ) == geometry_status_t::OK );
    REQUIRE( scratch.nAcquisitionSerial == common::CY_U64_MAX );

    const usize validAlignment = scratch.nBaseAlignment;
    scratch.nBaseAlignment = validAlignment / 2u;
    REQUIRE_FALSE( GeometryScratch_IsValid( &scratch ) );
    REQUIRE( GeometryScratch_Release( &scratch ) ==
             geometry_status_t::CORRUPT_STATE );
    scratch.nBaseAlignment = validAlignment;
    REQUIRE( GeometryScratch_IsValid( &scratch ) );

    scratch.nMarkerSerialCounter = common::CY_U64_MAX;
    geometry_scratch_mark_t mark{};
    mark.iOffset = common::CY_USIZE_MAX;
    REQUIRE( GeometryScratch_Mark( &scratch, &mark ) ==
             geometry_status_t::LIMIT_EXCEEDED );
    REQUIRE( mark.pOwner == nullptr );
    REQUIRE( mark.nMarkerSerial == 0u );
    REQUIRE( scratch.nMarkerSerialCounter == common::CY_U64_MAX );

    REQUIRE( GeometryScratch_Release( &scratch ) == geometry_status_t::OK );
    REQUIRE( GeometryScratch_Acquire(
                 &scratch,
                 { { storage, sizeof( storage ) },
                   nullptr,
                   sizeof( storage ),
                   sizeof( storage ),
                   32u } ) == geometry_status_t::LIMIT_EXCEEDED );
}

} // namespace cypher::editor::geometry
