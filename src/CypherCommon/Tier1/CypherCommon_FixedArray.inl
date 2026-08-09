//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_FixedArray.inl
//  Purpose: Implements compile-time fixed-extent array access.
//  Details: Operations preserve constness, represent zero extent without exposing
//           dummy storage, and use the canonical Span contract for borrowed ranges.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_FIXEDARRAY_INL
#define CYPHER_COMMON_TIER1_FIXEDARRAY_INL

#ifndef CYPHER_COMMON_TIER1_FIXEDARRAY_H
    #include "CypherCommon_FixedArray.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <typename type_t, usize nExtent>
type_t *FixedArray_Data(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept
{
    const bool_t bValidArray = pArray != nullptr;
    CY_ASSERT_MSG( bValidArray, "FixedArray_Data requires an array object." );
    return bValidArray && nExtent > 0u ? pArray->data : nullptr;
}

template <typename type_t, usize nExtent>
const type_t *FixedArray_Data(
    const fixed_array_t<type_t, nExtent> *pArray ) noexcept
{
    const bool_t bValidArray = pArray != nullptr;
    CY_ASSERT_MSG( bValidArray, "FixedArray_Data requires an array object." );
    return bValidArray && nExtent > 0u ? pArray->data : nullptr;
}

template <typename type_t, usize nExtent>
type_t *FixedArray_At(
    fixed_array_t<type_t, nExtent> *pArray,
    usize iIndex ) noexcept
{
    const bool_t bValidArray = pArray != nullptr;
    const bool_t bIndexInRange = iIndex < nExtent;
    CY_ASSERT_MSG( bValidArray, "FixedArray_At requires an array object." );
    CY_ASSERT_MSG( bIndexInRange, "FixedArray_At index is outside the array." );
    return bValidArray && bIndexInRange ? pArray->data + iIndex : nullptr;
}

template <typename type_t, usize nExtent>
const type_t *FixedArray_At(
    const fixed_array_t<type_t, nExtent> *pArray,
    usize iIndex ) noexcept
{
    const bool_t bValidArray = pArray != nullptr;
    const bool_t bIndexInRange = iIndex < nExtent;
    CY_ASSERT_MSG( bValidArray, "FixedArray_At requires an array object." );
    CY_ASSERT_MSG( bIndexInRange, "FixedArray_At index is outside the array." );
    return bValidArray && bIndexInRange ? pArray->data + iIndex : nullptr;
}

template <typename type_t, usize nExtent>
type_t *FixedArray_Begin(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept
{
    return FixedArray_Data( pArray );
}

template <typename type_t, usize nExtent>
const type_t *FixedArray_Begin(
    const fixed_array_t<type_t, nExtent> *pArray ) noexcept
{
    return FixedArray_Data( pArray );
}

template <typename type_t, usize nExtent>
type_t *FixedArray_End(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept
{
    type_t *pData = FixedArray_Data( pArray );
    return pData != nullptr ? pData + nExtent : nullptr;
}

template <typename type_t, usize nExtent>
const type_t *FixedArray_End(
    const fixed_array_t<type_t, nExtent> *pArray ) noexcept
{
    const type_t *pData = FixedArray_Data( pArray );
    return pData != nullptr ? pData + nExtent : nullptr;
}

template <typename type_t, usize nExtent>
type_t *FixedArray_Front(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept
{
    return FixedArray_At( pArray, 0u );
}

template <typename type_t, usize nExtent>
const type_t *FixedArray_Front(
    const fixed_array_t<type_t, nExtent> *pArray ) noexcept
{
    return FixedArray_At( pArray, 0u );
}

template <typename type_t, usize nExtent>
type_t *FixedArray_Back(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept
{
    return nExtent > 0u ? FixedArray_At( pArray, nExtent - 1u ) : nullptr;
}

template <typename type_t, usize nExtent>
const type_t *FixedArray_Back(
    const fixed_array_t<type_t, nExtent> *pArray ) noexcept
{
    return nExtent > 0u ? FixedArray_At( pArray, nExtent - 1u ) : nullptr;
}

template <typename type_t, usize nExtent>
span_t<type_t> FixedArray_Span(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept
{
    type_t *pData = FixedArray_Data( pArray );
    return pData != nullptr || nExtent == 0u
        ? span_t<type_t>{ pData, nExtent }
        : span_t<type_t>{};
}

template <typename type_t, usize nExtent>
span_t<const type_t> FixedArray_Span(
    const fixed_array_t<type_t, nExtent> *pArray ) noexcept
{
    const type_t *pData = FixedArray_Data( pArray );
    return pData != nullptr || nExtent == 0u
        ? span_t<const type_t>{ pData, nExtent }
        : span_t<const type_t>{};
}

template <typename type_t, usize nExtent>
void FixedArray_Fill(
    fixed_array_t<type_t, nExtent> *pArray,
    const type_t &value ) noexcept
{
    static_assert(
        std::is_nothrow_copy_assignable_v<type_t>,
        "FixedArray_Fill requires nothrow copy assignment." );

    const bool_t bValidArray = pArray != nullptr;
    CY_ASSERT_MSG( bValidArray, "FixedArray_Fill requires an array object." );
    if ( !bValidArray ) {
        return;
    }

    for ( usize iIndex = 0u; iIndex < nExtent; ++iIndex ) {
        pArray->data[iIndex] = value;
    }
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FIXEDARRAY_INL
