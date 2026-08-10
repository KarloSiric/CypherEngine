//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Sort.h
//  Purpose: Declares generic raw and typed sorting algorithms.
//  Details: Unstable sorting is in-place and allocation-free. Stable sorting requires
//           explicit scratch storage and reports when that storage is insufficient.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SORT_H
#define CYPHER_COMMON_TIER1_SORT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Functor.h"
#include "CypherCommon_Span.h"

namespace cypher::common
{

using sort_compare_fn_t = i32 ( * )(
    const void *pLeft,
    const void *pRight,
    void *pUserData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Sort_UnstableRaw(
    void *pData,
    usize nCount,
    usize cbElement,
    sort_compare_fn_t pCompare,
    void *pUserData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Sort_StableRaw(
    void *pData,
    usize nCount,
    usize cbElement,
    sort_compare_fn_t pCompare,
    void *pUserData,
    byte_span_t scratch ) noexcept;

template <typename type_t, typename compare_t = less_t<type_t>>
void Sort_Unstable(
    span_t<type_t> values,
    compare_t compare = {} ) noexcept;

template <typename type_t, typename compare_t = less_t<type_t>>
CYPHER_NODISCARD bool_t Sort_Stable(
    span_t<type_t> values,
    compare_t compare,
    byte_span_t scratch ) noexcept;

template <typename type_t, typename compare_t = less_t<type_t>>
CYPHER_NODISCARD bool_t Sort_IsOrdered(
    span_t<const type_t> values,
    compare_t compare = {} ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_SORT_INL
    #include "CypherCommon_Sort.inl"
#endif

#endif // CYPHER_COMMON_TIER1_SORT_H
