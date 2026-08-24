//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_MemoryOps.h
//  Purpose: Declares raw byte operations and checked typed-array byte counts.
//  Details: These routines do not construct, destroy, allocate, or validate object
//           lifetimes. Typed helpers are restricted to trivially copyable values.
//
//  History:
//  - Created by Karlo Siric on 2026-06-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_MEMORYOPS_H
#define CYPHER_COMMON_TIER0_MEMORYOPS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Memory Ops

Raw byte memory operations. This is not an allocator layer.
================
*/

#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_API.h"

#include <type_traits>

namespace cypher::common
{

// Copies byte_count bytes from src to dst; ranges must not overlap.
CYPHER_COMMON_API void *Cy_MemCopy( void *pDst, const void *pSrc, usize nByteCount ) noexcept;

// Moves byte_count bytes from src to dst; ranges may overlap.
CYPHER_COMMON_API void *Cy_MemMove( void *pDst, const void *pSrc, usize nByteCount ) noexcept;

// Fills byte_count bytes at dst with the specified byte value.
CYPHER_COMMON_API void *Cy_MemSet( void *pDst, u8 nValue, usize nByteCount ) noexcept;

// Clears byte_count bytes at dst to zero.
CYPHER_COMMON_API void *Cy_MemZero( void *pDst, usize nByteCount ) noexcept;

// Compares two byte ranges like memcmp.
CYPHER_NODISCARD CYPHER_COMMON_API i32 Cy_MemCompare(
    const void *pA,
    const void *pB,
    usize nByteCount ) noexcept;

// Returns true when both byte ranges are identical.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MemEqual(
    const void *pA,
    const void *pB,
    usize nByteCount ) noexcept;

// Returns true when two byte ranges overlap in memory.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MemRangesOverlap(
    const void *pA,
    usize nABytes,
    const void *pB,
    usize nBBytes ) noexcept;

// Returns true when pPtr points inside [pBase, pBase + nRangeBytes).
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MemPointerInRange(
    const void *pPtr,
    const void *pBase,
    usize nRangeBytes ) noexcept;

// Returns true when every byte in the range is zero.
CYPHER_NODISCARD CYPHER_COMMON_API bool_t Cy_MemIsZero(
    const void *pData,
    usize nByteCount ) noexcept;

// Computes element_count * sizeof(type_t) without unsigned overflow. This check is
// required before accepting counts read from files, packets, or tool input.
template <typename type_t>
CYPHER_NODISCARD constexpr bool_t Cy_TryArrayByteCount(
    usize nElementCount,
    usize &nOutByteCount ) noexcept
{
    if ( nElementCount > CY_USIZE_MAX / sizeof( type_t ) ) {
        nOutByteCount = 0u;
        return CY_FALSE;
    }

    nOutByteCount = sizeof( type_t ) * nElementCount;
    return CY_TRUE;
}

// Clears a trivially copyable object to zero bytes. Zero bytes are not guaranteed
// to represent a valid semantic value for every trivially copyable type.
template <typename type_t>
inline void Cy_ZeroStruct( type_t &value ) noexcept
{
    static_assert( std::is_trivially_copyable_v<type_t>, "Cy_ZeroStruct requires a trivially copyable type." );
    Cy_MemZero( &value, sizeof( value ) );
}

// Clears a fixed-size array of trivially copyable objects to zero bytes.
template <typename type_t, usize nCount>
inline void Cy_ZeroArray( type_t ( &values )[nCount] ) noexcept
{
    static_assert( std::is_trivially_copyable_v<type_t>, "Cy_ZeroArray requires a trivially copyable type." );
    Cy_MemZero( values, sizeof( values ) );
}

// Clears nCount trivially copyable objects to zero bytes.
template <typename type_t>
inline void Cy_ZeroArray( type_t *pValues, usize nCount ) noexcept
{
    static_assert( std::is_trivially_copyable_v<type_t>, "Cy_ZeroArray requires a trivially copyable type." );
    usize nByteCount = 0u;
    if ( nCount == 0u || !Cy_TryArrayByteCount<type_t>( nCount, nByteCount ) ) {
        return;
    }
    Cy_MemZero( pValues, nByteCount );
}

// Copies nCount trivially copyable objects; ranges must not overlap.
template <typename type_t>
inline void Cy_CopyArray( type_t *pDst, const type_t *pSrc, usize nCount ) noexcept
{
    static_assert( std::is_trivially_copyable_v<type_t>, "Cy_CopyArray requires a trivially copyable type." );
    usize nByteCount = 0u;
    if ( nCount == 0u || !Cy_TryArrayByteCount<type_t>( nCount, nByteCount ) ) {
        return;
    }
    Cy_MemCopy( pDst, pSrc, nByteCount );
}

// Moves nCount trivially copyable objects; ranges may overlap.
template <typename type_t>
inline void Cy_MoveArray( type_t *pDst, const type_t *pSrc, usize nCount ) noexcept
{
    static_assert( std::is_trivially_copyable_v<type_t>, "Cy_MoveArray requires a trivially copyable type." );
    usize nByteCount = 0u;
    if ( nCount == 0u || !Cy_TryArrayByteCount<type_t>( nCount, nByteCount ) ) {
        return;
    }
    Cy_MemMove( pDst, pSrc, nByteCount );
}

// Returns true when all bytes in a trivially copyable object are zero.
template <typename type_t>
CYPHER_NODISCARD inline bool_t Cy_StructIsZero( const type_t &value ) noexcept
{
    static_assert( std::is_trivially_copyable_v<type_t>, "Cy_StructIsZero requires a trivially copyable type." );
    return Cy_MemIsZero( &value, sizeof( value ) );
}

// Returns true when all bytes in nCount trivially copyable objects are zero.
template <typename type_t>
CYPHER_NODISCARD inline bool_t Cy_ArrayIsZero( const type_t *pValues, usize nCount ) noexcept
{
    static_assert( std::is_trivially_copyable_v<type_t>, "Cy_ArrayIsZero requires a trivially copyable type." );

    if ( nCount == 0u ) {
        return CY_TRUE;
    }

    usize nByteCount = 0u;
    if ( !Cy_TryArrayByteCount<type_t>( nCount, nByteCount ) ) {
        return CY_FALSE;
    }

    return Cy_MemIsZero( pValues, nByteCount );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MEMORYOPS_H
