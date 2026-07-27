//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier0/CypherCommon_Tier0_BaseTypes_Tests.cpp
//  Purpose: Tests Tier0 scalar and storage type contracts.
//  Details: These compile-time checks protect fixed-width layouts, runtime-sized
//           aliases, binary-size constants, limits, and stable Boolean storage.
//
//  History:
//  - Created by Karlo Siric on 2026-07-27
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BaseTypes.h"

#include <limits>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "BaseTypes fixed-width scalar layouts are stable", "[CypherCommon][Tier0][BaseTypes]" )
{
    STATIC_REQUIRE( sizeof( i8 ) == 1u );
    STATIC_REQUIRE( sizeof( i16 ) == 2u );
    STATIC_REQUIRE( sizeof( i32 ) == 4u );
    STATIC_REQUIRE( sizeof( i64 ) == 8u );
    STATIC_REQUIRE( sizeof( u8 ) == 1u );
    STATIC_REQUIRE( sizeof( u16 ) == 2u );
    STATIC_REQUIRE( sizeof( u32 ) == 4u );
    STATIC_REQUIRE( sizeof( u64 ) == 8u );
    STATIC_REQUIRE( sizeof( f32 ) == 4u );
    STATIC_REQUIRE( sizeof( f64 ) == 8u );
    STATIC_REQUIRE( sizeof( usize ) == sizeof( void * ) );
    STATIC_REQUIRE( sizeof( isize ) == sizeof( void * ) );
}

TEST_CASE( "BaseTypes signedness and floating-point formats are explicit", "[CypherCommon][Tier0][BaseTypes]" )
{
    STATIC_REQUIRE( std::is_signed_v<i8> );
    STATIC_REQUIRE( std::is_signed_v<i16> );
    STATIC_REQUIRE( std::is_signed_v<i32> );
    STATIC_REQUIRE( std::is_signed_v<i64> );
    STATIC_REQUIRE( std::is_unsigned_v<u8> );
    STATIC_REQUIRE( std::is_unsigned_v<u16> );
    STATIC_REQUIRE( std::is_unsigned_v<u32> );
    STATIC_REQUIRE( std::is_unsigned_v<u64> );
    STATIC_REQUIRE( std::numeric_limits<f32>::is_iec559 );
    STATIC_REQUIRE( std::numeric_limits<f64>::is_iec559 );
}

TEST_CASE( "BaseTypes binary-size constants use IEC scaling", "[CypherCommon][Tier0][BaseTypes]" )
{
    STATIC_REQUIRE( CY_BITS_PER_BYTE == 8u );
    STATIC_REQUIRE( CY_KIB == 1024u );
    STATIC_REQUIRE( CY_MIB == 1024u * 1024u );
    STATIC_REQUIRE( CY_GIB == 1024ull * 1024ull * 1024ull );
    STATIC_REQUIRE( CY_TIB == 1024ull * 1024ull * 1024ull * 1024ull );
    STATIC_REQUIRE( CY_DEFAULT_CACHE_LINE_SIZE == 64u );
}

TEST_CASE( "BaseTypes stable Boolean conversions normalize values", "[CypherCommon][Tier0][BaseTypes]" )
{
    STATIC_REQUIRE_FALSE( Cy_B8ToBool( b8::False ) );
    STATIC_REQUIRE( Cy_B8ToBool( b8::True ) );
    STATIC_REQUIRE( Cy_B8FromBool( CY_FALSE ) == b8::False );
    STATIC_REQUIRE( Cy_B8FromBool( CY_TRUE ) == b8::True );
}

TEST_CASE( "BaseTypes limits mirror the standard implementation", "[CypherCommon][Tier0][BaseTypes]" )
{
    STATIC_REQUIRE( CY_U32_MAX == std::numeric_limits<u32>::max() );
    STATIC_REQUIRE( CY_I32_MIN == std::numeric_limits<i32>::min() );
    STATIC_REQUIRE( CY_USIZE_MAX == std::numeric_limits<usize>::max() );
    STATIC_REQUIRE( CY_ISIZE_MIN == std::numeric_limits<isize>::min() );
    STATIC_REQUIRE( CY_F32_MAX == std::numeric_limits<f32>::max() );
    STATIC_REQUIRE( CY_F64_LOWEST == std::numeric_limits<f64>::lowest() );
    STATIC_REQUIRE( CY_INVALID_SIZE == CY_USIZE_MAX );
    STATIC_REQUIRE( CY_INVALID_OFFSET == std::numeric_limits<offset_t>::max() );
}
