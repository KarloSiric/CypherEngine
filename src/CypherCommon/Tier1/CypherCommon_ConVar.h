//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ConVar.h
//  Purpose: Declares CypherCommon Tier1 ConVar support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
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

/*
================
CypherCommon ConVar

Console variable declarations used for runtime tuning, renderer flags, tools,
debugging and persistent config values.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum class convar_type_t : u8 {
    Bool = 0u,
    I32,
    U32,
    F32,
    String
};

enum convar_flags_t : flags32_t {
    CONVAR_FLAG_NONE = 0u,
    CONVAR_FLAG_ARCHIVE = CYPHER_BIT32( 0 ),
    CONVAR_FLAG_READONLY = CYPHER_BIT32( 1 ),
    CONVAR_FLAG_CHEAT = CYPHER_BIT32( 2 ),
    CONVAR_FLAG_RENDERER = CYPHER_BIT32( 3 ),
    CONVAR_FLAG_TOOL = CYPHER_BIT32( 4 )
};

struct convar_value_t {
    convar_type_t type;
    union {
        bool_t boolValue;
        i32 i32Value;
        u32 u32Value;
        f32 f32Value;
        const char *pStringValue;
    };
};

using convar_changed_fn_t = void ( * )( const char *pName, const convar_value_t &oldValue, const convar_value_t &newValue, void *pUserData );

struct convar_desc_t {
    const char *pName;
    const char *pHelpText;
    convar_value_t defaultValue;
    convar_value_t minValue;
    convar_value_t maxValue;
    flags32_t flags;
    convar_changed_fn_t pChangedFn;
    void *pUserData;
};

bool_t ConVar_IsValidName( const char *pName );
bool_t ConVar_ValueFromString( convar_type_t type, const char *pText, convar_value_t *pOutValue );
bool_t ConVar_ValueToString( const convar_value_t &value, char *pDest, usize cchDest );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CONVAR_H
