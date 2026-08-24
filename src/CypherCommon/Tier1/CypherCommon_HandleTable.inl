//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HandleTable.inl
//  Purpose: Implements typed storage addressed by generational handles.
//  Details: Slots own object lifetimes explicitly. Transactional growth keeps every
//           live handle stable, while removal advances generation before slot reuse.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_HANDLETABLE_INL
#define CYPHER_COMMON_TIER1_HANDLETABLE_INL

#ifndef CYPHER_COMMON_TIER1_HANDLETABLE_H
    #include "CypherCommon_HandleTable.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ContainerOps.inl"

#include <new>
#include <type_traits>

namespace cypher::common
{

namespace detail
{

template <typename type_t>
type_t *HandleTable_SlotValue( handle_table_slot_t<type_t> &slot ) noexcept
{
    return std::launder( reinterpret_cast<type_t *>( slot.storage ) );
}

template <typename type_t>
const type_t *HandleTable_SlotValue(
    const handle_table_slot_t<type_t> &slot ) noexcept
{
    return std::launder( reinterpret_cast<const type_t *>( slot.storage ) );
}

template <typename type_t>
bool_t HandleTable_IsCanonicalEmpty(
    const handle_table_t<type_t> &table ) noexcept
{
    return table.pSlots == nullptr &&
           table.nCount == 0u &&
           table.nCapacity == 0u &&
           table.iFreeHead == CY_U32_MAX &&
           table.pAllocator == nullptr;
}

inline u32 HandleTable_NextGeneration( u32 nGeneration ) noexcept
{
    // Generation zero is reserved by the packed-handle contract as invalid.
    return nGeneration >= CY_HANDLE32_GENERATION_MAX
        ? 1u
        : nGeneration + 1u;
}

template <typename type_t>
void HandleTable_RebuildFreeList( handle_table_t<type_t> &table ) noexcept
{
    table.iFreeHead = CY_U32_MAX;
    // Walk backward and push each vacant index so allocation still begins at
    // the lowest available slot after growth or clear.
    for ( usize iSlot = table.nCapacity; iSlot > 0u; --iSlot ) {
        const u32 iCurrent = static_cast<u32>( iSlot - 1u );
        handle_table_slot_t<type_t> &slot = table.pSlots[iCurrent];
        if ( slot.bOccupied ) {
            slot.iNextFree = CY_U32_MAX;
            continue;
        }
        slot.iNextFree = table.iFreeHead;
        table.iFreeHead = iCurrent;
    }
}

template <typename type_t>
void HandleTable_DestroyValues( handle_table_t<type_t> &table ) noexcept
{
    static_assert(
        std::is_nothrow_destructible_v<type_t>,
        "HandleTable value destruction must not throw." );

    for ( usize iSlot = 0u; iSlot < table.nCapacity; ++iSlot ) {
        handle_table_slot_t<type_t> &slot = table.pSlots[iSlot];
        if ( slot.bOccupied ) {
            HandleTable_SlotValue( slot )->~type_t();
        }
    }
}

template <typename type_t>
void HandleTable_DestroySlotStorage(
    handle_table_t<type_t> &table ) noexcept
{
    for ( usize iSlot = 0u; iSlot < table.nCapacity; ++iSlot ) {
        table.pSlots[iSlot].~handle_table_slot_t<type_t>();
    }
    Allocator_FreeArrayStorage(
        table.pAllocator,
        table.pSlots,
        table.nCapacity );
}

template <typename type_t>
bool_t HandleTable_CalculateGrowth(
    usize nCurrentCapacity,
    usize &nCapacityOut ) noexcept
{
    if ( nCurrentCapacity >= CY_HANDLE_TABLE_MAX_CAPACITY ) {
        return CY_FALSE;
    }

    constexpr usize nMinimumCapacity = 8u;
    usize nCandidate = nCurrentCapacity < nMinimumCapacity
        ? nMinimumCapacity
        : nCurrentCapacity + nCurrentCapacity / 2u;
    if ( nCandidate <= nCurrentCapacity ||
         nCandidate > CY_HANDLE_TABLE_MAX_CAPACITY ) {
        nCandidate = CY_HANDLE_TABLE_MAX_CAPACITY;
    }
    nCapacityOut = nCandidate;
    return nCandidate > nCurrentCapacity;
}

template <typename type_t>
bool_t HandleTable_EnsureFreeSlot(
    handle_table_t<type_t> *pTable ) noexcept
{
    if ( pTable->iFreeHead != CY_U32_MAX ) {
        return CY_TRUE;
    }

    usize nGrowthCapacity = 0u;
    return HandleTable_CalculateGrowth<type_t>(
               pTable->nCapacity,
               nGrowthCapacity ) &&
           HandleTable_Reserve( pTable, nGrowthCapacity );
}

template <typename type_t>
bool_t HandleTable_DecodeLiveHandle(
    const handle_table_t<type_t> &table,
    handle32_t handle,
    u32 &iSlotOut ) noexcept
{
    if ( !Cy_Handle32IsValid( handle ) ) {
        return CY_FALSE;
    }

    const u32 iSlot = handle.value & CY_HANDLE32_INDEX_MAX; // Low packed index bits.
    const u32 nGeneration = handle.value >> CY_HANDLE32_INDEX_BITS;
    if ( static_cast<usize>( iSlot ) >= table.nCapacity ) {
        return CY_FALSE;
    }

    // Matching the generation rejects stale handles after a slot is recycled.
    const handle_table_slot_t<type_t> &slot = table.pSlots[iSlot];
    if ( !slot.bOccupied || slot.nGeneration != nGeneration ) {
        return CY_FALSE;
    }

    iSlotOut = iSlot;
    return CY_TRUE;
}

} // namespace detail

template <typename type_t>
handle_table_t<type_t>::~handle_table_t() noexcept
{
    HandleTable_Shutdown( this );
}

template <typename type_t>
bool_t HandleTable_Init(
    handle_table_t<type_t> *pTable,
    const allocator_t *pAllocator,
    usize nInitialCapacity ) noexcept
{
    const bool_t bValidDestination =
        pTable != nullptr && detail::HandleTable_IsCanonicalEmpty( *pTable );
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    const bool_t bValidCapacity =
        nInitialCapacity <= CY_HANDLE_TABLE_MAX_CAPACITY;
    CY_ASSERT_MSG(
        bValidDestination,
        "HandleTable_Init requires a canonical empty destination." );
    CY_ASSERT_MSG(
        bValidAllocator,
        "HandleTable_Init requires a valid allocator." );
    CY_ASSERT_MSG(
        bValidCapacity,
        "HandleTable_Init capacity exceeds the 16-bit handle index range." );
    if ( !bValidDestination || !bValidAllocator || !bValidCapacity ) {
        return CY_FALSE;
    }

    pTable->pAllocator = pAllocator;
    if ( nInitialCapacity == 0u ) {
        return CY_TRUE;
    }
    if ( !HandleTable_Reserve( pTable, nInitialCapacity ) ) {
        pTable->pAllocator = nullptr;
        return CY_FALSE;
    }
    return CY_TRUE;
}

template <typename type_t>
void HandleTable_Shutdown( handle_table_t<type_t> *pTable ) noexcept
{
    const bool_t bValidTable = HandleTable_IsValid( pTable );
    CY_ASSERT_MSG( bValidTable, "HandleTable_Shutdown requires a valid table." );
    if ( !bValidTable ) {
        return;
    }

    if ( pTable->pSlots != nullptr ) {
        detail::HandleTable_DestroyValues( *pTable );
        detail::HandleTable_DestroySlotStorage( *pTable );
    }
    pTable->pSlots = nullptr;
    pTable->nCount = 0u;
    pTable->nCapacity = 0u;
    pTable->iFreeHead = CY_U32_MAX;
    pTable->pAllocator = nullptr;
}

template <typename type_t>
void HandleTable_Clear( handle_table_t<type_t> *pTable ) noexcept
{
    const bool_t bValidTable = HandleTable_IsValid( pTable );
    CY_ASSERT_MSG( bValidTable, "HandleTable_Clear requires a valid table." );
    if ( !bValidTable || pTable->pSlots == nullptr ) {
        return;
    }

    for ( usize iSlot = 0u; iSlot < pTable->nCapacity; ++iSlot ) {
        handle_table_slot_t<type_t> &slot = pTable->pSlots[iSlot];
        if ( slot.bOccupied ) {
            detail::HandleTable_SlotValue( slot )->~type_t();
            slot.bOccupied = CY_FALSE;
            slot.nGeneration =
                detail::HandleTable_NextGeneration( slot.nGeneration );
        }
    }
    pTable->nCount = 0u;
    detail::HandleTable_RebuildFreeList( *pTable );
}

template <typename type_t>
bool_t HandleTable_IsValid(
    const handle_table_t<type_t> *pTable ) noexcept
{
    if ( pTable == nullptr ) {
        return CY_FALSE;
    }
    if ( pTable->pSlots == nullptr ) {
        return pTable->nCount == 0u &&
               pTable->nCapacity == 0u &&
               pTable->iFreeHead == CY_U32_MAX &&
               ( pTable->pAllocator == nullptr ||
                 Allocator_IsValid( pTable->pAllocator ) );
    }

    return pTable->nCapacity > 0u &&
           pTable->nCapacity <= CY_HANDLE_TABLE_MAX_CAPACITY &&
           pTable->nCount <= pTable->nCapacity &&
           ( pTable->iFreeHead == CY_U32_MAX ||
             static_cast<usize>( pTable->iFreeHead ) < pTable->nCapacity ) &&
           Allocator_IsValid( pTable->pAllocator );
}

template <typename type_t>
bool_t HandleTable_Reserve(
    handle_table_t<type_t> *pTable,
    usize nCapacity ) noexcept
{
    static_assert(
        std::is_nothrow_move_constructible_v<type_t> ||
        std::is_nothrow_copy_constructible_v<type_t>,
        "HandleTable growth requires nothrow move or copy construction." );

    const bool_t bValidTable = HandleTable_IsValid( pTable );
    const bool_t bAllocatorBound =
        bValidTable && Allocator_IsValid( pTable->pAllocator );
    const bool_t bValidCapacity = nCapacity <= CY_HANDLE_TABLE_MAX_CAPACITY;
    CY_ASSERT_MSG( bValidTable, "HandleTable_Reserve requires a valid table." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "HandleTable_Reserve requires an initialized allocator binding." );
    CY_ASSERT_MSG(
        bValidCapacity,
        "HandleTable capacity exceeds the 16-bit handle index range." );
    if ( !bValidTable || !bAllocatorBound || !bValidCapacity ) {
        return CY_FALSE;
    }
    if ( nCapacity <= pTable->nCapacity ) {
        return CY_TRUE;
    }

    using slot_t = handle_table_slot_t<type_t>;
    slot_t *pNewSlots = Allocator_AllocateArrayStorage<slot_t>(
        pTable->pAllocator,
        nCapacity );
    if ( pNewSlots == nullptr ) {
        return CY_FALSE;
    }

    // Construct every slot wrapper first; individual value lifetimes begin only
    // for occupied slots copied below.
    for ( usize iSlot = 0u; iSlot < nCapacity; ++iSlot ) {
        ::new ( static_cast<void *>( pNewSlots + iSlot ) ) slot_t;
    }

    for ( usize iSlot = 0u; iSlot < pTable->nCapacity; ++iSlot ) {
        slot_t &source = pTable->pSlots[iSlot];
        slot_t &destination = pNewSlots[iSlot];
        destination.nGeneration = source.nGeneration;
        destination.bOccupied = source.bOccupied;
        if ( !source.bOccupied ) {
            continue;
        }

        if constexpr ( std::is_nothrow_move_constructible_v<type_t> ) {
            ::new ( static_cast<void *>( destination.storage ) )
                type_t( static_cast<type_t &&>(
                    *detail::HandleTable_SlotValue( source ) ) );
        } else {
            ::new ( static_cast<void *>( destination.storage ) )
                type_t( *detail::HandleTable_SlotValue( source ) );
        }
    }

    // The new array is complete before the old array is touched, preserving the
    // table when allocation fails and preserving every packed slot index.
    if ( pTable->pSlots != nullptr ) {
        detail::HandleTable_DestroyValues( *pTable );
        detail::HandleTable_DestroySlotStorage( *pTable );
    }
    pTable->pSlots = pNewSlots;
    pTable->nCapacity = nCapacity;
    detail::HandleTable_RebuildFreeList( *pTable );
    return CY_TRUE;
}

template <typename type_t, typename... args_t>
handle32_t HandleTable_Emplace(
    handle_table_t<type_t> *pTable,
    args_t &&... args ) noexcept
{
    static_assert(
        std::is_nothrow_constructible_v<type_t, args_t...>,
        "HandleTable value construction must not throw." );

    const bool_t bValidTable = HandleTable_IsValid( pTable );
    const bool_t bAllocatorBound =
        bValidTable && Allocator_IsValid( pTable->pAllocator );
    CY_ASSERT_MSG( bValidTable, "HandleTable_Emplace requires a valid table." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "HandleTable_Emplace requires an initialized allocator binding." );
    if ( !bValidTable || !bAllocatorBound ||
         !detail::HandleTable_EnsureFreeSlot( pTable ) ) {
        return CY_HANDLE32_INVALID;
    }

    const u32 iSlot = pTable->iFreeHead; // Pop one slot from the intrusive free list.
    handle_table_slot_t<type_t> &slot = pTable->pSlots[iSlot];
    pTable->iFreeHead = slot.iNextFree;
    slot.iNextFree = CY_U32_MAX;
    ::new ( static_cast<void *>( slot.storage ) )
        type_t( static_cast<args_t &&>( args )... );
    slot.bOccupied = CY_TRUE;
    ++pTable->nCount;

    const handle32_t handle = Cy_Handle32Make( iSlot, slot.nGeneration );
    CY_ASSERT_MSG(
        Cy_Handle32IsValid( handle ),
        "HandleTable generated an invalid live handle." );
    return handle;
}

template <typename type_t>
handle32_t HandleTable_Insert(
    handle_table_t<type_t> *pTable,
    const type_t &value ) noexcept
{
    return HandleTable_Emplace( pTable, value );
}

template <typename type_t>
handle32_t HandleTable_InsertMove(
    handle_table_t<type_t> *pTable,
    type_t &&value ) noexcept
{
    return HandleTable_Emplace(
        pTable,
        static_cast<type_t &&>( value ) );
}

template <typename type_t>
type_t *HandleTable_Get(
    handle_table_t<type_t> *pTable,
    handle32_t handle ) noexcept
{
    const bool_t bValidTable = HandleTable_IsValid( pTable );
    CY_ASSERT_MSG( bValidTable, "HandleTable_Get requires a valid table." );
    if ( !bValidTable ) {
        return nullptr;
    }

    u32 iSlot = 0u;
    return detail::HandleTable_DecodeLiveHandle( *pTable, handle, iSlot )
        ? detail::HandleTable_SlotValue( pTable->pSlots[iSlot] )
        : nullptr;
}

template <typename type_t>
const type_t *HandleTable_Get(
    const handle_table_t<type_t> *pTable,
    handle32_t handle ) noexcept
{
    const bool_t bValidTable = HandleTable_IsValid( pTable );
    CY_ASSERT_MSG( bValidTable, "HandleTable_Get requires a valid table." );
    if ( !bValidTable ) {
        return nullptr;
    }

    u32 iSlot = 0u;
    return detail::HandleTable_DecodeLiveHandle( *pTable, handle, iSlot )
        ? detail::HandleTable_SlotValue( pTable->pSlots[iSlot] )
        : nullptr;
}

template <typename type_t>
bool_t HandleTable_Contains(
    const handle_table_t<type_t> *pTable,
    handle32_t handle ) noexcept
{
    return HandleTable_Get( pTable, handle ) != nullptr;
}

template <typename type_t>
bool_t HandleTable_Remove(
    handle_table_t<type_t> *pTable,
    handle32_t handle ) noexcept
{
    const bool_t bValidTable = HandleTable_IsValid( pTable );
    CY_ASSERT_MSG( bValidTable, "HandleTable_Remove requires a valid table." );
    if ( !bValidTable ) {
        return CY_FALSE;
    }

    u32 iSlot = 0u;
    if ( !detail::HandleTable_DecodeLiveHandle( *pTable, handle, iSlot ) ) {
        return CY_FALSE;
    }

    handle_table_slot_t<type_t> &slot = pTable->pSlots[iSlot];
    detail::HandleTable_SlotValue( slot )->~type_t();
    slot.bOccupied = CY_FALSE;
    // Advance before linking the slot back into the free list. Any outstanding
    // handle to the removed object becomes invalid immediately.
    slot.nGeneration =
        detail::HandleTable_NextGeneration( slot.nGeneration );
    slot.iNextFree = pTable->iFreeHead;
    pTable->iFreeHead = iSlot;
    --pTable->nCount;
    return CY_TRUE;
}

template <typename type_t, typename visitor_t>
usize HandleTable_ForEach(
    handle_table_t<type_t> *pTable,
    visitor_t &&visitor ) noexcept
{
    static_assert(
        std::is_nothrow_invocable_r_v<
            bool_t,
            visitor_t &,
            handle32_t,
            type_t &>,
        "HandleTable visitor must return bool_t and be noexcept." );

    const bool_t bValidTable = HandleTable_IsValid( pTable );
    CY_ASSERT_MSG( bValidTable, "HandleTable_ForEach requires a valid table." );
    if ( !bValidTable ) {
        return 0u;
    }

    usize nVisited = 0u;
    for ( usize iSlot = 0u; iSlot < pTable->nCapacity; ++iSlot ) {
        handle_table_slot_t<type_t> &slot = pTable->pSlots[iSlot];
        if ( !slot.bOccupied ) {
            continue;
        }

        const handle32_t handle = Cy_Handle32Make(
            static_cast<u32>( iSlot ),
            slot.nGeneration );
        ++nVisited;
        if ( !visitor( handle, *detail::HandleTable_SlotValue( slot ) ) ) {
            break;
        }
    }
    return nVisited;
}

template <typename type_t, typename visitor_t>
usize HandleTable_ForEach(
    const handle_table_t<type_t> *pTable,
    visitor_t &&visitor ) noexcept
{
    static_assert(
        std::is_nothrow_invocable_r_v<
            bool_t,
            visitor_t &,
            handle32_t,
            const type_t &>,
        "Const HandleTable visitor must return bool_t and be noexcept." );

    const bool_t bValidTable = HandleTable_IsValid( pTable );
    CY_ASSERT_MSG( bValidTable, "HandleTable_ForEach requires a valid table." );
    if ( !bValidTable ) {
        return 0u;
    }

    usize nVisited = 0u;
    for ( usize iSlot = 0u; iSlot < pTable->nCapacity; ++iSlot ) {
        const handle_table_slot_t<type_t> &slot = pTable->pSlots[iSlot];
        if ( !slot.bOccupied ) {
            continue;
        }

        const handle32_t handle = Cy_Handle32Make(
            static_cast<u32>( iSlot ),
            slot.nGeneration );
        ++nVisited;
        if ( !visitor( handle, *detail::HandleTable_SlotValue( slot ) ) ) {
            break;
        }
    }
    return nVisited;
}

template <typename type_t>
usize HandleTable_Count( const handle_table_t<type_t> *pTable ) noexcept
{
    const bool_t bValidTable = HandleTable_IsValid( pTable );
    CY_ASSERT_MSG( bValidTable, "HandleTable_Count requires a valid table." );
    return bValidTable ? pTable->nCount : 0u;
}

template <typename type_t>
usize HandleTable_Capacity( const handle_table_t<type_t> *pTable ) noexcept
{
    const bool_t bValidTable = HandleTable_IsValid( pTable );
    CY_ASSERT_MSG( bValidTable, "HandleTable_Capacity requires a valid table." );
    return bValidTable ? pTable->nCapacity : 0u;
}

template <typename type_t>
bool_t HandleTable_IsEmpty( const handle_table_t<type_t> *pTable ) noexcept
{
    return HandleTable_Count( pTable ) == 0u;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HANDLETABLE_INL
