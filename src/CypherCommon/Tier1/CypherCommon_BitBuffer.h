//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_BitBuffer.h
//  Purpose: Declares non-owning fixed-capacity bit storage.
//  Details: BitBuffer tracks an exact logical bit count over caller-provided bytes.
//           Bit index zero is the least-significant bit of byte zero.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_BITBUFFER_H
#define CYPHER_COMMON_TIER1_BITBUFFER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BinaryBlock.h"

namespace cypher::common
{

struct bit_buffer_t {
    byte *pData{ nullptr };
    usize nBitSize{ 0u };
    usize nBitCapacity{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitBuffer_Init( bit_buffer_t *pBuffer, byte_span_t storage ) noexcept;

CYPHER_COMMON_API void BitBuffer_Clear( bit_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitBuffer_IsValid( const bit_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitBuffer_IsEmpty( const bit_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BitBuffer_Size( const bit_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize BitBuffer_Capacity( const bit_buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitBuffer_Resize(
    bit_buffer_t *pBuffer,
    usize nBitSize,
    bool_t bFillValue = CY_FALSE ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitBuffer_Get(
    const bit_buffer_t *pBuffer,
    usize iBit,
    bool_t *pValueOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t BitBuffer_Set(
    bit_buffer_t *pBuffer,
    usize iBit,
    bool_t value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t BitBuffer_Block( const bit_buffer_t *pBuffer ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BITBUFFER_H
