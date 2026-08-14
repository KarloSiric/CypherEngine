//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherPak/CypherPak_Format.h
//  Purpose: Declares the CypherPak Pak Format module.
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

#ifndef CYPHER_ENGINE_PAK_FORMAT_H
#define CYPHER_ENGINE_PAK_FORMAT_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon.h"

namespace cypher::engine::pak
{

/*
================
CypherPak Format

CypherPak archives use readable magic bytes and explicit little-endian integer
serialization. Do not depend on host CPU endian layout when reading or writing.
================
*/
inline constexpr common::u32 CYPHER_PAK_MAGIC_SIZE              = 16u;
inline constexpr common::u32 CYPHER_PAK_FORMAT_VERSION          = 10u;
inline constexpr common::u32 CYPHER_PAK_ENDIAN_TAG              = 0x12345678u;
inline constexpr common::u32 CYPHER_PAK_DATA_ALIGNMENT          = 16u;
inline constexpr common::u32 CYPHER_PAK_MAX_PATH_LENGTH         = 260u;
inline constexpr common::u32 CYPHER_PAK_MAX_EXTENSION_LENGTH    = 16u;
inline constexpr common::u32 CYPHER_PAK_HEADER_SIZE             = 136u;
inline constexpr common::u32 CYPHER_PAK_FILE_ENTRY_SIZE         = 64u;

inline constexpr const char CYPHER_PAK_EXTENSION[]              = ".cypak";
inline constexpr const char CYPHER_PAK_FORMAT_NAME[]            = "Cypher Package Archive";
inline constexpr const char CYPHER_PAK_FORMAT_LABEL[]           = "CYPACKAGE10";
inline constexpr char CYPHER_PAK_MAGIC[CYPHER_PAK_MAGIC_SIZE]   = {
                                                                    'C', 'Y', 'P', 'A', 'C', 'K', 'A', 'G', 'E',
                                                                    '\0', '\0', '\0', '\0', '\0', '\0', '\0'
};

enum pak_format_flags_t : common::u32 {
    CYPHER_PAK_FORMAT_NONE              = 0u,
    CYPHER_PAK_FORMAT_INDEX_SORTED      = 1u << 0u,
    CYPHER_PAK_FORMAT_HAS_FILE_HASHES   = 1u << 1u,
    CYPHER_PAK_FORMAT_HAS_ARCHIVE_HASH  = 1u << 2u,
    CYPHER_PAK_FORMAT_COMPRESSED_INDEX  = 1u << 3u
};

enum pak_entry_flags_t : common::u32 {
    CYPHER_PAK_ENTRY_NONE               = 0u,
    CYPHER_PAK_ENTRY_COMPRESSED         = 1u << 0u,
    CYPHER_PAK_ENTRY_HAS_HASH           = 1u << 1u
};

enum class pak_compression_t : common::u32 {
    NONE = 0u,
    LZ4,
    ZSTD
};

/*
================
Serialized Header Layout

The field order below is the canonical disk order. Reader and writer code
should still serialize every numeric field explicitly as little-endian.
================
*/
struct pak_header_t {
    char magic[CYPHER_PAK_MAGIC_SIZE]{};
    common::u32 version{ CYPHER_PAK_FORMAT_VERSION };
    common::u32 nHeaderSize{ CYPHER_PAK_HEADER_SIZE };
    common::u32 endianTag{ CYPHER_PAK_ENDIAN_TAG };
    common::u32 flags{ CYPHER_PAK_FORMAT_NONE };

    common::u64 nArchiveSize{ 0u };
    common::u64 nFileCount{ 0u };

    common::u64 nIndexOffset{ 0u };
    common::u64 nIndexSize{ 0u };
    common::u64 nStringTableOffset{ 0u };
    common::u64 nStringTableSize{ 0u };
    common::u64 nDataOffset{ 0u };
    common::u64 nDataSize{ 0u };

    common::u64 archiveHash{ 0u };
    common::u64 reserved[4]{};
};

struct pak_disk_file_entry_t {
    common::u64 nPathOffset{ 0u };
    common::u64 nDataOffset{ 0u };
    common::u64 nStoredSize{ 0u };
    common::u64 nUnpackedSize{ 0u };
    common::u64 modifiedTimeUtc{ 0u };
    common::u64 contentHash{ 0u };

    common::u32 nPathSize{ 0u };
    common::u32 szPathHash{ 0u };
    common::u32 compression{ static_cast<common::u32>( pak_compression_t::NONE ) };
    common::u32 flags{ CYPHER_PAK_ENTRY_NONE };
};

static_assert( sizeof( common::u32 ) == 4u, "CypherPak requires 32-bit common::u32" );
static_assert( sizeof( common::u64 ) == 8u, "CypherPak requires 64-bit common::u64" );
static_assert( sizeof( pak_header_t ) == CYPHER_PAK_HEADER_SIZE, "CypherPak header size changed" );
static_assert( sizeof( pak_disk_file_entry_t ) == CYPHER_PAK_FILE_ENTRY_SIZE, "CypherPak file entry size changed" );

constexpr inline bool CypherPak_MagicEquals( const char *magic )
{
    if ( magic == nullptr ) {
        return false;
    }

    for ( common::u32 i = 0u; i < CYPHER_PAK_MAGIC_SIZE; ++i ) {
        if ( magic[i] != CYPHER_PAK_MAGIC[i] ) {
            return false;
        }
    }

    return true;
}

constexpr inline common::u32 CypherPak_AlignUp( const common::u32 value, const common::u32 alignment )
{
    return alignment == 0u ? value : ( ( value + alignment - 1u ) / alignment ) * alignment;
}

constexpr inline common::u64 CypherPak_AlignUp64( const common::u64 value, const common::u64 alignment )
{
    return alignment == 0u ? value : ( ( value + alignment - 1u ) / alignment ) * alignment;
}

inline common::u16 CypherPak_LoadU16LE( const void *data )
{
    const common::u8 *bytes = static_cast<const common::u8 *>( data );
    return static_cast<common::u16>(
        static_cast<common::u16>( bytes[0] ) |
        static_cast<common::u16>( static_cast<common::u16>( bytes[1] ) << 8u ) );
}

inline common::u32 CypherPak_LoadU32LE( const void *data )
{
    const common::u8 *bytes = static_cast<const common::u8 *>( data );
    return static_cast<common::u32>( bytes[0] ) |
        ( static_cast<common::u32>( bytes[1] ) << 8u ) |
        ( static_cast<common::u32>( bytes[2] ) << 16u ) |
        ( static_cast<common::u32>( bytes[3] ) << 24u );
}

inline common::u64 CypherPak_LoadU64LE( const void *data )
{
    const common::u8 *bytes = static_cast<const common::u8 *>( data );
    return static_cast<common::u64>( bytes[0] ) |
        ( static_cast<common::u64>( bytes[1] ) << 8u ) |
        ( static_cast<common::u64>( bytes[2] ) << 16u ) |
        ( static_cast<common::u64>( bytes[3] ) << 24u ) |
        ( static_cast<common::u64>( bytes[4] ) << 32u ) |
        ( static_cast<common::u64>( bytes[5] ) << 40u ) |
        ( static_cast<common::u64>( bytes[6] ) << 48u ) |
        ( static_cast<common::u64>( bytes[7] ) << 56u );
}

inline void CypherPak_StoreU16LE( void *data, const common::u16 value )
{
    common::u8 *bytes = static_cast<common::u8 *>( data );
    bytes[0] = static_cast<common::u8>( value & 0xFFu );
    bytes[1] = static_cast<common::u8>( ( value >> 8u ) & 0xFFu );
}

inline void CypherPak_StoreU32LE( void *data, const common::u32 value )
{
    common::u8 *bytes = static_cast<common::u8 *>( data );
    bytes[0] = static_cast<common::u8>( value & 0xFFu );
    bytes[1] = static_cast<common::u8>( ( value >> 8u ) & 0xFFu );
    bytes[2] = static_cast<common::u8>( ( value >> 16u ) & 0xFFu );
    bytes[3] = static_cast<common::u8>( ( value >> 24u ) & 0xFFu );
}

inline void CypherPak_StoreU64LE( void *data, const common::u64 value )
{
    common::u8 *bytes = static_cast<common::u8 *>( data );
    bytes[0] = static_cast<common::u8>( value & 0xFFu );
    bytes[1] = static_cast<common::u8>( ( value >> 8u ) & 0xFFu );
    bytes[2] = static_cast<common::u8>( ( value >> 16u ) & 0xFFu );
    bytes[3] = static_cast<common::u8>( ( value >> 24u ) & 0xFFu );
    bytes[4] = static_cast<common::u8>( ( value >> 32u ) & 0xFFu );
    bytes[5] = static_cast<common::u8>( ( value >> 40u ) & 0xFFu );
    bytes[6] = static_cast<common::u8>( ( value >> 48u ) & 0xFFu );
    bytes[7] = static_cast<common::u8>( ( value >> 56u ) & 0xFFu );
}

}       // namespace cypher::engine::pak

#endif // CYPHER_ENGINE_PAK_FORMAT_H
