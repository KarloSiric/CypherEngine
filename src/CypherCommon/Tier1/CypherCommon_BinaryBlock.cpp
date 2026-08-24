//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BinaryBlock.cpp
//  Purpose: Implements immutable borrowed binary blocks.
//  Details: Binary blocks share Span's null-state and clamped slicing rules while
//           providing byte-oriented semantics to serialization and resource APIs.
//
//  History:
//  - Created by Karlo Siric on 2026-08-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Binary Block Implementation Notes

The cursor and capacity form one invariant: no operation may advance beyond the supplied
storage. Failed writes report the condition without publishing a cursor that claims unwritten
bytes.
================
*/

#include "CypherCommon_BinaryBlock.h"

namespace cypher::common
{

binary_block_t BinaryBlock_FromData(
    const void *pData,
    usize cbSize ) noexcept
{
    const bool_t bValidRange = pData != nullptr || cbSize == 0u;
    CY_ASSERT_MSG(
        bValidRange,
        "BinaryBlock_FromData requires non-null data for a non-empty block." );
    if ( !bValidRange ) {
        return {};
    }

    return {
        static_cast<const byte *>( pData ),
        cbSize
    };
}

binary_block_t BinaryBlock_FromSpan( const_byte_span_t bytes ) noexcept
{
    const bool_t bValidSpan = Span_IsValid( bytes );
    CY_ASSERT_MSG(
        bValidSpan,
        "BinaryBlock_FromSpan requires a valid byte span." );
    if ( !bValidSpan ) {
        return {};
    }

    return { bytes.pData, bytes.nCount };
}

bool_t BinaryBlock_IsValid( binary_block_t block ) noexcept
{
    return block.pData != nullptr || block.cbSize == 0u;
}

bool_t BinaryBlock_IsEmpty( binary_block_t block ) noexcept
{
    return block.cbSize == 0u;
}

binary_block_t BinaryBlock_Subblock(
    binary_block_t block,
    usize iOffset,
    usize cbSize ) noexcept
{
    const bool_t bValidBlock = BinaryBlock_IsValid( block );
    CY_ASSERT_MSG(
        bValidBlock,
        "BinaryBlock_Subblock requires a valid source block." );
    if ( !bValidBlock ) {
        return {};
    }

    const bool_t bOffsetInRange = iOffset <= block.cbSize;
    CY_ASSERT_MSG(
        bOffsetInRange,
        "BinaryBlock_Subblock offset is outside the source block." );
    if ( !bOffsetInRange ) {
        iOffset = block.cbSize;
    }

    if ( block.pData == nullptr ) {
        return {};
    }

    // Length is clamped, but the starting offset is never silently corrected.
    const usize cbAvailable = block.cbSize - iOffset;
    const usize cbSubblock = cbSize < cbAvailable ? cbSize : cbAvailable;
    return { block.pData + iOffset, cbSubblock };
}

const_byte_span_t BinaryBlock_Span( binary_block_t block ) noexcept
{
    const bool_t bValidBlock = BinaryBlock_IsValid( block );
    CY_ASSERT_MSG(
        bValidBlock,
        "BinaryBlock_Span requires a valid source block." );
    if ( !bValidBlock ) {
        return {};
    }

    return { block.pData, block.cbSize };
}

} // namespace cypher::common
