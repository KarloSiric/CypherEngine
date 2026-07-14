//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherPak/CypherPak_Writer.h
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

#ifndef CYPHER_ENGINE_PAK_WRITER_H
#define CYPHER_ENGINE_PAK_WRITER_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherPak_Types.h"

namespace cypher::engine::pak
{

enum pak_writer_flags_t : common::u32 {
    CYPHER_PAK_WRITER_NONE              = 0u,
    CYPHER_PAK_WRITER_DETERMINISTIC     = 1u << 0u,
    CYPHER_PAK_WRITER_SORT_INDEX        = 1u << 1u,
    CYPHER_PAK_WRITER_WRITE_HASHES      = 1u << 2u,
    CYPHER_PAK_WRITER_FAIL_ON_DUPLICATE = 1u << 3u
};

struct pak_writer_config_t {
    const char *szArchivePath{ nullptr };
    common::u32 flags{ CYPHER_PAK_WRITER_DETERMINISTIC |
                       CYPHER_PAK_WRITER_SORT_INDEX |
                       CYPHER_PAK_WRITER_WRITE_HASHES |
                       CYPHER_PAK_WRITER_FAIL_ON_DUPLICATE };
    pak_compression_t defaultCompression{ pak_compression_t::NONE };
    common::u32 nDataAlignment{ CYPHER_PAK_DATA_ALIGNMENT };
};

pak_error_t CypherPak_CreateArchive(
    const pak_writer_config_t &config,
    const pak_source_file_t *files,
    common::u32 nFileCount );

pak_error_t CypherPak_BeginWriter(
    const pak_writer_config_t &config,
    pak_writer_t &writer );

pak_error_t CypherPak_AddFile(
    pak_writer_t &writer,
    const pak_source_file_t &file );

pak_error_t CypherPak_FinishWriter( pak_writer_t &writer );

pak_error_t CypherPak_CancelWriter( pak_writer_t &writer );

}       // namespace cypher::engine::pak

#endif // CYPHER_ENGINE_PAK_WRITER_H
