//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_IntrusiveList.h
//  Purpose: Declares CypherCommon Tier1 IntrusiveList support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_INTRUSIVELIST_H
#define CYPHER_COMMON_TIER1_INTRUSIVELIST_H
#pragma once

/*
================
CypherCommon Intrusive List

Intrusive list declarations.
================
*/

namespace cypher::common
{

struct intrusive_list_node_t {
    intrusive_list_node_t *pPrev;
    intrusive_list_node_t *pNext;
};

struct intrusive_list_t;

void IntrusiveList_Init( intrusive_list_t *pList );
void IntrusiveList_PushBack( intrusive_list_t *pList, intrusive_list_node_t *pNode );
void IntrusiveList_Remove( intrusive_list_t *pList, intrusive_list_node_t *pNode );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_INTRUSIVELIST_H
