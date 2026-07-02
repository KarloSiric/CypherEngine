//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_MemoryOps.cpp
//  Purpose: Implements CypherCommon Tier0 MemoryOps support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-07-01
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_MemoryOps.h"

#include <cstring>

namespace cypher::common
{

void *MemCopy( void *pDst, const void *pSrc, usize nByteCount )
{
    if ( nByteCount == 0u ) {
        return pDst;
    }
    return std::memcpy( pDst, pSrc, nByteCount );
}

void *MemMove( void *pDst, const void *pSrc, usize nByteCount )
{
    if ( nByteCount == 0u ) {
        return pDst;
    }
    return std::memmove( pDst, pSrc, nByteCount );
}

void *MemSet( void *pDst, i32 nValue, usize nByteCount )
{
    if ( nByteCount == 0u ) {
        return pDst;
    }
    return std::memset( pDst, nValue, nByteCount );
}

void *MemZero( void *pDst, usize nByteCount )
{
    return MemSet( pDst, 0, nByteCount );
}

i32 MemCompare( const void *pA, const void *pB, usize nByteCount )
{
    if ( nByteCount == 0u ) {
        return 0;
    }
    return std::memcmp( pA, pB, nByteCount );
}

bool_t MemEqual( const void *pA, const void *pB, usize nByteCount )
{
    return MemCompare( pA, pB, nByteCount ) == 0;
}

bool_t MemPointerInRange( const void *pPtr,
                          const void *pBase,
                          usize nRangeBytes )
{
    if ( pPtr == nullptr || pBase == nullptr || nRangeBytes == 0u ) {
        return CY_FALSE;
    }
    const uintptr nPtrAddress = reinterpret_cast<uintptr>( pPtr );
    const uintptr nBaseAddress = reinterpret_cast<uintptr>( pBase );
    if ( nPtrAddress < nBaseAddress ) {
        return CY_FALSE;
    }
    const uintptr nOffset = nPtrAddress - nBaseAddress;
    return nOffset < nRangeBytes;
}

bool_t MemRangesOverlap( const void *pA, usize nABytes,
                         const void *pB, usize nBBytes )
{
    if ( pA == nullptr || pB == nullptr || nABytes == 0u || nBBytes == 0u ) {
        return CY_FALSE;
    }

    const uintptr nA = reinterpret_cast<uintptr>( pA );
    const uintptr nB = reinterpret_cast<uintptr>( pB );

    if ( nA <= nB ) {
        return ( nB - nA ) < nABytes;
    }

    return ( nA - nB ) < nBBytes;
}

bool_t MemIsZero( const void *pData, usize nByteCount )
{
    static constexpr usize nZeroBlockBytes = 4096u;
    static constexpr u8 nZeroBlock[nZeroBlockBytes] = {};

    if ( nByteCount == 0u ) {
        return CY_TRUE;
    }

    if ( pData == nullptr ) {
        return CY_FALSE;
    }

    const u8 *pBytes = static_cast<const u8 *>( pData );

    while ( nByteCount > 0u ) {
        const usize nChunkBytes = nByteCount < nZeroBlockBytes ? nByteCount : nZeroBlockBytes;
        if ( std::memcmp( pBytes, nZeroBlock, nChunkBytes ) != 0 ) {
            return CY_FALSE;
        }
        pBytes += nChunkBytes;
        nByteCount -= nChunkBytes;
    }

    return CY_TRUE;
}

} // namespace cypher::common
