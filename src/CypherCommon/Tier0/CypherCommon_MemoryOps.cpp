//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_MemoryOps.cpp
//  Purpose: Implements raw byte operations and overflow-safe range predicates.
//  Details: Wrappers preserve libc semantics while giving engine code one stable
//           vocabulary and zero-length behavior across every supported platform.
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

//-----------------------------------------------------------------------------
// Raw byte operations
//
// These wrappers retain the corresponding libc overlap rules. Range helpers do
// their arithmetic in uintptr space so they can reject wraparound before an end
// address is formed.
//-----------------------------------------------------------------------------

void *Cy_MemCopy( void *pDst, const void *pSrc, usize nByteCount ) noexcept
{
    if ( nByteCount == 0u ) {
        return pDst;
    }
    return std::memcpy( pDst, pSrc, nByteCount );
}

void *Cy_MemMove( void *pDst, const void *pSrc, usize nByteCount ) noexcept
{
    if ( nByteCount == 0u ) {
        return pDst;
    }
    return std::memmove( pDst, pSrc, nByteCount );
}

void *Cy_MemSet( void *pDst, u8 nValue, usize nByteCount ) noexcept
{
    if ( nByteCount == 0u ) {
        return pDst;
    }
    return std::memset( pDst, nValue, nByteCount );
}

void *Cy_MemZero( void *pDst, usize nByteCount ) noexcept
{
    return Cy_MemSet( pDst, 0, nByteCount );
}

i32 Cy_MemCompare( const void *pA, const void *pB, usize nByteCount ) noexcept
{
    if ( nByteCount == 0u ) {
        return 0;
    }
    return std::memcmp( pA, pB, nByteCount );
}

bool_t Cy_MemEqual( const void *pA, const void *pB, usize nByteCount ) noexcept
{
    return Cy_MemCompare( pA, pB, nByteCount ) == 0;
}

bool_t Cy_MemPointerInRange( const void *pPtr,
                          const void *pBase,
                          usize nRangeBytes ) noexcept
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

bool_t Cy_MemRangesOverlap( const void *pA, usize nABytes,
                         const void *pB, usize nBBytes ) noexcept
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

bool_t Cy_MemIsZero( const void *pData, usize nByteCount ) noexcept
{
    // Compare against one shared read-only zero page in bounded chunks. This avoids
    // byte-at-a-time scanning while keeping stack use independent of input size.
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
