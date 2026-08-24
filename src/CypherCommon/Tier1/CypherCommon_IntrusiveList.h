//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_IntrusiveList.h
//  Purpose: Declares allocation-free intrusive doubly linked lists.
//  Details: Callers embed nodes in owned objects. A node may belong to at most one
//           list at a time and must be removed before its containing object dies.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_INTRUSIVELIST_H
#define CYPHER_COMMON_TIER1_INTRUSIVELIST_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

// The list owns no nodes. root is a sentinel whose next/previous links close the ring; callers
// embed intrusive_list_node_t in a longer-lived object and must unlink it before that object dies.

struct intrusive_list_t;

struct intrusive_list_node_t {
    intrusive_list_node_t *pPrevious{ nullptr }; // Previous node or the owner's root sentinel.
    intrusive_list_node_t *pNext{ nullptr };     // Next node or the owner's root sentinel.
    intrusive_list_t *pOwner{ nullptr };         // Null while unlinked; otherwise the owning list.
};

struct intrusive_list_t {
    intrusive_list_node_t root{}; // Sentinel; never returned as a user node.
    usize nCount{ 0u };           // Linked user nodes, excluding root.
};

CYPHER_COMMON_API void IntrusiveList_Init( intrusive_list_t *pList ) noexcept;
CYPHER_COMMON_API void IntrusiveList_Clear( intrusive_list_t *pList ) noexcept;

// Reports whether the node currently belongs to an intrusive list.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t IntrusiveList_IsLinked( const intrusive_list_node_t *pNode ) noexcept;

CYPHER_COMMON_API void IntrusiveList_PushFront(
    intrusive_list_t *pList,
    intrusive_list_node_t *pNode ) noexcept;

CYPHER_COMMON_API void IntrusiveList_PushBack(
    intrusive_list_t *pList,
    intrusive_list_node_t *pNode ) noexcept;

CYPHER_COMMON_API void IntrusiveList_InsertBefore(
    intrusive_list_t *pList,
    intrusive_list_node_t *pPosition,
    intrusive_list_node_t *pNode ) noexcept;

CYPHER_COMMON_API void IntrusiveList_Remove(
    intrusive_list_t *pList,
    intrusive_list_node_t *pNode ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
intrusive_list_node_t *IntrusiveList_Front( intrusive_list_t *pList ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const intrusive_list_node_t *IntrusiveList_Front(
    const intrusive_list_t *pList ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
intrusive_list_node_t *IntrusiveList_Back( intrusive_list_t *pList ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const intrusive_list_node_t *IntrusiveList_Back(
    const intrusive_list_t *pList ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
intrusive_list_node_t *IntrusiveList_Next(
    const intrusive_list_t *pList,
    intrusive_list_node_t *pNode ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const intrusive_list_node_t *IntrusiveList_Next(
    const intrusive_list_t *pList,
    const intrusive_list_node_t *pNode ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
intrusive_list_node_t *IntrusiveList_Previous(
    const intrusive_list_t *pList,
    intrusive_list_node_t *pNode ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const intrusive_list_node_t *IntrusiveList_Previous(
    const intrusive_list_t *pList,
    const intrusive_list_node_t *pNode ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize IntrusiveList_Count( const intrusive_list_t *pList ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t IntrusiveList_IsEmpty( const intrusive_list_t *pList ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_INTRUSIVELIST_H
