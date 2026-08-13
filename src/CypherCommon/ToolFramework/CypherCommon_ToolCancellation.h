//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolCancellation.h
//  Purpose: Declares cooperative cancellation shared by tool operations.
//  Details: An atomic flag supports direct cancellation while an optional host
//           callback supports GUI, signal, IPC, and test-controlled cancellation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLCANCELLATION_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLCANCELLATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Atomic.h"

namespace cypher::common
{

using tool_cancel_query_t = bool_t ( * )( void *pUserData ) noexcept;

struct tool_cancellation_t {
    const atomic_bool_t *pRequested{ nullptr };
    tool_cancel_query_t pfnQuery{ nullptr };
    void *pUserData{ nullptr };
};

// Cancellation is cooperative. Long operations must query at bounded intervals.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolCancellation_IsRequested(
    const tool_cancellation_t *pCancellation ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCANCELLATION_H
