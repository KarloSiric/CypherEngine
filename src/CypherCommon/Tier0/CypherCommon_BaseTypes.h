//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_BaseTypes.h
//  Purpose: Defines the fixed-width scalar and storage types used by CypherEngine.
//  Details: Layout checks at the end of this file enforce the runtime and file-format
//           assumptions shared by every engine module.
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

//=============================================================================
// Primitive engine-wide types. Keep this header free of allocation, containers,
// platform APIs, and subsystem policy.
//=============================================================================

#include "CypherCommon_Annotations.h"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace cypher::common
{

// Fixed-width integer types used by runtime state and serialized layouts.
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// Floating-point widths are checked for IEEE-754 representation below.
using f32 = float;
using f64 = double;

// Native size and pointer types; CypherEngine currently requires 64-bit targets.
using usize = std::size_t;
using isize = std::ptrdiff_t;
using uintptr = std::uintptr_t;
using intptr = std::intptr_t;

// Byte and narrow-text convenience aliases. These pointers never imply ownership.
using byte = u8;
using char8 = char;
using cstring = const char *;
using mstring = char *;

using bool_t = bool;

enum class b8 : u8 {
    False = 0u, // Stable false representation in one-byte storage.
    True = 1u   // Stable true representation in one-byte storage.
};

constexpr bool_t CY_FALSE = false;
constexpr bool_t CY_TRUE = true;

CYPHER_NODISCARD constexpr bool_t Cy_B8ToBool( b8 value ) noexcept
{
    return value == b8::True;
}

CYPHER_NODISCARD constexpr b8 Cy_B8FromBool( bool_t value ) noexcept
{
    return value ? b8::True : b8::False;
}

// Generic identifiers are storage vocabulary only; each owning subsystem defines
// the meaning and lifetime of its indices and generations.
using index_t = u32;       // Dense table or slot index.
using generation_t = u32;  // Reuse counter used to reject stale handles.
using handle_t = u32;      // Generic opaque 32-bit handle storage.
using frame_index_t = u64; // Monotonic frame sequence; wrapping is permitted.

constexpr index_t CY_INVALID_INDEX = std::numeric_limits<index_t>::max();            // All-one index sentinel.
constexpr generation_t CY_INVALID_GENERATION = std::numeric_limits<generation_t>::max(); // All-one generation sentinel.
constexpr handle_t CY_INVALID_HANDLE = 0u;                                           // Zero never identifies a live handle.
constexpr frame_index_t CY_INVALID_FRAME_INDEX = std::numeric_limits<frame_index_t>::max();

constexpr usize CY_BITS_PER_BYTE = static_cast<usize>( CHAR_BIT ); // Required to be eight below.
constexpr usize CY_KIB = static_cast<usize>( 1024u );
constexpr usize CY_MIB = CY_KIB * static_cast<usize>( 1024u );
constexpr usize CY_GIB = CY_MIB * static_cast<usize>( 1024u );
constexpr usize CY_TIB = CY_GIB * static_cast<usize>( 1024u );

constexpr usize CY_DEFAULT_CACHE_LINE_SIZE = 64u;                 // Fallback until runtime CPU detection completes.
constexpr usize CY_INVALID_SIZE = std::numeric_limits<usize>::max(); // All-one size sentinel.

// Compatibility aliases. New code should use the binary IEC names above.
constexpr usize CY_KB = CY_KIB;
constexpr usize CY_MB = CY_MIB;
constexpr usize CY_GB = CY_GIB;
constexpr usize CY_TB = CY_TIB;
constexpr usize CY_CACHE_LINE_SIZE = CY_DEFAULT_CACHE_LINE_SIZE;

using b32 = u32; // Stable 32-bit Boolean storage; values must be zero or one.

using flags8_t  = u8;
using flags16_t = u16;
using flags32_t = u32;
using flags64_t = u64;

using hash32_t  = u32;
using hash64_t  = u64;
using crc32_t   = u32;
using fourcc_t  = u32;

using offset_t = u64;     // Byte offset in a stream or serialized resource.
using byte_count_t = u64; // Byte count independent of the host size type.
using alignment_t = usize;

using version_t = u32;
using format_version_t = u32;

constexpr fourcc_t CY_INVALID_FOURCC = 0u; // Four zero bytes never name a valid format.

constexpr offset_t CY_INVALID_OFFSET = std::numeric_limits<offset_t>::max();
constexpr version_t CY_INVALID_VERSION = 0u;
constexpr format_version_t CY_INVALID_FORMAT_VERSION = 0u;

// Primitive limits are named here so low-level code does not mix STL spellings
// with the engine's fixed-width vocabulary.
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

// WARNING: Serialized formats and public module boundaries depend on these sizes.
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
