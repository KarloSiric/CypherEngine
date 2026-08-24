//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HeapSort.h
//  Purpose: Declares deterministic in-place heap sorting and heap primitives.
//  Details: HeapSort guarantees O(n log n) comparisons and constant extra storage,
//           making it useful when introsort fallback or strict memory bounds matter.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Heap Sort Contract

Algorithms operate only on the supplied range and callback contracts. Comparators must define a
consistent ordering; the implementation performs no hidden allocation unless explicitly
documented.
================
*/

#ifndef CYPHER_COMMON_TIER1_HEAPSORT_H
#define CYPHER_COMMON_TIER1_HEAPSORT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Sort.h"

namespace cypher::common
{

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t HeapSort_Raw(
    void *pData,
    usize nCount,
    usize cbElement,
    sort_compare_fn_t pCompare,
    void *pUserData ) noexcept;

template <typename type_t, typename compare_t = less_t<type_t>>
void Heap_Make( span_t<type_t> values, compare_t compare = {} ) noexcept;

template <typename type_t, typename compare_t = less_t<type_t>>
void Heap_Push( span_t<type_t> values, compare_t compare = {} ) noexcept;

template <typename type_t, typename compare_t = less_t<type_t>>
void Heap_Pop( span_t<type_t> values, compare_t compare = {} ) noexcept;

template <typename type_t, typename compare_t = less_t<type_t>>
void HeapSort_Sort( span_t<type_t> values, compare_t compare = {} ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_HEAPSORT_INL
    #include "CypherCommon_HeapSort.inl"
#endif

#endif // CYPHER_COMMON_TIER1_HEAPSORT_H
