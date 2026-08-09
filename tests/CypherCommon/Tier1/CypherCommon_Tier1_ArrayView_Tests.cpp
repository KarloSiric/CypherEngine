//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ArrayView_Tests.cpp
//  Purpose: Tests the Tier1 read-only array-view compatibility API.
//  Details: Confirms ArrayView preserves const access and delegates construction,
//           validity, slicing, and byte conversion to the canonical Span contract.
//
//  History:
//  - Created by Karlo Siric on 2026-08-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ArrayView.h"
#include "CypherCommon_Assert.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_arrayViewAssertCount = 0u;

assert_action_t CaptureArrayViewAssert( const assert_info_t & ) noexcept
{
    ++g_arrayViewAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "ArrayView is exactly a const Span contract",
           "[CypherCommon][Tier1][ArrayView]" )
{
    STATIC_REQUIRE( is_same_v<array_view_t<u32>, span_t<const u32>> );
    STATIC_REQUIRE( is_same_v<
        decltype( Span_At( array_view_t<u32>{}, 0u ) ),
        const u32 *> );
    STATIC_REQUIRE( is_trivially_copyable_v<array_view_t<u32>> );
}

TEST_CASE( "ArrayView construction preserves array data and count",
           "[CypherCommon][Tier1][ArrayView]" )
{
    const u32 values[] = { 3u, 5u, 8u, 13u };
    const array_view_t<u32> view = ArrayView_FromArray( values );

    REQUIRE( Span_IsValid( view ) );
    REQUIRE( Span_Data( view ) == values );
    REQUIRE( Span_Count( view ) == 4u );
    REQUIRE( *Span_At( view, 2u ) == 8u );

    const array_view_t<u32> suffix = Span_Suffix( view, 2u );
    REQUIRE( suffix.pData == values + 2u );
    REQUIRE( suffix.nCount == 2u );

    const const_byte_span_t bytes = Span_AsBytes( view );
    REQUIRE( bytes.pData == reinterpret_cast<const byte *>( values ) );
    REQUIRE( bytes.nCount == sizeof( values ) );
}

TEST_CASE( "ArrayView uses Span null-state validation",
           "[CypherCommon][Tier1][ArrayView]" )
{
    g_arrayViewAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureArrayViewAssert );

    const array_view_t<u32> invalid = ArrayView_Make<u32>( nullptr, 2u );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( g_arrayViewAssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    REQUIRE( Span_IsValid( invalid ) );
    REQUIRE( Span_IsEmpty( invalid ) );
}
