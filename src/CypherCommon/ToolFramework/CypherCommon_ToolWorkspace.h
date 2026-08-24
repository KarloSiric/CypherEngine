//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolWorkspace.h
//  Purpose: Declares metadata for embeddable authoring workspaces.
//  Details: Mason and focused Qt launches discover capabilities through this
//           descriptor while each product retains ownership of concrete UI code.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Workspace Contract

Workspace context resolves project, source, output, target, and profile roots once per
invocation. Tool modules consume normalized paths rather than ambient working-directory
assumptions.
================
*/

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLWORKSPACE_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLWORKSPACE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum tool_workspace_flags_t : flags32_t {
    TOOL_WORKSPACE_FLAG_NONE = 0u,
    TOOL_WORKSPACE_FLAG_EMBEDDABLE = CYPHER_BIT32( 0 ), // May be hosted inside Mason.
    TOOL_WORKSPACE_FLAG_STANDALONE = CYPHER_BIT32( 1 ), // May run as its own application.
    TOOL_WORKSPACE_FLAG_REQUIRES_PROJECT = CYPHER_BIT32( 2 ), // Needs a resolved project context.
    TOOL_WORKSPACE_FLAG_REQUIRES_ENGINE = CYPHER_BIT32( 3 ), // Needs live engine services.
    TOOL_WORKSPACE_FLAG_SUPPORTS_MULTIPLE_DOCUMENTS = CYPHER_BIT32( 4 ) // More than one open document.
};

struct tool_workspace_desc_t {
    string_view_t id{};                              // Stable machine-facing workspace identifier.
    string_view_t displayName{};                     // Human-readable title shown by a host.
    string_view_t summary{};                         // Short capability description.
    const string_view_t *pDocumentTypes{ nullptr };  // Borrowed array of accepted type identifiers.
    usize nDocumentTypes{ 0u };                      // Number of entries in pDocumentTypes.
    u32 nApiVersion{ 0u };                           // Workspace contract version expected by the host.
    flags32_t flags{ TOOL_WORKSPACE_FLAG_NONE };     // Combination of tool_workspace_flags_t.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolWorkspace_CheckDescriptor(
    const tool_workspace_desc_t &workspace ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolWorkspace_SupportsDocumentType(
    const tool_workspace_desc_t &workspace,
    string_view_t typeId ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLWORKSPACE_H
