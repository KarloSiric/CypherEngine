//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Map.h
//  Purpose: Declares the ordered key-value map facade.
//  Details: Map uses rb_tree_t and therefore provides deterministic key ordering,
//           logarithmic operations, and stable nodes.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_MAP_H
#define CYPHER_COMMON_TIER1_MAP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_RBTree.h"

namespace cypher::common
{

template <typename key_t, typename value_t, typename compare_t = less_t<key_t>>
using map_t = rb_tree_t<key_t, value_t, compare_t>;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD bool_t Map_Init(
    map_t<key_t, value_t, compare_t> *pMap,
    const allocator_t *pAllocator,
    compare_t compare = {} ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
void Map_Shutdown( map_t<key_t, value_t, compare_t> *pMap ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
void Map_Clear( map_t<key_t, value_t, compare_t> *pMap ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD rb_tree_insert_result_t<key_t, value_t> Map_Insert(
    map_t<key_t, value_t, compare_t> *pMap,
    const key_t &key,
    const value_t &value ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD value_t *Map_Find(
    map_t<key_t, value_t, compare_t> *pMap,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD const value_t *Map_Find(
    const map_t<key_t, value_t, compare_t> *pMap,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD bool_t Map_Erase(
    map_t<key_t, value_t, compare_t> *pMap,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD bool_t Map_Contains(
    const map_t<key_t, value_t, compare_t> *pMap,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename compare_t>
CYPHER_NODISCARD usize Map_Count(
    const map_t<key_t, value_t, compare_t> *pMap ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_MAP_H
