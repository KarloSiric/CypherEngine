//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Pair.inl
//  Purpose: Implements lightweight heterogeneous pair values.
//  Details: Pair operations remain constexpr, allocation-free, and transparent so
//           containers can use the type without introducing hidden runtime work.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_PAIR_INL
#define CYPHER_COMMON_TIER1_PAIR_INL

#ifndef CYPHER_COMMON_TIER1_PAIR_H
    #include "CypherCommon_Pair.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <typename first_t, typename second_t>
constexpr pair_t<first_t, second_t> Pair_Make(
    const first_t &first,
    const second_t &second ) noexcept
{
    return { first, second };
}

template <typename first_t, typename second_t>
constexpr bool_t Pair_Equals(
    const pair_t<first_t, second_t> &pairA,
    const pair_t<first_t, second_t> &pairB ) noexcept
{
    return pairA.first == pairB.first &&
           pairA.second == pairB.second;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_PAIR_INL
