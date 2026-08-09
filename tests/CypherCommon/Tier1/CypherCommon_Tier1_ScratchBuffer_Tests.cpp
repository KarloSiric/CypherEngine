//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ScratchBuffer_Tests.cpp
//  Purpose: Tests scoped local-first temporary byte storage.
//  Details: Protects local alignment, fallback ownership, automatic release,
//           zero fill, OOM rollback, active-buffer rejection, and empty requests.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_ScratchBuffer.h"

#include <catch2/catch_test_macros.hpp>

#include <new>

using namespace cypher::common;

namespace
{

struct scratch_allocator_state_t {
    u32 cAllocateCalls{};
    u32 cFreeCalls{};
    bool_t bFailAllocation{ CY_FALSE };
};

usize EffectiveAlignment( usize nAlignment ) noexcept
{
    return nAlignment < CY_ALLOCATOR_DEFAULT_ALIGNMENT
        ? CY_ALLOCATOR_DEFAULT_ALIGNMENT
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

u32 g_scratchBufferAssertCount = 0u;

assert_action_t CaptureScratchBufferAssert( const assert_info_t & ) noexcept
{
    ++g_scratchBufferAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "ScratchBuffer aligns and uses local storage without allocation",
           "[CypherCommon][Tier1][ScratchBuffer]" )
{
    alignas( 64 ) byte local[128]{};
    scratch_allocator_state_t state{};
    const allocator_t allocator = MakeScratchAllocator( &state );
    scratch_buffer_t buffer{};

    REQUIRE( ScratchBuffer_Acquire(
        &buffer,
        { local + 1u, sizeof( local ) - 1u },
        &allocator,
        64u,
        32u ) );
    REQUIRE( ScratchBuffer_IsValid( &buffer ) );
    REQUIRE( ScratchBuffer_UsesLocalStorage( &buffer ) );
    REQUIRE_FALSE( ScratchBuffer_UsesFallbackAllocation( &buffer ) );
    REQUIRE( Cy_AlignIsPointerAligned( ScratchBuffer_Data( &buffer ), 32u ) );
    REQUIRE( ScratchBuffer_Size( &buffer ) == 64u );
    REQUIRE( state.cAllocateCalls == 0u );

    ScratchBuffer_Release( &buffer );
    REQUIRE( ScratchBuffer_IsValid( &buffer ) );
    REQUIRE( state.cFreeCalls == 0u );
}

TEST_CASE( "ScratchBuffer fallback allocation releases automatically",
           "[CypherCommon][Tier1][ScratchBuffer]" )
{
    scratch_allocator_state_t state{};
    const allocator_t allocator = MakeScratchAllocator( &state );
    byte local[8]{};
    {
        scratch_buffer_t buffer{};
        REQUIRE( ScratchBuffer_Acquire(
            &buffer,
            { local, sizeof( local ) },
            &allocator,
            256u,
            64u ) );
        REQUIRE( ScratchBuffer_UsesFallbackAllocation( &buffer ) );
        REQUIRE_FALSE( ScratchBuffer_UsesLocalStorage( &buffer ) );
        REQUIRE( Cy_AlignIsPointerAligned( ScratchBuffer_Data( &buffer ), 64u ) );
        REQUIRE( state.cAllocateCalls == 1u );
        REQUIRE( state.cFreeCalls == 0u );
    }

    REQUIRE( state.cFreeCalls == 1u );
}

TEST_CASE( "ScratchBuffer zero acquisition and clear overwrite every byte",
           "[CypherCommon][Tier1][ScratchBuffer]" )
{
    byte local[64];
    Cy_MemSet( local, 0xCCu, sizeof( local ) );
    scratch_buffer_t buffer{};
    REQUIRE( ScratchBuffer_AcquireZeroed(
        &buffer,
        { local, sizeof( local ) },
        nullptr,
        32u,
        1u ) );
    REQUIRE( Cy_MemIsZero( ScratchBuffer_Data( &buffer ), 32u ) );

    Cy_MemSet( ScratchBuffer_Data( &buffer ), 0xA5u, 32u );
    ScratchBuffer_Clear( &buffer );
    REQUIRE( Cy_MemIsZero( ScratchBuffer_Data( &buffer ), 32u ) );

    const byte_span_t span = ScratchBuffer_Span( &buffer );
    const binary_block_t block = ScratchBuffer_Block( &buffer );
    REQUIRE( span.pData == block.pData );
    REQUIRE( span.nCount == block.cbSize );
}

TEST_CASE( "ScratchBuffer failed fallback leaves destination empty",
           "[CypherCommon][Tier1][ScratchBuffer]" )
{
    scratch_allocator_state_t state{};
    state.bFailAllocation = CY_TRUE;
    const allocator_t allocator = MakeScratchAllocator( &state );
    byte local[8]{};
    scratch_buffer_t buffer{};

    REQUIRE_FALSE( ScratchBuffer_Acquire(
        &buffer,
        { local, sizeof( local ) },
        &allocator,
        64u,
        16u ) );
    REQUIRE( ScratchBuffer_IsValid( &buffer ) );
    REQUIRE( ScratchBuffer_Data( &buffer ) == nullptr );
    REQUIRE( state.cAllocateCalls == 1u );
    REQUIRE( state.cFreeCalls == 0u );
}

TEST_CASE( "ScratchBuffer rejects active reacquisition and missing fallback",
           "[CypherCommon][Tier1][ScratchBuffer]" )
{
    byte local[32]{};
    scratch_buffer_t buffer{};
    REQUIRE( ScratchBuffer_Acquire(
        &buffer,
        { local, sizeof( local ) },
        nullptr,
        16u,
        8u ) );

    g_scratchBufferAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureScratchBufferAssert );

    REQUIRE_FALSE( ScratchBuffer_Acquire(
        &buffer,
        { local, sizeof( local ) },
        nullptr,
        16u,
        8u ) );
    ScratchBuffer_Release( &buffer );
    REQUIRE_FALSE( ScratchBuffer_Acquire(
        &buffer,
        { local, sizeof( local ) },
        nullptr,
        64u,
        8u ) );
    REQUIRE_FALSE( ScratchBuffer_Acquire(
        &buffer,
        { local, sizeof( local ) },
        nullptr,
        8u,
        3u ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_scratchBufferAssertCount ==
        3u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "ScratchBuffer accepts an empty request without allocator state",
           "[CypherCommon][Tier1][ScratchBuffer]" )
{
    scratch_buffer_t buffer{};
    REQUIRE( ScratchBuffer_Acquire(
        &buffer,
        {},
        nullptr,
        0u,
        16u ) );
    REQUIRE( ScratchBuffer_IsValid( &buffer ) );
    REQUIRE( ScratchBuffer_Size( &buffer ) == 0u );
    REQUIRE( ScratchBuffer_Data( &buffer ) == nullptr );
}
