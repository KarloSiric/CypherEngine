//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_IntrusiveList.cpp
//  Purpose: Implements allocation-free intrusive doubly linked lists.
//  Details: A circular sentinel removes endpoint branches. Each node records its
//           owning list so invalid cross-list removal is rejected in constant time.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_IntrusiveList.h"

namespace cypher::common
{

namespace
{

bool_t ListIsInitialized( const intrusive_list_t *pList ) noexcept
{
    return pList != nullptr &&
           pList->root.pPrevious != nullptr &&
           pList->root.pNext != nullptr &&
           pList->root.pOwner == pList;
}

bool_t NodeIsDetached( const intrusive_list_node_t *pNode ) noexcept
{
    return pNode != nullptr &&
           pNode->pPrevious == nullptr &&
           pNode->pNext == nullptr &&
           pNode->pOwner == nullptr;
}

void InsertBetween(
    intrusive_list_t *pList,
    intrusive_list_node_t *pPrevious,
    intrusive_list_node_t *pNext,
    intrusive_list_node_t *pNode ) noexcept
{
    // Nodes carry their links and owner; insertion performs no allocation and
    // ownership prevents one node from being linked into two lists at once.
    pNode->pPrevious = pPrevious;
    pNode->pNext = pNext;
    pNode->pOwner = pList;
    pPrevious->pNext = pNode;
    pNext->pPrevious = pNode;
    ++pList->nCount;
}

} // namespace

void IntrusiveList_Init( intrusive_list_t *pList ) noexcept
{
    CY_ASSERT_MSG( pList != nullptr, "IntrusiveList_Init requires a list." );
    if ( pList == nullptr ) {
        return;
    }
    if ( ListIsInitialized( pList ) ) {
        IntrusiveList_Clear( pList );
    }
    // The sentinel points to itself when empty, eliminating null endpoint cases.
    pList->root.pPrevious = &pList->root;
    pList->root.pNext = &pList->root;
    pList->root.pOwner = pList;
    pList->nCount = 0u;
}

void IntrusiveList_Clear( intrusive_list_t *pList ) noexcept
{
    const bool_t bValid = ListIsInitialized( pList );
    CY_ASSERT_MSG( bValid, "IntrusiveList_Clear requires an initialized list." );
    if ( !bValid ) {
        return;
    }

    intrusive_list_node_t *pNode = pList->root.pNext;
    while ( pNode != &pList->root ) {
        intrusive_list_node_t *pNext = pNode->pNext;
        // Clearing detaches nodes but never destroys their containing objects.
        *pNode = {};
        pNode = pNext;
    }
    pList->root.pPrevious = &pList->root;
    pList->root.pNext = &pList->root;
    pList->nCount = 0u;
}

bool_t IntrusiveList_IsLinked( const intrusive_list_node_t *pNode ) noexcept
{
    return pNode != nullptr &&
           pNode->pPrevious != nullptr &&
           pNode->pNext != nullptr &&
           pNode->pOwner != nullptr;
}

void IntrusiveList_PushFront(
    intrusive_list_t *pList,
    intrusive_list_node_t *pNode ) noexcept
{
    const bool_t bValid = ListIsInitialized( pList ) && NodeIsDetached( pNode );
    CY_ASSERT_MSG( bValid, "IntrusiveList_PushFront requires a detached node." );
    if ( !bValid ) {
        return;
    }
    InsertBetween( pList, &pList->root, pList->root.pNext, pNode );
}

void IntrusiveList_PushBack(
    intrusive_list_t *pList,
    intrusive_list_node_t *pNode ) noexcept
{
    const bool_t bValid = ListIsInitialized( pList ) && NodeIsDetached( pNode );
    CY_ASSERT_MSG( bValid, "IntrusiveList_PushBack requires a detached node." );
    if ( !bValid ) {
        return;
    }
    InsertBetween( pList, pList->root.pPrevious, &pList->root, pNode );
}

void IntrusiveList_InsertBefore(
    intrusive_list_t *pList,
    intrusive_list_node_t *pPosition,
    intrusive_list_node_t *pNode ) noexcept
{
    const bool_t bValid = ListIsInitialized( pList ) &&
                          pPosition != nullptr &&
                          pPosition->pOwner == pList &&
                          NodeIsDetached( pNode );
    CY_ASSERT_MSG(
        bValid,
        "IntrusiveList_InsertBefore requires a position in the list and a detached node." );
    if ( !bValid ) {
        return;
    }
    InsertBetween( pList, pPosition->pPrevious, pPosition, pNode );
}

void IntrusiveList_Remove(
    intrusive_list_t *pList,
    intrusive_list_node_t *pNode ) noexcept
{
    const bool_t bValid = ListIsInitialized( pList ) &&
                          pNode != nullptr &&
                          pNode != &pList->root &&
                          pNode->pOwner == pList;
    CY_ASSERT_MSG( bValid, "IntrusiveList_Remove requires a node owned by the list." );
    if ( !bValid ) {
        return;
    }

    // Repair neighbors before clearing the node's own links.
    pNode->pPrevious->pNext = pNode->pNext;
    pNode->pNext->pPrevious = pNode->pPrevious;
    *pNode = {};
    --pList->nCount;
}

intrusive_list_node_t *IntrusiveList_Front( intrusive_list_t *pList ) noexcept
{
    return const_cast<intrusive_list_node_t *>(
        IntrusiveList_Front( static_cast<const intrusive_list_t *>( pList ) ) );
}

const intrusive_list_node_t *IntrusiveList_Front(
    const intrusive_list_t *pList ) noexcept
{
    if ( !ListIsInitialized( pList ) || pList->nCount == 0u ) {
        return nullptr;
    }
    return pList->root.pNext;
}

intrusive_list_node_t *IntrusiveList_Back( intrusive_list_t *pList ) noexcept
{
    return const_cast<intrusive_list_node_t *>(
        IntrusiveList_Back( static_cast<const intrusive_list_t *>( pList ) ) );
}

const intrusive_list_node_t *IntrusiveList_Back(
    const intrusive_list_t *pList ) noexcept
{
    if ( !ListIsInitialized( pList ) || pList->nCount == 0u ) {
        return nullptr;
    }
    return pList->root.pPrevious;
}

intrusive_list_node_t *IntrusiveList_Next(
    const intrusive_list_t *pList,
    intrusive_list_node_t *pNode ) noexcept
{
    return const_cast<intrusive_list_node_t *>( IntrusiveList_Next(
        pList,
        static_cast<const intrusive_list_node_t *>( pNode ) ) );
}

const intrusive_list_node_t *IntrusiveList_Next(
    const intrusive_list_t *pList,
    const intrusive_list_node_t *pNode ) noexcept
{
    if ( !ListIsInitialized( pList ) || pNode == nullptr || pNode->pOwner != pList ) {
        return nullptr;
    }
    return pNode->pNext == &pList->root ? nullptr : pNode->pNext;
}

intrusive_list_node_t *IntrusiveList_Previous(
    const intrusive_list_t *pList,
    intrusive_list_node_t *pNode ) noexcept
{
    return const_cast<intrusive_list_node_t *>( IntrusiveList_Previous(
        pList,
        static_cast<const intrusive_list_node_t *>( pNode ) ) );
}

const intrusive_list_node_t *IntrusiveList_Previous(
    const intrusive_list_t *pList,
    const intrusive_list_node_t *pNode ) noexcept
{
    if ( !ListIsInitialized( pList ) || pNode == nullptr || pNode->pOwner != pList ) {
        return nullptr;
    }
    return pNode->pPrevious == &pList->root ? nullptr : pNode->pPrevious;
}

usize IntrusiveList_Count( const intrusive_list_t *pList ) noexcept
{
    return ListIsInitialized( pList ) ? pList->nCount : 0u;
}

bool_t IntrusiveList_IsEmpty( const intrusive_list_t *pList ) noexcept
{
    return IntrusiveList_Count( pList ) == 0u;
}

} // namespace cypher::common
