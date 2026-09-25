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
    // post-reconstruction invariant failure). Zero when the brush fails
    // before reconstruction.
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
//   - brush storage and allocator binding are internally consistent
//   - policy is valid and the side count is within its per-brush limit
//   - brush and side source IDs are valid and unique within the brush
//   - brush has at least 4 sides (tetrahedron minimum)
//   - every plane is finite
//   - every plane normal is unit-length within policy tolerance
//   - plane distance is within coordinate magnitude limit
//   - no two planes are duplicates (identical normal and distance within
//     tolerance) or contradictory (opposite normals, same distance)
//
// Returns OK if the canonical brush state and plane set are plausible, or the
// first failure in the order above. A canonical empty brush is
// NOT_INITIALIZED; malformed live storage is CORRUPT_STATE; reused source IDs
// are IDENTITY_CONFLICT. Quick validation never allocates.
// A plausible plane set can still fail deep validation if the planes do
// not define a bounded volume or if a plane is geometrically redundant.
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
//   - validation allocator is non-null and valid
//   - boundary reconstruction succeeds
//   - Euler relation: vertices - edges + faces == 2
//   - every brush side contributes exactly one boundary face
//   - packed face ranges and vertex indices are in bounds and exhaustive
//   - every boundary face has at least 3 distinct vertices
//   - vertex positions are finite, bounded, unique, and referenced
//   - canonical edge records are unique and match every face segment
//   - every edge is shared by two oppositely wound faces
//   - every face normal agrees with its side plane's outward direction
//
// The boundary used for validation is allocated internally and released
// before returning. If the caller needs the boundary for further use,
// they should reconstruct it separately.
//
// On any failure before successful reconstruction, all count/characteristic
// fields remain zero and bWatertight remains false. After reconstruction,
// counts remain available for a topological diagnostic. Every allocation is
// released before return, including allocation-failure paths.
CYPHER_NODISCARD brush_validation_result_t BrushValidation_Deep(
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy,
    const common::allocator_t *pAllocator ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_VALIDATION_H
