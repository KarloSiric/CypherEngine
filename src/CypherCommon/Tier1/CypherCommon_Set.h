//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Set.h
//  Purpose: Declares deterministic ordered unique-key sets.
//  Details: Set uses the red-black tree backend. Use hash_set_t when ordering is
//           unnecessary and average constant-time lookup is preferred.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SET_H
#define CYPHER_COMMON_TIER1_SET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_RBTree.h"

namespace cypher::common
{

struct set_unit_t {
};

template <typename key_t, typename compare_t = less_t<key_t>>
using set_t = rb_tree_t<key_t, set_unit_t, compare_t>;

template <typename key_t, typename compare_t>
CYPHER_NODISCARD bool_t Set_Init(
    set_t<key_t, compare_t> *pSet,
    const allocator_t *pAllocator,
    compare_t compare = {} ) noexcept;

template <typename key_t, typename compare_t>
void Set_Shutdown( set_t<key_t, compare_t> *pSet ) noexcept;

template <typename key_t, typename compare_t>
void Set_Clear( set_t<key_t, compare_t> *pSet ) noexcept;

template <typename key_t, typename compare_t>
CYPHER_NODISCARD bool_t Set_Insert(
    set_t<key_t, compare_t> *pSet,
    const key_t &key ) noexcept;

template <typename key_t, typename compare_t>
CYPHER_NODISCARD bool_t Set_Contains(
    const set_t<key_t, compare_t> *pSet,
    const key_t &key ) noexcept;

template <typename key_t, typename compare_t>
CYPHER_NODISCARD bool_t Set_Erase(
    set_t<key_t, compare_t> *pSet,
    const key_t &key ) noexcept;

template <typename key_t, typename compare_t>
CYPHER_NODISCARD usize Set_Count(
    const set_t<key_t, compare_t> *pSet ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SET_H
