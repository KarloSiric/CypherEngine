//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CompressionZstd.h
//  Purpose: Declares the pinned Zstandard backend adapter.
//  Details: Zstd targets package, cache, and distribution data requiring stronger
//           compression ratios, configurable levels, checksums, and dictionaries.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Compression Zstd Contract

Compression is a storage optimization, not an integrity or security boundary. Every decoder
receives an explicit output limit and must reject truncated, oversized, or inconsistent streams.
================
*/

#ifndef CYPHER_COMMON_TIER1_COMPRESSIONZSTD_H
#define CYPHER_COMMON_TIER1_COMPRESSIONZSTD_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Compression.h"

namespace cypher::common
{

CYPHER_NODISCARD CYPHER_COMMON_API
usize CompressionZstd_CompressBound( usize cbInput ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
compression_result_t CompressionZstd_Compress(
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
compression_result_t CompressionZstd_Decompress(
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize CompressionZstd_FrameContentSize( binary_block_t input ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMPRESSIONZSTD_H
