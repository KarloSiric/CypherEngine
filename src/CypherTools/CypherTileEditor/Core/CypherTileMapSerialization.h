//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileMapSerialization.h
//  Purpose: Declares deterministic CYKV persistence for authored tile maps.
//  Details: The source format is editor-facing text. Loading validates the
//           complete document before publishing a fresh tile-map document.
//
//  History:
//  - Created by Karlo Siric on 2026-09-16
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_TOOLS_TILEEDITOR_TILEMAPSERIALIZATION_H
#define CYPHER_TOOLS_TILEEDITOR_TILEMAPSERIALIZATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherTileMapDocument.h"

#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueWriter.h"
#include "CypherCommon/Tier1/CypherCommon_TextBuffer.h"

namespace cypher::tools::tile_editor
{

using namespace cypher::common;

inline constexpr const char *TILE_MAP_SCHEMA_ID = "cypher.map";
inline constexpr u32 TILE_MAP_SCHEMA_VERSION = 3u;
// Version 3 optionally adds materials: [{ slot: uint, path: canonical .cymat }].
// Version 1 and 2 retain their original root fields and remain readable.
// Version 2 cells optionally append shape (flat / stairs_north / stairs_east /
// stairs_south / stairs_west) and stair_steps (2..32). Omitted fields default to
// flat and 8. Version 1 remains readable with its original strict field set.

// Authored maps use the shared TILE_MAP_MAX_ACTIVE_CELLS document limit. The
// editor stores a sparse source representation; larger runtime worlds belong
// in a future partitioning/cooking stage.
inline constexpr usize TILE_MAP_SERIALIZATION_MAX_MARKERS = TILE_MAP_MAX_MARKERS;
inline constexpr usize TILE_MAP_SERIALIZATION_MAX_TEXT_BYTES = 64u * CY_MIB;
inline constexpr usize TILE_MAP_SERIALIZATION_FIELD_CAPACITY = 96u;

enum class tile_map_serialization_status_t : u8 {
    OK = 0u,

    INVALID_ARGUMENT,
    DESTINATION_NOT_EMPTY,
    NOT_INITIALIZED,
    INVALID_DOCUMENT,
    LIMIT_EXCEEDED,
    OUT_OF_MEMORY,

    CYKV_WRITE_FAILED,
    CYKV_PARSE_FAILED,
    HEADER_MISMATCH,
    ROOT_TYPE_MISMATCH,

    MISSING_FIELD,
    UNKNOWN_FIELD,
    TYPE_MISMATCH,
    VALUE_OUT_OF_RANGE,
    INVALID_ID,
    DUPLICATE_CELL,
    DUPLICATE_MARKER_ID,
    UNSUPPORTED_MARKER_KIND,

    DOCUMENT_INIT_FAILED,
    INVALID_MATERIAL_PATH,
    DUPLICATE_MATERIAL_SLOT
};

// One result carries both the stable map-format failure and lower-level detail.
// `field` is a bounded logical path such as "cells[].x". `iElement` identifies
// an array element when applicable and remains CY_INVALID_SIZE otherwise.
struct tile_map_serialization_result_t {
    tile_map_serialization_status_t status{
        tile_map_serialization_status_t::OK
    };

    key_value_parse_status_t parseStatus{
        key_value_parse_status_t::OK
    };
    key_value_write_status_t writeStatus{
        key_value_write_status_t::OK
    };
    tile_map_document_status_t documentStatus{
        tile_map_document_status_t::OK
    };

    text_location_t location{};
    char field[TILE_MAP_SERIALIZATION_FIELD_CAPACITY]{};
    usize iElement{ CY_INVALID_SIZE };
    usize cchText{ 0u };
};

// Replaces pTextOut only after a complete successful encode. The destination
// must be an initialized TextBuffer with an allocator binding.
CYPHER_NODISCARD
tile_map_serialization_result_t CypherTileMapSerialization_SaveToText(
    const tile_map_document_t *pDocument,
    text_buffer_t *pTextOut ) noexcept;

// Loads into a canonical fresh tile_map_document_t. Any failure leaves the
// destination fresh; callers never observe a partially decoded map.
CYPHER_NODISCARD
tile_map_serialization_result_t CypherTileMapSerialization_LoadFromText(
    string_view_t text,
    const allocator_t *pAllocator,
    tile_map_document_t *pDocumentOut ) noexcept;

CYPHER_NODISCARD
const char *CypherTileMapSerialization_StatusName(
    tile_map_serialization_status_t status ) noexcept;

} // namespace cypher::tools::tile_editor

#endif // CYPHER_TOOLS_TILEEDITOR_TILEMAPSERIALIZATION_H
