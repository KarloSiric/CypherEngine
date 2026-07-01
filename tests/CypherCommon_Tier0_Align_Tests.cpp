#include "CypherCommon_Align.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

TEST_CASE( "Align power-of-two checks reject zero and non power-of-two values", "[CypherCommon][Tier0][Align]" )
{
    REQUIRE_FALSE( IsPowerOfTwo( 0u ) );
    REQUIRE( IsPowerOfTwo( 1u ) );
    REQUIRE( IsPowerOfTwo( 2u ) );
    REQUIRE_FALSE( IsPowerOfTwo( 3u ) );
    REQUIRE( IsPowerOfTwo( 64u ) );
    REQUIRE_FALSE( IsPowerOfTwo( 96u ) );
}

TEST_CASE( "Align value helpers round power-of-two alignments up and down", "[CypherCommon][Tier0][Align]" )
{
    REQUIRE( AlignUp( 0u, 8u ) == 0u );
    REQUIRE( AlignUp( 1u, 8u ) == 8u );
    REQUIRE( AlignUp( 8u, 8u ) == 8u );
    REQUIRE( AlignUp( 13u, 8u ) == 16u );
    REQUIRE( AlignUp( 17u, 8u ) == 24u );

    REQUIRE( AlignDown( 0u, 8u ) == 0u );
    REQUIRE( AlignDown( 1u, 8u ) == 0u );
    REQUIRE( AlignDown( 8u, 8u ) == 8u );
    REQUIRE( AlignDown( 17u, 8u ) == 16u );

    REQUIRE( IsAligned( 0u, 8u ) );
    REQUIRE( IsAligned( 16u, 8u ) );
    REQUIRE_FALSE( IsAligned( 17u, 8u ) );
}

TEST_CASE( "Align padding reports bytes needed to reach the next boundary", "[CypherCommon][Tier0][Align]" )
{
    REQUIRE( AlignPadding( 0u, 8u ) == 0u );
    REQUIRE( AlignPadding( 1u, 8u ) == 7u );
    REQUIRE( AlignPadding( 8u, 8u ) == 0u );
    REQUIRE( AlignPadding( 13u, 8u ) == 3u );
    REQUIRE( AlignPadding( 17u, 8u ) == 7u );
}

TEST_CASE( "Align checked helper rejects invalid alignment and overflow", "[CypherCommon][Tier0][Align]" )
{
    usize nOutValue = 0u;

    REQUIRE( AlignUpChecked( 13u, 8u, nOutValue ) );
    REQUIRE( nOutValue == 16u );

    REQUIRE_FALSE( AlignUpChecked( 13u, 0u, nOutValue ) );
    REQUIRE( nOutValue == 0u );

    REQUIRE_FALSE( AlignUpChecked( 13u, 3u, nOutValue ) );
    REQUIRE( nOutValue == 0u );

    REQUIRE_FALSE( AlignUpChecked( CY_INVALID_SIZE - 3u, 8u, nOutValue ) );
    REQUIRE( nOutValue == 0u );
}

TEST_CASE( "Align pointer helpers round pointer addresses and report padding", "[CypherCommon][Tier0][Align]" )
{
    alignas( 64 ) u8 nBytes[128] = {};
    void *pOffset = nBytes + 13u;
    const void *pConstOffset = nBytes + 17u;

    REQUIRE( IsPointerAligned( nBytes, 64u ) );
    REQUIRE_FALSE( IsPointerAligned( pOffset, 16u ) );

    REQUIRE( AlignPointerUp( pOffset, 16u ) == nBytes + 16u );
    REQUIRE( AlignPointerDown( pOffset, 16u ) == nBytes );
    REQUIRE( AlignPointerUp( pConstOffset, 16u ) == nBytes + 32u );
    REQUIRE( AlignPointerDown( pConstOffset, 16u ) == nBytes + 16u );

    REQUIRE( AlignPointerPadding( pOffset, 16u ) == 3u );

    uintptr nOutAddress = 0u;
    REQUIRE( AlignPointerUpChecked( pOffset, 16u, nOutAddress ) );
    REQUIRE( nOutAddress == reinterpret_cast<uintptr>( nBytes + 16u ) );
}
