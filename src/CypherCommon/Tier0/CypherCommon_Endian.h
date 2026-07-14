//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Endian.h
//  Purpose: Declares CypherCommon Tier0 Endian support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_ENDIAN_H
#define CYPHER_COMMON_TIER0_ENDIAN_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Endian

Byte swapping, host/endian conversion and FourCC helpers.
================
*/

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Platform.h"

namespace cypher::common
{

// Reverses the byte order of a 16-bit integer.
constexpr u16 ByteSwap16( u16 nValue )
{
    return static_cast<u16>( ( nValue >> 8u ) | ( nValue << 8u ) );
}

// Reverses the byte order of a 32-bit integer.
constexpr u32 ByteSwap32( u32 nValue )
{
    return ( ( nValue & 0x000000FFu ) << 24u ) |
           ( ( nValue & 0x0000FF00u ) << 8u ) |
           ( ( nValue & 0x00FF0000u ) >> 8u ) |
           ( ( nValue & 0xFF000000u ) >> 24u );
}

// Reverses the byte order of a 64-bit integer.
constexpr u64 ByteSwap64( u64 nValue )
{
    return ( ( nValue & 0x00000000000000FFull ) << 56u ) |
           ( ( nValue & 0x000000000000FF00ull ) << 40u ) |
           ( ( nValue & 0x0000000000FF0000ull ) << 24u ) |
           ( ( nValue & 0x00000000FF000000ull ) << 8u ) |
           ( ( nValue & 0x000000FF00000000ull ) >> 8u ) |
           ( ( nValue & 0x0000FF0000000000ull ) >> 24u ) |
           ( ( nValue & 0x00FF000000000000ull ) >> 40u ) |
           ( ( nValue & 0xFF00000000000000ull ) >> 56u );
}

// Packs four ASCII characters into a little-endian FourCC value.
constexpr u32 MakeFourCC( char ch0, char ch1, char ch2, char ch3 )
{
    return static_cast<u32>( static_cast<u8>( ch0 ) ) |
           ( static_cast<u32>( static_cast<u8>( ch1 ) ) << 8u ) |
           ( static_cast<u32>( static_cast<u8>( ch2 ) ) << 16u ) |
           ( static_cast<u32>( static_cast<u8>( ch3 ) ) << 24u );
}

// Extracts one character from a FourCC; invalid indices return nul.
constexpr char FourCCChar( u32 nFourCC, u32 nIndex )
{
    return nIndex < 4u ? static_cast<char>( ( nFourCC >> ( nIndex * 8u ) ) & 0xFFu ) : '\0';
}

// Extracts the first byte from a FourCC.
constexpr char FourCCChar0( u32 nFourCC )
{
    return FourCCChar( nFourCC, 0u );
}

// Extracts the second byte from a FourCC.
constexpr char FourCCChar1( u32 nFourCC )
{
    return FourCCChar( nFourCC, 1u );
}

// Extracts the third byte from a FourCC.
constexpr char FourCCChar2( u32 nFourCC )
{
    return FourCCChar( nFourCC, 2u );
}

// Extracts the fourth byte from a FourCC.
constexpr char FourCCChar3( u32 nFourCC )
{
    return FourCCChar( nFourCC, 3u );
}

// Returns true when a FourCC matches four expected ASCII characters.
constexpr bool_t FourCCMatches( u32 nFourCC, char ch0, char ch1, char ch2, char ch3 )
{
    return nFourCC == MakeFourCC( ch0, ch1, ch2, ch3 );
}

// Converts host byte order to little-endian 16-bit order.
constexpr u16 HostToLittle16( u16 nValue )
{
#if CYPHER_ENDIAN_LITTLE
    return nValue;
#else
    return ByteSwap16( nValue );
#endif
}

// Converts host byte order to little-endian 32-bit order.
constexpr u32 HostToLittle32( u32 nValue )
{
#if CYPHER_ENDIAN_LITTLE
    return nValue;
#else
    return ByteSwap32( nValue );
#endif
}

// Converts host byte order to little-endian 64-bit order.
constexpr u64 HostToLittle64( u64 nValue )
{
#if CYPHER_ENDIAN_LITTLE
    return nValue;
#else
    return ByteSwap64( nValue );
#endif
}

// Converts little-endian 16-bit order to host byte order.
constexpr u16 LittleToHost16( u16 nValue )
{
    return HostToLittle16( nValue );
}

// Converts little-endian 32-bit order to host byte order.
constexpr u32 LittleToHost32( u32 nValue )
{
    return HostToLittle32( nValue );
}

// Converts little-endian 64-bit order to host byte order.
constexpr u64 LittleToHost64( u64 nValue )
{
    return HostToLittle64( nValue );
}

// Converts host byte order to big-endian 16-bit order.
constexpr u16 HostToBig16( u16 nValue )
{
#if CYPHER_ENDIAN_BIG
    return nValue;
#else
    return ByteSwap16( nValue );
#endif
}

// Converts host byte order to big-endian 32-bit order.
constexpr u32 HostToBig32( u32 nValue )
{
#if CYPHER_ENDIAN_BIG
    return nValue;
#else
    return ByteSwap32( nValue );
#endif
}

// Converts host byte order to big-endian 64-bit order.
constexpr u64 HostToBig64( u64 nValue )
{
#if CYPHER_ENDIAN_BIG
    return nValue;
#else
    return ByteSwap64( nValue );
#endif
}

// Converts big-endian 16-bit order to host byte order.
constexpr u16 BigToHost16( u16 nValue )
{
    return HostToBig16( nValue );
}

// Converts big-endian 32-bit order to host byte order.
constexpr u32 BigToHost32( u32 nValue )
{
    return HostToBig32( nValue );
}

// Converts big-endian 64-bit order to host byte order.
constexpr u64 BigToHost64( u64 nValue )
{
    return HostToBig64( nValue );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_ENDIAN_H
