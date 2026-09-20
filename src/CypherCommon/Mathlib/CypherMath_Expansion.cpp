//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_Expansion.cpp
//  Purpose: Implements exact floating-point expansion arithmetic primitives.
//  Details: This translation unit is compiled with -ffp-contract=off; TwoProduct
//           is the one place that deliberately opts back into fused multiply-add
//           via std::fma. Everything else here relies on plain IEEE-754
//           round-to-nearest double arithmetic being exactly that -- no silent
//           compiler fusion of unrelated expressions.
//
//  History:
//  - Created by Karlo Siric on 2026-09-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_Expansion.h"
#include "CypherCommon_Assert.h"

#include <cmath>

namespace cypher::math
{

void TwoSum( f64 a, f64 b, f64 *pSum, f64 *pError ) noexcept
{
    const bool_t bValidOutput = pSum != nullptr && pError != nullptr;
    CY_ASSERT_MSG( bValidOutput, "TwoSum requires output storage." );
    if ( !bValidOutput ) {
        return;
    }
    const f64 sum = a + b;
    const f64 bVirtual = sum - a;
    const f64 aVirtual = sum - bVirtual;
    const f64 bRoundoff = b - bVirtual;
    const f64 aRoundoff = a - aVirtual;
    *pSum = sum;
    *pError = aRoundoff + bRoundoff;
}

void FastTwoSum( f64 a, f64 b, f64 *pSum, f64 *pError ) noexcept
{
    const bool_t bValidOutput = pSum != nullptr && pError != nullptr;
    CY_ASSERT_MSG( bValidOutput, "FastTwoSum requires output storage." );
    if ( !bValidOutput ) {
        return;
    }
    const f64 sum = a + b;
    const f64 bVirtual = sum - a;
    *pSum = sum;
    *pError = b - bVirtual;
}

void TwoDiff( f64 a, f64 b, f64 *pDiff, f64 *pError ) noexcept
{
    const bool_t bValidOutput = pDiff != nullptr && pError != nullptr;
    CY_ASSERT_MSG( bValidOutput, "TwoDiff requires output storage." );
    if ( !bValidOutput ) {
        return;
    }
    const f64 diff = a - b;
    const f64 bVirtual = a - diff;
    const f64 aVirtual = diff + bVirtual;
    const f64 bRoundoff = bVirtual - b;
    const f64 aRoundoff = a - aVirtual;
    *pDiff = diff;
    *pError = aRoundoff + bRoundoff;
}

void TwoProduct( f64 a, f64 b, f64 *pProduct, f64 *pError ) noexcept
{
    const bool_t bValidOutput = pProduct != nullptr && pError != nullptr;
    CY_ASSERT_MSG( bValidOutput, "TwoProduct requires output storage." );
    if ( !bValidOutput ) {
        return;
    }
    const f64 product = a * b;
    // fma(a, b, -product) computes a*b - product with no intermediate rounding,
    // which is exactly the rounding error the plain multiplication above made.
    *pProduct = product;
    *pError = std::fma( a, b, -product );
}

void ExpansionGrow(
    const f64 *pE,
    usize cE,
    f64 b,
    f64 *pOutput ) noexcept
{
    const bool_t bValidInput = pE != nullptr || cE == 0u;
    const bool_t bValidOutput = pOutput != nullptr;
    CY_ASSERT_MSG( bValidInput, "ExpansionGrow requires source storage when cE > 0." );
    CY_ASSERT_MSG( bValidOutput, "ExpansionGrow requires output storage." );
    if ( !bValidInput || !bValidOutput ) {
        return;
    }

    f64 q = b;
    for ( usize i = 0u; i < cE; ++i ) {
        f64 sum = 0.0;
        f64 error = 0.0;
        TwoSum( q, pE[i], &sum, &error );
        pOutput[i] = error;
        q = sum;
    }
    pOutput[cE] = q;
}

void ExpansionScale(
    const f64 *pE,
    usize cE,
    f64 scalar,
    f64 *pOutput ) noexcept
{
    const bool_t bValidInput = ( pE != nullptr && cE > 0u );
    const bool_t bValidOutput = pOutput != nullptr;
    CY_ASSERT_MSG( bValidInput, "ExpansionScale requires a nonempty source expansion." );
    CY_ASSERT_MSG( bValidOutput, "ExpansionScale requires output storage." );
    if ( !bValidInput || !bValidOutput ) {
        return;
    }

    f64 product = 0.0;
    f64 error = 0.0;
    TwoProduct( pE[0], scalar, &product, &error );
    pOutput[0] = error;
    f64 q = product;

    usize outputIndex = 1u;
    for ( usize i = 1u; i < cE; ++i ) {
        f64 termProduct = 0.0;
        f64 termError = 0.0;
        TwoProduct( pE[i], scalar, &termProduct, &termError );

        f64 sum1 = 0.0;
        f64 subError1 = 0.0;
        TwoSum( q, termError, &sum1, &subError1 );
        pOutput[outputIndex++] = subError1;

        f64 sum2 = 0.0;
        f64 subError2 = 0.0;
        TwoSum( sum1, termProduct, &sum2, &subError2 );
        pOutput[outputIndex++] = subError2;
        q = sum2;
    }
    pOutput[outputIndex] = q;
}

i32 ExpansionSign( const f64 *pE, usize cE ) noexcept
{
    if ( pE == nullptr ) {
        return 0;
    }
    // Expansions are stored least-significant first, so the most significant
    // nonzero term -- scanning from the end -- determines the sign of the
    // exact total: the nonoverlapping property guarantees it dwarfs the sum
    // of every less-significant term still unexamined.
    for ( usize i = cE; i > 0u; --i ) {
        const f64 term = pE[i - 1u];
        if ( term > 0.0 ) {
            return 1;
        }
        if ( term < 0.0 ) {
            return -1;
        }
    }
    return 0;
}

} // namespace cypher::math
