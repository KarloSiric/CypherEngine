//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashMap.h
//  Purpose: Declares the unordered key-value map facade.
//  Details: HashMap is the public map spelling for hash_table_t and preserves all
//           allocator, load-factor, and pointer-invalidation rules of that backend.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Hash Map Contract

HashMap is the named key/value facade over hash_table_t. Iteration order and slot placement are
implementation details and must never be serialized. Keys that require persisted identity need a
separate StableHash value; the table hash exists only to distribute entries in memory.
================
*/

#ifndef CYPHER_COMMON_TIER1_HASHMAP_H
#define CYPHER_COMMON_TIER1_HASHMAP_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_HashTable.h"

namespace cypher::common
{

template <
    typename key_t,
    typename value_t,
    typename hasher_t = hash_functor_t<key_t>,
    typename equal_key_t = equal_t<key_t>>
using hash_map_t = hash_table_t<key_t, value_t, hasher_t, equal_key_t>;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashMap_Init(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    const allocator_t *pAllocator,
    usize nInitialCapacity = 0u,
    hasher_t hasher = {},
    equal_key_t equalKey = {} ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
void HashMap_Shutdown(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
void HashMap_Clear(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashMap_IsValid(
    const hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashMap_Reserve(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    usize nElementCapacity ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD hash_table_insert_result_t<value_t> HashMap_Insert(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    const key_t &key,
    const value_t &value ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD value_t *HashMap_Find(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD const value_t *HashMap_Find(
    const hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashMap_Erase(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashMap_Contains(
    const hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD usize HashMap_Count(
    const hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD usize HashMap_Capacity(
    const hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashMap_IsEmpty(
    const hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_HASHMAP_INL
    #include "CypherCommon_HashMap.inl"
#endif

#endif // CYPHER_COMMON_TIER1_HASHMAP_H
