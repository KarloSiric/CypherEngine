//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherTileGeometryAdapter.cpp
//  Purpose: Implements tile map box → brush solid conversion.
//  Details: Each tile_map_geometry_box_t is an axis-aligned box defined by
//           center + half-extents in f32. The adapter promotes to f64 and
//           uses BrushGenerator_TryMakeBox to create a canonical 6-plane
//           brush, then adds it to the caller's geometry document. Source
//           IDs are drawn from the caller's allocator so they integrate
//           into the document's identity domain.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherTileGeometryAdapter.h"
#include "CypherGeometry_BrushGenerator.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

tile_adapter_result_t GeometryTileMapAdapter_ConvertBoxes(
    const tools::tile_editor::tile_map_geometry_t *pTileGeometry,
    geometry_source_id_allocator_t *pIdAllocator,
    const geometry_policy_t &policy,
    geometry_document_t *pDocumentOut ) noexcept
{
    tile_adapter_result_t result{};

    if ( pTileGeometry == nullptr || pIdAllocator == nullptr ||
         pDocumentOut == nullptr ) {
        result.status = tile_adapter_status_t::INVALID_ARGUMENT;
        return result;
    }
    if ( !GeometryDocument_IsInitialized( pDocumentOut ) ) {
        result.status = tile_adapter_status_t::NOT_INITIALIZED;
        return result;
    }

    const allocator_t *pAllocator = pDocumentOut->pAllocator;
    const usize cBoxes = pTileGeometry->boxes.nCount;

    for ( usize i = 0u; i < cBoxes; ++i ) {
        const auto &box = pTileGeometry->boxes.pData[i];

        // Promote f32 tile coordinates to f64 brush coordinates.
        const math::vec3d_t center = math::Vec3d_Make(
            static_cast<f64>( box.centerX ),
            static_cast<f64>( box.centerY ),
            static_cast<f64>( box.centerZ ) );
        const math::vec3d_t halfExtents = math::Vec3d_Make(
            static_cast<f64>( box.halfExtentX ),
            static_cast<f64>( box.halfExtentY ),
            static_cast<f64>( box.halfExtentZ ) );

        brush_solid_t brush{};
        const geometry_status_t genStatus =
            BrushGenerator_TryMakeBox(
                &brush, pAllocator, policy, pIdAllocator,
                center, halfExtents );
        if ( genStatus != geometry_status_t::OK ) {
            result.status =
                tile_adapter_status_t::BRUSH_GENERATION_FAILED;
            return result;
        }

        const geometry_status_t addStatus =
            GeometryDocument_TryAddBrush( pDocumentOut, &brush );
        BrushSolid_Shutdown( &brush );

        if ( addStatus != geometry_status_t::OK ) {
            result.status =
                tile_adapter_status_t::DOCUMENT_ADD_FAILED;
            return result;
        }

        ++result.cBrushesAdded;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

const char *GeometryTileMapAdapter_StatusName(
    tile_adapter_status_t status ) noexcept
{
    switch ( status ) {
        case tile_adapter_status_t::OK:
            return "OK";
        case tile_adapter_status_t::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case tile_adapter_status_t::NOT_INITIALIZED:
            return "NOT_INITIALIZED";
        case tile_adapter_status_t::OUT_OF_MEMORY:
            return "OUT_OF_MEMORY";
        case tile_adapter_status_t::BRUSH_GENERATION_FAILED:
            return "BRUSH_GENERATION_FAILED";
        case tile_adapter_status_t::DOCUMENT_ADD_FAILED:
            return "DOCUMENT_ADD_FAILED";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor::geometry
