//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_TsList.cpp
//  Purpose: Implements CypherCommon Tier0 thread-safe intrusive list.
//  Details: This simple mutex-backed intrusive stack is useful for low-level
//           free lists before higher containers exist.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_TsList.h"

namespace cypher::common
{

void TsList_Init( tslist_t *pList )
{
    if ( pList == nullptr ) {
        return;
    }

    std::lock_guard<std::mutex> lock( pList->nativeMutex );
    pList->pHead = nullptr;
}

void TsList_Push( tslist_t *pList, tslist_node_t *pNode )
{
    if ( pList == nullptr || pNode == nullptr ) {
        return;
    }

    std::lock_guard<std::mutex> lock( pList->nativeMutex );
    pNode->pNext = pList->pHead;
    pList->pHead = pNode;
}

tslist_node_t *TsList_Pop( tslist_t *pList )
{
    if ( pList == nullptr ) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock( pList->nativeMutex );
    tslist_node_t *pNode = pList->pHead;
    if ( pNode != nullptr ) {
        pList->pHead = pNode->pNext;
        pNode->pNext = nullptr;
    }

    return pNode;
}

} // namespace cypher::common
