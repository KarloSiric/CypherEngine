//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_Compression_Tests.cpp
//  Purpose: Tests codec-neutral and backend-specific compression behavior.
//  Details: Covers binary and empty round trips, capacity reporting, corruption,
//           dictionaries, unsupported options, and incremental stream lifetime.
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

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>
#include <vector>

using namespace cypher::common;

namespace
{

std::vector<byte> MakeInput()
{
    std::vector<byte> input( 8192u );
    for ( usize iByte = 0u; iByte < input.size(); ++iByte ) {
        input[iByte] = static_cast<byte>(
            ( iByte % 257u ) < 220u
                ? ( iByte / 17u ) & 0x0Fu
                : ( iByte * 131u ) & 0xFFu );
    }
    return input;
}

void RequireRoundTrip(
    compression_codec_t codec,
    const compression_options_t &options = {} )
{
    const std::vector<byte> input = MakeInput();
    const usize cbBound = Compression_CompressBound(
        codec,
        input.size(),
        options );
    REQUIRE( ( cbBound >= input.size() ||
               codec != compression_codec_t::NONE ) );
    std::vector<byte> compressed( cbBound );
    const compression_result_t encoded = Compression_Compress(
        codec,
        { input.data(), input.size() },
        { compressed.data(), compressed.size() },
        options );
    REQUIRE( encoded.status == compression_status_t::OK );
    REQUIRE( encoded.cbRead == input.size() );
    REQUIRE( encoded.cbWritten <= compressed.size() );

    std::vector<byte> decoded( input.size() );
    const compression_result_t restored = Compression_Decompress(
        codec,
        { compressed.data(), encoded.cbWritten },
        { decoded.data(), decoded.size() },
        options );
    REQUIRE( restored.status == compression_status_t::OK );
    REQUIRE( restored.cbRead == encoded.cbWritten );
    REQUIRE( restored.cbWritten == input.size() );
    REQUIRE( decoded == input );
}

void RequireStreamingRoundTrip( compression_codec_t codec )
{
    const std::vector<byte> input = MakeInput();
    compression_stream_t *pEncoder = CompressionStream_Create(
        codec,
        CY_TRUE,
        { 0, {}, CY_TRUE },
        nullptr );
    REQUIRE( pEncoder != nullptr );

    std::vector<byte> compressed( input.size() * 2u + 4096u );
    const usize cbFirstInput = input.size() / 3u;
    const compression_result_t first = CompressionStream_Process(
        pEncoder,
        { input.data(), cbFirstInput },
        { compressed.data(), compressed.size() },
        CY_FALSE );
    REQUIRE( first.status == compression_status_t::OK );
    REQUIRE( first.cbRead == cbFirstInput );

    const compression_result_t last = CompressionStream_Process(
        pEncoder,
        { input.data() + cbFirstInput, input.size() - cbFirstInput },
        {
            compressed.data() + first.cbWritten,
            compressed.size() - first.cbWritten
        },
        CY_TRUE );
    REQUIRE( last.status == compression_status_t::OK );
    REQUIRE( last.cbRead == input.size() - cbFirstInput );
    const usize cbCompressed = first.cbWritten + last.cbWritten;
    CompressionStream_Destroy( pEncoder );

    compression_stream_t *pDecoder = CompressionStream_Create(
        codec,
        CY_FALSE,
        {},
        nullptr );
    REQUIRE( pDecoder != nullptr );
    std::vector<byte> decoded( input.size() );
    const compression_result_t restored = CompressionStream_Process(
        pDecoder,
        { compressed.data(), cbCompressed },
        { decoded.data(), decoded.size() },
        CY_TRUE );
    REQUIRE( restored.status == compression_status_t::OK );
    REQUIRE( restored.cbRead == cbCompressed );
    REQUIRE( restored.cbWritten == input.size() );
    REQUIRE( decoded == input );
    REQUIRE(
        CompressionStream_Process( pDecoder, {}, {}, CY_TRUE ).status ==
        compression_status_t::INVALID_ARGUMENT );
    CompressionStream_Destroy( pDecoder );
}

} // namespace

TEST_CASE( "Compression codecs perform exact binary round trips",
           "[CypherCommon][Tier1][Compression]" )
{
    RequireRoundTrip( compression_codec_t::NONE );
    RequireRoundTrip( compression_codec_t::CYPHER_LZ );
    RequireRoundTrip(
        compression_codec_t::LZ4,
        { 0, {}, CY_TRUE } );
    RequireRoundTrip(
        compression_codec_t::ZSTD,
        { 3, {}, CY_TRUE } );
}

TEST_CASE( "Compression codecs represent empty input",
           "[CypherCommon][Tier1][Compression]" )
{
    const std::array codecs{
        compression_codec_t::NONE,
        compression_codec_t::CYPHER_LZ,
        compression_codec_t::LZ4,
        compression_codec_t::ZSTD
    };
    for ( const compression_codec_t codec : codecs ) {
        std::array<byte, 256u> compressed{};
        const compression_result_t encoded = Compression_Compress(
            codec,
            {},
            { compressed.data(), compressed.size() } );
        REQUIRE( encoded.status == compression_status_t::OK );

        const compression_result_t decoded = Compression_Decompress(
            codec,
            { compressed.data(), encoded.cbWritten },
            {} );
        REQUIRE( decoded.status == compression_status_t::OK );
        REQUIRE( decoded.cbWritten == 0u );
    }
}

TEST_CASE( "Compression reports insufficient output without partial one-shot writes",
           "[CypherCommon][Tier1][Compression]" )
{
    const std::vector<byte> input = MakeInput();
    std::array<byte, 8u> output{};
    for ( const compression_codec_t codec : {
              compression_codec_t::NONE,
              compression_codec_t::CYPHER_LZ,
              compression_codec_t::LZ4,
              compression_codec_t::ZSTD } ) {
        const compression_result_t result = Compression_Compress(
            codec,
            { input.data(), input.size() },
            { output.data(), output.size() } );
        REQUIRE( result.status == compression_status_t::OUTPUT_TOO_SMALL );
        REQUIRE( result.cbWritten == 0u );
        REQUIRE( result.cbRequired > output.size() );
    }
}

TEST_CASE( "Compressed frames reject corruption",
           "[CypherCommon][Tier1][Compression]" )
{
    const std::vector<byte> input = MakeInput();
    for ( const compression_codec_t codec : {
              compression_codec_t::CYPHER_LZ,
              compression_codec_t::LZ4,
              compression_codec_t::ZSTD } ) {
        const compression_options_t options{
            0,
            {},
            codec != compression_codec_t::CYPHER_LZ
        };
        std::vector<byte> compressed(
            Compression_CompressBound( codec, input.size(), options ) );
        const compression_result_t encoded = Compression_Compress(
            codec,
            { input.data(), input.size() },
            { compressed.data(), compressed.size() },
            options );
        REQUIRE( encoded.status == compression_status_t::OK );
        compressed[encoded.cbWritten - 1u] ^= 0x5Au;

        std::vector<byte> decoded( input.size() );
        const compression_result_t result = Compression_Decompress(
            codec,
            { compressed.data(), encoded.cbWritten },
            { decoded.data(), decoded.size() },
            options );
        REQUIRE( result.status == compression_status_t::CORRUPT_INPUT );
    }
}

TEST_CASE( "Zstd accepts caller-provided dictionaries",
           "[CypherCommon][Tier1][Compression]" )
{
    const std::vector<byte> input = MakeInput();
    const std::array<byte, 64u> dictionary{
        'C', 'Y', 'P', 'H', 'E', 'R', '-', 'D', 'I', 'C', 'T'
    };
    RequireRoundTrip(
        compression_codec_t::ZSTD,
        {
            5,
            { dictionary.data(), dictionary.size() },
            CY_TRUE
        } );

    std::array<byte, 128u> output{};
    REQUIRE(
        Compression_Compress(
            compression_codec_t::LZ4,
            { input.data(), input.size() },
            { output.data(), output.size() },
            { 0, { dictionary.data(), dictionary.size() }, CY_FALSE } ).status ==
        compression_status_t::UNSUPPORTED_OPTION );
}

TEST_CASE( "LZ4 and Zstd streams preserve data across process calls",
           "[CypherCommon][Tier1][Compression]" )
{
    REQUIRE( !Compression_SupportsStreaming( compression_codec_t::CYPHER_LZ ) );
    RequireStreamingRoundTrip( compression_codec_t::LZ4 );
    RequireStreamingRoundTrip( compression_codec_t::ZSTD );
}

TEST_CASE( "Compression backend adapters satisfy their direct contracts",
           "[CypherCommon][Tier1][Compression][Backend]" )
{
    const std::vector<byte> input = MakeInput();

    std::vector<byte> cypherLz( CompressionLZ_CompressBound( input.size() ) );
    const compression_result_t cypherLzEncoded = CompressionLZ_Compress(
        { input.data(), input.size() },
        { cypherLz.data(), cypherLz.size() } );
    REQUIRE( cypherLzEncoded.status == compression_status_t::OK );
    std::vector<byte> cypherLzDecoded( input.size() );
    REQUIRE(
        CompressionLZ_Decompress(
            { cypherLz.data(), cypherLzEncoded.cbWritten },
            { cypherLzDecoded.data(), cypherLzDecoded.size() } ).status ==
        compression_status_t::OK );
    REQUIRE( cypherLzDecoded == input );

    const compression_options_t lz4Options{ 0, {}, CY_TRUE };
    std::vector<byte> lz4(
        CompressionLZ4_CompressBound( input.size(), lz4Options ) );
    const compression_result_t lz4Encoded = CompressionLZ4_Compress(
        { input.data(), input.size() },
        { lz4.data(), lz4.size() },
        lz4Options );
    REQUIRE( lz4Encoded.status == compression_status_t::OK );
    std::vector<byte> lz4Decoded( input.size() );
    REQUIRE(
        CompressionLZ4_Decompress(
            { lz4.data(), lz4Encoded.cbWritten },
            { lz4Decoded.data(), lz4Decoded.size() },
            lz4Options ).status == compression_status_t::OK );
    REQUIRE( lz4Decoded == input );

    const compression_options_t zstdOptions{ 3, {}, CY_TRUE };
    std::vector<byte> zstd( CompressionZstd_CompressBound( input.size() ) );
    const compression_result_t zstdEncoded = CompressionZstd_Compress(
        { input.data(), input.size() },
        { zstd.data(), zstd.size() },
        zstdOptions );
    REQUIRE( zstdEncoded.status == compression_status_t::OK );
    REQUIRE(
        CompressionZstd_FrameContentSize(
            { zstd.data(), zstdEncoded.cbWritten } ) == input.size() );
    std::vector<byte> zstdDecoded( input.size() );
    REQUIRE(
        CompressionZstd_Decompress(
            { zstd.data(), zstdEncoded.cbWritten },
            { zstdDecoded.data(), zstdDecoded.size() },
            zstdOptions ).status == compression_status_t::OK );
    REQUIRE( zstdDecoded == input );
}

TEST_CASE( "Compression capability and status reporting cover every codec",
           "[CypherCommon][Tier1][Compression][Contract]" )
{
    for ( const compression_codec_t codec : {
              compression_codec_t::NONE,
              compression_codec_t::CYPHER_LZ,
              compression_codec_t::LZ4,
              compression_codec_t::ZSTD } ) {
        REQUIRE( Compression_IsCodecAvailable( codec ) );
    }
    REQUIRE_FALSE(
        Compression_IsCodecAvailable(
            static_cast<compression_codec_t>( 0xFFu ) ) );
    REQUIRE(
        std::string_view(
            Compression_StatusName( compression_status_t::OK ) ) == "OK" );
    REQUIRE(
        std::string_view(
            Compression_StatusName(
                static_cast<compression_status_t>( 0xFFu ) ) ) ==
        "UNKNOWN_COMPRESSION_STATUS" );
}
