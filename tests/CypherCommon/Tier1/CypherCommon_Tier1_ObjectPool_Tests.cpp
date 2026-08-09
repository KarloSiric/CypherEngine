//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ObjectPool_Tests.cpp
//  Purpose: Tests typed construction over fixed-block memory pools.
//  Details: Protects object lifetime, exhaustion, slot reuse, reset, shutdown,
//           alignment, foreign pointers, duplicate destroy, and allocation count.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_ObjectPool.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct object_lifetime_t {
    static inline u32 cConstructed = 0u;
    static inline u32 cDestroyed = 0u;

    i32 nValue{};

    explicit object_lifetime_t( i32 nInitialValue ) noexcept
        : nValue( nInitialValue )
    {
        ++cConstructed;
    }

    ~object_lifetime_t() noexcept
    {
        ++cDestroyed;
    }

    static void ResetCounts() noexcept
    {
        cConstructed = 0u;
        cDestroyed = 0u;
    }
};

struct alignas( 128 ) aligned_pool_object_t {
    u64 words[16]{};

    explicit aligned_pool_object_t( u64 nValue ) noexcept
    {
        words[0] = nValue;
    }
};

struct object_allocator_state_t {
    usize cAllocations{};
    usize cFrees{};
};

void *ObjectAllocate(
    void *pUserData,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<object_allocator_state_t *>( pUserData );
    ++pState->cAllocations;
    return Allocator_Allocate( Allocator_GetSystem(), cbSize, nAlignment );
}

void ObjectFree(
    void *pUserData,
    void *pMemory,
    usize cbSize,
    usize nAlignment ) noexcept
{
    auto *pState = static_cast<object_allocator_state_t *>( pUserData );
    ++pState->cFrees;
    Allocator_Free( Allocator_GetSystem(), pMemory, cbSize, nAlignment );
}

u32 g_objectPoolAssertCount = 0u;

assert_action_t CaptureObjectPoolAssert( const assert_info_t & ) noexcept
{
    ++g_objectPoolAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "ObjectPool constructs, exhausts, destroys, and reuses slots",
           "[CypherCommon][Tier1][ObjectPool]" )
{
    object_lifetime_t::ResetCounts();
    object_allocator_state_t allocatorState{};
    const allocator_t allocator{
        ObjectAllocate,
        nullptr,
        ObjectFree,
        &allocatorState
    };

    {
        object_pool_t<object_lifetime_t> pool{};
        REQUIRE( ObjectPool_Init( &pool, &allocator, 4u ) );
        REQUIRE( ObjectPool_IsValid( &pool ) );
        REQUIRE( ObjectPool_Capacity( &pool ) == 4u );
        REQUIRE( allocatorState.cAllocations == 1u );

        object_lifetime_t *objects[4]{};
        for ( i32 iObject = 0; iObject < 4; ++iObject ) {
            objects[iObject] = ObjectPool_Create( &pool, iObject + 1 );
            REQUIRE( objects[iObject] != nullptr );
            REQUIRE( objects[iObject]->nValue == iObject + 1 );
            REQUIRE( ObjectPool_Owns( &pool, objects[iObject] ) );
            REQUIRE( ObjectPool_IsLive( &pool, objects[iObject] ) );
        }
        REQUIRE( ObjectPool_Create( &pool, 9 ) == nullptr );
        REQUIRE( ObjectPool_LiveCount( &pool ) == 4u );
        REQUIRE( ObjectPool_FreeCount( &pool ) == 0u );

        object_lifetime_t *pReleasedSlot = objects[2];
        REQUIRE( ObjectPool_Destroy( &pool, pReleasedSlot ) );
        REQUIRE_FALSE( ObjectPool_IsLive( &pool, pReleasedSlot ) );
        object_lifetime_t *pReused = ObjectPool_Create( &pool, 17 );
        REQUIRE( pReused == pReleasedSlot );
        REQUIRE( pReused->nValue == 17 );
    }

    REQUIRE( object_lifetime_t::cConstructed == 5u );
    REQUIRE( object_lifetime_t::cDestroyed == 5u );
    REQUIRE( allocatorState.cFrees == 1u );
}

TEST_CASE( "ObjectPool reset destroys all live objects and recycles capacity",
           "[CypherCommon][Tier1][ObjectPool]" )
{
    object_lifetime_t::ResetCounts();
    object_pool_t<object_lifetime_t> pool{};
    REQUIRE( ObjectPool_Init( &pool, Allocator_GetSystem(), 8u ) );
    REQUIRE( ObjectPool_Create( &pool, 3 ) != nullptr );
    REQUIRE( ObjectPool_Create( &pool, 5 ) != nullptr );
    REQUIRE( ObjectPool_Create( &pool, 7 ) != nullptr );

    ObjectPool_Reset( &pool );
    REQUIRE( object_lifetime_t::cDestroyed == 3u );
    REQUIRE( ObjectPool_LiveCount( &pool ) == 0u );
    REQUIRE( ObjectPool_FreeCount( &pool ) == 8u );
    REQUIRE( ObjectPool_Create( &pool, 11 ) != nullptr );
}

TEST_CASE( "ObjectPool preserves over-aligned object addresses",
           "[CypherCommon][Tier1][ObjectPool]" )
{
    object_pool_t<aligned_pool_object_t> pool{};
    REQUIRE( ObjectPool_Init( &pool, Allocator_GetSystem(), 4u ) );
    aligned_pool_object_t *pObject = ObjectPool_Create( &pool, 37u );
    REQUIRE( pObject != nullptr );
    REQUIRE(
        reinterpret_cast<uintptr>( pObject ) %
        alignof( aligned_pool_object_t ) == 0u );
    REQUIRE( pObject->words[0] == 37u );
}

TEST_CASE( "ObjectPool rejects foreign and duplicate destruction",
           "[CypherCommon][Tier1][ObjectPool]" )
{
    object_pool_t<object_lifetime_t> pool{};
    REQUIRE( ObjectPool_Init( &pool, Allocator_GetSystem(), 2u ) );
    object_lifetime_t *pObject = ObjectPool_Create( &pool, 41 );
    REQUIRE( pObject != nullptr );
    object_lifetime_t foreign{ 43 };

    g_objectPoolAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureObjectPoolAssert );

    REQUIRE_FALSE( ObjectPool_Destroy( &pool, &foreign ) );
    REQUIRE( ObjectPool_Destroy( &pool, pObject ) );
    REQUIRE_FALSE( ObjectPool_Destroy( &pool, pObject ) );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_objectPoolAssertCount ==
        2u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "ObjectPool invalid operations assert and fail safely",
           "[CypherCommon][Tier1][ObjectPool]" )
{
    g_objectPoolAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureObjectPoolAssert );

    object_pool_t<object_lifetime_t> pool{};
    REQUIRE_FALSE( ObjectPool_Init<object_lifetime_t>(
        nullptr,
        Allocator_GetSystem(),
        2u ) );
    REQUIRE( ObjectPool_Create( &pool, 1 ) == nullptr );
    REQUIRE_FALSE( ObjectPool_Destroy(
        &pool,
        static_cast<object_lifetime_t *>( nullptr ) ) );
    ObjectPool_Reset( &pool );
    REQUIRE( ObjectPool_Capacity( &pool ) == 0u );
    ObjectPool_Shutdown<object_lifetime_t>( nullptr );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_objectPoolAssertCount ==
        6u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
