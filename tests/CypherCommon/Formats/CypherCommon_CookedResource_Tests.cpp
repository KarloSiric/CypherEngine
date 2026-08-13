//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Formats/CypherCommon_CookedResource_Tests.cpp
//  Purpose: Tests the common cooked-resource binary envelope.
//  Details: Covers little-endian round trips, chunk ordering and alignment,
//           codec and hash invariants, malformed input, bounded output, and
//           transactional read behavior.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedResource.h"
#include "CypherCommon_RenderAssetSchema.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

cooked_resource_header_t MakeHeader() noexcept
{
    cooked_resource_header_t header{};
    header.resourceType = CY_RENDER_TEXTURE_RESOURCE_TYPE;
    header.nResourceVersion = 1u;
    header.flags = COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH;
    header.nChunks = 2u;
    header.cbFile = 320u;
    header.sourceHash = { 0x11u, 0x22u };
    return header;
}

void MakeChunks( cooked_chunk_desc_t ( &chunks )[2] ) noexcept
{
    chunks[0].chunkType = Cy_MakeFourCC( 'M', 'E', 'T', 'A' );
    chunks[0].nAlignment = 16u;
    chunks[0].iOffset = 208u;
    chunks[0].cbStored = 32u;
    chunks[0].cbDecoded = 32u;

    chunks[1].chunkType = Cy_MakeFourCC( 'P', 'I', 'X', 'L' );
    chunks[1].codec = cooked_chunk_codec_t::ZSTD;
    chunks[1].flags = COOKED_CHUNK_FLAG_COMPRESSED |
                      COOKED_CHUNK_FLAG_HAS_CONTENT_HASH;
    chunks[1].nAlignment = 64u;
    chunks[1].iOffset = 256u;
    chunks[1].cbStored = 64u;
    chunks[1].cbDecoded = 128u;
    chunks[1].contentHash = { 0x55u, 0x66u };
}

cooked_resource_result_t WriteCompleteFile(
    cooked_resource_header_t &header,
    cooked_chunk_desc_t ( &chunks )[2],
    byte ( &file )[320] ) noexcept
{
    cooked_resource_result_t result = CookedResource_WriteLayout(
        header,
        { chunks, 2u },
        Span_FromArray( file ) );
    if ( !CookedResource_Succeeded( result ) ) {
        return result;
    }
    for ( usize iByte = 208u; iByte < sizeof( file ); ++iByte ) {
        file[iByte] = static_cast<byte>( iByte & 0xFFu );
    }
    header.contentHash = CookedResource_ComputeContentHash(
        { file, sizeof( file ) } );
    header.flags |= COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH;
    return CookedResource_WriteLayout(
        header,
        { chunks, 2u },
        Span_FromArray( file ) );
}

} // namespace

TEST_CASE( "Cooked resource layouts round trip with stable encoding",
           "[CypherCommon][Formats][CookedResource]" )
{
    cooked_resource_header_t header = MakeHeader();
    cooked_chunk_desc_t chunks[2]{};
    MakeChunks( chunks );

    byte file[320]{};
    const cooked_resource_result_t written = WriteCompleteFile(
        header,
        chunks,
        file );
    REQUIRE( CookedResource_Succeeded( written ) );
    REQUIRE( written.cbRequired == 208u );
    REQUIRE( written.cbWritten == 208u );

    // FourCC and fixed-width integers are serialized in little-endian order.
    REQUIRE( file[0] == static_cast<byte>( 'C' ) );
    REQUIRE( file[1] == static_cast<byte>( 'Y' ) );
    REQUIRE( file[2] == static_cast<byte>( 'R' ) );
    REQUIRE( file[3] == static_cast<byte>( 'S' ) );
    REQUIRE( file[4] == static_cast<byte>( 1u ) );

    cooked_resource_header_t decodedHeader{};
    cooked_chunk_desc_t decodedChunks[2]{};
    const cooked_resource_result_t read = CookedResource_ReadLayout(
        { file, sizeof( file ) },
        &decodedHeader,
        { decodedChunks, 2u } );
    REQUIRE( CookedResource_Succeeded( read ) );
    REQUIRE( read.cbRead == 208u );
    REQUIRE( decodedHeader.resourceType == CY_RENDER_TEXTURE_RESOURCE_TYPE );
    REQUIRE( decodedHeader.cbFile == sizeof( file ) );
    REQUIRE( decodedChunks[0].chunkType == chunks[0].chunkType );
    REQUIRE( decodedChunks[1].codec == cooked_chunk_codec_t::ZSTD );
    REQUIRE( decodedChunks[1].iOffset == 256u );
    REQUIRE( ContentHash_Equals(
        decodedChunks[1].contentHash,
        chunks[1].contentHash ) );
}

TEST_CASE( "Cooked resource layout validation rejects unsafe chunks",
           "[CypherCommon][Formats][CookedResource][Validation]" )
{
    cooked_resource_header_t header = MakeHeader();
    cooked_chunk_desc_t chunks[2]{};
    MakeChunks( chunks );
    usize iInvalidChunk = CY_INVALID_SIZE;

    REQUIRE( CookedResource_ValidateLayout(
                 header,
                 { chunks, 2u },
                 header.cbFile,
                 &iInvalidChunk ) == cooked_resource_status_t::OK );
    REQUIRE( iInvalidChunk == CY_INVALID_SIZE );

    chunks[1].iOffset = 224u;
    REQUIRE( CookedResource_ValidateLayout(
                 header,
                 { chunks, 2u },
                 header.cbFile,
                 &iInvalidChunk ) ==
             cooked_resource_status_t::INVALID_CHUNK_ORDER );
    REQUIRE( iInvalidChunk == 1u );

    MakeChunks( chunks );
    chunks[1].iOffset = 248u;
    REQUIRE( CookedResource_ValidateLayout(
                 header,
                 { chunks, 2u },
                 header.cbFile,
                 &iInvalidChunk ) ==
             cooked_resource_status_t::INVALID_CHUNK );
    REQUIRE( iInvalidChunk == 1u );

    MakeChunks( chunks );
    chunks[1].codec = static_cast<cooked_chunk_codec_t>( 99u );
    REQUIRE( CookedResource_ValidateLayout(
                 header,
                 { chunks, 2u },
                 header.cbFile,
                 &iInvalidChunk ) ==
             cooked_resource_status_t::INVALID_CHUNK );

    MakeChunks( chunks );
    chunks[0].cbDecoded = 64u;
    REQUIRE( CookedResource_ValidateLayout(
                 header,
                 { chunks, 2u },
                 header.cbFile,
                 &iInvalidChunk ) ==
             cooked_resource_status_t::INVALID_CHUNK );

    MakeChunks( chunks );
    header.flags = COOKED_RESOURCE_FLAG_NONE;
    REQUIRE( CookedResource_ValidateLayout(
                 header,
                 { chunks, 2u },
                 header.cbFile ) == cooked_resource_status_t::INVALID_HEADER );

    header = MakeHeader();
    REQUIRE( CookedResource_ValidateLayout(
                 header,
                 { chunks, 2u },
                 header.cbFile - 1u ) ==
             cooked_resource_status_t::FILE_SIZE_MISMATCH );
}

TEST_CASE( "Cooked resource readers reject malformed input transactionally",
           "[CypherCommon][Formats][CookedResource][Failure]" )
{
    cooked_resource_header_t header = MakeHeader();
    cooked_chunk_desc_t chunks[2]{};
    MakeChunks( chunks );
    byte file[320]{};
    REQUIRE( CookedResource_Succeeded(
        WriteCompleteFile( header, chunks, file ) ) );

    cooked_resource_header_t output{};
    output.magic = Cy_MakeFourCC( 'K', 'E', 'E', 'P' );
    cooked_chunk_desc_t outputChunks[2]{};
    outputChunks[0].chunkType = Cy_MakeFourCC( 'K', 'E', 'E', 'P' );

    file[160] = static_cast<byte>( 224u );
    file[161] = static_cast<byte>( 0u );
    const cooked_resource_result_t overlap = CookedResource_ReadLayout(
        { file, sizeof( file ) },
        &output,
        { outputChunks, 2u } );
    REQUIRE( overlap.status ==
             cooked_resource_status_t::INVALID_CHUNK_ORDER );
    REQUIRE( overlap.iChunk == 1u );
    REQUIRE( output.magic == Cy_MakeFourCC( 'K', 'E', 'E', 'P' ) );
    REQUIRE( outputChunks[0].chunkType ==
             Cy_MakeFourCC( 'K', 'E', 'E', 'P' ) );

    REQUIRE( CookedResource_Succeeded(
        WriteCompleteFile( header, chunks, file ) ) );
    file[319] ^= static_cast<byte>( 1u );
    REQUIRE( CookedResource_ReadLayout(
                 { file, sizeof( file ) },
                 &output,
                 { outputChunks, 2u } ).status ==
             cooked_resource_status_t::CONTENT_HASH_MISMATCH );
    REQUIRE( output.magic == Cy_MakeFourCC( 'K', 'E', 'E', 'P' ) );

    REQUIRE( CookedResource_ReadLayout(
                 { file, CY_COOKED_RESOURCE_HEADER_SIZE - 1u },
                 &output,
                 { outputChunks, 2u } ).status ==
             cooked_resource_status_t::TRUNCATED_INPUT );

    REQUIRE( CookedResource_Succeeded(
        WriteCompleteFile( header, chunks, file ) ) );
    file[0] = static_cast<byte>( 'X' );
    REQUIRE( CookedResource_ReadLayout(
                 { file, sizeof( file ) },
                 &output,
                 { outputChunks, 2u } ).status ==
             cooked_resource_status_t::INVALID_MAGIC );

    REQUIRE( CookedResource_Succeeded(
        WriteCompleteFile( header, chunks, file ) ) );
    file[4] = static_cast<byte>( 2u );
    REQUIRE( CookedResource_ReadLayout(
                 { file, sizeof( file ) },
                 &output,
                 { outputChunks, 2u } ).status ==
             cooked_resource_status_t::VERSION_MISMATCH );

    REQUIRE( CookedResource_Succeeded(
        WriteCompleteFile( header, chunks, file ) ) );
    REQUIRE( CookedResource_ReadLayout(
                 { file, sizeof( file ) },
                 &output,
                 { outputChunks, 1u } ).status ==
             cooked_resource_status_t::OUTPUT_TOO_SMALL );

    MakeChunks( chunks );
    byte prefix[208]{};
    REQUIRE( CookedResource_WriteLayout(
                 header,
                 { chunks, 2u },
                 { prefix, sizeof( prefix ) - 1u } ).status ==
             cooked_resource_status_t::OUTPUT_TOO_SMALL );
}

TEST_CASE( "Cooked resource contract constants and status names are stable",
           "[CypherCommon][Formats][CookedResource][Contract]" )
{
    REQUIRE( CookedResource_PrefixSize( 0u ) == 0u );
    REQUIRE( CookedResource_PrefixSize( 1u ) == 144u );
    REQUIRE( CookedResource_PrefixSize( 2u ) == 208u );
    REQUIRE( CookedResource_PrefixSize(
                 CY_COOKED_RESOURCE_MAX_CHUNKS + 1u ) == 0u );
    REQUIRE( StringView_Equals(
        StringView_FromCString( CookedResource_StatusName(
            cooked_resource_status_t::INVALID_CHUNK_ORDER ) ),
        StringView_FromCString( "INVALID_CHUNK_ORDER" ) ) );
    REQUIRE( StringView_Equals(
        StringView_FromCString( CookedResource_StatusName(
            static_cast<cooked_resource_status_t>( 0xFFu ) ) ),
        StringView_FromCString( "UNKNOWN" ) ) );
}
