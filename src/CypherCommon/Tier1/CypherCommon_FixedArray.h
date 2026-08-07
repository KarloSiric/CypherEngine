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
    type_t data[nExtent > 0u ? nExtent : 1u]{};
};

template <typename type_t, usize nExtent>
CYPHER_NODISCARD constexpr usize FixedArray_Count(
    const fixed_array_t<type_t, nExtent> & ) noexcept
{
    return nExtent;
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
CYPHER_NODISCARD span_t<type_t> FixedArray_Span(
    fixed_array_t<type_t, nExtent> *pArray ) noexcept;

template <typename type_t, usize nExtent>
void FixedArray_Fill(
    fixed_array_t<type_t, nExtent> *pArray,
    const type_t &value ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FIXEDARRAY_H
