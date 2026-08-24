//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_TsList.cpp
//  Purpose: Implements CypherCommon Tier0 thread-safe intrusive list.
//  Details: This simple mutex-backed intrusive stack is useful for low-level
//           free lists before higher containers exist.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TsList.h"

namespace cypher::common
{

// pOwner is claimed atomically before linking a node. This prevents one
// intrusive node from being inserted into two lists even when callers race.

bool_t Cy_TsListInit( tslist_t *pList ) noexcept
{
    if ( pList == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pList->nativeMutex );
    if ( pList->isInitialized || pList->pHead != nullptr ||
         pList->nCount != 0u ) {
        return CY_FALSE;
    }

    pList->pHead = nullptr;
    pList->nCount = 0u;
    pList->isInitialized = CY_TRUE;
    return CY_TRUE;
}

bool_t Cy_TsListShutdown( tslist_t *pList ) noexcept
{
    if ( pList == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pList->nativeMutex );
    // Shutdown refuses a non-empty list because intrusive nodes are owned by their
    // callers; silently detaching them would strand their pOwner markers.
    if ( !pList->isInitialized || pList->pHead != nullptr ||
         pList->nCount != 0u ) {
        return CY_FALSE;
    }
    pList->isInitialized = CY_FALSE;
    return CY_TRUE;
}

bool_t Cy_TsListPush( tslist_t *pList, tslist_node_t *pNode ) noexcept
{
    if ( pList == nullptr || pNode == nullptr ) {
        return CY_FALSE;
    }

    std::lock_guard<std::mutex> lock( pList->nativeMutex );
    if ( !pList->isInitialized ) {
        return CY_FALSE;
    }

    // Claim ownership before mutating links. compare_exchange makes duplicate or
    // cross-list insertion fail even if callers race on the same node.
    tslist_t *pExpectedOwner = nullptr;
    if ( !pNode->pOwner.compare_exchange_strong(
             pExpectedOwner,
             pList,
             std::memory_order_acq_rel,
             std::memory_order_acquire ) ) {
        return CY_FALSE;
    }

    pNode->pNext = pList->pHead;
    pList->pHead = pNode;
    ++pList->nCount;
    return CY_TRUE;
}

tslist_node_t *Cy_TsListPop( tslist_t *pList ) noexcept
{
    if ( pList == nullptr ) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock( pList->nativeMutex );
    if ( !pList->isInitialized ) {
        return nullptr;
    }

    tslist_node_t *pNode = pList->pHead;
    if ( pNode != nullptr ) {
        pList->pHead = pNode->pNext;
        pNode->pNext = nullptr;
        // Clear links and ownership before returning the node for immediate reuse.
        pNode->pOwner.store( nullptr, std::memory_order_release );
        --pList->nCount;
    }

    return pNode;
}

usize Cy_TsListGetCount( const tslist_t *pList ) noexcept
{
    if ( pList == nullptr ) {
        return 0u;
    }
    std::lock_guard<std::mutex> lock( pList->nativeMutex );
    return pList->isInitialized ? pList->nCount : 0u;
}

bool_t Cy_TsListIsEmpty( const tslist_t *pList ) noexcept
{
    if ( pList == nullptr ) {
        return CY_FALSE;
    }
    std::lock_guard<std::mutex> lock( pList->nativeMutex );
    return pList->isInitialized && pList->nCount == 0u;
}

} // namespace cypher::common
