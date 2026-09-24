//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileGeometryAdapter.h
//  Purpose: Declares the bridge from tile map boxes to brush geometry.
//  Details: Converts tile_map_geometry_box_t records into brush_solid_t
//           brushes and populates a geometry_document_t so that tile-derived
//           brushes can render, pick, preview, commit, undo, and rebuild
//           through the standard geometry pipeline.
//
//           Coordinate promotion is f32 → f64. Axis-aligned box normals
//           match the brush convention: outward-facing, interior is
//           nonpositive half-space.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_TILE_MAP_ADAPTER_H
#define CYPHER_EDITOR_GEOMETRY_TILE_MAP_ADAPTER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Document.h"
#include "CypherGeometry_IdAllocator.h"

#include "CypherTileMapGeometry.h"

namespace cypher::editor::geometry
{

// Status codes specific to tile-to-brush conversion.
enum class tile_adapter_status_t : common::u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    NOT_INITIALIZED,
    OUT_OF_MEMORY,
    BRUSH_GENERATION_FAILED,
    DOCUMENT_ADD_FAILED
};

struct tile_adapter_result_t {
    tile_adapter_status_t status{ tile_adapter_status_t::OK };

    // Number of boxes successfully converted into brushes.
    common::usize cBrushesAdded{ 0u };
};

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

// Converts every box in pTileGeometry into a 6-plane brush and appends it
// to pDocumentOut. The document must already be initialized. Source IDs
// are allocated from pIdAllocator.
//
// On failure, any brushes already added remain in the document — the
// caller can inspect cBrushesAdded to see how far it got.
CYPHER_NODISCARD tile_adapter_result_t
GeometryTileMapAdapter_ConvertBoxes(
    const tools::tile_editor::tile_map_geometry_t *pTileGeometry,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    geometry_document_t *pDocumentOut ) noexcept;

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

CYPHER_NODISCARD const char *GeometryTileMapAdapter_StatusName(
    tile_adapter_status_t status ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_TILE_MAP_ADAPTER_H
