//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolWorkspace.cpp
//  Purpose: Implements validation and document support queries for workspaces.
//  Details: Descriptor validation rejects duplicate document types and workspaces
//           that cannot be launched either embedded or standalone.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Workspace Implementation Notes

Workspace context resolves project, source, output, target, and profile roots once per
invocation. Tool modules consume normalized paths rather than ambient working-directory
assumptions.
================
*/

#include "CypherCommon_ToolWorkspace.h"

namespace cypher::common
{

tool_status_t ToolWorkspace_CheckDescriptor(
    const tool_workspace_desc_t &workspace ) noexcept
{
    // Unknown flags are rejected so hosts never expose capabilities that an
    // older workspace implementation does not actually support.
    constexpr flags32_t knownFlags =
        TOOL_WORKSPACE_FLAG_EMBEDDABLE |
        TOOL_WORKSPACE_FLAG_STANDALONE |
        TOOL_WORKSPACE_FLAG_REQUIRES_PROJECT |
        TOOL_WORKSPACE_FLAG_REQUIRES_ENGINE |
        TOOL_WORKSPACE_FLAG_SUPPORTS_MULTIPLE_DOCUMENTS;

    if ( !StringView_IsValid( workspace.id ) || workspace.id.cchLength == 0u ||
         !StringView_IsValid( workspace.displayName ) ||
         workspace.displayName.cchLength == 0u ||
         !StringView_IsValid( workspace.summary ) ||
         workspace.nApiVersion == 0u ||
         ( workspace.nDocumentTypes != 0u &&
           workspace.pDocumentTypes == nullptr ) ||
         ( workspace.flags & ~knownFlags ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    // A descriptor must advertise at least one valid launch mode.
    if ( ( workspace.flags &
           ( TOOL_WORKSPACE_FLAG_EMBEDDABLE |
             TOOL_WORKSPACE_FLAG_STANDALONE ) ) == 0u ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }

    // Descriptor tables are deliberately small, so an allocation-free O(n^2)
    // duplicate check is preferable to temporary hashing and ownership rules.
    for ( usize i = 0u; i < workspace.nDocumentTypes; ++i ) {
        if ( !StringView_IsValid( workspace.pDocumentTypes[i] ) ||
             workspace.pDocumentTypes[i].cchLength == 0u ) {
            return tool_status_t::INVALID_CONFIGURATION;
        }
        for ( usize j = 0u; j < i; ++j ) {
            if ( StringView_Equals(
                     workspace.pDocumentTypes[i],
                     workspace.pDocumentTypes[j] ) ) {
                return tool_status_t::INVALID_CONFIGURATION;
            }
        }
    }
    return tool_status_t::OK;
}

bool_t ToolWorkspace_SupportsDocumentType(
    const tool_workspace_desc_t &workspace,
    string_view_t typeId ) noexcept
{
    if ( !StringView_IsValid( typeId ) ||
         ( workspace.nDocumentTypes != 0u &&
           workspace.pDocumentTypes == nullptr ) ) {
        return CY_FALSE;
    }
    // Type identifiers use exact byte equality; aliases belong in registration.
    for ( usize i = 0u; i < workspace.nDocumentTypes; ++i ) {
        if ( StringView_Equals( workspace.pDocumentTypes[i], typeId ) ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

} // namespace cypher::common
