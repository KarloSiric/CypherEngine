//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ByteWriter.h
//  Purpose: Declares bounds-checked sequential binary writers.
//  Details: ByteWriter borrows fixed caller storage, performs explicit endian
//           conversion, and never emits partial values after an out-of-space failure.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Byte Writer Contract

Writes never advance iOffset unless the complete operation fits. Seeking backward does not erase
published bytes, so cbHighWater records the complete initialized prefix returned by Block. Status
is sticky and prevents a caller from accidentally continuing after truncated output.
================
*/

#ifndef CYPHER_COMMON_TIER1_BYTEWRITER_H
#define CYPHER_COMMON_TIER1_BYTEWRITER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ByteReader.h"

namespace cypher::common
{

struct byte_writer_t {
    byte *pData{ nullptr };                                    // Borrowed writable storage.
    usize cbCapacity{ 0u };                                    // Maximum addressable bytes.
    usize iOffset{ 0u };                                       // Position of the next write.
    usize cbHighWater{ 0u };                                   // Furthest byte initialized by any write.
    data_byte_order_t byteOrder{ data_byte_order_t::LITTLE };  // Order used for fixed-width values.
    byte_cursor_status_t status{ byte_cursor_status_t::OK };   // Sticky first-failure state.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteWriter_Init(
    byte_writer_t *pWriter,
    byte_span_t storage,
    data_byte_order_t byteOrder = data_byte_order_t::LITTLE ) noexcept;

CYPHER_COMMON_API void ByteWriter_Reset( byte_writer_t *pWriter ) noexcept;
CYPHER_COMMON_API void ByteWriter_ClearStatus( byte_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteWriter_IsValid( const byte_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_cursor_status_t ByteWriter_Status(
    const byte_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize ByteWriter_Offset( const byte_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize ByteWriter_Capacity( const byte_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize ByteWriter_Remaining( const byte_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize ByteWriter_BytesWritten( const byte_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteWriter_Seek( byte_writer_t *pWriter, usize iOffset ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteWriter_Write(
    byte_writer_t *pWriter,
    const void *pSource,
    usize cbData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteWriter_WriteZero( byte_writer_t *pWriter, usize cbData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteWriter_WriteU8( byte_writer_t *pWriter, u8 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteWriter_WriteU16( byte_writer_t *pWriter, u16 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteWriter_WriteU32( byte_writer_t *pWriter, u32 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteWriter_WriteU64( byte_writer_t *pWriter, u64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteWriter_WriteI32( byte_writer_t *pWriter, i32 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteWriter_WriteI64( byte_writer_t *pWriter, i64 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteWriter_WriteF32( byte_writer_t *pWriter, f32 value ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteWriter_WriteF64( byte_writer_t *pWriter, f64 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteWriter_WriteVarU64( byte_writer_t *pWriter, u64 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteWriter_WriteVarI64( byte_writer_t *pWriter, i64 value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteWriter_WriteString(
    byte_writer_t *pWriter,
    string_view_t text,
    bool_t bWriteTerminator ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t ByteWriter_Block( const byte_writer_t *pWriter ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BYTEWRITER_H
