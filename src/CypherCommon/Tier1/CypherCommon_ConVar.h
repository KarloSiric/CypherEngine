//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ConVar.h
//  Purpose: Declares typed console-variable values and descriptors.
//  Details: Descriptors borrow static name/help/default text. Runtime string values
//           are owned by CommandSystem, not by convar_value_t.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_CONVAR_H
#define CYPHER_COMMON_TIER1_CONVAR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ConCommand.h"
#include "CypherCommon_StringParse.h"
#include "CypherCommon_Variant.h"

namespace cypher::common
{

enum class convar_type_t : u8 {
    BOOL = 0u, // Boolean true/false value.
    I64,       // Signed 64-bit integer.
    U64,       // Unsigned 64-bit integer.
    F64,       // Double-precision scalar.
    STRING     // Borrowed or registry-owned UTF-8 text.
};

enum convar_flags_t : flags32_t {
    CONVAR_FLAG_NONE          = 0u,                // No optional policy.
    CONVAR_FLAG_ARCHIVE       = CYPHER_BIT32( 0 ), // Persist in user configuration.
    CONVAR_FLAG_READ_ONLY     = CYPHER_BIT32( 1 ), // Reject runtime writes.
    CONVAR_FLAG_CHEAT         = CYPHER_BIT32( 2 ), // Requires cheat permission.
    CONVAR_FLAG_REPLICATED    = CYPHER_BIT32( 3 ), // Participates in network state.
    CONVAR_FLAG_DEVELOPMENT   = CYPHER_BIT32( 4 ), // Development contexts only.
    CONVAR_FLAG_HIDDEN        = CYPHER_BIT32( 5 ), // Omit from normal discovery.
    CONVAR_FLAG_NOTIFY        = CYPHER_BIT32( 6 ), // Report accepted value changes.
    CONVAR_FLAG_REMOTE_WRITE_ALLOWED = CYPHER_BIT32( 7 ) // Authorized peers may write.
};

constexpr flags32_t CONVAR_VALID_FLAGS =
    CONVAR_FLAG_ARCHIVE |
    CONVAR_FLAG_READ_ONLY |
    CONVAR_FLAG_CHEAT |
    CONVAR_FLAG_REPLICATED |
    CONVAR_FLAG_DEVELOPMENT |
    CONVAR_FLAG_HIDDEN |
    CONVAR_FLAG_NOTIFY |
    CONVAR_FLAG_REMOTE_WRITE_ALLOWED;

enum class convar_parse_status_t : u8 {
    OK = 0u,          // Text converted and satisfied optional bounds.
    INVALID_ARGUMENT, // Descriptor, input view, or output is invalid.
    INVALID_TYPE,     // Descriptor names an unsupported value type.
    EMBEDDED_NULL,    // Bounded string contains an interior null byte.
    INVALID_VALUE,    // Text cannot be converted to the declared type.
    BELOW_MINIMUM,    // Parsed scalar is below the inclusive lower bound.
    ABOVE_MAXIMUM     // Parsed scalar is above the inclusive upper bound.
};

struct convar_parse_result_t {
    convar_parse_status_t status{ convar_parse_status_t::INVALID_ARGUMENT }; // ConVar-level result.
    string_parse_result_t scalarResult{}; // Detailed numeric parse result when applicable.
};

struct convar_value_t {
    variant_t value{}; // Value whose variant type must match the descriptor.
};

using convar_changed_fn_t = void ( * )(
    string_view_t name,
    const convar_value_t &oldValue,
    const convar_value_t &newValue,
    void *pUserData ) noexcept;

struct convar_desc_t {
    string_view_t name{};                    // Unique console-visible variable name.
    string_view_t help{};                    // Human-readable purpose text.
    convar_type_t type{ convar_type_t::STRING }; // Declared storage and parse type.
    string_view_t defaultValue{};            // Required authored default text.
    string_view_t minValue{};                // Optional inclusive scalar minimum.
    string_view_t maxValue{};                // Optional inclusive scalar maximum.
    flags32_t flags{ CONVAR_FLAG_NONE };      // convar_flags_t policy bits.
    convar_changed_fn_t pfnChanged{ nullptr }; // Optional post-commit callback.
    void *pUserData{ nullptr };               // Opaque callback state.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConVar_IsValidName( string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConVar_ValidateDesc( const convar_desc_t &desc ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConVar_ParseSucceeded( convar_parse_result_t result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ConVar_ParseStatusName( convar_parse_status_t status ) noexcept;

// Parses one primitive value without applying descriptor bounds. Output remains
// unchanged on failure; string values borrow text.
CYPHER_NODISCARD CYPHER_COMMON_API
convar_parse_result_t ConVar_ParseValue(
    convar_type_t type,
    string_view_t text,
    convar_value_t *pValueOut ) noexcept;

// Parses one value and rejects values outside the descriptor's optional bounds.
CYPHER_NODISCARD CYPHER_COMMON_API
convar_parse_result_t ConVar_ParseValueForDesc(
    const convar_desc_t &desc,
    string_view_t text,
    convar_value_t *pValueOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConVar_ValueMatchesType(
    convar_type_t type,
    const convar_value_t &value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize ConVar_FormatValue(
    const convar_value_t &value,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CONVAR_H
