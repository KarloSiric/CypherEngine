//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_StringPool_Tests.cpp
//  Purpose: Tests stable allocator-backed string interning.
//  Details: Covers duplicate identity, block growth, case policy, empty strings,
//           clear semantics, allocation rollback, statistics, and invalid views.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_StringPool.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct pool_allocator_state_t {
    usize nAllocations{ 0u };
    usize nFrees{ 0u };
    bool_t bFailAllocations{ CY_FALSE };
};

void *PoolTestAllocate(
    void *pUserData,
    usize cbSize,
    usize nAlignment ) noexcept
{
    pool_allocator_state_t &state =
        *static_cast<pool_allocator_state_t *>( pUserData );
    if ( state.bFailAllocations ) {
        return nullptr;
    }
    ++state.nAllocations;
    return Allocator_GetSystem()->pfnAllocate(
        Allocator_GetSystem()->pUserData,
        cbSize,
        nAlignment );
}

void PoolTestFree(
    void *pUserData,
    void *pMemory,
    usize cbSize,
    usize nAlignment ) noexcept
{
    pool_allocator_state_t &state =
        *static_cast<pool_allocator_state_t *>( pUserData );
    ++state.nFrees;
    Allocator_GetSystem()->pfnFree(
        Allocator_GetSystem()->pUserData,
        pMemory,
        cbSize,
        nAlignment );
}

u32 g_stringPoolAssertCount = 0u;

assert_action_t CaptureStringPoolAssert( const assert_info_t & ) noexcept
{
    ++g_stringPoolAssertCount;
    return assert_action_t::Continue;
}

void RequireCStringEquals( const char *pValue, const char *pExpected )
{
    REQUIRE( pValue != nullptr );
    REQUIRE( StringView_Equals(
        StringView_FromCString( pValue ),
        StringView_FromCString( pExpected ) ) );
}

} // namespace

TEST_CASE( "StringPool returns one stable address for duplicate text",
           "[CypherCommon][Tier1][StringPool]" )
{
    string_pool_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    desc.nInitialBuckets = 8u;
    desc.cbInitialBlock = 16u;
    string_pool_t *pPool = StringPool_Create( desc );
    REQUIRE( pPool != nullptr );

    const char *pFirst = StringPool_Intern(
        pPool,
        StringView_FromCString( "renderer" ) );
    const char *pSecond = StringPool_Intern(
        pPool,
        StringView_FromCString( "materials/facility/wall.cymat" ) );
    const char *pThird = StringPool_Intern(
        pPool,
        StringView_FromCString( "entity" ) );
    REQUIRE( pFirst == StringPool_Intern(
        pPool,
        StringView_FromCString( "renderer" ) ) );
    RequireCStringEquals( pFirst, "renderer" );
    RequireCStringEquals( pSecond, "materials/facility/wall.cymat" );
    RequireCStringEquals( pThird, "entity" );
    REQUIRE( StringPool_Find(
        pPool,
        StringView_FromCString( "renderer" ) ) == pFirst );

    const string_pool_stats_t stats = StringPool_Stats( pPool );
    REQUIRE( stats.nStrings == 3u );
    REQUIRE( stats.cbStringData == 46u );
    REQUIRE( stats.cbReserved >= stats.cbStringData );
    REQUIRE( StringPool_IsValid( pPool ) );
    StringPool_Destroy( pPool );
}

TEST_CASE( "StringPool ASCII-insensitive mode preserves the first spelling",
           "[CypherCommon][Tier1][StringPool]" )
{
    string_pool_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    desc.flags = STRING_POOL_FLAG_CASE_INSENSITIVE_ASCII;
    string_pool_t *pPool = StringPool_Create( desc );
    REQUIRE( pPool != nullptr );

    const char *pOriginal = StringPool_Intern(
        pPool,
        StringView_FromCString( "Render.Debug" ) );
    const char *pDuplicate = StringPool_Intern(
        pPool,
        StringView_FromCString( "render.debug" ) );
    REQUIRE( pOriginal == pDuplicate );
    RequireCStringEquals( pOriginal, "Render.Debug" );
    REQUIRE( StringPool_Stats( pPool ).nStrings == 1u );
    StringPool_Destroy( pPool );
}

TEST_CASE( "StringPool supports empty text and clear resets all storage",
           "[CypherCommon][Tier1][StringPool]" )
{
    string_pool_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    string_pool_t *pPool = StringPool_Create( desc );
    REQUIRE( pPool != nullptr );

    const char *pEmpty = StringPool_Intern( pPool, {} );
    REQUIRE( pEmpty != nullptr );
    REQUIRE( pEmpty[0] == '\0' );
    REQUIRE( StringPool_Contains( pPool, {} ) );
    StringPool_Clear( pPool );
    REQUIRE( StringPool_IsValid( pPool ) );
    REQUIRE_FALSE( StringPool_Contains( pPool, {} ) );
    const string_pool_stats_t stats = StringPool_Stats( pPool );
    REQUIRE( stats.nStrings == 0u );
    REQUIRE( stats.cbStringData == 0u );
    REQUIRE( stats.cbReserved == 0u );
    StringPool_Destroy( pPool );
}

TEST_CASE( "StringPool allocation failure leaves the pool unchanged",
           "[CypherCommon][Tier1][StringPool]" )
{
    pool_allocator_state_t state{};
    const allocator_t allocator{
        PoolTestAllocate,
        nullptr,
        PoolTestFree,
        &state
    };
    string_pool_desc_t desc{};
    desc.pAllocator = &allocator;
    desc.nInitialBuckets = 8u;
    desc.cbInitialBlock = 64u;
    string_pool_t *pPool = StringPool_Create( desc );
    REQUIRE( pPool != nullptr );

    const string_pool_stats_t before = StringPool_Stats( pPool );
    state.bFailAllocations = CY_TRUE;
    REQUIRE( StringPool_Intern(
        pPool,
        StringView_FromCString( "allocation_failure" ) ) == nullptr );
    state.bFailAllocations = CY_FALSE;
    const string_pool_stats_t after = StringPool_Stats( pPool );
    REQUIRE( after.nStrings == before.nStrings );
    REQUIRE( after.cbStringData == before.cbStringData );
    REQUIRE( after.cbReserved == before.cbReserved );
    REQUIRE( StringPool_IsValid( pPool ) );

    REQUIRE( StringPool_Intern(
        pPool,
        StringView_FromCString( "allocation_failure" ) ) != nullptr );
    StringPool_Destroy( pPool );
    REQUIRE( state.nAllocations == state.nFrees );
}

TEST_CASE( "StringPool rejects invalid borrowed string ranges",
           "[CypherCommon][Tier1][StringPool]" )
{
    string_pool_desc_t desc{};
    desc.pAllocator = Allocator_GetSystem();
    string_pool_t *pPool = StringPool_Create( desc );
    REQUIRE( pPool != nullptr );

    g_stringPoolAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureStringPoolAssert );
    REQUIRE( StringPool_Intern( pPool, { nullptr, 1u } ) == nullptr );
    REQUIRE( StringPool_Find( pPool, { nullptr, 1u } ) == nullptr );
    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_stringPoolAssertCount ==
        2u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );

    StringPool_Destroy( pPool );
}
