//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Search.h
//  Purpose: Declares linear and ordered range-search algorithms.
//  Details: Ordered searches require a range sorted under the same comparison policy.
//           Missing indices return CY_INVALID_SIZE.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SEARCH_H
#define CYPHER_COMMON_TIER1_SEARCH_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Functor.h"
#include "CypherCommon_Span.h"

namespace cypher::common
{

template <typename type_t, typename equal_t = cypher::common::equal_t<type_t>>
CYPHER_NODISCARD usize Search_Linear(
    span_t<const type_t> values,
    const type_t &target,
    equal_t equal = {} ) noexcept;

template <typename type_t, typename compare_t = less_t<type_t>>
CYPHER_NODISCARD usize Search_LowerBound(
    span_t<const type_t> values,
    const type_t &target,
    compare_t compare = {} ) noexcept;

template <typename type_t, typename compare_t = less_t<type_t>>
CYPHER_NODISCARD usize Search_UpperBound(
    span_t<const type_t> values,
    const type_t &target,
    compare_t compare = {} ) noexcept;

template <typename type_t, typename compare_t = less_t<type_t>>
CYPHER_NODISCARD usize Search_Binary(
    span_t<const type_t> values,
    const type_t &target,
    compare_t compare = {} ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SEARCH_H
