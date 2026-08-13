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
    CLOSED = 0u,
    LOADING,
    READY,
    SAVING,
    CONFLICTED,
    FAILED
};

enum tool_document_flags_t : flags32_t {
    TOOL_DOCUMENT_FLAG_NONE = 0u,
    TOOL_DOCUMENT_FLAG_DIRTY = CYPHER_BIT32( 0 ),
    TOOL_DOCUMENT_FLAG_READ_ONLY = CYPHER_BIT32( 1 ),
    TOOL_DOCUMENT_FLAG_UNTITLED = CYPHER_BIT32( 2 ),
    TOOL_DOCUMENT_FLAG_EXTERNAL_CHANGE = CYPHER_BIT32( 3 )
};

struct tool_document_desc_t {
    unique_id_t id{};
    string_view_t typeId{};
    string_view_t path{};
    string_view_t displayName{};
    tool_document_state_t state{ tool_document_state_t::CLOSED };
    flags32_t flags{ TOOL_DOCUMENT_FLAG_NONE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolDocument_Validate(
    const tool_document_desc_t &document ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLDOCUMENT_H
