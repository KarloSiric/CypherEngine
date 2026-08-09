//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Color_Tests.cpp
//  Purpose: Tests packed and linear color contracts.
//  Details: Exhaustive byte round trips and edge cases pin transfer curves,
//           alpha handling, interpolation, and host-independent packing.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Color.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

using namespace cypher::common;
using Catch::Approx;

TEST_CASE( "sRGB conversion follows the transfer curve and keeps alpha linear",
           "[CypherCommon][Tier1][Color]" )
{
    const colorf_t black = Color_SrgbToLinear( CY_COLOR32_BLACK );
    REQUIRE( black.r == 0.0f );
    REQUIRE( black.g == 0.0f );
    REQUIRE( black.b == 0.0f );
    REQUIRE( black.a == 1.0f );

    const colorf_t midpoint = Color_SrgbToLinear( { 128u, 128u, 128u, 128u } );
    REQUIRE( midpoint.r == Approx( 0.2158605f ).margin( 0.000001f ) );
    REQUIRE( midpoint.g == Approx( midpoint.r ) );
    REQUIRE( midpoint.b == Approx( midpoint.r ) );
    REQUIRE( midpoint.a == Approx( 128.0f / 255.0f ) );
}

TEST_CASE( "all byte channels survive sRGB linear round trips",
           "[CypherCommon][Tier1][Color]" )
{
    for ( u32 nValue = 0u; nValue <= 255u; ++nValue ) {
        const u8 value = static_cast<u8>( nValue );
        const color32_t source{ value, value, value, value };
        const color32_t roundTrip = Color_LinearToSrgb(
            Color_SrgbToLinear( source ) );
        REQUIRE( roundTrip.r == value );
        REQUIRE( roundTrip.g == value );
        REQUIRE( roundTrip.b == value );
        REQUIRE( roundTrip.a == value );
    }
}

TEST_CASE( "color clamp handles finite limits, infinities, and NaN",
           "[CypherCommon][Tier1][Color]" )
{
    const f32 infinity = std::numeric_limits<f32>::infinity();
    const f32 nan = std::numeric_limits<f32>::quiet_NaN();
    const colorf_t clamped = Color_Clamp( { -1.0f, 0.5f, infinity, nan } );
    REQUIRE( clamped.r == 0.0f );
    REQUIRE( clamped.g == 0.5f );
    REQUIRE( clamped.b == 1.0f );
    REQUIRE( clamped.a == 0.0f );
}

TEST_CASE( "color interpolation and alpha operations preserve their contracts",
           "[CypherCommon][Tier1][Color]" )
{
    const colorf_t midpoint = Color_Lerp(
        { 0.0f, 0.2f, 0.4f, 0.0f },
        { 1.0f, 0.6f, 0.8f, 1.0f },
        0.5f );
    REQUIRE( midpoint.r == Approx( 0.5f ) );
    REQUIRE( midpoint.g == Approx( 0.4f ) );
    REQUIRE( midpoint.b == Approx( 0.6f ) );
    REQUIRE( midpoint.a == Approx( 0.5f ) );

    const colorf_t extrapolated = Color_Lerp(
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f },
        2.0f );
    REQUIRE( extrapolated.r == 2.0f );

    const colorf_t source{ 0.8f, 0.4f, 0.2f, 0.5f };
    const colorf_t premultiplied = Color_PremultiplyAlpha( source );
    REQUIRE( premultiplied.r == Approx( 0.4f ) );
    REQUIRE( premultiplied.g == Approx( 0.2f ) );
    REQUIRE( premultiplied.b == Approx( 0.1f ) );
    REQUIRE( Color_UnpremultiplyAlpha( premultiplied ).r == Approx( source.r ) );

    const colorf_t transparent = Color_UnpremultiplyAlpha(
        { 1.0f, 1.0f, 1.0f, 0.0f } );
    REQUIRE( transparent.r == 0.0f );
    REQUIRE( transparent.g == 0.0f );
    REQUIRE( transparent.b == 0.0f );
    REQUIRE( transparent.a == 0.0f );
}

TEST_CASE( "packed color integers use fixed bit positions",
           "[CypherCommon][Tier1][Color]" )
{
    constexpr color32_t color{ 0x11u, 0x22u, 0x33u, 0x44u };
    REQUIRE( Color32_PackRGBA8( color ) == 0x44332211u );
    REQUIRE( Color32_PackBGRA8( color ) == 0x44112233u );

    const color32_t rgba = Color32_UnpackRGBA8( 0x44332211u );
    REQUIRE( rgba.r == color.r );
    REQUIRE( rgba.g == color.g );
    REQUIRE( rgba.b == color.b );
    REQUIRE( rgba.a == color.a );

    const color32_t bgra = Color32_UnpackBGRA8( 0x44112233u );
    REQUIRE( bgra.r == color.r );
    REQUIRE( bgra.g == color.g );
    REQUIRE( bgra.b == color.b );
    REQUIRE( bgra.a == color.a );
}
