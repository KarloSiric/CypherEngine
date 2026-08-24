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

/*
================
Tool Option Contract

Options are typed descriptors resolved through explicit precedence. Parsing, defaults,
environment values, response files, and command-line overrides remain distinguishable for
diagnostics.
================
*/

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
    BOOLEAN = 0u, // true/false value.
    I64,          // Signed 64-bit integer text.
    U64,          // Unsigned 64-bit integer text.
    F64,          // Finite double-precision value.
    STRING,       // Arbitrary validated text.
    PATH,         // Non-empty path text.
    ENUM          // One value from pEnumValues.
};

enum class tool_option_source_t : u8 {
    DEFAULT_VALUE = 0u, // Descriptor-provided fallback; weakest source.
    PROJECT,            // Project manifest value.
    PROFILE,            // Selected build-profile override.
    MACHINE,            // Host or environment override.
    COMMAND_LINE        // Explicit invocation value; strongest source.
};

enum tool_option_flags_t : flags32_t {
    TOOL_OPTION_FLAG_NONE = 0u,                       // No optional descriptor policy.
    TOOL_OPTION_FLAG_REQUIRED = CYPHER_BIT32( 0 ),    // Effective value must be present.
    TOOL_OPTION_FLAG_REPEATABLE = CYPHER_BIT32( 1 ),  // Strongest source may provide a list.
    TOOL_OPTION_FLAG_HIDDEN = CYPHER_BIT32( 2 ),      // Omit from normal generated help.
    TOOL_OPTION_FLAG_SEMANTIC = CYPHER_BIT32( 3 ),    // Value participates in output/cache identity.
    TOOL_OPTION_FLAG_ALLOW_EMPTY = CYPHER_BIT32( 4 )  // Empty string is a valid explicit value.
};

struct tool_option_desc_t {
    string_view_t name{};      // Stable long option name without leading dashes.
    char shortName{ '\0' };    // Optional one-character alias.
    tool_option_type_t type{ tool_option_type_t::STRING }; // Typed parser policy.
    string_view_t valueName{}; // Placeholder used by generated help.
    string_view_t summary{};   // One-line option description.
    string_view_t defaultValue{}; // Optional validated fallback text.
    const string_view_t *pEnumValues{ nullptr }; // Borrowed allowed values for ENUM.
    usize nEnumValues{ 0u };   // Number of entries above.
    flags32_t flags{ TOOL_OPTION_FLAG_NONE }; // tool_option_flags_t bitset.
};

struct tool_option_value_t {
    const tool_option_desc_t *pDescriptor{ nullptr }; // Borrowed defining descriptor.
    string_view_t value{};     // Borrowed validated text representation.
    tool_option_source_t source{ tool_option_source_t::DEFAULT_VALUE }; // Winning layer.
    u32 nOccurrence{ 0u };     // One-based order for repeatable options.
    bool_t bPresent{ CY_FALSE }; // Distinguishes absent from an allowed empty value.
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
