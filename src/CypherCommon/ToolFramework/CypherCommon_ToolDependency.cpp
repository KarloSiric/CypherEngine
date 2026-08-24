//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolDependency.cpp
//  Purpose: Implements validation and names for tool dependency records.
//  Details: Missing optional dependencies remain reportable while contradictory
//           required/optional policy is rejected as malformed metadata.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Dependency Implementation Notes

Dependencies record every source that can invalidate an output. Paths are normalized and
ordering remains deterministic for cache keys and reproducible reports.
================
*/

#include "CypherCommon_ToolDependency.h"

namespace cypher::common
{

tool_status_t ToolDependency_Validate(
    const tool_dependency_t &dependency ) noexcept
{
    // Dependency policy is a closed bit set because cache invalidation must not
    // guess at the meaning of flags introduced by another producer version.
    constexpr flags32_t knownFlags =
        TOOL_DEPENDENCY_FLAG_REQUIRED |
        TOOL_DEPENDENCY_FLAG_OPTIONAL |
        TOOL_DEPENDENCY_FLAG_TRANSITIVE |
        TOOL_DEPENDENCY_FLAG_MISSING;

    if ( !StringView_IsValid( dependency.path ) ||
         dependency.path.cchLength == 0u ||
         dependency.kind > tool_dependency_kind_t::GENERATED ||
         ( dependency.flags & ~knownFlags ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    // Required and optional are mutually exclusive publication policies.
    if ( ( dependency.flags & TOOL_DEPENDENCY_FLAG_REQUIRED ) != 0u &&
         ( dependency.flags & TOOL_DEPENDENCY_FLAG_OPTIONAL ) != 0u ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }
    // Existing inputs require a content identity; a missing input has no bytes
    // to hash but remains recorded so its later creation invalidates the build.
    if ( ( dependency.flags & TOOL_DEPENDENCY_FLAG_MISSING ) == 0u &&
         !ContentHash_IsValid( dependency.contentHash ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    return tool_status_t::OK;
}

const char *ToolDependency_KindName(
    tool_dependency_kind_t kind ) noexcept
{
    // Keep serialized dependency names independent of C++ enum spelling.
    switch ( kind ) {
        case tool_dependency_kind_t::SOURCE: return "source";
        case tool_dependency_kind_t::RESOURCE: return "resource";
        case tool_dependency_kind_t::CONFIGURATION: return "configuration";
        case tool_dependency_kind_t::TOOLCHAIN: return "toolchain";
        case tool_dependency_kind_t::GENERATED: return "generated";
    }
    return "unknown";
}

} // namespace cypher::common
