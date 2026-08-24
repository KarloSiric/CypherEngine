//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CompressionZstd.cpp
//  Purpose: Implements the Zstandard backend adapter.
//  Details: One-shot compression supports pinned levels, content checksums, and
//           caller-provided dictionaries while preserving Cypher status semantics.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Compression Zstd Implementation Notes

Compression is a storage optimization, not an integrity or security boundary. Every decoder
receives an explicit output limit and must reject truncated, oversized, or inconsistent streams.
================
*/

#include "CypherCommon_CompressionZstd.h"

#include <zstd.h>
#include <zstd_errors.h>

namespace cypher::common
{

namespace
{

CYPHER_NODISCARD compression_result_t Fail(
    compression_status_t status,
    usize cbRequired = 0u ) noexcept
{
    return { status, 0u, 0u, cbRequired };
}

CYPHER_NODISCARD compression_status_t MapZstdError(
    usize result,
    bool_t bDecompress ) noexcept
{
    if ( ZSTD_isError( result ) == 0u ) {
        return compression_status_t::OK;
    }
    switch ( ZSTD_getErrorCode( result ) ) {
        case ZSTD_error_dstSize_tooSmall:
            return compression_status_t::OUTPUT_TOO_SMALL;
        case ZSTD_error_dictionary_wrong:
            return compression_status_t::DICTIONARY_MISMATCH;
        case ZSTD_error_memory_allocation:
            return compression_status_t::OUT_OF_MEMORY;
        default:
            return bDecompress
                ? compression_status_t::CORRUPT_INPUT
                : compression_status_t::INTERNAL_ERROR;
    }
}

CYPHER_NODISCARD bool_t IsValidArguments(
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options ) noexcept
{
    return BinaryBlock_IsValid( input ) &&
           Span_IsValid( output ) &&
           BinaryBlock_IsValid( options.dictionary );
}

} // namespace

usize CompressionZstd_CompressBound( usize cbInput ) noexcept
{
    const usize cbBound = ZSTD_compressBound( cbInput );
    return ZSTD_isError( cbBound ) != 0u ? 0u : cbBound;
}

compression_result_t CompressionZstd_Compress(
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options ) noexcept
{
    if ( !IsValidArguments( input, output, options ) ) {
        return Fail( compression_status_t::INVALID_ARGUMENT );
    }

    const usize cbBound = CompressionZstd_CompressBound( input.cbSize );
    if ( cbBound == 0u ) {
        return Fail( compression_status_t::INTERNAL_ERROR );
    }
    if ( output.nCount < cbBound ) {
        return Fail( compression_status_t::OUTPUT_TOO_SMALL, cbBound );
    }

    ZSTD_CCtx *pContext = ZSTD_createCCtx();
    if ( pContext == nullptr ) {
        return Fail( compression_status_t::OUT_OF_MEMORY );
    }

    // Configure the reusable context completely before presenting input data.
    // The first backend error remains authoritative for status translation.
    usize backendResult = ZSTD_CCtx_setParameter(
        pContext,
        ZSTD_c_compressionLevel,
        options.nLevel );
    if ( ZSTD_isError( backendResult ) == 0u ) {
        backendResult = ZSTD_CCtx_setParameter(
            pContext,
            ZSTD_c_checksumFlag,
            options.bChecksum ? 1 : 0 );
    }
    if ( ZSTD_isError( backendResult ) == 0u &&
         options.dictionary.cbSize != 0u ) {
        backendResult = ZSTD_CCtx_loadDictionary(
            pContext,
            options.dictionary.pData,
            options.dictionary.cbSize );
    }
    if ( ZSTD_isError( backendResult ) == 0u ) {
        backendResult = ZSTD_compress2(
            pContext,
            output.pData,
            output.nCount,
            input.pData,
            input.cbSize );
    }

    const compression_status_t status = MapZstdError(
        backendResult,
        CY_FALSE );
    static_cast<void>( ZSTD_freeCCtx( pContext ) );
    if ( status != compression_status_t::OK ) {
        return Fail( status, cbBound );
    }
    return {
        compression_status_t::OK,
        input.cbSize,
        backendResult,
        backendResult
    };
}

compression_result_t CompressionZstd_Decompress(
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options ) noexcept
{
    if ( !IsValidArguments( input, output, options ) ) {
        return Fail( compression_status_t::INVALID_ARGUMENT );
    }

    // Known frame sizes allow an early, mutation-free capacity rejection.
    const usize cbRequired = CompressionZstd_FrameContentSize( input );
    // A successful decoder must still agree with an advertised frame size.
    if ( cbRequired != CY_COMPRESSION_SIZE_UNKNOWN &&
         output.nCount < cbRequired ) {
        return Fail( compression_status_t::OUTPUT_TOO_SMALL, cbRequired );
    }

    ZSTD_DCtx *pContext = ZSTD_createDCtx();
    if ( pContext == nullptr ) {
        return Fail( compression_status_t::OUT_OF_MEMORY, cbRequired );
    }
    usize backendResult = 0u;
    if ( options.dictionary.cbSize != 0u ) {
        backendResult = ZSTD_DCtx_loadDictionary(
            pContext,
            options.dictionary.pData,
            options.dictionary.cbSize );
    }
    if ( ZSTD_isError( backendResult ) == 0u ) {
        backendResult = ZSTD_decompressDCtx(
            pContext,
            output.pData,
            output.nCount,
            input.pData,
            input.cbSize );
    }

    const compression_status_t status = MapZstdError(
        backendResult,
        CY_TRUE );
    static_cast<void>( ZSTD_freeDCtx( pContext ) );
    if ( status != compression_status_t::OK ) {
        return Fail( status, cbRequired );
    }
    if ( cbRequired != CY_COMPRESSION_SIZE_UNKNOWN &&
         backendResult != cbRequired ) {
        return Fail( compression_status_t::CORRUPT_INPUT, cbRequired );
    }
    return {
        compression_status_t::OK,
        input.cbSize,
        backendResult,
        backendResult
    };
}

usize CompressionZstd_FrameContentSize( binary_block_t input ) noexcept
{
    if ( !BinaryBlock_IsValid( input ) ) {
        return CY_COMPRESSION_SIZE_UNKNOWN;
    }
    const unsigned long long cbContent = ZSTD_getFrameContentSize(
        input.pData,
        input.cbSize );
    if ( cbContent == ZSTD_CONTENTSIZE_ERROR ||
         cbContent == ZSTD_CONTENTSIZE_UNKNOWN ||
         cbContent > static_cast<unsigned long long>( CY_USIZE_MAX ) ) {
        return CY_COMPRESSION_SIZE_UNKNOWN;
    }
    return static_cast<usize>( cbContent );
}

} // namespace cypher::common
