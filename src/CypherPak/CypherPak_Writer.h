//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherPak/CypherPak_Writer.h
//  Purpose: Declares the CypherPak Pak Writer module.
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
Writer Contract

Package output is deterministic and published only after the complete directory and payload have
been written successfully. On-disk offsets and sizes are checked before conversion to
fixed-width fields.
================
*/

#ifndef CYPHER_ENGINE_PAK_WRITER_H
#define CYPHER_ENGINE_PAK_WRITER_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherPak_Types.h"

namespace cypher::engine
{

enum pak_writer_flags_t : common::u32 {
    CYPHER_PAK_WRITER_NONE              = 0u,       // Disable all optional writer policies.
    CYPHER_PAK_WRITER_DETERMINISTIC     = 1u << 0u, // Remove unstable metadata and ordering from output.
    CYPHER_PAK_WRITER_SORT_INDEX        = 1u << 1u, // Sort normalized paths for deterministic binary lookup.
    CYPHER_PAK_WRITER_WRITE_HASHES      = 1u << 2u, // Persist unpacked content hashes for verification.
    CYPHER_PAK_WRITER_FAIL_ON_DUPLICATE = 1u << 3u  // Reject duplicate normalized virtual paths.
};

struct pak_writer_config_t {
    const char *szArchivePath{ nullptr };                   // Final archive destination; borrowed for the call.
    common::u32 flags{ CYPHER_PAK_WRITER_DETERMINISTIC |
                       CYPHER_PAK_WRITER_SORT_INDEX |
                       CYPHER_PAK_WRITER_WRITE_HASHES |
                       CYPHER_PAK_WRITER_FAIL_ON_DUPLICATE };
    pak_compression_t defaultCompression{ pak_compression_t::NONE }; // Compression applied unless a source overrides it.
    common::u32 nDataAlignment{ CYPHER_PAK_DATA_ALIGNMENT }; // Required alignment of every payload offset.
};

pak_error_t Pak_CreateArchive(
    const pak_writer_config_t &config,
    const pak_source_file_t *files,
    common::u32 nFileCount );

pak_error_t Pak_BeginWriter(
    const pak_writer_config_t &config,
    pak_writer_t &writer );

pak_error_t Pak_AddFile(
    pak_writer_t &writer,
    const pak_source_file_t &file );

pak_error_t Pak_FinishWriter( pak_writer_t &writer );

pak_error_t Pak_CancelWriter( pak_writer_t &writer );

}       // namespace cypher::engine

#endif // CYPHER_ENGINE_PAK_WRITER_H
