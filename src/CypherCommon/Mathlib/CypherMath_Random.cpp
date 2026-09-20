//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Random.cpp
//  Purpose: Implements the deterministic PCG32 pseudorandom generator.
//  Details: Every operation is exact unsigned integer arithmetic with defined
//           wraparound, which is what makes a sequence reproducible across
//           platforms and build configurations.
//
//  History:
//  - Created by Karlo Siric on 2026-09-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Random.h"

#include "CypherCommon_Assert.h"

namespace cypher::math
{

namespace
{

// PCG32 XSH-RR constants. The multiplier is the 64-bit LCG multiplier from the
// published algorithm; the shift widths follow from splitting a 64-bit state
// into a 5-bit rotation selector and a 32-bit xorshifted output.
constexpr u64 kMultiplier = 6364136223846793005ull;

u32 AdvanceAndOutput( rng_t *pRng ) noexcept
{
    const u64 previousState = pRng->state;
    pRng->state = previousState * kMultiplier + pRng->increment;

    // XSH: fold the high bits down so the weakest low bits of the LCG never
    // reach the output on their own.
    const u32 xorshifted =
        static_cast<u32>( ( ( previousState >> 18u ) ^ previousState ) >> 27u );
    // RR: rotate by the top 5 bits, which is what breaks the LCG's lattice
    // structure and lifts this above a bare linear congruential generator.
    const u32 rotation = static_cast<u32>( previousState >> 59u );
    return ( xorshifted >> rotation ) | ( xorshifted << ( ( 32u - rotation ) & 31u ) );
}

} // namespace

rng_t Rng_Seed( u64 seed, u64 stream ) noexcept
{
    rng_t rng{};
    rng.state = 0ull;
    // The increment must be odd for the LCG to reach full period, so the stream
    // selector occupies the high 63 bits and bit zero is forced set.
    rng.increment = ( stream << 1u ) | 1ull;

    // Two warm-up draws mix the seed through the state; without them, nearby
    // seeds would produce visibly correlated first outputs.
    ( void )AdvanceAndOutput( &rng );
    rng.state += seed;
    ( void )AdvanceAndOutput( &rng );
    return rng;
}

u32 Rng_NextU32( rng_t *pRng ) noexcept
{
    const bool_t bValidState = pRng != nullptr;
    CY_ASSERT_MSG( bValidState, "Rng_NextU32 requires generator state." );
    if ( !bValidState ) {
        return 0u;
    }
    return AdvanceAndOutput( pRng );
}

u64 Rng_NextU64( rng_t *pRng ) noexcept
{
    const bool_t bValidState = pRng != nullptr;
    CY_ASSERT_MSG( bValidState, "Rng_NextU64 requires generator state." );
    if ( !bValidState ) {
        return 0ull;
    }
    // High word first so the 64-bit sequence is a fixed, documented pairing of
    // the 32-bit one rather than depending on argument evaluation order.
    const u64 high = AdvanceAndOutput( pRng );
    const u64 low = AdvanceAndOutput( pRng );
    return ( high << 32u ) | low;
}

u32 Rng_NextBoundedU32( rng_t *pRng, u32 bound ) noexcept
{
    const bool_t bValidState = pRng != nullptr;
    CY_ASSERT_MSG( bValidState, "Rng_NextBoundedU32 requires generator state." );
    if ( !bValidState || bound == 0u ) {
        return 0u;
    }

    // 2^32 % bound, computed without needing 64-bit width: the count of low
    // outputs that would make the modulo non-uniform. Discarding exactly those
    // leaves a range that bound divides evenly.
    const u32 threshold = ( 0u - bound ) % bound;
    for ( ;; ) {
        const u32 draw = AdvanceAndOutput( pRng );
        if ( draw >= threshold ) {
            return draw % bound;
        }
    }
}

f32 Rng_NextUnitF32( rng_t *pRng ) noexcept
{
    const bool_t bValidState = pRng != nullptr;
    CY_ASSERT_MSG( bValidState, "Rng_NextUnitF32 requires generator state." );
    if ( !bValidState ) {
        return 0.0f;
    }
    // 24 bits is f32's full mantissa width including the implicit leading bit,
    // so scaling by 2^-24 lands exactly on the representable grid of [0, 1).
    return static_cast<f32>( AdvanceAndOutput( pRng ) >> 8u ) * 0x1.0p-24f;
}

f64 Rng_NextUnitF64( rng_t *pRng ) noexcept
{
    const bool_t bValidState = pRng != nullptr;
    CY_ASSERT_MSG( bValidState, "Rng_NextUnitF64 requires generator state." );
    if ( !bValidState ) {
        return 0.0;
    }
    return static_cast<f64>( Rng_NextU64( pRng ) >> 11u ) * 0x1.0p-53;
}

f64 Rng_NextRangeF64( rng_t *pRng, f64 minimum, f64 maximum ) noexcept
{
    const bool_t bValidState = pRng != nullptr;
    CY_ASSERT_MSG( bValidState, "Rng_NextRangeF64 requires generator state." );
    if ( !bValidState ) {
        return 0.0;
    }
    if ( !Scalar_IsFinite( minimum ) || !Scalar_IsFinite( maximum ) ||
         !( maximum > minimum ) ) {
        return minimum;
    }

    const f64 unit = Rng_NextUnitF64( pRng );
    const f64 value = minimum + ( maximum - minimum ) * unit;
    // A wide interval can round the scaled result up onto the exclusive bound.
    return value < maximum ? value : minimum;
}

} // namespace cypher::math
