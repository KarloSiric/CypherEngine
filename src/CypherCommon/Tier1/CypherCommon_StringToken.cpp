//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringToken.cpp
//  Purpose: Implements compact deterministic string tokens.
//  Details: Tokens combine a fast 64-bit hash with byte length and reserve zero hash
//           for the invalid sentinel. Full text remains required for collision safety.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
String Token Implementation Notes

Text operations distinguish bounded byte ranges from null-terminated strings. Cursor movement
and conversion validate limits before reading, and failure never relies on ambient locale state.
================
*/

#include "CypherCommon_StringToken.h"

#include "CypherCommon_Hash.h"

namespace cypher::common
{

namespace
{

string_token_t StringToken_Make(
    string_view_t text,
    bool_t bCaseInsensitiveAscii ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidLength =
        !bValidText || text.cchLength <= CY_U32_MAX;
    CY_ASSERT_MSG(
        bValidText,
        "StringToken requires a valid string view." );
    CY_ASSERT_MSG(
        bValidLength,
        "StringToken cannot represent a string longer than u32." );
    if ( !bValidText || !bValidLength ) {
        return CY_STRING_TOKEN_INVALID;
    }

    hash64_t hash = bCaseInsensitiveAscii
        ? Hash64_StringInsensitiveAscii( text )
        : Hash64_String( text );
    // Zero is reserved for the invalid token, so remap the one colliding digest.
    if ( hash == 0u ) {
        hash = 1u;
    }
    return { hash, static_cast<u32>( text.cchLength ) };
}

} // namespace

string_token_t StringToken_FromView( string_view_t text ) noexcept
{
    return StringToken_Make( text, CY_FALSE );
}

string_token_t StringToken_FromViewInsensitiveAscii(
    string_view_t text ) noexcept
{
    return StringToken_Make( text, CY_TRUE );
}

bool_t StringToken_IsValid( string_token_t token ) noexcept
{
    return token.hash != 0u;
}

bool_t StringToken_Equals(
    string_token_t left,
    string_token_t right ) noexcept
{
    // Tokens are fast candidates, not collision-proof text identity. Callers
    // requiring adversarial equality must compare the original strings as well.
    return left.hash == right.hash &&
           left.cchLength == right.cchLength;
}

} // namespace cypher::common
