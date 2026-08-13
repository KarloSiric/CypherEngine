//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolContext.h
//  Purpose: Declares immutable context shared by one tool invocation.
//  Details: Context paths are borrowed views resolved by the application host.
//           Semantic build settings remain explicit for reproducible tool output.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLCONTEXT_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLCONTEXT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_ToolTarget.h"

namespace cypher::common
{

struct vfs_t;

enum tool_context_flags_t : flags32_t {
    TOOL_CONTEXT_FLAG_NONE = 0u,
    TOOL_CONTEXT_FLAG_INTERACTIVE = CYPHER_BIT32( 0 ),
    TOOL_CONTEXT_FLAG_AUTOMATION = CYPHER_BIT32( 1 ),
    TOOL_CONTEXT_FLAG_OFFLINE = CYPHER_BIT32( 2 ),
    TOOL_CONTEXT_FLAG_REPRODUCIBLE = CYPHER_BIT32( 3 )
};

struct tool_context_t {
    string_view_t applicationId{};
    string_view_t projectFile{};
    string_view_t workingDirectory{};
    string_view_t sourceRoot{};
    string_view_t outputRoot{};
    string_view_t cacheRoot{};
    // Borrowed source view supplied by the host. Compilers resolve authored
    // resource identities through this boundary instead of native path joins.
    const vfs_t *pSourceVfs{ nullptr };
    tool_target_t target{};
    tool_profile_t profile{ tool_profile_t::UNKNOWN };
    u32 nWorkerCount{ 0u };
    flags32_t flags{ TOOL_CONTEXT_FLAG_NONE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolContext_Validate( const tool_context_t &context ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCONTEXT_H
