//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Predicates.cpp
//  Purpose: Implements exact-sign Orient2D/Orient3D geometric predicates.
//  Details: Each predicate tries a plain-double filtered fast path first, and
//           only falls back to exact expansion arithmetic (CypherMath_Expansion.h)
//           when the filter cannot certify the sign. The exact fallback derives
//           each determinant from scratch as a fixed-size sum of exact products
//           (Shewchuk's classic, non-adaptive construction) rather than a tuned
//           adaptive-precision pipeline, trading some performance in the rare
//           near-degenerate case for a design that is easy to verify term by
//           term against the determinant formula it implements.
//
//  History:
//  - Created by Karlo Siric on 2026-09-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Predicates.h"
#include "CypherMath_Expansion.h"

#include "CypherCommon_Assert.h"

#include <cmath>
#include <utility>

namespace cypher::math
{

namespace
{

constexpr f64 kHalfEpsilon = 0.5 * common::CY_F64_EPSILON;

// Deliberately oversized relative to the tight bounds Shewchuk derives for his
// adaptive predicates: an oversized filter can only cost performance (more
// escalations to the exact path below), never correctness.
constexpr f64 kOrient2DErrorFactor = 16.0 * kHalfEpsilon;
constexpr f64 kOrient3DErrorFactor = 64.0 * kHalfEpsilon;

// Upper bound on any expansion length this file constructs (Orient3D's final
// merged expansion tops out at 192 terms), sized once so every scratch buffer
// below can share the same conservative capacity.
constexpr usize kMaxExpansionTerms = 192u;

// Upper bound on the length of an expansion ever passed as the "pM" argument
// to MultiplyExpansionByPair in this file (the largest is the 16-term minors
// M1/M2/M3 inside Orient3D).
constexpr usize kMaxScaleInputTerms = 16u;

// Fills outExpansion with the exact difference a - b as a nonoverlapping pair,
// ordered least-significant first to match the expansion convention used by
// ExpansionGrow/ExpansionSign.
void ExactDiffExpansion( f64 a, f64 b, f64 outExpansion[2] ) noexcept
{
    TwoDiff( a, b, &outExpansion[1], &outExpansion[0] );
}

// Merges expansion b (length cB) into expansion a (length cA), writing the
// exact sum as an expansion of length cA + cB to pOutput. pOutput must not
// alias pA or pB. Growing one term at a time via ExpansionGrow is exact and
// order-independent, so the two source expansions may be merged in either
// order without affecting the final sign.
void MergeExpansions(
    const f64 *pA, usize cA, const f64 *pB, usize cB, f64 *pOutput ) noexcept
{
    CY_ASSERT_MSG( cA + cB <= kMaxExpansionTerms, "MergeExpansions exceeded its scratch capacity." );

    // ExpansionGrow forbids output/input aliasing, so the running accumulator
    // must ping-pong between two scratch buffers as it lengthens by one term
    // per call.
    f64 scratchA[kMaxExpansionTerms];
    f64 scratchB[kMaxExpansionTerms];
    for ( usize i = 0u; i < cA; ++i ) {
        scratchA[i] = pA[i];
    }

    usize length = cA;
    f64 *pCurrent = scratchA;
    f64 *pNext = scratchB;
    for ( usize i = 0u; i < cB; ++i ) {
        ExpansionGrow( pCurrent, length, pB[i], pNext );
        ++length;
        std::swap( pCurrent, pNext );
    }

    for ( usize i = 0u; i < length; ++i ) {
        pOutput[i] = pCurrent[i];
    }
}

// Computes the exact product of expansion pM (length nM) with the two-term
// expansion (e + p), writing a nonoverlapping expansion of length 4 * nM to
// pOutput. This is how every exact multiplication in this file is built: an
// exact difference (from ExactDiffExpansion) is always a two-term expansion,
// so multiplying it against another expansion always reduces to this shape.
void MultiplyExpansionByPair(
    const f64 *pM, usize nM, f64 e, f64 p, f64 *pOutput ) noexcept
{
    CY_ASSERT_MSG( nM <= kMaxScaleInputTerms, "MultiplyExpansionByPair exceeded its scratch capacity." );

    f64 scaledByE[2u * kMaxScaleInputTerms];
    f64 scaledByP[2u * kMaxScaleInputTerms];
    ExpansionScale( pM, nM, e, scaledByE );
    ExpansionScale( pM, nM, p, scaledByP );
    MergeExpansions( scaledByE, 2u * nM, scaledByP, 2u * nM, pOutput );
}

void NegateExpansion( const f64 *pE, usize cE, f64 *pOutput ) noexcept
{
    for ( usize i = 0u; i < cE; ++i ) {
        pOutput[i] = -pE[i];
    }
}

// Exact 2x2 minor lhs.hi*rhs.hi... i.e. exact(lhsA)*exact(rhsA) - exact(lhsB)*exact(rhsB),
// where every operand is a two-term exact-difference expansion. Used both for
// Orient2D's determinant and for each 2x2 cofactor inside Orient3D.
void ExactCross(
    const f64 lhsA[2], const f64 rhsA[2],
    const f64 lhsB[2], const f64 rhsB[2],
    f64 *pOutput /* length 16 */ ) noexcept
{
    f64 productA[8];
    MultiplyExpansionByPair( rhsA, 2u, lhsA[0], lhsA[1], productA );
    f64 productB[8];
    MultiplyExpansionByPair( rhsB, 2u, lhsB[0], lhsB[1], productB );

    f64 negatedProductB[8];
    NegateExpansion( productB, 8u, negatedProductB );
    MergeExpansions( productA, 8u, negatedProductB, 8u, pOutput );
}

} // namespace

i32 Orient2D( vec2d_t a, vec2d_t b, vec2d_t c ) noexcept
{
    const f64 acx = a.x - c.x;
    const f64 acy = a.y - c.y;
    const f64 bcx = b.x - c.x;
    const f64 bcy = b.y - c.y;

    const f64 detLeft = acx * bcy;
    const f64 detRight = acy * bcx;
    const f64 det = detLeft - detRight;

    const f64 errorBound =
        kOrient2DErrorFactor * ( std::abs( detLeft ) + std::abs( detRight ) );
    if ( det > errorBound ) {
        return 1;
    }
    if ( det < -errorBound ) {
        return -1;
    }

    // The fast filter could not certify a sign; fall back to exact expansion
    // arithmetic so nearly-collinear inputs still resolve correctly.
    f64 exactAcx[2];
    ExactDiffExpansion( a.x, c.x, exactAcx );
    f64 exactAcy[2];
    ExactDiffExpansion( a.y, c.y, exactAcy );
    f64 exactBcx[2];
    ExactDiffExpansion( b.x, c.x, exactBcx );
    f64 exactBcy[2];
    ExactDiffExpansion( b.y, c.y, exactBcy );

    f64 finalExpansion[16];
    ExactCross( exactAcx, exactBcy, exactAcy, exactBcx, finalExpansion );

    return ExpansionSign( finalExpansion, 16u );
}

i32 Orient3D( vec3d_t a, vec3d_t b, vec3d_t c, vec3d_t d ) noexcept
{
    const f64 adx = a.x - d.x;
    const f64 ady = a.y - d.y;
    const f64 adz = a.z - d.z;
    const f64 bdx = b.x - d.x;
    const f64 bdy = b.y - d.y;
    const f64 bdz = b.z - d.z;
    const f64 cdx = c.x - d.x;
    const f64 cdy = c.y - d.y;
    const f64 cdz = c.z - d.z;

    const f64 bdxcdy = bdx * cdy;
    const f64 bdycdx = bdy * cdx;
    const f64 bdycdz = bdy * cdz;
    const f64 bdzcdy = bdz * cdy;
    const f64 bdxcdz = bdx * cdz;
    const f64 bdzcdx = bdz * cdx;

    const f64 det = adx * ( bdycdz - bdzcdy ) - ady * ( bdxcdz - bdzcdx ) +
                    adz * ( bdxcdy - bdycdx );

    const f64 permanent =
        std::abs( adx ) * ( std::abs( bdycdz ) + std::abs( bdzcdy ) ) +
        std::abs( ady ) * ( std::abs( bdxcdz ) + std::abs( bdzcdx ) ) +
        std::abs( adz ) * ( std::abs( bdxcdy ) + std::abs( bdycdx ) );
    const f64 errorBound = kOrient3DErrorFactor * permanent;
    if ( det > errorBound ) {
        return 1;
    }
    if ( det < -errorBound ) {
        return -1;
    }

    // The fast filter could not certify a sign; fall back to exact expansion
    // arithmetic so nearly-coplanar inputs still resolve correctly.
    f64 exactAdx[2];
    ExactDiffExpansion( a.x, d.x, exactAdx );
    f64 exactAdy[2];
    ExactDiffExpansion( a.y, d.y, exactAdy );
    f64 exactAdz[2];
    ExactDiffExpansion( a.z, d.z, exactAdz );
    f64 exactBdx[2];
    ExactDiffExpansion( b.x, d.x, exactBdx );
    f64 exactBdy[2];
    ExactDiffExpansion( b.y, d.y, exactBdy );
    f64 exactBdz[2];
    ExactDiffExpansion( b.z, d.z, exactBdz );
    f64 exactCdx[2];
    ExactDiffExpansion( c.x, d.x, exactCdx );
    f64 exactCdy[2];
    ExactDiffExpansion( c.y, d.y, exactCdy );
    f64 exactCdz[2];
    ExactDiffExpansion( c.z, d.z, exactCdz );

    // Each minor is a 16-term expansion exact for one 2x2 cofactor of the
    // determinant, mirroring Orient2D's own construction.
    f64 minorYZ[16]; // bdy*cdz - bdz*cdy
    ExactCross( exactBdy, exactCdz, exactBdz, exactCdy, minorYZ );
    f64 minorXZ[16]; // bdx*cdz - bdz*cdx
    ExactCross( exactBdx, exactCdz, exactBdz, exactCdx, minorXZ );
    f64 minorXY[16]; // bdx*cdy - bdy*cdx
    ExactCross( exactBdx, exactCdy, exactBdy, exactCdx, minorXY );

    f64 termX[64]; // adx * minorYZ
    MultiplyExpansionByPair( minorYZ, 16u, exactAdx[0], exactAdx[1], termX );
    f64 termY[64]; // ady * minorXZ
    MultiplyExpansionByPair( minorXZ, 16u, exactAdy[0], exactAdy[1], termY );
    f64 termZ[64]; // adz * minorXY
    MultiplyExpansionByPair( minorXY, 16u, exactAdz[0], exactAdz[1], termZ );

    f64 negatedTermY[64];
    NegateExpansion( termY, 64u, negatedTermY );
    f64 partialSum[128]; // termX - termY
    MergeExpansions( termX, 64u, negatedTermY, 64u, partialSum );

    f64 finalExpansion[192]; // (termX - termY) + termZ
    MergeExpansions( partialSum, 128u, termZ, 64u, finalExpansion );

    return ExpansionSign( finalExpansion, 192u );
}

} // namespace cypher::math
