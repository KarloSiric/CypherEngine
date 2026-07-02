//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherPak/CypherPak_Compression.cpp
//  Purpose: Implements the CypherPak Pak Compression module.
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

#include "CypherPak_Compression.h"

#include <cstring>

namespace cypher::engine::pak
{

const char *CypherPak_CompressionName( const pak_compression_t method )
{
    switch ( method ) {
    case pak_compression_t::NONE:
        return "NONE";
    case pak_compression_t::LZ4:
        return "LZ4";
    case pak_compression_t::ZSTD:
        return "ZSTD";
    default:
        return "UNKNOWN";
    }
}

bool CypherPak_CompressionSupported( const pak_compression_t method )
{
    return ( method == pak_compression_t::NONE );
}

pak_error_t CypherPak_CompressBound(
    const pak_compression_t method,
    const common::u64 nInputSize,
    common::u64 &nOutMaxOutputSize )
{
    nOutMaxOutputSize = 0u;

    if ( !CypherPak_CompressionSupported( method ) ) {
        return pak_error_t::ERR_UNSUPPORTED_COMPRESSION;
    }

    nOutMaxOutputSize = nInputSize;
    return pak_error_t::OK;
}

pak_error_t CypherPak_Compress(
    const pak_compression_config_t &config,
    const void *input,
    const common::u64 nInputSize,
    void *output,
    const common::u64 nOutputSize,
    common::u64 &nOutBytesWritten )
{
    nOutBytesWritten = 0u;

    if ( !CypherPak_CompressionSupported( config.method ) ) {
        return pak_error_t::ERR_UNSUPPORTED_COMPRESSION;
    }
    if ( nInputSize > 0u && input == nullptr ) {
        return pak_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( nInputSize > 0u && output == nullptr ) {
        return pak_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( nOutputSize < nInputSize ) {
        return pak_error_t::ERR_BUFFER_TOO_SMALL;
    }

    if ( nInputSize > 0u ) {
        std::memcpy( output, input, static_cast<common::usize>( nInputSize ) );
    }
    nOutBytesWritten = nInputSize;
    return pak_error_t::OK;
}

pak_error_t CypherPak_Decompress(
    const pak_compression_t method,
    const void *input,
    const common::u64 nInputSize,
    void *output,
    const common::u64 nOutputSize,
    common::u64 &nOutBytesWritten )
{
    nOutBytesWritten = 0u;

    if ( !CypherPak_CompressionSupported( method ) ) {
        return pak_error_t::ERR_UNSUPPORTED_COMPRESSION;
    }
    if ( nInputSize > 0u && input == nullptr ) {
        return pak_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( nInputSize > 0u && output == nullptr ) {
        return pak_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( nOutputSize < nInputSize ) {
        return pak_error_t::ERR_BUFFER_TOO_SMALL;
    }

    if ( nInputSize > 0u ) {
        std::memcpy( output, input, static_cast<common::usize>( nInputSize ) );
    }
    nOutBytesWritten = nInputSize;
    return pak_error_t::OK;
}

}       // namespace cypher::engine::pak
