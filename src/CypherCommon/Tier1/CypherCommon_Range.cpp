//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Range.cpp
//  Purpose: Implements overflow-aware index and byte ranges.
//  Details: Half-open range operations centralize safe offset/count arithmetic for
//           containers, streams, packets, files, and serialized asset formats.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Range.h"

namespace cypher::common
{

bool_t IndexRange_IsValid( index_range_t range ) noexcept
{
    return range.nCount <= CY_USIZE_MAX - range.iFirst;
}

usize IndexRange_End( index_range_t range ) noexcept
{
    const bool_t bValidRange = IndexRange_IsValid( range );
    CY_ASSERT_MSG( bValidRange, "IndexRange_End received an overflowing range." );
    return bValidRange ? range.iFirst + range.nCount : CY_INVALID_SIZE;
}

bool_t IndexRange_Contains(
    index_range_t range,
    usize iIndex ) noexcept
{
    const bool_t bValidRange = IndexRange_IsValid( range );
    CY_ASSERT_MSG( bValidRange, "IndexRange_Contains received an overflowing range." );
    if ( !bValidRange ) {
        return CY_FALSE;
    }

    return iIndex >= range.iFirst &&
           iIndex < range.iFirst + range.nCount;
}

bool_t IndexRange_ContainsRange(
    index_range_t outer,
    index_range_t inner ) noexcept
{
    const bool_t bValidOuter = IndexRange_IsValid( outer );
    const bool_t bValidInner = IndexRange_IsValid( inner );
    CY_ASSERT_MSG( bValidOuter, "IndexRange_ContainsRange received an overflowing outer range." );
    CY_ASSERT_MSG( bValidInner, "IndexRange_ContainsRange received an overflowing inner range." );
    if ( !bValidOuter || !bValidInner ) {
        return CY_FALSE;
    }

    const usize iOuterEnd = outer.iFirst + outer.nCount;
    const usize iInnerEnd = inner.iFirst + inner.nCount;
    return inner.iFirst >= outer.iFirst &&
           iInnerEnd <= iOuterEnd;
}

index_range_t IndexRange_Intersection(
    index_range_t rangeA,
    index_range_t rangeB ) noexcept
{
    const bool_t bValidA = IndexRange_IsValid( rangeA );
    const bool_t bValidB = IndexRange_IsValid( rangeB );
    CY_ASSERT_MSG( bValidA, "IndexRange_Intersection received an overflowing first range." );
    CY_ASSERT_MSG( bValidB, "IndexRange_Intersection received an overflowing second range." );
    if ( !bValidA || !bValidB ) {
        return {};
    }

    const usize iStart = rangeA.iFirst > rangeB.iFirst
        ? rangeA.iFirst
        : rangeB.iFirst;
    const usize iEndA = rangeA.iFirst + rangeA.nCount;
    const usize iEndB = rangeB.iFirst + rangeB.nCount;
    const usize iEnd = iEndA < iEndB ? iEndA : iEndB;
    return {
        iStart,
        iEnd > iStart ? iEnd - iStart : 0u
    };
}

bool_t ByteRange_IsValid( byte_range_t range ) noexcept
{
    return range.cbSize <= CY_USIZE_MAX - range.iOffset;
}

usize ByteRange_End( byte_range_t range ) noexcept
{
    const bool_t bValidRange = ByteRange_IsValid( range );
    CY_ASSERT_MSG( bValidRange, "ByteRange_End received an overflowing range." );
    return bValidRange ? range.iOffset + range.cbSize : CY_INVALID_SIZE;
}

bool_t ByteRange_ContainsOffset(
    byte_range_t range,
    usize iOffset ) noexcept
{
    const bool_t bValidRange = ByteRange_IsValid( range );
    CY_ASSERT_MSG( bValidRange, "ByteRange_ContainsOffset received an overflowing range." );
    if ( !bValidRange ) {
        return CY_FALSE;
    }

    return iOffset >= range.iOffset &&
           iOffset < range.iOffset + range.cbSize;
}

bool_t ByteRange_ContainsRange(
    byte_range_t outer,
    byte_range_t inner ) noexcept
{
    const bool_t bValidOuter = ByteRange_IsValid( outer );
    const bool_t bValidInner = ByteRange_IsValid( inner );
    CY_ASSERT_MSG( bValidOuter, "ByteRange_ContainsRange received an overflowing outer range." );
    CY_ASSERT_MSG( bValidInner, "ByteRange_ContainsRange received an overflowing inner range." );
    if ( !bValidOuter || !bValidInner ) {
        return CY_FALSE;
    }

    const usize iOuterEnd = outer.iOffset + outer.cbSize;
    const usize iInnerEnd = inner.iOffset + inner.cbSize;
    return inner.iOffset >= outer.iOffset &&
           iInnerEnd <= iOuterEnd;
}

byte_range_t ByteRange_Intersection(
    byte_range_t rangeA,
    byte_range_t rangeB ) noexcept
{
    const bool_t bValidA = ByteRange_IsValid( rangeA );
    const bool_t bValidB = ByteRange_IsValid( rangeB );
    CY_ASSERT_MSG( bValidA, "ByteRange_Intersection received an overflowing first range." );
    CY_ASSERT_MSG( bValidB, "ByteRange_Intersection received an overflowing second range." );
    if ( !bValidA || !bValidB ) {
        return {};
    }

    const usize iStart = rangeA.iOffset > rangeB.iOffset
        ? rangeA.iOffset
        : rangeB.iOffset;
    const usize iEndA = rangeA.iOffset + rangeA.cbSize;
    const usize iEndB = rangeB.iOffset + rangeB.cbSize;
    const usize iEnd = iEndA < iEndB ? iEndA : iEndB;
    return {
        iStart,
        iEnd > iStart ? iEnd - iStart : 0u
    };
}

} // namespace cypher::common
