//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Functor_Tests.cpp
//  Purpose: Tests default comparison and hashing policies.
//  Details: Protects scalar ordering, content-based string equality, deterministic
//           hash behavior, floating zero normalization, and malformed-view handling.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Assert.h"
#include "CypherCommon_Functor.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

enum class test_key_t : i32 {
    Negative = -3,
    Positive = 9
};

u32 g_functorAssertCount = 0u;

assert_action_t CaptureFunctorAssert( const assert_info_t & ) noexcept
{
    ++g_functorAssertCount;
    return assert_action_t::Continue;
}

} // namespace

TEST_CASE( "Default comparison functors are empty stateless policies",
           "[CypherCommon][Tier1][Functor]" )
{
    STATIC_REQUIRE( sizeof( less_t<u32> ) == 1u );
    STATIC_REQUIRE( sizeof( equal_t<u32> ) == 1u );
    STATIC_REQUIRE( sizeof( hash_functor_t<u32> ) == 1u );

    constexpr less_t<i32> less{};
    constexpr equal_t<i32> equal{};
    STATIC_REQUIRE( less( -2, 4 ) );
    STATIC_REQUIRE_FALSE( less( 4, -2 ) );
    STATIC_REQUIRE( equal( 7, 7 ) );
    STATIC_REQUIRE_FALSE( equal( 7, 8 ) );
}

TEST_CASE( "String comparison policies use represented content instead of addresses",
           "[CypherCommon][Tier1][Functor]" )
{
    const char textA[] = { 'a', 'b', '\0', 'c' };
    const char textB[] = { 'a', 'b', '\0', 'c' };
    const char textC[] = { 'a', 'b', '\0', 'd' };
    const string_view_t viewA = StringView_FromRange( textA, sizeof( textA ) );
    const string_view_t viewB = StringView_FromRange( textB, sizeof( textB ) );
    const string_view_t viewC = StringView_FromRange( textC, sizeof( textC ) );

    const equal_t<string_view_t> equal{};
    const less_t<string_view_t> less{};
    REQUIRE( equal( viewA, viewB ) );
    REQUIRE_FALSE( equal( viewA, viewC ) );
    REQUIRE( less( viewA, viewC ) );
    REQUIRE_FALSE( less( viewC, viewA ) );
}

TEST_CASE( "Default scalar hashing is deterministic and value-sensitive",
           "[CypherCommon][Tier1][Functor]" )
{
    const hash_functor_t<u64> hashU64{};
    const hash_functor_t<i64> hashI64{};
    const hash_functor_t<test_key_t> hashEnum{};

    REQUIRE( hashU64( 42u ) == hashU64( 42u ) );
    REQUIRE( hashU64( 42u ) != hashU64( 43u ) );
    REQUIRE( hashI64( -1 ) == hashI64( -1 ) );
    REQUIRE( hashEnum( test_key_t::Negative ) != hashEnum( test_key_t::Positive ) );
}

TEST_CASE( "Floating hash normalization preserves the equality contract for zero",
           "[CypherCommon][Tier1][Functor]" )
{
    const hash_functor_t<f32> hashF32{};
    const hash_functor_t<f64> hashF64{};

    REQUIRE( 0.0f == -0.0f );
    REQUIRE( hashF32( 0.0f ) == hashF32( -0.0f ) );
    REQUIRE( hashF64( 0.0 ) == hashF64( -0.0 ) );
    REQUIRE( hashF64( 1.0 ) != hashF64( 2.0 ) );
}

TEST_CASE( "StringView hashing is content-based and length-bounded",
           "[CypherCommon][Tier1][Functor]" )
{
    const char textA[] = { 'c', 'y', '\0', 'x' };
    const char textB[] = { 'c', 'y', '\0', 'x' };
    const string_view_t viewA = StringView_FromRange( textA, sizeof( textA ) );
    const string_view_t viewB = StringView_FromRange( textB, sizeof( textB ) );
    const string_view_t prefix = StringView_FromRange( textA, 2u );
    const hash_functor_t<string_view_t> hashString{};

    REQUIRE( hashString( viewA ) == hashString( viewB ) );
    REQUIRE( hashString( viewA ) != hashString( prefix ) );
    REQUIRE( hashString( StringView_FromRange( nullptr, 0u ) ) ==
             hashString( StringView_FromRange( nullptr, 0u ) ) );
}

TEST_CASE( "StringView hashing reports malformed ranges",
           "[CypherCommon][Tier1][Functor]" )
{
    const string_view_t invalid{ nullptr, 3u };
    const hash_functor_t<string_view_t> hashString{};

    g_functorAssertCount = 0u;
    const assert_handler_t pPreviousHandler = Cy_AssertGetHandler();
    Cy_AssertSetHandler( CaptureFunctorAssert );

    const hash64_t hash = hashString( invalid );

    Cy_AssertSetHandler( pPreviousHandler );
    REQUIRE( hash != 0u );
    REQUIRE( g_functorAssertCount == static_cast<u32>( CYPHER_ASSERTS_ENABLED ) );
}

TEST_CASE( "Pointer hashing tracks pointer identity",
           "[CypherCommon][Tier1][Functor]" )
{
    i32 values[] = { 1, 2 };
    const hash_functor_t<i32 *> hashPointer{};

    REQUIRE( hashPointer( values ) == hashPointer( values ) );
    REQUIRE( hashPointer( values ) != hashPointer( values + 1u ) );
}
