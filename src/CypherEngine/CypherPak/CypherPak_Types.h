//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherPak/CypherPak_Types.h
//  Purpose: Declares the CypherPak Pak Types module.
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

#ifndef CYPHER_ENGINE_PAK_TYPES_H
#define CYPHER_ENGINE_PAK_TYPES_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherPak_Error.h"
#include "CypherPak_Format.h"

namespace cypher::engine::pak
{

inline constexpr common::u32 CYPHER_PAK_INVALID_HANDLE = 0u;
inline constexpr common::u32 CYPHER_PAK_INVALID_FILE_INDEX = 0xFFFFFFFFu;
inline constexpr common::u32 CYPHER_PAK_MAX_ARCHIVE_PATH_LENGTH = CYPHER_PAK_MAX_PATH_LENGTH;

using pak_handle_t = common::u32;
using pak_file_index_t = common::u32;

enum pak_open_flags_t : common::u32 {
    CYPHER_PAK_OPEN_NONE                = 0u,
    CYPHER_PAK_OPEN_VERIFY_HEADER       = 1u << 0u,
    CYPHER_PAK_OPEN_VERIFY_INDEX        = 1u << 1u,
    CYPHER_PAK_OPEN_VERIFY_FILE_HASHES  = 1u << 2u,
    CYPHER_PAK_OPEN_MEMORY_MAP          = 1u << 3u
};

enum pak_verify_flags_t : common::u32 {
    CYPHER_PAK_VERIFY_NONE              = 0u,
    CYPHER_PAK_VERIFY_HEADER            = 1u << 0u,
    CYPHER_PAK_VERIFY_INDEX             = 1u << 1u,
    CYPHER_PAK_VERIFY_FILE_HASHES       = 1u << 2u,
    CYPHER_PAK_VERIFY_ARCHIVE_HASH      = 1u << 3u,
    CYPHER_PAK_VERIFY_FULL              = CYPHER_PAK_VERIFY_HEADER |
                                          CYPHER_PAK_VERIFY_INDEX |
                                          CYPHER_PAK_VERIFY_FILE_HASHES |
                                          CYPHER_PAK_VERIFY_ARCHIVE_HASH
};

struct pak_file_info_t {
    char szVirtualPath[CYPHER_PAK_MAX_PATH_LENGTH]{};
    pak_file_index_t index{ CYPHER_PAK_INVALID_FILE_INDEX };
    common::u64 nDataOffset{ 0u };
    common::u64 nStoredSize{ 0u };
    common::u64 nUnpackedSize{ 0u };
    common::u64 modifiedTimeUtc{ 0u };
    common::u64 contentHash{ 0u };
    common::u32 szPathHash{ 0u };
    pak_compression_t compression{ pak_compression_t::NONE };
    common::u32 flags{ CYPHER_PAK_ENTRY_NONE };
};

struct pak_stats_t {
    common::u64 nFileCount{ 0u };
    common::u64 nArchiveSize{ 0u };
    common::u64 nCompressedFileCount{ 0u };
    common::u64 nStoredDataSize{ 0u };
    common::u64 nUnpackedDataSize{ 0u };
    common::u64 nReadCount{ 0u };
    common::u64 nBytesRead{ 0u };
};

struct pak_reader_t {
    pak_handle_t handle{ CYPHER_PAK_INVALID_HANDLE };
    void *pNativeFile{ nullptr };
    char szArchivePath[CYPHER_PAK_MAX_ARCHIVE_PATH_LENGTH]{};
    pak_header_t header{};
    pak_disk_file_entry_t *entries{ nullptr };
    char *stringTable{ nullptr };
    common::u64 nStringTableSize{ 0u };
    common::u32 nFileCount{ 0u };
    common::u32 flags{ CYPHER_PAK_OPEN_NONE };
    pak_stats_t stats{};
    bool open{ false };
};

struct pak_writer_t {
    pak_handle_t handle{ CYPHER_PAK_INVALID_HANDLE };
    void *pNativeFile{ nullptr };
    void *pBuilderState{ nullptr };
    char szArchivePath[CYPHER_PAK_MAX_ARCHIVE_PATH_LENGTH]{};
    common::u32 flags{ 0u };
    common::u32 nDataAlignment{ CYPHER_PAK_DATA_ALIGNMENT };
    pak_compression_t defaultCompression{ pak_compression_t::NONE };
    common::u32 nFileCount{ 0u };
    bool open{ false };
    bool finalized{ false };
};

struct pak_source_file_t {
    const char *szVirtualPath{ nullptr };
    const char *szPhysicalPath{ nullptr };
    pak_compression_t compression{ pak_compression_t::NONE };
    common::u32 flags{ CYPHER_PAK_ENTRY_NONE };
};

}       // namespace cypher::engine::pak

#endif // CYPHER_ENGINE_PAK_TYPES_H
