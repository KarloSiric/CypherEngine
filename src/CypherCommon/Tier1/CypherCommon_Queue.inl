//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Queue.inl
//  Purpose: Implements allocator-backed first-in-first-out circular queues.
//  Details: Wrapped elements retain explicit lifetimes, growth linearizes logical
//           order transactionally, and failed allocation leaves the queue unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_QUEUE_INL
#define CYPHER_COMMON_TIER1_QUEUE_INL

#ifndef CYPHER_COMMON_TIER1_QUEUE_H
    #include "CypherCommon_Queue.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ContainerOps.inl"

#include <limits>
#include <new>
#include <type_traits>

namespace cypher::common
{

namespace detail
{

template <typename type_t>
bool_t Queue_IsCanonicalEmpty( const queue_t<type_t> &queue ) noexcept
{
    return queue.pData == nullptr &&
           queue.nCapacity == 0u &&
           queue.nCount == 0u &&
           queue.iHead == 0u &&
           queue.pAllocator == nullptr;
}

template <typename type_t>
void Queue_Reset( queue_t<type_t> &queue ) noexcept
{
    queue.pData = nullptr;
    queue.nCapacity = 0u;
    queue.nCount = 0u;
    queue.iHead = 0u;
    queue.pAllocator = nullptr;
}

inline usize Queue_PhysicalIndex(
    usize iHead,
    usize iLogicalIndex,
    usize nCapacity ) noexcept
{
    const usize nUntilWrap = nCapacity - iHead;
    return iLogicalIndex < nUntilWrap
        ? iHead + iLogicalIndex
        : iLogicalIndex - nUntilWrap;
}

template <typename type_t>
void Queue_DestroyLiveElements( queue_t<type_t> &queue ) noexcept
{
    if ( queue.nCount == 0u ) {
        return;
    }

    const usize nFirstCount =
        queue.nCount < queue.nCapacity - queue.iHead
            ? queue.nCount
            : queue.nCapacity - queue.iHead;
    Container_DestroyRange( queue.pData + queue.iHead, nFirstCount );
    Container_DestroyRange( queue.pData, queue.nCount - nFirstCount );
}

template <typename type_t>
bool_t Queue_ReallocateExact(
    queue_t<type_t> *pQueue,
    usize nCapacity ) noexcept
{
    static_assert(
        std::is_nothrow_move_constructible_v<type_t> ||
        std::is_nothrow_copy_constructible_v<type_t>,
        "Queue relocation requires nothrow move or copy construction." );

    const bool_t bValidCapacity =
        nCapacity >= pQueue->nCount && nCapacity > 0u;
    CY_ASSERT_MSG(
        bValidCapacity,
        "Queue reallocation capacity must contain all live elements." );
    if ( !bValidCapacity ) {
        return CY_FALSE;
    }

    type_t *pNewData = Allocator_AllocateArrayStorage<type_t>(
        pQueue->pAllocator,
        nCapacity );
    if ( pNewData == nullptr ) {
        return CY_FALSE;
    }

    if ( pQueue->nCount > 0u ) {
        const usize nFirstCount =
            pQueue->nCount < pQueue->nCapacity - pQueue->iHead
                ? pQueue->nCount
                : pQueue->nCapacity - pQueue->iHead;
        Container_RelocateConstructRange(
            pNewData,
            pQueue->pData + pQueue->iHead,
            nFirstCount );
        Container_RelocateConstructRange(
            pNewData + nFirstCount,
            pQueue->pData,
            pQueue->nCount - nFirstCount );
        Queue_DestroyLiveElements( *pQueue );
    }

    Allocator_FreeArrayStorage(
        pQueue->pAllocator,
        pQueue->pData,
        pQueue->nCapacity );
    pQueue->pData = pNewData;
    pQueue->nCapacity = nCapacity;
    pQueue->iHead = 0u;
    return CY_TRUE;
}

template <typename type_t>
bool_t Queue_CalculateGrowth(
    usize nCurrentCapacity,
    usize nRequiredCapacity,
    usize &nCapacityOut ) noexcept
{
    constexpr usize nMaximumCapacity = CY_USIZE_MAX / sizeof( type_t );
    if ( nRequiredCapacity > nMaximumCapacity ) {
        return CY_FALSE;
    }

    constexpr usize nMinimumCapacity = 8u;
    usize nCandidate = nCurrentCapacity < nMinimumCapacity
        ? nMinimumCapacity
        : nCurrentCapacity;
    if ( nCandidate < nRequiredCapacity ) {
        const usize nHalf = nCandidate / 2u;
        nCandidate = nCandidate > nMaximumCapacity - nHalf
            ? nMaximumCapacity
            : nCandidate + nHalf;
    }
    if ( nCandidate < nRequiredCapacity ) {
        nCandidate = nRequiredCapacity;
    }

    nCapacityOut = nCandidate;
    return nCandidate >= nRequiredCapacity;
}

template <typename type_t>
bool_t Queue_EnsureAdditional(
    queue_t<type_t> *pQueue,
    usize nAdditional ) noexcept
{
    if ( nAdditional > CY_USIZE_MAX - pQueue->nCount ) {
        CY_ASSERT_MSG( CY_FALSE, "Queue count overflowed." );
        return CY_FALSE;
    }
    const usize nRequired = pQueue->nCount + nAdditional;
    if ( nRequired <= pQueue->nCapacity ) {
        return CY_TRUE;
    }

    usize nGrowthCapacity = 0u;
    const bool_t bValidGrowth = Queue_CalculateGrowth<type_t>(
        pQueue->nCapacity,
        nRequired,
        nGrowthCapacity );
    CY_ASSERT_MSG( bValidGrowth, "Queue growth exceeds addressable storage." );
    return bValidGrowth && Queue_ReallocateExact( pQueue, nGrowthCapacity );
}

template <typename type_t>
bool_t Queue_PointerIsInternal(
    const queue_t<type_t> &queue,
    const type_t *pValue ) noexcept
{
    if ( pValue == nullptr || queue.pData == nullptr ) {
        return CY_FALSE;
    }

    usize cbStorage = 0u;
    if ( !Cy_TryArrayByteCount<type_t>( queue.nCapacity, cbStorage ) ) {
        return CY_TRUE;
    }
    constexpr uintptr nMaximumAddress =
        std::numeric_limits<uintptr>::max();
    const uintptr nBegin = reinterpret_cast<uintptr>( queue.pData );
    if ( nBegin > nMaximumAddress - cbStorage ) {
        return CY_TRUE;
    }
    const uintptr nValue = reinterpret_cast<uintptr>( pValue );
    return nValue >= nBegin && nValue < nBegin + cbStorage;
}

} // namespace detail

template <typename type_t>
queue_t<type_t>::~queue_t() noexcept
{
    Queue_Shutdown( this );
}

template <typename type_t>
bool_t Queue_Init(
    queue_t<type_t> *pQueue,
    const allocator_t *pAllocator,
    usize nInitialCapacity ) noexcept
{
    const bool_t bValidDestination =
        pQueue != nullptr && detail::Queue_IsCanonicalEmpty( *pQueue );
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG(
        bValidDestination,
        "Queue_Init requires a canonical empty destination." );
    CY_ASSERT_MSG(
        bValidAllocator,
        "Queue_Init requires a valid allocator." );
    if ( !bValidDestination || !bValidAllocator ) {
        return CY_FALSE;
    }

    if ( nInitialCapacity == 0u ) {
        pQueue->pAllocator = pAllocator;
        return CY_TRUE;
    }

    type_t *pData = Allocator_AllocateArrayStorage<type_t>(
        pAllocator,
        nInitialCapacity );
    if ( pData == nullptr ) {
        return CY_FALSE;
    }

    pQueue->pData = pData;
    pQueue->nCapacity = nInitialCapacity;
    pQueue->pAllocator = pAllocator;
    return CY_TRUE;
}

template <typename type_t>
void Queue_Shutdown( queue_t<type_t> *pQueue ) noexcept
{
    const bool_t bValidQueue = Queue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "Queue_Shutdown requires a valid queue." );
    if ( !bValidQueue ) {
        return;
    }

    detail::Queue_DestroyLiveElements( *pQueue );
    Allocator_FreeArrayStorage(
        pQueue->pAllocator,
        pQueue->pData,
        pQueue->nCapacity );
    detail::Queue_Reset( *pQueue );
}

template <typename type_t>
void Queue_Clear( queue_t<type_t> *pQueue ) noexcept
{
    const bool_t bValidQueue = Queue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "Queue_Clear requires a valid queue." );
    if ( !bValidQueue ) {
        return;
    }

    detail::Queue_DestroyLiveElements( *pQueue );
    pQueue->nCount = 0u;
    pQueue->iHead = 0u;
}

template <typename type_t>
bool_t Queue_IsValid( const queue_t<type_t> *pQueue ) noexcept
{
    if ( pQueue == nullptr ) {
        return CY_FALSE;
    }
    if ( pQueue->pData == nullptr ) {
        return pQueue->nCapacity == 0u &&
               pQueue->nCount == 0u &&
               pQueue->iHead == 0u &&
               ( pQueue->pAllocator == nullptr ||
                 Allocator_IsValid( pQueue->pAllocator ) );
    }

    return pQueue->nCapacity > 0u &&
           pQueue->nCount <= pQueue->nCapacity &&
           pQueue->iHead < pQueue->nCapacity &&
           ( pQueue->nCount > 0u || pQueue->iHead == 0u ) &&
           Allocator_IsValid( pQueue->pAllocator );
}

template <typename type_t>
bool_t Queue_Reserve(
    queue_t<type_t> *pQueue,
    usize nCapacity ) noexcept
{
    const bool_t bValidQueue = Queue_IsValid( pQueue );
    const bool_t bAllocatorBound =
        bValidQueue && Allocator_IsValid( pQueue->pAllocator );
    CY_ASSERT_MSG( bValidQueue, "Queue_Reserve requires a valid queue." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "Queue_Reserve requires an initialized allocator binding." );
    if ( !bValidQueue || !bAllocatorBound ) {
        return CY_FALSE;
    }
    if ( nCapacity <= pQueue->nCapacity ) {
        return CY_TRUE;
    }
    return detail::Queue_ReallocateExact( pQueue, nCapacity );
}

template <typename type_t>
bool_t Queue_ShrinkToFit( queue_t<type_t> *pQueue ) noexcept
{
    const bool_t bValidQueue = Queue_IsValid( pQueue );
    const bool_t bAllocatorBound =
        bValidQueue && Allocator_IsValid( pQueue->pAllocator );
    CY_ASSERT_MSG(
        bValidQueue,
        "Queue_ShrinkToFit requires a valid queue." );
    CY_ASSERT_MSG(
        bAllocatorBound,
        "Queue_ShrinkToFit requires an initialized allocator binding." );
    if ( !bValidQueue || !bAllocatorBound ) {
        return CY_FALSE;
    }
    if ( pQueue->nCount == pQueue->nCapacity ) {
        return CY_TRUE;
    }
    if ( pQueue->nCount == 0u ) {
        Allocator_FreeArrayStorage(
            pQueue->pAllocator,
            pQueue->pData,
            pQueue->nCapacity );
        pQueue->pData = nullptr;
        pQueue->nCapacity = 0u;
        pQueue->iHead = 0u;
        return CY_TRUE;
    }
    return detail::Queue_ReallocateExact( pQueue, pQueue->nCount );
}

template <typename type_t>
bool_t Queue_Push(
    queue_t<type_t> *pQueue,
    const type_t &value ) noexcept
{
    static_assert(
        std::is_nothrow_copy_constructible_v<type_t>,
        "Queue_Push requires nothrow copy construction." );

    const bool_t bValidQueue = Queue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "Queue_Push requires a valid queue." );
    if ( !bValidQueue || !Allocator_IsValid( pQueue->pAllocator ) ) {
        return CY_FALSE;
    }

    const bool_t bMustGrow = pQueue->nCount == pQueue->nCapacity;
    const bool_t bInternalValue =
        detail::Queue_PointerIsInternal( *pQueue, &value );
    if ( bMustGrow && bInternalValue ) {
        type_t temporary( value );
        if ( !detail::Queue_EnsureAdditional( pQueue, 1u ) ) {
            return CY_FALSE;
        }
        const usize iTail = detail::Queue_PhysicalIndex(
            pQueue->iHead,
            pQueue->nCount,
            pQueue->nCapacity );
        ::new ( static_cast<void *>( pQueue->pData + iTail ) )
            type_t( temporary );
    } else {
        if ( !detail::Queue_EnsureAdditional( pQueue, 1u ) ) {
            return CY_FALSE;
        }
        const usize iTail = detail::Queue_PhysicalIndex(
            pQueue->iHead,
            pQueue->nCount,
            pQueue->nCapacity );
        ::new ( static_cast<void *>( pQueue->pData + iTail ) )
            type_t( value );
    }

    ++pQueue->nCount;
    return CY_TRUE;
}

template <typename type_t>
bool_t Queue_PushMove(
    queue_t<type_t> *pQueue,
    type_t &&value ) noexcept
{
    static_assert(
        std::is_nothrow_move_constructible_v<type_t>,
        "Queue_PushMove requires nothrow move construction." );

    const bool_t bValidQueue = Queue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "Queue_PushMove requires a valid queue." );
    if ( !bValidQueue || !Allocator_IsValid( pQueue->pAllocator ) ) {
        return CY_FALSE;
    }

    const bool_t bMustGrow = pQueue->nCount == pQueue->nCapacity;
    const bool_t bInternalValue =
        detail::Queue_PointerIsInternal( *pQueue, &value );
    if ( bMustGrow && bInternalValue ) {
        type_t temporary( static_cast<type_t &&>( value ) );
        if ( !detail::Queue_EnsureAdditional( pQueue, 1u ) ) {
            return CY_FALSE;
        }
        const usize iTail = detail::Queue_PhysicalIndex(
            pQueue->iHead,
            pQueue->nCount,
            pQueue->nCapacity );
        ::new ( static_cast<void *>( pQueue->pData + iTail ) )
            type_t( static_cast<type_t &&>( temporary ) );
    } else {
        if ( !detail::Queue_EnsureAdditional( pQueue, 1u ) ) {
            return CY_FALSE;
        }
        const usize iTail = detail::Queue_PhysicalIndex(
            pQueue->iHead,
            pQueue->nCount,
            pQueue->nCapacity );
        ::new ( static_cast<void *>( pQueue->pData + iTail ) )
            type_t( static_cast<type_t &&>( value ) );
    }

    ++pQueue->nCount;
    return CY_TRUE;
}

template <typename type_t, typename... args_t>
type_t *Queue_Emplace(
    queue_t<type_t> *pQueue,
    args_t &&... args ) noexcept
{
    static_assert(
        std::is_nothrow_constructible_v<type_t, args_t...>,
        "Queue_Emplace construction must not throw." );
    static_assert(
        std::is_nothrow_move_constructible_v<type_t>,
        "Queue_Emplace requires nothrow move construction." );

    const bool_t bValidQueue = Queue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "Queue_Emplace requires a valid queue." );
    if ( !bValidQueue || !Allocator_IsValid( pQueue->pAllocator ) ) {
        return nullptr;
    }

    type_t temporary( static_cast<args_t &&>( args )... );
    if ( !detail::Queue_EnsureAdditional( pQueue, 1u ) ) {
        return nullptr;
    }
    const usize iTail = detail::Queue_PhysicalIndex(
        pQueue->iHead,
        pQueue->nCount,
        pQueue->nCapacity );
    type_t *pElement = pQueue->pData + iTail;
    ::new ( static_cast<void *>( pElement ) )
        type_t( static_cast<type_t &&>( temporary ) );
    ++pQueue->nCount;
    return pElement;
}

template <typename type_t>
bool_t Queue_Pop(
    queue_t<type_t> *pQueue,
    type_t *pValueOut ) noexcept
{
    const bool_t bValidQueue = Queue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "Queue_Pop requires a valid queue." );
    if ( !bValidQueue || pQueue->nCount == 0u ) {
        return CY_FALSE;
    }

    const bool_t bExternalOutput =
        !detail::Queue_PointerIsInternal( *pQueue, pValueOut );
    CY_ASSERT_MSG(
        bExternalOutput,
        "Queue_Pop output may not alias queue storage." );
    if ( !bExternalOutput ) {
        return CY_FALSE;
    }

    type_t *pFront = pQueue->pData + pQueue->iHead;
    if ( pValueOut != nullptr ) {
        if constexpr ( std::is_nothrow_move_assignable_v<type_t> ) {
            *pValueOut = static_cast<type_t &&>( *pFront );
        } else if constexpr ( std::is_nothrow_copy_assignable_v<type_t> ) {
            *pValueOut = *pFront;
        } else {
            CY_ASSERT_MSG(
                CY_FALSE,
                "Queue_Pop output requires nothrow move or copy assignment." );
            return CY_FALSE;
        }
    }
    Container_DestroyRange( pFront, 1u );
    --pQueue->nCount;
    pQueue->iHead = pQueue->nCount > 0u
        ? detail::Queue_PhysicalIndex(
            pQueue->iHead,
            1u,
            pQueue->nCapacity )
        : 0u;
    return CY_TRUE;
}

template <typename type_t>
type_t *Queue_Front( queue_t<type_t> *pQueue ) noexcept
{
    return Queue_At( pQueue, 0u );
}

template <typename type_t>
const type_t *Queue_Front( const queue_t<type_t> *pQueue ) noexcept
{
    return Queue_At( pQueue, 0u );
}

template <typename type_t>
type_t *Queue_Back( queue_t<type_t> *pQueue ) noexcept
{
    const bool_t bValidQueue = Queue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "Queue_Back requires a valid queue." );
    return bValidQueue && pQueue->nCount > 0u
        ? Queue_At( pQueue, pQueue->nCount - 1u )
        : nullptr;
}

template <typename type_t>
const type_t *Queue_Back( const queue_t<type_t> *pQueue ) noexcept
{
    const bool_t bValidQueue = Queue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "Queue_Back requires a valid queue." );
    return bValidQueue && pQueue->nCount > 0u
        ? Queue_At( pQueue, pQueue->nCount - 1u )
        : nullptr;
}

template <typename type_t>
type_t *Queue_At(
    queue_t<type_t> *pQueue,
    usize iLogicalIndex ) noexcept
{
    const bool_t bValidQueue = Queue_IsValid( pQueue );
    const bool_t bIndexInRange =
        bValidQueue && iLogicalIndex < pQueue->nCount;
    CY_ASSERT_MSG( bValidQueue, "Queue_At requires a valid queue." );
    if ( bValidQueue && pQueue->nCount > 0u ) {
        CY_ASSERT_MSG( bIndexInRange, "Queue_At index is outside the queue." );
    }
    return bIndexInRange
        ? pQueue->pData + detail::Queue_PhysicalIndex(
            pQueue->iHead,
            iLogicalIndex,
            pQueue->nCapacity )
        : nullptr;
}

template <typename type_t>
const type_t *Queue_At(
    const queue_t<type_t> *pQueue,
    usize iLogicalIndex ) noexcept
{
    const bool_t bValidQueue = Queue_IsValid( pQueue );
    const bool_t bIndexInRange =
        bValidQueue && iLogicalIndex < pQueue->nCount;
    CY_ASSERT_MSG( bValidQueue, "Queue_At requires a valid queue." );
    if ( bValidQueue && pQueue->nCount > 0u ) {
        CY_ASSERT_MSG( bIndexInRange, "Queue_At index is outside the queue." );
    }
    return bIndexInRange
        ? pQueue->pData + detail::Queue_PhysicalIndex(
            pQueue->iHead,
            iLogicalIndex,
            pQueue->nCapacity )
        : nullptr;
}

template <typename type_t>
usize Queue_Count( const queue_t<type_t> *pQueue ) noexcept
{
    const bool_t bValidQueue = Queue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "Queue_Count requires a valid queue." );
    return bValidQueue ? pQueue->nCount : 0u;
}

template <typename type_t>
usize Queue_Capacity( const queue_t<type_t> *pQueue ) noexcept
{
    const bool_t bValidQueue = Queue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "Queue_Capacity requires a valid queue." );
    return bValidQueue ? pQueue->nCapacity : 0u;
}

template <typename type_t>
bool_t Queue_IsEmpty( const queue_t<type_t> *pQueue ) noexcept
{
    const bool_t bValidQueue = Queue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "Queue_IsEmpty requires a valid queue." );
    return bValidQueue ? pQueue->nCount == 0u : CY_TRUE;
}

template <typename type_t>
void Queue_Move(
    queue_t<type_t> *pDest,
    queue_t<type_t> *pSource ) noexcept
{
    const bool_t bDistinctQueues =
        pDest != nullptr && pSource != nullptr && pDest != pSource;
    const bool_t bDestinationEmpty =
        bDistinctQueues && detail::Queue_IsCanonicalEmpty( *pDest );
    const bool_t bSourceValid =
        bDistinctQueues && Queue_IsValid( pSource );
    const bool_t bValidMove =
        bDistinctQueues && bDestinationEmpty && bSourceValid;
    CY_ASSERT_MSG(
        bValidMove,
        "Queue_Move requires distinct queues and a canonical empty destination." );
    if ( !bValidMove ) {
        return;
    }

    pDest->pData = pSource->pData;
    pDest->nCapacity = pSource->nCapacity;
    pDest->nCount = pSource->nCount;
    pDest->iHead = pSource->iHead;
    pDest->pAllocator = pSource->pAllocator;
    detail::Queue_Reset( *pSource );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_QUEUE_INL
