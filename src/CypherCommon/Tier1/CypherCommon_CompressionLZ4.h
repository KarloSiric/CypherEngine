//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CompressionLZ4.h
//  Purpose: Declares the pinned LZ4 backend adapter.
//  Details: LZ4 targets very fast compression/decompression for runtime streaming and
//           transient package blocks where ratio is secondary to latency.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_COMPRESSIONLZ4_H
#define CYPHER_COMMON_TIER1_COMPRESSIONLZ4_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Compression.h"

namespace cypher::common
{

CYPHER_NODISCARD CYPHER_COMMON_API
usize CompressionLZ4_CompressBound(
    usize cbInput,
    const compression_options_t &options = {} ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
compression_result_t CompressionLZ4_Compress(
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options = {} ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
compression_result_t CompressionLZ4_Decompress(
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options = {} ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMPRESSIONLZ4_H
