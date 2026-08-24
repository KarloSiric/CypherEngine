//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BitReader.cpp
//  Purpose: Implements bounds-checked bitstream readers.
//  Details: Field reads are transactional and use an explicit physical and value-bit
//           order, allowing packet and cooked-data formats to define stable layouts.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BitReader.h"

#include <bit>

namespace cypher::common
{

namespace
{

// Failure status is sticky. A malformed field cannot be ignored accidentally by
// issuing another read without an explicit reset or ClearStatus operation.

bool_t IsBitOrderValid( bit_order_t bitOrder ) noexcept
{
    switch ( bitOrder ) {
        case bit_order_t::LEAST_SIGNIFICANT_FIRST:
        case bit_order_t::MOST_SIGNIFICANT_FIRST:
            return CY_TRUE;
    }
    return CY_FALSE;
}

bool_t IsBitCursorStatusValid( bit_cursor_status_t status ) noexcept
{
    switch ( status ) {
        case bit_cursor_status_t::OK:
        case bit_cursor_status_t::INVALID_ARGUMENT:
        case bit_cursor_status_t::OUT_OF_BOUNDS:
        case bit_cursor_status_t::INVALID_BIT_COUNT:
        case bit_cursor_status_t::VALUE_OUT_OF_RANGE:
        case bit_cursor_status_t::CURSOR_OVERFLOW:
            return CY_TRUE;
    }
    return CY_FALSE;
}

bool_t BitReaderFail(
    bit_reader_t *pReader,
    bit_cursor_status_t status ) noexcept
{
    if ( pReader != nullptr && pReader->status == bit_cursor_status_t::OK ) {
        pReader->status = status;
    }
    return CY_FALSE;
}

bool_t BitReaderCanRead( bit_reader_t *pReader, usize nBits ) noexcept
{
    const bool_t bValidReader = BitReader_IsValid( pReader );
    CY_ASSERT_MSG( bValidReader, "BitReader operation requires a valid reader." );
    if ( !bValidReader || pReader->status != bit_cursor_status_t::OK ) {
        return CY_FALSE;
    }
    if ( nBits > CY_USIZE_MAX - pReader->iBit ) {
        return BitReaderFail( pReader, bit_cursor_status_t::CURSOR_OVERFLOW );
    }
    if ( nBits > pReader->nBitSize - pReader->iBit ) {
        return BitReaderFail( pReader, bit_cursor_status_t::OUT_OF_BOUNDS );
    }
    return CY_TRUE;
}

u32 PhysicalBitIndex( usize iStreamBit, bit_order_t bitOrder ) noexcept
{
    const u32 iBitInByte = static_cast<u32>( iStreamBit % 8u );
    return bitOrder == bit_order_t::LEAST_SIGNIFICANT_FIRST
        ? iBitInByte
        : 7u - iBitInByte;
}

} // namespace

bool_t BitReader_Init(
    bit_reader_t *pReader,
    binary_block_t source,
    usize nBitSize,
    bit_order_t bitOrder ) noexcept
{
    const bool_t bValidReader = pReader != nullptr;
    const bool_t bValidSource = BinaryBlock_IsValid( source );
    const bool_t bValidOrder = IsBitOrderValid( bitOrder );
    const usize cbRequired =
        ( nBitSize / 8u ) + ( ( nBitSize % 8u ) != 0u ? 1u : 0u );
    const bool_t bSizeFits = cbRequired <= source.cbSize;
    CY_ASSERT_MSG( bValidReader, "BitReader_Init requires a reader object." );
    CY_ASSERT_MSG( bValidSource, "BitReader_Init requires a valid source block." );
    CY_ASSERT_MSG( bValidOrder, "BitReader_Init requires a valid bit order." );
    CY_ASSERT_MSG( bSizeFits, "BitReader_Init bit size exceeds its source." );
    if ( !bValidReader || !bValidSource || !bValidOrder || !bSizeFits ) {
        return CY_FALSE;
    }

    pReader->pData = source.pData;
    pReader->nBitSize = nBitSize;
    pReader->iBit = 0u;
    pReader->bitOrder = bitOrder;
    pReader->status = bit_cursor_status_t::OK;
    return CY_TRUE;
}

void BitReader_Reset( bit_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = BitReader_IsValid( pReader );
    CY_ASSERT_MSG( bValidReader, "BitReader_Reset requires a valid reader." );
    if ( bValidReader ) {
        pReader->iBit = 0u;
        pReader->status = bit_cursor_status_t::OK;
    }
}

void BitReader_ClearStatus( bit_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = BitReader_IsValid( pReader );
    CY_ASSERT_MSG( bValidReader, "BitReader_ClearStatus requires a valid reader." );
    if ( bValidReader ) {
        pReader->status = bit_cursor_status_t::OK;
    }
}

bool_t BitReader_IsValid( const bit_reader_t *pReader ) noexcept
{
    return pReader != nullptr &&
           ( pReader->pData != nullptr || pReader->nBitSize == 0u ) &&
           pReader->iBit <= pReader->nBitSize &&
           IsBitOrderValid( pReader->bitOrder ) &&
           IsBitCursorStatusValid( pReader->status );
}

bit_cursor_status_t BitReader_Status( const bit_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = BitReader_IsValid( pReader );
    CY_ASSERT_MSG( bValidReader, "BitReader_Status requires a valid reader." );
    return bValidReader ? pReader->status : bit_cursor_status_t::INVALID_ARGUMENT;
}

usize BitReader_Offset( const bit_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = BitReader_IsValid( pReader );
    CY_ASSERT_MSG( bValidReader, "BitReader_Offset requires a valid reader." );
    return bValidReader ? pReader->iBit : 0u;
}

usize BitReader_Size( const bit_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = BitReader_IsValid( pReader );
    CY_ASSERT_MSG( bValidReader, "BitReader_Size requires a valid reader." );
    return bValidReader ? pReader->nBitSize : 0u;
}

usize BitReader_Remaining( const bit_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = BitReader_IsValid( pReader );
    CY_ASSERT_MSG( bValidReader, "BitReader_Remaining requires a valid reader." );
    return bValidReader ? pReader->nBitSize - pReader->iBit : 0u;
}

bool_t BitReader_Seek( bit_reader_t *pReader, usize iBit ) noexcept
{
    const bool_t bValidReader = BitReader_IsValid( pReader );
    CY_ASSERT_MSG( bValidReader, "BitReader_Seek requires a valid reader." );
    if ( !bValidReader || pReader->status != bit_cursor_status_t::OK ) {
        return CY_FALSE;
    }
    if ( iBit > pReader->nBitSize ) {
        return BitReaderFail( pReader, bit_cursor_status_t::OUT_OF_BOUNDS );
    }
    pReader->iBit = iBit;
    return CY_TRUE;
}

bool_t BitReader_Skip( bit_reader_t *pReader, usize nBits ) noexcept
{
    if ( !BitReaderCanRead( pReader, nBits ) ) {
        return CY_FALSE;
    }
    pReader->iBit += nBits;
    return CY_TRUE;
}

bool_t BitReader_AlignToByte( bit_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = BitReader_IsValid( pReader );
    CY_ASSERT_MSG( bValidReader, "BitReader_AlignToByte requires a valid reader." );
    if ( !bValidReader || pReader->status != bit_cursor_status_t::OK ) {
        return CY_FALSE;
    }
    const usize nPadding = ( 8u - ( pReader->iBit % 8u ) ) % 8u;
    return BitReader_Skip( pReader, nPadding );
}

bool_t BitReader_ReadBits(
    bit_reader_t *pReader,
    u32 nBits,
    u64 *pValueOut ) noexcept
{
    const bool_t bValidOutput = pValueOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "BitReader_ReadBits requires output." );
    if ( !bValidOutput ) {
        return BitReaderFail( pReader, bit_cursor_status_t::INVALID_ARGUMENT );
    }
    if ( nBits == 0u || nBits > 64u ) {
        return BitReaderFail( pReader, bit_cursor_status_t::INVALID_BIT_COUNT );
    }
    if ( !BitReaderCanRead( pReader, nBits ) ) {
        return CY_FALSE;
    }

    const u32 iBitInByte = static_cast<u32>( pReader->iBit % 8u );
    if ( iBitInByte == 0u && ( nBits % 8u ) == 0u ) {
        const usize iFirstByte = pReader->iBit / 8u;
        const u32 cbField = nBits / 8u;
        u64 value = 0u;
        if ( pReader->bitOrder == bit_order_t::LEAST_SIGNIFICANT_FIRST ) {
            for ( u32 iByte = 0u; iByte < cbField; ++iByte ) {
                value |= static_cast<u64>( pReader->pData[iFirstByte + iByte] )
                         << ( iByte * 8u );
            }
        } else {
            for ( u32 iByte = 0u; iByte < cbField; ++iByte ) {
                value = ( value << 8u ) | pReader->pData[iFirstByte + iByte];
            }
        }
        pReader->iBit += nBits;
        *pValueOut = value;
        return CY_TRUE;
    }

    if ( nBits <= 8u && iBitInByte + nBits <= 8u ) {
        const byte source = pReader->pData[pReader->iBit / 8u];
        const u32 iShift =
            pReader->bitOrder == bit_order_t::LEAST_SIGNIFICANT_FIRST
                ? iBitInByte
                : 8u - iBitInByte - nBits;
        const u32 mask = ( 1u << nBits ) - 1u;
        *pValueOut = static_cast<u64>( ( source >> iShift ) & mask );
        pReader->iBit += nBits;
        return CY_TRUE;
    }

    // General cross-byte path. Physical stream order and integer value order are
    // handled separately so both LSB-first and MSB-first formats are stable.
    u64 value = 0u;
    for ( u32 iFieldBit = 0u; iFieldBit < nBits; ++iFieldBit ) {
        const usize iStreamBit = pReader->iBit + iFieldBit;
        const u32 iPhysicalBit = PhysicalBitIndex(
            iStreamBit,
            pReader->bitOrder );
        const u64 nStreamBit =
            ( pReader->pData[iStreamBit / 8u] >> iPhysicalBit ) & 1u;
        const u32 iValueBit =
            pReader->bitOrder == bit_order_t::LEAST_SIGNIFICANT_FIRST
                ? iFieldBit
                : nBits - 1u - iFieldBit;
        value |= nStreamBit << iValueBit;
    }

    pReader->iBit += nBits;
    *pValueOut = value;
    return CY_TRUE;
}

bool_t BitReader_ReadSignedBits(
    bit_reader_t *pReader,
    u32 nBits,
    i64 *pValueOut ) noexcept
{
    const bool_t bValidOutput = pValueOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "BitReader_ReadSignedBits requires output." );
    if ( !bValidOutput ) {
        return BitReaderFail( pReader, bit_cursor_status_t::INVALID_ARGUMENT );
    }

    u64 bits = 0u;
    if ( !BitReader_ReadBits( pReader, nBits, &bits ) ) {
        return CY_FALSE;
    }
    // Sign-extend the field width before interpreting the bits as i64.
    if ( nBits < 64u && ( bits & ( 1ull << ( nBits - 1u ) ) ) != 0u ) {
        bits |= ~(( 1ull << nBits ) - 1u );
    }
    *pValueOut = std::bit_cast<i64>( bits );
    return CY_TRUE;
}

bool_t BitReader_ReadBool(
    bit_reader_t *pReader,
    bool_t *pValueOut ) noexcept
{
    const bool_t bValidOutput = pValueOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "BitReader_ReadBool requires output." );
    if ( !bValidOutput ) {
        return BitReaderFail( pReader, bit_cursor_status_t::INVALID_ARGUMENT );
    }

    u64 value = 0u;
    if ( !BitReader_ReadBits( pReader, 1u, &value ) ) {
        return CY_FALSE;
    }
    *pValueOut = value != 0u;
    return CY_TRUE;
}

} // namespace cypher::common
