//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BitWriter.h
//  Purpose: Declares bounds-checked bitstream writers.
//  Details: BitWriter borrows fixed storage and commits no partial field when the
//           requested write exceeds remaining capacity.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Bit Writer Contract

Fields are committed only when the complete bit count fits. nBitHighWater survives backward seeks
and defines the published prefix. Callers must clear or initialize storage before writing when
untouched padding bits need deterministic values.
================
*/

#ifndef CYPHER_COMMON_TIER1_BITWRITER_H
#define CYPHER_COMMON_TIER1_BITWRITER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BitReader.h"

namespace cypher::common
{

struct bit_writer_t {
    byte *pData{ nullptr };                                        // Borrowed packed-bit destination.
    usize nBitCapacity{ 0u };                                      // Writable bits in the destination.
    usize iBit{ 0u };                                               // Position of the next written bit.
    usize nBitHighWater{ 0u };                                      // Furthest bit initialized by any write.
    bit_order_t bitOrder{ bit_order_t::LEAST_SIGNIFICANT_FIRST };  // Bit numbering within each byte.
    bit_cursor_status_t status{ bit_cursor_status_t::OK };         // Sticky first-failure state.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitWriter_Init(
    bit_writer_t *pWriter,
    byte_span_t storage,
    bit_order_t bitOrder = bit_order_t::LEAST_SIGNIFICANT_FIRST ) noexcept;

CYPHER_COMMON_API void BitWriter_Reset( bit_writer_t *pWriter ) noexcept;
CYPHER_COMMON_API void BitWriter_ClearStatus( bit_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitWriter_IsValid( const bit_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bit_cursor_status_t BitWriter_Status( const bit_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BitWriter_Offset( const bit_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BitWriter_Capacity( const bit_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BitWriter_Remaining( const bit_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BitWriter_BitsWritten( const bit_writer_t *pWriter ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitWriter_Seek( bit_writer_t *pWriter, usize iBit ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitWriter_AlignToByte(
    bit_writer_t *pWriter,
    bool_t bFillValue = CY_FALSE ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitWriter_WriteBits(
    bit_writer_t *pWriter,
    u64 value,
    u32 nBits ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitWriter_WriteSignedBits(
    bit_writer_t *pWriter,
    i64 value,
    u32 nBits ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitWriter_WriteBool(
    bit_writer_t *pWriter,
    bool_t value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t BitWriter_Block( const bit_writer_t *pWriter ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BITWRITER_H
