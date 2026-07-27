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

#include <bit>
#include <cstring>

namespace cypher::common
{

// Reverses the byte order of a 16-bit integer.
[[nodiscard]] constexpr u16 Cy_ByteSwap16( u16 nValue ) noexcept
{
    return static_cast<u16>( ( nValue >> 8u ) | ( nValue << 8u ) );
}

// Reverses the byte order of a 32-bit integer.
[[nodiscard]] constexpr u32 Cy_ByteSwap32( u32 nValue ) noexcept
{
    return ( ( nValue & 0x000000FFu ) << 24u ) |
           ( ( nValue & 0x0000FF00u ) << 8u ) |
           ( ( nValue & 0x00FF0000u ) >> 8u ) |
           ( ( nValue & 0xFF000000u ) >> 24u );
}

// Reverses the byte order of a 64-bit integer.
[[nodiscard]] constexpr u64 Cy_ByteSwap64( u64 nValue ) noexcept
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
[[nodiscard]] constexpr u32 Cy_MakeFourCC( char ch0, char ch1, char ch2, char ch3 ) noexcept
{
    return static_cast<u32>( static_cast<u8>( ch0 ) ) |
           ( static_cast<u32>( static_cast<u8>( ch1 ) ) << 8u ) |
           ( static_cast<u32>( static_cast<u8>( ch2 ) ) << 16u ) |
           ( static_cast<u32>( static_cast<u8>( ch3 ) ) << 24u );
}

// Extracts one character from a FourCC; invalid indices return nul.
[[nodiscard]] constexpr char Cy_FourCCChar( u32 nFourCC, u32 nIndex ) noexcept
{
    return nIndex < 4u ? static_cast<char>( ( nFourCC >> ( nIndex * 8u ) ) & 0xFFu ) : '\0';
}

// Extracts the first byte from a FourCC.
[[nodiscard]] constexpr char Cy_FourCCChar0( u32 nFourCC ) noexcept
{
    return Cy_FourCCChar( nFourCC, 0u );
}

// Extracts the second byte from a FourCC.
[[nodiscard]] constexpr char Cy_FourCCChar1( u32 nFourCC ) noexcept
{
    return Cy_FourCCChar( nFourCC, 1u );
}

// Extracts the third byte from a FourCC.
[[nodiscard]] constexpr char Cy_FourCCChar2( u32 nFourCC ) noexcept
{
    return Cy_FourCCChar( nFourCC, 2u );
}

// Extracts the fourth byte from a FourCC.
[[nodiscard]] constexpr char Cy_FourCCChar3( u32 nFourCC ) noexcept
{
    return Cy_FourCCChar( nFourCC, 3u );
}

// Returns true when a FourCC matches four expected ASCII characters.
[[nodiscard]] constexpr bool_t Cy_FourCCMatches( u32 nFourCC, char ch0, char ch1, char ch2, char ch3 ) noexcept
{
    return nFourCC == Cy_MakeFourCC( ch0, ch1, ch2, ch3 );
}

// Converts host byte order to little-endian 16-bit order.
[[nodiscard]] constexpr u16 Cy_HostToLittle16( u16 nValue ) noexcept
{
#if CYPHER_ENDIAN_LITTLE
    return nValue;
#else
    return Cy_ByteSwap16( nValue );
#endif
}

// Converts host byte order to little-endian 32-bit order.
[[nodiscard]] constexpr u32 Cy_HostToLittle32( u32 nValue ) noexcept
{
#if CYPHER_ENDIAN_LITTLE
    return nValue;
#else
    return Cy_ByteSwap32( nValue );
#endif
}

// Converts host byte order to little-endian 64-bit order.
[[nodiscard]] constexpr u64 Cy_HostToLittle64( u64 nValue ) noexcept
{
#if CYPHER_ENDIAN_LITTLE
    return nValue;
#else
    return Cy_ByteSwap64( nValue );
#endif
}

// Converts little-endian 16-bit order to host byte order.
[[nodiscard]] constexpr u16 Cy_LittleToHost16( u16 nValue ) noexcept
{
    return Cy_HostToLittle16( nValue );
}

// Converts little-endian 32-bit order to host byte order.
[[nodiscard]] constexpr u32 Cy_LittleToHost32( u32 nValue ) noexcept
{
    return Cy_HostToLittle32( nValue );
}

// Converts little-endian 64-bit order to host byte order.
[[nodiscard]] constexpr u64 Cy_LittleToHost64( u64 nValue ) noexcept
{
    return Cy_HostToLittle64( nValue );
}

// Converts host byte order to big-endian 16-bit order.
[[nodiscard]] constexpr u16 Cy_HostToBig16( u16 nValue ) noexcept
{
#if CYPHER_ENDIAN_BIG
    return nValue;
#else
    return Cy_ByteSwap16( nValue );
#endif
}

// Converts host byte order to big-endian 32-bit order.
[[nodiscard]] constexpr u32 Cy_HostToBig32( u32 nValue ) noexcept
{
#if CYPHER_ENDIAN_BIG
    return nValue;
#else
    return Cy_ByteSwap32( nValue );
#endif
}

// Converts host byte order to big-endian 64-bit order.
[[nodiscard]] constexpr u64 Cy_HostToBig64( u64 nValue ) noexcept
{
#if CYPHER_ENDIAN_BIG
    return nValue;
#else
    return Cy_ByteSwap64( nValue );
#endif
}

// Converts big-endian 16-bit order to host byte order.
[[nodiscard]] constexpr u16 Cy_BigToHost16( u16 nValue ) noexcept
{
    return Cy_HostToBig16( nValue );
}

// Converts big-endian 32-bit order to host byte order.
[[nodiscard]] constexpr u32 Cy_BigToHost32( u32 nValue ) noexcept
{
    return Cy_HostToBig32( nValue );
}

// Converts big-endian 64-bit order to host byte order.
[[nodiscard]] constexpr u64 Cy_BigToHost64( u64 nValue ) noexcept
{
    return Cy_HostToBig64( nValue );
}

// Reverses the byte order of an IEEE-754 32-bit floating-point value.
[[nodiscard]] constexpr f32 Cy_ByteSwapF32( f32 flValue ) noexcept
{
    return std::bit_cast<f32>( Cy_ByteSwap32( std::bit_cast<u32>( flValue ) ) );
}

// Reverses the byte order of an IEEE-754 64-bit floating-point value.
[[nodiscard]] constexpr f64 Cy_ByteSwapF64( f64 flValue ) noexcept
{
    return std::bit_cast<f64>( Cy_ByteSwap64( std::bit_cast<u64>( flValue ) ) );
}

// Converts host byte order to little-endian IEEE-754 32-bit order.
[[nodiscard]] constexpr f32 Cy_HostToLittleF32( f32 flValue ) noexcept
{
#if CYPHER_ENDIAN_LITTLE
    return flValue;
#else
    return Cy_ByteSwapF32( flValue );
#endif
}

// Converts host byte order to little-endian IEEE-754 64-bit order.
[[nodiscard]] constexpr f64 Cy_HostToLittleF64( f64 flValue ) noexcept
{
#if CYPHER_ENDIAN_LITTLE
    return flValue;
#else
    return Cy_ByteSwapF64( flValue );
#endif
}

// Converts little-endian IEEE-754 32-bit order to host byte order.
[[nodiscard]] constexpr f32 Cy_LittleToHostF32( f32 flValue ) noexcept
{
    return Cy_HostToLittleF32( flValue );
}

// Converts little-endian IEEE-754 64-bit order to host byte order.
[[nodiscard]] constexpr f64 Cy_LittleToHostF64( f64 flValue ) noexcept
{
    return Cy_HostToLittleF64( flValue );
}

// Converts host byte order to big-endian IEEE-754 32-bit order.
[[nodiscard]] constexpr f32 Cy_HostToBigF32( f32 flValue ) noexcept
{
#if CYPHER_ENDIAN_BIG
    return flValue;
#else
    return Cy_ByteSwapF32( flValue );
#endif
}

// Converts host byte order to big-endian IEEE-754 64-bit order.
[[nodiscard]] constexpr f64 Cy_HostToBigF64( f64 flValue ) noexcept
{
#if CYPHER_ENDIAN_BIG
    return flValue;
#else
    return Cy_ByteSwapF64( flValue );
#endif
}

// Converts big-endian IEEE-754 32-bit order to host byte order.
[[nodiscard]] constexpr f32 Cy_BigToHostF32( f32 flValue ) noexcept
{
    return Cy_HostToBigF32( flValue );
}

// Converts big-endian IEEE-754 64-bit order to host byte order.
[[nodiscard]] constexpr f64 Cy_BigToHostF64( f64 flValue ) noexcept
{
    return Cy_HostToBigF64( flValue );
}

// Reads an unaligned 16-bit little-endian value. pSrc must reference two bytes.
[[nodiscard]] inline u16 Cy_ReadLittle16( const void *pSrc ) noexcept
{
    u16 nValue = 0u;
    std::memcpy( &nValue, pSrc, sizeof( nValue ) );
    return Cy_LittleToHost16( nValue );
}

// Reads an unaligned 32-bit little-endian value. pSrc must reference four bytes.
[[nodiscard]] inline u32 Cy_ReadLittle32( const void *pSrc ) noexcept
{
    u32 nValue = 0u;
    std::memcpy( &nValue, pSrc, sizeof( nValue ) );
    return Cy_LittleToHost32( nValue );
}

// Reads an unaligned 64-bit little-endian value. pSrc must reference eight bytes.
[[nodiscard]] inline u64 Cy_ReadLittle64( const void *pSrc ) noexcept
{
    u64 nValue = 0u;
    std::memcpy( &nValue, pSrc, sizeof( nValue ) );
    return Cy_LittleToHost64( nValue );
}

// Reads an unaligned 32-bit little-endian floating-point value.
[[nodiscard]] inline f32 Cy_ReadLittleF32( const void *pSrc ) noexcept
{
    return std::bit_cast<f32>( Cy_ReadLittle32( pSrc ) );
}

// Reads an unaligned 64-bit little-endian floating-point value.
[[nodiscard]] inline f64 Cy_ReadLittleF64( const void *pSrc ) noexcept
{
    return std::bit_cast<f64>( Cy_ReadLittle64( pSrc ) );
}

// Reads an unaligned 16-bit big-endian value. pSrc must reference two bytes.
[[nodiscard]] inline u16 Cy_ReadBig16( const void *pSrc ) noexcept
{
    u16 nValue = 0u;
    std::memcpy( &nValue, pSrc, sizeof( nValue ) );
    return Cy_BigToHost16( nValue );
}

// Reads an unaligned 32-bit big-endian value. pSrc must reference four bytes.
[[nodiscard]] inline u32 Cy_ReadBig32( const void *pSrc ) noexcept
{
    u32 nValue = 0u;
    std::memcpy( &nValue, pSrc, sizeof( nValue ) );
    return Cy_BigToHost32( nValue );
}

// Reads an unaligned 64-bit big-endian value. pSrc must reference eight bytes.
[[nodiscard]] inline u64 Cy_ReadBig64( const void *pSrc ) noexcept
{
    u64 nValue = 0u;
    std::memcpy( &nValue, pSrc, sizeof( nValue ) );
    return Cy_BigToHost64( nValue );
}

// Reads an unaligned 32-bit big-endian floating-point value.
[[nodiscard]] inline f32 Cy_ReadBigF32( const void *pSrc ) noexcept
{
    return std::bit_cast<f32>( Cy_ReadBig32( pSrc ) );
}

// Reads an unaligned 64-bit big-endian floating-point value.
[[nodiscard]] inline f64 Cy_ReadBigF64( const void *pSrc ) noexcept
{
    return std::bit_cast<f64>( Cy_ReadBig64( pSrc ) );
}

// Writes a 16-bit value in little-endian order. pDst must reference two bytes.
inline void Cy_WriteLittle16( void *pDst, u16 nValue ) noexcept
{
    nValue = Cy_HostToLittle16( nValue );
    std::memcpy( pDst, &nValue, sizeof( nValue ) );
}

// Writes a 32-bit value in little-endian order. pDst must reference four bytes.
inline void Cy_WriteLittle32( void *pDst, u32 nValue ) noexcept
{
    nValue = Cy_HostToLittle32( nValue );
    std::memcpy( pDst, &nValue, sizeof( nValue ) );
}

// Writes a 64-bit value in little-endian order. pDst must reference eight bytes.
inline void Cy_WriteLittle64( void *pDst, u64 nValue ) noexcept
{
    nValue = Cy_HostToLittle64( nValue );
    std::memcpy( pDst, &nValue, sizeof( nValue ) );
}

// Writes a 32-bit floating-point value in little-endian order.
inline void Cy_WriteLittleF32( void *pDst, f32 flValue ) noexcept
{
    Cy_WriteLittle32( pDst, std::bit_cast<u32>( flValue ) );
}

// Writes a 64-bit floating-point value in little-endian order.
inline void Cy_WriteLittleF64( void *pDst, f64 flValue ) noexcept
{
    Cy_WriteLittle64( pDst, std::bit_cast<u64>( flValue ) );
}

// Writes a 16-bit value in big-endian order. pDst must reference two bytes.
inline void Cy_WriteBig16( void *pDst, u16 nValue ) noexcept
{
    nValue = Cy_HostToBig16( nValue );
    std::memcpy( pDst, &nValue, sizeof( nValue ) );
}

// Writes a 32-bit value in big-endian order. pDst must reference four bytes.
inline void Cy_WriteBig32( void *pDst, u32 nValue ) noexcept
{
    nValue = Cy_HostToBig32( nValue );
    std::memcpy( pDst, &nValue, sizeof( nValue ) );
}

// Writes a 64-bit value in big-endian order. pDst must reference eight bytes.
inline void Cy_WriteBig64( void *pDst, u64 nValue ) noexcept
{
    nValue = Cy_HostToBig64( nValue );
    std::memcpy( pDst, &nValue, sizeof( nValue ) );
}

// Writes a 32-bit floating-point value in big-endian order.
inline void Cy_WriteBigF32( void *pDst, f32 flValue ) noexcept
{
    Cy_WriteBig32( pDst, std::bit_cast<u32>( flValue ) );
}

// Writes a 64-bit floating-point value in big-endian order.
inline void Cy_WriteBigF64( void *pDst, f64 flValue ) noexcept
{
    Cy_WriteBig64( pDst, std::bit_cast<u64>( flValue ) );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_ENDIAN_H
