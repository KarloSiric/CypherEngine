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

#ifndef CYPHER_ENGINE_PAK_COMPRESSION_H
#define CYPHER_ENGINE_PAK_COMPRESSION_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherPak_Types.h"

namespace cypher::engine::pak
{

enum pak_compression_flags_t : common::u32 {
    CYPHER_PAK_COMPRESS_NONE            = 0u,
    CYPHER_PAK_COMPRESS_FAST            = 1u << 0u,
    CYPHER_PAK_COMPRESS_BEST            = 1u << 1u,
    CYPHER_PAK_COMPRESS_DETERMINISTIC   = 1u << 2u
};

struct pak_compression_config_t {
    pak_compression_t method{ pak_compression_t::NONE };
    common::u32 flags{ CYPHER_PAK_COMPRESS_NONE };
    common::u32 level{ 0u };
    common::u32 nChunkSize{ 0u };
};

const char *CypherPak_CompressionName( pak_compression_t method );

bool CypherPak_CompressionSupported( pak_compression_t method );

pak_error_t CypherPak_CompressBound(
    pak_compression_t method,
    common::u64 nInputSize,
    common::u64 &nOutMaxOutputSize );

pak_error_t CypherPak_Compress(
    const pak_compression_config_t &config,
    const void *input,
    common::u64 nInputSize,
    void *output,
    common::u64 nOutputSize,
    common::u64 &nOutBytesWritten );

pak_error_t CypherPak_Decompress(
    pak_compression_t method,
    const void *input,
    common::u64 nInputSize,
    void *output,
    common::u64 nOutputSize,
    common::u64 &nOutBytesWritten );

}       // namespace cypher::engine::pak

#endif // CYPHER_ENGINE_PAK_COMPRESSION_H
