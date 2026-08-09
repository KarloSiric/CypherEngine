//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Span_Tests.cpp
//  Purpose: Tests the Tier1 non-owning contiguous range contract.
//  Details: Protects null-state validity, bounded access, clamped slicing,
//           byte conversion, writable views, and byte-count overflow handling.
//
//  History:
//  - Created by Karlo Siric on 2026-08-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Span.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

u32 g_spanAssertCount = 0u;

assert_action_t CaptureSpanAssert( const assert_info_t & ) noexcept
{
    ++g_spanAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Span remains a lightweight non-owning value",
           "[CypherCommon][Tier1][Span]" )
{
    STATIC_REQUIRE( is_trivially_copyable_v<span_t<u32>> );
    STATIC_REQUIRE( is_standard_layout_v<span_t<u32>> );
    STATIC_REQUIRE( sizeof( span_t<u32> ) == sizeof( u32 * ) + sizeof( usize ) );
    STATIC_REQUIRE( is_same_v<decltype( Span_Make( static_cast<const u32 *>( nullptr ), 0u ) ), span_t<const u32>> );
}

TEST_CASE( "Span construction preserves valid empty and populated ranges",
           "[CypherCommon][Tier1][Span]" )
{
    u32 values[] = { 10u, 20u, 30u, 40u };
    const span_t<u32> fromArray = Span_FromArray( values );
    REQUIRE( Span_IsValid( fromArray ) );
    REQUIRE_FALSE( Span_IsEmpty( fromArray ) );
    REQUIRE( Span_Count( fromArray ) == 4u );
    REQUIRE( Span_Data( fromArray ) == values );

    const span_t<u32> canonicalEmpty = Span_Make<u32>( nullptr, 0u );
    REQUIRE( Span_IsValid( canonicalEmpty ) );
    REQUIRE( Span_IsEmpty( canonicalEmpty ) );
    REQUIRE( Span_Begin( canonicalEmpty ) == nullptr );
    REQUIRE( Span_End( canonicalEmpty ) == nullptr );

    const span_t<u32> positionedEmpty = Span_Make( values + 2u, 0u );
    REQUIRE( Span_IsValid( positionedEmpty ) );
    REQUIRE( Span_IsEmpty( positionedEmpty ) );
    REQUIRE( Span_Begin( positionedEmpty ) == values + 2u );
    REQUIRE( Span_End( positionedEmpty ) == values + 2u );
}

TEST_CASE( "Span construction rejects null non-empty ranges",
           "[CypherCommon][Tier1][Span]" )
{
    g_spanAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureSpanAssert );

    const span_t<u32> span = Span_Make<u32>( nullptr, 4u );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( g_spanAssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    REQUIRE( Span_IsValid( span ) );
    REQUIRE( Span_IsEmpty( span ) );
    REQUIRE( span.pData == nullptr );
}

TEST_CASE( "Span access returns represented elements and endpoints",
           "[CypherCommon][Tier1][Span]" )
{
    i32 values[] = { -4, 8, 15, 16, 23, 42 };
    const span_t<i32> span = Span_FromArray( values );

    REQUIRE( Span_Begin( span ) == values );
    REQUIRE( Span_End( span ) == values + 6u );
    REQUIRE( Span_At( span, 0u ) == values );
    REQUIRE( Span_At( span, 4u ) == values + 4u );
    REQUIRE( Span_Front( span ) == values );
    REQUIRE( Span_Back( span ) == values + 5u );
    REQUIRE( *Span_Back( span ) == 42 );
}

TEST_CASE( "Span access reports invalid state and bounds without dereferencing",
           "[CypherCommon][Tier1][Span]" )
{
    i32 values[] = { 1, 2, 3 };
    const span_t<i32> invalid{ nullptr, 3u };
    const span_t<i32> valid = Span_FromArray( values );
    const span_t<i32> empty = Span_Make( values + 3u, 0u );

    g_spanAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureSpanAssert );

    REQUIRE( Span_Data( invalid ) == nullptr );
    REQUIRE( Span_Begin( invalid ) == nullptr );
    REQUIRE( Span_End( invalid ) == nullptr );
    REQUIRE( Span_At( invalid, 0u ) == nullptr );
    REQUIRE( Span_At( valid, 3u ) == nullptr );
    REQUIRE( Span_Front( empty ) == nullptr );
    REQUIRE( Span_Back( empty ) == nullptr );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE(
        g_spanAssertCount ==
        7u * static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "Span slices clamp counts and preserve empty positions",
           "[CypherCommon][Tier1][Span]" )
{
    u16 values[] = { 2u, 4u, 6u, 8u, 10u };
    const span_t<u16> span = Span_FromArray( values );

    const span_t<u16> middle = Span_Subspan( span, 1u, 3u );
    REQUIRE( middle.pData == values + 1u );
    REQUIRE( middle.nCount == 3u );

    const span_t<u16> clamped = Span_Subspan( span, 3u, 99u );
    REQUIRE( clamped.pData == values + 3u );
    REQUIRE( clamped.nCount == 2u );

    const span_t<u16> atEnd = Span_Subspan( span, 5u, 2u );
    REQUIRE( atEnd.pData == values + 5u );
    REQUIRE( atEnd.nCount == 0u );

    const span_t<u16> prefix = Span_Prefix( span, 99u );
    REQUIRE( prefix.pData == values );
    REQUIRE( prefix.nCount == 5u );

    const span_t<u16> suffix = Span_Suffix( span, 2u );
    REQUIRE( suffix.pData == values + 3u );
    REQUIRE( suffix.nCount == 2u );

    const span_t<u16> emptySuffix = Span_Suffix( span, 0u );
    REQUIRE( emptySuffix.pData == values + 5u );
    REQUIRE( emptySuffix.nCount == 0u );
}

TEST_CASE( "Span out-of-range slicing asserts and safely returns the end position",
           "[CypherCommon][Tier1][Span]" )
{
    u8 values[] = { 1u, 2u, 3u };
    const span_t<u8> span = Span_FromArray( values );

    g_spanAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureSpanAssert );

    const span_t<u8> result = Span_Subspan( span, 99u, 1u );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( g_spanAssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
    REQUIRE( result.pData == values + 3u );
    REQUIRE( result.nCount == 0u );
}

TEST_CASE( "Span byte views preserve storage and writable access",
           "[CypherCommon][Tier1][Span]" )
{
    u32 values[] = { 0x11223344u, 0x55667788u };
    const span_t<u32> span = Span_FromArray( values );
    const const_byte_span_t bytes = Span_AsBytes( span );

    REQUIRE( bytes.pData == reinterpret_cast<const byte *>( values ) );
    REQUIRE( bytes.nCount == sizeof( values ) );
    REQUIRE( Span_SizeBytes( span ) == sizeof( values ) );

    byte_span_t writable = Span_AsWritableBytes( span );
    REQUIRE( writable.pData == reinterpret_cast<byte *>( values ) );
    REQUIRE( writable.nCount == sizeof( values ) );
    Cy_MemZero( writable.pData, writable.nCount );
    REQUIRE( Cy_MemIsZero( values, sizeof( values ) ) );

    const span_t<u32> positionedEmpty = Span_Make( values + 1u, 0u );
    const const_byte_span_t emptyBytes = Span_AsBytes( positionedEmpty );
    REQUIRE( emptyBytes.pData == reinterpret_cast<const byte *>( values + 1u ) );
    REQUIRE( emptyBytes.nCount == 0u );
}

TEST_CASE( "Span byte size rejects multiplication overflow",
           "[CypherCommon][Tier1][Span]" )
{
    u32 value = 0u;
    const span_t<u32> oversized{
        &value,
        CY_USIZE_MAX / sizeof( u32 ) + 1u
    };

    g_spanAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureSpanAssert );

    const usize cbSize = Span_SizeBytes( oversized );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( cbSize == 0u );
    REQUIRE( g_spanAssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}
