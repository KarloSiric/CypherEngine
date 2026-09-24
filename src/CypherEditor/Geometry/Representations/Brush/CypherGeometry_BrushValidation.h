//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushValidation.h
//  Purpose: Declares quick and deep validation for plane-defined brushes.
//  Details: Quick validation inspects only the plane set — finiteness,
//           normalization, duplicates, minimum side count — without
//           reconstructing the boundary. It is cheap enough to run on
//           every edit preview.
//
//           Deep validation performs a full boundary reconstruction and
//           checks topological invariants: Euler relation v - e + f = 2,
//           every side contributing exactly one face, every edge shared
//           by exactly two faces, and every face normal matching its
//           side plane's outward direction. It is the closing check for
//           Gate 2's acceptance criterion.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_VALIDATION_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_VALIDATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushSolid.h"

namespace cypher::editor::geometry
{

// Detailed result from deep validation. The raw counts let the caller
// diagnose exactly what went wrong without repeating the reconstruction.
struct brush_validation_result_t {
    geometry_status_t status{ geometry_status_t::NOT_INITIALIZED };

    // Populated only when reconstruction succeeds (status is OK or a
    // topological failure). Zero when the brush fails before reconstruction.
    common::u32 cVertices{ 0u };
    common::u32 cEdges{ 0u };
    common::u32 cFaces{ 0u };

    // Euler characteristic for a closed convex polyhedron must be exactly 2.
    // A value other than 2 means the boundary is not watertight.
    common::i32 eulerCharacteristic{ 0 };

    // True only when every topological invariant holds.
    common::bool_t bWatertight{ false };
};

// ---------------------------------------------------------------------------
// Quick validation
// ---------------------------------------------------------------------------

// Inspects the plane set without reconstructing the boundary. Checks:
//   - brush is initialized and has at least 4 sides (tetrahedron minimum)
//   - every plane is finite
//   - every plane normal is unit-length within policy tolerance
//   - plane distance is within coordinate magnitude limit
//   - no two planes are duplicates (identical normal and distance within
//     tolerance) or contradictory (opposite normals, same distance)
//   - side count does not exceed limits.cBrushSidesPerBrushMax
//   - the brush and every side carry valid, pairwise-distinct source IDs
//     (IDENTITY_CONFLICT otherwise; reported after all plane checks)
//
// Returns OK if the plane set is plausible, or the first failure found.
// A plausible plane set can still fail deep validation if the planes do
// not define a bounded volume.
CYPHER_NODISCARD geometry_status_t BrushValidation_Quick(
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy ) noexcept;

// ---------------------------------------------------------------------------
// Deep validation
// ---------------------------------------------------------------------------

// Performs a full boundary reconstruction and checks topological invariants.
// This is the authoritative validation: a brush that passes deep validation
// is guaranteed to be a closed, watertight, outward-oriented convex solid.
//
// Checks (in addition to everything quick validation covers):
//   - boundary reconstruction succeeds
//   - Euler relation: vertices - edges + faces == 2
//   - every brush side contributes exactly one boundary face
//   - every boundary face has at least 3 vertices
//   - every edge is shared by exactly two faces
//   - every face's Newell normal agrees with its side plane's outward
//     direction and encloses at least fMinimumFaceArea
//   - every edge is at least fMinimumEdgeLength long
//
// The boundary used for validation is allocated internally and released
// before returning. If the caller needs the boundary for further use,
// they should reconstruct it separately.
CYPHER_NODISCARD brush_validation_result_t BrushValidation_Deep(
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy,
    const common::allocator_t *pAllocator ) noexcept;

// Runs the topological half of deep validation against a boundary the
// caller already reconstructed from pBrush with the same policy. Callers
// that keep the boundary (immutable brush values cache it) use this to
// avoid reconstructing twice. Quick validation is NOT repeated here; the
// caller must have run it. An empty boundary reports DEGENERATE.
CYPHER_NODISCARD brush_validation_result_t BrushValidation_CheckBoundary(
    const brush_solid_t *pBrush,
    const brush_boundary_t *pBoundary,
    const geometry_policy_t &policy ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_VALIDATION_H
