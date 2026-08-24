//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherPak/CypherPak_Compression.h
//  Purpose: Declares the CypherPak Pak Compression module.
//  Details: This file participates in the CypherPak archive format and package access
//           path. Keep binary layout, endian rules, and validation stable so shipped
//           content remains readable.
//
//  History:
//  - Created by Karlo Siric on 2026-06-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Compression Contract

Package compression is selected per entry and remains independent of package integrity checks.
Decoders receive exact compressed and uncompressed limits and reject partial output.
================
*/

#ifndef CYPHER_ENGINE_PAK_COMPRESSION_H
#define CYPHER_ENGINE_PAK_COMPRESSION_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherPak_Types.h"

namespace cypher::engine
{

enum pak_compression_flags_t : common::u32 {
    CYPHER_PAK_COMPRESS_NONE            = 0u,       // Use the codec's normal level and behavior.
    CYPHER_PAK_COMPRESS_FAST            = 1u << 0u, // Prefer build speed over archive size.
    CYPHER_PAK_COMPRESS_BEST            = 1u << 1u, // Prefer archive size over build speed.
    CYPHER_PAK_COMPRESS_DETERMINISTIC   = 1u << 2u  // Require byte-identical output for identical input.
};

struct pak_compression_config_t {
    pak_compression_t method{ pak_compression_t::NONE }; // Codec used for the payload.
    common::u32 flags{ CYPHER_PAK_COMPRESS_NONE };       // pak_compression_flags_t policy bits.
    common::u32 level{ 0u };                             // Codec-specific level; zero selects its default.
    common::u32 nChunkSize{ 0u };                        // Reserved streaming chunk size in bytes; zero is automatic.
};

const char *Pak_CompressionName( pak_compression_t method );

bool Pak_CompressionSupported( pak_compression_t method );

pak_error_t Pak_CompressBound(
    pak_compression_t method,
    common::u64 nInputSize,
    common::u64 &nOutMaxOutputSize );

pak_error_t Pak_Compress(
    const pak_compression_config_t &config,
    const void *input,
    common::u64 nInputSize,
    void *output,
    common::u64 nOutputSize,
    common::u64 &nOutBytesWritten );

pak_error_t Pak_Decompress(
    pak_compression_t method,
    const void *input,
    common::u64 nInputSize,
    void *output,
    common::u64 nOutputSize,
    common::u64 &nOutBytesWritten );

}       // namespace cypher::engine

#endif // CYPHER_ENGINE_PAK_COMPRESSION_H
