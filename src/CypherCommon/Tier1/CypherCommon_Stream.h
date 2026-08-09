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

#ifndef CYPHER_COMMON_TIER1_STREAM_H
#define CYPHER_COMMON_TIER1_STREAM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

enum stream_capability_flags_t : flags32_t {
    STREAM_CAPABILITY_NONE       = 0u,
    STREAM_CAPABILITY_READ       = CYPHER_BIT32( 0 ),
    STREAM_CAPABILITY_WRITE      = CYPHER_BIT32( 1 ),
    STREAM_CAPABILITY_SEEK       = CYPHER_BIT32( 2 ),
    STREAM_CAPABILITY_SIZE       = CYPHER_BIT32( 3 ),
    STREAM_CAPABILITY_FLUSH      = CYPHER_BIT32( 4 )
};

enum class stream_seek_origin_t : u8 {
    BEGIN = 0u,
    CURRENT,
    END
};

enum class stream_status_t : u8 {
    OK = 0u,
    END_OF_STREAM,
    INVALID_ARGUMENT,
    UNSUPPORTED,
    IO_ERROR,
    OUT_OF_RANGE,
    CLOSED
};

struct stream_io_result_t {
    stream_status_t status{ stream_status_t::OK };
    usize cbTransferred{ 0u };
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
    stream_read_fn_t pfnRead{ nullptr };
    stream_write_fn_t pfnWrite{ nullptr };
    stream_seek_fn_t pfnSeek{ nullptr };
    stream_query_u64_fn_t pfnTell{ nullptr };
    stream_query_u64_fn_t pfnSize{ nullptr };
    stream_flush_fn_t pfnFlush{ nullptr };
};

struct stream_t {
    const stream_ops_t *pOps{ nullptr };
    void *pUserData{ nullptr };
    flags32_t capabilities{ STREAM_CAPABILITY_NONE };
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
