//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Sort.cpp
//  Purpose: Implements raw unstable and stable sorting.
//  Details: Unstable sorting uses constant-space heap sort. Stable sorting uses a
//           caller-provided full-size scratch buffer and bottom-up merge passes.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Sort.h"

#include "CypherCommon_HeapSort.h"

#include <limits>

namespace cypher::common
{

namespace
{

bool_t RawSortArgumentsAreValid(
    const void *pData,
    usize nCount,
    usize cbElement,
    sort_compare_fn_t pCompare ) noexcept
{
    return cbElement > 0u && pCompare != nullptr &&
           ( pData != nullptr || nCount == 0u ) &&
           nCount <= CY_USIZE_MAX / cbElement;
}

bool_t MemoryRangesOverlap(
    const byte *pLeft,
    usize cbLeft,
    const byte *pRight,
    usize cbRight ) noexcept
{
    if ( cbLeft == 0u || cbRight == 0u ) {
        return CY_FALSE;
    }
    const uintptr nLeft = reinterpret_cast<uintptr>( pLeft );
    const uintptr nRight = reinterpret_cast<uintptr>( pRight );
    constexpr uintptr nMaximumAddress = std::numeric_limits<uintptr>::max();
    if ( nLeft > nMaximumAddress - cbLeft || nRight > nMaximumAddress - cbRight ) {
        return CY_TRUE;
    }
    return nLeft < nRight + cbRight && nRight < nLeft + cbLeft;
}

} // namespace

bool_t Sort_UnstableRaw(
    void *pData,
    usize nCount,
    usize cbElement,
    sort_compare_fn_t pCompare,
    void *pUserData ) noexcept
{
    if ( !RawSortArgumentsAreValid( pData, nCount, cbElement, pCompare ) ) {
        return CY_FALSE;
    }
    return HeapSort_Raw( pData, nCount, cbElement, pCompare, pUserData );
}

bool_t Sort_StableRaw(
    void *pData,
    usize nCount,
    usize cbElement,
    sort_compare_fn_t pCompare,
    void *pUserData,
    byte_span_t scratch ) noexcept
{
    if ( !RawSortArgumentsAreValid( pData, nCount, cbElement, pCompare ) ||
         !Span_IsValid( scratch ) ) {
        return CY_FALSE;
    }
    if ( nCount < 2u ) {
        return CY_TRUE;
    }
    const usize cbRequired = nCount * cbElement;
    auto *pBytes = static_cast<byte *>( pData );
    if ( scratch.nCount < cbRequired ||
         MemoryRangesOverlap( pBytes, cbRequired, scratch.pData, scratch.nCount ) ) {
        return CY_FALSE;
    }

    byte *pSource = pBytes;
    byte *pDest = scratch.pData;
    for ( usize nWidth = 1u; nWidth < nCount; ) {
        for ( usize iRun = 0u; iRun < nCount; ) {
            const usize iMiddle = nWidth < nCount - iRun
                ? iRun + nWidth
                : nCount;
            const usize iEnd = nWidth < nCount - iMiddle
                ? iMiddle + nWidth
                : nCount;
            usize iLeft = iRun;
            usize iRight = iMiddle;
            usize iOutput = iRun;
            while ( iLeft < iMiddle && iRight < iEnd ) {
                const byte *pLeft = pSource + iLeft * cbElement;
                const byte *pRight = pSource + iRight * cbElement;
                const bool_t bTakeLeft = pCompare( pLeft, pRight, pUserData ) <= 0;
                const usize iSelected = bTakeLeft ? iLeft++ : iRight++;
                Cy_MemCopy(
                    pDest + iOutput * cbElement,
                    pSource + iSelected * cbElement,
                    cbElement );
                ++iOutput;
            }
            while ( iLeft < iMiddle ) {
                Cy_MemCopy(
                    pDest + iOutput * cbElement,
                    pSource + iLeft * cbElement,
                    cbElement );
                ++iLeft;
                ++iOutput;
            }
            while ( iRight < iEnd ) {
                Cy_MemCopy(
                    pDest + iOutput * cbElement,
                    pSource + iRight * cbElement,
                    cbElement );
                ++iRight;
                ++iOutput;
            }
            iRun = iEnd;
        }

        byte *pTemporary = pSource;
        pSource = pDest;
        pDest = pTemporary;
        if ( nWidth > nCount / 2u ) {
            break;
        }
        nWidth *= 2u;
    }
    if ( pSource != pBytes ) {
        Cy_MemCopy( pBytes, pSource, cbRequired );
    }
    return CY_TRUE;
}

} // namespace cypher::common
