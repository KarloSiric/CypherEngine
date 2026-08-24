//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ByteReader.cpp
//  Purpose: Implements bounds-checked sequential binary readers.
//  Details: Reads are transactional, endian conversion is explicit, and malformed
//           data produces a sticky status without advancing the cursor.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ByteReader.h"

#include <bit>

namespace cypher::common
{

namespace
{

// Reader failures are sticky. The first error identifies the operation that
// invalidated the cursor; later reads must not overwrite that diagnosis.

bool_t IsByteOrderValid( data_byte_order_t byteOrder ) noexcept
{
    switch ( byteOrder ) {
        case data_byte_order_t::LITTLE:
        case data_byte_order_t::BIG:
        case data_byte_order_t::NATIVE:
            return CY_TRUE;
    }
    return CY_FALSE;
}

bool_t IsByteCursorStatusValid( byte_cursor_status_t status ) noexcept
{
    switch ( status ) {
        case byte_cursor_status_t::OK:
        case byte_cursor_status_t::INVALID_ARGUMENT:
        case byte_cursor_status_t::OUT_OF_BOUNDS:
        case byte_cursor_status_t::INVALID_ENCODING:
        case byte_cursor_status_t::CURSOR_OVERFLOW:
            return CY_TRUE;
    }
    return CY_FALSE;
}

bool_t ByteReaderFail(
    byte_reader_t *pReader,
    byte_cursor_status_t status ) noexcept
{
    if ( pReader != nullptr && pReader->status == byte_cursor_status_t::OK ) {
        pReader->status = status;
    }
    return CY_FALSE;
}

bool_t ByteReaderCanRead(
    byte_reader_t *pReader,
    usize cbData ) noexcept
{
    const bool_t bValidReader = ByteReader_IsValid( pReader );
    CY_ASSERT_MSG(
        bValidReader,
        "ByteReader operation requires a valid reader." );
    if ( !bValidReader || pReader->status != byte_cursor_status_t::OK ) {
        return CY_FALSE;
    }
    if ( cbData > CY_USIZE_MAX - pReader->iOffset ) {
        return ByteReaderFail(
            pReader,
            byte_cursor_status_t::CURSOR_OVERFLOW );
    }
    if ( cbData > pReader->cbSize - pReader->iOffset ) {
        return ByteReaderFail(
            pReader,
            byte_cursor_status_t::OUT_OF_BOUNDS );
    }
    return CY_TRUE;
}

u16 ConvertU16FromData( u16 value, data_byte_order_t order ) noexcept
{
    switch ( order ) {
        case data_byte_order_t::LITTLE: return Cy_LittleToHost16( value );
        case data_byte_order_t::BIG: return Cy_BigToHost16( value );
        case data_byte_order_t::NATIVE: return value;
    }
    return value;
}

u32 ConvertU32FromData( u32 value, data_byte_order_t order ) noexcept
{
    switch ( order ) {
        case data_byte_order_t::LITTLE: return Cy_LittleToHost32( value );
        case data_byte_order_t::BIG: return Cy_BigToHost32( value );
        case data_byte_order_t::NATIVE: return value;
    }
    return value;
}

u64 ConvertU64FromData( u64 value, data_byte_order_t order ) noexcept
{
    switch ( order ) {
        case data_byte_order_t::LITTLE: return Cy_LittleToHost64( value );
        case data_byte_order_t::BIG: return Cy_BigToHost64( value );
        case data_byte_order_t::NATIVE: return value;
    }
    return value;
}

} // namespace

bool_t ByteReader_Init(
    byte_reader_t *pReader,
    binary_block_t source,
    data_byte_order_t byteOrder ) noexcept
{
    const bool_t bValidReader = pReader != nullptr;
    const bool_t bValidSource = BinaryBlock_IsValid( source );
    const bool_t bValidOrder = IsByteOrderValid( byteOrder );
    CY_ASSERT_MSG(
        bValidReader,
        "ByteReader_Init requires a reader object." );
    CY_ASSERT_MSG(
        bValidSource,
        "ByteReader_Init requires a valid source block." );
    CY_ASSERT_MSG(
        bValidOrder,
        "ByteReader_Init requires a valid byte order." );
    if ( !bValidReader || !bValidSource || !bValidOrder ) {
        return CY_FALSE;
    }

    pReader->pData = source.pData;
    pReader->cbSize = source.cbSize;
    pReader->iOffset = 0u;
    pReader->byteOrder = byteOrder;
    pReader->status = byte_cursor_status_t::OK;
    return CY_TRUE;
}

void ByteReader_Reset( byte_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = ByteReader_IsValid( pReader );
    CY_ASSERT_MSG(
        bValidReader,
        "ByteReader_Reset requires a valid reader." );
    if ( bValidReader ) {
        pReader->iOffset = 0u;
        pReader->status = byte_cursor_status_t::OK;
    }
}

void ByteReader_ClearStatus( byte_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = ByteReader_IsValid( pReader );
    CY_ASSERT_MSG(
        bValidReader,
        "ByteReader_ClearStatus requires a valid reader." );
    if ( bValidReader ) {
        pReader->status = byte_cursor_status_t::OK;
    }
}

bool_t ByteReader_IsValid( const byte_reader_t *pReader ) noexcept
{
    return pReader != nullptr &&
           ( pReader->pData != nullptr || pReader->cbSize == 0u ) &&
           pReader->iOffset <= pReader->cbSize &&
           IsByteOrderValid( pReader->byteOrder ) &&
           IsByteCursorStatusValid( pReader->status );
}

byte_cursor_status_t ByteReader_Status(
    const byte_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = ByteReader_IsValid( pReader );
    CY_ASSERT_MSG(
        bValidReader,
        "ByteReader_Status requires a valid reader." );
    return bValidReader
        ? pReader->status
        : byte_cursor_status_t::INVALID_ARGUMENT;
}

usize ByteReader_Offset( const byte_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = ByteReader_IsValid( pReader );
    CY_ASSERT_MSG(
        bValidReader,
        "ByteReader_Offset requires a valid reader." );
    return bValidReader ? pReader->iOffset : 0u;
}

usize ByteReader_Size( const byte_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = ByteReader_IsValid( pReader );
    CY_ASSERT_MSG(
        bValidReader,
        "ByteReader_Size requires a valid reader." );
    return bValidReader ? pReader->cbSize : 0u;
}

usize ByteReader_Remaining( const byte_reader_t *pReader ) noexcept
{
    const bool_t bValidReader = ByteReader_IsValid( pReader );
    CY_ASSERT_MSG(
        bValidReader,
        "ByteReader_Remaining requires a valid reader." );
    return bValidReader ? pReader->cbSize - pReader->iOffset : 0u;
}

bool_t ByteReader_Seek( byte_reader_t *pReader, usize iOffset ) noexcept
{
    const bool_t bValidReader = ByteReader_IsValid( pReader );
    CY_ASSERT_MSG( bValidReader, "ByteReader_Seek requires a valid reader." );
    if ( !bValidReader || pReader->status != byte_cursor_status_t::OK ) {
        return CY_FALSE;
    }
    if ( iOffset > pReader->cbSize ) {
        return ByteReaderFail(
            pReader,
            byte_cursor_status_t::OUT_OF_BOUNDS );
    }
    pReader->iOffset = iOffset;
    return CY_TRUE;
}

bool_t ByteReader_Skip( byte_reader_t *pReader, usize cbData ) noexcept
{
    if ( !ByteReaderCanRead( pReader, cbData ) ) {
        return CY_FALSE;
    }
    pReader->iOffset += cbData;
    return CY_TRUE;
}

bool_t ByteReader_Read(
    byte_reader_t *pReader,
    void *pDest,
    usize cbData ) noexcept
{
    const bool_t bValidDestination = pDest != nullptr || cbData == 0u;
    CY_ASSERT_MSG(
        bValidDestination,
        "ByteReader_Read requires a destination for non-empty data." );
    if ( !bValidDestination ) {
        return ByteReaderFail(
            pReader,
            byte_cursor_status_t::INVALID_ARGUMENT );
    }
    if ( !ByteReaderCanRead( pReader, cbData ) ) {
        return CY_FALSE;
    }

    // Check the complete range before copying so a failed read changes neither
    // the destination nor the cursor.
    if ( cbData > 0u ) {
        Cy_MemMove( pDest, pReader->pData + pReader->iOffset, cbData );
    }
    pReader->iOffset += cbData;
    return CY_TRUE;
}

bool_t ByteReader_ReadBlock(
    byte_reader_t *pReader,
    usize cbData,
    binary_block_t *pBlockOut ) noexcept
{
    const bool_t bValidOutput = pBlockOut != nullptr;
    CY_ASSERT_MSG(
        bValidOutput,
        "ByteReader_ReadBlock requires an output block." );
    if ( !bValidOutput ) {
        return ByteReaderFail(
            pReader,
            byte_cursor_status_t::INVALID_ARGUMENT );
    }
    if ( !ByteReaderCanRead( pReader, cbData ) ) {
        return CY_FALSE;
    }

    *pBlockOut = {
        cbData > 0u ? pReader->pData + pReader->iOffset : nullptr,
        cbData
    };
    pReader->iOffset += cbData;
    return CY_TRUE;
}

bool_t ByteReader_ReadU8( byte_reader_t *pReader, u8 *pOut ) noexcept
{
    return ByteReader_Read( pReader, pOut, sizeof( *pOut ) );
}

bool_t ByteReader_ReadU16( byte_reader_t *pReader, u16 *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "ByteReader_ReadU16 requires output." );
    if ( !bValidOutput ) {
        return ByteReaderFail( pReader, byte_cursor_status_t::INVALID_ARGUMENT );
    }
    u16 value = 0u;
    if ( !ByteReader_Read( pReader, &value, sizeof( value ) ) ) {
        return CY_FALSE;
    }
    *pOut = ConvertU16FromData( value, pReader->byteOrder );
    return CY_TRUE;
}

bool_t ByteReader_ReadU32( byte_reader_t *pReader, u32 *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "ByteReader_ReadU32 requires output." );
    if ( !bValidOutput ) {
        return ByteReaderFail( pReader, byte_cursor_status_t::INVALID_ARGUMENT );
    }
    u32 value = 0u;
    if ( !ByteReader_Read( pReader, &value, sizeof( value ) ) ) {
        return CY_FALSE;
    }
    *pOut = ConvertU32FromData( value, pReader->byteOrder );
    return CY_TRUE;
}

bool_t ByteReader_ReadU64( byte_reader_t *pReader, u64 *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "ByteReader_ReadU64 requires output." );
    if ( !bValidOutput ) {
        return ByteReaderFail( pReader, byte_cursor_status_t::INVALID_ARGUMENT );
    }
    u64 value = 0u;
    if ( !ByteReader_Read( pReader, &value, sizeof( value ) ) ) {
        return CY_FALSE;
    }
    *pOut = ConvertU64FromData( value, pReader->byteOrder );
    return CY_TRUE;
}

bool_t ByteReader_ReadI32( byte_reader_t *pReader, i32 *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "ByteReader_ReadI32 requires output." );
    if ( !bValidOutput ) {
        return ByteReaderFail( pReader, byte_cursor_status_t::INVALID_ARGUMENT );
    }
    u32 bits = 0u;
    if ( !ByteReader_ReadU32( pReader, &bits ) ) {
        return CY_FALSE;
    }
    *pOut = std::bit_cast<i32>( bits );
    return CY_TRUE;
}

bool_t ByteReader_ReadI64( byte_reader_t *pReader, i64 *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "ByteReader_ReadI64 requires output." );
    if ( !bValidOutput ) {
        return ByteReaderFail( pReader, byte_cursor_status_t::INVALID_ARGUMENT );
    }
    u64 bits = 0u;
    if ( !ByteReader_ReadU64( pReader, &bits ) ) {
        return CY_FALSE;
    }
    *pOut = std::bit_cast<i64>( bits );
    return CY_TRUE;
}

bool_t ByteReader_ReadF32( byte_reader_t *pReader, f32 *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "ByteReader_ReadF32 requires output." );
    if ( !bValidOutput ) {
        return ByteReaderFail( pReader, byte_cursor_status_t::INVALID_ARGUMENT );
    }
    u32 bits = 0u;
    if ( !ByteReader_ReadU32( pReader, &bits ) ) {
        return CY_FALSE;
    }
    *pOut = std::bit_cast<f32>( bits );
    return CY_TRUE;
}

bool_t ByteReader_ReadF64( byte_reader_t *pReader, f64 *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "ByteReader_ReadF64 requires output." );
    if ( !bValidOutput ) {
        return ByteReaderFail( pReader, byte_cursor_status_t::INVALID_ARGUMENT );
    }
    u64 bits = 0u;
    if ( !ByteReader_ReadU64( pReader, &bits ) ) {
        return CY_FALSE;
    }
    *pOut = std::bit_cast<f64>( bits );
    return CY_TRUE;
}

bool_t ByteReader_ReadVarU64( byte_reader_t *pReader, u64 *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "ByteReader_ReadVarU64 requires output." );
    if ( !bValidOutput ) {
        return ByteReaderFail( pReader, byte_cursor_status_t::INVALID_ARGUMENT );
    }
    const bool_t bValidReader = ByteReader_IsValid( pReader );
    CY_ASSERT_MSG(
        bValidReader,
        "ByteReader_ReadVarU64 requires a valid reader." );
    if ( !bValidReader || pReader->status != byte_cursor_status_t::OK ) {
        return CY_FALSE;
    }

    usize iCursor = pReader->iOffset;
    u64 nValue = 0u;

    // Unsigned LEB128 needs at most ten bytes for 64 bits. The tenth byte may
    // carry only bit 63; any other payload bit would overflow the result.
    for ( u32 iByte = 0u; iByte < 10u; ++iByte ) {
        if ( iCursor == pReader->cbSize ) {
            return ByteReaderFail(
                pReader,
                byte_cursor_status_t::OUT_OF_BOUNDS );
        }
        const u8 nByte = pReader->pData[iCursor++];
        if ( iByte == 9u && ( nByte & 0xFEu ) != 0u ) {
            return ByteReaderFail(
                pReader,
                byte_cursor_status_t::INVALID_ENCODING );
        }
        nValue |= static_cast<u64>( nByte & 0x7Fu ) << ( iByte * 7u );
        if ( ( nByte & 0x80u ) == 0u ) {
            // A multi-byte value whose final group is zero has a shorter encoding.
            // Rejecting it keeps serialized data canonical and non-malleable.
            if ( iByte > 0u && nByte == 0u ) {
                return ByteReaderFail(
                    pReader,
                    byte_cursor_status_t::INVALID_ENCODING );
            }
            pReader->iOffset = iCursor;
            *pOut = nValue;
            return CY_TRUE;
        }
    }

    return ByteReaderFail(
        pReader,
        byte_cursor_status_t::INVALID_ENCODING );
}

bool_t ByteReader_ReadVarI64( byte_reader_t *pReader, i64 *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "ByteReader_ReadVarI64 requires output." );
    if ( !bValidOutput ) {
        return ByteReaderFail( pReader, byte_cursor_status_t::INVALID_ARGUMENT );
    }
    u64 nEncoded = 0u;
    if ( !ByteReader_ReadVarU64( pReader, &nEncoded ) ) {
        return CY_FALSE;
    }

    // Zig-zag decoding maps alternating unsigned values back to
    // 0, -1, 1, -2, 2, ... without implementation-defined signed shifts.
    const u64 nDecoded =
        ( nEncoded >> 1u ) ^ ( 0u - ( nEncoded & 1u ) );
    *pOut = std::bit_cast<i64>( nDecoded );
    return CY_TRUE;
}

bool_t ByteReader_ReadCString(
    byte_reader_t *pReader,
    usize cchMax,
    string_view_t *pOut ) noexcept
{
    const bool_t bValidOutput = pOut != nullptr;
    CY_ASSERT_MSG( bValidOutput, "ByteReader_ReadCString requires output." );
    if ( !bValidOutput ) {
        return ByteReaderFail( pReader, byte_cursor_status_t::INVALID_ARGUMENT );
    }
    const bool_t bValidReader = ByteReader_IsValid( pReader );
    CY_ASSERT_MSG(
        bValidReader,
        "ByteReader_ReadCString requires a valid reader." );
    if ( !bValidReader || pReader->status != byte_cursor_status_t::OK ) {
        return CY_FALSE;
    }

    const usize cbRemaining = pReader->cbSize - pReader->iOffset;
    for ( usize iCharacter = 0u; iCharacter < cbRemaining; ++iCharacter ) {
        if ( iCharacter > cchMax ) {
            break;
        }
        if ( pReader->pData[pReader->iOffset + iCharacter] == 0u ) {
            *pOut = {
                iCharacter > 0u
                    ? reinterpret_cast<const char *>(
                        pReader->pData + pReader->iOffset )
                    : nullptr,
                iCharacter
            };
            pReader->iOffset += iCharacter + 1u;
            return CY_TRUE;
        }
    }

    return ByteReaderFail(
        pReader,
        byte_cursor_status_t::INVALID_ENCODING );
}

} // namespace cypher::common
