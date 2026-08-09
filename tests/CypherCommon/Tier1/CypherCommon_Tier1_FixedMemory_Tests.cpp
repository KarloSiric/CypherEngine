//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_FixedMemory_Tests.cpp
//  Purpose: Tests non-owning bounded writable memory regions.
//  Details: Protects pointer/size invariants, half-open address semantics,
//           zero-length end ranges, offsets, and bounded subspans.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_FixedMemory.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_fixedMemoryAssertCount = 0u;

assert_action_t CaptureFixedMemoryAssert( const assert_info_t & ) noexcept
{
    ++g_fixedMemoryAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "FixedMemory preserves a valid caller-owned region",
           "[CypherCommon][Tier1][FixedMemory]" )
{
    byte storage[32]{};
    const fixed_memory_t memory = FixedMemory_Make( storage, sizeof( storage ) );

    STATIC_REQUIRE( is_trivially_copyable_v<fixed_memory_t> );
    STATIC_REQUIRE( is_standard_layout_v<fixed_memory_t> );
    REQUIRE( FixedMemory_IsValid( memory ) );
    REQUIRE_FALSE( FixedMemory_IsEmpty( memory ) );
    REQUIRE( FixedMemory_Data( memory ) == storage );
    REQUIRE( FixedMemory_Size( memory ) == sizeof( storage ) );

    const byte_span_t span = FixedMemory_Span( memory );
    REQUIRE( span.pData == storage );
    REQUIRE( span.nCount == sizeof( storage ) );
    REQUIRE( FixedMemory_FromSpan( span ).pData == storage );
}

TEST_CASE( "FixedMemory address and range checks use half-open semantics",
           "[CypherCommon][Tier1][FixedMemory]" )
{
    byte storage[16]{};
    const fixed_memory_t memory = FixedMemory_Make( storage, sizeof( storage ) );

    REQUIRE( FixedMemory_ContainsAddress( memory, storage ) );
    REQUIRE( FixedMemory_ContainsAddress( memory, storage + 15u ) );
    REQUIRE_FALSE( FixedMemory_ContainsAddress( memory, storage + 16u ) );
    REQUIRE_FALSE( FixedMemory_ContainsAddress( memory, nullptr ) );

    REQUIRE( FixedMemory_ContainsRange( memory, storage, 16u ) );
    REQUIRE( FixedMemory_ContainsRange( memory, storage + 8u, 8u ) );
    REQUIRE( FixedMemory_ContainsRange( memory, storage + 16u, 0u ) );
    REQUIRE_FALSE( FixedMemory_ContainsRange( memory, storage + 15u, 2u ) );
    REQUIRE_FALSE(
        FixedMemory_ContainsRange( memory, storage, CY_INVALID_SIZE ) );
}

TEST_CASE( "FixedMemory offset and subspan remain bounded",
           "[CypherCommon][Tier1][FixedMemory]" )
{
    byte storage[16]{};
    const fixed_memory_t memory = FixedMemory_Make( storage, sizeof( storage ) );

    REQUIRE( FixedMemory_OffsetOf( memory, storage ) == 0u );
    REQUIRE( FixedMemory_OffsetOf( memory, storage + 9u ) == 9u );
    REQUIRE( FixedMemory_OffsetOf( memory, storage + 16u ) == CY_INVALID_SIZE );

    const byte_span_t middle = FixedMemory_Subspan( memory, 4u, 6u );
    REQUIRE( middle.pData == storage + 4u );
    REQUIRE( middle.nCount == 6u );

    const byte_span_t clamped = FixedMemory_Subspan( memory, 12u, 32u );
    REQUIRE( clamped.pData == storage + 12u );
    REQUIRE( clamped.nCount == 4u );

    const byte_span_t end = FixedMemory_Subspan( memory, 16u, 4u );
    REQUIRE( end.pData == storage + 16u );
    REQUIRE( end.nCount == 0u );
}

TEST_CASE( "FixedMemory invalid construction asserts and canonicalizes",
           "[CypherCommon][Tier1][FixedMemory]" )
{
    g_fixedMemoryAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureFixedMemoryAssert );

    const fixed_memory_t invalid = FixedMemory_Make( nullptr, 8u );
    const byte_span_t invalidSpan{ nullptr, 8u };
    const fixed_memory_t fromInvalidSpan = FixedMemory_FromSpan( invalidSpan );
    byte storage[4]{};
    const fixed_memory_t valid = FixedMemory_Make( storage, sizeof( storage ) );
    const byte_span_t outside = FixedMemory_Subspan( valid, 5u, 1u );

    Cy_AssertSetHandler( pPreviousHandler );

    REQUIRE( FixedMemory_IsValid( invalid ) );
    REQUIRE( FixedMemory_IsEmpty( invalid ) );
    REQUIRE( FixedMemory_IsValid( fromInvalidSpan ) );
    REQUIRE( FixedMemory_IsEmpty( fromInvalidSpan ) );
    REQUIRE( outside.pData == storage + 4u );
    REQUIRE( outside.nCount == 0u );
    REQUIRE(
        g_fixedMemoryAssertCount ==
        3u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
