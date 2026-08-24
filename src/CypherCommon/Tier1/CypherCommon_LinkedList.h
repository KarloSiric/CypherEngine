//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_LinkedList.h
//  Purpose: Declares allocator-backed doubly linked lists.
//  Details: Nodes have stable addresses until erased, but each insertion allocates.
//           Prefer contiguous containers unless stable nodes or bulk relinking matter.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Linked List Contract

Container mutations must preserve structural invariants and element lifetime. Iterators or
handles are invalidated only according to the rules stated by the public API.
================
*/

#ifndef CYPHER_COMMON_TIER1_LINKEDLIST_H
#define CYPHER_COMMON_TIER1_LINKEDLIST_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"

namespace cypher::common
{

// Nodes are individually allocated and retain stable addresses until erased. pOwner makes
// cross-list misuse detectable and is updated for every node during SpliceBack.

template <typename type_t>
struct linked_list_t;

template <typename type_t>
struct linked_list_node_t {
    linked_list_node_t *pPrevious{ nullptr }; // Previous node, or null at the head.
    linked_list_node_t *pNext{ nullptr };     // Next node, or null at the tail.
    linked_list_t<type_t> *pOwner{ nullptr }; // List currently responsible for this node.
    type_t value{};                           // Payload constructed with the node.
};

template <typename type_t>
struct linked_list_t {
    linked_list_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( linked_list_t );
    ~linked_list_t() noexcept;

    linked_list_node_t<type_t> *pHead{ nullptr }; // First node in traversal order.
    linked_list_node_t<type_t> *pTail{ nullptr }; // Last node in traversal order.
    usize nCount{ 0u };                           // Number of allocated live nodes.
    const allocator_t *pAllocator{ nullptr };     // Allocates and releases each node.
};

template <typename type_t>
CYPHER_NODISCARD bool_t LinkedList_Init(
    linked_list_t<type_t> *pList,
    const allocator_t *pAllocator ) noexcept;

template <typename type_t>
void LinkedList_Shutdown( linked_list_t<type_t> *pList ) noexcept;

template <typename type_t>
void LinkedList_Clear( linked_list_t<type_t> *pList ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t LinkedList_IsValid(
    const linked_list_t<type_t> *pList ) noexcept;

template <typename type_t>
CYPHER_NODISCARD linked_list_node_t<type_t> *LinkedList_PushFront(
    linked_list_t<type_t> *pList,
    const type_t &value ) noexcept;

template <typename type_t>
CYPHER_NODISCARD linked_list_node_t<type_t> *LinkedList_PushBack(
    linked_list_t<type_t> *pList,
    const type_t &value ) noexcept;

template <typename type_t>
CYPHER_NODISCARD linked_list_node_t<type_t> *LinkedList_InsertBefore(
    linked_list_t<type_t> *pList,
    linked_list_node_t<type_t> *pPosition,
    const type_t &value ) noexcept;

template <typename type_t>
void LinkedList_Erase(
    linked_list_t<type_t> *pList,
    linked_list_node_t<type_t> *pNode ) noexcept;

template <typename type_t>
void LinkedList_SpliceBack(
    linked_list_t<type_t> *pDest,
    linked_list_t<type_t> *pSource ) noexcept;

template <typename type_t>
CYPHER_NODISCARD linked_list_node_t<type_t> *LinkedList_Front(
    linked_list_t<type_t> *pList ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const linked_list_node_t<type_t> *LinkedList_Front(
    const linked_list_t<type_t> *pList ) noexcept;

template <typename type_t>
CYPHER_NODISCARD linked_list_node_t<type_t> *LinkedList_Back(
    linked_list_t<type_t> *pList ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const linked_list_node_t<type_t> *LinkedList_Back(
    const linked_list_t<type_t> *pList ) noexcept;

template <typename type_t>
CYPHER_NODISCARD linked_list_node_t<type_t> *LinkedList_Next(
    linked_list_node_t<type_t> *pNode ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const linked_list_node_t<type_t> *LinkedList_Next(
    const linked_list_node_t<type_t> *pNode ) noexcept;

template <typename type_t>
CYPHER_NODISCARD linked_list_node_t<type_t> *LinkedList_Previous(
    linked_list_node_t<type_t> *pNode ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const linked_list_node_t<type_t> *LinkedList_Previous(
    const linked_list_node_t<type_t> *pNode ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize LinkedList_Count(
    const linked_list_t<type_t> *pList ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t LinkedList_IsEmpty(
    const linked_list_t<type_t> *pList ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_LINKEDLIST_INL
    #include "CypherCommon_LinkedList.inl"
#endif

#endif // CYPHER_COMMON_TIER1_LINKEDLIST_H
