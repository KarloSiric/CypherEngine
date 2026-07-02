#ifndef CYPHER_COMMON_TIER0_BITS_H
#define CYPHER_COMMON_TIER0_BITS_H
#pragma once

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
constexpr u32 Bit32( u32 nBit )
{
    return 1u << nBit;
}

// Builds a 64-bit mask with one bit set.
constexpr u64 Bit64( u32 nBit )
{
    return 1ull << nBit;
}

// Builds a mask with the lowest bit_count 32-bit bits set.
constexpr u32 LowMask32( u32 nBitCount )
{
    return nBitCount >= 32u ? CY_U32_MAX : ( ( 1u << nBitCount ) - 1u );
}

// Builds a mask with the lowest bit_count 64-bit bits set.
constexpr u64 LowMask64( u32 nBitCount )
{
    return nBitCount >= 64u ? CY_U64_MAX : ( ( 1ull << nBitCount ) - 1ull );
}

// Returns true when any requested flag is set.
constexpr bool_t HasAnyFlags( u32 nValue, u32 nFlags )
{
    return ( nValue & nFlags ) != 0u;
}

// Returns true when all requested flags are set.
constexpr bool_t HasAllFlags( u32 nValue, u32 nFlags )
{
    return ( nValue & nFlags ) == nFlags;
}

// Returns value with flags enabled.
constexpr u32 SetFlags( u32 nValue, u32 nFlags )
{
    return nValue | nFlags;
}

// Returns value with flags disabled.
constexpr u32 ClearFlags( u32 nValue, u32 nFlags )
{
    return nValue & ~nFlags;
}

// Returns value with flags flipped.
constexpr u32 ToggleFlags( u32 nValue, u32 nFlags )
{
    return nValue ^ nFlags;
}

// Returns true when a specific bit is set in a 32-bit value.
constexpr bool_t TestBit32( u32 nValue, u32 nBit )
{
    return ( nValue & Bit32( nBit ) ) != 0u;
}

// Returns true when a specific bit is set in a 64-bit value.
constexpr bool_t TestBit64( u64 nValue, u32 nBit )
{
    return ( nValue & Bit64( nBit ) ) != 0u;
}

// Returns value with a specific 32-bit bit enabled.
constexpr u32 SetBit32( u32 nValue, u32 nBit )
{
    return nValue | Bit32( nBit );
}

// Returns value with a specific 64-bit bit enabled.
constexpr u64 SetBit64( u64 nValue, u32 nBit )
{
    return nValue | Bit64( nBit );
}

// Returns value with a specific 32-bit bit disabled.
constexpr u32 ClearBit32( u32 nValue, u32 nBit )
{
    return nValue & ~Bit32( nBit );
}

// Returns value with a specific 64-bit bit disabled.
constexpr u64 ClearBit64( u64 nValue, u32 nBit )
{
    return nValue & ~Bit64( nBit );
}

// Counts set bits in a 32-bit value.
inline i32 PopCount32( u32 nValue )
{
    return static_cast<i32>( std::popcount( nValue ) );
}

// Counts set bits in a 64-bit value.
inline i32 PopCount64( u64 nValue )
{
    return static_cast<i32>( std::popcount( nValue ) );
}

// Counts zero bits before the highest set 32-bit bit.
inline i32 CountLeadingZeros32( u32 nValue )
{
    return static_cast<i32>( std::countl_zero( nValue ) );
}

// Counts zero bits before the highest set 64-bit bit.
inline i32 CountLeadingZeros64( u64 nValue )
{
    return static_cast<i32>( std::countl_zero( nValue ) );
}

// Counts zero bits after the lowest set 32-bit bit.
inline i32 CountTrailingZeros32( u32 nValue )
{
    return static_cast<i32>( std::countr_zero( nValue ) );
}

// Counts zero bits after the lowest set 64-bit bit.
inline i32 CountTrailingZeros64( u64 nValue )
{
    return static_cast<i32>( std::countr_zero( nValue ) );
}

// Returns the index of the lowest set bit, or -1 when value is zero.
inline i32 FindLowestSetBit32( u32 nValue )
{
    return nValue == 0u ? -1 : CountTrailingZeros32( nValue );
}

// Returns the index of the lowest set bit, or -1 when value is zero.
inline i32 FindLowestSetBit64( u64 nValue )
{
    return nValue == 0ull ? -1 : CountTrailingZeros64( nValue );
}

// Returns the index of the highest set bit, or -1 when value is zero.
inline i32 FindHighestSetBit32( u32 nValue )
{
    return nValue == 0u ? -1 : 31 - CountLeadingZeros32( nValue );
}

// Returns the index of the highest set bit, or -1 when value is zero.
inline i32 FindHighestSetBit64( u64 nValue )
{
    return nValue == 0ull ? -1 : 63 - CountLeadingZeros64( nValue );
}

// Rounds up to the next power of two; zero returns one.
constexpr u32 NextPowerOfTwo32( u32 nValue )
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

// Rounds up to the next power of two; zero returns one.
constexpr u64 NextPowerOfTwo64( u64 nValue )
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

// Rounds down to the previous power of two; zero returns zero.
inline u32 PreviousPowerOfTwo32( u32 nValue )
{
    return nValue == 0u ? 0u : Bit32( static_cast<u32>( FindHighestSetBit32( nValue ) ) );
}

// Rounds down to the previous power of two; zero returns zero.
inline u64 PreviousPowerOfTwo64( u64 nValue )
{
    return nValue == 0ull ? 0ull : Bit64( static_cast<u32>( FindHighestSetBit64( nValue ) ) );
}

// Rotates a 32-bit value left by shift bits.
constexpr u32 RotateLeft32( u32 nValue, i32 nShift )
{
    return std::rotl( nValue, nShift );
}

// Rotates a 32-bit value right by shift bits.
constexpr u32 RotateRight32( u32 nValue, i32 nShift )
{
    return std::rotr( nValue, nShift );
}

// Rotates a 64-bit value left by shift bits.
constexpr u64 RotateLeft64( u64 nValue, i32 nShift )
{
    return std::rotl( nValue, nShift );
}

// Rotates a 64-bit value right by shift bits.
constexpr u64 RotateRight64( u64 nValue, i32 nShift )
{
    return std::rotr( nValue, nShift );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_BITS_H
