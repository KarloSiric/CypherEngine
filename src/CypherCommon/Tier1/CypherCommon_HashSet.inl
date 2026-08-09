//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashSet.inl
//  Purpose: Implements unordered unique-key sets.
//  Details: HashSet stores the shared zero-state set unit as its table value and
//           delegates ownership, probing, and collision handling to HashTable.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_HASHSET_INL
#define CYPHER_COMMON_TIER1_HASHSET_INL

#ifndef CYPHER_COMMON_TIER1_HASHSET_H
    #include "CypherCommon_HashSet.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <typename key_t, typename hasher_t, typename equal_key_t>
bool_t HashSet_Init(
    hash_set_t<key_t, hasher_t, equal_key_t> *pSet,
    const allocator_t *pAllocator,
    usize nInitialCapacity,
    hasher_t hasher,
    equal_key_t equalKey ) noexcept
{
    return HashTable_Init(
        pSet,
        pAllocator,
        nInitialCapacity,
        static_cast<hasher_t &&>( hasher ),
        static_cast<equal_key_t &&>( equalKey ) );
}

template <typename key_t, typename hasher_t, typename equal_key_t>
void HashSet_Shutdown(
    hash_set_t<key_t, hasher_t, equal_key_t> *pSet ) noexcept
{
    HashTable_Shutdown( pSet );
}

template <typename key_t, typename hasher_t, typename equal_key_t>
void HashSet_Clear(
    hash_set_t<key_t, hasher_t, equal_key_t> *pSet ) noexcept
{
    HashTable_Clear( pSet );
}

template <typename key_t, typename hasher_t, typename equal_key_t>
bool_t HashSet_IsValid(
    const hash_set_t<key_t, hasher_t, equal_key_t> *pSet ) noexcept
{
    return HashTable_IsValid( pSet );
}

template <typename key_t, typename hasher_t, typename equal_key_t>
bool_t HashSet_Reserve(
    hash_set_t<key_t, hasher_t, equal_key_t> *pSet,
    usize nElementCapacity ) noexcept
{
    return HashTable_Reserve( pSet, nElementCapacity );
}

template <typename key_t, typename hasher_t, typename equal_key_t>
bool_t HashSet_Insert(
    hash_set_t<key_t, hasher_t, equal_key_t> *pSet,
    const key_t &key ) noexcept
{
    const set_unit_t unit{};
    const hash_table_insert_result_t<set_unit_t> result =
        HashTable_Insert( pSet, key, unit );
    return result.bInserted;
}

template <typename key_t, typename hasher_t, typename equal_key_t>
bool_t HashSet_Contains(
    const hash_set_t<key_t, hasher_t, equal_key_t> *pSet,
    const key_t &key ) noexcept
{
    return HashTable_Contains( pSet, key );
}

template <typename key_t, typename hasher_t, typename equal_key_t>
bool_t HashSet_Erase(
    hash_set_t<key_t, hasher_t, equal_key_t> *pSet,
    const key_t &key ) noexcept
{
    return HashTable_Erase( pSet, key );
}

template <typename key_t, typename hasher_t, typename equal_key_t>
usize HashSet_Count(
    const hash_set_t<key_t, hasher_t, equal_key_t> *pSet ) noexcept
{
    return HashTable_Count( pSet );
}

template <typename key_t, typename hasher_t, typename equal_key_t>
usize HashSet_Capacity(
    const hash_set_t<key_t, hasher_t, equal_key_t> *pSet ) noexcept
{
    return HashTable_Capacity( pSet );
}

template <typename key_t, typename hasher_t, typename equal_key_t>
bool_t HashSet_IsEmpty(
    const hash_set_t<key_t, hasher_t, equal_key_t> *pSet ) noexcept
{
    return HashTable_IsEmpty( pSet );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASHSET_INL
