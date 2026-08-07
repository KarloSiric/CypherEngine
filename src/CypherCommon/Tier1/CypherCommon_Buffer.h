//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Buffer.h
//  Purpose: Declares non-owning bounded writable byte buffers.
//  Details: Buffer tracks used bytes inside caller storage and never reallocates.
//           Failed writes leave existing bytes intact.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_BUFFER_H
#define CYPHER_COMMON_TIER1_BUFFER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BinaryBlock.h"

namespace cypher::common
{

struct buffer_t {
    byte *pData{ nullptr };
    usize cbSize{ 0u };
    usize cbCapacity{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_Init( buffer_t *pBuffer, byte_span_t storage ) noexcept;

CYPHER_COMMON_API void Buffer_Clear( buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_IsValid( const buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize Buffer_Remaining( const buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_Resize( buffer_t *pBuffer, usize cbSize ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_Append(
    buffer_t *pBuffer,
    const void *pData,
    usize cbData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_AppendZero( buffer_t *pBuffer, usize cbData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t Buffer_WritableSpan( buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t Buffer_Block( const buffer_t *pBuffer ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BUFFER_H
