//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashTable.h
//  Purpose: Declares the canonical open-addressing associative table.
//  Details: HashTable owns contiguous slots, uses explicit hash/equality policies,
//           and invalidates slot pointers whenever insertion triggers rehashing.
//           Keys and values are constructed only in occupied slots.
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

/*
================
Hash Table Layout

Open addressing keeps every key/value pair inside one slot array. EMPTY terminates a probe chain;
there are no tombstones because erase closes the cluster by reinserting following entries. Slot
storage is raw until state becomes OCCUPIED, and both objects must be destroyed before it becomes
EMPTY again. Rehashing invalidates every key and value pointer returned by the table.
================
*/

enum class hash_slot_state_t : u8 {
    EMPTY = 0u, // Raw storage; probing may stop at this slot.
    OCCUPIED    // keyStorage and valueStorage contain live objects.
};

template <typename key_t, typename value_t>
struct hash_table_slot_t {
    static_assert( is_object_v<key_t>, "HashTable keys must be object types." );
    static_assert( is_object_v<value_t>, "HashTable values must be object types." );

    hash_table_slot_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( hash_table_slot_t );

    hash64_t hash{ 0u };                                     // Cached full hash used during probing.
    hash_slot_state_t state{ hash_slot_state_t::EMPTY };     // Controls both stored object lifetimes.
    alignas( key_t ) byte keyStorage[sizeof( key_t )];       // Raw in-place key storage.
    alignas( value_t ) byte valueStorage[sizeof( value_t )]; // Raw in-place mapped-value storage.
};

template <
    typename key_t,
    typename value_t,
    typename hasher_t = hash_functor_t<key_t>,
    typename equal_key_t = equal_t<key_t>>
struct hash_table_t {
    hash_table_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( hash_table_t );
    ~hash_table_t() noexcept;

    hash_table_slot_t<key_t, value_t> *pSlots{ nullptr }; // Power-of-two slot array.
    usize nCount{ 0u };                                   // Number of occupied slots.
    usize nCapacity{ 0u };                                // Total probe slots, including empty slots.
    const allocator_t *pAllocator{ nullptr };             // Allocator fixed for the table lifetime.
    hasher_t hasher{};                                    // Produces the full hash for a key.
    equal_key_t equalKey{};                               // Resolves collisions between equal hashes.
};

template <typename value_t>
struct hash_table_insert_result_t {
    value_t *pValue{ nullptr };   // Existing or newly constructed mapped value.
    bool_t bInserted{ CY_FALSE }; // False when the key was already present.
};

// Returns the live key stored in an occupied slot.
template <typename key_t, typename value_t>
CYPHER_NODISCARD key_t *HashTable_SlotKey(
    hash_table_slot_t<key_t, value_t> *pSlot ) noexcept;

template <typename key_t, typename value_t>
CYPHER_NODISCARD const key_t *HashTable_SlotKey(
    const hash_table_slot_t<key_t, value_t> *pSlot ) noexcept;

// Returns the live value stored in an occupied slot.
template <typename key_t, typename value_t>
CYPHER_NODISCARD value_t *HashTable_SlotValue(
    hash_table_slot_t<key_t, value_t> *pSlot ) noexcept;

template <typename key_t, typename value_t>
CYPHER_NODISCARD const value_t *HashTable_SlotValue(
    const hash_table_slot_t<key_t, value_t> *pSlot ) noexcept;

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
CYPHER_NODISCARD bool_t HashTable_IsValid(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept;

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
CYPHER_NODISCARD bool_t HashTable_Contains(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
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

#ifndef CYPHER_COMMON_TIER1_HASHTABLE_INL
    #include "CypherCommon_HashTable.inl"
#endif

#endif // CYPHER_COMMON_TIER1_HASHTABLE_H
