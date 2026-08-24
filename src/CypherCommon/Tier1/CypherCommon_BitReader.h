//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BitReader.h
//  Purpose: Declares bounds-checked bitstream readers.
//  Details: BitReader borrows immutable data and supports explicit least- or
//           most-significant-bit-first packing within bytes and fields.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_BITREADER_H
#define CYPHER_COMMON_TIER1_BITREADER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BinaryBlock.h"

namespace cypher::common
{

enum class bit_order_t : u8 {
    LEAST_SIGNIFICANT_FIRST = 0u, // Stream bit zero is byte bit zero; fields consume low bits first.
    MOST_SIGNIFICANT_FIRST       // Stream bit zero is byte bit seven; fields consume high bits first.
};

enum class bit_cursor_status_t : u8 {
    OK = 0u,           // No operation has failed.
    INVALID_ARGUMENT, // Null output or inconsistent source state.
    OUT_OF_BOUNDS,    // Requested field extends beyond nBitSize.
    INVALID_BIT_COUNT,// A scalar field requested zero or more than 64 bits.
    VALUE_OUT_OF_RANGE,// Signed or unsigned value cannot fit in the requested width.
    CURSOR_OVERFLOW   // Bit-position arithmetic overflowed before access.
};

struct bit_reader_t {
    const byte *pData{ nullptr };                                  // Borrowed packed-bit source.
    usize nBitSize{ 0u };                                          // Readable bits; may end inside a byte.
    usize iBit{ 0u };                                               // Next unread stream bit.
    bit_order_t bitOrder{ bit_order_t::LEAST_SIGNIFICANT_FIRST };  // Bit numbering within each byte.
    bit_cursor_status_t status{ bit_cursor_status_t::OK };         // Sticky first-failure state.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitReader_Init(
    bit_reader_t *pReader,
    binary_block_t source,
    usize nBitSize,
    bit_order_t bitOrder = bit_order_t::LEAST_SIGNIFICANT_FIRST ) noexcept;

CYPHER_COMMON_API void BitReader_Reset( bit_reader_t *pReader ) noexcept;
CYPHER_COMMON_API void BitReader_ClearStatus( bit_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitReader_IsValid( const bit_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bit_cursor_status_t BitReader_Status( const bit_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BitReader_Offset( const bit_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BitReader_Size( const bit_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BitReader_Remaining( const bit_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitReader_Seek( bit_reader_t *pReader, usize iBit ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitReader_Skip( bit_reader_t *pReader, usize nBits ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitReader_AlignToByte( bit_reader_t *pReader ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitReader_ReadBits(
    bit_reader_t *pReader,
    u32 nBits,
    u64 *pValueOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitReader_ReadSignedBits(
    bit_reader_t *pReader,
    u32 nBits,
    i64 *pValueOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitReader_ReadBool(
    bit_reader_t *pReader,
    bool_t *pValueOut ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BITREADER_H
