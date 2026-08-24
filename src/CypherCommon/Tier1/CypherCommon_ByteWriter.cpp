//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ByteWriter.cpp
//  Purpose: Implements bounds-checked sequential binary writers.
//  Details: Values are converted before one transactional write, high-water state
//           survives seeking, and insufficient storage never emits partial values.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Byte Writer Implementation Notes

The cursor and capacity form one invariant: no operation may advance beyond the supplied
storage. Failed writes report the condition without publishing a cursor that claims unwritten
bytes.
================
*/

#include "CypherCommon_ByteWriter.h"

#include <bit>

namespace cypher::common
{

namespace
{

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

bool_t ByteWriterFail(
    byte_writer_t *pWriter,
    byte_cursor_status_t status ) noexcept
{
    if ( pWriter != nullptr && pWriter->status == byte_cursor_status_t::OK ) {
        pWriter->status = status;
    }
    return CY_FALSE;
}

bool_t ByteWriterCanWrite(
    byte_writer_t *pWriter,
    usize cbData ) noexcept
{
    const bool_t bValidWriter = ByteWriter_IsValid( pWriter );
    CY_ASSERT_MSG(
        bValidWriter,
        "ByteWriter operation requires a valid writer." );
    if ( !bValidWriter || pWriter->status != byte_cursor_status_t::OK ) {
        return CY_FALSE;
    }
    if ( cbData > CY_USIZE_MAX - pWriter->iOffset ) {
        return ByteWriterFail(
            pWriter,
            byte_cursor_status_t::CURSOR_OVERFLOW );
    }
    if ( cbData > pWriter->cbCapacity - pWriter->iOffset ) {
        return ByteWriterFail(
            pWriter,
            byte_cursor_status_t::OUT_OF_BOUNDS );
    }
    return CY_TRUE;
}

void ByteWriterCommit(
    byte_writer_t *pWriter,
    const void *pSource,
    usize cbData ) noexcept
{
    // MemMove permits callers to patch or duplicate bytes from the writer's
    // own storage without introducing an overlap restriction.
    if ( cbData > 0u ) {
        Cy_MemMove( pWriter->pData + pWriter->iOffset, pSource, cbData );
    }
    pWriter->iOffset += cbData;

    // Seeking may move the cursor backward. The high-water mark records the
    // complete initialized prefix and therefore never shrinks on a patch.
    if ( pWriter->iOffset > pWriter->cbHighWater ) {
        pWriter->cbHighWater = pWriter->iOffset;
    }
}

u16 ConvertU16ToData( u16 value, data_byte_order_t order ) noexcept
{
    switch ( order ) {
        case data_byte_order_t::LITTLE: return Cy_HostToLittle16( value );
        case data_byte_order_t::BIG: return Cy_HostToBig16( value );
        case data_byte_order_t::NATIVE: return value;
    }
    return value;
}

u32 ConvertU32ToData( u32 value, data_byte_order_t order ) noexcept
{
    switch ( order ) {
        case data_byte_order_t::LITTLE: return Cy_HostToLittle32( value );
        case data_byte_order_t::BIG: return Cy_HostToBig32( value );
        case data_byte_order_t::NATIVE: return value;
    }
    return value;
}

u64 ConvertU64ToData( u64 value, data_byte_order_t order ) noexcept
{
    switch ( order ) {
        case data_byte_order_t::LITTLE: return Cy_HostToLittle64( value );
        case data_byte_order_t::BIG: return Cy_HostToBig64( value );
        case data_byte_order_t::NATIVE: return value;
    }
    return value;
}

} // namespace

bool_t ByteWriter_Init(
    byte_writer_t *pWriter,
    byte_span_t storage,
    data_byte_order_t byteOrder ) noexcept
{
    const bool_t bValidWriter = pWriter != nullptr;
    const bool_t bValidStorage = Span_IsValid( storage );
    const bool_t bValidOrder = IsByteOrderValid( byteOrder );
    CY_ASSERT_MSG(
        bValidWriter,
        "ByteWriter_Init requires a writer object." );
    CY_ASSERT_MSG(
        bValidStorage,
        "ByteWriter_Init requires a valid storage span." );
    if ( !bValidWriter || !bValidStorage || !bValidOrder ) {
        return CY_FALSE;
    }

    pWriter->pData = storage.pData;
    pWriter->cbCapacity = storage.nCount;
    pWriter->iOffset = 0u;
    pWriter->cbHighWater = 0u;
    pWriter->byteOrder = byteOrder;
    pWriter->status = byte_cursor_status_t::OK;
    return CY_TRUE;
}

void ByteWriter_Reset( byte_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = ByteWriter_IsValid( pWriter );
    CY_ASSERT_MSG(
        bValidWriter,
        "ByteWriter_Reset requires a valid writer." );
    if ( bValidWriter ) {
        pWriter->iOffset = 0u;
        pWriter->cbHighWater = 0u;
        pWriter->status = byte_cursor_status_t::OK;
    }
}

void ByteWriter_ClearStatus( byte_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = ByteWriter_IsValid( pWriter );
    CY_ASSERT_MSG(
        bValidWriter,
        "ByteWriter_ClearStatus requires a valid writer." );
    if ( bValidWriter ) {
        pWriter->status = byte_cursor_status_t::OK;
    }
}

bool_t ByteWriter_IsValid( const byte_writer_t *pWriter ) noexcept
{
    if ( pWriter == nullptr ||
         ( pWriter->pData == nullptr && pWriter->cbCapacity != 0u ) ||
         pWriter->iOffset > pWriter->cbCapacity ||
         pWriter->cbHighWater > pWriter->cbCapacity ||
         pWriter->iOffset > pWriter->cbHighWater ) {
        return CY_FALSE;
    }

    return IsByteOrderValid( pWriter->byteOrder ) &&
           IsByteCursorStatusValid( pWriter->status );
}

byte_cursor_status_t ByteWriter_Status(
    const byte_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = ByteWriter_IsValid( pWriter );
    CY_ASSERT_MSG(
        bValidWriter,
        "ByteWriter_Status requires a valid writer." );
    return bValidWriter
        ? pWriter->status
        : byte_cursor_status_t::INVALID_ARGUMENT;
}

usize ByteWriter_Offset( const byte_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = ByteWriter_IsValid( pWriter );
    CY_ASSERT_MSG(
        bValidWriter,
        "ByteWriter_Offset requires a valid writer." );
    return bValidWriter ? pWriter->iOffset : 0u;
}

usize ByteWriter_Capacity( const byte_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = ByteWriter_IsValid( pWriter );
    CY_ASSERT_MSG(
        bValidWriter,
        "ByteWriter_Capacity requires a valid writer." );
    return bValidWriter ? pWriter->cbCapacity : 0u;
}

usize ByteWriter_Remaining( const byte_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = ByteWriter_IsValid( pWriter );
    CY_ASSERT_MSG(
        bValidWriter,
        "ByteWriter_Remaining requires a valid writer." );
    return bValidWriter ? pWriter->cbCapacity - pWriter->iOffset : 0u;
}

usize ByteWriter_BytesWritten( const byte_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = ByteWriter_IsValid( pWriter );
    CY_ASSERT_MSG(
        bValidWriter,
        "ByteWriter_BytesWritten requires a valid writer." );
    return bValidWriter ? pWriter->cbHighWater : 0u;
}

bool_t ByteWriter_Seek( byte_writer_t *pWriter, usize iOffset ) noexcept
{
    const bool_t bValidWriter = ByteWriter_IsValid( pWriter );
    CY_ASSERT_MSG( bValidWriter, "ByteWriter_Seek requires a valid writer." );
    if ( !bValidWriter || pWriter->status != byte_cursor_status_t::OK ) {
        return CY_FALSE;
    }
    if ( iOffset > pWriter->cbHighWater ) {
        return ByteWriterFail(
            pWriter,
            byte_cursor_status_t::OUT_OF_BOUNDS );
    }
    pWriter->iOffset = iOffset;
    return CY_TRUE;
}

bool_t ByteWriter_Write(
    byte_writer_t *pWriter,
    const void *pSource,
    usize cbData ) noexcept
{
    const bool_t bValidSource = pSource != nullptr || cbData == 0u;
    CY_ASSERT_MSG(
        bValidSource,
        "ByteWriter_Write requires a source for non-empty data." );
    if ( !bValidSource ) {
        return ByteWriterFail(
            pWriter,
            byte_cursor_status_t::INVALID_ARGUMENT );
    }
    if ( !ByteWriterCanWrite( pWriter, cbData ) ) {
        return CY_FALSE;
    }

    ByteWriterCommit( pWriter, pSource, cbData );
    return CY_TRUE;
}

bool_t ByteWriter_WriteZero( byte_writer_t *pWriter, usize cbData ) noexcept
{
    if ( !ByteWriterCanWrite( pWriter, cbData ) ) {
        return CY_FALSE;
    }
    if ( cbData > 0u ) {
        Cy_MemZero( pWriter->pData + pWriter->iOffset, cbData );
    }
    pWriter->iOffset += cbData;
    if ( pWriter->iOffset > pWriter->cbHighWater ) {
        pWriter->cbHighWater = pWriter->iOffset;
    }
    return CY_TRUE;
}

bool_t ByteWriter_WriteU8( byte_writer_t *pWriter, u8 value ) noexcept
{
    return ByteWriter_Write( pWriter, &value, sizeof( value ) );
}

bool_t ByteWriter_WriteU16( byte_writer_t *pWriter, u16 value ) noexcept
{
    if ( !ByteWriterCanWrite( pWriter, sizeof( value ) ) ) {
        return CY_FALSE;
    }
    const u16 encoded = ConvertU16ToData( value, pWriter->byteOrder );
    ByteWriterCommit( pWriter, &encoded, sizeof( encoded ) );
    return CY_TRUE;
}

bool_t ByteWriter_WriteU32( byte_writer_t *pWriter, u32 value ) noexcept
{
    if ( !ByteWriterCanWrite( pWriter, sizeof( value ) ) ) {
        return CY_FALSE;
    }
    const u32 encoded = ConvertU32ToData( value, pWriter->byteOrder );
    ByteWriterCommit( pWriter, &encoded, sizeof( encoded ) );
    return CY_TRUE;
}

bool_t ByteWriter_WriteU64( byte_writer_t *pWriter, u64 value ) noexcept
{
    if ( !ByteWriterCanWrite( pWriter, sizeof( value ) ) ) {
        return CY_FALSE;
    }
    const u64 encoded = ConvertU64ToData( value, pWriter->byteOrder );
    ByteWriterCommit( pWriter, &encoded, sizeof( encoded ) );
    return CY_TRUE;
}

bool_t ByteWriter_WriteI32( byte_writer_t *pWriter, i32 value ) noexcept
{
    return ByteWriter_WriteU32( pWriter, std::bit_cast<u32>( value ) );
}

bool_t ByteWriter_WriteI64( byte_writer_t *pWriter, i64 value ) noexcept
{
    return ByteWriter_WriteU64( pWriter, std::bit_cast<u64>( value ) );
}

bool_t ByteWriter_WriteF32( byte_writer_t *pWriter, f32 value ) noexcept
{
    return ByteWriter_WriteU32( pWriter, std::bit_cast<u32>( value ) );
}

bool_t ByteWriter_WriteF64( byte_writer_t *pWriter, f64 value ) noexcept
{
    return ByteWriter_WriteU64( pWriter, std::bit_cast<u64>( value ) );
}

bool_t ByteWriter_WriteVarU64( byte_writer_t *pWriter, u64 value ) noexcept
{
    // Build the complete LEB128 representation locally, then submit one
    // bounds-checked write. A short destination never receives a partial value.
    byte encoded[10]{};
    usize cbEncoded = 0u;
    do {
        u8 nByte = static_cast<u8>( value & 0x7Fu );
        value >>= 7u;
        if ( value != 0u ) {
            nByte |= 0x80u;
        }
        encoded[cbEncoded++] = nByte;
    } while ( value != 0u );

    return ByteWriter_Write( pWriter, encoded, cbEncoded );
}

bool_t ByteWriter_WriteVarI64( byte_writer_t *pWriter, i64 value ) noexcept
{
    const u64 bits = std::bit_cast<u64>( value );
    const u64 encoded =
        ( bits << 1u ) ^ ( 0u - ( bits >> 63u ) );
    return ByteWriter_WriteVarU64( pWriter, encoded );
}

bool_t ByteWriter_WriteString(
    byte_writer_t *pWriter,
    string_view_t text,
    bool_t bWriteTerminator ) noexcept
{
    const bool_t bValidText = StringView_IsValid( text );
    CY_ASSERT_MSG(
        bValidText,
        "ByteWriter_WriteString requires a valid text view." );
    if ( !bValidText ) {
        return ByteWriterFail(
            pWriter,
            byte_cursor_status_t::INVALID_ARGUMENT );
    }
    const usize cbTerminator = bWriteTerminator ? 1u : 0u;
    if ( text.cchLength > CY_USIZE_MAX - cbTerminator ) {
        return ByteWriterFail(
            pWriter,
            byte_cursor_status_t::CURSOR_OVERFLOW );
    }
    const usize cbTotal = text.cchLength + cbTerminator;
    if ( !ByteWriterCanWrite( pWriter, cbTotal ) ) {
        return CY_FALSE;
    }

    if ( text.cchLength > 0u ) {
        Cy_MemMove(
            pWriter->pData + pWriter->iOffset,
            text.pData,
            text.cchLength );
    }
    if ( bWriteTerminator ) {
        pWriter->pData[pWriter->iOffset + text.cchLength] = 0u;
    }
    pWriter->iOffset += cbTotal;
    if ( pWriter->iOffset > pWriter->cbHighWater ) {
        pWriter->cbHighWater = pWriter->iOffset;
    }
    return CY_TRUE;
}

binary_block_t ByteWriter_Block( const byte_writer_t *pWriter ) noexcept
{
    const bool_t bValidWriter = ByteWriter_IsValid( pWriter );
    CY_ASSERT_MSG(
        bValidWriter,
        "ByteWriter_Block requires a valid writer." );
    return bValidWriter
        ? binary_block_t{ pWriter->pData, pWriter->cbHighWater }
        : binary_block_t{};
}

} // namespace cypher::common
