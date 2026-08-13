//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolOption.h
//  Purpose: Declares typed option metadata shared by tools and their frontends.
//  Details: Descriptors drive CLI help, validation, Mason controls, reports, and
//           reproducible cache-key policy without embedding presentation objects.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLOPTION_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLOPTION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class tool_option_type_t : u8 {
    BOOLEAN = 0u,
    I64,
    U64,
    F64,
    STRING,
    PATH,
    ENUM
};

enum class tool_option_source_t : u8 {
    DEFAULT_VALUE = 0u,
    PROJECT,
    PROFILE,
    MACHINE,
    COMMAND_LINE
};

enum tool_option_flags_t : flags32_t {
    TOOL_OPTION_FLAG_NONE = 0u,
    TOOL_OPTION_FLAG_REQUIRED = CYPHER_BIT32( 0 ),
    TOOL_OPTION_FLAG_REPEATABLE = CYPHER_BIT32( 1 ),
    TOOL_OPTION_FLAG_HIDDEN = CYPHER_BIT32( 2 ),
    TOOL_OPTION_FLAG_SEMANTIC = CYPHER_BIT32( 3 ),
    TOOL_OPTION_FLAG_ALLOW_EMPTY = CYPHER_BIT32( 4 )
};

struct tool_option_desc_t {
    string_view_t name{};
    char shortName{ '\0' };
    tool_option_type_t type{ tool_option_type_t::STRING };
    string_view_t valueName{};
    string_view_t summary{};
    string_view_t defaultValue{};
    const string_view_t *pEnumValues{ nullptr };
    usize nEnumValues{ 0u };
    flags32_t flags{ TOOL_OPTION_FLAG_NONE };
};

struct tool_option_value_t {
    const tool_option_desc_t *pDescriptor{ nullptr };
    string_view_t value{};
    tool_option_source_t source{ tool_option_source_t::DEFAULT_VALUE };
    u32 nOccurrence{ 0u };
    bool_t bPresent{ CY_FALSE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolOption_CheckDescriptor(
    const tool_option_desc_t &desc ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolOption_ValidateValue(
    const tool_option_desc_t &desc,
    string_view_t value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ToolOption_SourceOverrides(
    tool_option_source_t incoming,
    tool_option_source_t current ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolOption_TypeName( tool_option_type_t type ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolOption_SourceName( tool_option_source_t source ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLOPTION_H
