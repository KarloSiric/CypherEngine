//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Stream.h
//  Purpose: Declares a non-owning callback-based byte stream interface.
//  Details: Stream unifies files, memory, packages, compression, and network-backed
//           sources without inheritance. The producer owns stream state and lifetime.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Stream Contract

stream_t is a borrowed interface pair: pOps supplies behavior and pUserData identifies one backend
instance. Capability bits are promises made by the backend, not inferred from null callbacks.
ReadExact and WriteExact loop over partial transfers but stop on the first non-progress result.
================
*/

#ifndef CYPHER_COMMON_TIER1_STREAM_H
#define CYPHER_COMMON_TIER1_STREAM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum stream_capability_flags_t : flags32_t {
    STREAM_CAPABILITY_NONE       = 0u,               // No operations are promised.
    STREAM_CAPABILITY_READ       = CYPHER_BIT32( 0 ),// pfnRead is callable.
    STREAM_CAPABILITY_WRITE      = CYPHER_BIT32( 1 ),// pfnWrite is callable.
    STREAM_CAPABILITY_SEEK       = CYPHER_BIT32( 2 ),// Random positioning is supported.
    STREAM_CAPABILITY_SIZE       = CYPHER_BIT32( 3 ),// Total-size query is supported.
    STREAM_CAPABILITY_FLUSH      = CYPHER_BIT32( 4 ) // Buffered writes can be committed.
};

enum class stream_seek_origin_t : u8 {
    BEGIN = 0u, // Offset is relative to byte zero.
    CURRENT,    // Offset is relative to the current cursor.
    END         // Offset is relative to the current stream length.
};

enum class stream_status_t : u8 {
    OK = 0u,       // Operation completed without a terminal condition.
    END_OF_STREAM, // Read reached logical end before filling the request.
    INVALID_ARGUMENT, // Stream state, range, or output pointer is invalid.
    UNSUPPORTED,   // Backend does not implement the requested operation.
    IO_ERROR,      // Underlying device or file operation failed.
    OUT_OF_RANGE,  // Seek or size conversion lies outside representable bounds.
    CLOSED         // Backend handle is no longer open.
};

struct stream_io_result_t {
    stream_status_t status{ stream_status_t::OK }; // Result of this transfer attempt.
    usize cbTransferred{ 0u };                     // Bytes actually consumed or produced.
};

using stream_read_fn_t = stream_io_result_t ( * )(
    void *pUserData,
    void *pDest,
    usize cbRequested ) noexcept;

using stream_write_fn_t = stream_io_result_t ( * )(
    void *pUserData,
    const void *pSource,
    usize cbRequested ) noexcept;

using stream_seek_fn_t = stream_status_t ( * )(
    void *pUserData,
    i64 nOffset,
    stream_seek_origin_t origin,
    u64 *pPositionOut ) noexcept;

using stream_query_u64_fn_t = stream_status_t ( * )(
    void *pUserData,
    u64 *pValueOut ) noexcept;

using stream_flush_fn_t = stream_status_t ( * )( void *pUserData ) noexcept;

struct stream_ops_t {
    stream_read_fn_t pfnRead{ nullptr };       // Pull bytes from the backend.
    stream_write_fn_t pfnWrite{ nullptr };     // Push bytes to the backend.
    stream_seek_fn_t pfnSeek{ nullptr };       // Change and optionally report position.
    stream_query_u64_fn_t pfnTell{ nullptr };  // Report current byte position.
    stream_query_u64_fn_t pfnSize{ nullptr };  // Report total byte size.
    stream_flush_fn_t pfnFlush{ nullptr };     // Commit backend buffering.
};

struct stream_t {
    const stream_ops_t *pOps{ nullptr };                 // Process-lifetime operation table.
    void *pUserData{ nullptr };                          // Borrowed backend instance state.
    flags32_t capabilities{ STREAM_CAPABILITY_NONE };   // Explicit subset callable on this stream.
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Stream_IsValid( const stream_t *pStream ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Stream_HasCapabilities(
    const stream_t *pStream,
    flags32_t capabilities ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
stream_io_result_t Stream_Read(
    stream_t *pStream,
    void *pDest,
    usize cbRequested ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
stream_status_t Stream_ReadExact(
    stream_t *pStream,
    void *pDest,
    usize cbRequired ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
stream_io_result_t Stream_Write(
    stream_t *pStream,
    const void *pSource,
    usize cbRequested ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
stream_status_t Stream_WriteExact(
    stream_t *pStream,
    const void *pSource,
    usize cbRequired ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
stream_status_t Stream_Seek(
    stream_t *pStream,
    i64 nOffset,
    stream_seek_origin_t origin,
    u64 *pPositionOut = nullptr ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
stream_status_t Stream_Tell( stream_t *pStream, u64 *pPositionOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
stream_status_t Stream_Size( stream_t *pStream, u64 *pSizeOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
stream_status_t Stream_Flush( stream_t *pStream ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STREAM_H
