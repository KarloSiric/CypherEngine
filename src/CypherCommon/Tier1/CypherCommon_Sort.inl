//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Sort.inl
//  Purpose: Implements typed sorting helpers.
//  Details: Typed unstable sorting uses an in-place max heap. Stable typed sorting
//           delegates to raw merge sort and therefore requires trivially copyable data.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SORT_INL
#define CYPHER_COMMON_TIER1_SORT_INL

#ifndef CYPHER_COMMON_TIER1_SORT_H
    #include "CypherCommon_Sort.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <utility>

namespace cypher::common
{

namespace detail
{

template <typename type_t>
void Sort_Swap( type_t &left, type_t &right ) noexcept
{
    type_t temporary = std::move( left );
    left = std::move( right );
    right = std::move( temporary );
}

template <typename type_t, typename compare_t>
void Sort_SiftDown(
    type_t *pValues,
    usize iRoot,
    usize nCount,
    compare_t &compare ) noexcept
{
    for ( ;; ) {
        if ( iRoot >= nCount / 2u ) {
            return;
        }
        const usize iLeft = iRoot * 2u + 1u;
        usize iLargest = iLeft;
        const usize iRight = iLeft + 1u;
        if ( iRight < nCount && compare( pValues[iLargest], pValues[iRight] ) ) {
            iLargest = iRight;
        }
        if ( !compare( pValues[iRoot], pValues[iLargest] ) ) {
            return;
        }
        Sort_Swap( pValues[iRoot], pValues[iLargest] );
        iRoot = iLargest;
    }
}

template <typename type_t, typename compare_t>
i32 Sort_TypedCompare(
    const void *pLeft,
    const void *pRight,
    void *pUserData ) noexcept
{
    auto &compare = *static_cast<compare_t *>( pUserData );
    const auto &left = *static_cast<const type_t *>( pLeft );
    const auto &right = *static_cast<const type_t *>( pRight );
    if ( compare( left, right ) ) {
        return -1;
    }
    if ( compare( right, left ) ) {
        return 1;
    }
    return 0;
}

} // namespace detail

template <typename type_t, typename compare_t>
void Sort_Unstable(
    span_t<type_t> values,
    compare_t compare ) noexcept
{
    const bool_t bValid = Span_IsValid( values );
    CY_ASSERT_MSG( bValid, "Sort_Unstable requires a valid span." );
    if ( !bValid || values.nCount < 2u ) {
        return;
    }

    for ( usize iRoot = values.nCount / 2u; iRoot > 0u; --iRoot ) {
        detail::Sort_SiftDown(
            values.pData,
            iRoot - 1u,
            values.nCount,
            compare );
    }
    for ( usize nHeap = values.nCount; nHeap > 1u; --nHeap ) {
        detail::Sort_Swap( values.pData[0], values.pData[nHeap - 1u] );
        detail::Sort_SiftDown( values.pData, 0u, nHeap - 1u, compare );
    }
}

template <typename type_t, typename compare_t>
bool_t Sort_Stable(
    span_t<type_t> values,
    compare_t compare,
    byte_span_t scratch ) noexcept
{
    static_assert(
        is_trivially_copyable_v<type_t>,
        "Sort_Stable requires trivially copyable values for raw scratch relocation." );
    if ( !Span_IsValid( values ) || !Span_IsValid( scratch ) ) {
        return CY_FALSE;
    }
    return Sort_StableRaw(
        values.pData,
        values.nCount,
        sizeof( type_t ),
        &detail::Sort_TypedCompare<type_t, compare_t>,
        &compare,
        scratch );
}

template <typename type_t, typename compare_t>
bool_t Sort_IsOrdered(
    span_t<const type_t> values,
    compare_t compare ) noexcept
{
    if ( !Span_IsValid( values ) ) {
        return CY_FALSE;
    }
    for ( usize iValue = 1u; iValue < values.nCount; ++iValue ) {
        if ( compare( values.pData[iValue], values.pData[iValue - 1u] ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SORT_INL
