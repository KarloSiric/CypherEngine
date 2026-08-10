//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Compression.cpp
//  Purpose: Implements the codec-neutral compression facade.
//  Details: The facade centralizes validation, backend selection, and incremental
//           stream lifetime while keeping all input/output storage caller-owned.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Compression.h"

#include "CypherCommon_CompressionLZ.h"
#include "CypherCommon_CompressionLZ4.h"
#include "CypherCommon_CompressionZstd.h"

#include <lz4frame.h>
#include <zstd.h>

#include <new>

namespace cypher::common
{

struct compression_stream_t {
    compression_codec_t codec{ compression_codec_t::NONE };
    bool_t bCompress{ CY_FALSE };
    bool_t bBegan{ CY_FALSE };
    bool_t bFinishSeen{ CY_FALSE };
    bool_t bFinished{ CY_FALSE };
    compression_options_t options{};
    const allocator_t *pAllocator{ nullptr };
    void *pBackend{ nullptr };
};

namespace
{

CYPHER_NODISCARD compression_result_t Fail(
    compression_status_t status,
    usize cbRequired = 0u ) noexcept
{
    return { status, 0u, 0u, cbRequired };
}

CYPHER_NODISCARD bool_t HasDefaultOptions(
    const compression_options_t &options ) noexcept
{
    return options.nLevel == 0 &&
           BinaryBlock_IsValid( options.dictionary ) &&
           options.dictionary.cbSize == 0u &&
           !options.bChecksum;
}

CYPHER_NODISCARD compression_result_t CopyUncompressed(
    binary_block_t input,
    byte_span_t output ) noexcept
{
    if ( !BinaryBlock_IsValid( input ) || !Span_IsValid( output ) ) {
        return Fail( compression_status_t::INVALID_ARGUMENT );
    }
    if ( output.nCount < input.cbSize ) {
        return Fail( compression_status_t::OUTPUT_TOO_SMALL, input.cbSize );
    }
    if ( input.cbSize != 0u ) {
        Cy_MemMove( output.pData, input.pData, input.cbSize );
    }
    return {
        compression_status_t::OK,
        input.cbSize,
        input.cbSize,
        input.cbSize
    };
}

CYPHER_NODISCARD LZ4F_preferences_t MakeLZ4Preferences(
    const compression_options_t &options ) noexcept
{
    LZ4F_preferences_t preferences = LZ4F_INIT_PREFERENCES;
    preferences.frameInfo.contentChecksumFlag = options.bChecksum
        ? LZ4F_contentChecksumEnabled
        : LZ4F_noContentChecksum;
    preferences.compressionLevel = options.nLevel;
    preferences.autoFlush = 1u;
    return preferences;
}

CYPHER_NODISCARD compression_result_t ProcessNone(
    compression_stream_t &stream,
    binary_block_t input,
    byte_span_t output,
    bool_t bFinish ) noexcept
{
    const usize cbTransfer = input.cbSize < output.nCount
        ? input.cbSize
        : output.nCount;
    if ( cbTransfer != 0u ) {
        Cy_MemMove( output.pData, input.pData, cbTransfer );
    }
    if ( bFinish && cbTransfer == input.cbSize ) {
        stream.bFinished = CY_TRUE;
    }
    return {
        cbTransfer == input.cbSize
            ? compression_status_t::OK
            : compression_status_t::OUTPUT_TOO_SMALL,
        cbTransfer,
        cbTransfer,
        input.cbSize
    };
}

CYPHER_NODISCARD compression_result_t ProcessLZ4Compress(
    compression_stream_t &stream,
    binary_block_t input,
    byte_span_t output,
    bool_t bFinish ) noexcept
{
    auto *pContext = static_cast<LZ4F_cctx *>( stream.pBackend );
    const LZ4F_preferences_t preferences = MakeLZ4Preferences( stream.options );
    usize cbRequired = stream.bBegan ? 0u : LZ4F_HEADER_SIZE_MAX;
    const usize cbUpdate = LZ4F_compressBound( input.cbSize, &preferences );
    if ( LZ4F_isError( cbUpdate ) != 0u ||
         cbUpdate > CY_USIZE_MAX - cbRequired ) {
        return Fail( compression_status_t::INTERNAL_ERROR );
    }
    cbRequired += cbUpdate;
    if ( bFinish ) {
        const usize cbEnd = LZ4F_compressBound( 0u, &preferences );
        if ( LZ4F_isError( cbEnd ) != 0u ||
             cbEnd > CY_USIZE_MAX - cbRequired ) {
            return Fail( compression_status_t::INTERNAL_ERROR );
        }
        cbRequired += cbEnd;
    }
    if ( output.nCount < cbRequired ) {
        return Fail( compression_status_t::OUTPUT_TOO_SMALL, cbRequired );
    }

    usize iOutput = 0u;
    if ( !stream.bBegan ) {
        const usize cbHeader = LZ4F_compressBegin(
            pContext,
            output.pData,
            output.nCount,
            &preferences );
        if ( LZ4F_isError( cbHeader ) != 0u ) {
            return Fail( compression_status_t::INTERNAL_ERROR );
        }
        stream.bBegan = CY_TRUE;
        iOutput += cbHeader;
    }
    if ( input.cbSize != 0u ) {
        const usize cbWritten = LZ4F_compressUpdate(
            pContext,
            output.pData + iOutput,
            output.nCount - iOutput,
            input.pData,
            input.cbSize,
            nullptr );
        if ( LZ4F_isError( cbWritten ) != 0u ) {
            return Fail( compression_status_t::INTERNAL_ERROR );
        }
        iOutput += cbWritten;
    }
    if ( bFinish ) {
        const usize cbEnd = LZ4F_compressEnd(
            pContext,
            output.pData + iOutput,
            output.nCount - iOutput,
            nullptr );
        if ( LZ4F_isError( cbEnd ) != 0u ) {
            return Fail( compression_status_t::INTERNAL_ERROR );
        }
        iOutput += cbEnd;
        stream.bFinished = CY_TRUE;
    }
    return {
        compression_status_t::OK,
        input.cbSize,
        iOutput,
        iOutput
    };
}

CYPHER_NODISCARD compression_result_t ProcessLZ4Decompress(
    compression_stream_t &stream,
    binary_block_t input,
    byte_span_t output,
    bool_t bFinish ) noexcept
{
    auto *pContext = static_cast<LZ4F_dctx *>( stream.pBackend );
    byte inputDummy = 0u;
    byte outputDummy = 0u;
    usize cbInput = input.cbSize;
    usize cbOutput = output.nCount;
    const usize backendResult = LZ4F_decompress(
        pContext,
        cbOutput != 0u ? output.pData : &outputDummy,
        &cbOutput,
        cbInput != 0u ? input.pData : &inputDummy,
        &cbInput,
        nullptr );
    if ( LZ4F_isError( backendResult ) != 0u ) {
        return Fail( compression_status_t::CORRUPT_INPUT );
    }
    if ( backendResult == 0u ) {
        stream.bFinished = CY_TRUE;
        return {
            compression_status_t::OK,
            cbInput,
            cbOutput,
            cbOutput
        };
    }
    if ( output.nCount == cbOutput ) {
        return {
            compression_status_t::OUTPUT_TOO_SMALL,
            cbInput,
            cbOutput,
            1u
        };
    }
    if ( bFinish && cbInput == input.cbSize ) {
        return {
            compression_status_t::CORRUPT_INPUT,
            cbInput,
            cbOutput,
            CY_COMPRESSION_SIZE_UNKNOWN
        };
    }
    return {
        compression_status_t::OK,
        cbInput,
        cbOutput,
        CY_COMPRESSION_SIZE_UNKNOWN
    };
}

CYPHER_NODISCARD compression_result_t ProcessZstdCompress(
    compression_stream_t &stream,
    binary_block_t input,
    byte_span_t output,
    bool_t bFinish ) noexcept
{
    byte inputDummy = 0u;
    byte outputDummy = 0u;
    ZSTD_inBuffer source{
        input.cbSize != 0u ? input.pData : &inputDummy,
        input.cbSize,
        0u
    };
    ZSTD_outBuffer destination{
        output.nCount != 0u ? output.pData : &outputDummy,
        output.nCount,
        0u
    };
    const usize backendResult = ZSTD_compressStream2(
        static_cast<ZSTD_CCtx *>( stream.pBackend ),
        &destination,
        &source,
        bFinish ? ZSTD_e_end : ZSTD_e_continue );
    if ( ZSTD_isError( backendResult ) != 0u ) {
        return Fail( compression_status_t::INTERNAL_ERROR );
    }
    if ( bFinish && backendResult == 0u && source.pos == source.size ) {
        stream.bFinished = CY_TRUE;
    }
    const bool_t bNeedsOutput =
        source.pos != source.size || ( bFinish && backendResult != 0u );
    return {
        bNeedsOutput
            ? compression_status_t::OUTPUT_TOO_SMALL
            : compression_status_t::OK,
        source.pos,
        destination.pos,
        bNeedsOutput ? ( backendResult != 0u ? backendResult : 1u )
                     : destination.pos
    };
}

CYPHER_NODISCARD compression_result_t ProcessZstdDecompress(
    compression_stream_t &stream,
    binary_block_t input,
    byte_span_t output,
    bool_t bFinish ) noexcept
{
    byte inputDummy = 0u;
    byte outputDummy = 0u;
    ZSTD_inBuffer source{
        input.cbSize != 0u ? input.pData : &inputDummy,
        input.cbSize,
        0u
    };
    ZSTD_outBuffer destination{
        output.nCount != 0u ? output.pData : &outputDummy,
        output.nCount,
        0u
    };
    const usize backendResult = ZSTD_decompressStream(
        static_cast<ZSTD_DCtx *>( stream.pBackend ),
        &destination,
        &source );
    if ( ZSTD_isError( backendResult ) != 0u ) {
        return Fail( compression_status_t::CORRUPT_INPUT );
    }
    if ( backendResult == 0u ) {
        stream.bFinished = CY_TRUE;
        return {
            compression_status_t::OK,
            source.pos,
            destination.pos,
            destination.pos
        };
    }
    if ( destination.pos == destination.size ) {
        return {
            compression_status_t::OUTPUT_TOO_SMALL,
            source.pos,
            destination.pos,
            backendResult
        };
    }
    if ( bFinish && source.pos == source.size ) {
        return {
            compression_status_t::CORRUPT_INPUT,
            source.pos,
            destination.pos,
            CY_COMPRESSION_SIZE_UNKNOWN
        };
    }
    return {
        compression_status_t::OK,
        source.pos,
        destination.pos,
        CY_COMPRESSION_SIZE_UNKNOWN
    };
}

CYPHER_NODISCARD bool_t InitializeBackend(
    compression_stream_t &stream ) noexcept
{
    switch ( stream.codec ) {
        case compression_codec_t::NONE:
            return HasDefaultOptions( stream.options );
        case compression_codec_t::CYPHER_LZ:
            return CY_FALSE;
        case compression_codec_t::LZ4:
            if ( stream.options.dictionary.cbSize != 0u ) {
                return CY_FALSE;
            }
            if ( stream.bCompress ) {
                LZ4F_cctx *pContext = nullptr;
                const usize result = LZ4F_createCompressionContext(
                    &pContext,
                    LZ4F_VERSION );
                stream.pBackend = pContext;
                return LZ4F_isError( result ) == 0u && pContext != nullptr;
            } else {
                LZ4F_dctx *pContext = nullptr;
                const usize result = LZ4F_createDecompressionContext(
                    &pContext,
                    LZ4F_VERSION );
                stream.pBackend = pContext;
                return LZ4F_isError( result ) == 0u && pContext != nullptr;
            }
        case compression_codec_t::ZSTD:
            if ( stream.bCompress ) {
                auto *pContext = ZSTD_createCCtx();
                stream.pBackend = pContext;
                if ( pContext == nullptr ) {
                    return CY_FALSE;
                }
                usize result = ZSTD_CCtx_setParameter(
                    pContext,
                    ZSTD_c_compressionLevel,
                    stream.options.nLevel );
                if ( ZSTD_isError( result ) == 0u ) {
                    result = ZSTD_CCtx_setParameter(
                        pContext,
                        ZSTD_c_checksumFlag,
                        stream.options.bChecksum ? 1 : 0 );
                }
                if ( ZSTD_isError( result ) == 0u &&
                     stream.options.dictionary.cbSize != 0u ) {
                    result = ZSTD_CCtx_loadDictionary(
                        pContext,
                        stream.options.dictionary.pData,
                        stream.options.dictionary.cbSize );
                }
                return ZSTD_isError( result ) == 0u;
            } else {
                auto *pContext = ZSTD_createDCtx();
                stream.pBackend = pContext;
                if ( pContext == nullptr ) {
                    return CY_FALSE;
                }
                if ( stream.options.dictionary.cbSize == 0u ) {
                    return CY_TRUE;
                }
                const usize result = ZSTD_DCtx_loadDictionary(
                    pContext,
                    stream.options.dictionary.pData,
                    stream.options.dictionary.cbSize );
                return ZSTD_isError( result ) == 0u;
            }
    }
    return CY_FALSE;
}

void DestroyBackend( compression_stream_t &stream ) noexcept
{
    if ( stream.pBackend == nullptr ) {
        return;
    }
    switch ( stream.codec ) {
        case compression_codec_t::LZ4:
            if ( stream.bCompress ) {
                static_cast<void>( LZ4F_freeCompressionContext(
                    static_cast<LZ4F_cctx *>( stream.pBackend ) ) );
            } else {
                static_cast<void>( LZ4F_freeDecompressionContext(
                    static_cast<LZ4F_dctx *>( stream.pBackend ) ) );
            }
            break;
        case compression_codec_t::ZSTD:
            if ( stream.bCompress ) {
                static_cast<void>( ZSTD_freeCCtx(
                    static_cast<ZSTD_CCtx *>( stream.pBackend ) ) );
            } else {
                static_cast<void>( ZSTD_freeDCtx(
                    static_cast<ZSTD_DCtx *>( stream.pBackend ) ) );
            }
            break;
        case compression_codec_t::NONE:
        case compression_codec_t::CYPHER_LZ:
            break;
    }
    stream.pBackend = nullptr;
}

} // namespace

const char *Compression_StatusName( compression_status_t status ) noexcept
{
    switch ( status ) {
        case compression_status_t::OK:                  return "OK";
        case compression_status_t::INVALID_ARGUMENT:    return "INVALID_ARGUMENT";
        case compression_status_t::UNSUPPORTED_CODEC:   return "UNSUPPORTED_CODEC";
        case compression_status_t::UNSUPPORTED_OPTION:  return "UNSUPPORTED_OPTION";
        case compression_status_t::BACKEND_UNAVAILABLE: return "BACKEND_UNAVAILABLE";
        case compression_status_t::OUTPUT_TOO_SMALL:    return "OUTPUT_TOO_SMALL";
        case compression_status_t::CORRUPT_INPUT:       return "CORRUPT_INPUT";
        case compression_status_t::DICTIONARY_MISMATCH: return "DICTIONARY_MISMATCH";
        case compression_status_t::OUT_OF_MEMORY:       return "OUT_OF_MEMORY";
        case compression_status_t::INTERNAL_ERROR:      return "INTERNAL_ERROR";
    }
    return "UNKNOWN_COMPRESSION_STATUS";
}

bool_t Compression_IsCodecAvailable( compression_codec_t codec ) noexcept
{
    switch ( codec ) {
        case compression_codec_t::NONE:
        case compression_codec_t::CYPHER_LZ:
        case compression_codec_t::LZ4:
        case compression_codec_t::ZSTD:
            return CY_TRUE;
    }
    return CY_FALSE;
}

bool_t Compression_SupportsStreaming( compression_codec_t codec ) noexcept
{
    return codec == compression_codec_t::NONE ||
           codec == compression_codec_t::LZ4 ||
           codec == compression_codec_t::ZSTD;
}

usize Compression_CompressBound(
    compression_codec_t codec,
    usize cbInput,
    const compression_options_t &options ) noexcept
{
    switch ( codec ) {
        case compression_codec_t::NONE:
            return HasDefaultOptions( options ) ? cbInput : 0u;
        case compression_codec_t::CYPHER_LZ:
            return HasDefaultOptions( options )
                ? CompressionLZ_CompressBound( cbInput )
                : 0u;
        case compression_codec_t::LZ4:
            return CompressionLZ4_CompressBound( cbInput, options );
        case compression_codec_t::ZSTD:
            return BinaryBlock_IsValid( options.dictionary )
                ? CompressionZstd_CompressBound( cbInput )
                : 0u;
    }
    return 0u;
}

compression_result_t Compression_Compress(
    compression_codec_t codec,
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options ) noexcept
{
    if ( !Compression_IsCodecAvailable( codec ) ) {
        return Fail( compression_status_t::UNSUPPORTED_CODEC );
    }
    switch ( codec ) {
        case compression_codec_t::NONE:
            return HasDefaultOptions( options )
                ? CopyUncompressed( input, output )
                : Fail( compression_status_t::UNSUPPORTED_OPTION );
        case compression_codec_t::CYPHER_LZ:
            return HasDefaultOptions( options )
                ? CompressionLZ_Compress( input, output )
                : Fail( compression_status_t::UNSUPPORTED_OPTION );
        case compression_codec_t::LZ4:
            return CompressionLZ4_Compress( input, output, options );
        case compression_codec_t::ZSTD:
            return CompressionZstd_Compress( input, output, options );
    }
    return Fail( compression_status_t::UNSUPPORTED_CODEC );
}

compression_result_t Compression_Decompress(
    compression_codec_t codec,
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options ) noexcept
{
    if ( !Compression_IsCodecAvailable( codec ) ) {
        return Fail( compression_status_t::UNSUPPORTED_CODEC );
    }
    switch ( codec ) {
        case compression_codec_t::NONE:
            return HasDefaultOptions( options )
                ? CopyUncompressed( input, output )
                : Fail( compression_status_t::UNSUPPORTED_OPTION );
        case compression_codec_t::CYPHER_LZ:
            return HasDefaultOptions( options )
                ? CompressionLZ_Decompress( input, output )
                : Fail( compression_status_t::UNSUPPORTED_OPTION );
        case compression_codec_t::LZ4:
            return CompressionLZ4_Decompress( input, output, options );
        case compression_codec_t::ZSTD:
            return CompressionZstd_Decompress( input, output, options );
    }
    return Fail( compression_status_t::UNSUPPORTED_CODEC );
}

compression_stream_t *CompressionStream_Create(
    compression_codec_t codec,
    bool_t bCompress,
    const compression_options_t &options,
    const allocator_t *pAllocator ) noexcept
{
    if ( !Compression_SupportsStreaming( codec ) ||
         !BinaryBlock_IsValid( options.dictionary ) ) {
        return nullptr;
    }
    const allocator_t *pOwner = pAllocator != nullptr
        ? pAllocator
        : Allocator_GetSystem();
    if ( !Allocator_IsValid( pOwner ) ) {
        return nullptr;
    }

    void *pStorage = Allocator_Allocate(
        pOwner,
        sizeof( compression_stream_t ),
        alignof( compression_stream_t ) );
    if ( pStorage == nullptr ) {
        return nullptr;
    }
    auto *pStream = new ( pStorage ) compression_stream_t{};
    pStream->codec = codec;
    pStream->bCompress = bCompress;
    pStream->options = options;
    pStream->pAllocator = pOwner;
    if ( !InitializeBackend( *pStream ) ) {
        DestroyBackend( *pStream );
        pStream->~compression_stream_t();
        Allocator_Free(
            pOwner,
            pStorage,
            sizeof( compression_stream_t ),
            alignof( compression_stream_t ) );
        return nullptr;
    }
    return pStream;
}

void CompressionStream_Destroy( compression_stream_t *pContext ) noexcept
{
    if ( pContext == nullptr ) {
        return;
    }
    const allocator_t *pAllocator = pContext->pAllocator;
    DestroyBackend( *pContext );
    pContext->~compression_stream_t();
    Allocator_Free(
        pAllocator,
        pContext,
        sizeof( compression_stream_t ),
        alignof( compression_stream_t ) );
}

compression_result_t CompressionStream_Process(
    compression_stream_t *pContext,
    binary_block_t input,
    byte_span_t output,
    bool_t bFinish ) noexcept
{
    if ( pContext == nullptr || pContext->bFinished ||
         !BinaryBlock_IsValid( input ) || !Span_IsValid( output ) ||
         ( pContext->bFinishSeen && !bFinish ) ) {
        return Fail( compression_status_t::INVALID_ARGUMENT );
    }
    if ( bFinish ) {
        pContext->bFinishSeen = CY_TRUE;
    }
    switch ( pContext->codec ) {
        case compression_codec_t::NONE:
            return ProcessNone( *pContext, input, output, bFinish );
        case compression_codec_t::LZ4:
            return pContext->bCompress
                ? ProcessLZ4Compress( *pContext, input, output, bFinish )
                : ProcessLZ4Decompress( *pContext, input, output, bFinish );
        case compression_codec_t::ZSTD:
            return pContext->bCompress
                ? ProcessZstdCompress( *pContext, input, output, bFinish )
                : ProcessZstdDecompress( *pContext, input, output, bFinish );
        case compression_codec_t::CYPHER_LZ:
            return Fail( compression_status_t::UNSUPPORTED_CODEC );
    }
    return Fail( compression_status_t::UNSUPPORTED_CODEC );
}

} // namespace cypher::common
