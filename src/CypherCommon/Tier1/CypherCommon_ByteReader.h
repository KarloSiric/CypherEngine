//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ByteReader.h
//  Purpose: Declares bounds-checked sequential binary readers.
//  Details: ByteReader borrows immutable memory, performs explicit endian conversion,
//           and becomes sticky-failed after an invalid read until reset or cleared.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_BYTEREADER_H
#define CYPHER_COMMON_TIER1_BYTEREADER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum class data_byte_order_t : u8 {
    LITTLE = 0u,  // Serialized least-significant byte first.
    BIG,          // Serialized most-significant byte first.
    NATIVE        // Host order; unsuitable for portable persistent formats.
};

enum class byte_cursor_status_t : u8 {
    OK = 0u,          // No operation has failed.
    INVALID_ARGUMENT,// Null output or inconsistent source state.
    OUT_OF_BOUNDS,   // Requested bytes do not fit in the supplied region.
    INVALID_ENCODING,// Encoded integer or string violates its representation.
    CURSOR_OVERFLOW  // Offset arithmetic exceeded usize before a memory access.
};

// Reader failures are sticky. Once status changes from OK, later reads fail without advancing
// until ByteReader_ClearStatus is called or the reader is reset.
struct byte_reader_t {
    const byte *pData{ nullptr };                              // Borrowed serialized bytes.
    usize cbSize{ 0u };                                        // Total readable byte extent.
    usize iOffset{ 0u };                                       // Next unread byte.
    data_byte_order_t byteOrder{ data_byte_order_t::LITTLE };  // Order used for fixed-width values.
    byte_cursor_status_t status{ byte_cursor_status_t::OK };   // Sticky first-failure state.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteReader_Init(
    byte_reader_t *pReader,
    binary_block_t source,
    data_byte_order_t byteOrder = data_byte_order_t::LITTLE ) noexcept;

CYPHER_COMMON_API void ByteReader_Reset( byte_reader_t *pReader ) noexcept;
CYPHER_COMMON_API void ByteReader_ClearStatus( byte_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteReader_IsValid( const byte_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_cursor_status_t ByteReader_Status(
    const byte_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize ByteReader_Offset( const byte_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize ByteReader_Size( const byte_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize ByteReader_Remaining( const byte_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteReader_Seek( byte_reader_t *pReader, usize iOffset ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteReader_Skip( byte_reader_t *pReader, usize cbData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteReader_Read(
    byte_reader_t *pReader,
    void *pDest,
    usize cbData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteReader_ReadBlock(
    byte_reader_t *pReader,
    usize cbData,
    binary_block_t *pBlockOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteReader_ReadU8( byte_reader_t *pReader, u8 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteReader_ReadU16( byte_reader_t *pReader, u16 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteReader_ReadU32( byte_reader_t *pReader, u32 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteReader_ReadU64( byte_reader_t *pReader, u64 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteReader_ReadI32( byte_reader_t *pReader, i32 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteReader_ReadI64( byte_reader_t *pReader, i64 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteReader_ReadF32( byte_reader_t *pReader, f32 *pOut ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API bool_t ByteReader_ReadF64( byte_reader_t *pReader, f64 *pOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteReader_ReadVarU64( byte_reader_t *pReader, u64 *pOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteReader_ReadVarI64( byte_reader_t *pReader, i64 *pOut ) noexcept;

// Reads a borrowed string view excluding its terminator. cchMax is the maximum
// number of characters before the required terminator.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteReader_ReadCString(
    byte_reader_t *pReader,
    usize cchMax,
    string_view_t *pOut ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BYTEREADER_H
