//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SurfaceSerialization.h
//  Purpose: Declares the schema-4 "patches" and "heightfields" sections of
//           the geometry document format.
//  Details: Like meshes, bulk data is written as flat per-channel arrays so
//           the CYKV node count stays close to the scalar count:
//
//             patches[]:      source_id, basis ("biquadratic_bezier" |
//                             "bicubic_bezier"), columns, rows, material,
//                             control_ids [u64], positions [f64 x3],
//                             uvs [f64 x2]          (row-major controls)
//             heightfields[]: source_id, origin [f64 x3], cell_size,
//                             cells_x, cells_y, tile_cells,
//                             tile_ids [u64] (row-major tiles),
//                             heights [f64]  (row-major samples),
//                             hole_cells [u64] (indices of hole cells)
//
//           Holes are stored sparsely (most terrain has few). Tile
//           revisions and dirty flags are runtime state and are not saved:
//           a loaded field starts with every tile dirty, which is what a
//           fresh cook needs anyway. Sections are written only when the
//           document has such objects.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SURFACE_SERIALIZATION_H
#define CYPHER_EDITOR_GEOMETRY_SURFACE_SERIALIZATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSerialization.h"

namespace cypher::editor::geometry
{

// Validates every document patch and heightfield (as saving requires) and
// appends all their IDs to *pIdsOut. CORRUPT_DOCUMENT names an invalid
// object or an ID the registry does not hold.
CYPHER_NODISCARD geometry_serialization_result_t SurfaceSerialization_TryCollectIds(
    const geometry_document_t *pDocument,
    const common::allocator_t *pScratchAllocator,
    common::vector_t<geometry_source_id_t> *pIdsOut ) noexcept;

// Writes the "patches" / "heightfields" sections under pRoot.
CYPHER_NODISCARD geometry_serialization_result_t SurfaceSerialization_TryWrite(
    common::key_value_document_t *pKvDocument,
    common::key_value_t *pRoot,
    const geometry_document_t *pDocument ) noexcept;

// Reads both sections (each optional) into the document. bRequireClaimed:
// every ID must already be claimed by the loaded identity state.
CYPHER_NODISCARD geometry_serialization_result_t SurfaceSerialization_TryRead(
    const common::key_value_t *pRoot,
    geometry_document_t *pDocument,
    bool bRequireClaimed ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SURFACE_SERIALIZATION_H
