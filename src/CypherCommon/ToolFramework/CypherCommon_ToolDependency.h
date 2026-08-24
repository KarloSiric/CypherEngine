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

/*
================
Tool Dependency Contract

Dependencies record every source that can invalidate an output. Paths are normalized and
ordering remains deterministic for cache keys and reproducible reports.
================
*/

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
    SOURCE = 0u,  // Authored source file consumed directly.
    RESOURCE,     // Another logical resource referenced by this build.
    CONFIGURATION,// Project, profile, or settings input.
    TOOLCHAIN,    // Compiler executable, library, or versioned rule set.
    GENERATED     // Intermediate output consumed as input.
};

enum tool_dependency_flags_t : flags32_t {
    TOOL_DEPENDENCY_FLAG_NONE = 0u,                    // No optional dependency policy.
    TOOL_DEPENDENCY_FLAG_REQUIRED = CYPHER_BIT32( 0 ), // Absence invalidates the build.
    TOOL_DEPENDENCY_FLAG_OPTIONAL = CYPHER_BIT32( 1 ), // Absence is permitted.
    TOOL_DEPENDENCY_FLAG_TRANSITIVE = CYPHER_BIT32( 2 ), // Discovered through another input.
    TOOL_DEPENDENCY_FLAG_MISSING = CYPHER_BIT32( 3 )   // Dependency was recorded but absent.
};

struct tool_dependency_t {
    string_view_t path{}; // Canonical dependency identity.
    tool_dependency_kind_t kind{ tool_dependency_kind_t::SOURCE }; // Shared category.
    content_hash_t contentHash{}; // Observed content identity, when available.
    flags32_t flags{ TOOL_DEPENDENCY_FLAG_NONE }; // tool_dependency_flags_t bitset.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolDependency_Validate(
    const tool_dependency_t &dependency ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolDependency_KindName(
    tool_dependency_kind_t kind ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLDEPENDENCY_H
