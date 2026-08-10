//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_SparseSet_Tests.cpp
//  Purpose: Tests sparse-key lookup with packed dense value iteration.
//  Details: Covers sparse growth, replacement, swap erasure, moved-key repair,
//           dense spans, clear reuse, invalid sentinel keys, and shutdown.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_SparseSet.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "SparseSet maps sparse keys to packed dense values",
           "[CypherCommon][Tier1][SparseSet]" )
{
    sparse_set_t<u32> set{};
    REQUIRE( SparseSet_Init( &set, Allocator_GetSystem(), 4u ) );
    REQUIRE( SparseSet_Insert( &set, 2u, 20u ) != nullptr );
    REQUIRE( SparseSet_Insert( &set, 100u, 1000u ) != nullptr );
    REQUIRE( SparseSet_Insert( &set, 7u, 70u ) != nullptr );
    REQUIRE( SparseSet_Count( &set ) == 3u );
    REQUIRE( *SparseSet_Find( &set, 2u ) == 20u );
    REQUIRE( *SparseSet_Find( &set, 100u ) == 1000u );
    REQUIRE( SparseSet_Find( &set, 99u ) == nullptr );

    REQUIRE( SparseSet_Insert( &set, 7u, 77u ) != nullptr );
    REQUIRE( SparseSet_Count( &set ) == 3u );
    REQUIRE( *SparseSet_Find( &set, 7u ) == 77u );
    REQUIRE( SparseSet_Insert( &set, CY_U32_MAX, 1u ) == nullptr );
}

TEST_CASE( "SparseSet swap erase repairs the moved sparse mapping",
           "[CypherCommon][Tier1][SparseSet]" )
{
    sparse_set_t<u32> set{};
    REQUIRE( SparseSet_Init( &set, Allocator_GetSystem() ) );
    for ( u32 nKey : { 1u, 8u, 32u, 91u } ) {
        REQUIRE( SparseSet_Insert( &set, nKey, nKey * 10u ) != nullptr );
    }

    REQUIRE( SparseSet_Erase( &set, 8u ) );
    REQUIRE_FALSE( SparseSet_Contains( &set, 8u ) );
    REQUIRE( SparseSet_Count( &set ) == 3u );
    for ( u32 nKey : { 1u, 32u, 91u } ) {
        REQUIRE( SparseSet_Contains( &set, nKey ) );
        REQUIRE( *SparseSet_Find( &set, nKey ) == nKey * 10u );
    }
    REQUIRE_FALSE( SparseSet_Erase( &set, 400u ) );

    const sparse_set_t<u32> &constSet = set;
    const span_t<const u32> keys = SparseSet_Keys( &constSet );
    const span_t<const u32> values = SparseSet_Values( &constSet );
    REQUIRE( keys.nCount == values.nCount );
    for ( usize iDense = 0u; iDense < keys.nCount; ++iDense ) {
        REQUIRE( values.pData[iDense] == keys.pData[iDense] * 10u );
    }
}

TEST_CASE( "SparseSet clear retains sparse storage for reuse",
           "[CypherCommon][Tier1][SparseSet]" )
{
    sparse_set_t<u32> set{};
    REQUIRE( SparseSet_Init( &set, Allocator_GetSystem(), 128u ) );
    REQUIRE( SparseSet_Insert( &set, 64u, 5u ) != nullptr );
    const usize nSparseCount = set.sparse.nCount;

    SparseSet_Clear( &set );
    REQUIRE( SparseSet_IsEmpty( &set ) );
    REQUIRE( set.sparse.nCount == nSparseCount );
    REQUIRE_FALSE( SparseSet_Contains( &set, 64u ) );
    REQUIRE( SparseSet_Insert( &set, 64u, 9u ) != nullptr );
    REQUIRE( *SparseSet_Find( &set, 64u ) == 9u );

    SparseSet_Shutdown( &set );
    REQUIRE( SparseSet_IsValid( &set ) );
}
