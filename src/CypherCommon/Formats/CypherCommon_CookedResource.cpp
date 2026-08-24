//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_CookedResource.cpp
//  Purpose: Implements the common envelope for cooked runtime resources.
//  Details: Readers reject malformed metadata before exposing any descriptors.
//           Chunk descriptors must be ordered by file offset, making overlap and
//           bounds validation linear in the number of chunks.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CookedResource.h"

#include "CypherCommon_ByteReader.h"
#include "CypherCommon_ByteWriter.h"

namespace cypher::common
{

namespace
{

constexpr flags32_t g_knownResourceFlags =
    COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH |
    COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH; // Complete V1 resource flag domain.
constexpr flags32_t g_knownChunkFlags =
    COOKED_CHUNK_FLAG_COMPRESSED |
    COOKED_CHUNK_FLAG_OPTIONAL |
    COOKED_CHUNK_FLAG_HAS_CONTENT_HASH; // Complete V1 chunk flag domain.

CYPHER_NODISCARD bool_t IsCodecValid(
    cooked_chunk_codec_t codec ) noexcept
{
    switch ( codec ) {
        case cooked_chunk_codec_t::NONE:
        case cooked_chunk_codec_t::LZ4:
        case cooked_chunk_codec_t::ZSTD:
            return CY_TRUE;
    }
    return CY_FALSE;
}

CYPHER_NODISCARD cooked_resource_status_t ValidateHeader(
    const cooked_resource_header_t &header ) noexcept
{
    // Fixed offsets and zeroed reserves make the V1 envelope deterministic.
    if ( header.magic != CY_COOKED_RESOURCE_MAGIC ) {
        return cooked_resource_status_t::INVALID_MAGIC;
    }
    if ( header.nContainerVersion !=
         CY_COOKED_RESOURCE_CONTAINER_VERSION ) {
        return cooked_resource_status_t::VERSION_MISMATCH;
    }
    if ( header.cbHeader != CY_COOKED_RESOURCE_HEADER_SIZE ||
         header.iChunkTable != CY_COOKED_RESOURCE_HEADER_SIZE ||
         header.nReserved != 0u ) {
        return cooked_resource_status_t::INVALID_HEADER;
    }
    if ( header.resourceType == CY_INVALID_FOURCC ||
         header.nResourceVersion == 0u ) {
        return cooked_resource_status_t::INVALID_RESOURCE_TYPE;
    }
    if ( ( header.flags & ~g_knownResourceFlags ) != 0u ) {
        return cooked_resource_status_t::INVALID_FLAGS;
    }
    if ( header.nChunks == 0u ||
         header.nChunks > CY_COOKED_RESOURCE_MAX_CHUNKS ) {
        return cooked_resource_status_t::CHUNK_LIMIT_EXCEEDED;
    }

    // Presence bits and sentinel hash values must agree in both directions.
    const bool_t bHasSourceHash =
        ( header.flags & COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH ) != 0u;
    const bool_t bHasContentHash =
        ( header.flags & COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH ) != 0u;
    if ( bHasSourceHash != ContentHash_IsValid( header.sourceHash ) ||
         bHasContentHash != ContentHash_IsValid( header.contentHash ) ) {
        return cooked_resource_status_t::INVALID_HEADER;
    }

    const usize cbPrefix = CookedResource_PrefixSize( header.nChunks );
    if ( cbPrefix == 0u || header.cbFile < cbPrefix ) {
        return cooked_resource_status_t::INVALID_HEADER;
    }
    return cooked_resource_status_t::OK;
}

CYPHER_NODISCARD cooked_resource_status_t ValidateChunk(
    const cooked_chunk_desc_t &chunk ) noexcept
{
    // Alignment is persisted as a bounded power of two for direct payload access.
    if ( chunk.chunkType == CY_INVALID_FOURCC ||
         !IsCodecValid( chunk.codec ) || chunk.nReserved != 0u ||
         chunk.cbStored == 0u || chunk.cbDecoded == 0u ||
         chunk.nAlignment == 0u ||
         chunk.nAlignment > CY_COOKED_RESOURCE_MAX_ALIGNMENT ||
         ( chunk.nAlignment & ( chunk.nAlignment - 1u ) ) != 0u ) {
        return cooked_resource_status_t::INVALID_CHUNK;
    }
    if ( ( chunk.flags & ~g_knownChunkFlags ) != 0u ) {
        return cooked_resource_status_t::INVALID_FLAGS;
    }

    // Codec and compression flag are redundant by design and cross-validated.
    const bool_t bCompressed =
        ( chunk.flags & COOKED_CHUNK_FLAG_COMPRESSED ) != 0u;
    if ( chunk.codec == cooked_chunk_codec_t::NONE ) {
        if ( bCompressed || chunk.cbStored != chunk.cbDecoded ) {
            return cooked_resource_status_t::INVALID_CHUNK;
        }
    } else if ( !bCompressed ) {
        return cooked_resource_status_t::INVALID_CHUNK;
    }

    const bool_t bHasContentHash =
        ( chunk.flags & COOKED_CHUNK_FLAG_HAS_CONTENT_HASH ) != 0u;
    if ( bHasContentHash != ContentHash_IsValid( chunk.contentHash ) ) {
        return cooked_resource_status_t::INVALID_CHUNK;
    }
    return cooked_resource_status_t::OK;
}

CYPHER_NODISCARD bool_t WriteHeader(
    byte_writer_t &writer,
    const cooked_resource_header_t &header ) noexcept
{
    // Serialize fields explicitly; native padding and host endianness are irrelevant.
    return ByteWriter_WriteU32( &writer, header.magic ) &&
           ByteWriter_WriteU32( &writer, header.nContainerVersion ) &&
           ByteWriter_WriteU32( &writer, header.cbHeader ) &&
           ByteWriter_WriteU32( &writer, header.resourceType ) &&
           ByteWriter_WriteU32( &writer, header.nResourceVersion ) &&
           ByteWriter_WriteU32( &writer, header.flags ) &&
           ByteWriter_WriteU32( &writer, header.nChunks ) &&
           ByteWriter_WriteU32( &writer, header.nReserved ) &&
           ByteWriter_WriteU64( &writer, header.cbFile ) &&
           ByteWriter_WriteU64( &writer, header.iChunkTable ) &&
           ByteWriter_WriteU64( &writer, header.sourceHash.low ) &&
           ByteWriter_WriteU64( &writer, header.sourceHash.high ) &&
           ByteWriter_WriteU64( &writer, header.contentHash.low ) &&
           ByteWriter_WriteU64( &writer, header.contentHash.high );
}

CYPHER_NODISCARD bool_t ReadHeader(
    byte_reader_t &reader,
    cooked_resource_header_t &header ) noexcept
{
    return ByteReader_ReadU32( &reader, &header.magic ) &&
           ByteReader_ReadU32( &reader, &header.nContainerVersion ) &&
           ByteReader_ReadU32( &reader, &header.cbHeader ) &&
           ByteReader_ReadU32( &reader, &header.resourceType ) &&
           ByteReader_ReadU32( &reader, &header.nResourceVersion ) &&
           ByteReader_ReadU32( &reader, &header.flags ) &&
           ByteReader_ReadU32( &reader, &header.nChunks ) &&
           ByteReader_ReadU32( &reader, &header.nReserved ) &&
           ByteReader_ReadU64( &reader, &header.cbFile ) &&
           ByteReader_ReadU64( &reader, &header.iChunkTable ) &&
           ByteReader_ReadU64( &reader, &header.sourceHash.low ) &&
           ByteReader_ReadU64( &reader, &header.sourceHash.high ) &&
           ByteReader_ReadU64( &reader, &header.contentHash.low ) &&
           ByteReader_ReadU64( &reader, &header.contentHash.high );
}

CYPHER_NODISCARD bool_t WriteChunk(
    byte_writer_t &writer,
    const cooked_chunk_desc_t &chunk ) noexcept
{
    return ByteWriter_WriteU32( &writer, chunk.chunkType ) &&
           ByteWriter_WriteU32(
               &writer,
               static_cast<u32>( chunk.codec ) ) &&
           ByteWriter_WriteU32( &writer, chunk.flags ) &&
           ByteWriter_WriteU32( &writer, chunk.nAlignment ) &&
           ByteWriter_WriteU64( &writer, chunk.iOffset ) &&
           ByteWriter_WriteU64( &writer, chunk.cbStored ) &&
           ByteWriter_WriteU64( &writer, chunk.cbDecoded ) &&
           ByteWriter_WriteU64( &writer, chunk.nReserved ) &&
           ByteWriter_WriteU64( &writer, chunk.contentHash.low ) &&
           ByteWriter_WriteU64( &writer, chunk.contentHash.high );
}

CYPHER_NODISCARD bool_t ReadChunk(
    byte_reader_t &reader,
    cooked_chunk_desc_t &chunk ) noexcept
{
    u32 codec = 0u;
    if ( !ByteReader_ReadU32( &reader, &chunk.chunkType ) ||
         !ByteReader_ReadU32( &reader, &codec ) ||
         !ByteReader_ReadU32( &reader, &chunk.flags ) ||
         !ByteReader_ReadU32( &reader, &chunk.nAlignment ) ||
         !ByteReader_ReadU64( &reader, &chunk.iOffset ) ||
         !ByteReader_ReadU64( &reader, &chunk.cbStored ) ||
         !ByteReader_ReadU64( &reader, &chunk.cbDecoded ) ||
         !ByteReader_ReadU64( &reader, &chunk.nReserved ) ||
         !ByteReader_ReadU64( &reader, &chunk.contentHash.low ) ||
         !ByteReader_ReadU64( &reader, &chunk.contentHash.high ) ) {
        return CY_FALSE;
    }
    chunk.codec = static_cast<cooked_chunk_codec_t>( codec );
    return CY_TRUE;
}

} // namespace

usize CookedResource_PrefixSize( u32 nChunks ) noexcept
{
    if ( nChunks == 0u || nChunks > CY_COOKED_RESOURCE_MAX_CHUNKS ) {
        return 0u;
    }
    return CY_COOKED_RESOURCE_HEADER_SIZE +
           static_cast<usize>( nChunks ) * CY_COOKED_RESOURCE_CHUNK_SIZE;
}

content_hash_t CookedResource_ComputeContentHash(
    binary_block_t file ) noexcept
{
    if ( !BinaryBlock_IsValid( file ) ||
         file.cbSize <= CY_COOKED_RESOURCE_HEADER_SIZE ) {
        return CY_CONTENT_HASH_INVALID;
    }
    // Excluding the fixed header permits its contentHash field to seal the file.
    return ContentHash_Data( {
        file.pData + CY_COOKED_RESOURCE_HEADER_SIZE,
        file.cbSize - CY_COOKED_RESOURCE_HEADER_SIZE
    } );
}

cooked_resource_status_t CookedResource_ValidateLayout(
    const cooked_resource_header_t &header,
    span_t<const cooked_chunk_desc_t> chunks,
    u64 cbActualFile,
    usize *pInvalidChunk ) noexcept
{
    if ( pInvalidChunk != nullptr ) {
        *pInvalidChunk = CY_INVALID_SIZE;
    }
    if ( !Span_IsValid( chunks ) ) {
        return cooked_resource_status_t::INVALID_ARGUMENT;
    }

    const cooked_resource_status_t headerStatus = ValidateHeader( header );
    if ( headerStatus != cooked_resource_status_t::OK ) {
        return headerStatus;
    }
    if ( cbActualFile != header.cbFile ) {
        return cooked_resource_status_t::FILE_SIZE_MISMATCH;
    }
    if ( chunks.nCount != header.nChunks ) {
        return cooked_resource_status_t::INVALID_HEADER;
    }

    // Ordered descriptors make overlap and bounds checks a single linear pass.
    const u64 cbPrefix = CookedResource_PrefixSize( header.nChunks );
    u64 iPreviousEnd = cbPrefix;
    for ( usize iChunk = 0u; iChunk < chunks.nCount; ++iChunk ) {
        const cooked_chunk_desc_t &chunk = chunks.pData[iChunk];
        const cooked_resource_status_t chunkStatus = ValidateChunk( chunk );
        if ( chunkStatus != cooked_resource_status_t::OK ) {
            if ( pInvalidChunk != nullptr ) {
                *pInvalidChunk = iChunk;
            }
            return chunkStatus;
        }
        if ( chunk.iOffset < iPreviousEnd ) {
            if ( pInvalidChunk != nullptr ) {
                *pInvalidChunk = iChunk;
            }
            return cooked_resource_status_t::INVALID_CHUNK_ORDER;
        }
        if ( ( chunk.iOffset & ( chunk.nAlignment - 1u ) ) != 0u ||
             chunk.iOffset > header.cbFile ||
             chunk.cbStored > header.cbFile - chunk.iOffset ) {
            if ( pInvalidChunk != nullptr ) {
                *pInvalidChunk = iChunk;
            }
            return cooked_resource_status_t::INVALID_CHUNK;
        }
        iPreviousEnd = chunk.iOffset + chunk.cbStored;
    }
    return cooked_resource_status_t::OK;
}

cooked_resource_result_t CookedResource_WriteLayout(
    const cooked_resource_header_t &header,
    span_t<const cooked_chunk_desc_t> chunks,
    byte_span_t output ) noexcept
{
    cooked_resource_result_t result{};
    result.cbRequired = CookedResource_PrefixSize( header.nChunks );
    if ( !Span_IsValid( output ) ) {
        result.status = cooked_resource_status_t::INVALID_ARGUMENT;
        return result;
    }

    result.status = CookedResource_ValidateLayout(
        header,
        chunks,
        header.cbFile,
        &result.iChunk );
    if ( result.status != cooked_resource_status_t::OK ) {
        return result;
    }
    if ( output.nCount < result.cbRequired ) {
        result.status = cooked_resource_status_t::OUTPUT_TOO_SMALL;
        return result;
    }

    // Payload bytes are caller-owned; this routine emits only header and table.
    byte_writer_t writer{};
    if ( !ByteWriter_Init(
             &writer,
             output,
             data_byte_order_t::LITTLE ) ||
         !WriteHeader( writer, header ) ) {
        result.status = cooked_resource_status_t::OUTPUT_TOO_SMALL;
        return result;
    }
    for ( usize iChunk = 0u; iChunk < chunks.nCount; ++iChunk ) {
        if ( !WriteChunk( writer, chunks.pData[iChunk] ) ) {
            result.status = cooked_resource_status_t::OUTPUT_TOO_SMALL;
            result.iChunk = iChunk;
            return result;
        }
    }

    result.cbWritten = ByteWriter_BytesWritten( &writer );
    return result;
}

cooked_resource_result_t CookedResource_ReadLayout(
    binary_block_t input,
    cooked_resource_header_t *pHeaderOut,
    span_t<cooked_chunk_desc_t> chunksOut ) noexcept
{
    cooked_resource_result_t result{};
    if ( !BinaryBlock_IsValid( input ) || pHeaderOut == nullptr ||
         !Span_IsValid( chunksOut ) ) {
        result.status = cooked_resource_status_t::INVALID_ARGUMENT;
        return result;
    }
    result.cbRequired = CY_COOKED_RESOURCE_HEADER_SIZE;
    if ( input.cbSize < CY_COOKED_RESOURCE_HEADER_SIZE ) {
        result.status = cooked_resource_status_t::TRUNCATED_INPUT;
        return result;
    }

    // Decode into local state so malformed input cannot alter caller outputs.
    byte_reader_t reader{};
    cooked_resource_header_t header{};
    if ( !ByteReader_Init(
             &reader,
             input,
             data_byte_order_t::LITTLE ) ||
         !ReadHeader( reader, header ) ) {
        result.status = cooked_resource_status_t::TRUNCATED_INPUT;
        return result;
    }

    result.status = ValidateHeader( header );
    result.cbRequired = CookedResource_PrefixSize( header.nChunks );
    if ( result.status != cooked_resource_status_t::OK ) {
        return result;
    }
    if ( header.cbFile != input.cbSize ) {
        result.status = cooked_resource_status_t::FILE_SIZE_MISMATCH;
        return result;
    }
    if ( input.cbSize < result.cbRequired ) {
        result.status = cooked_resource_status_t::TRUNCATED_INPUT;
        return result;
    }
    if ( chunksOut.nCount < header.nChunks ) {
        result.status = cooked_resource_status_t::OUTPUT_TOO_SMALL;
        return result;
    }

    // The first pass validates every descriptor and the complete file layout.
    u64 iPreviousEnd = result.cbRequired;
    for ( usize iChunk = 0u; iChunk < header.nChunks; ++iChunk ) {
        cooked_chunk_desc_t chunk{};
        if ( !ReadChunk( reader, chunk ) ) {
            result.status = cooked_resource_status_t::TRUNCATED_INPUT;
            result.iChunk = iChunk;
            return result;
        }
        result.status = ValidateChunk( chunk );
        if ( result.status != cooked_resource_status_t::OK ) {
            result.iChunk = iChunk;
            return result;
        }
        if ( chunk.iOffset < iPreviousEnd ) {
            result.status = cooked_resource_status_t::INVALID_CHUNK_ORDER;
            result.iChunk = iChunk;
            return result;
        }
        if ( ( chunk.iOffset & ( chunk.nAlignment - 1u ) ) != 0u ||
             chunk.iOffset > header.cbFile ||
             chunk.cbStored > header.cbFile - chunk.iOffset ) {
            result.status = cooked_resource_status_t::INVALID_CHUNK;
            result.iChunk = iChunk;
            return result;
        }
        iPreviousEnd = chunk.iOffset + chunk.cbStored;
    }

    if ( ( header.flags & COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH ) != 0u &&
         !ContentHash_Equals(
             CookedResource_ComputeContentHash( input ),
             header.contentHash ) ) {
        result.status = cooked_resource_status_t::CONTENT_HASH_MISMATCH;
        result.iChunk = CY_INVALID_SIZE;
        return result;
    }

    // Re-read only after validation, keeping output unchanged on malformed input.
    if ( !ByteReader_Seek( &reader, CY_COOKED_RESOURCE_HEADER_SIZE ) ) {
        result.status = cooked_resource_status_t::INVALID_HEADER;
        return result;
    }
    for ( usize iChunk = 0u; iChunk < header.nChunks; ++iChunk ) {
        cooked_chunk_desc_t chunk{};
        if ( !ReadChunk( reader, chunk ) ) {
            result.status = cooked_resource_status_t::TRUNCATED_INPUT;
            result.iChunk = iChunk;
            return result;
        }
        chunksOut.pData[iChunk] = chunk;
    }

    *pHeaderOut = header;
    result.cbRead = result.cbRequired;
    return result;
}

bool_t CookedResource_Succeeded(
    const cooked_resource_result_t &result ) noexcept
{
    return result.status == cooked_resource_status_t::OK;
}

const char *CookedResource_StatusName(
    cooked_resource_status_t status ) noexcept
{
    switch ( status ) {
        case cooked_resource_status_t::OK: return "OK";
        case cooked_resource_status_t::INVALID_ARGUMENT: return "INVALID_ARGUMENT";
        case cooked_resource_status_t::OUTPUT_TOO_SMALL: return "OUTPUT_TOO_SMALL";
        case cooked_resource_status_t::TRUNCATED_INPUT: return "TRUNCATED_INPUT";
        case cooked_resource_status_t::INVALID_MAGIC: return "INVALID_MAGIC";
        case cooked_resource_status_t::VERSION_MISMATCH: return "VERSION_MISMATCH";
        case cooked_resource_status_t::INVALID_HEADER: return "INVALID_HEADER";
        case cooked_resource_status_t::INVALID_RESOURCE_TYPE: return "INVALID_RESOURCE_TYPE";
        case cooked_resource_status_t::INVALID_FLAGS: return "INVALID_FLAGS";
        case cooked_resource_status_t::CHUNK_LIMIT_EXCEEDED: return "CHUNK_LIMIT_EXCEEDED";
        case cooked_resource_status_t::INVALID_CHUNK: return "INVALID_CHUNK";
        case cooked_resource_status_t::INVALID_CHUNK_ORDER: return "INVALID_CHUNK_ORDER";
        case cooked_resource_status_t::FILE_SIZE_MISMATCH: return "FILE_SIZE_MISMATCH";
        case cooked_resource_status_t::CONTENT_HASH_MISMATCH: return "CONTENT_HASH_MISMATCH";
    }
    return "UNKNOWN";
}

} // namespace cypher::common
