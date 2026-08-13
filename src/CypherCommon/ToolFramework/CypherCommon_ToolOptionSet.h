//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolOptionSet.h
//  Purpose: Declares caller-owned storage for resolved tool option values.
//  Details: Resolution applies explicit source precedence without allocation;
//           callers own descriptor, value text, and record storage lifetimes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLOPTIONSET_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLOPTIONSET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolOption.h"

namespace cypher::common
{

struct tool_option_set_t {
    tool_option_value_t *pValues{ nullptr };
    usize nCount{ 0u };
    usize nCapacity{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolOptionSet_Init(
    tool_option_set_t *pSet,
    tool_option_value_t *pStorage,
    usize nCapacity ) noexcept;

CYPHER_COMMON_API void ToolOptionSet_Clear( tool_option_set_t *pSet ) noexcept;

// Resolves one value using source precedence. Repeatable options retain every
// value from the strongest source; scalar options retain one effective value.
CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolOptionSet_Resolve(
    tool_option_set_t *pSet,
    const tool_option_desc_t *pDescriptor,
    string_view_t value,
    tool_option_source_t source ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const tool_option_value_t *ToolOptionSet_Find(
    const tool_option_set_t *pSet,
    string_view_t name ) noexcept;

// Returns the number of effective values stored for a named option.
CYPHER_NODISCARD CYPHER_COMMON_API
usize ToolOptionSet_CountValues(
    const tool_option_set_t *pSet,
    string_view_t name ) noexcept;

// Returns one effective occurrence by zero-based occurrence index.
CYPHER_NODISCARD CYPHER_COMMON_API
const tool_option_value_t *ToolOptionSet_FindAt(
    const tool_option_set_t *pSet,
    string_view_t name,
    usize iOccurrence ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLOPTIONSET_H
