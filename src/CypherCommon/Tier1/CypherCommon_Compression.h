//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Compression.h
//  Purpose: Declares the codec-neutral compression facade.
//  Details: One-shot calls never allocate. Streaming contexts use an explicit
//           allocator and wrap pinned codec backends behind stable Cypher contracts.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_COMPRESSION_H
#define CYPHER_COMMON_TIER1_COMPRESSION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_Stream.h"

namespace cypher::common
{

enum class compression_codec_t : u8 {
    NONE = 0u,
    CYPHER_LZ,
    LZ4,
    ZSTD
};

enum class compression_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    UNSUPPORTED_CODEC,
    BACKEND_UNAVAILABLE,
    OUTPUT_TOO_SMALL,
    CORRUPT_INPUT,
    DICTIONARY_MISMATCH,
    OUT_OF_MEMORY,
    INTERNAL_ERROR
};

struct compression_options_t {
    i32 nLevel{ 0 };
    binary_block_t dictionary{};
    bool_t bChecksum{ CY_FALSE };
};

struct compression_result_t {
    compression_status_t status{ compression_status_t::OK };
    usize cbRead{ 0u };
    usize cbWritten{ 0u };
    usize cbRequired{ 0u };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Compression_IsCodecAvailable( compression_codec_t codec ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize Compression_CompressBound(
    compression_codec_t codec,
    usize cbInput ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
compression_result_t Compression_Compress(
    compression_codec_t codec,
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options = {} ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
compression_result_t Compression_Decompress(
    compression_codec_t codec,
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options = {} ) noexcept;

struct compression_stream_t;

CYPHER_NODISCARD CYPHER_COMMON_API
compression_stream_t *CompressionStream_Create(
    compression_codec_t codec,
    bool_t bCompress,
    const compression_options_t &options,
    const allocator_t *pAllocator ) noexcept;

CYPHER_COMMON_API void CompressionStream_Destroy(
    compression_stream_t *pContext ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
compression_result_t CompressionStream_Process(
    compression_stream_t *pContext,
    binary_block_t input,
    byte_span_t output,
    bool_t bFinish ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMPRESSION_H
