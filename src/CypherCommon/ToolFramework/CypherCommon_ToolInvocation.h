//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolInvocation.h
//  Purpose: Declares one resolved invocation of a Cypher tool command.
//  Details: Invocation data is frontend-independent and can be constructed from
//           argv, Mason controls, automation requests, or test fixtures.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLINVOCATION_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLINVOCATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolApplication.h"
#include "CypherCommon_ToolCommand.h"
#include "CypherCommon_ToolContext.h"
#include "CypherCommon_ToolHost.h"
#include "CypherCommon_ToolOptionSet.h"
#include "CypherCommon_ToolOutput.h"

namespace cypher::common
{

enum tool_invocation_flags_t : flags32_t {
    TOOL_INVOCATION_FLAG_NONE = 0u,
    TOOL_INVOCATION_FLAG_DRY_RUN = CYPHER_BIT32( 0 ),
    TOOL_INVOCATION_FLAG_FORCE_ROOTS = CYPHER_BIT32( 1 ),
    TOOL_INVOCATION_FLAG_FORCE_CLOSURE = CYPHER_BIT32( 2 ),
    TOOL_INVOCATION_FLAG_KEEP_GOING = CYPHER_BIT32( 3 ),
    TOOL_INVOCATION_FLAG_NO_CACHE = CYPHER_BIT32( 4 )
};

struct tool_invocation_t {
    const tool_application_desc_t *pApplication{ nullptr };
    const tool_command_desc_t *pCommand{ nullptr };
    const tool_context_t *pContext{ nullptr };
    const tool_option_set_t *pOptions{ nullptr };
    const string_view_t *pInputs{ nullptr };
    usize nInputs{ 0u };
    const tool_host_t *pHost{ nullptr };
    tool_output_policy_t output{};
    flags32_t flags{ TOOL_INVOCATION_FLAG_NONE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolInvocation_Validate(
    const tool_invocation_t &invocation ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLINVOCATION_H
