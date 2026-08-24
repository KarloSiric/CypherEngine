//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolInputSet.h
//  Purpose: Declares input roots and filters shared by Cypher tools.
//  Details: Input sets describe files, directories, patterns, and manifests
//           without performing filesystem traversal. CLI and Mason hosts resolve
//           these descriptions through their normal filesystem services.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLINPUTSET_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLINPUTSET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_PathMatch.h"

namespace cypher::common
{

enum class tool_input_kind_t : u8 {
    FILE = 0u, // One explicit source file.
    DIRECTORY, // Directory root resolved by the host.
    PATTERN,   // Wildcard or matcher expression.
    MANIFEST   // File containing another declared input list.
};

enum tool_input_flags_t : flags32_t {
    TOOL_INPUT_FLAG_NONE = 0u,                       // No optional traversal policy.
    TOOL_INPUT_FLAG_REQUIRED = CYPHER_BIT32( 0 ),    // Missing input is an error.
    TOOL_INPUT_FLAG_RECURSIVE = CYPHER_BIT32( 1 ),   // Descend below directory/pattern root.
    TOOL_INPUT_FLAG_FOLLOW_SYMLINKS = CYPHER_BIT32( 2 ), // Directory traversal follows links.
    TOOL_INPUT_FLAG_ALLOW_MISSING = CYPHER_BIT32( 3 )    // Missing input is accepted.
};

struct tool_input_t {
    string_view_t value{};         // Path, directory, pattern, or manifest text.
    string_view_t baseDirectory{}; // Optional resolution base.
    tool_input_kind_t kind{ tool_input_kind_t::FILE }; // Interpretation of value.
    flags32_t flags{ TOOL_INPUT_FLAG_NONE }; // tool_input_flags_t bitset.
};

struct tool_input_set_t {
    tool_input_t *pInputs{ nullptr }; // Caller-owned descriptor storage.
    usize nCount{ 0u };               // Active inputs in source order.
    usize nCapacity{ 0u };            // Maximum entries in pInputs.
    path_filter_t filter{};           // Borrowed include/exclude policy.
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolInput_Validate( const tool_input_t &input ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolInputSet_Init(
    tool_input_set_t *pSet,
    tool_input_t *pStorage,
    usize nCapacity ) noexcept;

CYPHER_COMMON_API void ToolInputSet_Clear( tool_input_set_t *pSet ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolInputSet_SetFilter(
    tool_input_set_t *pSet,
    const path_filter_t &filter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolInputSet_Add(
    tool_input_set_t *pSet,
    const tool_input_t &input ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const tool_input_t *ToolInputSet_At(
    const tool_input_set_t *pSet,
    usize iInput ) noexcept;

// Applies include/exclude policy to one already-resolved path.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolInputSet_AcceptsPath(
    const tool_input_set_t *pSet,
    string_view_t path ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolInput_KindName( tool_input_kind_t kind ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLINPUTSET_H
