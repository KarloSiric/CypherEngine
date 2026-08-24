//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Search.inl
//  Purpose: Implements generic linear, binary, and ordered-bound searches.
//  Details: Every algorithm operates on borrowed spans and caller-provided policies.
//           Ordered searches use half-open ranges and never allocate memory.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Search Template Definitions

Algorithms operate only on the supplied range and callback contracts. Comparators must define a
consistent ordering; the implementation performs no hidden allocation unless explicitly
documented. Template definitions remain in this file so each concrete instantiation is compiled
at its call site.
================
*/

#ifndef CYPHER_COMMON_TIER1_SEARCH_INL
#define CYPHER_COMMON_TIER1_SEARCH_INL

#ifndef CYPHER_COMMON_TIER1_SEARCH_H
    #include "CypherCommon_Search.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <typename type_t, typename equal_t>
usize Search_Linear(
    span_t<const type_t> values,
    const type_t &target,
    equal_t equal ) noexcept
{
    const bool_t bValidValues = Span_IsValid( values );
    CY_ASSERT_MSG( bValidValues, "Search_Linear requires a valid span." );
    if ( !bValidValues ) {
        return CY_INVALID_SIZE;
    }

    for ( usize iValue = 0u; iValue < values.nCount; ++iValue ) {
        if ( equal( values.pData[iValue], target ) ) {
            return iValue;
        }
    }
    return CY_INVALID_SIZE;
}

template <typename type_t, typename compare_t>
usize Search_LowerBound(
    span_t<const type_t> values,
    const type_t &target,
    compare_t compare ) noexcept
{
    const bool_t bValidValues = Span_IsValid( values );
    CY_ASSERT_MSG( bValidValues, "Search_LowerBound requires a valid span." );
    if ( !bValidValues ) {
        return CY_INVALID_SIZE;
    }

    // The candidate range is [iFirst, iFirst + nRemaining). Elements before
    // it are known to compare less than target; no pointer sentinel is needed.
    usize iFirst = 0u;
    usize nRemaining = values.nCount;
    while ( nRemaining > 0u ) {
        const usize nHalf = nRemaining / 2u;
        const usize iMiddle = iFirst + nHalf;
        if ( compare( values.pData[iMiddle], target ) ) {
            iFirst = iMiddle + 1u;
            nRemaining -= nHalf + 1u;
        } else {
            nRemaining = nHalf;
        }
    }
    return iFirst;
}

template <typename type_t, typename compare_t>
usize Search_UpperBound(
    span_t<const type_t> values,
    const type_t &target,
    compare_t compare ) noexcept
{
    const bool_t bValidValues = Span_IsValid( values );
    CY_ASSERT_MSG( bValidValues, "Search_UpperBound requires a valid span." );
    if ( !bValidValues ) {
        return CY_INVALID_SIZE;
    }

    // This is the same half-open search as LowerBound, but equality advances
    // the lower edge so the result is the first element strictly after target.
    usize iFirst = 0u;
    usize nRemaining = values.nCount;
    while ( nRemaining > 0u ) {
        const usize nHalf = nRemaining / 2u;
        const usize iMiddle = iFirst + nHalf;
        if ( !compare( target, values.pData[iMiddle] ) ) {
            iFirst = iMiddle + 1u;
            nRemaining -= nHalf + 1u;
        } else {
            nRemaining = nHalf;
        }
    }
    return iFirst;
}

template <typename type_t, typename compare_t>
usize Search_Binary(
    span_t<const type_t> values,
    const type_t &target,
    compare_t compare ) noexcept
{
    // LowerBound gives the only position at which an equivalent value can occur.
    const usize iCandidate = Search_LowerBound( values, target, compare );
    if ( iCandidate == CY_INVALID_SIZE || iCandidate >= values.nCount ) {
        return CY_INVALID_SIZE;
    }

    const type_t &candidate = values.pData[iCandidate];
    // Strict-order comparators express equality when neither direction is less.
    return !compare( candidate, target ) && !compare( target, candidate )
        ? iCandidate
        : CY_INVALID_SIZE;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SEARCH_INL
