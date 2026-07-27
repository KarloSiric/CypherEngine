//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_TsList.h
//  Purpose: Declares CypherCommon Tier0 TsList support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_TSLIST_H
#define CYPHER_COMMON_TIER0_TSLIST_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon TS List

Thread-safe intrusive single-list declarations.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"

#include <atomic>
#include <mutex>

namespace cypher::common
{

struct tslist_t;

struct tslist_node_t {
    tslist_node_t *pNext = nullptr;
    std::atomic<tslist_t *> pOwner{ nullptr };
};

struct tslist_t {
    mutable std::mutex nativeMutex;
    tslist_node_t *pHead = nullptr;
    usize nCount = 0u;
    bool_t isInitialized = CY_FALSE;
};

// Initializes an empty intrusive LIFO list.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_TsListInit( tslist_t *pList ) noexcept;

// Shuts down an empty list; live nodes must be popped first.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_TsListShutdown( tslist_t *pList ) noexcept;

// Pushes one unowned node onto the list.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_TsListPush(
    tslist_t *pList,
    tslist_node_t *pNode ) noexcept;

// Pops the most recently pushed node, or nullptr when empty/invalid.
[[nodiscard]] CYPHER_COMMON_API tslist_node_t *Cy_TsListPop(
    tslist_t *pList ) noexcept;

// Returns a consistent node-count snapshot.
[[nodiscard]] CYPHER_COMMON_API usize Cy_TsListGetCount(
    const tslist_t *pList ) noexcept;

// Returns whether the initialized list currently contains no nodes.
[[nodiscard]] CYPHER_COMMON_API bool_t Cy_TsListIsEmpty(
    const tslist_t *pList ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_TSLIST_H
