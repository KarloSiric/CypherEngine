//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashMap.inl
//  Purpose: Implements the unordered key-value map facade.
//  Details: The facade preserves one hash-table implementation while exposing
//           map-specific naming to engine and tool callers.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_HASHMAP_INL
#define CYPHER_COMMON_TIER1_HASHMAP_INL

#ifndef CYPHER_COMMON_TIER1_HASHMAP_H
    #include "CypherCommon_HashMap.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
bool_t HashMap_Init(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    const allocator_t *pAllocator,
    usize nInitialCapacity,
    hasher_t hasher,
    equal_key_t equalKey ) noexcept
{
    return HashTable_Init(
        pMap,
        pAllocator,
        nInitialCapacity,
        static_cast<hasher_t &&>( hasher ),
        static_cast<equal_key_t &&>( equalKey ) );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
void HashMap_Shutdown(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap ) noexcept
{
    HashTable_Shutdown( pMap );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
void HashMap_Clear(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap ) noexcept
{
    HashTable_Clear( pMap );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
bool_t HashMap_IsValid(
    const hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap ) noexcept
{
    return HashTable_IsValid( pMap );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
bool_t HashMap_Reserve(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    usize nElementCapacity ) noexcept
{
    return HashTable_Reserve( pMap, nElementCapacity );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
hash_table_insert_result_t<value_t> HashMap_Insert(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    const key_t &key,
    const value_t &value ) noexcept
{
    return HashTable_Insert( pMap, key, value );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
value_t *HashMap_Find(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    const key_t &key ) noexcept
{
    return HashTable_Find( pMap, key );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
const value_t *HashMap_Find(
    const hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    const key_t &key ) noexcept
{
    return HashTable_Find( pMap, key );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
bool_t HashMap_Erase(
    hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    const key_t &key ) noexcept
{
    return HashTable_Erase( pMap, key );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
bool_t HashMap_Contains(
    const hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap,
    const key_t &key ) noexcept
{
    return HashTable_Contains( pMap, key );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
usize HashMap_Count(
    const hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap ) noexcept
{
    return HashTable_Count( pMap );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
usize HashMap_Capacity(
    const hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap ) noexcept
{
    return HashTable_Capacity( pMap );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
bool_t HashMap_IsEmpty(
    const hash_map_t<key_t, value_t, hasher_t, equal_key_t> *pMap ) noexcept
{
    return HashTable_IsEmpty( pMap );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASHMAP_INL
