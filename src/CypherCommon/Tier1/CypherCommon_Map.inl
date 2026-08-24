//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Map.inl
//  Purpose: Implements the ordered key-value map facade.
//  Details: The facade delegates balancing and ownership to RBTree while exposing
//           value-oriented lookup for callers that do not need tree node metadata.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Map Template Definitions

Container mutations must preserve structural invariants and element lifetime. Iterators or
handles are invalidated only according to the rules stated by the public API. Template
definitions remain in this file so each concrete instantiation is compiled at its call site.
================
*/

#ifndef CYPHER_COMMON_TIER1_MAP_INL
#define CYPHER_COMMON_TIER1_MAP_INL

#ifndef CYPHER_COMMON_TIER1_MAP_H
    #include "CypherCommon_Map.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <typename key_t, typename value_t, typename compare_t>
bool_t Map_Init(
    map_t<key_t, value_t, compare_t> *pMap,
    const allocator_t *pAllocator,
    compare_t compare ) noexcept
{
    // Ordered-map behavior is exactly the RBTree comparator and node-lifetime contract.
    return RBTree_Init(
        pMap,
        pAllocator,
        static_cast<compare_t &&>( compare ) );
}

template <typename key_t, typename value_t, typename compare_t>
void Map_Shutdown( map_t<key_t, value_t, compare_t> *pMap ) noexcept
{
    RBTree_Shutdown( pMap );
}

template <typename key_t, typename value_t, typename compare_t>
void Map_Clear( map_t<key_t, value_t, compare_t> *pMap ) noexcept
{
    RBTree_Clear( pMap );
}

template <typename key_t, typename value_t, typename compare_t>
rb_tree_insert_result_t<key_t, value_t> Map_Insert(
    map_t<key_t, value_t, compare_t> *pMap,
    const key_t &key,
    const value_t &value ) noexcept
{
    return RBTree_Insert( pMap, key, value );
}

template <typename key_t, typename value_t, typename compare_t>
value_t *Map_Find(
    map_t<key_t, value_t, compare_t> *pMap,
    const key_t &key ) noexcept
{
    rb_tree_node_t<key_t, value_t> *pNode = RBTree_Find( pMap, key ); // Hide node metadata from map callers.
    return pNode != nullptr ? &pNode->value : nullptr;
}

template <typename key_t, typename value_t, typename compare_t>
const value_t *Map_Find(
    const map_t<key_t, value_t, compare_t> *pMap,
    const key_t &key ) noexcept
{
    const rb_tree_node_t<key_t, value_t> *pNode = RBTree_Find( pMap, key );
    return pNode != nullptr ? &pNode->value : nullptr;
}

template <typename key_t, typename value_t, typename compare_t>
bool_t Map_Erase(
    map_t<key_t, value_t, compare_t> *pMap,
    const key_t &key ) noexcept
{
    return RBTree_Erase( pMap, key );
}

template <typename key_t, typename value_t, typename compare_t>
bool_t Map_Contains(
    const map_t<key_t, value_t, compare_t> *pMap,
    const key_t &key ) noexcept
{
    return RBTree_Find( pMap, key ) != nullptr;
}

template <typename key_t, typename value_t, typename compare_t>
usize Map_Count(
    const map_t<key_t, value_t, compare_t> *pMap ) noexcept
{
    return RBTree_Count( pMap );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_MAP_INL
