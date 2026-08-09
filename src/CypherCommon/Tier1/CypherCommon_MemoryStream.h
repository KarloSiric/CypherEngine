//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_MemoryStream.h
//  Purpose: Declares stream adapters over borrowed memory.
//  Details: MemoryStream owns no bytes. Read-only and writable initialization expose
//           only the capabilities supported by the supplied storage.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_MEMORYSTREAM_H
#define CYPHER_COMMON_TIER1_MEMORYSTREAM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_Stream.h"

namespace cypher::common
{

struct memory_stream_t {
    const byte *pReadData{ nullptr };
    byte *pWriteData{ nullptr };
    usize cbSize{ 0u };
    usize cbCapacity{ 0u };
    usize iPosition{ 0u };
    bool_t bWritable{ CY_FALSE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryStream_InitRead(
    memory_stream_t *pMemoryStream,
    binary_block_t source ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryStream_InitWrite(
    memory_stream_t *pMemoryStream,
    byte_span_t storage,
    usize cbInitialSize = 0u ) noexcept;

CYPHER_COMMON_API void MemoryStream_Reset( memory_stream_t *pMemoryStream ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryStream_IsValid( const memory_stream_t *pMemoryStream ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryStream_IsWritable( const memory_stream_t *pMemoryStream ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize MemoryStream_Size( const memory_stream_t *pMemoryStream ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize MemoryStream_Capacity( const memory_stream_t *pMemoryStream ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize MemoryStream_Position( const memory_stream_t *pMemoryStream ) noexcept;

// Changes writable logical size. Newly exposed bytes are initialized to zero.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryStream_SetSize(
    memory_stream_t *pMemoryStream,
    usize cbSize ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t MemoryStream_Clear( memory_stream_t *pMemoryStream ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
stream_t MemoryStream_AsStream( memory_stream_t *pMemoryStream ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
binary_block_t MemoryStream_Block( const memory_stream_t *pMemoryStream ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_MEMORYSTREAM_H
