//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_LinkedList.h
//  Purpose: Declares CypherCommon Tier1 LinkedList support.
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

#ifndef CYPHER_COMMON_TIER1_LINKEDLIST_H
#define CYPHER_COMMON_TIER1_LINKEDLIST_H
#pragma once

/*
================
CypherCommon Linked List

Linked list declarations.
================
*/

namespace cypher::common
{

template <typename type_t>
struct linked_list_t;

template <typename type_t>
struct linked_list_node_t;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_LINKEDLIST_H
