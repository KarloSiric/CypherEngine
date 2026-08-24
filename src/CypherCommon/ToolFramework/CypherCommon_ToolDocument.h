//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolDocument.h
//  Purpose: Declares editor-neutral document metadata shared by authoring tools.
//  Details: The contract describes identity and lifecycle state only; document
//           models, Qt views, undo storage, and domain editing remain product code.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Document Contract

Tool documents hold authored data and source identity independently of a GUI. Save and reload
operations preserve transactional error reporting.
================
*/

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLDOCUMENT_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLDOCUMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_UniqueId.h"

namespace cypher::common
{

enum class tool_document_state_t : u8 {
    CLOSED = 0u, // No authored payload is attached.
    LOADING,     // Source data is being decoded into the document model.
    READY,       // Document may be inspected or edited.
    SAVING,      // An authored representation is being published.
    CONFLICTED,  // The source changed externally and needs user resolution.
    FAILED       // The last lifecycle operation left the document unusable.
};

enum tool_document_flags_t : flags32_t {
    TOOL_DOCUMENT_FLAG_NONE = 0u,
    TOOL_DOCUMENT_FLAG_DIRTY = CYPHER_BIT32( 0 ),          // In-memory state differs from disk.
    TOOL_DOCUMENT_FLAG_READ_ONLY = CYPHER_BIT32( 1 ),      // Save must not replace the source.
    TOOL_DOCUMENT_FLAG_UNTITLED = CYPHER_BIT32( 2 ),       // No persistent source path exists yet.
    TOOL_DOCUMENT_FLAG_EXTERNAL_CHANGE = CYPHER_BIT32( 3 ) // A watcher observed a source update.
};

struct tool_document_desc_t {
    unique_id_t id{};                                     // Session-unique document identity.
    string_view_t typeId{};                               // Stable resource or editor document type.
    string_view_t path{};                                 // Borrowed normalized source path, if any.
    string_view_t displayName{};                          // Label used by tabs and recent-file lists.
    tool_document_state_t state{ tool_document_state_t::CLOSED }; // Current lifecycle state.
    flags32_t flags{ TOOL_DOCUMENT_FLAG_NONE };            // Combination of tool_document_flags_t.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolDocument_Validate(
    const tool_document_desc_t &document ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLDOCUMENT_H
