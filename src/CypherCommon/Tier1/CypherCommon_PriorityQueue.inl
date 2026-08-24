//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_PriorityQueue.inl
//  Purpose: Implements allocator-backed binary-heap priority queues.
//  Details: Push and pop preserve the heap invariant in logarithmic time. Storage is
//           contiguous and all allocation failure is reported without losing values.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_PRIORITYQUEUE_INL
#define CYPHER_COMMON_TIER1_PRIORITYQUEUE_INL

#ifndef CYPHER_COMMON_TIER1_PRIORITYQUEUE_H
    #include "CypherCommon_PriorityQueue.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <type_traits>

namespace cypher::common
{

namespace detail
{

template <typename type_t>
void PriorityQueue_Swap( type_t &left, type_t &right ) noexcept
{
    static_assert(
        std::is_nothrow_move_constructible_v<type_t> &&
        std::is_nothrow_move_assignable_v<type_t>,
        "PriorityQueue values must support nothrow move operations." );

    type_t temporary( static_cast<type_t &&>( left ) );
    left = static_cast<type_t &&>( right );
    right = static_cast<type_t &&>( temporary );
}

template <typename type_t, typename compare_t>
void PriorityQueue_SiftUp(
    priority_queue_t<type_t, compare_t> *pQueue,
    usize iChild ) noexcept
{
    // compare(parent, child) means the child has higher priority. Swap upward
    // until the parent already dominates or the root is reached.
    while ( iChild > 0u ) {
        const usize iParent = ( iChild - 1u ) / 2u;
        if ( !pQueue->compare(
                 pQueue->storage.pData[iParent],
                 pQueue->storage.pData[iChild] ) ) {
            break;
        }
        PriorityQueue_Swap(
            pQueue->storage.pData[iParent],
            pQueue->storage.pData[iChild] );
        iChild = iParent;
    }
}

template <typename type_t, typename compare_t>
void PriorityQueue_SiftDown(
    priority_queue_t<type_t, compare_t> *pQueue,
    usize iParent ) noexcept
{
    const usize nCount = pQueue->storage.nCount;
    while ( iParent < nCount / 2u ) {
        const usize iLeft = iParent * 2u + 1u;
        const usize iRight = iLeft + 1u;
        // Select the higher-priority child before comparing it with the parent;
        // restoring only one branch is sufficient after removing the root.
        usize iPreferred = iLeft;
        if ( iRight < nCount &&
             pQueue->compare(
                 pQueue->storage.pData[iLeft],
                 pQueue->storage.pData[iRight] ) ) {
            iPreferred = iRight;
        }
        if ( !pQueue->compare(
                 pQueue->storage.pData[iParent],
                 pQueue->storage.pData[iPreferred] ) ) {
            break;
        }
        PriorityQueue_Swap(
            pQueue->storage.pData[iParent],
            pQueue->storage.pData[iPreferred] );
        iParent = iPreferred;
    }
}

} // namespace detail

template <typename type_t, typename compare_t>
bool_t PriorityQueue_Init(
    priority_queue_t<type_t, compare_t> *pQueue,
    const allocator_t *pAllocator,
    usize nInitialCapacity,
    compare_t compare ) noexcept
{
    const bool_t bValidDestination = pQueue != nullptr;
    CY_ASSERT_MSG(
        bValidDestination,
        "PriorityQueue_Init requires destination storage." );
    if ( !bValidDestination ||
         !Vector_Init( &pQueue->storage, pAllocator, nInitialCapacity ) ) {
        return CY_FALSE;
    }

    pQueue->compare = static_cast<compare_t &&>( compare );
    return CY_TRUE;
}

template <typename type_t, typename compare_t>
void PriorityQueue_Shutdown(
    priority_queue_t<type_t, compare_t> *pQueue ) noexcept
{
    const bool_t bValidQueue = PriorityQueue_IsValid( pQueue );
    CY_ASSERT_MSG(
        bValidQueue,
        "PriorityQueue_Shutdown requires a valid queue." );
    if ( bValidQueue ) {
        Vector_Shutdown( &pQueue->storage );
    }
}

template <typename type_t, typename compare_t>
void PriorityQueue_Clear(
    priority_queue_t<type_t, compare_t> *pQueue ) noexcept
{
    const bool_t bValidQueue = PriorityQueue_IsValid( pQueue );
    CY_ASSERT_MSG(
        bValidQueue,
        "PriorityQueue_Clear requires a valid queue." );
    if ( bValidQueue ) {
        Vector_Clear( &pQueue->storage );
    }
}

template <typename type_t, typename compare_t>
bool_t PriorityQueue_IsValid(
    const priority_queue_t<type_t, compare_t> *pQueue ) noexcept
{
    return pQueue != nullptr && Vector_IsValid( &pQueue->storage );
}

template <typename type_t, typename compare_t>
bool_t PriorityQueue_Push(
    priority_queue_t<type_t, compare_t> *pQueue,
    const type_t &value ) noexcept
{
    const bool_t bValidQueue = PriorityQueue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "PriorityQueue_Push requires a valid queue." );
    // Grow and construct first. Allocation failure leaves the existing heap untouched.
    if ( !bValidQueue || !Vector_PushBack( &pQueue->storage, value ) ) {
        return CY_FALSE;
    }

    detail::PriorityQueue_SiftUp( pQueue, pQueue->storage.nCount - 1u );
    return CY_TRUE;
}

template <typename type_t, typename compare_t>
bool_t PriorityQueue_Pop(
    priority_queue_t<type_t, compare_t> *pQueue,
    type_t *pValueOut ) noexcept
{
    static_assert(
        std::is_nothrow_copy_assignable_v<type_t>,
        "PriorityQueue output requires nothrow copy assignment." );

    const bool_t bValidQueue = PriorityQueue_IsValid( pQueue );
    CY_ASSERT_MSG( bValidQueue, "PriorityQueue_Pop requires a valid queue." );
    if ( !bValidQueue || pQueue->storage.nCount == 0u ) {
        return CY_FALSE;
    }

    if ( pValueOut != nullptr ) {
        *pValueOut = pQueue->storage.pData[0];
    }
    const usize iLast = pQueue->storage.nCount - 1u;
    if ( iLast == 0u ) {
        Vector_PopBack( &pQueue->storage );
        return CY_TRUE;
    }

    // Move the final leaf into the root hole, remove the duplicate tail object,
    // then restore heap order downward.
    pQueue->storage.pData[0] =
        static_cast<type_t &&>( pQueue->storage.pData[iLast] );
    Vector_PopBack( &pQueue->storage );
    detail::PriorityQueue_SiftDown( pQueue, 0u );
    return CY_TRUE;
}

template <typename type_t, typename compare_t>
type_t *PriorityQueue_Top(
    priority_queue_t<type_t, compare_t> *pQueue ) noexcept
{
    return const_cast<type_t *>( PriorityQueue_Top(
        static_cast<const priority_queue_t<type_t, compare_t> *>( pQueue ) ) );
}

template <typename type_t, typename compare_t>
const type_t *PriorityQueue_Top(
    const priority_queue_t<type_t, compare_t> *pQueue ) noexcept
{
    return PriorityQueue_IsValid( pQueue ) && pQueue->storage.nCount > 0u
        ? pQueue->storage.pData
        : nullptr;
}

template <typename type_t, typename compare_t>
usize PriorityQueue_Count(
    const priority_queue_t<type_t, compare_t> *pQueue ) noexcept
{
    return PriorityQueue_IsValid( pQueue ) ? pQueue->storage.nCount : 0u;
}

template <typename type_t, typename compare_t>
bool_t PriorityQueue_IsEmpty(
    const priority_queue_t<type_t, compare_t> *pQueue ) noexcept
{
    return PriorityQueue_Count( pQueue ) == 0u;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_PRIORITYQUEUE_INL
