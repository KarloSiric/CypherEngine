//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherPak/CypherPak_Types.h
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

/*
================
Types Contract

Package structures are serialized contracts rather than native object layouts. Their widths,
byte order, alignment, and version fields must remain explicit and validated.
================
*/

#ifndef CYPHER_ENGINE_PAK_TYPES_H
#define CYPHER_ENGINE_PAK_TYPES_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherPak_Error.h"
#include "CypherPak_Format.h"

namespace cypher::engine
{

inline constexpr common::u32 CYPHER_PAK_INVALID_HANDLE = 0u;
inline constexpr common::u32 CYPHER_PAK_INVALID_FILE_INDEX = 0xFFFFFFFFu;
inline constexpr common::u32 CYPHER_PAK_MAX_ARCHIVE_PATH_LENGTH = CYPHER_PAK_MAX_PATH_LENGTH;

using pak_handle_t = common::u32;
using pak_file_index_t = common::u32;

enum pak_open_flags_t : common::u32 {
    CYPHER_PAK_OPEN_NONE                = 0u,       // Perform only mandatory structural validation.
    CYPHER_PAK_OPEN_VERIFY_HEADER       = 1u << 0u, // Validate header identity, version, and section ranges.
    CYPHER_PAK_OPEN_VERIFY_INDEX        = 1u << 1u, // Validate every index and string-table reference.
    CYPHER_PAK_OPEN_VERIFY_FILE_HASHES  = 1u << 2u, // Hash each unpacked file while opening the archive.
    CYPHER_PAK_OPEN_MEMORY_MAP          = 1u << 3u  // Request mapped access when the platform backend supports it.
};

enum pak_verify_flags_t : common::u32 {
    CYPHER_PAK_VERIFY_NONE              = 0u,       // Do not request an optional verification pass.
    CYPHER_PAK_VERIFY_HEADER            = 1u << 0u, // Recheck the serialized header contract.
    CYPHER_PAK_VERIFY_INDEX             = 1u << 1u, // Recheck entry ordering and all referenced ranges.
    CYPHER_PAK_VERIFY_FILE_HASHES       = 1u << 2u, // Decode and hash entries that advertise content hashes.
    CYPHER_PAK_VERIFY_ARCHIVE_HASH      = 1u << 3u, // Verify the whole-archive digest when one is present.
    CYPHER_PAK_VERIFY_FULL              = CYPHER_PAK_VERIFY_HEADER |
                                          CYPHER_PAK_VERIFY_INDEX |
                                          CYPHER_PAK_VERIFY_FILE_HASHES |
                                          CYPHER_PAK_VERIFY_ARCHIVE_HASH
};

struct pak_file_info_t {
    char szVirtualPath[CYPHER_PAK_MAX_PATH_LENGTH]{};       // Normalized package-relative path, always terminated.
    pak_file_index_t index{ CYPHER_PAK_INVALID_FILE_INDEX }; // Stable index for the lifetime of the open reader.
    common::u64 nDataOffset{ 0u };                          // Absolute archive offset of stored payload bytes.
    common::u64 nStoredSize{ 0u };                          // Compressed or raw byte count on disk.
    common::u64 nUnpackedSize{ 0u };                        // Required destination size for a normal read.
    common::u64 modifiedTimeUtc{ 0u };                      // Archived source modification timestamp.
    common::u64 contentHash{ 0u };                          // Hash of unpacked content when HAS_HASH is set.
    common::u32 szPathHash{ 0u };                           // Lookup accelerator; not an identity by itself.
    pak_compression_t compression{ pak_compression_t::NONE }; // Decoder required for the stored payload.
    common::u32 flags{ CYPHER_PAK_ENTRY_NONE };             // pak_entry_flags_t bits copied from the disk entry.
};

struct pak_stats_t {
    common::u64 nFileCount{ 0u };                           // Number of indexed files.
    common::u64 nArchiveSize{ 0u };                         // Complete archive length in bytes.
    common::u64 nCompressedFileCount{ 0u };                 // Entries that require decompression.
    common::u64 nStoredDataSize{ 0u };                      // Sum of stored payload sizes.
    common::u64 nUnpackedDataSize{ 0u };                    // Sum of logical payload sizes.
    common::u64 nReadCount{ 0u };                           // Successful file reads through this reader.
    common::u64 nBytesRead{ 0u };                           // Unpacked bytes returned to callers.
};

struct pak_reader_t {
    pak_handle_t handle{ CYPHER_PAK_INVALID_HANDLE };       // Process-local diagnostic handle; never serialized.
    void *pNativeFile{ nullptr };                           // Opaque FILE/platform handle owned by the reader.
    char szArchivePath[CYPHER_PAK_MAX_ARCHIVE_PATH_LENGTH]{}; // Physical archive path retained for diagnostics.
    pak_header_t header{};                                  // Host-order validated copy of the disk header.
    pak_disk_file_entry_t *entries{ nullptr };              // Owned host-order index array.
    char *stringTable{ nullptr };                           // Owned packed path bytes loaded from the archive.
    common::u64 nStringTableSize{ 0u };                     // Valid byte extent of stringTable.
    common::u32 nFileCount{ 0u };                           // Narrowed count after capacity validation.
    common::u32 flags{ CYPHER_PAK_OPEN_NONE };              // pak_open_flags_t used when the reader was opened.
    pak_stats_t stats{};                                    // Structural totals and accumulated read counters.
    bool open{ false };                                     // True only after complete transactional initialization.
};

struct pak_writer_t {
    pak_handle_t handle{ CYPHER_PAK_INVALID_HANDLE };       // Process-local diagnostic handle; never serialized.
    void *pNativeFile{ nullptr };                           // Temporary archive file owned until finish or cancel.
    void *pBuilderState{ nullptr };                         // Private writer index/string bookkeeping.
    char szArchivePath[CYPHER_PAK_MAX_ARCHIVE_PATH_LENGTH]{}; // Final destination path retained for publication.
    common::u32 flags{ 0u };                               // pak_writer_flags_t controlling deterministic output.
    common::u32 nDataAlignment{ CYPHER_PAK_DATA_ALIGNMENT }; // Required payload alignment in archive bytes.
    pak_compression_t defaultCompression{ pak_compression_t::NONE }; // Method used when a source has no override.
    common::u32 nFileCount{ 0u };                           // Files accepted into the in-progress archive.
    bool open{ false };                                     // Writer owns live native/builder resources.
    bool finalized{ false };                                // Final header and index were published successfully.
};

struct pak_source_file_t {
    const char *szVirtualPath{ nullptr };                   // Package-relative lookup name stored in the archive.
    const char *szPhysicalPath{ nullptr };                  // Host filesystem source read during the build.
    pak_compression_t compression{ pak_compression_t::NONE }; // Per-file compression choice.
    common::u32 flags{ CYPHER_PAK_ENTRY_NONE };             // Additional pak_entry_flags_t requested by the caller.
};

}       // namespace cypher::engine

#endif // CYPHER_ENGINE_PAK_TYPES_H
