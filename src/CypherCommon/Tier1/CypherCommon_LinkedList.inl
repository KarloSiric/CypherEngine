//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_LinkedList.inl
//  Purpose: Implements allocator-backed doubly linked lists.
//  Details: Nodes own their values and retain stable addresses until erased. Splicing
//           compatible lists relinks endpoints without allocating or moving values.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Linked List Template Definitions

Container mutations must preserve structural invariants and element lifetime. Iterators or
handles are invalidated only according to the rules stated by the public API. Template
definitions remain in this file so each concrete instantiation is compiled at its call site.
================
*/

#ifndef CYPHER_COMMON_TIER1_LINKEDLIST_INL
#define CYPHER_COMMON_TIER1_LINKEDLIST_INL

#ifndef CYPHER_COMMON_TIER1_LINKEDLIST_H
    #include "CypherCommon_LinkedList.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <new>
#include <type_traits>

namespace cypher::common
{

namespace detail
{

template <typename type_t>
bool_t LinkedList_IsCanonicalEmpty(
    const linked_list_t<type_t> &list ) noexcept
{
    return list.pHead == nullptr &&
           list.pTail == nullptr &&
           list.nCount == 0u &&
           list.pAllocator == nullptr;
}

template <typename type_t>
linked_list_node_t<type_t> *LinkedList_CreateNode(
    linked_list_t<type_t> *pList,
    const type_t &value ) noexcept
{
    static_assert(
        std::is_nothrow_copy_constructible_v<type_t>,
        "LinkedList values must support nothrow copy construction." );
    static_assert(
        std::is_nothrow_destructible_v<type_t>,
        "LinkedList values must support nothrow destruction." );

    // Allocate and construct before publishing links into the list.
    void *pStorage = Allocator_Allocate(
        pList->pAllocator,
        sizeof( linked_list_node_t<type_t> ),
        alignof( linked_list_node_t<type_t> ) );
    if ( pStorage == nullptr ) {
        return nullptr;
    }

    return ::new ( pStorage ) linked_list_node_t<type_t>{
        nullptr,
        nullptr,
        pList,
        value
    };
}

template <typename type_t>
void LinkedList_DestroyNode(
    linked_list_t<type_t> *pList,
    linked_list_node_t<type_t> *pNode ) noexcept
{
    pNode->~linked_list_node_t<type_t>();
    Allocator_Free(
        pList->pAllocator,
        pNode,
        sizeof( linked_list_node_t<type_t> ),
        alignof( linked_list_node_t<type_t> ) );
}

} // namespace detail

template <typename type_t>
linked_list_t<type_t>::~linked_list_t() noexcept
{
    LinkedList_Shutdown( this );
}

template <typename type_t>
bool_t LinkedList_Init(
    linked_list_t<type_t> *pList,
    const allocator_t *pAllocator ) noexcept
{
    const bool_t bValidDestination =
        pList != nullptr && detail::LinkedList_IsCanonicalEmpty( *pList );
    const bool_t bValidAllocator = Allocator_IsValid( pAllocator );
    CY_ASSERT_MSG(
        bValidDestination,
        "LinkedList_Init requires a canonical empty destination." );
    CY_ASSERT_MSG(
        bValidAllocator,
        "LinkedList_Init requires a valid allocator." );
    if ( !bValidDestination || !bValidAllocator ) {
        return CY_FALSE;
    }

    pList->pAllocator = pAllocator;
    return CY_TRUE;
}

template <typename type_t>
void LinkedList_Shutdown( linked_list_t<type_t> *pList ) noexcept
{
    if ( pList == nullptr || detail::LinkedList_IsCanonicalEmpty( *pList ) ) {
        return;
    }

    const bool_t bValidList = LinkedList_IsValid( pList );
    CY_ASSERT_MSG(
        bValidList,
        "LinkedList_Shutdown requires a valid list." );
    if ( !bValidList ) {
        return;
    }

    LinkedList_Clear( pList );
    pList->pAllocator = nullptr;
}

template <typename type_t>
void LinkedList_Clear( linked_list_t<type_t> *pList ) noexcept
{
    const bool_t bValidList = LinkedList_IsValid( pList );
    CY_ASSERT_MSG( bValidList, "LinkedList_Clear requires a valid list." );
    if ( !bValidList ) {
        return;
    }

    // Save the next link before destroying the current node.
    linked_list_node_t<type_t> *pNode = pList->pHead;
    while ( pNode != nullptr ) {
        linked_list_node_t<type_t> *pNext = pNode->pNext;
        detail::LinkedList_DestroyNode( pList, pNode );
        pNode = pNext;
    }
    pList->pHead = nullptr;
    pList->pTail = nullptr;
    pList->nCount = 0u;
}

template <typename type_t>
bool_t LinkedList_IsValid( const linked_list_t<type_t> *pList ) noexcept
{
    if ( pList == nullptr ) {
        return CY_FALSE;
    }
    if ( pList->pAllocator == nullptr ) {
        return detail::LinkedList_IsCanonicalEmpty( *pList );
    }
    if ( !Allocator_IsValid( pList->pAllocator ) ) {
        return CY_FALSE;
    }
    if ( pList->nCount == 0u ) {
        return pList->pHead == nullptr && pList->pTail == nullptr;
    }
    return pList->pHead != nullptr &&
           pList->pTail != nullptr &&
           pList->pHead->pPrevious == nullptr &&
           pList->pTail->pNext == nullptr &&
           pList->pHead->pOwner == pList &&
           pList->pTail->pOwner == pList;
}

template <typename type_t>
linked_list_node_t<type_t> *LinkedList_PushFront(
    linked_list_t<type_t> *pList,
    const type_t &value ) noexcept
{
    const bool_t bValidList = LinkedList_IsValid( pList ) &&
                              pList->pAllocator != nullptr;
    CY_ASSERT_MSG( bValidList, "LinkedList_PushFront requires an initialized list." );
    if ( !bValidList ) {
        return nullptr;
    }

    linked_list_node_t<type_t> *pNode =
        detail::LinkedList_CreateNode( pList, value );
    if ( pNode == nullptr ) {
        return nullptr;
    }
    pNode->pNext = pList->pHead;
    if ( pList->pHead != nullptr ) {
        pList->pHead->pPrevious = pNode;
    } else {
        pList->pTail = pNode;
    }
    pList->pHead = pNode;
    ++pList->nCount;
    return pNode;
}

template <typename type_t>
linked_list_node_t<type_t> *LinkedList_PushBack(
    linked_list_t<type_t> *pList,
    const type_t &value ) noexcept
{
    const bool_t bValidList = LinkedList_IsValid( pList ) &&
                              pList->pAllocator != nullptr;
    CY_ASSERT_MSG( bValidList, "LinkedList_PushBack requires an initialized list." );
    if ( !bValidList ) {
        return nullptr;
    }

    linked_list_node_t<type_t> *pNode =
        detail::LinkedList_CreateNode( pList, value );
    if ( pNode == nullptr ) {
        return nullptr;
    }
    pNode->pPrevious = pList->pTail;
    if ( pList->pTail != nullptr ) {
        pList->pTail->pNext = pNode;
    } else {
        pList->pHead = pNode;
    }
    pList->pTail = pNode;
    ++pList->nCount;
    return pNode;
}

template <typename type_t>
linked_list_node_t<type_t> *LinkedList_InsertBefore(
    linked_list_t<type_t> *pList,
    linked_list_node_t<type_t> *pPosition,
    const type_t &value ) noexcept
{
    const bool_t bValidPosition =
        LinkedList_IsValid( pList ) &&
        pPosition != nullptr &&
        pPosition->pOwner == pList;
    CY_ASSERT_MSG(
        bValidPosition,
        "LinkedList_InsertBefore requires a valid position." );
    if ( !bValidPosition ) {
        return nullptr;
    }
    if ( pPosition == pList->pHead ) {
        return LinkedList_PushFront( pList, value );
    }

    linked_list_node_t<type_t> *pNode =
        detail::LinkedList_CreateNode( pList, value );
    if ( pNode == nullptr ) {
        return nullptr;
    }
    pNode->pPrevious = pPosition->pPrevious;
    pNode->pNext = pPosition;
    pPosition->pPrevious->pNext = pNode;
    pPosition->pPrevious = pNode;
    ++pList->nCount;
    return pNode;
}

template <typename type_t>
void LinkedList_Erase(
    linked_list_t<type_t> *pList,
    linked_list_node_t<type_t> *pNode ) noexcept
{
    const bool_t bValidNode = LinkedList_IsValid( pList ) &&
                              pNode != nullptr &&
                              pNode->pOwner == pList &&
                              pList->nCount > 0u;
    CY_ASSERT_MSG( bValidNode, "LinkedList_Erase requires a live list node." );
    if ( !bValidNode ) {
        return;
    }

    // Reconnect both neighbors before the removed node is destroyed.
    if ( pNode->pPrevious != nullptr ) {
        pNode->pPrevious->pNext = pNode->pNext;
    } else {
        pList->pHead = pNode->pNext;
    }
    if ( pNode->pNext != nullptr ) {
        pNode->pNext->pPrevious = pNode->pPrevious;
    } else {
        pList->pTail = pNode->pPrevious;
    }
    detail::LinkedList_DestroyNode( pList, pNode );
    --pList->nCount;
}

template <typename type_t>
void LinkedList_SpliceBack(
    linked_list_t<type_t> *pDest,
    linked_list_t<type_t> *pSource ) noexcept
{
    const bool_t bValidLists = LinkedList_IsValid( pDest ) &&
                               LinkedList_IsValid( pSource ) &&
                               pDest != pSource &&
                               pDest->pAllocator != nullptr &&
                               pDest->pAllocator == pSource->pAllocator;
    CY_ASSERT_MSG(
        bValidLists,
        "LinkedList_SpliceBack requires distinct lists with the same allocator." );
    if ( !bValidLists || pSource->nCount == 0u ) {
        return;
    }

    // Splicing transfers nodes without allocation or element movement.
    if ( pDest->pTail != nullptr ) {
        pDest->pTail->pNext = pSource->pHead;
        pSource->pHead->pPrevious = pDest->pTail;
    } else {
        pDest->pHead = pSource->pHead;
    }
    pDest->pTail = pSource->pTail;
    pDest->nCount += pSource->nCount;
    // Rebind ownership so later erase validation accepts the transferred nodes.
    for ( linked_list_node_t<type_t> *pNode = pSource->pHead;
          pNode != nullptr;
          pNode = pNode->pNext ) {
        pNode->pOwner = pDest;
    }
    pSource->pHead = nullptr;
    pSource->pTail = nullptr;
    pSource->nCount = 0u;
}

template <typename type_t>
linked_list_node_t<type_t> *LinkedList_Front(
    linked_list_t<type_t> *pList ) noexcept
{
    return const_cast<linked_list_node_t<type_t> *>( LinkedList_Front(
        static_cast<const linked_list_t<type_t> *>( pList ) ) );
}

template <typename type_t>
const linked_list_node_t<type_t> *LinkedList_Front(
    const linked_list_t<type_t> *pList ) noexcept
{
    return LinkedList_IsValid( pList ) ? pList->pHead : nullptr;
}

template <typename type_t>
linked_list_node_t<type_t> *LinkedList_Back(
    linked_list_t<type_t> *pList ) noexcept
{
    return const_cast<linked_list_node_t<type_t> *>( LinkedList_Back(
        static_cast<const linked_list_t<type_t> *>( pList ) ) );
}

template <typename type_t>
const linked_list_node_t<type_t> *LinkedList_Back(
    const linked_list_t<type_t> *pList ) noexcept
{
    return LinkedList_IsValid( pList ) ? pList->pTail : nullptr;
}

template <typename type_t>
linked_list_node_t<type_t> *LinkedList_Next(
    linked_list_node_t<type_t> *pNode ) noexcept
{
    return pNode != nullptr ? pNode->pNext : nullptr;
}

template <typename type_t>
const linked_list_node_t<type_t> *LinkedList_Next(
    const linked_list_node_t<type_t> *pNode ) noexcept
{
    return pNode != nullptr ? pNode->pNext : nullptr;
}

template <typename type_t>
linked_list_node_t<type_t> *LinkedList_Previous(
    linked_list_node_t<type_t> *pNode ) noexcept
{
    return pNode != nullptr ? pNode->pPrevious : nullptr;
}

template <typename type_t>
const linked_list_node_t<type_t> *LinkedList_Previous(
    const linked_list_node_t<type_t> *pNode ) noexcept
{
    return pNode != nullptr ? pNode->pPrevious : nullptr;
}

template <typename type_t>
usize LinkedList_Count( const linked_list_t<type_t> *pList ) noexcept
{
    return LinkedList_IsValid( pList ) ? pList->nCount : 0u;
}

template <typename type_t>
bool_t LinkedList_IsEmpty( const linked_list_t<type_t> *pList ) noexcept
{
    return LinkedList_Count( pList ) == 0u;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_LINKEDLIST_INL
