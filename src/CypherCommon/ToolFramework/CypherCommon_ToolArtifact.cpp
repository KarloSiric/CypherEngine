//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolArtifact.cpp
//  Purpose: Implements validation and names for tool artifact records.
//  Details: Temporary artifacts may omit a hash while published non-empty output
//           records require stable content identity for reports and caches.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ToolArtifact.h"

namespace cypher::common
{

tool_status_t ToolArtifact_Validate( const tool_artifact_t &artifact ) noexcept
{
    constexpr flags32_t knownFlags =
        TOOL_ARTIFACT_FLAG_PRIMARY |
        TOOL_ARTIFACT_FLAG_GENERATED |
        TOOL_ARTIFACT_FLAG_TEMPORARY |
        TOOL_ARTIFACT_FLAG_CACHE_HIT;

    if ( !StringView_IsValid( artifact.path ) ||
         artifact.path.cchLength == 0u ||
         !StringView_IsValid( artifact.mediaType ) ||
         artifact.kind > tool_artifact_kind_t::OTHER ||
         ( artifact.flags & ~knownFlags ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    if ( artifact.cbSize != 0u &&
         ( artifact.flags & TOOL_ARTIFACT_FLAG_TEMPORARY ) == 0u &&
         !ContentHash_IsValid( artifact.contentHash ) ) {
        return tool_status_t::INVALID_ARGUMENT;
    }
    return tool_status_t::OK;
}

const char *ToolArtifact_KindName( tool_artifact_kind_t kind ) noexcept
{
    switch ( kind ) {
        case tool_artifact_kind_t::COOKED_RESOURCE: return "cooked-resource";
        case tool_artifact_kind_t::PACKAGE: return "package";
        case tool_artifact_kind_t::REPORT: return "report";
        case tool_artifact_kind_t::DEPENDENCY_FILE: return "dependency-file";
        case tool_artifact_kind_t::LOG: return "log";
        case tool_artifact_kind_t::TRACE: return "trace";
        case tool_artifact_kind_t::CACHE_ENTRY: return "cache-entry";
        case tool_artifact_kind_t::OTHER: return "other";
    }
    return "unknown";
}

} // namespace cypher::common
