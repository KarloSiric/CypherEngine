//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Expansion.h
//  Purpose: Declares exact floating-point expansion arithmetic primitives.
//  Details: An expansion represents the exact value of a sum or product of
//           doubles as a set of nonoverlapping doubles whose plain sum equals
//           the true mathematical result bit-for-bit. These exist to give
//           Orient2D/Orient3D an exact fallback when their fast filtered path
//           is inconclusive; they are not meant as general-purpose arithmetic.
//
//  History:
//  - Created by Karlo Siric on 2026-09-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Expansion Contract

Every function here is exact under IEEE-754 round-to-nearest double arithmetic: no
rounding error is introduced beyond what the inputs already carried. TwoProduct's
error term uses std::fma, which is why this translation unit is compiled with
-ffp-contract=off -- the FMA use here is deliberate and explicit, and disabling
automatic contraction elsewhere in the file stops the compiler from silently
fusing other expressions in ways that would break TwoSum/TwoDiff's exactness.

An "expansion" is a small fixed-length array of f64, ordered from least to most
significant, where no term overlaps the bit range of the next. That nonoverlapping
property is what lets ExpansionSign determine the sign of the whole exact sum by
inspecting only the most significant nonzero term.
================
*/

#ifndef CYPHER_COMMON_MATH_EXPANSION_H
#define CYPHER_COMMON_MATH_EXPANSION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Annotations.h"
#include "CypherMath_API.h"
#include "CypherCommon_BaseTypes.h"

namespace cypher::math
{

using common::bool_t;
using common::f64;
using common::i32;
using common::usize;

// Exact a + b as a nonoverlapping pair: a + b == sum + error with no rounding lost.
CYPHER_MATH_API void TwoSum(
    f64 a, f64 b, CY_OUT f64 *pSum, CY_OUT f64 *pError ) noexcept;

// Same contract as TwoSum but requires the caller to already know |a| >= |b|;
// cheaper because it skips the branch-free reconstruction TwoSum needs otherwise.
CYPHER_MATH_API void FastTwoSum(
    f64 a, f64 b, CY_OUT f64 *pSum, CY_OUT f64 *pError ) noexcept;

// Exact a - b as a nonoverlapping pair.
CYPHER_MATH_API void TwoDiff(
    f64 a, f64 b, CY_OUT f64 *pDiff, CY_OUT f64 *pError ) noexcept;

// Exact a * b as a nonoverlapping pair, using std::fma for the error term.
CYPHER_MATH_API void TwoProduct(
    f64 a, f64 b, CY_OUT f64 *pProduct, CY_OUT f64 *pError ) noexcept;

// Adds scalar b into an existing nonoverlapping expansion e (length cE),
// writing a new nonoverlapping expansion of length cE + 1 to pOutput. pOutput
// must not alias pE. This is Shewchuk's "grow_expansion".
CYPHER_MATH_API void ExpansionGrow(
    CY_IN_READS( cE ) const f64 *pE,
    usize cE,
    f64 b,
    CY_OUT_WRITES( cE + 1 ) f64 *pOutput ) noexcept;

// Multiplies every term of expansion e (length cE) by scalar, writing a new
// nonoverlapping expansion of length 2 * cE to pOutput. pOutput must not alias
// pE. This is Shewchuk's "scale_expansion" (without zero elimination, which
// sign determination does not need).
CYPHER_MATH_API void ExpansionScale(
    CY_IN_READS( cE ) const f64 *pE,
    usize cE,
    f64 scalar,
    CY_OUT_WRITES( 2 * cE ) f64 *pOutput ) noexcept;

// Returns the sign of a nonoverlapping expansion: +1, -1, or 0 if every term is
// exactly zero. Exact because of the nonoverlapping property -- the first
// nonzero term found scanning from the most significant end dwarfs the sum of
// every term still unexamined, so it alone determines the sign of the total.
CYPHER_NODISCARD CYPHER_MATH_API i32 ExpansionSign(
    CY_IN_READS( cE ) const f64 *pE, usize cE ) noexcept;

} // namespace cypher::math

#endif // CYPHER_COMMON_MATH_EXPANSION_H
