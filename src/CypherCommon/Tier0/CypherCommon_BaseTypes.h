//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_BaseTypes.h
//  Purpose: Declares CypherCommon Tier0 BaseTypes support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_BASETYPES_H
#define CYPHER_COMMON_TIER0_BASETYPES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Base Types

Primitive engine-wide scalar types and constants.

Rules:
- No CypherEngine dependency.
- No allocation.
- No containers.
- No platform API calls.
- No subsystem policy.
================
*/

#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace cypher::common
{

/*
================
Fixed Width Integer Types
================
*/
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

/*
================
Floating Point Types
================
*/
using f32 = float;
using f64 = double;

/*
================
Size And Pointer Types
================
*/
using usize = std::size_t;
using isize = std::ptrdiff_t;
using uintptr = std::uintptr_t;
using intptr = std::intptr_t;

/*
================
Byte And Text Pointer Types
================
*/
using byte = u8;
using char8 = char;
using cstring = const char *;
using mstring = char *;

/*
================
Boolean Types
================
*/
using bool_t = bool;

enum class b8 : u8 {
    False = 0u,
    True = 1u
};

constexpr bool_t CY_FALSE = false;
constexpr bool_t CY_TRUE = true;

// Converts stable one-byte Boolean storage into a runtime Boolean.
[[nodiscard]] constexpr bool_t Cy_B8ToBool( b8 value ) noexcept
{
    return value == b8::True;
}

// Converts a runtime Boolean into stable one-byte Boolean storage.
[[nodiscard]] constexpr b8 Cy_B8FromBool( bool_t value ) noexcept
{
    return value ? b8::True : b8::False;
}

/*
================
Generic Engine ID Types
================
*/
using index_t = u32;
using generation_t = u32;
using handle_t = u32;
using frame_index_t = u64;

constexpr index_t CY_INVALID_INDEX = std::numeric_limits<index_t>::max();
constexpr generation_t CY_INVALID_GENERATION = std::numeric_limits<generation_t>::max();
constexpr handle_t CY_INVALID_HANDLE = 0u;
constexpr frame_index_t CY_INVALID_FRAME_INDEX = std::numeric_limits<frame_index_t>::max();

/*
================
Common Size Constants
================
*/
constexpr usize CY_BITS_PER_BYTE = static_cast<usize>( CHAR_BIT );
constexpr usize CY_KIB = static_cast<usize>( 1024u );
constexpr usize CY_MIB = CY_KIB * static_cast<usize>( 1024u );
constexpr usize CY_GIB = CY_MIB * static_cast<usize>( 1024u );
constexpr usize CY_TIB = CY_GIB * static_cast<usize>( 1024u );

// Conservative fallback used before runtime CPU cache information is available.
constexpr usize CY_DEFAULT_CACHE_LINE_SIZE = 64u;
constexpr usize CY_INVALID_SIZE = std::numeric_limits<usize>::max();

// Compatibility aliases. New code should use the binary IEC names above.
constexpr usize CY_KB = CY_KIB;
constexpr usize CY_MB = CY_MIB;
constexpr usize CY_GB = CY_GIB;
constexpr usize CY_TB = CY_TIB;
constexpr usize CY_CACHE_LINE_SIZE = CY_DEFAULT_CACHE_LINE_SIZE;

/*
================
Semantic Storage Types
================
*/
// Stable 32-bit Boolean storage. Callers must normalize values to zero or one.
using b32 = u32;

using flags8_t  = u8;
using flags16_t = u16;
using flags32_t = u32;
using flags64_t = u64;

using hash32_t  = u32;
using hash64_t  = u64;
using crc32_t   = u32;
using fourcc_t  = u32;

using offset_t  = u64;
using byte_count_t = u64;
using alignment_t = usize;

using version_t = u32;
using format_version_t = u32;

/*
================
Invalid Semantic Values
================
*/
constexpr fourcc_t CY_INVALID_FOURCC = 0u;

constexpr offset_t CY_INVALID_OFFSET = std::numeric_limits<offset_t>::max();
constexpr version_t CY_INVALID_VERSION = 0u;
constexpr format_version_t CY_INVALID_FORMAT_VERSION = 0u;

/*
================
Primitive Limits
================
*/
constexpr u8 CY_U8_MAX = std::numeric_limits<u8>::max();
constexpr u8 CY_U8_MIN = std::numeric_limits<u8>::min();
constexpr u16 CY_U16_MAX = std::numeric_limits<u16>::max();
constexpr u16 CY_U16_MIN = std::numeric_limits<u16>::min();
constexpr u32 CY_U32_MAX = std::numeric_limits<u32>::max();
constexpr u32 CY_U32_MIN = std::numeric_limits<u32>::min();
constexpr u64 CY_U64_MAX = std::numeric_limits<u64>::max();
constexpr u64 CY_U64_MIN = std::numeric_limits<u64>::min();

constexpr i8 CY_I8_MAX = std::numeric_limits<i8>::max();
constexpr i8 CY_I8_MIN = std::numeric_limits<i8>::min();
constexpr i16 CY_I16_MAX = std::numeric_limits<i16>::max();
constexpr i16 CY_I16_MIN = std::numeric_limits<i16>::min();
constexpr i32 CY_I32_MAX = std::numeric_limits<i32>::max();
constexpr i32 CY_I32_MIN = std::numeric_limits<i32>::min();
constexpr i64 CY_I64_MAX = std::numeric_limits<i64>::max();
constexpr i64 CY_I64_MIN = std::numeric_limits<i64>::min();

constexpr usize CY_USIZE_MAX = std::numeric_limits<usize>::max();
constexpr usize CY_USIZE_MIN = std::numeric_limits<usize>::min();
constexpr isize CY_ISIZE_MAX = std::numeric_limits<isize>::max();
constexpr isize CY_ISIZE_MIN = std::numeric_limits<isize>::min();

constexpr f32 CY_F32_MAX = std::numeric_limits<f32>::max();
constexpr f32 CY_F32_LOWEST = std::numeric_limits<f32>::lowest();
constexpr f32 CY_F32_EPSILON = std::numeric_limits<f32>::epsilon();
constexpr f32 CY_F32_INFINITY = std::numeric_limits<f32>::infinity();

constexpr f64 CY_F64_MAX = std::numeric_limits<f64>::max();
constexpr f64 CY_F64_LOWEST = std::numeric_limits<f64>::lowest();
constexpr f64 CY_F64_EPSILON = std::numeric_limits<f64>::epsilon();
constexpr f64 CY_F64_INFINITY = std::numeric_limits<f64>::infinity();

/*
================
Layout Checks
================
*/
static_assert( sizeof( i8 ) == 1, "i8 must be 1 byte." );
static_assert( sizeof( i16 ) == 2, "i16 must be 2 bytes." );
static_assert( sizeof( i32 ) == 4, "i32 must be 4 bytes." );
static_assert( sizeof( i64 ) == 8, "i64 must be 8 bytes." );

static_assert( sizeof( u8 ) == 1, "u8 must be 1 byte." );
static_assert( sizeof( u16 ) == 2, "u16 must be 2 bytes." );
static_assert( sizeof( u32 ) == 4, "u32 must be 4 bytes." );
static_assert( sizeof( u64 ) == 8, "u64 must be 8 bytes." );

static_assert( sizeof( f32 ) == 4, "f32 must be 4 bytes." );
static_assert( sizeof( f64 ) == 8, "f64 must be 8 bytes." );
static_assert( std::numeric_limits<f32>::is_iec559, "f32 must use IEEE-754 representation." );
static_assert( std::numeric_limits<f64>::is_iec559, "f64 must use IEEE-754 representation." );

static_assert( sizeof( byte ) == 1, "byte must be 1 byte." );
static_assert( sizeof( b8 ) == 1, "b8 must be 1 byte." );
static_assert( CHAR_BIT == 8, "CypherEngine requires 8-bit bytes." );
static_assert( std::is_signed_v<i8>, "i8 must be signed." );
static_assert( std::is_unsigned_v<u8>, "u8 must be unsigned." );

static_assert( sizeof( b32 ) == 4, "b32 must be 4 bytes." );
static_assert( sizeof( usize ) == 8, "CypherEngine requires a 64-bit size type." );
static_assert( sizeof( isize ) == 8, "CypherEngine requires a 64-bit pointer-difference type." );
static_assert( sizeof( uintptr ) == sizeof( void * ), "uintptr must exactly match pointer width." );
static_assert( sizeof( intptr ) == sizeof( void * ), "intptr must exactly match pointer width." );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_BASETYPES_H
