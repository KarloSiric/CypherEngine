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

#include "CypherCommon_BaseTypes.h"

#include <mutex>

namespace cypher::common
{

struct tslist_node_t {
    tslist_node_t *pNext;
};

struct tslist_t {
    std::mutex nativeMutex;
    tslist_node_t *pHead;
};

void TsList_Init( tslist_t *pList );
void TsList_Push( tslist_t *pList, tslist_node_t *pNode );
tslist_node_t *TsList_Pop( tslist_t *pList );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_TSLIST_H
