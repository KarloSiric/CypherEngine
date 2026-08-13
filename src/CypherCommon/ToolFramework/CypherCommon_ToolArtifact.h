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
    COOKED_RESOURCE = 0u,
    PACKAGE,
    REPORT,
    DEPENDENCY_FILE,
    LOG,
    TRACE,
    CACHE_ENTRY,
    OTHER
};

enum tool_artifact_flags_t : flags32_t {
    TOOL_ARTIFACT_FLAG_NONE = 0u,
    TOOL_ARTIFACT_FLAG_PRIMARY = CYPHER_BIT32( 0 ),
    TOOL_ARTIFACT_FLAG_GENERATED = CYPHER_BIT32( 1 ),
    TOOL_ARTIFACT_FLAG_TEMPORARY = CYPHER_BIT32( 2 ),
    TOOL_ARTIFACT_FLAG_CACHE_HIT = CYPHER_BIT32( 3 )
};

struct tool_artifact_t {
    string_view_t path{};
    string_view_t mediaType{};
    tool_artifact_kind_t kind{ tool_artifact_kind_t::OTHER };
    content_hash_t contentHash{};
    u64 cbSize{ 0u };
    flags32_t flags{ TOOL_ARTIFACT_FLAG_NONE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolArtifact_Validate( const tool_artifact_t &artifact ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolArtifact_KindName( tool_artifact_kind_t kind ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLARTIFACT_H
