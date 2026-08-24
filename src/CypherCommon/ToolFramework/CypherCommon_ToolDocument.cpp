//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolDocument.cpp
//  Purpose: Implements structural validation for shared tool document metadata.
//  Details: Untitled documents may omit a path; persisted documents require one.
//           Dirty read-only documents are rejected as contradictory host state.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Document Implementation Notes

Tool documents hold authored data and source identity independently of a GUI. Save and reload
operations preserve transactional error reporting.
================
*/

#include "CypherCommon_ToolDocument.h"

namespace cypher::common
{

tool_status_t ToolDocument_Validate(
    const tool_document_desc_t &document ) noexcept
{
    // Unknown lifecycle flags are rejected at the API boundary rather than
    // leaking unsupported state into save, reload, or conflict handling.
    constexpr flags32_t knownFlags =
        TOOL_DOCUMENT_FLAG_DIRTY |
        TOOL_DOCUMENT_FLAG_READ_ONLY |
        TOOL_DOCUMENT_FLAG_UNTITLED |
        TOOL_DOCUMENT_FLAG_EXTERNAL_CHANGE;

    if ( !UniqueId_IsValid( document.id ) ||
         !StringView_IsValid( document.typeId ) ||
         document.typeId.cchLength == 0u ||
         !StringView_IsValid( document.path ) ||
         !StringView_IsValid( document.displayName ) ||
         document.displayName.cchLength == 0u ||
         document.state > tool_document_state_t::FAILED ||
         ( document.flags & ~knownFlags ) != 0u ) {
        return tool_status_t::INVALID_ARGUMENT;
    }

    // Only a newly-created untitled document may exist without a source path.
    const bool_t bUntitled =
        ( document.flags & TOOL_DOCUMENT_FLAG_UNTITLED ) != 0u;
    if ( !bUntitled && document.path.cchLength == 0u ) {
        return tool_status_t::INVALID_CONFIGURATION;
    }
    // A read-only model cannot legally acquire unsaved authored changes.
    if ( ( document.flags & TOOL_DOCUMENT_FLAG_DIRTY ) != 0u &&
         ( document.flags & TOOL_DOCUMENT_FLAG_READ_ONLY ) != 0u ) {
        return tool_status_t::INVALID_STATE;
    }
    return tool_status_t::OK;
}

} // namespace cypher::common
