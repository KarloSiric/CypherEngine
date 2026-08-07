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

#include "CypherCommon_StringParse.h"
#include "CypherCommon_Variant.h"

namespace cypher::common
{

enum class convar_type_t : u8 {
    BOOL = 0u,
    I64,
    U64,
    F64,
    STRING
};

enum convar_flags_t : flags32_t {
    CONVAR_FLAG_NONE          = 0u,
    CONVAR_FLAG_ARCHIVE       = CYPHER_BIT32( 0 ),
    CONVAR_FLAG_READ_ONLY     = CYPHER_BIT32( 1 ),
    CONVAR_FLAG_CHEAT         = CYPHER_BIT32( 2 ),
    CONVAR_FLAG_REPLICATED    = CYPHER_BIT32( 3 ),
    CONVAR_FLAG_DEVELOPMENT   = CYPHER_BIT32( 4 ),
    CONVAR_FLAG_HIDDEN        = CYPHER_BIT32( 5 ),
    CONVAR_FLAG_NOTIFY        = CYPHER_BIT32( 6 )
};

struct convar_value_t {
    variant_t value{};
};

using convar_changed_fn_t = void ( * )(
    string_view_t name,
    const convar_value_t &oldValue,
    const convar_value_t &newValue,
    void *pUserData ) noexcept;

struct convar_desc_t {
    string_view_t name{};
    string_view_t help{};
    convar_type_t type{ convar_type_t::STRING };
    string_view_t defaultValue{};
    string_view_t minValue{};
    string_view_t maxValue{};
    flags32_t flags{ CONVAR_FLAG_NONE };
    convar_changed_fn_t pfnChanged{ nullptr };
    void *pUserData{ nullptr };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConVar_IsValidName( string_view_t name ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConVar_ValidateDesc( const convar_desc_t &desc ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ConVar_ParseValue(
    convar_type_t type,
    string_view_t text,
    convar_value_t *pValueOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize ConVar_FormatValue(
    const convar_value_t &value,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CONVAR_H
