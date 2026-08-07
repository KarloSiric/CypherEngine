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
    LITTLE = 0u,
    BIG,
    NATIVE
};

enum class byte_cursor_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    OUT_OF_BOUNDS,
    INVALID_ENCODING,
    CURSOR_OVERFLOW
};

struct byte_reader_t {
    const byte *pData{ nullptr };
    usize cbSize{ 0u };
    usize iOffset{ 0u };
    data_byte_order_t byteOrder{ data_byte_order_t::LITTLE };
    byte_cursor_status_t status{ byte_cursor_status_t::OK };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteReader_Init(
    byte_reader_t *pReader,
    binary_block_t source,
    data_byte_order_t byteOrder = data_byte_order_t::LITTLE ) noexcept;

CYPHER_COMMON_API void ByteReader_Reset( byte_reader_t *pReader ) noexcept;
CYPHER_COMMON_API void ByteReader_ClearStatus( byte_reader_t *pReader ) noexcept;

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

// Reads a borrowed string view excluding its terminator.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteReader_ReadCString(
    byte_reader_t *pReader,
    usize cchMax,
    string_view_t *pOut ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BYTEREADER_H
