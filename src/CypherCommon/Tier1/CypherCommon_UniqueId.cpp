//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_UniqueId.cpp
//  Purpose: Implements portable UUID creation, parsing, and formatting.
//  Details: Version-4 UUID bytes come directly from the operating-system CSPRNG.
//           Text conversion is bounded, allocation-free, and preserves output when
//           parsing fails.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_UniqueId.h"

#include "CypherCommon_Char.h"

#if CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    #include <bcrypt.h>
#elif CYPHER_PLATFORM_LINUX
    #include <cerrno>
    #include <sys/random.h>
#elif CYPHER_PLATFORM_MACOS
    #include <cstdlib>
#endif

namespace cypher::common
{

namespace
{

constexpr char g_uniqueIdHexDigits[] = "0123456789abcdef";

bool_t FillSecureRandom( void *pDest, usize cbDest ) noexcept
{
#if CYPHER_PLATFORM_WINDOWS
    if ( cbDest > static_cast<usize>( CY_U32_MAX ) ) {
        return CY_FALSE;
    }
    return BCryptGenRandom(
        nullptr,
        static_cast<PUCHAR>( pDest ),
        static_cast<ULONG>( cbDest ),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG ) == 0
            ? CY_TRUE
            : CY_FALSE;
#elif CYPHER_PLATFORM_LINUX
    usize cbWritten = 0u;
    // getrandom may be interrupted or legally return a short count; complete
    // the request before exposing any UUID bytes to the caller.
    while ( cbWritten < cbDest ) {
        const ssize_t cbResult = getrandom(
            static_cast<byte *>( pDest ) + cbWritten,
            cbDest - cbWritten,
            0u );
        if ( cbResult > 0 ) {
            cbWritten += static_cast<usize>( cbResult );
            continue;
        }
        if ( cbResult < 0 && errno == EINTR ) {
            continue;
        }
        return CY_FALSE;
    }
    return CY_TRUE;
#elif CYPHER_PLATFORM_MACOS
    arc4random_buf( pDest, cbDest );
    return CY_TRUE;
#else
    #error "UniqueId requires an operating-system random backend."
#endif
}

bool_t IsHyphenPosition( usize iChar ) noexcept
{
    return iChar == 8u || iChar == 13u || iChar == 18u || iChar == 23u;
}

} // namespace

bool_t UniqueId_CreateRandom( unique_id_t *pIdOut ) noexcept
{
    const bool_t bValidOutput = pIdOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "UniqueId_CreateRandom requires output storage." );
    if ( !bValidOutput ) {
        return CY_FALSE;
    }

    // Parse into a temporary so malformed text never partially changes pIdOut.
    unique_id_t id{};
    if ( !FillSecureRandom( id.bytes, sizeof( id.bytes ) ) ) {
        return CY_FALSE;
    }

    // RFC 4122 version 4 and variant 1 occupy the high bits of bytes 6 and 8.
    id.bytes[6] = static_cast<byte>( ( id.bytes[6] & 0x0Fu ) | 0x40u );
    id.bytes[8] = static_cast<byte>( ( id.bytes[8] & 0x3Fu ) | 0x80u );
    *pIdOut = id;
    return CY_TRUE;
}

bool_t UniqueId_FromBytes(
    const_byte_span_t bytes,
    unique_id_t *pIdOut ) noexcept
{
    const bool_t bValidBytes = Span_IsValid( bytes );
    const bool_t bValidOutput = pIdOut != nullptr;
    CY_ASSERT_MSG( bValidBytes, "UniqueId_FromBytes requires a valid byte span." );
    CY_ASSERT_MSG( bValidOutput, "UniqueId_FromBytes requires output storage." );
    if ( !bValidBytes || !bValidOutput ||
         bytes.nCount != CY_UNIQUE_ID_BYTE_COUNT ) {
        return CY_FALSE;
    }

    unique_id_t id{};
    Cy_MemCopy( id.bytes, bytes.pData, sizeof( id.bytes ) );
    *pIdOut = id;
    return CY_TRUE;
}

bool_t UniqueId_FromString(
    string_view_t text,
    unique_id_t *pIdOut ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    const bool_t bValidOutput = pIdOut != nullptr;
    CY_ASSERT_MSG( bValidText, "UniqueId_FromString requires a valid string view." );
    CY_ASSERT_MSG( bValidOutput, "UniqueId_FromString requires output storage." );
    if ( !bValidText || !bValidOutput ||
         text.cchLength != CY_UNIQUE_ID_STRING_LENGTH ) {
        return CY_FALSE;
    }

    unique_id_t id{};
    usize iByte = 0u;
    bool_t bHighNibble = CY_TRUE;
    for ( usize iChar = 0u; iChar < text.cchLength; ++iChar ) {
        if ( IsHyphenPosition( iChar ) ) {
            if ( text.pData[iChar] != '-' ) {
                return CY_FALSE;
            }
            continue;
        }

        const u8 nDigit = Char_HexValueAscii( text.pData[iChar] );
        if ( nDigit == CY_CHAR_INVALID_DIGIT_VALUE || iByte >= sizeof( id.bytes ) ) {
            return CY_FALSE;
        }
        if ( bHighNibble ) {
            id.bytes[iByte] = static_cast<byte>( nDigit << 4u );
        } else {
            id.bytes[iByte] = static_cast<byte>( id.bytes[iByte] | nDigit );
            ++iByte;
        }
        bHighNibble = !bHighNibble;
    }
    if ( iByte != sizeof( id.bytes ) || !bHighNibble ) {
        return CY_FALSE;
    }

    *pIdOut = id;
    return CY_TRUE;
}

usize UniqueId_ToString(
    unique_id_t id,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidDest =
        pDest != nullptr && cchDest >= CY_UNIQUE_ID_STRING_CAPACITY;
    if ( !bValidDest ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return 0u;
    }

    // Canonical lower-case 8-4-4-4-12 spelling; output size was checked above.
    usize iDest = 0u;
    for ( usize iByte = 0u; iByte < sizeof( id.bytes ); ++iByte ) {
        if ( iByte == 4u || iByte == 6u || iByte == 8u || iByte == 10u ) {
            pDest[iDest++] = '-';
        }
        pDest[iDest++] = g_uniqueIdHexDigits[id.bytes[iByte] >> 4u];
        pDest[iDest++] = g_uniqueIdHexDigits[id.bytes[iByte] & 0x0Fu];
    }
    pDest[iDest] = '\0';
    return iDest;
}

bool_t UniqueId_IsValid( unique_id_t id ) noexcept
{
    for ( byte value : id.bytes ) {
        if ( value != 0u ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

bool_t UniqueId_Equals( unique_id_t left, unique_id_t right ) noexcept
{
    return Cy_MemCompare( left.bytes, right.bytes, sizeof( left.bytes ) ) == 0;
}

i32 UniqueId_Compare( unique_id_t left, unique_id_t right ) noexcept
{
    const i32 nComparison = Cy_MemCompare(
        left.bytes,
        right.bytes,
        sizeof( left.bytes ) );
    return nComparison < 0 ? -1 : ( nComparison > 0 ? 1 : 0 );
}

} // namespace cypher::common
