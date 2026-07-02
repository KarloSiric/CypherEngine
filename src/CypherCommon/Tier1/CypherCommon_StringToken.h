//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringToken.h
//  Purpose: Declares CypherCommon Tier1 StringToken support.
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

#ifndef CYPHER_COMMON_TIER1_STRINGTOKEN_H
#define CYPHER_COMMON_TIER1_STRINGTOKEN_H
#pragma once

/*
================
CypherCommon String Token

Stable hashed string token declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct string_token_t {
    u32 hash;
};

string_token_t StringToken_FromString( const char *pString );
bool_t StringToken_IsValid( string_token_t token );
bool_t StringToken_Equals( string_token_t a, string_token_t b );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGTOKEN_H
