//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CompressionLZ.h
//  Purpose: Declares the internal deterministic Cypher LZ codec.
//  Details: This small codec exists for learning and format-controlled use. LZ4 or
//           Zstd should remain the default for production package and streaming data.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_COMPRESSIONLZ_H
#define CYPHER_COMMON_TIER1_COMPRESSIONLZ_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Compression.h"

namespace cypher::common
{

CYPHER_NODISCARD CYPHER_COMMON_API
usize CompressionLZ_CompressBound( usize cbInput ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
compression_result_t CompressionLZ_Compress(
    binary_block_t input,
    byte_span_t output ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
compression_result_t CompressionLZ_Decompress(
    binary_block_t input,
    byte_span_t output ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_COMPRESSIONLZ_H
