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

// buffer_t is a writable cursor over caller-owned storage. It never grows the backing allocation.
// Bytes in [0, cbSize) are logical content; [cbSize, cbCapacity) is unused writable storage.

struct buffer_t {
    byte *pData{ nullptr };       // Borrowed first byte of the backing storage.
    usize cbSize{ 0u };           // Logical bytes currently published to readers.
    usize cbCapacity{ 0u };       // Total writable byte extent at pData.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_Init( buffer_t *pBuffer, byte_span_t storage ) noexcept;

CYPHER_COMMON_API void Buffer_Clear( buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_IsValid( const buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_IsEmpty( const buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte *Buffer_Data( buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
const byte *Buffer_Data( const buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize Buffer_Size( const buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize Buffer_Capacity( const buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize Buffer_Remaining( const buffer_t *pBuffer ) noexcept;

// Changes logical size without initializing newly exposed bytes.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_Resize( buffer_t *pBuffer, usize cbSize ) noexcept;

// Replaces the logical contents with one source block.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_Assign( buffer_t *pBuffer, binary_block_t source ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_Append(
    buffer_t *pBuffer,
    const void *pData,
    usize cbData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_AppendBlock(
    buffer_t *pBuffer,
    binary_block_t source ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_AppendByte( buffer_t *pBuffer, byte value ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Buffer_AppendZero( buffer_t *pBuffer, usize cbData ) noexcept;

// Appends uninitialized bytes and returns their writable range.
CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t Buffer_AppendUninitialized(
    buffer_t *pBuffer,
    usize cbData ) noexcept;

// Returns the logical writable contents [0, cbSize).
CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t Buffer_WritableSpan( buffer_t *pBuffer ) noexcept;

// Returns the unused writable tail [cbSize, cbCapacity).
CYPHER_NODISCARD CYPHER_COMMON_API
byte_span_t Buffer_RemainingSpan( buffer_t *pBuffer ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t Buffer_Block( const buffer_t *pBuffer ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_BUFFER_H
