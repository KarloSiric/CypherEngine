//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashTable.h
//  Purpose: Declares the canonical open-addressing associative table.
//  Details: HashTable owns contiguous slots, uses explicit hash/equality policies,
//           and invalidates slot pointers whenever insertion triggers rehashing.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_HASHTABLE_H
#define CYPHER_COMMON_TIER1_HASHTABLE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_Functor.h"

namespace cypher::common
{

enum class hash_slot_state_t : u8 {
    EMPTY = 0u,
    OCCUPIED,
    DELETED
};

template <typename key_t, typename value_t>
struct hash_table_slot_t {
    hash64_t hash{ 0u };
    hash_slot_state_t state{ hash_slot_state_t::EMPTY };
    key_t key{};
    value_t value{};
};

template <
    typename key_t,
    typename value_t,
    typename hasher_t = hash_functor_t<key_t>,
    typename equal_key_t = equal_t<key_t>>
struct hash_table_t {
    hash_table_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( hash_table_t );

    hash_table_slot_t<key_t, value_t> *pSlots{ nullptr };
    usize nCount{ 0u };
    usize nDeleted{ 0u };
    usize nCapacity{ 0u };
    const allocator_t *pAllocator{ nullptr };
    hasher_t hasher{};
    equal_key_t equalKey{};
    f32 flMaxLoadFactor{ 0.80f };
};

template <typename value_t>
struct hash_table_insert_result_t {
    value_t *pValue{ nullptr };
    bool_t bInserted{ CY_FALSE };
};

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashTable_Init(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    const allocator_t *pAllocator,
    usize nInitialCapacity = 0u,
    hasher_t hasher = {},
    equal_key_t equalKey = {} ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
void HashTable_Shutdown(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
void HashTable_Clear(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashTable_Reserve(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    usize nElementCapacity ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD hash_table_insert_result_t<value_t> HashTable_Insert(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    const key_t &key,
    const value_t &value ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD value_t *HashTable_Find(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD const value_t *HashTable_Find(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashTable_Erase(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    const key_t &key ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD hash_table_slot_t<key_t, value_t> *HashTable_NextOccupied(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    usize *pSlotIndexInOut ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD const hash_table_slot_t<key_t, value_t> *HashTable_NextOccupied(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    usize *pSlotIndexInOut ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD usize HashTable_Count(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD usize HashTable_Capacity(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept;

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashTable_IsEmpty(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASHTABLE_H
