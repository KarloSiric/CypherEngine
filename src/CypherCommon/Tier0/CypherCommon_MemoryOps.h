#ifndef CYPHER_COMMON_TIER0_MEMORYOPS_H
#define CYPHER_COMMON_TIER0_MEMORYOPS_H
#pragma once

/*
================
CypherCommon Memory Ops

Raw byte memory operations. This is not an allocator layer.
================
*/

#include "CypherCommon_BaseTypes.h"

#include <type_traits>

namespace cypher::common
{

// Copies byte_count bytes from src to dst; ranges must not overlap.
void *MemCopy( void *pDst, const void *pSrc, usize nByteCount );

// Moves byte_count bytes from src to dst; ranges may overlap.
void *MemMove( void *pDst, const void *pSrc, usize nByteCount );

// Fills byte_count bytes at dst with value.
void *MemSet( void *pDst, i32 nValue, usize nByteCount );

// Clears byte_count bytes at dst to zero.
void *MemZero( void *pDst, usize nByteCount );

// Compares two byte ranges like memcmp.
i32 MemCompare( const void *pA, const void *pB, usize nByteCount );

// Returns true when both byte ranges are identical.
bool_t MemEqual( const void *pA, const void *pB, usize nByteCount );

// Returns true when two byte ranges overlap in memory.
bool_t MemRangesOverlap( const void *pA, usize nABytes,
                         const void *pB, usize nBBytes );

// Returns true when pPtr points inside [pBase, pBase + nRangeBytes).
bool_t MemPointerInRange( const void *pPtr,
                          const void *pBase,
                          usize nRangeBytes );

// Returns true when every byte in the range is zero.
bool_t MemIsZero( const void *pData, usize nByteCount );

// Clears a trivially copyable object to zero bytes.
template <typename type_t>
inline void ZeroStruct( type_t &value )
{
    static_assert( std::is_trivially_copyable_v<type_t>, "ZeroStruct requires a trivially copyable type." );
    MemZero( &value, sizeof( value ) );
}

// Clears a fixed-size array of trivially copyable objects to zero bytes.
template <typename type_t, usize nCount>
inline void ZeroArray( type_t ( &values )[nCount] )
{
    static_assert( std::is_trivially_copyable_v<type_t>, "ZeroArray requires a trivially copyable type." );
    MemZero( values, sizeof( values ) );
}

// Clears nCount trivially copyable objects to zero bytes.
template <typename type_t>
inline void ZeroArray( type_t *pValues, usize nCount )
{
    static_assert( std::is_trivially_copyable_v<type_t>, "ZeroArray requires a trivially copyable type." );
    if ( nCount == 0u ) {
        return;
    }
    MemZero( pValues, sizeof( type_t ) * nCount );
}

// Copies nCount trivially copyable objects; ranges must not overlap.
template <typename type_t>
inline void CopyArray( type_t *pDst, const type_t *pSrc, usize nCount )
{
    static_assert( std::is_trivially_copyable_v<type_t>, "CopyArray requires a trivially copyable type." );
    if ( nCount == 0u ) {
        return;
    }
    MemCopy( pDst, pSrc, sizeof( type_t ) * nCount );
}

// Moves nCount trivially copyable objects; ranges may overlap.
template <typename type_t>
inline void MoveArray( type_t *pDst, const type_t *pSrc, usize nCount )
{
    static_assert( std::is_trivially_copyable_v<type_t>, "MoveArray requires a trivially copyable type." );
    if ( nCount == 0u ) {
        return;
    }
    MemMove( pDst, pSrc, sizeof( type_t ) * nCount );
}

// Returns true when all bytes in a trivially copyable object are zero.
template <typename type_t>
inline bool_t StructIsZero( const type_t &value )
{
    static_assert( std::is_trivially_copyable_v<type_t>, "StructIsZero requires a trivially copyable type." );
    return MemIsZero( &value, sizeof( value ) );
}

// Returns true when all bytes in nCount trivially copyable objects are zero.
template <typename type_t>
inline bool_t ArrayIsZero( const type_t *pValues, usize nCount )
{
    static_assert( std::is_trivially_copyable_v<type_t>, "ArrayIsZero requires a trivially copyable type." );

    if ( nCount == 0u ) {
        return CY_TRUE;
    }

    return MemIsZero( pValues, sizeof( type_t ) * nCount );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_MEMORYOPS_H
