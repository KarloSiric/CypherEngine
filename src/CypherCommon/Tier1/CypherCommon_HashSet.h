//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashSet.h
//  Purpose: Declares unordered unique-key sets.
//  Details: HashSet uses the canonical open-addressing table and does not preserve
//           insertion or key order.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_HASHSET_H
#define CYPHER_COMMON_TIER1_HASHSET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_HashTable.h"
#include "CypherCommon_Set.h"

namespace cypher::common
{

template <
    typename key_t,
    typename hasher_t = hash_functor_t<key_t>,
    typename equal_key_t = equal_t<key_t>>
using hash_set_t = hash_table_t<key_t, set_unit_t, hasher_t, equal_key_t>;

template <typename key_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashSet_Init(
    hash_set_t<key_t, hasher_t, equal_key_t> *pSet,
    const allocator_t *pAllocator,
    usize nInitialCapacity = 0u,
    hasher_t hasher = {},
    equal_key_t equalKey = {} ) noexcept;

template <typename key_t, typename hasher_t, typename equal_key_t>
void HashSet_Shutdown(
    hash_set_t<key_t, hasher_t, equal_key_t> *pSet ) noexcept;

template <typename key_t, typename hasher_t, typename equal_key_t>
void HashSet_Clear(
    hash_set_t<key_t, hasher_t, equal_key_t> *pSet ) noexcept;

template <typename key_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashSet_Insert(
    hash_set_t<key_t, hasher_t, equal_key_t> *pSet,
    const key_t &key ) noexcept;

template <typename key_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashSet_Contains(
    const hash_set_t<key_t, hasher_t, equal_key_t> *pSet,
    const key_t &key ) noexcept;

template <typename key_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashSet_Erase(
    hash_set_t<key_t, hasher_t, equal_key_t> *pSet,
    const key_t &key ) noexcept;

template <typename key_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD usize HashSet_Count(
    const hash_set_t<key_t, hasher_t, equal_key_t> *pSet ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASHSET_H
