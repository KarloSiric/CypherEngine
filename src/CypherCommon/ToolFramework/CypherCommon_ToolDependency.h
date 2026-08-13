//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolDependency.h
//  Purpose: Declares input and dependency records produced by tool operations.
//  Details: Dependency metadata supports rebuild explanations, reverse references,
//           cache identity, reports, and source navigation across all asset tools.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLDEPENDENCY_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLDEPENDENCY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_ContentHash.h"

namespace cypher::common
{

enum class tool_dependency_kind_t : u8 {
    SOURCE = 0u,
    RESOURCE,
    CONFIGURATION,
    TOOLCHAIN,
    GENERATED
};

enum tool_dependency_flags_t : flags32_t {
    TOOL_DEPENDENCY_FLAG_NONE = 0u,
    TOOL_DEPENDENCY_FLAG_REQUIRED = CYPHER_BIT32( 0 ),
    TOOL_DEPENDENCY_FLAG_OPTIONAL = CYPHER_BIT32( 1 ),
    TOOL_DEPENDENCY_FLAG_TRANSITIVE = CYPHER_BIT32( 2 ),
    TOOL_DEPENDENCY_FLAG_MISSING = CYPHER_BIT32( 3 )
};

struct tool_dependency_t {
    string_view_t path{};
    tool_dependency_kind_t kind{ tool_dependency_kind_t::SOURCE };
    content_hash_t contentHash{};
    flags32_t flags{ TOOL_DEPENDENCY_FLAG_NONE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolDependency_Validate(
    const tool_dependency_t &dependency ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolDependency_KindName(
    tool_dependency_kind_t kind ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLDEPENDENCY_H
