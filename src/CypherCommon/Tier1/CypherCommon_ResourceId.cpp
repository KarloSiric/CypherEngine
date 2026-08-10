//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ResourceId.cpp
//  Purpose: Implements deterministic resource type and path identifiers.
//  Details: Resource IDs use a versioned typed stable-hash stream. The asset database
//           must retain canonical paths and types so the theoretical collision case
//           can be detected rather than silently aliasing two resources.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ResourceId.h"

#include "CypherCommon_Char.h"
#include "CypherCommon_HashFNV.h"
#include "CypherCommon_StableHash.h"

namespace cypher::common
{

namespace
{

constexpr stable_hash_domain_t CY_RESOURCE_ID_HASH_DOMAIN =
    0x4359524553494431ull; // "CYRESID1"
constexpr u32 CY_RESOURCE_ID_HASH_SCHEMA = 1u;
constexpr char g_resourceIdHexDigits[] = "0123456789abcdef";

} // namespace

resource_type_id_t ResourceTypeId_FromName( string_view_t typeName ) noexcept
{
    const bool_t bValidName =
        StringView_IsValid( typeName ) && typeName.cchLength > 0u;
    if ( !bValidName ) {
        return 0u;
    }

    hash32_t hash = CY_FNV1A32_OFFSET;
    for ( usize iChar = 0u; iChar < typeName.cchLength; ++iChar ) {
        const char ch = typeName.pData[iChar];
        if ( !Char_IsAscii( ch ) || Char_IsControlAscii( ch ) ||
             Char_IsWhitespaceAscii( ch ) ) {
            return 0u;
        }
        const byte canonical = static_cast<byte>( Char_ToLowerAscii( ch ) );
        hash ^= canonical;
        hash *= CY_FNV1A32_PRIME;
    }
    return hash != 0u ? hash : 1u;
}

resource_id_t ResourceId_FromPath(
    string_view_t normalizedVirtualPath,
    resource_type_id_t type ) noexcept
{
    const bool_t bValidPath =
        StringView_IsValid( normalizedVirtualPath ) &&
        normalizedVirtualPath.cchLength > 0u;
    if ( !bValidPath || type == 0u ) {
        return CY_RESOURCE_ID_INVALID;
    }

    stable_hash_builder_t builder{};
    hash64_t hash = 0u;
    if ( !StableHash_Begin(
             &builder,
             CY_RESOURCE_ID_HASH_DOMAIN,
             CY_RESOURCE_ID_HASH_SCHEMA ) ||
         !StableHash_WriteU32( &builder, type ) ||
         !StableHash_WriteString( &builder, normalizedVirtualPath ) ||
         !StableHash_End( &builder, &hash ) ) {
        return CY_RESOURCE_ID_INVALID;
    }

    // Zero is the invalid sentinel. Remapping this one theoretical digest does not
    // remove the asset database's obligation to verify canonical collision keys.
    return { hash != 0u ? hash : 1u };
}

bool_t ResourceId_IsValid( resource_id_t id ) noexcept
{
    return id.value != 0u;
}

bool_t ResourceId_Equals( resource_id_t left, resource_id_t right ) noexcept
{
    return left.value == right.value;
}

usize ResourceId_ToString(
    resource_id_t id,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidDest =
        pDest != nullptr && cchDest >= CY_RESOURCE_ID_STRING_CAPACITY;
    CY_ASSERT_MSG(
        bValidDest,
        "ResourceId_ToString requires storage for 16 digits and a terminator." );
    if ( !bValidDest ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return 0u;
    }

    for ( usize iDigit = 0u; iDigit < CY_RESOURCE_ID_STRING_LENGTH; ++iDigit ) {
        const u32 nShift = static_cast<u32>(
            ( CY_RESOURCE_ID_STRING_LENGTH - 1u - iDigit ) * 4u );
        pDest[iDigit] = g_resourceIdHexDigits[( id.value >> nShift ) & 0x0Fu];
    }
    pDest[CY_RESOURCE_ID_STRING_LENGTH] = '\0';
    return CY_RESOURCE_ID_STRING_LENGTH;
}

bool_t ResourceId_FromString(
    string_view_t text,
    resource_id_t *pIdOut ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidOutput = pIdOut != nullptr;
    CY_ASSERT_MSG( bValidText, "ResourceId_FromString requires a valid string view." );
    CY_ASSERT_MSG( bValidOutput, "ResourceId_FromString requires output storage." );
    if ( !bValidText || !bValidOutput ||
         text.cchLength != CY_RESOURCE_ID_STRING_LENGTH ) {
        return CY_FALSE;
    }

    resource_id_t parsed{};
    for ( usize iDigit = 0u; iDigit < text.cchLength; ++iDigit ) {
        const u8 nDigit = Char_HexValueAscii( text.pData[iDigit] );
        if ( nDigit == CY_CHAR_INVALID_DIGIT_VALUE ) {
            return CY_FALSE;
        }
        parsed.value = ( parsed.value << 4u ) | nDigit;
    }
    if ( !ResourceId_IsValid( parsed ) ) {
        return CY_FALSE;
    }

    *pIdOut = parsed;
    return CY_TRUE;
}

} // namespace cypher::common
