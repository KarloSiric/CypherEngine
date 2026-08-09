//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Pair.h
//  Purpose: Declares a lightweight heterogeneous pair value.
//  Details: The aggregate stores exactly two values and introduces no allocation,
//           ownership indirection, or hidden runtime behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_PAIR_H
#define CYPHER_COMMON_TIER1_PAIR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename first_t, typename second_t>
struct pair_t {
    first_t first{};
    second_t second{};
};

template <typename first_t, typename second_t>
CYPHER_NODISCARD constexpr pair_t<first_t, second_t> Pair_Make(
    const first_t &first,
    const second_t &second ) noexcept;

template <typename first_t, typename second_t>
CYPHER_NODISCARD constexpr bool_t Pair_Equals(
    const pair_t<first_t, second_t> &pairA,
    const pair_t<first_t, second_t> &pairB ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_PAIR_INL
    #include "CypherCommon_Pair.inl"
#endif

#endif // CYPHER_COMMON_TIER1_PAIR_H
