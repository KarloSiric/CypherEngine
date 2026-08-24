//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolArtifact.h
//  Purpose: Declares artifacts produced or consumed by Cypher tools.
//  Details: Artifacts identify generated files, reports, packages, logs, and cache
//           objects without imposing a filesystem writer or graphical document.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Artifact Contract

Artifacts are published transactionally and recorded with their type and path. Failed work must
not replace the last known-good output with a partial file.
================
*/

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLARTIFACT_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLARTIFACT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_ContentHash.h"

namespace cypher::common
{

enum class tool_artifact_kind_t : u8 {
    COOKED_RESOURCE = 0u, // Runtime-ready compiled resource.
    PACKAGE,              // Archive containing one or more resources.
    REPORT,               // Human- or machine-readable operation report.
    DEPENDENCY_FILE,      // Build-system dependency manifest.
    LOG,                  // Persistent textual operation log.
    TRACE,                // Profiling or execution trace.
    CACHE_ENTRY,          // Reusable intermediate or final cache object.
    OTHER                 // Tool-defined artifact without a shared category.
};

enum tool_artifact_flags_t : flags32_t {
    TOOL_ARTIFACT_FLAG_NONE = 0u,                    // No optional artifact policy.
    TOOL_ARTIFACT_FLAG_PRIMARY = CYPHER_BIT32( 0 ),  // Main result of the operation.
    TOOL_ARTIFACT_FLAG_GENERATED = CYPHER_BIT32( 1 ),// Produced rather than copied.
    TOOL_ARTIFACT_FLAG_TEMPORARY = CYPHER_BIT32( 2 ),// Not a durable published output.
    TOOL_ARTIFACT_FLAG_CACHE_HIT = CYPHER_BIT32( 3 ) // Reused from cache, not rebuilt.
};

struct tool_artifact_t {
    string_view_t path{};      // Canonical output identity or native report path.
    string_view_t mediaType{}; // Optional MIME-like content description.
    tool_artifact_kind_t kind{ tool_artifact_kind_t::OTHER }; // Shared category.
    content_hash_t contentHash{}; // Hash of completed bytes, when available.
    u64 cbSize{ 0u };          // Published byte size.
    flags32_t flags{ TOOL_ARTIFACT_FLAG_NONE }; // tool_artifact_flags_t bitset.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolArtifact_Validate( const tool_artifact_t &artifact ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolArtifact_KindName( tool_artifact_kind_t kind ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLARTIFACT_H
