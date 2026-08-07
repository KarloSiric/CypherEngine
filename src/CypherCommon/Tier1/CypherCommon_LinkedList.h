//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_LinkedList.h
//  Purpose: Declares allocator-backed doubly linked lists.
//  Details: Nodes have stable addresses until erased, but each insertion allocates.
//           Prefer contiguous containers unless stable nodes or constant-time splice matter.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_LINKEDLIST_H
#define CYPHER_COMMON_TIER1_LINKEDLIST_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"

namespace cypher::common
{

template <typename type_t>
struct linked_list_node_t {
    linked_list_node_t *pPrevious{ nullptr };
    linked_list_node_t *pNext{ nullptr };
    type_t value{};
};

template <typename type_t>
struct linked_list_t {
    linked_list_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( linked_list_t );

    linked_list_node_t<type_t> *pHead{ nullptr };
    linked_list_node_t<type_t> *pTail{ nullptr };
    usize nCount{ 0u };
    const allocator_t *pAllocator{ nullptr };
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

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_LINKEDLIST_H
