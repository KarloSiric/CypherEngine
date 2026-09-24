//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PlanarExtrude.h
//  Purpose: Declares polygon extrusion: a PlanarRegion (polygons with
//           holes) swept along its frame normal into a closed EditableMesh.
//  Details: The floor-plan workflow: draw a region, extrude it into slabs,
//           walls, or columns. Each region polygon becomes one closed shell:
//
//             bottom cap  — the polygon, reversed so it faces -normal;
//             top cap     — the polygon offset by `height`, facing +normal;
//             sides       — one quad per contour edge, outward.
//
//           Caps with holes cannot be single faces (an editable-mesh face
//           owns one outer loop in the current representation), so caps
//           are triangulated with the Planar hole-aware triangulator; side
//           walls stay quads. Collinear boundary corners are kept, so the
//           walls stay aligned with authored vertices.
//
//           Provenance: pFaceSourceOut (optional) receives, per output face
//           in soup order, the source ID of the contour that produced a
//           side wall, or the polygon's ID for cap triangles.
//
//           Negative heights extrude along -normal; the result is still
//           outward-oriented.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PLANAR_EXTRUDE_H
#define CYPHER_EDITOR_GEOMETRY_PLANAR_EXTRUDE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_PlanarRegion.h"
#include "CypherGeometry_EditableMesh.h"
#include "CypherGeometry_Policy.h"

namespace cypher::editor::geometry
{

// Extrudes pRegion by `height` along its frame normal into pMeshOut (must
// be zero-initialized). The region must pass PlanarRegion_Validate;
// |height| must exceed policy.numerical.fMinimumEdgeLength.
//
// Returns INVALID_ARGUMENT, NOT_INITIALIZED, the region's validation status
// if it is invalid, DEGENERATE for an empty region, or a Sanitation /
// allocation failure. On failure pMeshOut stays uninitialized.
CYPHER_NODISCARD geometry_status_t PlanarExtrude_TryExtrude(
    const planar_region_t *pRegion,
    common::f64 height,
    const geometry_policy_t &policy,
    const common::allocator_t *pAllocator,
    editable_mesh_t *pMeshOut,
    common::vector_t<geometry_source_id_t> *pFaceSourceOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PLANAR_EXTRUDE_H
