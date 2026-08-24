//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Defines.h
//  Purpose: Defines the small portable macro vocabulary shared by engine modules.
//  Details: Compiler-specific spelling lives here instead of leaking into subsystems.
//
//  History:
//  - Created by Karlo Siric on 2026-06-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_DEFINES_H
#define CYPHER_COMMON_TIER0_DEFINES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Platform.h"

#include <cstddef>

namespace cypher::common::defines_detail
{

// Returns the compile-time element count of a native array.
template <typename type_t, usize nCount>
CYPHER_NODISCARD constexpr usize ArrayCount( const type_t ( & )[nCount] ) noexcept
{
    return nCount;
}

// Builds a 32-bit mask without performing an invalid shift.
CYPHER_NODISCARD constexpr u32 Cy_Bit32( u32 nBit ) noexcept
{
    return nBit < 32u ? ( 1u << nBit ) : 0u;
}

// Builds a 64-bit mask without performing an invalid shift.
CYPHER_NODISCARD constexpr u64 Cy_Bit64( u32 nBit ) noexcept
{
    return nBit < 64u ? ( 1ull << nBit ) : 0ull;
}

} // namespace cypher::common::defines_detail

// Two-stage expansion is required so macro arguments expand before stringizing
// or token concatenation.
#define CYPHER_STRINGIFY_IMPL( x )          #x
#define CYPHER_STRINGIFY( x )               CYPHER_STRINGIFY_IMPL( x )

#define CYPHER_JOIN_IMPL( a, b )            a##b
#define CYPHER_JOIN( a, b )                 CYPHER_JOIN_IMPL( a, b )

// General utility macros.
#define CYPHER_UNUSED( x )                  ( void )( x )
#define CYPHER_ARRAY_COUNT( array )         ::cypher::common::defines_detail::ArrayCount( array )

#define CYPHER_BIT32( bit )                 ::cypher::common::defines_detail::Cy_Bit32( bit )
#define CYPHER_BIT64( bit )                 ::cypher::common::defines_detail::Cy_Bit64( bit )

#define CYPHER_KIB( n )                     ( static_cast<::cypher::common::u64>( n ) * 1024ull )
#define CYPHER_MIB( n )                     ( CYPHER_KIB( n ) * 1024ull )
#define CYPHER_GIB( n )                     ( CYPHER_MIB( n ) * 1024ull )
#define CYPHER_TIB( n )                     ( CYPHER_GIB( n ) * 1024ull )

// Compatibility spellings. New code should use the explicit IEC names above.
#define CYPHER_KB( n )                      CYPHER_KIB( n )
#define CYPHER_MB( n )                      CYPHER_MIB( n )
#define CYPHER_GB( n )                      CYPHER_GIB( n )

// Compiler attributes used on hot paths and alias-sensitive memory code.
#if CYPHER_COMPILER_MSVC_ABI
    #define CYPHER_FORCE_INLINE             __forceinline
    #define CYPHER_NO_INLINE                __declspec( noinline )
    #define CYPHER_RESTRICT                 __restrict
#elif CYPHER_COMPILER_CLANG || CYPHER_COMPILER_GCC
    #define CYPHER_FORCE_INLINE             inline __attribute__(( always_inline ))
    #define CYPHER_NO_INLINE                __attribute__(( noinline ))
    #define CYPHER_RESTRICT                 __restrict__
#else
    #error "Unsupported compiler for Cypher defines."
#endif

#define CYPHER_ALIGNAS( n )                 alignas( n )
#define CYPHER_ALIGNOF( type )              alignof( type )

// Raw source-location spellings used where std::source_location is unsuitable.
#define CYPHER_FILE                         __FILE__
#define CYPHER_LINE                         __LINE__

#if CYPHER_COMPILER_MSVC_ABI
    #define CYPHER_FUNCTION_NAME            __FUNCTION__
#else
    #define CYPHER_FUNCTION_NAME            __func__
#endif

// Branch hints affect optimization only; each argument is still evaluated once.
#if CYPHER_COMPILER_CLANG || CYPHER_COMPILER_GCC
    #define CYPHER_LIKELY( x )              __builtin_expect( !!( x ), 1 )
    #define CYPHER_UNLIKELY( x )            __builtin_expect( !!( x ), 0 )
#else
    #define CYPHER_LIKELY( x )              ( x )
    #define CYPHER_UNLIKELY( x )            ( x )
#endif

// Explicitly suppress accidental ownership copies or moves.
#define CYPHER_NO_COPY( type )              \
    type( const type & ) = delete;          \
    type &operator=( const type & ) = delete

#define CYPHER_NO_MOVE( type )              \
    type( type && ) = delete;               \
    type &operator=( type && ) = delete

#define CYPHER_NO_COPY_MOVE( type )         \
    CYPHER_NO_COPY( type );                 \
    CYPHER_NO_MOVE( type )

#define CYPHER_OFFSETOF( type, member )     offsetof( type, member ) // Standard-layout types only.

#endif // CYPHER_COMMON_TIER0_DEFINES_H
