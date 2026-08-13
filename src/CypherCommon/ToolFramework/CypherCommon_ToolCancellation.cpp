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

#include "CypherCommon_ToolCancellation.h"

namespace cypher::common
{

bool_t ToolCancellation_IsRequested(
    const tool_cancellation_t *pCancellation ) noexcept
{
    if ( pCancellation == nullptr ) {
        return CY_FALSE;
    }
    if ( pCancellation->pRequested != nullptr &&
         Cy_AtomicLoad(
             pCancellation->pRequested,
             CY_MEMORY_ORDER_ACQUIRE ) ) {
        return CY_TRUE;
    }
    return pCancellation->pfnQuery != nullptr
        ? pCancellation->pfnQuery( pCancellation->pUserData )
        : CY_FALSE;
}

} // namespace cypher::common
