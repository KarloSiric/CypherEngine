//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashTable.inl
//  Purpose: Implements the canonical allocator-backed hash table.
//  Details: Robin Hood probing bounds lookup variance, backward-shift deletion
//           avoids tombstones, and explicit slot lifetime supports non-default-
//           constructible engine values without hidden allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_HASHTABLE_INL
#define CYPHER_COMMON_TIER1_HASHTABLE_INL

#ifndef CYPHER_COMMON_TIER1_HASHTABLE_H
    #include "CypherCommon_HashTable.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ContainerOps.inl"

#include <new>
#include <type_traits>
#include <utility>

namespace cypher::common
{

namespace detail
{

inline constexpr usize CY_HASH_TABLE_MIN_CAPACITY = 8u; // First power-of-two table size.

template <typename key_t, typename value_t>
CYPHER_NODISCARD key_t *HashTable_SlotKeyUnchecked(
    hash_table_slot_t<key_t, value_t> *pSlot ) noexcept
{
    return std::launder( reinterpret_cast<key_t *>( pSlot->keyStorage ) );
}

template <typename key_t, typename value_t>
CYPHER_NODISCARD const key_t *HashTable_SlotKeyUnchecked(
    const hash_table_slot_t<key_t, value_t> *pSlot ) noexcept
{
    return std::launder(
        reinterpret_cast<const key_t *>( pSlot->keyStorage ) );
}

template <typename key_t, typename value_t>
CYPHER_NODISCARD value_t *HashTable_SlotValueUnchecked(
    hash_table_slot_t<key_t, value_t> *pSlot ) noexcept
{
    return std::launder(
        reinterpret_cast<value_t *>( pSlot->valueStorage ) );
}

template <typename key_t, typename value_t>
CYPHER_NODISCARD const value_t *HashTable_SlotValueUnchecked(
    const hash_table_slot_t<key_t, value_t> *pSlot ) noexcept
{
    return std::launder(
        reinterpret_cast<const value_t *>( pSlot->valueStorage ) );
}

template <typename key_t, typename value_t, typename key_arg_t, typename value_arg_t>
void HashTable_ConstructSlot(
    hash_table_slot_t<key_t, value_t> *pSlot,
    hash64_t hash,
    key_arg_t &&key,
    value_arg_t &&value ) noexcept
{
    static_assert(
        std::is_nothrow_constructible_v<key_t, key_arg_t &&>,
        "HashTable key construction must not throw." );
    static_assert(
        std::is_nothrow_constructible_v<value_t, value_arg_t &&>,
        "HashTable value construction must not throw." );

    ::new ( static_cast<void *>( pSlot->keyStorage ) )
        key_t( static_cast<key_arg_t &&>( key ) );
    ::new ( static_cast<void *>( pSlot->valueStorage ) )
        value_t( static_cast<value_arg_t &&>( value ) );
    pSlot->hash = hash;
    pSlot->state = hash_slot_state_t::OCCUPIED;
}

template <typename key_t, typename value_t>
void HashTable_DestroySlot(
    hash_table_slot_t<key_t, value_t> *pSlot ) noexcept
{
    static_assert(
        std::is_nothrow_destructible_v<key_t>,
        "HashTable key destruction must not throw." );
    static_assert(
        std::is_nothrow_destructible_v<value_t>,
        "HashTable value destruction must not throw." );

    if ( pSlot->state == hash_slot_state_t::OCCUPIED ) {
        HashTable_SlotValueUnchecked( pSlot )->~value_t();
        HashTable_SlotKeyUnchecked( pSlot )->~key_t();
    }
    pSlot->hash = 0u;
    pSlot->state = hash_slot_state_t::EMPTY;
}

template <typename key_t, typename value_t>
void HashTable_MoveSlot(
    hash_table_slot_t<key_t, value_t> *pDestination,
    hash_table_slot_t<key_t, value_t> *pSource ) noexcept
{
    static_assert(
        std::is_nothrow_move_constructible_v<key_t>,
        "HashTable keys must be nothrow move constructible." );
    static_assert(
        std::is_nothrow_move_constructible_v<value_t>,
        "HashTable values must be nothrow move constructible." );

    HashTable_ConstructSlot(
        pDestination,
        pSource->hash,
        static_cast<key_t &&>( *HashTable_SlotKeyUnchecked( pSource ) ),
        static_cast<value_t &&>( *HashTable_SlotValueUnchecked( pSource ) ) );
    HashTable_DestroySlot( pSource );
}

CYPHER_NODISCARD constexpr usize HashTable_ProbeDistance(
    usize iSlot,
    hash64_t hash,
    usize nCapacity ) noexcept
{
    // Power-of-two capacity permits both home placement and wrap with a mask.
    const usize iHome = static_cast<usize>( hash ) & ( nCapacity - 1u );
    return ( iSlot - iHome ) & ( nCapacity - 1u );
}

CYPHER_NODISCARD constexpr usize HashTable_MaxElementCount(
    usize nCapacity ) noexcept
{
    // Keep at least 20 percent of slots empty to bound Robin Hood probe chains.
    return ( nCapacity / 5u ) * 4u +
           ( ( nCapacity % 5u ) * 4u ) / 5u;
}

template <typename key_t, typename value_t>
CYPHER_NODISCARD bool_t HashTable_CapacityForElements(
    usize nElementCount,
    usize &nCapacityOut ) noexcept
{
    nCapacityOut = 0u;
    if ( nElementCount == 0u ) {
        return CY_TRUE;
    }

    const usize nLoadHeadroom =
        nElementCount / 4u + ( nElementCount % 4u != 0u ? 1u : 0u );
    if ( nElementCount > CY_USIZE_MAX - nLoadHeadroom ) {
        return CY_FALSE;
    }

    usize nRequired = nElementCount + nLoadHeadroom;
    if ( nRequired < CY_HASH_TABLE_MIN_CAPACITY ) {
        nRequired = CY_HASH_TABLE_MIN_CAPACITY;
    }

    u64 nPowerOfTwo = 0u;
    if ( !Cy_NextPowerOfTwo64Checked(
             static_cast<u64>( nRequired ),
             nPowerOfTwo ) ||
         nPowerOfTwo > static_cast<u64>( CY_USIZE_MAX ) ||
         nPowerOfTwo >
             static_cast<u64>( CY_USIZE_MAX / sizeof( hash_table_slot_t<key_t, value_t> ) ) ) {
        return CY_FALSE;
    }

    nCapacityOut = static_cast<usize>( nPowerOfTwo );
    return CY_TRUE;
}

template <typename key_t, typename value_t>
CYPHER_NODISCARD hash_table_slot_t<key_t, value_t> *HashTable_AllocateSlots(
    const allocator_t *pAllocator,
    usize nCapacity ) noexcept
{
    using slot_t = hash_table_slot_t<key_t, value_t>;
    slot_t *pSlots = Allocator_AllocateArrayStorage<slot_t>(
        pAllocator,
        nCapacity );
    if ( pSlots == nullptr ) {
        return nullptr;
    }

    Container_DefaultConstructRange( pSlots, nCapacity );
    return pSlots;
}

template <typename key_t, typename value_t>
void HashTable_FreeSlots(
    const allocator_t *pAllocator,
    hash_table_slot_t<key_t, value_t> *pSlots,
    usize nCapacity ) noexcept
{
    if ( pSlots == nullptr ) {
        return;
    }

    for ( usize iSlot = 0u; iSlot < nCapacity; ++iSlot ) {
        HashTable_DestroySlot( pSlots + iSlot );
    }
    Container_DestroyRange( pSlots, nCapacity );
    Allocator_FreeArrayStorage( pAllocator, pSlots, nCapacity );
}

template <typename key_t, typename value_t>
CYPHER_NODISCARD value_t *HashTable_InsertPrepared(
    hash_table_slot_t<key_t, value_t> *pSlots,
    usize nCapacity,
    hash64_t hash,
    key_t &&key,
    value_t &&value ) noexcept
{
    static_assert(
        std::is_nothrow_move_constructible_v<key_t> &&
        std::is_nothrow_swappable_v<key_t>,
        "HashTable keys must support nothrow movement and swapping." );
    static_assert(
        std::is_nothrow_move_constructible_v<value_t> &&
        std::is_nothrow_swappable_v<value_t>,
        "HashTable values must support nothrow movement and swapping." );

    // The pending pair walks forward until it finds an empty slot. Robin Hood
    // swaps let the entry farther from home take the earlier position.
    key_t pendingKey( static_cast<key_t &&>( key ) );
    value_t pendingValue( static_cast<value_t &&>( value ) );
    hash64_t pendingHash = hash;
    value_t *pInsertedValue = nullptr;
    usize nPendingDistance = 0u;
    usize iSlot = static_cast<usize>( pendingHash ) & ( nCapacity - 1u );

    for ( usize nVisited = 0u; nVisited < nCapacity; ++nVisited ) {
        hash_table_slot_t<key_t, value_t> *pSlot = pSlots + iSlot;
        if ( pSlot->state == hash_slot_state_t::EMPTY ) {
            HashTable_ConstructSlot(
                pSlot,
                pendingHash,
                static_cast<key_t &&>( pendingKey ),
                static_cast<value_t &&>( pendingValue ) );
            return pInsertedValue != nullptr
                ? pInsertedValue
                : HashTable_SlotValueUnchecked( pSlot );
        }

        const usize nResidentDistance =
            HashTable_ProbeDistance( iSlot, pSlot->hash, nCapacity );
        if ( nResidentDistance < nPendingDistance ) {
            using std::swap;
            swap( pendingHash, pSlot->hash );
            swap( pendingKey, *HashTable_SlotKeyUnchecked( pSlot ) );
            swap( pendingValue, *HashTable_SlotValueUnchecked( pSlot ) );
            if ( pInsertedValue == nullptr ) {
                pInsertedValue = HashTable_SlotValueUnchecked( pSlot );
            }
            nPendingDistance = nResidentDistance;
        }

        iSlot = ( iSlot + 1u ) & ( nCapacity - 1u );
        ++nPendingDistance;
    }

    CY_ASSERT_MSG( CY_FALSE, "HashTable insertion exhausted its slot array." );
    return nullptr;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD usize HashTable_FindSlotIndex(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> &table,
    const key_t &key,
    hash64_t hash ) noexcept
{
    if ( table.nCapacity == 0u ) {
        return CY_USIZE_MAX;
    }

    usize nProbeDistance = 0u;
    usize iSlot = static_cast<usize>( hash ) & ( table.nCapacity - 1u );
    for ( usize nVisited = 0u; nVisited < table.nCapacity; ++nVisited ) {
        const hash_table_slot_t<key_t, value_t> *pSlot = table.pSlots + iSlot;
        if ( pSlot->state == hash_slot_state_t::EMPTY ) {
            return CY_USIZE_MAX;
        }

        const usize nResidentDistance =
            HashTable_ProbeDistance( iSlot, pSlot->hash, table.nCapacity );
        // Probe distances cannot decrease past the key's possible position in
        // a Robin Hood cluster; this permits an early unsuccessful lookup.
        if ( nResidentDistance < nProbeDistance ) {
            return CY_USIZE_MAX;
        }

        if ( pSlot->hash == hash &&
             table.equalKey( *HashTable_SlotKeyUnchecked( pSlot ), key ) ) {
            return iSlot;
        }

        iSlot = ( iSlot + 1u ) & ( table.nCapacity - 1u );
        ++nProbeDistance;
    }
    return CY_USIZE_MAX;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashTable_Rehash(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    usize nNewCapacity ) noexcept
{
    using slot_t = hash_table_slot_t<key_t, value_t>;
    slot_t *pNewSlots = HashTable_AllocateSlots<key_t, value_t>(
        pTable->pAllocator,
        nNewCapacity );
    if ( pNewSlots == nullptr ) {
        return CY_FALSE;
    }

    // Reinsert because every element's home slot depends on the capacity mask.
    for ( usize iSlot = 0u; iSlot < pTable->nCapacity; ++iSlot ) {
        slot_t *pOldSlot = pTable->pSlots + iSlot;
        if ( pOldSlot->state != hash_slot_state_t::OCCUPIED ) {
            continue;
        }

        value_t *pInserted = HashTable_InsertPrepared(
            pNewSlots,
            nNewCapacity,
            pOldSlot->hash,
            static_cast<key_t &&>( *HashTable_SlotKeyUnchecked( pOldSlot ) ),
            static_cast<value_t &&>( *HashTable_SlotValueUnchecked( pOldSlot ) ) );
        CY_ASSERT_MSG( pInserted != nullptr, "HashTable rehash insertion failed." );
        if ( pInserted == nullptr ) {
            HashTable_FreeSlots( pTable->pAllocator, pNewSlots, nNewCapacity );
            return CY_FALSE;
        }
        HashTable_DestroySlot( pOldSlot );
    }

    HashTable_FreeSlots(
        pTable->pAllocator,
        pTable->pSlots,
        pTable->nCapacity );
    pTable->pSlots = pNewSlots;
    pTable->nCapacity = nNewCapacity;
    return CY_TRUE;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
CYPHER_NODISCARD bool_t HashTable_EnsureInsertCapacity(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept
{
    if ( pTable->nCount == CY_USIZE_MAX ) {
        return CY_FALSE;
    }
    if ( pTable->nCapacity > 0u &&
         pTable->nCount + 1u <= HashTable_MaxElementCount( pTable->nCapacity ) ) {
        return CY_TRUE;
    }

    return HashTable_Reserve( pTable, pTable->nCount + 1u );
}

} // namespace detail

template <typename key_t, typename value_t>
key_t *HashTable_SlotKey(
    hash_table_slot_t<key_t, value_t> *pSlot ) noexcept
{
    const bool_t bOccupied =
        pSlot != nullptr && pSlot->state == hash_slot_state_t::OCCUPIED;
    CY_ASSERT_MSG( bOccupied, "HashTable_SlotKey requires an occupied slot." );
    return bOccupied ? detail::HashTable_SlotKeyUnchecked( pSlot ) : nullptr;
}

template <typename key_t, typename value_t>
const key_t *HashTable_SlotKey(
    const hash_table_slot_t<key_t, value_t> *pSlot ) noexcept
{
    const bool_t bOccupied =
        pSlot != nullptr && pSlot->state == hash_slot_state_t::OCCUPIED;
    CY_ASSERT_MSG( bOccupied, "HashTable_SlotKey requires an occupied slot." );
    return bOccupied ? detail::HashTable_SlotKeyUnchecked( pSlot ) : nullptr;
}

template <typename key_t, typename value_t>
value_t *HashTable_SlotValue(
    hash_table_slot_t<key_t, value_t> *pSlot ) noexcept
{
    const bool_t bOccupied =
        pSlot != nullptr && pSlot->state == hash_slot_state_t::OCCUPIED;
    CY_ASSERT_MSG( bOccupied, "HashTable_SlotValue requires an occupied slot." );
    return bOccupied ? detail::HashTable_SlotValueUnchecked( pSlot ) : nullptr;
}

template <typename key_t, typename value_t>
const value_t *HashTable_SlotValue(
    const hash_table_slot_t<key_t, value_t> *pSlot ) noexcept
{
    const bool_t bOccupied =
        pSlot != nullptr && pSlot->state == hash_slot_state_t::OCCUPIED;
    CY_ASSERT_MSG( bOccupied, "HashTable_SlotValue requires an occupied slot." );
    return bOccupied ? detail::HashTable_SlotValueUnchecked( pSlot ) : nullptr;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
hash_table_t<key_t, value_t, hasher_t, equal_key_t>::~hash_table_t() noexcept
{
    HashTable_Shutdown( this );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
bool_t HashTable_Init(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    const allocator_t *pAllocator,
    usize nInitialCapacity,
    hasher_t hasher,
    equal_key_t equalKey ) noexcept
{
    const bool_t bCanonicalDestination =
        pTable != nullptr &&
        pTable->pSlots == nullptr &&
        pTable->nCount == 0u &&
        pTable->nCapacity == 0u &&
        pTable->pAllocator == nullptr;
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG(
        bCanonicalDestination,
        "HashTable_Init requires a canonical empty destination." );
    CY_ASSERT_MSG(
        bValidAllocator,
        "HashTable_Init requires a valid allocator." );
    if ( !bCanonicalDestination || !bValidAllocator ) {
        return CY_FALSE;
    }

    pTable->pAllocator = pAllocator;
    pTable->hasher = static_cast<hasher_t &&>( hasher );
    pTable->equalKey = static_cast<equal_key_t &&>( equalKey );
    if ( nInitialCapacity == 0u ) {
        return CY_TRUE;
    }
    if ( !HashTable_Reserve( pTable, nInitialCapacity ) ) {
        pTable->pAllocator = nullptr;
        return CY_FALSE;
    }
    return CY_TRUE;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
void HashTable_Shutdown(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept
{
    CY_ASSERT_MSG(
        HashTable_IsValid( pTable ),
        "HashTable_Shutdown requires a valid table." );
    if ( pTable == nullptr ) {
        return;
    }

    detail::HashTable_FreeSlots(
        pTable->pAllocator,
        pTable->pSlots,
        pTable->nCapacity );
    pTable->pSlots = nullptr;
    pTable->nCount = 0u;
    pTable->nCapacity = 0u;
    pTable->pAllocator = nullptr;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
void HashTable_Clear(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept
{
    CY_ASSERT_MSG(
        HashTable_IsValid( pTable ),
        "HashTable_Clear requires a valid table." );
    if ( pTable == nullptr ) {
        return;
    }

    for ( usize iSlot = 0u; iSlot < pTable->nCapacity; ++iSlot ) {
        detail::HashTable_DestroySlot( pTable->pSlots + iSlot );
    }
    pTable->nCount = 0u;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
bool_t HashTable_IsValid(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept
{
    if ( pTable == nullptr ) {
        return CY_FALSE;
    }
    if ( pTable->pSlots == nullptr ) {
        return pTable->nCount == 0u &&
               pTable->nCapacity == 0u &&
               ( pTable->pAllocator == nullptr ||
                 Allocator_IsValid( pTable->pAllocator ) );
    }

    return Allocator_IsValid( pTable->pAllocator ) &&
           pTable->nCapacity >= detail::CY_HASH_TABLE_MIN_CAPACITY &&
           Cy_AlignIsPowerOfTwo( pTable->nCapacity ) &&
           pTable->nCount <=
               detail::HashTable_MaxElementCount( pTable->nCapacity ) &&
           Cy_AlignIsPointerAligned(
               pTable->pSlots,
               alignof( hash_table_slot_t<key_t, value_t> ) );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
bool_t HashTable_Reserve(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    usize nElementCapacity ) noexcept
{
    CY_ASSERT_MSG(
        HashTable_IsValid( pTable ),
        "HashTable_Reserve requires a valid table." );
    const bool_t bAllocatorBound =
        pTable != nullptr && Allocator_IsValid( pTable->pAllocator );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "HashTable_Reserve requires an initialized allocator binding." );
    if ( !bAllocatorBound ) {
        return CY_FALSE;
    }
    if ( nElementCapacity <=
         detail::HashTable_MaxElementCount( pTable->nCapacity ) ) {
        return CY_TRUE;
    }

    usize nNewCapacity = 0u;
    const bool_t bCapacityFits =
        detail::HashTable_CapacityForElements<key_t, value_t>(
            nElementCapacity,
            nNewCapacity );
    CY_ASSERT_MSG( bCapacityFits, "HashTable capacity calculation overflowed." );
    if ( !bCapacityFits ) {
        return CY_FALSE;
    }
    return detail::HashTable_Rehash( pTable, nNewCapacity );
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
hash_table_insert_result_t<value_t> HashTable_Insert(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    const key_t &key,
    const value_t &value ) noexcept
{
    static_assert(
        std::is_nothrow_copy_constructible_v<key_t>,
        "HashTable_Insert requires nothrow-copyable keys." );
    static_assert(
        std::is_nothrow_copy_constructible_v<value_t>,
        "HashTable_Insert requires nothrow-copyable values." );

    CY_ASSERT_MSG(
        HashTable_IsValid( pTable ),
        "HashTable_Insert requires a valid table." );
    const bool_t bAllocatorBound =
        pTable != nullptr && Allocator_IsValid( pTable->pAllocator );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "HashTable_Insert requires an initialized allocator binding." );
    if ( !bAllocatorBound ) {
        return {};
    }

    const hash64_t hash = pTable->hasher( key );
    const usize iExisting = detail::HashTable_FindSlotIndex(
        *pTable,
        key,
        hash );
    if ( iExisting != CY_USIZE_MAX ) {
        return {
            detail::HashTable_SlotValueUnchecked( pTable->pSlots + iExisting ),
            CY_FALSE
        };
    }

    // Materialize arguments before reserve because either reference may alias
    // an object stored in the table that rehashing will move.
    key_t keyCopy( key );
    value_t valueCopy( value );
    if ( !detail::HashTable_EnsureInsertCapacity( pTable ) ) {
        return {};
    }

    value_t *pInserted = detail::HashTable_InsertPrepared(
        pTable->pSlots,
        pTable->nCapacity,
        hash,
        static_cast<key_t &&>( keyCopy ),
        static_cast<value_t &&>( valueCopy ) );
    if ( pInserted == nullptr ) {
        return {};
    }

    ++pTable->nCount;
    return { pInserted, CY_TRUE };
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
value_t *HashTable_Find(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    const key_t &key ) noexcept
{
    CY_ASSERT_MSG(
        HashTable_IsValid( pTable ),
        "HashTable_Find requires a valid table." );
    if ( pTable == nullptr ) {
        return nullptr;
    }

    const usize iSlot = detail::HashTable_FindSlotIndex(
        *pTable,
        key,
        pTable->hasher( key ) );
    return iSlot != CY_USIZE_MAX
        ? detail::HashTable_SlotValueUnchecked( pTable->pSlots + iSlot )
        : nullptr;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
const value_t *HashTable_Find(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    const key_t &key ) noexcept
{
    CY_ASSERT_MSG(
        HashTable_IsValid( pTable ),
        "HashTable_Find requires a valid table." );
    if ( pTable == nullptr ) {
        return nullptr;
    }

    const usize iSlot = detail::HashTable_FindSlotIndex(
        *pTable,
        key,
        pTable->hasher( key ) );
    return iSlot != CY_USIZE_MAX
        ? detail::HashTable_SlotValueUnchecked( pTable->pSlots + iSlot )
        : nullptr;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
bool_t HashTable_Erase(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    const key_t &key ) noexcept
{
    CY_ASSERT_MSG(
        HashTable_IsValid( pTable ),
        "HashTable_Erase requires a valid table." );
    if ( pTable == nullptr ) {
        return CY_FALSE;
    }

    usize iVacant = detail::HashTable_FindSlotIndex(
        *pTable,
        key,
        pTable->hasher( key ) );
    if ( iVacant == CY_USIZE_MAX ) {
        return CY_FALSE;
    }

    detail::HashTable_DestroySlot( pTable->pSlots + iVacant );
    // Pull the remainder of this cluster backward until an empty slot or an
    // entry already in its home slot is reached. This avoids tombstones.
    usize iNext = ( iVacant + 1u ) & ( pTable->nCapacity - 1u );
    while ( pTable->pSlots[iNext].state == hash_slot_state_t::OCCUPIED ) {
        const usize nDistance = detail::HashTable_ProbeDistance(
            iNext,
            pTable->pSlots[iNext].hash,
            pTable->nCapacity );
        if ( nDistance == 0u ) {
            break;
        }

        detail::HashTable_MoveSlot(
            pTable->pSlots + iVacant,
            pTable->pSlots + iNext );
        iVacant = iNext;
        iNext = ( iNext + 1u ) & ( pTable->nCapacity - 1u );
    }

    --pTable->nCount;
    return CY_TRUE;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
bool_t HashTable_Contains(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    const key_t &key ) noexcept
{
    return HashTable_Find( pTable, key ) != nullptr;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
hash_table_slot_t<key_t, value_t> *HashTable_NextOccupied(
    hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    usize *pSlotIndexInOut ) noexcept
{
    const bool_t bValidIndex = pSlotIndexInOut != nullptr;
    CY_ASSERT_MSG(
        HashTable_IsValid( pTable ),
        "HashTable iteration requires a valid table." );
    CY_ASSERT_MSG( bValidIndex, "HashTable iteration requires an index cursor." );
    if ( pTable == nullptr || !bValidIndex ) {
        return nullptr;
    }

    for ( usize iSlot = *pSlotIndexInOut; iSlot < pTable->nCapacity; ++iSlot ) {
        if ( pTable->pSlots[iSlot].state == hash_slot_state_t::OCCUPIED ) {
            *pSlotIndexInOut = iSlot + 1u;
            return pTable->pSlots + iSlot;
        }
    }

    *pSlotIndexInOut = pTable->nCapacity;
    return nullptr;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
const hash_table_slot_t<key_t, value_t> *HashTable_NextOccupied(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable,
    usize *pSlotIndexInOut ) noexcept
{
    const bool_t bValidIndex = pSlotIndexInOut != nullptr;
    CY_ASSERT_MSG(
        HashTable_IsValid( pTable ),
        "HashTable iteration requires a valid table." );
    CY_ASSERT_MSG( bValidIndex, "HashTable iteration requires an index cursor." );
    if ( pTable == nullptr || !bValidIndex ) {
        return nullptr;
    }

    for ( usize iSlot = *pSlotIndexInOut; iSlot < pTable->nCapacity; ++iSlot ) {
        if ( pTable->pSlots[iSlot].state == hash_slot_state_t::OCCUPIED ) {
            *pSlotIndexInOut = iSlot + 1u;
            return pTable->pSlots + iSlot;
        }
    }

    *pSlotIndexInOut = pTable->nCapacity;
    return nullptr;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
usize HashTable_Count(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept
{
    CY_ASSERT_MSG(
        HashTable_IsValid( pTable ),
        "HashTable_Count requires a valid table." );
    return pTable != nullptr ? pTable->nCount : 0u;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
usize HashTable_Capacity(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept
{
    CY_ASSERT_MSG(
        HashTable_IsValid( pTable ),
        "HashTable_Capacity requires a valid table." );
    return pTable != nullptr ? pTable->nCapacity : 0u;
}

template <typename key_t, typename value_t, typename hasher_t, typename equal_key_t>
bool_t HashTable_IsEmpty(
    const hash_table_t<key_t, value_t, hasher_t, equal_key_t> *pTable ) noexcept
{
    CY_ASSERT_MSG(
        HashTable_IsValid( pTable ),
        "HashTable_IsEmpty requires a valid table." );
    return pTable == nullptr || pTable->nCount == 0u;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HASHTABLE_INL
