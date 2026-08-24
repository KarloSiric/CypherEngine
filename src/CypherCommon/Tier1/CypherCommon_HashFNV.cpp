//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HashFNV.cpp
//  Purpose: Implements deterministic FNV-1a hashing.
//  Details: These routines preserve the standard FNV offset bases and primes so
//           identifiers remain stable across platforms, builds, and chunk sizes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Hash FNV Implementation Notes

This algorithm is deterministic over an explicit byte range. It is suitable for lookup, change
detection, or corruption checks, but must not be used as a cryptographic authenticator.
================
*/

#include "CypherCommon_HashFNV.h"

namespace cypher::common
{

hash32_t HashFNV1a32_Update( hash32_t state, binary_block_t data ) noexcept
{
    const bool_t bValidData = BinaryBlock_IsValid( data );
    CY_ASSERT_MSG( bValidData, "FNV-1a hashing requires a valid binary block." );
    if ( !bValidData ) {
        return state;
    }

    // FNV-1a xors the next byte before multiplying by the fixed prime.
    for ( usize iByte = 0u; iByte < data.cbSize; ++iByte ) {
        state ^= static_cast<hash32_t>( data.pData[iByte] );
        state *= CY_FNV1A32_PRIME;
    }
    return state;
}

hash64_t HashFNV1a64_Update( hash64_t state, binary_block_t data ) noexcept
{
    const bool_t bValidData = BinaryBlock_IsValid( data );
    CY_ASSERT_MSG( bValidData, "FNV-1a hashing requires a valid binary block." );
    if ( !bValidData ) {
        return state;
    }

    for ( usize iByte = 0u; iByte < data.cbSize; ++iByte ) {
        state ^= static_cast<hash64_t>( data.pData[iByte] );
        state *= CY_FNV1A64_PRIME;
    }
    return state;
}

hash32_t HashFNV1a32_Data( binary_block_t data ) noexcept
{
    return HashFNV1a32_Update( CY_FNV1A32_OFFSET, data );
}

hash64_t HashFNV1a64_Data( binary_block_t data ) noexcept
{
    return HashFNV1a64_Update( CY_FNV1A64_OFFSET, data );
}

hash32_t HashFNV1a32_String( string_view_t text ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG( bValidText, "FNV-1a string hashing requires a valid string view." );
    if ( !bValidText ) {
        return CY_FNV1A32_OFFSET;
    }

    return HashFNV1a32_Data(
        BinaryBlock_FromData( text.pData, text.cchLength ) );
}

hash64_t HashFNV1a64_String( string_view_t text ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG( bValidText, "FNV-1a string hashing requires a valid string view." );
    if ( !bValidText ) {
        return CY_FNV1A64_OFFSET;
    }

    return HashFNV1a64_Data(
        BinaryBlock_FromData( text.pData, text.cchLength ) );
}

} // namespace cypher::common
