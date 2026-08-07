//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringToken.h
//  Purpose: Declares compact deterministic tokens derived from string bytes.
//  Details: Tokens accelerate stable lookup but are not collision-proof identities or
//           security hashes. Collision-sensitive systems must retain and compare text.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STRINGTOKEN_H
#define CYPHER_COMMON_TIER1_STRINGTOKEN_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

struct string_token_t {
    hash64_t hash{ 0u };
    u32 cchLength{ 0u };
};

constexpr string_token_t CY_STRING_TOKEN_INVALID{};

CYPHER_NODISCARD CYPHER_COMMON_API
string_token_t StringToken_FromView( string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
string_token_t StringToken_FromViewInsensitiveAscii( string_view_t text ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringToken_IsValid( string_token_t token ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringToken_Equals( string_token_t left, string_token_t right ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGTOKEN_H
