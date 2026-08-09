//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Allocator_Tests.cpp
//  Purpose: Tests the Tier1 allocator interface and system allocator backend.
//  Details: Protects callback forwarding, alignment, reallocation failure ownership,
//           allocate-copy-free fallback, owned records, and invalid-input behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Allocator.h"
#include "CypherCommon_Assert.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <new>
#include <thread>
#include <utility>

using namespace cypher::common;

namespace
{

struct tracked_allocator_state_t {
    u32 cAllocateCalls{ 0u };
    u32 cReallocateCalls{ 0u };
    u32 cFreeCalls{ 0u };
    usize cbLastAllocate{ 0u };
    usize nLastAllocateAlignment{ 0u };
    usize cbLastOldSize{ 0u };
    usize cbLastNewSize{ 0u };
    usize nLastReallocateAlignment{ 0u };
    void *pLastFreed{ nullptr };
    usize cbLastFree{ 0u };
    usize nLastFreeAlignment{ 0u };
    bool_t bFailAllocate{ CY_FALSE };
    bool_t bFailReallocate{ CY_FALSE };
};

struct misaligned_allocator_state_t {
    alignas( 64 ) u8 storage[128]{};
    u32 cAllocateCalls{ 0u };
    u32 cFreeCalls{ 0u };
};

u32 g_allocatorAssertCount = 0u;

assert_action_t CaptureAllocatorAssert( const assert_info_t & ) noexcept
{
    ++g_allocatorAssertCount;
    return assert_action_t::Continue;
}

usize EffectiveAlignment( usize nAlignment ) noexcept
{
    return nAlignment < CY_ALLOCATOR_DEFAULT_ALIGNMENT
        ? CY_ALLOCATOR_DEFAULT_ALIGNMENT
        : nAlignment;
}

void *RawAllocate( usize cbSize, usize nAlignment ) noexcept
{
    return ::operator new(
        cbSize,
        static_cast<std::align_val_t>( EffectiveAlignment( nAlignment ) ),
        std::nothrow );
}

void RawFree( void *pMemory, usize nAlignment ) noexcept
{
    ::operator delete(
        pMemory,
        static_cast<std::align_val_t>( EffectiveAlignment( nAlignment ) ) );
}

void *TrackedAllocate(
    void *pUserData,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<tracked_allocator_state_t *>( pUserData );
    ++pState->cAllocateCalls;
    pState->cbLastAllocate = cbSize;
    pState->nLastAllocateAlignment = nAlignment;

    if ( pState->bFailAllocate ) {
        return nullptr;
    }

    return RawAllocate( cbSize, nAlignment );
}

void *TrackedReallocate(
    void *pUserData,
    void *pMemory,
    usize cbOldSize,
    usize cbNewSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<tracked_allocator_state_t *>( pUserData );
    ++pState->cReallocateCalls;
    pState->cbLastOldSize = cbOldSize;
    pState->cbLastNewSize = cbNewSize;
    pState->nLastReallocateAlignment = nAlignment;

    if ( pState->bFailReallocate ) {
        return nullptr;
    }

    void *pNewMemory = RawAllocate( cbNewSize, nAlignment );
    if ( pNewMemory == nullptr ) {
        return nullptr;
    }

    const usize cbCopySize = cbOldSize < cbNewSize
        ? cbOldSize
        : cbNewSize;
    Cy_MemCopy( pNewMemory, pMemory, cbCopySize );
    RawFree( pMemory, nAlignment );
    return pNewMemory;
}

void TrackedFree(
    void *pUserData,
    void *pMemory,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<tracked_allocator_state_t *>( pUserData );
    ++pState->cFreeCalls;
    pState->pLastFreed = pMemory;
    pState->cbLastFree = cbSize;
    pState->nLastFreeAlignment = nAlignment;
    RawFree( pMemory, nAlignment );
}

void *MisalignedAllocate(
    void *pUserData,
    usize,
    usize ) noexcept
{
    auto *pState = static_cast<misaligned_allocator_state_t *>( pUserData );
    ++pState->cAllocateCalls;
    return pState->storage + 1u;
}

void MisalignedFree(
    void *pUserData,
    void *,
    usize,
    usize ) noexcept
{
    auto *pState = static_cast<misaligned_allocator_state_t *>( pUserData );
    ++pState->cFreeCalls;
}

allocator_t MakeTrackedAllocator(
    tracked_allocator_state_t *pState,
    bool_t bNativeReallocate = CY_FALSE ) noexcept
{
    return {
        TrackedAllocate,
        bNativeReallocate ? TrackedReallocate : nullptr,
        TrackedFree,
        pState
    };
}

void FillPattern( void *pMemory, usize cbSize ) noexcept
{
    auto *pBytes = static_cast<u8 *>( pMemory );
    for ( usize i = 0u; i < cbSize; ++i ) {
        pBytes[i] = static_cast<u8>( ( i * 29u ) & 0xFFu );
    }
}

void RequirePattern( const void *pMemory, usize cbSize )
{
    const auto *pBytes = static_cast<const u8 *>( pMemory );
    for ( usize i = 0u; i < cbSize; ++i ) {
        CAPTURE( i );
        REQUIRE( pBytes[i] == static_cast<u8>( ( i * 29u ) & 0xFFu ) );
    }
}

} // namespace

TEST_CASE( "Allocator exposes a compact valid function-table contract",
           "[CypherCommon][Tier1][Allocator]" )
{
    STATIC_REQUIRE( Cy_AlignIsPowerOfTwo( CY_ALLOCATOR_DEFAULT_ALIGNMENT ) );
    STATIC_REQUIRE( CY_ALLOCATOR_DEFAULT_ALIGNMENT >= alignof( void * ) );
    STATIC_REQUIRE( is_trivially_copyable_v<allocator_t> );
    STATIC_REQUIRE( is_standard_layout_v<allocator_t> );
    STATIC_REQUIRE_FALSE( is_copy_constructible_v<owned_allocation_t> );
    STATIC_REQUIRE_FALSE( is_copy_assignable_v<owned_allocation_t> );
    STATIC_REQUIRE( is_move_constructible_v<owned_allocation_t> );
    STATIC_REQUIRE( is_move_assignable_v<owned_allocation_t> );
    STATIC_REQUIRE( is_standard_layout_v<owned_allocation_t> );

    tracked_allocator_state_t state{};
    allocator_t allocator = MakeTrackedAllocator( &state );
    const allocator_t emptyAllocator{};

    REQUIRE_FALSE( Allocator_IsValid( nullptr ) );
    REQUIRE_FALSE( Allocator_IsValid( &emptyAllocator ) );

    allocator.pfnFree = nullptr;
    REQUIRE_FALSE( Allocator_IsValid( &allocator ) );
    allocator.pfnFree = TrackedFree;
    REQUIRE( Allocator_IsValid( &allocator ) );

    const allocator_t *pSystemA = Allocator_GetSystem();
    const allocator_t *pSystemB = Allocator_GetSystem();
    REQUIRE( pSystemA == pSystemB );
    REQUIRE( Allocator_IsValid( pSystemA ) );
}

TEST_CASE( "Allocator forwards allocation and release metadata",
           "[CypherCommon][Tier1][Allocator]" )
{
    tracked_allocator_state_t state{};
    const allocator_t allocator = MakeTrackedAllocator( &state );

    void *pMemory = Allocator_Allocate( &allocator, 73u, 64u );
    REQUIRE( pMemory != nullptr );
    REQUIRE( Cy_AlignIsPointerAligned( pMemory, 64u ) );
    REQUIRE( state.cAllocateCalls == 1u );
    REQUIRE( state.cbLastAllocate == 73u );
    REQUIRE( state.nLastAllocateAlignment == 64u );

    FillPattern( pMemory, 73u );
    RequirePattern( pMemory, 73u );

    Allocator_Free( &allocator, pMemory, 73u, 64u );
    REQUIRE( state.cFreeCalls == 1u );
    REQUIRE( state.pLastFreed == pMemory );
    REQUIRE( state.cbLastFree == 73u );
    REQUIRE( state.nLastFreeAlignment == 64u );
}

TEST_CASE( "Allocator treats zero allocation and null release as no-ops",
           "[CypherCommon][Tier1][Allocator]" )
{
    g_allocatorAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureAllocatorAssert );

    REQUIRE( Allocator_Allocate( nullptr, 0u, 0u ) == nullptr );
    REQUIRE( Allocator_Reallocate( nullptr, nullptr, 0u, 0u, 0u ) == nullptr );
    Allocator_Free( nullptr, nullptr, 0u, 0u );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( g_allocatorAssertCount == 0u );
}

TEST_CASE( "Allocator rejects invalid allocate and free contracts",
           "[CypherCommon][Tier1][Allocator]" )
{
    tracked_allocator_state_t state{};
    const allocator_t allocator = MakeTrackedAllocator( &state );
    alignas( 64 ) u8 storage[64]{};

    g_allocatorAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureAllocatorAssert );

    REQUIRE( Allocator_Allocate( nullptr, 8u, 8u ) == nullptr );
    REQUIRE( Allocator_Allocate( &allocator, 8u, 3u ) == nullptr );
    Allocator_Free( nullptr, storage, sizeof( storage ), 64u );
    Allocator_Free( &allocator, storage, 0u, 64u );
    Allocator_Free( &allocator, storage, sizeof( storage ), 3u );
    Allocator_Free( &allocator, storage + 1u, sizeof( storage ) - 1u, 8u );

    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( state.cAllocateCalls == 0u );
    REQUIRE( state.cFreeCalls == 0u );
    REQUIRE( g_allocatorAssertCount == 6u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "Allocator distinguishes allocation failure from callback contract failure",
           "[CypherCommon][Tier1][Allocator]" )
{
    tracked_allocator_state_t failureState{};
    failureState.bFailAllocate = CY_TRUE;
    const allocator_t failureAllocator = MakeTrackedAllocator( &failureState );

    misaligned_allocator_state_t misalignedState{};
    const allocator_t misalignedAllocator{
        MisalignedAllocate,
        nullptr,
        MisalignedFree,
        &misalignedState
    };

    g_allocatorAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureAllocatorAssert );

    REQUIRE( Allocator_Allocate( &failureAllocator, 32u, 16u ) == nullptr );
    REQUIRE( Allocator_Allocate( &misalignedAllocator, 32u, 64u ) == nullptr );

    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( failureState.cAllocateCalls == 1u );
    REQUIRE( misalignedState.cAllocateCalls == 1u );
    REQUIRE( misalignedState.cFreeCalls == 1u );
    REQUIRE( g_allocatorAssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "Allocator fallback reallocation preserves bytes while growing and shrinking",
           "[CypherCommon][Tier1][Allocator]" )
{
    tracked_allocator_state_t state{};
    const allocator_t allocator = MakeTrackedAllocator( &state );

    void *pMemory = Allocator_Allocate( &allocator, 32u, 64u );
    REQUIRE( pMemory != nullptr );
    FillPattern( pMemory, 32u );

    pMemory = Allocator_Reallocate( &allocator, pMemory, 32u, 96u, 64u );
    REQUIRE( pMemory != nullptr );
    REQUIRE( Cy_AlignIsPointerAligned( pMemory, 64u ) );
    RequirePattern( pMemory, 32u );
    REQUIRE( state.cAllocateCalls == 2u );
    REQUIRE( state.cReallocateCalls == 0u );
    REQUIRE( state.cFreeCalls == 1u );

    pMemory = Allocator_Reallocate( &allocator, pMemory, 96u, 8u, 64u );
    REQUIRE( pMemory != nullptr );
    RequirePattern( pMemory, 8u );
    REQUIRE( state.cAllocateCalls == 3u );
    REQUIRE( state.cFreeCalls == 2u );

    void *pSameMemory = Allocator_Reallocate( &allocator, pMemory, 8u, 8u, 64u );
    REQUIRE( pSameMemory == pMemory );
    REQUIRE( state.cAllocateCalls == 3u );
    REQUIRE( state.cFreeCalls == 2u );

    REQUIRE( Allocator_Reallocate( &allocator, pMemory, 8u, 0u, 64u ) == nullptr );
    REQUIRE( state.cFreeCalls == 3u );
}

TEST_CASE( "Allocator fallback failure preserves the original allocation",
           "[CypherCommon][Tier1][Allocator]" )
{
    tracked_allocator_state_t state{};
    const allocator_t allocator = MakeTrackedAllocator( &state );

    void *pMemory = Allocator_Allocate( &allocator, 32u, 32u );
    REQUIRE( pMemory != nullptr );
    FillPattern( pMemory, 32u );

    state.bFailAllocate = CY_TRUE;
    REQUIRE( Allocator_Reallocate( &allocator, pMemory, 32u, 96u, 32u ) == nullptr );
    RequirePattern( pMemory, 32u );
    REQUIRE( state.cAllocateCalls == 2u );
    REQUIRE( state.cFreeCalls == 0u );

    Allocator_Free( &allocator, pMemory, 32u, 32u );
    REQUIRE( state.cFreeCalls == 1u );
}

TEST_CASE( "Allocator uses a native reallocation callback when supplied",
           "[CypherCommon][Tier1][Allocator]" )
{
    tracked_allocator_state_t state{};
    const allocator_t allocator = MakeTrackedAllocator( &state, CY_TRUE );

    void *pMemory = Allocator_Allocate( &allocator, 24u, 32u );
    REQUIRE( pMemory != nullptr );
    FillPattern( pMemory, 24u );

    pMemory = Allocator_Reallocate( &allocator, pMemory, 24u, 80u, 32u );
    REQUIRE( pMemory != nullptr );
    RequirePattern( pMemory, 24u );
    REQUIRE( state.cAllocateCalls == 1u );
    REQUIRE( state.cReallocateCalls == 1u );
    REQUIRE( state.cFreeCalls == 0u );
    REQUIRE( state.cbLastOldSize == 24u );
    REQUIRE( state.cbLastNewSize == 80u );
    REQUIRE( state.nLastReallocateAlignment == 32u );

    state.bFailReallocate = CY_TRUE;
    REQUIRE( Allocator_Reallocate( &allocator, pMemory, 80u, 160u, 32u ) == nullptr );
    RequirePattern( pMemory, 24u );
    REQUIRE( state.cReallocateCalls == 2u );
    REQUIRE( state.cFreeCalls == 0u );

    Allocator_Free( &allocator, pMemory, 80u, 32u );
    REQUIRE( state.cFreeCalls == 1u );
}

TEST_CASE( "Allocator rejects invalid reallocation metadata without losing ownership",
           "[CypherCommon][Tier1][Allocator]" )
{
    tracked_allocator_state_t state{};
    const allocator_t allocator = MakeTrackedAllocator( &state );
    void *pMemory = Allocator_Allocate( &allocator, 32u, 64u );
    REQUIRE( pMemory != nullptr );
    alignas( 64 ) u8 storage[64]{};

    g_allocatorAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureAllocatorAssert );

    REQUIRE( Allocator_Reallocate( &allocator, nullptr, 1u, 8u, 8u ) == nullptr );
    REQUIRE( Allocator_Reallocate( nullptr, pMemory, 32u, 64u, 64u ) == nullptr );
    REQUIRE( Allocator_Reallocate( &allocator, pMemory, 0u, 64u, 64u ) == nullptr );
    REQUIRE( Allocator_Reallocate( &allocator, pMemory, 32u, 64u, 3u ) == nullptr );
    REQUIRE( Allocator_Reallocate( &allocator, storage + 1u, 32u, 64u, 8u ) == nullptr );

    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( state.cAllocateCalls == 1u );
    REQUIRE( state.cFreeCalls == 0u );
    REQUIRE( g_allocatorAssertCount == 5u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );

    Allocator_Free( &allocator, pMemory, 32u, 64u );
}

TEST_CASE( "Allocator_FreeOwned releases valid ownership and preserves invalid ownership",
           "[CypherCommon][Tier1][Allocator]" )
{
    tracked_allocator_state_t state{};
    const allocator_t allocator = MakeTrackedAllocator( &state );

    owned_allocation_t allocation{};
    REQUIRE( Allocator_AllocateOwned( &allocation, &allocator, 48u, 16u ) );
    REQUIRE( allocation.pData != nullptr );
    REQUIRE( Allocator_OwnedIsValid( &allocation ) );

    Allocator_FreeOwned( &allocation );
    REQUIRE( allocation.pData == nullptr );
    REQUIRE( allocation.cbSize == 0u );
    REQUIRE( allocation.nAlignment == 0u );
    REQUIRE( allocation.pAllocator == nullptr );
    REQUIRE( Allocator_OwnedIsValid( &allocation ) );
    REQUIRE( state.cFreeCalls == 1u );

    Allocator_FreeOwned( &allocation );
    REQUIRE( state.cFreeCalls == 1u );

    g_allocatorAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureAllocatorAssert );

    Allocator_FreeOwned( nullptr );

    owned_allocation_t inconsistentEmpty{};
    inconsistentEmpty.cbSize = 4u;
    inconsistentEmpty.nAlignment = 4u;
    inconsistentEmpty.pAllocator = &allocator;
    Allocator_FreeOwned( &inconsistentEmpty );
    REQUIRE( inconsistentEmpty.pData == nullptr );
    REQUIRE( inconsistentEmpty.cbSize == 0u );
    REQUIRE( inconsistentEmpty.nAlignment == 0u );
    REQUIRE( inconsistentEmpty.pAllocator == nullptr );

    owned_allocation_t invalidOwnership{};
    invalidOwnership.pData = Allocator_Allocate( &allocator, 32u, 16u );
    invalidOwnership.cbSize = 32u;
    invalidOwnership.nAlignment = 16u;
    REQUIRE( invalidOwnership.pData != nullptr );
    REQUIRE_FALSE( Allocator_OwnedIsValid( &invalidOwnership ) );
    void *pOwnedMemory = invalidOwnership.pData;
    Allocator_FreeOwned( &invalidOwnership );
    REQUIRE( invalidOwnership.pData == pOwnedMemory );

    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( g_allocatorAssertCount == 3u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    REQUIRE( state.cFreeCalls == 1u );

    invalidOwnership.pAllocator = &allocator;
    Allocator_FreeOwned( &invalidOwnership );
    REQUIRE( invalidOwnership.pData == nullptr );
    REQUIRE( state.cFreeCalls == 2u );
}

TEST_CASE( "Allocator adopts and transfers ownership without duplicating it",
           "[CypherCommon][Tier1][Allocator]" )
{
    tracked_allocator_state_t state{};
    const allocator_t allocator = MakeTrackedAllocator( &state );
    void *pMemory = Allocator_Allocate( &allocator, 64u, 32u );
    REQUIRE( pMemory != nullptr );

    owned_allocation_t adopted{};
    REQUIRE( Allocator_AdoptOwned(
        &adopted,
        &allocator,
        pMemory,
        64u,
        32u ) );
    REQUIRE( Allocator_OwnedIsValid( &adopted ) );

    owned_allocation_t moved{ std::move( adopted ) };
    REQUIRE( Allocator_OwnedIsValid( &adopted ) );
    REQUIRE( adopted.pData == nullptr );
    REQUIRE( moved.pData == pMemory );

    owned_allocation_t destination{};
    REQUIRE( Allocator_MoveOwned( &destination, &moved ) );
    REQUIRE( Allocator_OwnedIsValid( &moved ) );
    REQUIRE( moved.pData == nullptr );
    REQUIRE( destination.pData == pMemory );

    Allocator_FreeOwned( &destination );
    REQUIRE( state.cFreeCalls == 1u );
}

TEST_CASE( "Allocator checked storage helpers preserve data and reject overflow",
           "[CypherCommon][Tier1][Allocator]" )
{
    tracked_allocator_state_t state{};
    const allocator_t allocator = MakeTrackedAllocator( &state );

    u32 *pValues = Allocator_AllocateArrayStorage<u32>( &allocator, 16u );
    REQUIRE( pValues != nullptr );
    for ( usize i = 0u; i < 16u; ++i ) {
        pValues[i] = static_cast<u32>( i * 17u );
    }

    pValues = Allocator_ReallocateArrayStorage<u32>(
        &allocator,
        pValues,
        16u,
        64u );
    REQUIRE( pValues != nullptr );
    for ( usize i = 0u; i < 16u; ++i ) {
        REQUIRE( pValues[i] == static_cast<u32>( i * 17u ) );
    }
    Allocator_FreeArrayStorage( &allocator, pValues, 64u );

    constexpr usize cbZeroedSize = 257u;
    void *pZeroed = Allocator_AllocateZeroed( &allocator, cbZeroedSize, 64u );
    REQUIRE( pZeroed != nullptr );
    REQUIRE( Cy_MemIsZero( pZeroed, cbZeroedSize ) );
    Allocator_Free( &allocator, pZeroed, cbZeroedSize, 64u );

    g_allocatorAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureAllocatorAssert );

    constexpr usize nOverflowCount = CY_USIZE_MAX / sizeof( u64 ) + 1u;
    REQUIRE( Allocator_AllocateArrayStorage<u64>(
        &allocator,
        nOverflowCount ) == nullptr );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( g_allocatorAssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "System allocator supports natural and over-aligned allocations",
           "[CypherCommon][Tier1][Allocator]" )
{
    constexpr usize alignments[] = {
        1u,
        2u,
        8u,
        CY_ALLOCATOR_DEFAULT_ALIGNMENT,
        32u,
        64u,
        256u,
        4096u
    };

    const allocator_t *pSystem = Allocator_GetSystem();
    for ( usize nAlignment : alignments ) {
        CAPTURE( nAlignment );
        void *pMemory = Allocator_Allocate( pSystem, 97u, nAlignment );
        REQUIRE( pMemory != nullptr );
        REQUIRE( Cy_AlignIsPointerAligned( pMemory, nAlignment ) );

        FillPattern( pMemory, 97u );
        RequirePattern( pMemory, 97u );
        Allocator_Free( pSystem, pMemory, 97u, nAlignment );
    }

    void *pMemory = Allocator_Allocate( pSystem, 32u );
    REQUIRE( pMemory != nullptr );
    FillPattern( pMemory, 32u );

    pMemory = Allocator_Reallocate( pSystem, pMemory, 32u, 128u );
    REQUIRE( pMemory != nullptr );
    RequirePattern( pMemory, 32u );
    Allocator_Free( pSystem, pMemory, 128u );
}

TEST_CASE( "System allocator supports concurrent independent allocation traffic",
           "[CypherCommon][Tier1][Allocator][Threaded]" )
{
    constexpr usize nThreadCount = 8u;
    constexpr usize nIterations = 256u;
    const allocator_t *pSystem = Allocator_GetSystem();
    std::atomic<bool> bFailed{ false };
    std::array<std::thread, nThreadCount> workers{};

    for ( usize iThread = 0u; iThread < nThreadCount; ++iThread ) {
        workers[iThread] = std::thread( [=, &bFailed]() {
            for ( usize i = 0u; i < nIterations; ++i ) {
                const usize cbSize = ( ( i + 1u ) * ( iThread + 7u ) ) % 4096u + 1u;
                const usize nAlignment = static_cast<usize>( 1u ) << ( i % 7u );
                void *pMemory = Allocator_Allocate( pSystem, cbSize, nAlignment );
                if ( pMemory == nullptr ||
                     !Cy_AlignIsPointerAligned( pMemory, nAlignment ) ) {
                    bFailed.store( true, std::memory_order_relaxed );
                    return;
                }

                const u8 nPattern = static_cast<u8>( iThread + i );
                Cy_MemSet( pMemory, nPattern, cbSize );
                const auto *pBytes = static_cast<const u8 *>( pMemory );
                if ( pBytes[0] != nPattern || pBytes[cbSize - 1u] != nPattern ) {
                    bFailed.store( true, std::memory_order_relaxed );
                }
                Allocator_Free( pSystem, pMemory, cbSize, nAlignment );
            }
        } );
    }

    for ( std::thread &worker : workers ) {
        worker.join();
    }

    REQUIRE_FALSE( bFailed.load( std::memory_order_relaxed ) );
}
