//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCancellation.cpp
//  Purpose: Implements cooperative tool cancellation queries.
//  Details: Atomic cancellation is checked first so a callback is avoided after
//           an already-published cancellation request.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Cancellation Implementation Notes

Cancellation is cooperative. Signal handlers request cancellation through async-safe state,
while normal tool code observes that state at well-defined interruption points.
================
*/

#include "CypherCommon_ToolCancellation.h"

namespace cypher::common
{

bool_t ToolCancellation_IsRequested(
    const tool_cancellation_t *pCancellation ) noexcept
{
    if ( pCancellation == nullptr ) {
        return CY_FALSE;
    }

    // The atomic path is signal-safe and cheap. Acquire pairs with the release
    // store used by the requesting thread before cancellation becomes visible.
    if ( pCancellation->pRequested != nullptr &&
         Cy_AtomicLoad(
             pCancellation->pRequested,
             CY_MEMORY_ORDER_ACQUIRE ) ) {
        return CY_TRUE;
    }

    // A host callback covers cancellation sources that are not represented by
    // the shared atomic, such as an editor job or remote build controller.
    return pCancellation->pfnQuery != nullptr
        ? pCancellation->pfnQuery( pCancellation->pUserData )
        : CY_FALSE;
}

} // namespace cypher::common
