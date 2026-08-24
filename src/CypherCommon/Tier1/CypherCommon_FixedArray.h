//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_FixedArray.h
//  Purpose: Declares compile-time fixed-extent arrays.
//  Details: Every element is part of the array for its full lifetime. This type does
//           not track a logical count and performs no allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Fixed Array Contract

The extent is part of the type and every logical element exists for the full lifetime of the
record. The one-element physical fallback keeps zero-extent specializations legal C++ storage;
FixedArray_Count still reports zero and no caller may address that fallback element.
================
*/

#ifndef CYPHER_COMMON_TIER1_FIXEDARRAY_H
#define CYPHER_COMMON_TIER1_FIXEDARRAY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Span.h"

namespace cypher::common
{

template <typename type_t, usize nExtent>
struct fixed_array_t {
    static_assert( is_object_v<type_t>, "fixed_array_t requires an object type." );
    static_assert( !is_array_v<type_t>, "fixed_array_t does not store array elements." );

    type_t data[nExtent > 0u ? nExtent : 1u]{}; // Inline elements; one unused slot when nExtent is zero.
};

template <typename type_t, usize nExtent>
CYPHER_NODISCARD constexpr usize FixedArray_Count(
    const fixed_array_t<type_t, nExtent> & ) noexcept
{
    return nExtent;
}

template <typename type_t, usize nExtent>
CYPHER_NODISCARD constexpr bool_t FixedArray_IsEmpty(
    const fixed_array_t<type_t, nExtent> & ) noexcept
{
    return nExtent == 0u;
}

template <typename type_t, usize nExtent>
CYPHER_NODISCARD type_t *FixedArray_Data(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD const type_t *FixedArray_Data(
    const fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD type_t *FixedArray_At(
    fixed_array_t<type_t, nExtent> *pArray,
    usize iIndex ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD const type_t *FixedArray_At(
    const fixed_array_t<type_t, nExtent> *pArray,
    usize iIndex ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD type_t *FixedArray_Begin(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD const type_t *FixedArray_Begin(
    const fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD type_t *FixedArray_End(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD const type_t *FixedArray_End(
    const fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD type_t *FixedArray_Front(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD const type_t *FixedArray_Front(
    const fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD type_t *FixedArray_Back(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD const type_t *FixedArray_Back(
    const fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD span_t<type_t> FixedArray_Span(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD span_t<const type_t> FixedArray_Span(
    const fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
void FixedArray_Fill(
    fixed_array_t<type_t, nExtent> *pArray,
    const type_t &value ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_FIXEDARRAY_INL
    #include "CypherCommon_FixedArray.inl"
#endif

#endif // CYPHER_COMMON_TIER1_FIXEDARRAY_H
