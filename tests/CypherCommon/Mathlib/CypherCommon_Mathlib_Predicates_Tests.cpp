//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Mathlib/CypherCommon_Mathlib_Predicates_Tests.cpp
//  Purpose: Tests exact expansion arithmetic and exact-sign geometric predicates.
//  Details: Coverage includes the raw TwoSum/TwoDiff/TwoProduct/Expansion*
//           primitives against hand-verified rounding cases, plus adversarial
//           nearly-degenerate orientation and circumcircle/circumsphere queries
//           constructed so plain double arithmetic cannot be trusted but the
//           exact predicates still resolve correctly.
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
#include <limits>

using namespace cypher::math;

namespace
{

f64 NaiveInCircle( vec2d_t a, vec2d_t b, vec2d_t c, vec2d_t d ) noexcept
{
    const f64 adx = a.x - d.x;
    const f64 ady = a.y - d.y;
    const f64 bdx = b.x - d.x;
    const f64 bdy = b.y - d.y;
    const f64 cdx = c.x - d.x;
    const f64 cdy = c.y - d.y;
    const f64 abdet = adx * bdy - ady * bdx;
    const f64 bcdet = bdx * cdy - bdy * cdx;
    const f64 cadet = cdx * ady - cdy * adx;
    const f64 alift = adx * adx + ady * ady;
    const f64 blift = bdx * bdx + bdy * bdy;
    const f64 clift = cdx * cdx + cdy * cdy;
    return alift * bcdet + blift * cadet + clift * abdet;
}

f64 NaiveDet3( vec3d_t a, vec3d_t b, vec3d_t c ) noexcept
{
    return a.x * ( b.y * c.z - b.z * c.y ) -
           a.y * ( b.x * c.z - b.z * c.x ) +
           a.z * ( b.x * c.y - b.y * c.x );
}

f64 NaiveInSphere(
    vec3d_t a,
    vec3d_t b,
    vec3d_t c,
    vec3d_t d,
    vec3d_t e ) noexcept
{
    const vec3d_t ae = Vec3d_Subtract( a, e );
    const vec3d_t be = Vec3d_Subtract( b, e );
    const vec3d_t ce = Vec3d_Subtract( c, e );
    const vec3d_t de = Vec3d_Subtract( d, e );
    const f64 alift = Vec3d_Dot( ae, ae );
    const f64 blift = Vec3d_Dot( be, be );
    const f64 clift = Vec3d_Dot( ce, ce );
    const f64 dlift = Vec3d_Dot( de, de );
    return -alift * NaiveDet3( be, ce, de ) +
           blift * NaiveDet3( ae, ce, de ) -
           clift * NaiveDet3( ae, be, de ) +
           dlift * NaiveDet3( ae, be, ce );
}

std::int64_t IntegerDet3(
    const std::int64_t u[3],
    const std::int64_t v[3],
    const std::int64_t w[3] ) noexcept
{
    return u[0] * ( v[1] * w[2] - v[2] * w[1] ) -
           u[1] * ( v[0] * w[2] - v[2] * w[0] ) +
           u[2] * ( v[0] * w[1] - v[1] * w[0] );
}

i32 IntegerSign( std::int64_t value ) noexcept
{
    return value > 0 ? 1 : ( value < 0 ? -1 : 0 );
}

i32 IntegerInCircleSign( vec2d_t a, vec2d_t b, vec2d_t c, vec2d_t d ) noexcept
{
    const std::int64_t adx = static_cast<std::int64_t>( a.x - d.x );
    const std::int64_t ady = static_cast<std::int64_t>( a.y - d.y );
    const std::int64_t bdx = static_cast<std::int64_t>( b.x - d.x );
    const std::int64_t bdy = static_cast<std::int64_t>( b.y - d.y );
    const std::int64_t cdx = static_cast<std::int64_t>( c.x - d.x );
    const std::int64_t cdy = static_cast<std::int64_t>( c.y - d.y );
    const std::int64_t alift = adx * adx + ady * ady;
    const std::int64_t blift = bdx * bdx + bdy * bdy;
    const std::int64_t clift = cdx * cdx + cdy * cdy;
    const std::int64_t determinant =
        alift * ( bdx * cdy - bdy * cdx ) +
        blift * ( cdx * ady - cdy * adx ) +
        clift * ( adx * bdy - ady * bdx );
    return IntegerSign( determinant );
}

i32 IntegerInSphereSign(
    vec3d_t a,
    vec3d_t b,
    vec3d_t c,
    vec3d_t d,
    vec3d_t e ) noexcept
{
    const vec3d_t points[4] = { a, b, c, d };
    std::int64_t relative[4][3]{};
    std::int64_t lift[4]{};
    for ( usize point = 0u; point < 4u; ++point ) {
        relative[point][0] = static_cast<std::int64_t>( points[point].x - e.x );
        relative[point][1] = static_cast<std::int64_t>( points[point].y - e.y );
        relative[point][2] = static_cast<std::int64_t>( points[point].z - e.z );
        for ( usize axis = 0u; axis < 3u; ++axis ) {
            lift[point] += relative[point][axis] * relative[point][axis];
        }
    }

    const std::int64_t determinant =
        -lift[0] * IntegerDet3( relative[1], relative[2], relative[3] ) +
         lift[1] * IntegerDet3( relative[0], relative[2], relative[3] ) -
         lift[2] * IntegerDet3( relative[0], relative[1], relative[3] ) +
         lift[3] * IntegerDet3( relative[0], relative[1], relative[2] );
    return IntegerSign( determinant );
}

} // namespace

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

//==========================================================================
// InCircle
//==========================================================================

TEST_CASE( "InCircle classifies inside outside and exact cocircular points",
           "[CypherCommon][Mathlib][Predicates][InCircle]" )
{
    // a,b,c are counter-clockwise, so the documented positive-orientation
    // convention maps positive to inside and negative to outside.
    const vec2d_t a = Vec2d_Make( 1.0, 0.0 );
    const vec2d_t b = Vec2d_Make( 0.0, 1.0 );
    const vec2d_t c = Vec2d_Make( -1.0, 0.0 );

    REQUIRE( Orient2D( a, b, c ) == 1 );
    REQUIRE( InCircle( a, b, c, Vec2d_Make( 0.0, 0.0 ) ) == 1 );
    REQUIRE( InCircle( a, b, c, Vec2d_Make( 0.0, -2.0 ) ) == -1 );
    REQUIRE( InCircle( a, b, c, Vec2d_Make( 0.0, -1.0 ) ) == 0 );

    // Four nonzero integer-coordinate points on the radius-five circle centered
    // at (7,-11) exercise exact cancellation in the full lifted determinant.
    REQUIRE( InCircle(
                 Vec2d_Make( 12.0, -11.0 ),
                 Vec2d_Make( 7.0, -6.0 ),
                 Vec2d_Make( 2.0, -11.0 ),
                 Vec2d_Make( 7.0, -16.0 ) ) == 0 );
}

TEST_CASE( "InCircle determinant is alternating under row permutations",
           "[CypherCommon][Mathlib][Predicates][InCircle]" )
{
    const vec2d_t a = Vec2d_Make( 1.0, 0.0 );
    const vec2d_t b = Vec2d_Make( 0.0, 1.0 );
    const vec2d_t c = Vec2d_Make( -1.0, 0.0 );
    const vec2d_t d = Vec2d_Make( 0.25, -0.125 );
    const i32 baseline = InCircle( a, b, c, d );

    REQUIRE( baseline == 1 );
    REQUIRE( InCircle( b, a, c, d ) == -baseline ); // Defining orientation reverses.
    REQUIRE( InCircle( b, c, a, d ) == baseline );  // Even three-cycle.
    REQUIRE( InCircle( d, b, c, a ) == -baseline ); // Query/vertex row swap.
}

TEST_CASE( "InCircle is invariant under exact translation and positive power-of-two scale",
           "[CypherCommon][Mathlib][Predicates][InCircle]" )
{
    const vec2d_t a = Vec2d_Make( -3.0, -1.0 );
    const vec2d_t b = Vec2d_Make( 2.0, -2.0 );
    const vec2d_t c = Vec2d_Make( 1.0, 4.0 );
    const vec2d_t d = Vec2d_Make( 0.25, 0.5 );
    const i32 baseline = InCircle( a, b, c, d );
    REQUIRE( baseline != 0 );

    const vec2d_t translation = Vec2d_Make( 64.0, -32.0 );
    const f64 scale = 8.0;
    const auto transform = [translation, scale]( vec2d_t point ) noexcept {
        return Vec2d_Make(
            translation.x + scale * point.x,
            translation.y + scale * point.y );
    };
    REQUIRE( InCircle( transform( a ), transform( b ), transform( c ), transform( d ) ) ==
             baseline );
}

TEST_CASE( "InCircle resolves one-ULP radial perturbations that naive double rounds to zero",
           "[CypherCommon][Mathlib][Predicates][InCircle]" )
{
    constexpr f64 radius = 0x1.0p20;
    const vec2d_t a = Vec2d_Make( radius, 0.0 );
    const vec2d_t b = Vec2d_Make( 0.0, radius );
    const vec2d_t c = Vec2d_Make( -radius, 0.0 );
    const vec2d_t justInside = Vec2d_Make( 0.0, -0x1.fffffffffffffp19 );
    const vec2d_t justOutside = Vec2d_Make( 0.0, -0x1.0000000000001p20 );

    REQUIRE( NaiveInCircle( a, b, c, justInside ) == 0.0 );
    REQUIRE( NaiveInCircle( a, b, c, justOutside ) == 0.0 );
    REQUIRE( InCircle( a, b, c, justInside ) == 1 );
    REQUIRE( InCircle( a, b, c, justOutside ) == -1 );
}

TEST_CASE( "InCircle remains exact across the full finite binary64 exponent range",
           "[CypherCommon][Mathlib][Predicates][InCircle]" )
{
    const f64 tiny = std::numeric_limits<f64>::denorm_min();
    REQUIRE( InCircle(
                 Vec2d_Make( 0.0, 0.0 ),
                 Vec2d_Make( 1.0, 0.0 ),
                 Vec2d_Make( 0.0, 1.0 ),
                 Vec2d_Make( tiny, tiny ) ) == 1 );

    const f64 huge = std::numeric_limits<f64>::max();
    const vec2d_t hugeA = Vec2d_Make( huge, 0.0 );
    const vec2d_t hugeB = Vec2d_Make( 0.0, huge );
    const vec2d_t hugeC = Vec2d_Make( -huge, 0.0 );
    const vec2d_t tinyQuery = Vec2d_Make( tiny, tiny );
    REQUIRE( InCircle( hugeA, hugeB, hugeC, tinyQuery ) == 1 );
    REQUIRE( InCircle( hugeB, hugeA, hugeC, tinyQuery ) == -1 );
}

TEST_CASE( "InCircle reports exact determinant degeneracy for repeated defining vertices",
           "[CypherCommon][Mathlib][Predicates][InCircle]" )
{
    const vec2d_t repeated = Vec2d_Make( 2.0, -3.0 );
    REQUIRE( InCircle(
                 repeated,
                 repeated,
                 Vec2d_Make( 4.0, 1.0 ),
                 Vec2d_Make( -7.0, 9.0 ) ) == 0 );
}

TEST_CASE( "InCircle agrees with an independent integer oracle across deterministic samples",
           "[CypherCommon][Mathlib][Predicates][InCircle]" )
{
    const auto coordinate = []( i32 sample, i32 salt ) noexcept {
        return static_cast<std::int64_t>(
            ( sample * ( 2 * salt + 3 ) + salt * salt + 11 ) % 17 - 8 );
    };

    for ( i32 sample = 0; sample < 128; ++sample ) {
        const vec2d_t a = Vec2d_Make(
            static_cast<f64>( coordinate( sample, 1 ) ),
            static_cast<f64>( coordinate( sample, 2 ) ) );
        const vec2d_t b = Vec2d_Make(
            static_cast<f64>( coordinate( sample, 3 ) ),
            static_cast<f64>( coordinate( sample, 4 ) ) );
        const vec2d_t c = Vec2d_Make(
            static_cast<f64>( coordinate( sample, 5 ) ),
            static_cast<f64>( coordinate( sample, 6 ) ) );
        const vec2d_t d = Vec2d_Make(
            static_cast<f64>( coordinate( sample, 7 ) ),
            static_cast<f64>( coordinate( sample, 8 ) ) );
        REQUIRE( InCircle( a, b, c, d ) == IntegerInCircleSign( a, b, c, d ) );
    }
}

//==========================================================================
// InSphere
//==========================================================================

TEST_CASE( "InSphere classifies inside outside and exact cospherical points",
           "[CypherCommon][Mathlib][Predicates][InSphere]" )
{
    const vec3d_t a = Vec3d_Make( 1.0, 0.0, 0.0 );
    const vec3d_t b = Vec3d_Make( 0.0, 1.0, 0.0 );
    const vec3d_t c = Vec3d_Make( 0.0, 0.0, 1.0 );
    const vec3d_t d = Vec3d_Make( 0.0, 0.0, 0.0 );

    REQUIRE( Orient3D( a, b, c, d ) == 1 );
    REQUIRE( InSphere( a, b, c, d, Vec3d_Make( 0.5, 0.5, 0.5 ) ) == 1 );
    REQUIRE( InSphere( a, b, c, d, Vec3d_Make( 2.0, 2.0, 2.0 ) ) == -1 );
    REQUIRE( InSphere( a, b, c, d, Vec3d_Make( 1.0, 1.0, 1.0 ) ) == 0 );

    // Five nonzero integer-coordinate points on a translated radius-five
    // sphere force exact cancellation without relying on origin/zero shortcuts.
    REQUIRE( InSphere(
                 Vec3d_Make( 12.0, -11.0, 13.0 ),
                 Vec3d_Make( 7.0, -6.0, 13.0 ),
                 Vec3d_Make( 7.0, -11.0, 18.0 ),
                 Vec3d_Make( 2.0, -11.0, 13.0 ),
                 Vec3d_Make( 7.0, -16.0, 13.0 ) ) == 0 );
}

TEST_CASE( "InSphere determinant is alternating under row permutations",
           "[CypherCommon][Mathlib][Predicates][InSphere]" )
{
    const vec3d_t a = Vec3d_Make( 1.0, 0.0, 0.0 );
    const vec3d_t b = Vec3d_Make( 0.0, 1.0, 0.0 );
    const vec3d_t c = Vec3d_Make( 0.0, 0.0, 1.0 );
    const vec3d_t d = Vec3d_Make( 0.0, 0.0, 0.0 );
    const vec3d_t e = Vec3d_Make( 0.25, 0.375, 0.5 );
    const i32 baseline = InSphere( a, b, c, d, e );

    REQUIRE( baseline == 1 );
    REQUIRE( InSphere( b, a, c, d, e ) == -baseline );
    REQUIRE( InSphere( b, c, a, d, e ) == baseline );
    REQUIRE( InSphere( e, b, c, d, a ) == -baseline );
}

TEST_CASE( "InSphere is invariant under exact translation and positive power-of-two scale",
           "[CypherCommon][Mathlib][Predicates][InSphere]" )
{
    const vec3d_t a = Vec3d_Make( 1.0, 0.0, 0.0 );
    const vec3d_t b = Vec3d_Make( 0.0, 1.0, 0.0 );
    const vec3d_t c = Vec3d_Make( 0.0, 0.0, 1.0 );
    const vec3d_t d = Vec3d_Make( 0.0, 0.0, 0.0 );
    const vec3d_t e = Vec3d_Make( 0.25, 0.375, 0.5 );
    const i32 baseline = InSphere( a, b, c, d, e );
    REQUIRE( baseline != 0 );

    const vec3d_t translation = Vec3d_Make( 32.0, -64.0, 16.0 );
    const f64 scale = 4.0;
    const auto transform = [translation, scale]( vec3d_t point ) noexcept {
        return Vec3d_Make(
            translation.x + scale * point.x,
            translation.y + scale * point.y,
            translation.z + scale * point.z );
    };
    REQUIRE( InSphere(
                 transform( a ), transform( b ), transform( c ), transform( d ), transform( e ) ) ==
             baseline );
}

TEST_CASE( "InSphere resolves one-ULP radial perturbations that naive double rounds to zero",
           "[CypherCommon][Mathlib][Predicates][InSphere]" )
{
    constexpr f64 radius = 0x1.0p5;
    const vec3d_t a = Vec3d_Make( radius, 0.0, 0.0 );
    const vec3d_t b = Vec3d_Make( 0.0, radius, 0.0 );
    const vec3d_t c = Vec3d_Make( 0.0, 0.0, radius );
    const vec3d_t d = Vec3d_Make( -radius, 0.0, 0.0 );
    const vec3d_t justInside = Vec3d_Make( 0.0, -0x1.fffffffffffffp4, 0.0 );
    const vec3d_t justOutside = Vec3d_Make( 0.0, -0x1.0000000000001p5, 0.0 );

    REQUIRE( NaiveInSphere( a, b, c, d, justInside ) == 0.0 );
    REQUIRE( NaiveInSphere( a, b, c, d, justOutside ) == 0.0 );
    REQUIRE( InSphere( a, b, c, d, justInside ) == 1 );
    REQUIRE( InSphere( a, b, c, d, justOutside ) == -1 );
}

TEST_CASE( "InSphere remains exact across the full finite binary64 exponent range",
           "[CypherCommon][Mathlib][Predicates][InSphere]" )
{
    const vec3d_t a = Vec3d_Make( 1.0, 0.0, 0.0 );
    const vec3d_t b = Vec3d_Make( 0.0, 1.0, 0.0 );
    const vec3d_t c = Vec3d_Make( 0.0, 0.0, 1.0 );
    const vec3d_t d = Vec3d_Make( 0.0, 0.0, 0.0 );
    const f64 tiny = std::numeric_limits<f64>::denorm_min();
    REQUIRE( InSphere( a, b, c, d, Vec3d_Make( tiny, tiny, tiny ) ) == 1 );

    const f64 huge = std::numeric_limits<f64>::max();
    const vec3d_t hugeA = Vec3d_Make( huge, 0.0, 0.0 );
    const vec3d_t hugeB = Vec3d_Make( 0.0, huge, 0.0 );
    const vec3d_t hugeC = Vec3d_Make( 0.0, 0.0, huge );
    const vec3d_t hugeD = Vec3d_Make( -huge, 0.0, 0.0 );
    const vec3d_t tinyQuery = Vec3d_Make( tiny, tiny, tiny );
    REQUIRE( InSphere( hugeA, hugeB, hugeC, hugeD, tinyQuery ) == 1 );
    REQUIRE( InSphere( hugeB, hugeA, hugeC, hugeD, tinyQuery ) == -1 );
}

TEST_CASE( "InSphere reports exact determinant degeneracy for repeated defining vertices",
           "[CypherCommon][Mathlib][Predicates][InSphere]" )
{
    const vec3d_t repeated = Vec3d_Make( 2.0, -3.0, 4.0 );
    REQUIRE( InSphere(
                 repeated,
                 repeated,
                 Vec3d_Make( 1.0, 0.0, 0.0 ),
                 Vec3d_Make( 0.0, 1.0, 0.0 ),
                 Vec3d_Make( -7.0, 9.0, 5.0 ) ) == 0 );
}

TEST_CASE( "InSphere agrees with an independent integer oracle across deterministic samples",
           "[CypherCommon][Mathlib][Predicates][InSphere]" )
{
    const auto coordinate = []( i32 sample, i32 salt ) noexcept {
        return static_cast<std::int64_t>(
            ( sample * ( 2 * salt + 5 ) + salt * salt + 7 ) % 17 - 8 );
    };
    const auto point = [coordinate]( i32 sample, i32 salt ) noexcept {
        return Vec3d_Make(
            static_cast<f64>( coordinate( sample, salt ) ),
            static_cast<f64>( coordinate( sample, salt + 1 ) ),
            static_cast<f64>( coordinate( sample, salt + 2 ) ) );
    };

    for ( i32 sample = 0; sample < 96; ++sample ) {
        const vec3d_t a = point( sample, 1 );
        const vec3d_t b = point( sample, 4 );
        const vec3d_t c = point( sample, 7 );
        const vec3d_t d = point( sample, 10 );
        const vec3d_t e = point( sample, 13 );
        REQUIRE( InSphere( a, b, c, d, e ) == IntegerInSphereSign( a, b, c, d, e ) );
    }
}

TEST_CASE( "exact-sign predicates reject non-finite input up front",
           "[CypherCommon][Mathlib][Predicates]" )
{
    // Without an explicit guard these slip through: every comparison against
    // NaN is false, so NaN passes the filter untouched, reaches the exact
    // fallback, and is skipped by ExpansionSign -- producing 0. The guard makes
    // that rejection deliberate instead of accidental, and avoids running an
    // exact fallback over garbage.
    //
    // NOTE: 0 is also the legitimate "exactly degenerate" answer. The i32
    // result cannot distinguish the two, so a caller needing that distinction
    // must validate first; Cypher::EditorGeometry's Kernel owns that for
    // authored geometry.
    const f64 nan = std::numeric_limits<f64>::quiet_NaN();
    const f64 infinity = std::numeric_limits<f64>::infinity();

    REQUIRE( Orient2D( Vec2d_Make( nan, 0.0 ), Vec2d_Make( 1.0, 0.0 ),
                       Vec2d_Make( 0.0, 1.0 ) ) == 0 );
    REQUIRE( Orient2D( Vec2d_Make( 0.0, 0.0 ), Vec2d_Make( infinity, 0.0 ),
                       Vec2d_Make( 0.0, 1.0 ) ) == 0 );
    REQUIRE( Orient2D( Vec2d_Make( 0.0, 0.0 ), Vec2d_Make( 1.0, 0.0 ),
                       Vec2d_Make( 0.0, -infinity ) ) == 0 );

    REQUIRE( Orient3D( Vec3d_Make( nan, 0.0, 0.0 ), Vec3d_Make( 1.0, 0.0, 0.0 ),
                       Vec3d_Make( 0.0, 1.0, 0.0 ),
                       Vec3d_Make( 0.0, 0.0, 1.0 ) ) == 0 );
    REQUIRE( Orient3D( Vec3d_Make( 0.0, 0.0, 0.0 ), Vec3d_Make( 1.0, 0.0, 0.0 ),
                       Vec3d_Make( 0.0, infinity, 0.0 ),
                       Vec3d_Make( 0.0, 0.0, 1.0 ) ) == 0 );

    REQUIRE( InCircle(
                 Vec2d_Make( 1.0, 0.0 ),
                 Vec2d_Make( 0.0, 1.0 ),
                 Vec2d_Make( -1.0, 0.0 ),
                 Vec2d_Make( nan, 0.0 ) ) == 0 );
    REQUIRE( InCircle(
                 Vec2d_Make( infinity, 0.0 ),
                 Vec2d_Make( 0.0, 1.0 ),
                 Vec2d_Make( -1.0, 0.0 ),
                 Vec2d_Make( 0.0, 0.0 ) ) == 0 );

    REQUIRE( InSphere(
                 Vec3d_Make( 1.0, 0.0, 0.0 ),
                 Vec3d_Make( 0.0, 1.0, 0.0 ),
                 Vec3d_Make( 0.0, 0.0, 1.0 ),
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( nan, 0.0, 0.0 ) ) == 0 );
    REQUIRE( InSphere(
                 Vec3d_Make( 1.0, 0.0, 0.0 ),
                 Vec3d_Make( 0.0, 1.0, 0.0 ),
                 Vec3d_Make( 0.0, 0.0, infinity ),
                 Vec3d_Make( 0.0, 0.0, 0.0 ),
                 Vec3d_Make( 0.25, 0.25, 0.25 ) ) == 0 );

    // Finite input next to the same call sites must still resolve normally --
    // the guard must not have swallowed the valid path.
    REQUIRE( Orient2D( Vec2d_Make( 0.0, 0.0 ), Vec2d_Make( 1.0, 0.0 ),
                       Vec2d_Make( 0.0, 1.0 ) ) == 1 );
    REQUIRE( InCircle(
                 Vec2d_Make( 1.0, 0.0 ),
                 Vec2d_Make( 0.0, 1.0 ),
                 Vec2d_Make( -1.0, 0.0 ),
                 Vec2d_Make( 0.0, 0.0 ) ) == 1 );
}
