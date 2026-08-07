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

struct intrusive_list_node_t {
    intrusive_list_node_t *pPrevious{ nullptr };
    intrusive_list_node_t *pNext{ nullptr };
};

struct intrusive_list_t {
    intrusive_list_node_t root{};
    usize nCount{ 0u };
};

CYPHER_COMMON_API void IntrusiveList_Init( intrusive_list_t *pList ) noexcept;
CYPHER_COMMON_API void IntrusiveList_Clear( intrusive_list_t *pList ) noexcept;

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
intrusive_list_node_t *IntrusiveList_Next(
    const intrusive_list_t *pList,
    intrusive_list_node_t *pNode ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_INTRUSIVELIST_H
