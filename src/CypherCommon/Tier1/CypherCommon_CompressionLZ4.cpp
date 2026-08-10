//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CompressionLZ4.cpp
//  Purpose: Implements the LZ4 frame backend adapter.
//  Details: The adapter emits standard LZ4 frames with content size metadata so
//           decompression can report required output storage before mutation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CompressionLZ4.h"

#include <lz4frame.h>

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

CYPHER_NODISCARD bool_t IsValidOptions(
    const compression_options_t &options ) noexcept
{
    return BinaryBlock_IsValid( options.dictionary ) &&
           options.dictionary.cbSize == 0u;
}

CYPHER_NODISCARD LZ4F_preferences_t MakePreferences(
    usize cbInput,
    const compression_options_t &options ) noexcept
{
    LZ4F_preferences_t preferences = LZ4F_INIT_PREFERENCES;
    preferences.frameInfo.contentSize =
        static_cast<unsigned long long>( cbInput );
    preferences.frameInfo.contentChecksumFlag = options.bChecksum
        ? LZ4F_contentChecksumEnabled
        : LZ4F_noContentChecksum;
    preferences.compressionLevel = options.nLevel;
    return preferences;
}

} // namespace

usize CompressionLZ4_CompressBound(
    usize cbInput,
    const compression_options_t &options ) noexcept
{
    if ( !IsValidOptions( options ) ) {
        return 0u;
    }
    const LZ4F_preferences_t preferences = MakePreferences( cbInput, options );
    const usize cbBound = LZ4F_compressFrameBound( cbInput, &preferences );
    return LZ4F_isError( cbBound ) != 0u ? 0u : cbBound;
}

compression_result_t CompressionLZ4_Compress(
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options ) noexcept
{
    if ( !BinaryBlock_IsValid( input ) || !Span_IsValid( output ) ) {
        return Fail( compression_status_t::INVALID_ARGUMENT );
    }
    if ( !IsValidOptions( options ) ) {
        return Fail( compression_status_t::UNSUPPORTED_OPTION );
    }

    const LZ4F_preferences_t preferences = MakePreferences(
        input.cbSize,
        options );
    const usize cbBound = LZ4F_compressFrameBound(
        input.cbSize,
        &preferences );
    if ( LZ4F_isError( cbBound ) != 0u ) {
        return Fail( compression_status_t::INTERNAL_ERROR );
    }
    if ( output.nCount < cbBound ) {
        return Fail( compression_status_t::OUTPUT_TOO_SMALL, cbBound );
    }

    const byte empty = 0u;
    const void *pInput = input.cbSize != 0u ? input.pData : &empty;
    const usize cbWritten = LZ4F_compressFrame(
        output.pData,
        output.nCount,
        pInput,
        input.cbSize,
        &preferences );
    if ( LZ4F_isError( cbWritten ) != 0u ) {
        return Fail( compression_status_t::INTERNAL_ERROR, cbBound );
    }
    return {
        compression_status_t::OK,
        input.cbSize,
        cbWritten,
        cbWritten
    };
}

compression_result_t CompressionLZ4_Decompress(
    binary_block_t input,
    byte_span_t output,
    const compression_options_t &options ) noexcept
{
    if ( !BinaryBlock_IsValid( input ) || !Span_IsValid( output ) ) {
        return Fail( compression_status_t::INVALID_ARGUMENT );
    }
    if ( !IsValidOptions( options ) ) {
        return Fail( compression_status_t::UNSUPPORTED_OPTION );
    }
    if ( input.cbSize < LZ4F_MIN_SIZE_TO_KNOW_HEADER_LENGTH ) {
        return Fail( compression_status_t::CORRUPT_INPUT );
    }

    LZ4F_dctx *pContext = nullptr;
    const usize createResult = LZ4F_createDecompressionContext(
        &pContext,
        LZ4F_VERSION );
    if ( LZ4F_isError( createResult ) != 0u || pContext == nullptr ) {
        return Fail( compression_status_t::OUT_OF_MEMORY );
    }

    LZ4F_frameInfo_t frameInfo = LZ4F_INIT_FRAMEINFO;
    usize cbHeader = input.cbSize;
    const usize infoResult = LZ4F_getFrameInfo(
        pContext,
        &frameInfo,
        input.pData,
        &cbHeader );
    if ( LZ4F_isError( infoResult ) != 0u ) {
        static_cast<void>( LZ4F_freeDecompressionContext( pContext ) );
        return Fail( compression_status_t::CORRUPT_INPUT );
    }

    usize cbRequired = CY_COMPRESSION_SIZE_UNKNOWN;
    if ( frameInfo.contentSize != 0u ) {
        if ( frameInfo.contentSize >
             static_cast<unsigned long long>( CY_USIZE_MAX ) ) {
            static_cast<void>( LZ4F_freeDecompressionContext( pContext ) );
            return Fail( compression_status_t::INTERNAL_ERROR );
        }
        cbRequired = static_cast<usize>( frameInfo.contentSize );
        if ( output.nCount < cbRequired ) {
            static_cast<void>( LZ4F_freeDecompressionContext( pContext ) );
            return Fail( compression_status_t::OUTPUT_TOO_SMALL, cbRequired );
        }
    }

    usize iInput = cbHeader;
    usize iOutput = 0u;
    usize nextHint = infoResult;
    byte emptyOutput = 0u;
    while ( nextHint != 0u ) {
        usize cbSource = input.cbSize - iInput;
        usize cbDest = output.nCount - iOutput;
        void *pDest = cbDest != 0u ? output.pData + iOutput : &emptyOutput;
        nextHint = LZ4F_decompress(
            pContext,
            pDest,
            &cbDest,
            input.pData + iInput,
            &cbSource,
            nullptr );
        if ( LZ4F_isError( nextHint ) != 0u ) {
            static_cast<void>( LZ4F_freeDecompressionContext( pContext ) );
            return Fail( compression_status_t::CORRUPT_INPUT, cbRequired );
        }
        iInput += cbSource;
        iOutput += cbDest;
        if ( nextHint != 0u && iOutput == output.nCount ) {
            static_cast<void>( LZ4F_freeDecompressionContext( pContext ) );
            return Fail( compression_status_t::OUTPUT_TOO_SMALL, cbRequired );
        }
        if ( nextHint != 0u && cbSource == 0u && cbDest == 0u ) {
            static_cast<void>( LZ4F_freeDecompressionContext( pContext ) );
            return Fail( compression_status_t::CORRUPT_INPUT, cbRequired );
        }
    }

    static_cast<void>( LZ4F_freeDecompressionContext( pContext ) );
    if ( iInput != input.cbSize ||
         ( cbRequired != CY_COMPRESSION_SIZE_UNKNOWN &&
           iOutput != cbRequired ) ) {
        return Fail( compression_status_t::CORRUPT_INPUT, cbRequired );
    }
    return {
        compression_status_t::OK,
        input.cbSize,
        iOutput,
        iOutput
    };
}

} // namespace cypher::common
