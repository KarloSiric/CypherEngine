//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BitWriter.cpp
//  Purpose: Implements bounds-checked bitstream writers.
//  Details: Writes validate field width and value range before touching storage,
//           preserve high-water state during patching, and zero serialized padding.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_BitWriter.h"

#include <bit>

namespace cypher::common
{

namespace
{

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

bool_t BitWriterFail(
    bit_writer_t *pWriter,
    bit_cursor_status_t status ) noexcept
{
    if ( pWriter != nullptr && pWriter->status == bit_cursor_status_t::OK ) {
        pWriter->status = status;
    }
    return CY_FALSE;
}

bool_t BitWriterCanWrite( bit_writer_t *pWriter, usize nBits ) noexcept
{
    const bool_t bValidWriter = BitWriter_IsValid( pWriter );
    CY_ASSERT_MSG( bValidWriter, "BitWriter operation requires a valid writer." );
    if ( !bValidWriter || pWriter->status != bit_cursor_status_t::OK ) {
        return CY_FALSE;
    }
    if ( nBits > CY_USIZE_MAX - pWriter->iBit ) {
        return BitWriterFail( pWriter, bit_cursor_status_t::CURSOR_OVERFLOW );
    }
    if ( nBits > pWriter->nBitCapacity - pWriter->iBit ) {
        return BitWriterFail( pWriter, bit_cursor_status_t::OUT_OF_BOUNDS );
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

void BitWriterCommitBits(
    bit_writer_t *pWriter,
    u64 value,
    u32 nBits ) noexcept
{
    const u32 iBitInByte = static_cast<u32>( pWriter->iBit % 8u );
    if ( iBitInByte == 0u && ( nBits % 8u ) == 0u ) {
        const usize iFirstByte = pWriter->iBit / 8u;
        const u32 cbField = nBits / 8u;
        for ( u32 iByte = 0u; iByte < cbField; ++iByte ) {
            const u32 iShift =
                pWriter->bitOrder == bit_order_t::LEAST_SIGNIFICANT_FIRST
                    ? iByte * 8u
                    : ( cbField - 1u - iByte ) * 8u;
            pWriter->pData[iFirstByte + iByte] = static_cast<byte>(
                ( value >> iShift ) & 0xFFu );
        }
        pWriter->iBit += nBits;
        if ( pWriter->iBit > pWriter->nBitHighWater ) {
            pWriter->nBitHighWater = pWriter->iBit;
        }
        return;
    }

    if ( nBits <= 8u && iBitInByte + nBits <= 8u ) {
        const usize iByte = pWriter->iBit / 8u;
        if ( pWriter->iBit >= pWriter->nBitHighWater && iBitInByte == 0u ) {
            pWriter->pData[iByte] = 0u;
        }
        const u32 iShift =
            pWriter->bitOrder == bit_order_t::LEAST_SIGNIFICANT_FIRST
                ? iBitInByte
                : 8u - iBitInByte - nBits;
        const byte mask = static_cast<byte>(
            ( ( 1u << nBits ) - 1u ) << iShift );
        const byte encoded = static_cast<byte>( value << iShift );
        pWriter->pData[iByte] = static_cast<byte>(
            ( pWriter->pData[iByte] & static_cast<byte>( ~mask ) ) |
            ( encoded & mask ) );
        pWriter->iBit += nBits;
        if ( pWriter->iBit > pWriter->nBitHighWater ) {
            pWriter->nBitHighWater = pWriter->iBit;
        }
        return;
    }

    for ( u32 iFieldBit = 0u; iFieldBit < nBits; ++iFieldBit ) {
        const usize iStreamBit = pWriter->iBit + iFieldBit;
        const usize iByte = iStreamBit / 8u;
        if ( iStreamBit >= pWriter->nBitHighWater &&
             ( iStreamBit % 8u ) == 0u ) {
            pWriter->pData[iByte] = 0u;
        }

        const u32 iValueBit =
            pWriter->bitOrder == bit_order_t::LEAST_SIGNIFICANT_FIRST
                ? iFieldBit
                : nBits - 1u - iFieldBit;
        const bool_t bSet = ( ( value >> iValueBit ) & 1u ) != 0u;
        const byte mask = static_cast<byte>(
            1u << PhysicalBitIndex( iStreamBit, pWriter->bitOrder ) );
        if ( bSet ) {
            pWriter->pData[iByte] = static_cast<byte>(
                pWriter->pData[iByte] | mask );
        } else {
            pWriter->pData[iByte] = static_cast<byte>(
                pWriter->pData[iByte] & static_cast<byte>( ~mask ) );
        }
    }

    pWriter->iBit += nBits;
    if ( pWriter->iBit > pWriter->nBitHighWater ) {
        pWriter->nBitHighWater = pWriter->iBit;
    }
}

} // namespace

bool_t BitWriter_Init(
    bit_writer_t *pWriter,
    byte_span_t storage,
    bit_order_t bitOrder ) noexcept
{
    const bool_t bValidWriter = pWriter != nullptr;
    const bool_t bValidStorage = Span_IsValid( storage );
    const bool_t bValidOrder = IsBitOrderValid( bitOrder );
    const bool_t bCapacityFits = storage.nCount <= CY_USIZE_MAX / 8u;
    CY_ASSERT_MSG( bValidWriter, "BitWriter_Init requires a writer object." );
    CY_ASSERT_MSG( bValidStorage, "BitWriter_Init requires valid storage." );
    CY_ASSERT_MSG( bValidOrder, "BitWriter_Init requires a valid bit order." );
    CY_ASSERT_MSG( bCapacityFits, "BitWriter_Init bit capacity overflowed." );
    if ( !bValidWriter || !bValidStorage || !bValidOrder || !bCapacityFits ) {
        return CY_FALSE;
    }

    pWriter->pData = storage.pData;
    pWriter->nBitCapacity = storage.nCount * 8u;
    pWriter->iBit = 0u;
    pWriter->nBitHighWater = 0u;
    pWriter->bitOrder = bitOrder;
    pWriter->status = bit_cursor_status_t::OK;
    return CY_TRUE;
}

void BitWriter_Reset( bit_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = BitWriter_IsValid( pWriter );
    CY_ASSERT_MSG( bValidWriter, "BitWriter_Reset requires a valid writer." );
    if ( bValidWriter ) {
        pWriter->iBit = 0u;
        pWriter->nBitHighWater = 0u;
        pWriter->status = bit_cursor_status_t::OK;
    }
}

void BitWriter_ClearStatus( bit_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = BitWriter_IsValid( pWriter );
    CY_ASSERT_MSG( bValidWriter, "BitWriter_ClearStatus requires a valid writer." );
    if ( bValidWriter ) {
        pWriter->status = bit_cursor_status_t::OK;
    }
}

bool_t BitWriter_IsValid( const bit_writer_t *pWriter ) noexcept
{
    return pWriter != nullptr &&
           ( pWriter->pData != nullptr || pWriter->nBitCapacity == 0u ) &&
           ( pWriter->nBitCapacity % 8u ) == 0u &&
           pWriter->iBit <= pWriter->nBitHighWater &&
           pWriter->nBitHighWater <= pWriter->nBitCapacity &&
           IsBitOrderValid( pWriter->bitOrder ) &&
           IsBitCursorStatusValid( pWriter->status );
}

bit_cursor_status_t BitWriter_Status( const bit_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = BitWriter_IsValid( pWriter );
    CY_ASSERT_MSG( bValidWriter, "BitWriter_Status requires a valid writer." );
    return bValidWriter ? pWriter->status : bit_cursor_status_t::INVALID_ARGUMENT;
}

usize BitWriter_Offset( const bit_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = BitWriter_IsValid( pWriter );
    CY_ASSERT_MSG( bValidWriter, "BitWriter_Offset requires a valid writer." );
    return bValidWriter ? pWriter->iBit : 0u;
}

usize BitWriter_Capacity( const bit_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = BitWriter_IsValid( pWriter );
    CY_ASSERT_MSG( bValidWriter, "BitWriter_Capacity requires a valid writer." );
    return bValidWriter ? pWriter->nBitCapacity : 0u;
}

usize BitWriter_Remaining( const bit_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = BitWriter_IsValid( pWriter );
    CY_ASSERT_MSG( bValidWriter, "BitWriter_Remaining requires a valid writer." );
    return bValidWriter ? pWriter->nBitCapacity - pWriter->iBit : 0u;
}

usize BitWriter_BitsWritten( const bit_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = BitWriter_IsValid( pWriter );
    CY_ASSERT_MSG( bValidWriter, "BitWriter_BitsWritten requires a valid writer." );
    return bValidWriter ? pWriter->nBitHighWater : 0u;
}

bool_t BitWriter_Seek( bit_writer_t *pWriter, usize iBit ) noexcept
{
    const bool_t bValidWriter = BitWriter_IsValid( pWriter );
    CY_ASSERT_MSG( bValidWriter, "BitWriter_Seek requires a valid writer." );
    if ( !bValidWriter || pWriter->status != bit_cursor_status_t::OK ) {
        return CY_FALSE;
    }
    if ( iBit > pWriter->nBitHighWater ) {
        return BitWriterFail( pWriter, bit_cursor_status_t::OUT_OF_BOUNDS );
    }
    pWriter->iBit = iBit;
    return CY_TRUE;
}

bool_t BitWriter_AlignToByte(
    bit_writer_t *pWriter,
    bool_t bFillValue ) noexcept
{
    const bool_t bValidWriter = BitWriter_IsValid( pWriter );
    CY_ASSERT_MSG( bValidWriter, "BitWriter_AlignToByte requires a valid writer." );
    if ( !bValidWriter || pWriter->status != bit_cursor_status_t::OK ) {
        return CY_FALSE;
    }
    const u32 nPadding = static_cast<u32>(
        ( 8u - ( pWriter->iBit % 8u ) ) % 8u );
    if ( nPadding == 0u ) {
        return CY_TRUE;
    }
    const u64 value = bFillValue ? ( 1ull << nPadding ) - 1u : 0u;
    return BitWriter_WriteBits( pWriter, value, nPadding );
}

bool_t BitWriter_WriteBits(
    bit_writer_t *pWriter,
    u64 value,
    u32 nBits ) noexcept
{
    if ( nBits == 0u || nBits > 64u ) {
        return BitWriterFail( pWriter, bit_cursor_status_t::INVALID_BIT_COUNT );
    }
    if ( nBits < 64u && value > ( ( 1ull << nBits ) - 1u ) ) {
        return BitWriterFail( pWriter, bit_cursor_status_t::VALUE_OUT_OF_RANGE );
    }
    if ( !BitWriterCanWrite( pWriter, nBits ) ) {
        return CY_FALSE;
    }

    BitWriterCommitBits( pWriter, value, nBits );
    return CY_TRUE;
}

bool_t BitWriter_WriteSignedBits(
    bit_writer_t *pWriter,
    i64 value,
    u32 nBits ) noexcept
{
    if ( nBits == 0u || nBits > 64u ) {
        return BitWriterFail( pWriter, bit_cursor_status_t::INVALID_BIT_COUNT );
    }
    if ( nBits < 64u ) {
        const i64 nMagnitude = 1ll << ( nBits - 1u );
        if ( value < -nMagnitude || value > nMagnitude - 1 ) {
            return BitWriterFail( pWriter, bit_cursor_status_t::VALUE_OUT_OF_RANGE );
        }
    }
    if ( !BitWriterCanWrite( pWriter, nBits ) ) {
        return CY_FALSE;
    }

    u64 bits = std::bit_cast<u64>( value );
    if ( nBits < 64u ) {
        bits &= ( 1ull << nBits ) - 1u;
    }
    BitWriterCommitBits( pWriter, bits, nBits );
    return CY_TRUE;
}

bool_t BitWriter_WriteBool(
    bit_writer_t *pWriter,
    bool_t value ) noexcept
{
    if ( !BitWriterCanWrite( pWriter, 1u ) ) {
        return CY_FALSE;
    }
    BitWriterCommitBits( pWriter, value ? 1u : 0u, 1u );
    return CY_TRUE;
}

binary_block_t BitWriter_Block( const bit_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = BitWriter_IsValid( pWriter );
    CY_ASSERT_MSG( bValidWriter, "BitWriter_Block requires a valid writer." );
    if ( !bValidWriter || pWriter->nBitHighWater == 0u ) {
        return {};
    }
    const usize cbSize =
        ( pWriter->nBitHighWater / 8u ) +
        ( ( pWriter->nBitHighWater % 8u ) != 0u ? 1u : 0u );
    return { pWriter->pData, cbSize };
}

} // namespace cypher::common
