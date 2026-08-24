//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Formats/CypherCommon_CookedResource.h
//  Purpose: Declares the common envelope for cooked runtime resources.
//  Details: The envelope is serialized explicitly in little-endian order. It
//           identifies a resource format and describes ordered, bounds-checked
//           payload chunks without exposing compiler struct layout on disk.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_FORMATS_COOKEDRESOURCE_H
#define CYPHER_COMMON_FORMATS_COOKEDRESOURCE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ContentHash.h"

namespace cypher::common
{

inline constexpr fourcc_t CY_COOKED_RESOURCE_MAGIC =
    Cy_MakeFourCC( 'C', 'Y', 'R', 'S' ); // Every cooked resource begins with CYRS.
inline constexpr format_version_t CY_COOKED_RESOURCE_CONTAINER_VERSION = 1u; // Envelope.
inline constexpr usize CY_COOKED_RESOURCE_HEADER_SIZE = 80u; // Serialized header bytes.
inline constexpr usize CY_COOKED_RESOURCE_CHUNK_SIZE = 64u; // Serialized table record.
inline constexpr u32 CY_COOKED_RESOURCE_MAX_CHUNKS = 4096u; // Parser allocation bound.
inline constexpr u32 CY_COOKED_RESOURCE_MAX_ALIGNMENT = 1u * CY_MIB; // Chunk limit.

enum cooked_resource_flags_t : flags32_t {
    COOKED_RESOURCE_FLAG_NONE             = 0u, // No optional header hashes.
    COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH  = CYPHER_BIT32( 0 ), // sourceHash is valid.
    COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH = CYPHER_BIT32( 1 )  // contentHash is valid.
};

enum cooked_chunk_flags_t : flags32_t {
    COOKED_CHUNK_FLAG_NONE             = 0u, // Plain required payload.
    COOKED_CHUNK_FLAG_COMPRESSED       = CYPHER_BIT32( 0 ), // Stored and decoded sizes differ.
    COOKED_CHUNK_FLAG_OPTIONAL         = CYPHER_BIT32( 1 ), // Unknown readers may skip it.
    COOKED_CHUNK_FLAG_HAS_CONTENT_HASH = CYPHER_BIT32( 2 )  // contentHash is valid.
};

// These numeric values are part of the versioned file format.
enum class cooked_chunk_codec_t : u32 {
    NONE = 0u, // Payload is stored verbatim.
    LZ4 = 1u,  // Payload uses the engine's LZ4 contract.
    ZSTD = 2u  // Payload uses the engine's Zstandard contract.
};

struct cooked_resource_header_t {
    fourcc_t magic{ CY_COOKED_RESOURCE_MAGIC }; // Container signature.
    format_version_t nContainerVersion{
        CY_COOKED_RESOURCE_CONTAINER_VERSION
    }; // CYRS envelope version, independent of resource payload version.
    u32 cbHeader{ static_cast<u32>( CY_COOKED_RESOURCE_HEADER_SIZE ) }; // Header bytes.
    fourcc_t resourceType{ CY_INVALID_FOURCC }; // CYSH, CYTX, CYMT, or another type.
    format_version_t nResourceVersion{ 0u }; // Type-specific format version.
    flags32_t flags{ COOKED_RESOURCE_FLAG_NONE }; // cooked_resource_flags_t bits.
    u32 nChunks{ 0u }; // Number of records in the chunk table.
    u32 nReserved{ 0u }; // Must be zero in container version one.
    u64 cbFile{ 0u }; // Exact canonical file size.
    u64 iChunkTable{ CY_COOKED_RESOURCE_HEADER_SIZE }; // Table byte offset.
    content_hash_t sourceHash{}; // Optional authored-input identity.
    content_hash_t contentHash{}; // Optional hash of bytes after the fixed header.
};

struct cooked_chunk_desc_t {
    fourcc_t chunkType{ CY_INVALID_FOURCC }; // Type-specific payload identity.
    cooked_chunk_codec_t codec{ cooked_chunk_codec_t::NONE }; // Storage codec.
    flags32_t flags{ COOKED_CHUNK_FLAG_NONE }; // cooked_chunk_flags_t bits.
    u32 nAlignment{ 1u }; // Required power-of-two file alignment.
    u64 iOffset{ 0u }; // Absolute payload byte offset.
    u64 cbStored{ 0u }; // Bytes occupied in the file.
    u64 cbDecoded{ 0u }; // Bytes after decompression.
    u64 nReserved{ 0u }; // Must remain zero in container version one.
    content_hash_t contentHash{}; // Optional hash of stored payload bytes.
};

enum class cooked_resource_status_t : u8 {
    OK = 0u,             // Envelope operation completed.
    INVALID_ARGUMENT,   // Input block, span, or output pointer is invalid.
    OUTPUT_TOO_SMALL,   // Destination cannot hold the prefix or descriptors.
    TRUNCATED_INPUT,    // Input ends before declared metadata is available.
    INVALID_MAGIC,      // File does not begin with the CYRS signature.
    VERSION_MISMATCH,   // Container version is unsupported.
    INVALID_HEADER,     // Header offsets, reserves, hashes, or counts disagree.
    INVALID_RESOURCE_TYPE,// Payload type or payload version is absent.
    INVALID_FLAGS,      // Header or chunk contains unknown flag bits.
    CHUNK_LIMIT_EXCEEDED,// Chunk count exceeds the bounded parser contract.
    INVALID_CHUNK,      // Descriptor codec, size, alignment, or hash is invalid.
    INVALID_CHUNK_ORDER,// Descriptors overlap or are not ordered by file offset.
    FILE_SIZE_MISMATCH, // Actual and declared complete file sizes differ.
    CONTENT_HASH_MISMATCH // Sealed payload-area hash failed verification.
};

struct cooked_resource_result_t {
    cooked_resource_status_t status{ cooked_resource_status_t::OK }; // Result code.
    usize cbRead{ 0u };              // Input bytes consumed after full validation.
    usize cbWritten{ 0u };           // Header and table bytes emitted.
    usize cbRequired{ 0u };          // Exact prefix capacity required.
    usize iChunk{ CY_INVALID_SIZE }; // First invalid chunk-table entry, when known.
};

// Returns the encoded header and chunk-table size, or zero for an invalid count.
CYPHER_NODISCARD CYPHER_COMMON_API
usize CookedResource_PrefixSize( u32 nChunks ) noexcept;

// Hashes the exact serialized bytes after the fixed header. Cookers call this
// after writing the chunk table, deterministic padding, and stored payloads.
CYPHER_NODISCARD CYPHER_COMMON_API
content_hash_t CookedResource_ComputeContentHash(
    binary_block_t file ) noexcept;

// Validates the complete declared layout without reading payload bytes.
CYPHER_NODISCARD CYPHER_COMMON_API
cooked_resource_status_t CookedResource_ValidateLayout(
    const cooked_resource_header_t &header,
    span_t<const cooked_chunk_desc_t> chunks,
    u64 cbActualFile,
    usize *pInvalidChunk = nullptr ) noexcept;

// Writes only the fixed header and chunk table. Payload storage is caller-owned.
CYPHER_NODISCARD CYPHER_COMMON_API
cooked_resource_result_t CookedResource_WriteLayout(
    const cooked_resource_header_t &header,
    span_t<const cooked_chunk_desc_t> chunks,
    byte_span_t output ) noexcept;

// Reads and validates a complete file before committing caller-visible outputs.
CYPHER_NODISCARD CYPHER_COMMON_API
cooked_resource_result_t CookedResource_ReadLayout(
    binary_block_t input,
    cooked_resource_header_t *pHeaderOut,
    span_t<cooked_chunk_desc_t> chunksOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t CookedResource_Succeeded(
    const cooked_resource_result_t &result ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *CookedResource_StatusName(
    cooked_resource_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_FORMATS_COOKEDRESOURCE_H
