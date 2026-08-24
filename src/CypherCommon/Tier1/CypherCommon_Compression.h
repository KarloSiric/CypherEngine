//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Compression.h
//  Purpose: Declares the codec-neutral compression facade.
//  Details: Callers own input and output storage. Streaming contexts use an explicit
//           allocator and wrap pinned codec backends behind stable Cypher contracts.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Compression Contract

Compression is a storage optimization, not an integrity or security boundary. Every decoder
receives an explicit output limit and must reject truncated, oversized, or inconsistent streams.
================
*/

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
    NONE = 0u, // Identity copy; useful for uniform package records.
    CYPHER_LZ, // Internal deterministic codec for bootstrap and tests.
    LZ4,       // Low-latency external codec.
    ZSTD       // Higher-ratio external codec with level control.
};

enum class compression_status_t : u8 {
    OK = 0u,           // Operation completed successfully.
    INVALID_ARGUMENT,  // Input, output, options, or stream state is invalid.
    UNSUPPORTED_CODEC, // Codec identifier has no implementation.
    UNSUPPORTED_OPTION, // Selected codec cannot honor an option.
    BACKEND_UNAVAILABLE, // Optional codec library was not built or initialized.
    OUTPUT_TOO_SMALL,  // Destination cannot hold the next or complete result.
    CORRUPT_INPUT,     // Compressed stream is malformed or truncated.
    DICTIONARY_MISMATCH, // Stream requires different dictionary bytes.
    OUT_OF_MEMORY,     // Codec or facade state allocation failed.
    INTERNAL_ERROR     // Backend failed without a more precise public status.
};

inline constexpr usize CY_COMPRESSION_SIZE_UNKNOWN = CY_USIZE_MAX; // Decoder could not predict output size.

struct compression_options_t {
    i32 nLevel{ 0 };                    // Zero selects the backend's documented default level.
    binary_block_t dictionary{};       // Borrowed codec dictionary; must match during decode.
    bool_t bChecksum{ CY_FALSE };       // Requests a backend integrity field when supported.
};

struct compression_result_t {
    compression_status_t status{ compression_status_t::OK }; // Final codec or facade status.
    usize cbRead{ 0u };                                      // Input bytes consumed.
    usize cbWritten{ 0u };                                   // Output bytes initialized.
    usize cbRequired{ 0u };                                  // Required output capacity when known.
};

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *Compression_StatusName( compression_status_t status ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Compression_IsCodecAvailable( compression_codec_t codec ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Compression_SupportsStreaming( compression_codec_t codec ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize Compression_CompressBound(
    compression_codec_t codec,
    usize cbInput,
    const compression_options_t &options = {} ) noexcept;

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
