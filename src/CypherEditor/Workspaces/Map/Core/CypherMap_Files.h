//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Files.h
//  Purpose: Declares reading and writing a map on disk: one `.cymap` file
//           (CYMAP.md section 1).
//  Details: The map document itself does no file I/O so that the editor,
//           the map compiler, and tests share it; this is the one place
//           that turns a path into text and text back into a file.
//
//           Saving writes `<file>.tmp` beside the map and renames it over
//           the map only once it was written completely, so a full disk or
//           an unwritable folder leaves the map on disk exactly as it was.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//  - Reworked on 2026-10-12 for V11: one file; chunk folders are gone
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_FILES_H
#define CYPHER_EDITOR_MAP_FILES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

namespace cypher::editor::map
{

enum class map_files_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    ROOT_MISSING,       // No file at the path.
    READ_FAILED,        // A file exists but could not be read.
    WRITE_FAILED,       // A file could not be written or replaced; see the log.
    DOCUMENT_FAILED     // The map document refused; see documentStatus.
};

struct map_files_result_t {
    map_files_status_t status{ map_files_status_t::OK };
    map_status_t documentStatus{ map_status_t::OK };
    common::usize cbFile{ 0u }; // Bytes read or written.
};

// Loads a map from disk into a default-constructed document. status is OK
// when the document is usable - check documentStatus for READ_ONLY. A file
// over MAP_TEXT_MAX is not read.
CYPHER_NODISCARD map_files_result_t MapFiles_Load(
    map_document_t *pMap,
    const common::allocator_t *pAllocator,
    const char *pPath,
    common::u32 flags = MAP_LOAD_FLAG_NONE ) noexcept;

// Saves a map to disk at pPath (which may differ from where it was loaded:
// "Save As").
CYPHER_NODISCARD map_files_result_t MapFiles_Save( map_document_t *pMap, const char *pPath ) noexcept;

CYPHER_NODISCARD const char *MapFiles_StatusName( map_files_status_t status ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_FILES_H
