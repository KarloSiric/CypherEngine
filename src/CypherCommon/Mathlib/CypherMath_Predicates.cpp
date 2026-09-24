//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Predicates.cpp
//  Purpose: Implements exact-sign geometric predicates.
//  Details: Each predicate tries a conservative plain-double filter first. An
//           uncertain circumcircle/circumsphere query uses a fixed-size floating
//           expansion of the lifted determinant when one exact power-of-two
//           normalization keeps every component representable. Inputs with a
//           wider exponent span use a fixed radix-2 superaccumulator instead;
//           this preserves the exact sign for every finite binary64 input without
//           heap allocation, long double, epsilon classification, or unbounded
//           storage.
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

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
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
constexpr f64 kInCircleErrorFactor = 256.0 * kHalfEpsilon;
constexpr f64 kInSphereErrorFactor = 1024.0 * kHalfEpsilon;

// Upper bound on any expansion length this file constructs (Orient3D's final
// merged expansion tops out at 192 terms), sized once so every scratch buffer
// below can share the same conservative capacity.
constexpr usize kMaxExpansionTerms = 192u;

// Upper bound on the length of an expansion ever passed as the "pM" argument
// to MultiplyExpansionByPair in this file (the largest is the 16-term minors
// M1/M2/M3 inside Orient3D).
constexpr usize kMaxScaleInputTerms = 16u;

// The lifted 4x4 determinant has 4! permutations and two monomials in its
// squared-length column. Each degree-four monomial occupies at most 2^(4-1)
// expansion components, hence 24 * 2 * 8 = 384 components. The lifted 5x5
// determinant analogously needs at most 5! * 3 * 2^(5-1) = 5760 components.
// These bounds deliberately include zeros; no zero-elimination assumption is
// hidden in the storage calculation.
constexpr usize kInCircleExpansionTerms = 384u;
constexpr usize kInSphereExpansionTerms = 5760u;
constexpr usize kMaxMonomialExpansionTerms = 16u;

// The wide-exponent fallback stores the lifted determinant as a signed base-2^32
// expansion. Every finite binary64 value has magnitude below 2^1024 and is an
// integer multiple of 2^-1074. InSphere monomials have degree five, so their
// common quantum is 2^-5370 and each is below 2^5120. Summing its 5! * 3 = 360
// monomials stays below 2^5129: 10499 magnitude bits suffice after applying the
// common quantum. 330 32-bit words provide 10560 bits. InCircle's degree-four,
// 48-monomial determinant is strictly smaller and shares the same storage.
constexpr i32 kWideBaseExponent = -5370;
constexpr usize kWideAccumulatorWords = 330u;
constexpr usize kSmallProductWords = 9u;

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

bool PermutationIsOdd( const usize *pColumns, usize count ) noexcept
{
    usize inversions = 0u;
    for ( usize i = 0u; i < count; ++i ) {
        for ( usize j = i + 1u; j < count; ++j ) {
            if ( pColumns[i] > pColumns[j] ) {
                ++inversions;
            }
        }
    }
    return ( inversions & 1u ) != 0u;
}

template<usize tCapacity>
struct expansion_accumulator_t final {
    f64 termsA[tCapacity]{};
    f64 termsB[tCapacity]{};
    f64 *pCurrent{ termsA };
    f64 *pScratch{ termsB };
    usize length{ 0u };
};

template<usize tCapacity>
void AppendExpansion(
    expansion_accumulator_t<tCapacity> *pAccumulator,
    const f64 *pTerms,
    usize termCount,
    bool bNegate ) noexcept
{
    CY_ASSERT_MSG( pAccumulator != nullptr, "AppendExpansion requires an accumulator." );
    CY_ASSERT_MSG( pTerms != nullptr || termCount == 0u, "AppendExpansion requires term storage." );
    CY_ASSERT_MSG(
        pAccumulator->length + termCount <= tCapacity,
        "AppendExpansion exceeded its proven determinant capacity." );

    for ( usize i = 0u; i < termCount; ++i ) {
        const f64 term = bNegate ? -pTerms[i] : pTerms[i];
        ExpansionGrow(
            pAccumulator->pCurrent,
            pAccumulator->length,
            term,
            pAccumulator->pScratch );
        ++pAccumulator->length;
        std::swap( pAccumulator->pCurrent, pAccumulator->pScratch );
    }
}

// A product of degree n in raw scalar inputs needs at most 2^(n - 1)
// components: the first scalar is exact already and each ExpansionScale doubles
// the length. Circumcircle/circumsphere monomials have degree four/five.
usize ExactScalarProduct(
    const f64 *pFactors,
    usize factorCount,
    f64 outExpansion[kMaxMonomialExpansionTerms] ) noexcept
{
    CY_ASSERT_MSG( pFactors != nullptr && factorCount > 0u, "ExactScalarProduct requires factors." );
    CY_ASSERT_MSG( factorCount <= 5u, "ExactScalarProduct degree exceeds its proven capacity." );

    f64 scratchA[kMaxMonomialExpansionTerms]{};
    f64 scratchB[kMaxMonomialExpansionTerms]{};
    scratchA[0] = pFactors[0];
    f64 *pCurrent = scratchA;
    f64 *pNext = scratchB;
    usize length = 1u;

    for ( usize i = 1u; i < factorCount; ++i ) {
        ExpansionScale( pCurrent, length, pFactors[i], pNext );
        length *= 2u;
        CY_ASSERT_MSG(
            length <= kMaxMonomialExpansionTerms,
            "ExactScalarProduct exceeded its expansion capacity." );
        std::swap( pCurrent, pNext );
    }

    for ( usize i = 0u; i < length; ++i ) {
        outExpansion[i] = pCurrent[i];
    }
    return length;
}

int ExactUnitExponent( f64 value ) noexcept
{
    const std::uint64_t bits = std::bit_cast<std::uint64_t>( value );
    const std::uint64_t exponentBits = ( bits >> 52u ) & UINT64_C( 0x7ff );
    if ( exponentBits == 0u ) {
        return -1074;
    }
    return static_cast<int>( exponentBits ) - 1023 - 52;
}

template<usize tPointCount, usize tDimension, usize tDegree>
bool PrepareScaledCoordinates(
    const f64 ( &input )[tPointCount][tDimension],
    f64 ( &output )[tPointCount][tDimension],
    bool *pExpansionSafe ) noexcept
{
    CY_ASSERT_MSG( pExpansionSafe != nullptr, "PrepareScaledCoordinates requires an output flag." );

    f64 maximumMagnitude = 0.0;
    for ( usize point = 0u; point < tPointCount; ++point ) {
        for ( usize axis = 0u; axis < tDimension; ++axis ) {
            maximumMagnitude = std::max( maximumMagnitude, std::abs( input[point][axis] ) );
        }
    }

    const int maximumExponent = maximumMagnitude == 0.0 ? 0 : std::ilogb( maximumMagnitude );
    const int scaleExponent = -maximumExponent;
    int minimumUnitExponent = 0;
    bool bFoundNonzero = false;

    for ( usize point = 0u; point < tPointCount; ++point ) {
        for ( usize axis = 0u; axis < tDimension; ++axis ) {
            const f64 original = input[point][axis];
            const f64 scaled = std::scalbn( original, scaleExponent );
            output[point][axis] = scaled;

            // A common power-of-two scale preserves the determinant sign only
            // if it preserves every input bit. Reverse the operation to catch
            // both flush-to-zero and rounded subnormal scaling.
            if ( std::scalbn( scaled, -scaleExponent ) != original ) {
                *pExpansionSafe = false;
                return false;
            }

            if ( scaled != 0.0 ) {
                const int unitExponent = ExactUnitExponent( scaled );
                if ( !bFoundNonzero || unitExponent < minimumUnitExponent ) {
                    minimumUnitExponent = unitExponent;
                }
                bFoundNonzero = true;
            }
        }
    }

    // The normalized coordinates have magnitude below two, so overflow is
    // impossible. This lower-bit bound guarantees that every degree-tDegree
    // exact product, including its error components, remains representable in
    // binary64 with gradual underflow.
    *pExpansionSafe = !bFoundNonzero ||
        static_cast<int>( tDegree ) * minimumUnitExponent >= -1074;
    return true;
}

bool FilterProduct( f64 a, f64 b, f64 *pProduct ) noexcept
{
    CY_ASSERT_MSG( pProduct != nullptr, "FilterProduct requires output storage." );
    *pProduct = a * b;
    if ( !std::isfinite( *pProduct ) ) {
        return false;
    }
    if ( a == 0.0 || b == 0.0 ) {
        return true;
    }
    // The classic relative-error filters assume normalized products. A
    // subnormal result is therefore an automatic escalation, never a sign.
    return std::abs( *pProduct ) >= std::numeric_limits<f64>::min();
}

bool FilterDet3(
    const f64 u[3],
    const f64 v[3],
    const f64 w[3],
    f64 *pDeterminant,
    f64 *pPermanent ) noexcept
{
    f64 uvw[6]{};
    bool bSafe = true;

    f64 pair = 0.0;
    bSafe = FilterProduct( u[0], v[1], &pair ) && bSafe;
    bSafe = FilterProduct( pair, w[2], &uvw[0] ) && bSafe;
    bSafe = FilterProduct( u[1], v[2], &pair ) && bSafe;
    bSafe = FilterProduct( pair, w[0], &uvw[1] ) && bSafe;
    bSafe = FilterProduct( u[2], v[0], &pair ) && bSafe;
    bSafe = FilterProduct( pair, w[1], &uvw[2] ) && bSafe;
    bSafe = FilterProduct( u[2], v[1], &pair ) && bSafe;
    bSafe = FilterProduct( pair, w[0], &uvw[3] ) && bSafe;
    bSafe = FilterProduct( u[1], v[0], &pair ) && bSafe;
    bSafe = FilterProduct( pair, w[2], &uvw[4] ) && bSafe;
    bSafe = FilterProduct( u[0], v[2], &pair ) && bSafe;
    bSafe = FilterProduct( pair, w[1], &uvw[5] ) && bSafe;

    *pDeterminant = ( uvw[0] + uvw[1] + uvw[2] ) - ( uvw[3] + uvw[4] + uvw[5] );
    *pPermanent = 0.0;
    for ( f64 term : uvw ) {
        *pPermanent += std::abs( term );
    }
    return bSafe && std::isfinite( *pDeterminant ) && std::isfinite( *pPermanent );
}

bool TryInCircleFilter( const f64 points[4][2], i32 *pSign ) noexcept
{
    const f64 adx = points[0][0] - points[3][0];
    const f64 ady = points[0][1] - points[3][1];
    const f64 bdx = points[1][0] - points[3][0];
    const f64 bdy = points[1][1] - points[3][1];
    const f64 cdx = points[2][0] - points[3][0];
    const f64 cdy = points[2][1] - points[3][1];

    f64 adxbdy = 0.0;
    f64 adybdx = 0.0;
    f64 bdxcdy = 0.0;
    f64 bdycdx = 0.0;
    f64 cdxady = 0.0;
    f64 cdyadx = 0.0;
    bool bSafe = FilterProduct( adx, bdy, &adxbdy );
    bSafe = FilterProduct( ady, bdx, &adybdx ) && bSafe;
    bSafe = FilterProduct( bdx, cdy, &bdxcdy ) && bSafe;
    bSafe = FilterProduct( bdy, cdx, &bdycdx ) && bSafe;
    bSafe = FilterProduct( cdx, ady, &cdxady ) && bSafe;
    bSafe = FilterProduct( cdy, adx, &cdyadx ) && bSafe;

    const f64 abdet = adxbdy - adybdx;
    const f64 bcdet = bdxcdy - bdycdx;
    const f64 cadet = cdxady - cdyadx;

    f64 adx2 = 0.0;
    f64 ady2 = 0.0;
    f64 bdx2 = 0.0;
    f64 bdy2 = 0.0;
    f64 cdx2 = 0.0;
    f64 cdy2 = 0.0;
    bSafe = FilterProduct( adx, adx, &adx2 ) && bSafe;
    bSafe = FilterProduct( ady, ady, &ady2 ) && bSafe;
    bSafe = FilterProduct( bdx, bdx, &bdx2 ) && bSafe;
    bSafe = FilterProduct( bdy, bdy, &bdy2 ) && bSafe;
    bSafe = FilterProduct( cdx, cdx, &cdx2 ) && bSafe;
    bSafe = FilterProduct( cdy, cdy, &cdy2 ) && bSafe;
    const f64 alift = adx2 + ady2;
    const f64 blift = bdx2 + bdy2;
    const f64 clift = cdx2 + cdy2;

    f64 adet = 0.0;
    f64 bdet = 0.0;
    f64 cdet = 0.0;
    bSafe = FilterProduct( alift, bcdet, &adet ) && bSafe;
    bSafe = FilterProduct( blift, cadet, &bdet ) && bSafe;
    bSafe = FilterProduct( clift, abdet, &cdet ) && bSafe;
    const f64 determinant = adet + bdet + cdet;

    f64 aPermanent = 0.0;
    f64 bPermanent = 0.0;
    f64 cPermanent = 0.0;
    bSafe = FilterProduct(
        alift, std::abs( bdxcdy ) + std::abs( bdycdx ), &aPermanent ) && bSafe;
    bSafe = FilterProduct(
        blift, std::abs( cdxady ) + std::abs( cdyadx ), &bPermanent ) && bSafe;
    bSafe = FilterProduct(
        clift, std::abs( adxbdy ) + std::abs( adybdx ), &cPermanent ) && bSafe;
    const f64 permanent = aPermanent + bPermanent + cPermanent;

    f64 errorBound = 0.0;
    bSafe = FilterProduct( kInCircleErrorFactor, permanent, &errorBound ) && bSafe;
    if ( !bSafe || !std::isfinite( determinant ) || !std::isfinite( permanent ) ||
         permanent == 0.0 ) {
        return false;
    }
    if ( determinant > errorBound ) {
        *pSign = 1;
        return true;
    }
    if ( determinant < -errorBound ) {
        *pSign = -1;
        return true;
    }
    return false;
}

bool TryInSphereFilter( const f64 points[5][3], i32 *pSign ) noexcept
{
    f64 relative[4][3]{};
    for ( usize point = 0u; point < 4u; ++point ) {
        for ( usize axis = 0u; axis < 3u; ++axis ) {
            relative[point][axis] = points[point][axis] - points[4][axis];
        }
    }

    f64 lift[4]{};
    bool bSafe = true;
    for ( usize point = 0u; point < 4u; ++point ) {
        for ( usize axis = 0u; axis < 3u; ++axis ) {
            f64 square = 0.0;
            bSafe = FilterProduct(
                relative[point][axis], relative[point][axis], &square ) && bSafe;
            lift[point] += square;
        }
    }

    f64 minorBCD = 0.0;
    f64 minorACD = 0.0;
    f64 minorABD = 0.0;
    f64 minorABC = 0.0;
    f64 permanentBCD = 0.0;
    f64 permanentACD = 0.0;
    f64 permanentABD = 0.0;
    f64 permanentABC = 0.0;
    bSafe = FilterDet3(
        relative[1], relative[2], relative[3], &minorBCD, &permanentBCD ) && bSafe;
    bSafe = FilterDet3(
        relative[0], relative[2], relative[3], &minorACD, &permanentACD ) && bSafe;
    bSafe = FilterDet3(
        relative[0], relative[1], relative[3], &minorABD, &permanentABD ) && bSafe;
    bSafe = FilterDet3(
        relative[0], relative[1], relative[2], &minorABC, &permanentABC ) && bSafe;

    f64 terms[4]{};
    bSafe = FilterProduct( lift[0], minorBCD, &terms[0] ) && bSafe;
    bSafe = FilterProduct( lift[1], minorACD, &terms[1] ) && bSafe;
    bSafe = FilterProduct( lift[2], minorABD, &terms[2] ) && bSafe;
    bSafe = FilterProduct( lift[3], minorABC, &terms[3] ) && bSafe;
    const f64 determinant = -terms[0] + terms[1] - terms[2] + terms[3];

    f64 permanentTerms[4]{};
    bSafe = FilterProduct( lift[0], permanentBCD, &permanentTerms[0] ) && bSafe;
    bSafe = FilterProduct( lift[1], permanentACD, &permanentTerms[1] ) && bSafe;
    bSafe = FilterProduct( lift[2], permanentABD, &permanentTerms[2] ) && bSafe;
    bSafe = FilterProduct( lift[3], permanentABC, &permanentTerms[3] ) && bSafe;
    const f64 permanent =
        permanentTerms[0] + permanentTerms[1] + permanentTerms[2] + permanentTerms[3];

    f64 errorBound = 0.0;
    bSafe = FilterProduct( kInSphereErrorFactor, permanent, &errorBound ) && bSafe;
    if ( !bSafe || !std::isfinite( determinant ) || !std::isfinite( permanent ) ||
         permanent == 0.0 ) {
        return false;
    }
    if ( determinant > errorBound ) {
        *pSign = 1;
        return true;
    }
    if ( determinant < -errorBound ) {
        *pSign = -1;
        return true;
    }
    return false;
}

i32 ExactInCircleExpansion( const f64 points[4][2] ) noexcept
{
    expansion_accumulator_t<kInCircleExpansionTerms> accumulator{};
    usize columns[4] = { 0u, 1u, 2u, 3u };
    do {
        const bool bPermutationNegative = PermutationIsOdd( columns, 4u );
        for ( usize liftAxis = 0u; liftAxis < 2u; ++liftAxis ) {
            f64 factors[4]{};
            usize factorCount = 0u;
            for ( usize row = 0u; row < 4u; ++row ) {
                if ( columns[row] < 2u ) {
                    factors[factorCount++] = points[row][columns[row]];
                } else if ( columns[row] == 2u ) {
                    factors[factorCount++] = points[row][liftAxis];
                    factors[factorCount++] = points[row][liftAxis];
                }
            }
            CY_ASSERT_MSG( factorCount == 4u, "InCircle monomial degree changed." );

            f64 product[kMaxMonomialExpansionTerms]{};
            const usize productLength = ExactScalarProduct( factors, factorCount, product );
            AppendExpansion( &accumulator, product, productLength, bPermutationNegative );
        }
    } while ( std::next_permutation( columns, columns + 4u ) );

    CY_ASSERT_MSG(
        accumulator.length == kInCircleExpansionTerms,
        "InCircle determinant did not fill its proven expansion length." );
    return ExpansionSign( accumulator.pCurrent, accumulator.length );
}

i32 ExactInSphereExpansion( const f64 points[5][3] ) noexcept
{
    expansion_accumulator_t<kInSphereExpansionTerms> accumulator{};
    usize columns[5] = { 0u, 1u, 2u, 3u, 4u };
    do {
        const bool bPermutationNegative = PermutationIsOdd( columns, 5u );
        for ( usize liftAxis = 0u; liftAxis < 3u; ++liftAxis ) {
            f64 factors[5]{};
            usize factorCount = 0u;
            for ( usize row = 0u; row < 5u; ++row ) {
                if ( columns[row] < 3u ) {
                    factors[factorCount++] = points[row][columns[row]];
                } else if ( columns[row] == 3u ) {
                    factors[factorCount++] = points[row][liftAxis];
                    factors[factorCount++] = points[row][liftAxis];
                }
            }
            CY_ASSERT_MSG( factorCount == 5u, "InSphere monomial degree changed." );

            f64 product[kMaxMonomialExpansionTerms]{};
            const usize productLength = ExactScalarProduct( factors, factorCount, product );
            AppendExpansion( &accumulator, product, productLength, bPermutationNegative );
        }
    } while ( std::next_permutation( columns, columns + 5u ) );

    CY_ASSERT_MSG(
        accumulator.length == kInSphereExpansionTerms,
        "InSphere determinant did not fill its proven expansion length." );
    return ExpansionSign( accumulator.pCurrent, accumulator.length );
}

struct binary64_factor_t final {
    std::uint64_t mantissa{ 0u };
    i32 exponent{ 0 };
    bool bNegative{ false };
};

bool DecodeBinary64( f64 value, binary64_factor_t *pFactor ) noexcept
{
    CY_ASSERT_MSG( pFactor != nullptr, "DecodeBinary64 requires output storage." );
    const std::uint64_t bits = std::bit_cast<std::uint64_t>( value );
    const std::uint64_t fraction = bits & UINT64_C( 0x000fffffffffffff );
    const std::uint64_t exponentBits = ( bits >> 52u ) & UINT64_C( 0x7ff );
    if ( exponentBits == 0u && fraction == 0u ) {
        return false;
    }

    pFactor->bNegative = ( bits >> 63u ) != 0u;
    if ( exponentBits == 0u ) {
        pFactor->mantissa = fraction;
        pFactor->exponent = -1074;
    } else {
        pFactor->mantissa = UINT64_C( 0x0010000000000000 ) | fraction;
        pFactor->exponent = static_cast<i32>( exponentBits ) - 1023 - 52;
    }
    return true;
}

struct small_product_t final {
    std::uint32_t words[kSmallProductWords]{};
    usize length{ 1u };
    i32 exponent{ 0 };
    bool bNegative{ false };
};

void MultiplySmallProduct( small_product_t *pProduct, std::uint64_t multiplier ) noexcept
{
    CY_ASSERT_MSG( pProduct != nullptr, "MultiplySmallProduct requires product storage." );
    const std::uint32_t multiplierWords[2] = {
        static_cast<std::uint32_t>( multiplier ),
        static_cast<std::uint32_t>( multiplier >> 32u )
    };
    const usize multiplierLength = multiplierWords[1] == 0u ? 1u : 2u;
    std::uint32_t result[kSmallProductWords]{};

    for ( usize i = 0u; i < pProduct->length; ++i ) {
        std::uint64_t carry = 0u;
        for ( usize j = 0u; j < multiplierLength; ++j ) {
            const usize resultIndex = i + j;
            CY_ASSERT_MSG(
                resultIndex < kSmallProductWords,
                "Small exact product exceeded its proven word capacity." );
            const std::uint64_t sum =
                static_cast<std::uint64_t>( result[resultIndex] ) +
                static_cast<std::uint64_t>( pProduct->words[i] ) * multiplierWords[j] + carry;
            result[resultIndex] = static_cast<std::uint32_t>( sum );
            carry = sum >> 32u;
        }

        usize resultIndex = i + multiplierLength;
        while ( carry != 0u ) {
            CY_ASSERT_MSG(
                resultIndex < kSmallProductWords,
                "Small exact product carry exceeded its proven word capacity." );
            const std::uint64_t sum =
                static_cast<std::uint64_t>( result[resultIndex] ) + carry;
            result[resultIndex] = static_cast<std::uint32_t>( sum );
            carry = sum >> 32u;
            ++resultIndex;
        }
    }

    usize resultLength = pProduct->length + multiplierLength;
    if ( resultLength > kSmallProductWords ) {
        resultLength = kSmallProductWords;
    }
    while ( resultLength > 1u && result[resultLength - 1u] == 0u ) {
        --resultLength;
    }
    for ( usize i = 0u; i < resultLength; ++i ) {
        pProduct->words[i] = result[i];
    }
    for ( usize i = resultLength; i < kSmallProductWords; ++i ) {
        pProduct->words[i] = 0u;
    }
    pProduct->length = resultLength;
}

bool BuildSmallProduct(
    const f64 *pFactors,
    usize factorCount,
    bool bNegative,
    small_product_t *pProduct ) noexcept
{
    CY_ASSERT_MSG( pFactors != nullptr && factorCount > 0u, "BuildSmallProduct requires factors." );
    CY_ASSERT_MSG( factorCount <= 5u, "BuildSmallProduct degree exceeds its proven capacity." );
    CY_ASSERT_MSG( pProduct != nullptr, "BuildSmallProduct requires output storage." );

    *pProduct = {};
    pProduct->words[0] = 1u;
    pProduct->bNegative = bNegative;
    for ( usize i = 0u; i < factorCount; ++i ) {
        binary64_factor_t factor{};
        if ( !DecodeBinary64( pFactors[i], &factor ) ) {
            return false;
        }
        MultiplySmallProduct( pProduct, factor.mantissa );
        pProduct->exponent += factor.exponent;
        pProduct->bNegative = pProduct->bNegative != factor.bNegative;
    }
    return true;
}

struct signed_wide_accumulator_t final {
    std::uint32_t words[kWideAccumulatorWords]{};
    bool bNegative{ false };
};

bool MagnitudeIsZero( const std::uint32_t *pWords ) noexcept
{
    for ( usize i = 0u; i < kWideAccumulatorWords; ++i ) {
        if ( pWords[i] != 0u ) {
            return false;
        }
    }
    return true;
}

i32 CompareMagnitudes( const std::uint32_t *pA, const std::uint32_t *pB ) noexcept
{
    for ( usize i = kWideAccumulatorWords; i > 0u; --i ) {
        if ( pA[i - 1u] > pB[i - 1u] ) {
            return 1;
        }
        if ( pA[i - 1u] < pB[i - 1u] ) {
            return -1;
        }
    }
    return 0;
}

void AddMagnitudes( std::uint32_t *pA, const std::uint32_t *pB ) noexcept
{
    std::uint64_t carry = 0u;
    for ( usize i = 0u; i < kWideAccumulatorWords; ++i ) {
        const std::uint64_t sum =
            static_cast<std::uint64_t>( pA[i] ) + pB[i] + carry;
        pA[i] = static_cast<std::uint32_t>( sum );
        carry = sum >> 32u;
    }
    CY_ASSERT_MSG( carry == 0u, "Exact determinant exceeded its proven accumulator capacity." );
}

void SubtractMagnitudes( std::uint32_t *pA, const std::uint32_t *pB ) noexcept
{
    std::uint64_t borrow = 0u;
    for ( usize i = 0u; i < kWideAccumulatorWords; ++i ) {
        const std::uint64_t minuend = pA[i];
        const std::uint64_t subtrahend = static_cast<std::uint64_t>( pB[i] ) + borrow;
        if ( minuend >= subtrahend ) {
            pA[i] = static_cast<std::uint32_t>( minuend - subtrahend );
            borrow = 0u;
        } else {
            pA[i] = static_cast<std::uint32_t>(
                ( UINT64_C( 1 ) << 32u ) + minuend - subtrahend );
            borrow = 1u;
        }
    }
    CY_ASSERT_MSG( borrow == 0u, "Exact determinant magnitude subtraction underflowed." );
}

void AddSignedMagnitude(
    signed_wide_accumulator_t *pAccumulator,
    const std::uint32_t *pMagnitude,
    bool bNegative ) noexcept
{
    if ( MagnitudeIsZero( pMagnitude ) ) {
        return;
    }
    if ( MagnitudeIsZero( pAccumulator->words ) ) {
        std::copy(
            pMagnitude,
            pMagnitude + kWideAccumulatorWords,
            pAccumulator->words );
        pAccumulator->bNegative = bNegative;
        return;
    }
    if ( pAccumulator->bNegative == bNegative ) {
        AddMagnitudes( pAccumulator->words, pMagnitude );
        return;
    }

    const i32 comparison = CompareMagnitudes( pAccumulator->words, pMagnitude );
    if ( comparison == 0 ) {
        std::fill(
            pAccumulator->words,
            pAccumulator->words + kWideAccumulatorWords,
            std::uint32_t{ 0u } );
        pAccumulator->bNegative = false;
    } else if ( comparison > 0 ) {
        SubtractMagnitudes( pAccumulator->words, pMagnitude );
    } else {
        std::uint32_t difference[kWideAccumulatorWords]{};
        std::copy( pMagnitude, pMagnitude + kWideAccumulatorWords, difference );
        SubtractMagnitudes( difference, pAccumulator->words );
        std::copy(
            difference,
            difference + kWideAccumulatorWords,
            pAccumulator->words );
        pAccumulator->bNegative = bNegative;
    }
}

void AccumulateWideProduct(
    signed_wide_accumulator_t *pAccumulator,
    const f64 *pFactors,
    usize factorCount,
    bool bNegative ) noexcept
{
    small_product_t product{};
    if ( !BuildSmallProduct( pFactors, factorCount, bNegative, &product ) ) {
        return;
    }

    const i32 shift = product.exponent - kWideBaseExponent;
    CY_ASSERT_MSG( shift >= 0, "Exact determinant product fell below its common quantum." );
    const usize wordOffset = static_cast<usize>( shift ) / 32u;
    const std::uint32_t bitOffset = static_cast<std::uint32_t>( shift ) & 31u;
    std::uint32_t magnitude[kWideAccumulatorWords]{};

    for ( usize i = 0u; i < product.length; ++i ) {
        const usize outputIndex = wordOffset + i;
        CY_ASSERT_MSG(
            outputIndex < kWideAccumulatorWords,
            "Exact determinant product exceeded its accumulator capacity." );
        const std::uint64_t shifted =
            static_cast<std::uint64_t>( product.words[i] ) << bitOffset;
        magnitude[outputIndex] |= static_cast<std::uint32_t>( shifted );
        if ( ( shifted >> 32u ) != 0u ) {
            CY_ASSERT_MSG(
                outputIndex + 1u < kWideAccumulatorWords,
                "Exact determinant product carry exceeded its accumulator capacity." );
            magnitude[outputIndex + 1u] |= static_cast<std::uint32_t>( shifted >> 32u );
        }
    }
    AddSignedMagnitude( pAccumulator, magnitude, product.bNegative );
}

i32 WideAccumulatorSign( const signed_wide_accumulator_t &accumulator ) noexcept
{
    if ( MagnitudeIsZero( accumulator.words ) ) {
        return 0;
    }
    return accumulator.bNegative ? -1 : 1;
}

i32 ExactInCircleWide( const f64 points[4][2] ) noexcept
{
    signed_wide_accumulator_t accumulator{};
    usize columns[4] = { 0u, 1u, 2u, 3u };
    do {
        const bool bPermutationNegative = PermutationIsOdd( columns, 4u );
        for ( usize liftAxis = 0u; liftAxis < 2u; ++liftAxis ) {
            f64 factors[4]{};
            usize factorCount = 0u;
            for ( usize row = 0u; row < 4u; ++row ) {
                if ( columns[row] < 2u ) {
                    factors[factorCount++] = points[row][columns[row]];
                } else if ( columns[row] == 2u ) {
                    factors[factorCount++] = points[row][liftAxis];
                    factors[factorCount++] = points[row][liftAxis];
                }
            }
            CY_ASSERT_MSG( factorCount == 4u, "InCircle wide monomial degree changed." );
            AccumulateWideProduct(
                &accumulator, factors, factorCount, bPermutationNegative );
        }
    } while ( std::next_permutation( columns, columns + 4u ) );
    return WideAccumulatorSign( accumulator );
}

i32 ExactInSphereWide( const f64 points[5][3] ) noexcept
{
    signed_wide_accumulator_t accumulator{};
    usize columns[5] = { 0u, 1u, 2u, 3u, 4u };
    do {
        const bool bPermutationNegative = PermutationIsOdd( columns, 5u );
        for ( usize liftAxis = 0u; liftAxis < 3u; ++liftAxis ) {
            f64 factors[5]{};
            usize factorCount = 0u;
            for ( usize row = 0u; row < 5u; ++row ) {
                if ( columns[row] < 3u ) {
                    factors[factorCount++] = points[row][columns[row]];
                } else if ( columns[row] == 3u ) {
                    factors[factorCount++] = points[row][liftAxis];
                    factors[factorCount++] = points[row][liftAxis];
                }
            }
            CY_ASSERT_MSG( factorCount == 5u, "InSphere wide monomial degree changed." );
            AccumulateWideProduct(
                &accumulator, factors, factorCount, bPermutationNegative );
        }
    } while ( std::next_permutation( columns, columns + 5u ) );
    return WideAccumulatorSign( accumulator );
}

} // namespace

i32 Orient2D( vec2d_t a, vec2d_t b, vec2d_t c ) noexcept
{
    // Non-finite input must be rejected before the filter, not after. Every
    // comparison against NaN is false, so NaN would slip past the fast path,
    // reach the exact fallback, and be skipped by ExpansionSign -- yielding 0,
    // which is indistinguishable from a genuinely collinear answer. A predicate
    // that reports confident degeneracy for garbage is worse than one that is
    // slow, because the caller acts on the result and corrupts topology.
    if ( !Vec2d_IsFinite( a ) || !Vec2d_IsFinite( b ) || !Vec2d_IsFinite( c ) ) {
        return 0;
    }

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
    // See Orient2D: non-finite input would otherwise be reported as exact
    // coplanarity rather than rejected.
    if ( !Vec3d_IsFinite( a ) || !Vec3d_IsFinite( b ) ||
         !Vec3d_IsFinite( c ) || !Vec3d_IsFinite( d ) ) {
        return 0;
    }

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

i32 InCircle( vec2d_t a, vec2d_t b, vec2d_t c, vec2d_t d ) noexcept
{
    if ( !Vec2d_IsFinite( a ) || !Vec2d_IsFinite( b ) ||
         !Vec2d_IsFinite( c ) || !Vec2d_IsFinite( d ) ) {
        return 0;
    }

    const f64 input[4][2] = {
        { a.x, a.y },
        { b.x, b.y },
        { c.x, c.y },
        { d.x, d.y }
    };
    f64 scaled[4][2]{};
    bool bExpansionSafe = false;
    if ( PrepareScaledCoordinates<4u, 2u, 4u>( input, scaled, &bExpansionSafe ) ) {
        i32 filteredSign = 0;
        if ( TryInCircleFilter( scaled, &filteredSign ) ) {
            return filteredSign;
        }
        if ( bExpansionSafe ) {
            return ExactInCircleExpansion( scaled );
        }
    }

    // A single binary64 expansion cannot represent determinant components
    // spanning more than the format's exponent range. Preserve those finite
    // inputs exactly with the bounded radix-2 expansion instead of silently
    // underflowing an allegedly exact fallback.
    return ExactInCircleWide( input );
}

i32 InSphere(
    vec3d_t a,
    vec3d_t b,
    vec3d_t c,
    vec3d_t d,
    vec3d_t e ) noexcept
{
    if ( !Vec3d_IsFinite( a ) || !Vec3d_IsFinite( b ) ||
         !Vec3d_IsFinite( c ) || !Vec3d_IsFinite( d ) || !Vec3d_IsFinite( e ) ) {
        return 0;
    }

    const f64 input[5][3] = {
        { a.x, a.y, a.z },
        { b.x, b.y, b.z },
        { c.x, c.y, c.z },
        { d.x, d.y, d.z },
        { e.x, e.y, e.z }
    };
    f64 scaled[5][3]{};
    bool bExpansionSafe = false;
    if ( PrepareScaledCoordinates<5u, 3u, 5u>( input, scaled, &bExpansionSafe ) ) {
        i32 filteredSign = 0;
        if ( TryInSphereFilter( scaled, &filteredSign ) ) {
            return filteredSign;
        }
        if ( bExpansionSafe ) {
            return ExactInSphereExpansion( scaled );
        }
    }

    return ExactInSphereWide( input );
}

} // namespace cypher::math
