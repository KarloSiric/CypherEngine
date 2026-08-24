//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HeapSort.cpp
//  Purpose: Implements raw constant-space heap sorting.
//  Details: Opaque records are exchanged byte-by-byte so no allocator or fixed-size
//           temporary buffer is required.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Heap Sort Implementation Notes

Algorithms operate only on the supplied range and callback contracts. Comparators must define a
consistent ordering; the implementation performs no hidden allocation unless explicitly
documented.
================
*/

#include "CypherCommon_HeapSort.h"

namespace cypher::common
{

namespace
{

void RawSwap( byte *pLeft, byte *pRight, usize cbElement ) noexcept
{
    if ( pLeft == pRight ) {
        return;
    }
    // Raw sorting cannot assume a maximum record size, so swap in place one byte
    // at a time and keep the algorithm allocation-free.
    for ( usize iByte = 0u; iByte < cbElement; ++iByte ) {
        const byte temporary = pLeft[iByte];
        pLeft[iByte] = pRight[iByte];
        pRight[iByte] = temporary;
    }
}

void RawSiftDown(
    byte *pData,
    usize iRoot,
    usize nCount,
    usize cbElement,
    sort_compare_fn_t pCompare,
    void *pUserData ) noexcept
{
    for ( ;; ) {
        if ( iRoot >= nCount / 2u ) {
            return;
        }
        const usize iLeft = iRoot * 2u + 1u;
        usize iLargest = iLeft;
        const usize iRight = iLeft + 1u;
        if ( iRight < nCount &&
             pCompare(
                 pData + iLargest * cbElement,
                 pData + iRight * cbElement,
                 pUserData ) < 0 ) {
            iLargest = iRight;
        }
        if ( pCompare(
                 pData + iRoot * cbElement,
                 pData + iLargest * cbElement,
                 pUserData ) >= 0 ) {
            return;
        }
        RawSwap(
            pData + iRoot * cbElement,
            pData + iLargest * cbElement,
            cbElement );
        iRoot = iLargest;
    }
}

} // namespace

bool_t HeapSort_Raw(
    void *pData,
    usize nCount,
    usize cbElement,
    sort_compare_fn_t pCompare,
    void *pUserData ) noexcept
{
    if ( cbElement == 0u || pCompare == nullptr ||
         ( pData == nullptr && nCount != 0u ) ||
         nCount > CY_USIZE_MAX / cbElement ) {
        return CY_FALSE;
    }
    if ( nCount < 2u ) {
        return CY_TRUE;
    }

    auto *pBytes = static_cast<byte *>( pData );
    // First build a max heap, then repeatedly place its root at the sorted tail.
    for ( usize iRoot = nCount / 2u; iRoot > 0u; --iRoot ) {
        RawSiftDown(
            pBytes,
            iRoot - 1u,
            nCount,
            cbElement,
            pCompare,
            pUserData );
    }
    for ( usize nHeap = nCount; nHeap > 1u; --nHeap ) {
        RawSwap( pBytes, pBytes + ( nHeap - 1u ) * cbElement, cbElement );
        RawSiftDown(
            pBytes,
            0u,
            nHeap - 1u,
            cbElement,
            pCompare,
            pUserData );
    }
    return CY_TRUE;
}

} // namespace cypher::common
