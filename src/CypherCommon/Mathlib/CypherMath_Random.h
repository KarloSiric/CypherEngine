//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Random.h
//  Purpose: Declares a deterministic, explicitly-seeded pseudorandom generator.
//  Details: State is caller-owned so that a sequence is reproducible from its
//           seed alone. There is no global generator and no hidden entropy.
//
//  History:
//  - Created by Karlo Siric on 2026-09-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Random Contract

This is a deterministic generator for reproducible work: bounded property tests,
procedural authoring, and simulation that must replay identically. It is NOT
cryptographically secure and must never be used for keys, tokens, or nonces --
CypherSecurity owns that.

The same (seed, stream) pair always produces the same sequence on every platform
and build configuration, because every operation is exact unsigned integer
arithmetic with defined wraparound. Two generators seeded with the same seed but
different stream values produce independent sequences, so subsystems can share
one run seed without correlating with each other.

ALGORITHM PROVENANCE: this is PCG32 (the XSH-RR variant) published by
Melissa O'Neill, "PCG: A Family of Simple Fast Space-Efficient Statistically
Good Algorithms for Random Number Generation" (2014). Written here from the
published algorithm description rather than copied from the reference
implementation; review licensing before shipping if that distinction matters
to you.
================
*/

#ifndef CYPHER_COMMON_MATH_RANDOM_H
#define CYPHER_COMMON_MATH_RANDOM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Scalar.h"

#include <type_traits>

namespace cypher::math
{

using common::u32;
using common::u64;

// Caller-owned generator state. Copying a rng_t forks the sequence: both copies
// then produce identical output, which is intentional and useful for replay,
// but is a bug if you expected two independent streams -- use Rng_Seed with
// different stream values for that.
struct rng_t {
    u64 state;     // Advances every draw.
    u64 increment; // Selects the stream; always odd after Rng_Seed.
};

// Returns a generator for the given seed and stream. Any seed and any stream
// value are valid, including zero for both.
CYPHER_NODISCARD CYPHER_MATH_API rng_t Rng_Seed( u64 seed, u64 stream ) noexcept;

// Uniform over the full 32-bit and 64-bit ranges respectively.
CYPHER_NODISCARD CYPHER_MATH_API u32 Rng_NextU32( CY_INOUT rng_t *pRng ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API u64 Rng_NextU64( CY_INOUT rng_t *pRng ) noexcept;

// Uniform over [0, bound). Rejection sampling removes the modulo bias that a
// bare `% bound` would introduce for bounds that do not divide 2^32. Returns 0
// when bound is 0, which is the only value with no valid result to return.
CYPHER_NODISCARD CYPHER_MATH_API u32 Rng_NextBoundedU32(
    CY_INOUT rng_t *pRng, u32 bound ) noexcept;

// Uniform over [0, 1). Never returns exactly 1.0. These consume the full
// mantissa width of their type (24 bits for f32, 53 for f64), so every
// representable value in the interval at that spacing is reachable.
CYPHER_NODISCARD CYPHER_MATH_API f32 Rng_NextUnitF32( CY_INOUT rng_t *pRng ) noexcept;
CYPHER_NODISCARD CYPHER_MATH_API f64 Rng_NextUnitF64( CY_INOUT rng_t *pRng ) noexcept;

// Uniform over [minimum, maximum). Returns minimum when the interval is empty
// or either bound is non-finite, so a malformed range yields a usable value
// rather than a NaN that would silently poison downstream arithmetic.
CYPHER_NODISCARD CYPHER_MATH_API f64 Rng_NextRangeF64(
    CY_INOUT rng_t *pRng, f64 minimum, f64 maximum ) noexcept;

static_assert( sizeof( rng_t ) == sizeof( u64 ) * 2u );
static_assert( std::is_standard_layout_v<rng_t> );
static_assert( std::is_trivially_copyable_v<rng_t> );

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_RANDOM_H
