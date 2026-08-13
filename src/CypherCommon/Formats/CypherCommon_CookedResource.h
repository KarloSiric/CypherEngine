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
    Cy_MakeFourCC( 'C', 'Y', 'R', 'S' );
inline constexpr format_version_t CY_COOKED_RESOURCE_CONTAINER_VERSION = 1u;
inline constexpr usize CY_COOKED_RESOURCE_HEADER_SIZE = 80u;
inline constexpr usize CY_COOKED_RESOURCE_CHUNK_SIZE = 64u;
inline constexpr u32 CY_COOKED_RESOURCE_MAX_CHUNKS = 4096u;
inline constexpr u32 CY_COOKED_RESOURCE_MAX_ALIGNMENT = 1u * CY_MIB;

enum cooked_resource_flags_t : flags32_t {
    COOKED_RESOURCE_FLAG_NONE             = 0u,
    COOKED_RESOURCE_FLAG_HAS_SOURCE_HASH  = CYPHER_BIT32( 0 ),
    COOKED_RESOURCE_FLAG_HAS_CONTENT_HASH = CYPHER_BIT32( 1 )
};

enum cooked_chunk_flags_t : flags32_t {
    COOKED_CHUNK_FLAG_NONE             = 0u,
    COOKED_CHUNK_FLAG_COMPRESSED       = CYPHER_BIT32( 0 ),
    COOKED_CHUNK_FLAG_OPTIONAL         = CYPHER_BIT32( 1 ),
    COOKED_CHUNK_FLAG_HAS_CONTENT_HASH = CYPHER_BIT32( 2 )
};

// These numeric values are part of the versioned file format.
enum class cooked_chunk_codec_t : u32 {
    NONE = 0u,
    LZ4 = 1u,
    ZSTD = 2u
};

struct cooked_resource_header_t {
    fourcc_t magic{ CY_COOKED_RESOURCE_MAGIC };
    format_version_t nContainerVersion{
        CY_COOKED_RESOURCE_CONTAINER_VERSION
    };
    u32 cbHeader{ static_cast<u32>( CY_COOKED_RESOURCE_HEADER_SIZE ) };
    fourcc_t resourceType{ CY_INVALID_FOURCC };
    format_version_t nResourceVersion{ 0u };
    flags32_t flags{ COOKED_RESOURCE_FLAG_NONE };
    u32 nChunks{ 0u };
    u32 nReserved{ 0u };
    u64 cbFile{ 0u };
    u64 iChunkTable{ CY_COOKED_RESOURCE_HEADER_SIZE };
    content_hash_t sourceHash{};
    content_hash_t contentHash{};
};

struct cooked_chunk_desc_t {
    fourcc_t chunkType{ CY_INVALID_FOURCC };
    cooked_chunk_codec_t codec{ cooked_chunk_codec_t::NONE };
    flags32_t flags{ COOKED_CHUNK_FLAG_NONE };
    u32 nAlignment{ 1u };
    u64 iOffset{ 0u };
    u64 cbStored{ 0u };
    u64 cbDecoded{ 0u };
    u64 nReserved{ 0u };
    content_hash_t contentHash{};
};

enum class cooked_resource_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    OUTPUT_TOO_SMALL,
    TRUNCATED_INPUT,
    INVALID_MAGIC,
    VERSION_MISMATCH,
    INVALID_HEADER,
    INVALID_RESOURCE_TYPE,
    INVALID_FLAGS,
    CHUNK_LIMIT_EXCEEDED,
    INVALID_CHUNK,
    INVALID_CHUNK_ORDER,
    FILE_SIZE_MISMATCH,
    CONTENT_HASH_MISMATCH
};

struct cooked_resource_result_t {
    cooked_resource_status_t status{ cooked_resource_status_t::OK };
    usize cbRead{ 0u };
    usize cbWritten{ 0u };
    usize cbRequired{ 0u };
    usize iChunk{ CY_INVALID_SIZE };
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
