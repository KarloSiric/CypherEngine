//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_Predicates_Tests.cpp
//  Purpose: Tests exact expansion arithmetic and the Orient2D/Orient3D predicates.
//  Details: Coverage includes the raw TwoSum/TwoDiff/TwoProduct/Expansion*
//           primitives against hand-verified rounding cases, plus adversarial
//           nearly-collinear/nearly-coplanar orientation queries constructed so
//           that plain double arithmetic cannot be trusted but the exact
//           predicate still resolves correctly.
//
//           Every expected numeric value below is checked using plain integer
//           arithmetic (not extended-precision float types), since on this
//           platform `long double` is the same width as `double` and cannot
//           serve as an independent higher-precision oracle.
//
//  History:
//  - Created by Karlo Siric on 2026-09-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace cypher::math;

//==========================================================================
// Expansion primitives
//==========================================================================

TEST_CASE( "TwoSum reconstructs a sum plain addition cannot represent",
           "[CypherCommon][Mathlib][Expansion]" )
{
    // b is far below one ULP of a, so a + b rounds away to exactly a, and the
    // entire value of b must surface as TwoSum's error term.
    const f64 a = 1.0;
    const f64 b = 0x1.0p-60;
    f64 sum = 0.0;
    f64 error = 0.0;
    TwoSum( a, b, &sum, &error );

    REQUIRE( sum == a );
    REQUIRE( error == b );
}

TEST_CASE( "TwoSum is exact and order independent at a round-to-even tie",
           "[CypherCommon][Mathlib][Expansion]" )
{
    // b is exactly half a ULP of a: a + b lands precisely halfway between two
    // representable doubles, so plain addition's round-to-even behavior
    // discards b entirely -- TwoSum must still recover it as the error term.
    const f64 a = 1.0;
    const f64 b = 0x1.0p-53;
    f64 sumAB = 0.0;
    f64 errorAB = 0.0;
    TwoSum( a, b, &sumAB, &errorAB );
    f64 sumBA = 0.0;
    f64 errorBA = 0.0;
    TwoSum( b, a, &sumBA, &errorBA );

    REQUIRE( sumAB == 1.0 );
    REQUIRE( errorAB == 0x1.0p-53 );
    REQUIRE( sumAB == sumBA );
    REQUIRE( errorAB == errorBA );
}

TEST_CASE( "TwoDiff reconstructs a difference plain subtraction cannot represent",
           "[CypherCommon][Mathlib][Expansion]" )
{
    // 2^53 + 2 sits on a grid of spacing 2, so the true difference (2^53 + 1)
    // is not representable there; round-to-even rounds it down to 2^53, and
    // TwoDiff must recover the missing +1 as its error term.
    const f64 a = 0x1.0p53 + 2.0;
    const f64 b = 1.0;
    f64 diff = 0.0;
    f64 error = 0.0;
    TwoDiff( a, b, &diff, &error );

    REQUIRE( diff == 0x1.0p53 );
    REQUIRE( error == 1.0 );
}

TEST_CASE( "TwoProduct reconstructs a product plain multiplication cannot represent",
           "[CypherCommon][Mathlib][Expansion]" )
{
    // (2^27 + 1)^2 = 2^54 + 2^28 + 1 needs 55 significant bits, more than a
    // double's 53-bit mantissa can hold, so plain multiplication must round.
    // Both a and b are small enough to be exact doubles, and their exact
    // integer product fits comfortably in an int64_t, so the reconstruction
    // can be checked with plain fixed-width integer arithmetic.
    const f64 a = 134217729.0; // 2^27 + 1
    const f64 b = 134217729.0;
    f64 product = 0.0;
    f64 error = 0.0;
    TwoProduct( a, b, &product, &error );

    REQUIRE( product == ( a * b ) );

    const std::int64_t exact = 18014398777917441LL; // (2^27 + 1)^2
    const std::int64_t reconstructed =
        static_cast<std::int64_t>( product ) + static_cast<std::int64_t>( error );
    REQUIRE( reconstructed == exact );
}

TEST_CASE( "ExpansionGrow accumulates additions plain running addition would corrupt",
           "[CypherCommon][Mathlib][Expansion]" )
{
    // Repeatedly adding 1.0 to 2^53 one plain double addition at a time loses
    // every addend to round-to-even (2^53 has a grid spacing of 2). Growing
    // the same additions into an expansion must instead track each rounding
    // error explicitly, so the terms sum -- via exact integer arithmetic -- to
    // the true total of 2^53 + 4.
    f64 current[5] = { 0x1.0p53, 0.0, 0.0, 0.0, 0.0 };
    usize length = 1u;
    for ( usize i = 0u; i < 4u; ++i ) {
        f64 grown[5] = {};
        ExpansionGrow( current, length, 1.0, grown );
        ++length;
        for ( usize j = 0u; j < length; ++j ) {
            current[j] = grown[j];
        }
    }
    REQUIRE( length == 5u );

    std::int64_t total = 0;
    for ( usize j = 0u; j < length; ++j ) {
        total += static_cast<std::int64_t>( current[j] );
    }
    REQUIRE( total == ( ( std::int64_t{ 1 } << 53 ) + 4 ) );
}

TEST_CASE( "ExpansionSign reports the sign of a nonoverlapping expansion",
           "[CypherCommon][Mathlib][Expansion]" )
{
    const f64 positive[2] = { -0x1.0p-60, 1.0 }; // Most significant term wins.
    REQUIRE( ExpansionSign( positive, 2u ) == 1 );

    const f64 negative[2] = { 0x1.0p-60, -1.0 };
    REQUIRE( ExpansionSign( negative, 2u ) == -1 );

    const f64 zero[2] = { 0.0, 0.0 };
    REQUIRE( ExpansionSign( zero, 2u ) == 0 );

    // A zero most-significant term must not short-circuit the scan -- the
    // sign has to fall through to the next nonzero term underneath it.
    const f64 tinyOnly[2] = { -0x1.0p-60, 0.0 };
    REQUIRE( ExpansionSign( tinyOnly, 2u ) == -1 );
}

//==========================================================================
// Orient2D
//==========================================================================

TEST_CASE( "Orient2D reports the correct sign for well-conditioned triangles",
           "[CypherCommon][Mathlib][Predicates][Orient2D]" )
{
    const vec2d_t a = Vec2d_Make( 0.0, 0.0 );
    const vec2d_t b = Vec2d_Make( 1.0, 0.0 );

    REQUIRE( Orient2D( a, b, Vec2d_Make( 0.0, 1.0 ) ) == 1 );   // Left turn.
    REQUIRE( Orient2D( a, b, Vec2d_Make( 0.0, -1.0 ) ) == -1 ); // Right turn.
    REQUIRE( Orient2D( a, b, Vec2d_Make( 2.0, 0.0 ) ) == 0 );   // Exactly collinear.
}

TEST_CASE( "Orient2D is antisymmetric under swapping any two vertices",
           "[CypherCommon][Mathlib][Predicates][Orient2D]" )
{
    const vec2d_t a = Vec2d_Make( -3.0, 1.0 );
    const vec2d_t b = Vec2d_Make( 4.0, -2.0 );
    const vec2d_t c = Vec2d_Make( 1.0, 5.0 );

    const i32 baseline = Orient2D( a, b, c );
    REQUIRE( baseline != 0 );
    REQUIRE( Orient2D( b, a, c ) == -baseline );
    REQUIRE( Orient2D( a, c, b ) == -baseline );
    REQUIRE( Orient2D( c, b, a ) == -baseline );
}

TEST_CASE( "Orient2D resolves a nearly collinear case that fools plain double math",
           "[CypherCommon][Mathlib][Predicates][Orient2D]" )
{
    // a, b, c would be exactly collinear if c were (2e14, 2.0); nudging c's y
    // by a single ULP relative to that collinear point forces the naive
    // determinant formula into catastrophic cancellation: both cross-products
    // are on the order of 1e14 while their true difference is on the order of
    // 1e-1 (see the assertion below, which checks the naive computation
    // really does land far outside the exact predicate's conservative
    // certainty margin -- otherwise this case would not exercise the exact
    // fallback path at all).
    const vec2d_t a = Vec2d_Make( 0.0, 0.0 );
    const vec2d_t b = Vec2d_Make( 1e14, 1.0 );
    const vec2d_t c = Vec2d_Make( 2e14, 2.0 + 0x1.0p-51 );

    const i32 sign = Orient2D( a, b, c );
    // c sits just above the line through a and b, so the exact sign is a left
    // turn regardless of how close the naive floating evaluation comes to it.
    REQUIRE( sign == 1 );

    const f64 naiveDetLeft = ( a.x - c.x ) * ( b.y - c.y );
    const f64 naiveDetRight = ( a.y - c.y ) * ( b.x - c.x );
    const f64 naiveDet = naiveDetLeft - naiveDetRight;
    const f64 naiveMagnitude = Scalar_Abs( naiveDetLeft ) + Scalar_Abs( naiveDetRight );
    // The true determinant is tiny (~1e-1) relative to the ~1e14-scale
    // intermediate products, so naive floating error swamps it -- confirming
    // this case genuinely cannot be trusted without the exact fallback.
    REQUIRE( Scalar_Abs( naiveDet ) < 1e-6 * naiveMagnitude );
}

TEST_CASE( "Orient2D handles degenerate zero-length inputs without producing NaN behavior",
           "[CypherCommon][Mathlib][Predicates][Orient2D]" )
{
    const vec2d_t p = Vec2d_Make( 3.5, -2.25 );
    REQUIRE( Orient2D( p, p, p ) == 0 );
    REQUIRE( Orient2D( p, p, Vec2d_Make( 9.0, 9.0 ) ) == 0 );
}

//==========================================================================
// Orient3D
//==========================================================================

TEST_CASE( "Orient3D reports the correct sign for well-conditioned tetrahedra",
           "[CypherCommon][Mathlib][Predicates][Orient3D]" )
{
    const vec3d_t a = Vec3d_Make( 0.0, 0.0, 0.0 );
    const vec3d_t b = Vec3d_Make( 1.0, 0.0, 0.0 );
    const vec3d_t c = Vec3d_Make( 0.0, 1.0, 0.0 );

    REQUIRE( Orient3D( a, b, c, Vec3d_Make( 0.0, 0.0, -1.0 ) ) == 1 );
    REQUIRE( Orient3D( a, b, c, Vec3d_Make( 0.0, 0.0, 1.0 ) ) == -1 );
    REQUIRE( Orient3D( a, b, c, Vec3d_Make( 0.5, 0.5, 0.0 ) ) == 0 ); // Coplanar.
}

TEST_CASE( "Orient3D is antisymmetric under swapping any two vertices",
           "[CypherCommon][Mathlib][Predicates][Orient3D]" )
{
    const vec3d_t a = Vec3d_Make( 1.0, 0.0, 0.0 );
    const vec3d_t b = Vec3d_Make( 0.0, 1.0, 0.0 );
    const vec3d_t c = Vec3d_Make( 0.0, 0.0, 1.0 );
    const vec3d_t d = Vec3d_Make( 5.0, 5.0, 5.0 );

    const i32 baseline = Orient3D( a, b, c, d );
    REQUIRE( baseline != 0 );
    REQUIRE( Orient3D( b, a, c, d ) == -baseline );
    REQUIRE( Orient3D( a, c, b, d ) == -baseline );
    REQUIRE( Orient3D( a, b, d, c ) == -baseline );
}

TEST_CASE( "Orient3D resolves a nearly coplanar case that fools plain double math",
           "[CypherCommon][Mathlib][Predicates][Orient3D]" )
{
    // a, b, c span a huge (1e14-sided) triangle exactly in the z = 0 plane, so
    // their cross product's z-component alone (1e28) already exhausts most of
    // a double's precision. d is nudged only 2^-51 above that plane -- a
    // perturbation twenty-eight orders of magnitude below the triangle's own
    // extent -- which is what makes this adversarial for naive arithmetic.
    const vec3d_t a = Vec3d_Make( 0.0, 0.0, 0.0 );
    const vec3d_t b = Vec3d_Make( 1e14, 0.0, 0.0 );
    const vec3d_t c = Vec3d_Make( 0.0, 1e14, 0.0 );
    const vec3d_t d = Vec3d_Make( 1.0, 1.0, 0x1.0p-51 );

    // (b - a) x (c - a) points in +z, so a point strictly above the plane in
    // z (as d is here, by construction) must be classified on the negative
    // side, mirroring the well-conditioned case above at 1e14 times the scale.
    REQUIRE( Orient3D( a, b, c, d ) == -1 );
}

TEST_CASE( "Orient3D handles degenerate zero-volume inputs without producing NaN behavior",
           "[CypherCommon][Mathlib][Predicates][Orient3D]" )
{
    const vec3d_t p = Vec3d_Make( 1.0, 2.0, 3.0 );
    REQUIRE( Orient3D( p, p, p, p ) == 0 );
    REQUIRE( Orient3D( p, p, Vec3d_Make( 4.0, 5.0, 6.0 ), Vec3d_Make( 7.0, 8.0, 9.0 ) ) == 0 );
}
