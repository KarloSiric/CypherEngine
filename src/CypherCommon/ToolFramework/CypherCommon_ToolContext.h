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
    TOOL_CONTEXT_FLAG_NONE = 0u,                     // No optional host policy.
    TOOL_CONTEXT_FLAG_INTERACTIVE = CYPHER_BIT32( 0 ), // Human is supervising work.
    TOOL_CONTEXT_FLAG_AUTOMATION = CYPHER_BIT32( 1 ),  // Invocation runs under CI/build automation.
    TOOL_CONTEXT_FLAG_OFFLINE = CYPHER_BIT32( 2 ),     // Network access is forbidden.
    TOOL_CONTEXT_FLAG_REPRODUCIBLE = CYPHER_BIT32( 3 ) // Output must avoid host-specific variation.
};

struct tool_context_t {
    string_view_t applicationId{};      // Stable ID of the invoking tool.
    string_view_t projectFile{};        // Canonical project manifest path, if any.
    string_view_t workingDirectory{};   // Host working directory at invocation.
    string_view_t sourceRoot{};         // Authored-resource root identity.
    string_view_t outputRoot{};         // Cooked-artifact publication root.
    string_view_t cacheRoot{};          // Intermediate/cache storage root.
    // Borrowed source view supplied by the host. Compilers resolve authored
    // resource identities through this boundary instead of native path joins.
    const vfs_t *pSourceVfs{ nullptr }; // Optional borrowed authored-resource VFS.
    tool_target_t target{};             // Platform/architecture being cooked.
    tool_profile_t profile{ tool_profile_t::UNKNOWN }; // Development/release policy.
    u32 nWorkerCount{ 0u };             // Maximum host-approved worker count.
    flags32_t flags{ TOOL_CONTEXT_FLAG_NONE }; // tool_context_flags_t bitset.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolContext_Validate( const tool_context_t &context ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLCONTEXT_H
