//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Files.h
//  Purpose: Declares reading and writing a map on disk: the `.cymap` root
//           and the chunk directory beside it (CYMAP.md section 1).
//  Details: The map document itself does no file I/O so that the editor,
//           the map compiler, and tests share it; this is the one place
//           that turns a path into texts and texts back into files.
//
//           Chunks are found by scanning the chunk directory for
//           `<layer>/<cell>.cymapchunk`; other files (notes, backups,
//           leftover `.tmp` files, deeper folders) are ignored, so a stray
//           copy of a chunk cannot load as a duplicate. The scan only sees
//           lowercase names, which is all the writer produces.
//
//           Saving happens in two phases. Every file is first written beside
//           its target as `<file>.tmp`; only when all of them exist is each
//           renamed over its target, root last, and only then are emptied
//           chunks deleted. A full disk or an unwritable folder therefore
//           leaves the map on disk exactly as it was. A rename failing in the
//           second phase (rare: same folder, same volume) stops the save
//           before anything is deleted, so objects that moved between chunks
//           can end up in two files - a duplicate the loader reports - but
//           never in none. Empty layer folders are left in place; the loader
//           does not care and deleting folders is not worth the risk.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
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
    ROOT_MISSING,       // No file at the root path.
    READ_FAILED,        // A file exists but could not be read.
    WRITE_FAILED,       // A file could not be written or replaced; see the log.
    DOCUMENT_FAILED     // The map document refused; see documentStatus.
};

struct map_files_result_t {
    map_files_status_t status{ map_files_status_t::OK };
    map_status_t documentStatus{ map_status_t::OK };
    common::usize nChunkFiles{ 0u };  // Chunk files read or written.
    common::usize nRemoved{ 0u };     // Chunk files deleted on save.
};

// The chunk directory of a root path: the path without `.cymap`.
// Returns the length written, 0 when the path does not end in `.cymap` or
// does not fit.
common::usize MapFiles_ChunkDirectory( const char *pRootPath, char *pBuffer, common::usize cchBuffer ) noexcept;

// Loads a map from disk into a default-constructed document. status is OK
// when the document is usable - check documentStatus for READ_ONLY. Chunk
// files that cannot be read, or are over MAP_CHUNK_TEXT_MAX, are passed to
// the document as unreadable, so they are reported and never overwritten.
CYPHER_NODISCARD map_files_result_t MapFiles_Load(
    map_document_t *pMap,
    const common::allocator_t *pAllocator,
    const char *pRootPath,
    common::u32 flags = MAP_LOAD_FLAG_NONE ) noexcept;

// Saves a map to disk at pRootPath (which may differ from where it was
// loaded: "Save As"). Chunk files already in a Save As target that this map
// does not write are left alone; the editor warns before saving into a
// folder that holds files.
CYPHER_NODISCARD map_files_result_t MapFiles_Save( map_document_t *pMap, const char *pRootPath ) noexcept;

// Editor history can restore a document from before its last save. This
// overload keeps persistence bookkeeping separate: only these previously
// managed relative chunk paths may be removed, and only when no staged
// output replaces them. An empty inventory describes a fresh Save As target.
// The optional initialized output is cleared and receives newline-separated
// relative paths only after each chunk replacement succeeds. It also reports
// partial publication on failure, so the caller can retain recovery ownership.
// Tracking storage is reserved before publication; failure cannot lose a path
// that was already replaced on disk.
CYPHER_NODISCARD map_files_result_t MapFiles_Save(
    map_document_t *pMap,
    const char *pRootPath,
    common::span_t<const common::string_view_t> persistedChunkPaths,
    common::text_buffer_t *pPublishedChunkPaths = nullptr ) noexcept;

CYPHER_NODISCARD const char *MapFiles_StatusName( map_files_status_t status ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_FILES_H
