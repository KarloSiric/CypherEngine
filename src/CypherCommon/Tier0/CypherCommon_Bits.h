//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Bits.h
//  Purpose: Declares CypherCommon Tier0 Bits support.
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

#ifndef CYPHER_COMMON_TIER0_BITS_H
#define CYPHER_COMMON_TIER0_BITS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Bits

Typed bit helpers for flags, masks, handles, packet fields and allocators.
================
*/

#include "CypherCommon_BaseTypes.h"

#include <bit>

namespace cypher::common
{

// Builds a 32-bit mask with one bit set.
[[nodiscard]] constexpr u32 Cy_Bit32( u32 nBit ) noexcept
{
    return nBit < 32u ? ( 1u << nBit ) : 0u;
}

// Builds a 64-bit mask with one bit set.
[[nodiscard]] constexpr u64 Cy_Bit64( u32 nBit ) noexcept
{
    return nBit < 64u ? ( 1ull << nBit ) : 0ull;
}

// Builds a mask with the lowest bit_count 32-bit bits set.
[[nodiscard]] constexpr u32 Cy_LowMask32( u32 nBitCount ) noexcept
{
    return nBitCount >= 32u ? CY_U32_MAX : ( ( 1u << nBitCount ) - 1u );
}

// Builds a mask with the lowest bit_count 64-bit bits set.
[[nodiscard]] constexpr u64 Cy_LowMask64( u32 nBitCount ) noexcept
{
    return nBitCount >= 64u ? CY_U64_MAX : ( ( 1ull << nBitCount ) - 1ull );
}

// Returns true when any requested flag is set.
[[nodiscard]] constexpr bool_t Cy_HasAnyFlags( u32 nValue, u32 nFlags ) noexcept
{
    return ( nValue & nFlags ) != 0u;
}

// Returns true when all requested flags are set.
[[nodiscard]] constexpr bool_t Cy_HasAllFlags( u32 nValue, u32 nFlags ) noexcept
{
    return ( nValue & nFlags ) == nFlags;
}

// Returns value with flags enabled.
[[nodiscard]] constexpr u32 Cy_SetFlags( u32 nValue, u32 nFlags ) noexcept
{
    return nValue | nFlags;
}

// Returns value with flags disabled.
[[nodiscard]] constexpr u32 Cy_ClearFlags( u32 nValue, u32 nFlags ) noexcept
{
    return nValue & ~nFlags;
}

// Returns value with flags flipped.
[[nodiscard]] constexpr u32 Cy_ToggleFlags( u32 nValue, u32 nFlags ) noexcept
{
    return nValue ^ nFlags;
}

// 64-bit variants for large feature and category masks.
[[nodiscard]] constexpr bool_t Cy_HasAnyFlags( u64 nValue, u64 nFlags ) noexcept
{
    return ( nValue & nFlags ) != 0ull;
}

[[nodiscard]] constexpr bool_t Cy_HasAllFlags( u64 nValue, u64 nFlags ) noexcept
{
    return ( nValue & nFlags ) == nFlags;
}

[[nodiscard]] constexpr u64 Cy_SetFlags( u64 nValue, u64 nFlags ) noexcept
{
    return nValue | nFlags;
}

[[nodiscard]] constexpr u64 Cy_ClearFlags( u64 nValue, u64 nFlags ) noexcept
{
    return nValue & ~nFlags;
}

[[nodiscard]] constexpr u64 Cy_ToggleFlags( u64 nValue, u64 nFlags ) noexcept
{
    return nValue ^ nFlags;
}

// Returns true when a specific bit is set in a 32-bit value.
[[nodiscard]] constexpr bool_t Cy_TestBit32( u32 nValue, u32 nBit ) noexcept
{
    return ( nValue & Cy_Bit32( nBit ) ) != 0u;
}

// Returns true when a specific bit is set in a 64-bit value.
[[nodiscard]] constexpr bool_t Cy_TestBit64( u64 nValue, u32 nBit ) noexcept
{
    return ( nValue & Cy_Bit64( nBit ) ) != 0u;
}

// Returns value with a specific 32-bit bit enabled.
[[nodiscard]] constexpr u32 Cy_SetBit32( u32 nValue, u32 nBit ) noexcept
{
    return nValue | Cy_Bit32( nBit );
}

// Returns value with a specific 64-bit bit enabled.
[[nodiscard]] constexpr u64 Cy_SetBit64( u64 nValue, u32 nBit ) noexcept
{
    return nValue | Cy_Bit64( nBit );
}

// Returns value with a specific 32-bit bit disabled.
[[nodiscard]] constexpr u32 Cy_ClearBit32( u32 nValue, u32 nBit ) noexcept
{
    return nValue & ~Cy_Bit32( nBit );
}

// Returns value with a specific 64-bit bit disabled.
[[nodiscard]] constexpr u64 Cy_ClearBit64( u64 nValue, u32 nBit ) noexcept
{
    return nValue & ~Cy_Bit64( nBit );
}

// Counts set bits in a 32-bit value.
[[nodiscard]] constexpr u32 Cy_PopCount32( u32 nValue ) noexcept
{
    return static_cast<u32>( std::popcount( nValue ) );
}

// Counts set bits in a 64-bit value.
[[nodiscard]] constexpr u32 Cy_PopCount64( u64 nValue ) noexcept
{
    return static_cast<u32>( std::popcount( nValue ) );
}

// Counts zero bits before the highest set 32-bit bit.
[[nodiscard]] constexpr u32 Cy_CountLeadingZeros32( u32 nValue ) noexcept
{
    return static_cast<u32>( std::countl_zero( nValue ) );
}

// Counts zero bits before the highest set 64-bit bit.
[[nodiscard]] constexpr u32 Cy_CountLeadingZeros64( u64 nValue ) noexcept
{
    return static_cast<u32>( std::countl_zero( nValue ) );
}

// Counts zero bits after the lowest set 32-bit bit.
[[nodiscard]] constexpr u32 Cy_CountTrailingZeros32( u32 nValue ) noexcept
{
    return static_cast<u32>( std::countr_zero( nValue ) );
}

// Counts zero bits after the lowest set 64-bit bit.
[[nodiscard]] constexpr u32 Cy_CountTrailingZeros64( u64 nValue ) noexcept
{
    return static_cast<u32>( std::countr_zero( nValue ) );
}

// Returns the index of the lowest set bit, or -1 when value is zero.
[[nodiscard]] constexpr i32 Cy_FindLowestSetBit32( u32 nValue ) noexcept
{
    return nValue == 0u ? -1 : static_cast<i32>( Cy_CountTrailingZeros32( nValue ) );
}

// Returns the index of the lowest set bit, or -1 when value is zero.
[[nodiscard]] constexpr i32 Cy_FindLowestSetBit64( u64 nValue ) noexcept
{
    return nValue == 0ull ? -1 : static_cast<i32>( Cy_CountTrailingZeros64( nValue ) );
}

// Returns the index of the highest set bit, or -1 when value is zero.
[[nodiscard]] constexpr i32 Cy_FindHighestSetBit32( u32 nValue ) noexcept
{
    return nValue == 0u ? -1 : static_cast<i32>( 31u - Cy_CountLeadingZeros32( nValue ) );
}

// Returns the index of the highest set bit, or -1 when value is zero.
[[nodiscard]] constexpr i32 Cy_FindHighestSetBit64( u64 nValue ) noexcept
{
    return nValue == 0ull ? -1 : static_cast<i32>( 63u - Cy_CountLeadingZeros64( nValue ) );
}

// Rounds up to the next power of two. Zero returns one and overflow returns zero.
[[nodiscard]] constexpr u32 Cy_NextPowerOfTwo32( u32 nValue ) noexcept
{
    if ( nValue <= 1u ) {
        return 1u;
    }

    --nValue;
    nValue |= nValue >> 1u;
    nValue |= nValue >> 2u;
    nValue |= nValue >> 4u;
    nValue |= nValue >> 8u;
    nValue |= nValue >> 16u;
    return nValue + 1u;
}

// Rounds up to the next power of two. Zero returns one and overflow returns zero.
[[nodiscard]] constexpr u64 Cy_NextPowerOfTwo64( u64 nValue ) noexcept
{
    if ( nValue <= 1ull ) {
        return 1ull;
    }

    --nValue;
    nValue |= nValue >> 1u;
    nValue |= nValue >> 2u;
    nValue |= nValue >> 4u;
    nValue |= nValue >> 8u;
    nValue |= nValue >> 16u;
    nValue |= nValue >> 32u;
    return nValue + 1ull;
}

[[nodiscard]] constexpr bool_t Cy_NextPowerOfTwo32Checked( u32 nValue, u32 &nOutValue ) noexcept
{
    nOutValue = Cy_NextPowerOfTwo32( nValue );
    return nOutValue != 0u;
}

[[nodiscard]] constexpr bool_t Cy_NextPowerOfTwo64Checked( u64 nValue, u64 &nOutValue ) noexcept
{
    nOutValue = Cy_NextPowerOfTwo64( nValue );
    return nOutValue != 0ull;
}

// Rounds down to the previous power of two; zero returns zero.
[[nodiscard]] constexpr u32 Cy_PreviousPowerOfTwo32( u32 nValue ) noexcept
{
    return nValue == 0u ? 0u : Cy_Bit32( static_cast<u32>( Cy_FindHighestSetBit32( nValue ) ) );
}

// Rounds down to the previous power of two; zero returns zero.
[[nodiscard]] constexpr u64 Cy_PreviousPowerOfTwo64( u64 nValue ) noexcept
{
    return nValue == 0ull ? 0ull : Cy_Bit64( static_cast<u32>( Cy_FindHighestSetBit64( nValue ) ) );
}

// Rotates a 32-bit value left by shift bits.
[[nodiscard]] constexpr u32 Cy_RotateLeft32( u32 nValue, i32 nShift ) noexcept
{
    return std::rotl( nValue, nShift );
}

// Rotates a 32-bit value right by shift bits.
[[nodiscard]] constexpr u32 Cy_RotateRight32( u32 nValue, i32 nShift ) noexcept
{
    return std::rotr( nValue, nShift );
}

// Rotates a 64-bit value left by shift bits.
[[nodiscard]] constexpr u64 Cy_RotateLeft64( u64 nValue, i32 nShift ) noexcept
{
    return std::rotl( nValue, nShift );
}

// Rotates a 64-bit value right by shift bits.
[[nodiscard]] constexpr u64 Cy_RotateRight64( u64 nValue, i32 nShift ) noexcept
{
    return std::rotr( nValue, nShift );
}

// Builds a bounded contiguous mask. Invalid offsets return zero and counts are
// clamped to the remaining width.
[[nodiscard]] constexpr u32 Cy_BitRangeMask32( u32 nBitOffset, u32 nBitCount ) noexcept
{
    return nBitOffset < 32u
        ? Cy_LowMask32( nBitCount < 32u - nBitOffset ? nBitCount : 32u - nBitOffset ) << nBitOffset
        : 0u;
}

[[nodiscard]] constexpr u64 Cy_BitRangeMask64( u32 nBitOffset, u32 nBitCount ) noexcept
{
    return nBitOffset < 64u
        ? Cy_LowMask64( nBitCount < 64u - nBitOffset ? nBitCount : 64u - nBitOffset ) << nBitOffset
        : 0ull;
}

// Extracts a bounded bit field and right-aligns it.
[[nodiscard]] constexpr u32 Cy_ExtractBits32( u32 nValue, u32 nBitOffset, u32 nBitCount ) noexcept
{
    return nBitOffset < 32u
        ? ( nValue & Cy_BitRangeMask32( nBitOffset, nBitCount ) ) >> nBitOffset
        : 0u;
}

[[nodiscard]] constexpr u64 Cy_ExtractBits64( u64 nValue, u32 nBitOffset, u32 nBitCount ) noexcept
{
    return nBitOffset < 64u
        ? ( nValue & Cy_BitRangeMask64( nBitOffset, nBitCount ) ) >> nBitOffset
        : 0ull;
}

// Replaces a bounded bit field with the low bits from fieldValue.
[[nodiscard]] constexpr u32 Cy_ReplaceBits32(
    u32 nValue,
    u32 nFieldValue,
    u32 nBitOffset,
    u32 nBitCount ) noexcept
{
    const u32 nMask = Cy_BitRangeMask32( nBitOffset, nBitCount );
    return nBitOffset < 32u
        ? ( nValue & ~nMask ) | ( ( nFieldValue << nBitOffset ) & nMask )
        : nValue;
}

[[nodiscard]] constexpr u64 Cy_ReplaceBits64(
    u64 nValue,
    u64 nFieldValue,
    u32 nBitOffset,
    u32 nBitCount ) noexcept
{
    const u64 nMask = Cy_BitRangeMask64( nBitOffset, nBitCount );
    return nBitOffset < 64u
        ? ( nValue & ~nMask ) | ( ( nFieldValue << nBitOffset ) & nMask )
        : nValue;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_BITS_H
