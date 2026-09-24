//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushGenerator.h
//  Purpose: Declares pure primitive generators that produce brush solids.
//  Details: Generators are stateless free functions. They allocate
//           persistent source IDs, populate a brush_solid_t, and return.
//           The caller owns the resulting brush and its attribute store.
//
//           Generators do not depend on Document, Transactions, or any
//           host-level concept. They are consumed by creation commands
//           in Mason and CypherTileEditor, not called by the user directly.
//
//           Additional generators (wedge, prism, stair, arch) will be
//           added in Gate 6 alongside brush CSG. The box is the only
//           generator needed for Gate 2's closing criterion.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_GENERATOR_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_GENERATOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_IdAllocator.h"

namespace cypher::editor::geometry
{

// Constructs a six-plane axis-aligned box brush centered at `center` with
// the given `halfExtents` along each world axis. The brush and all six
// sides receive fresh source IDs from `pIdAllocator`.
//
// The resulting brush is fully formed but has no attribute store — the
// caller must create a brush_side_attribute_store_t and populate it with
// six default records whose indices match the sides (0 through 5). This
// separation keeps the generator free of attribute-store ownership.
//
// Side ordering is deterministic and axis-aligned:
//   index 0: +X  (normal  1, 0, 0)
//   index 1: -X  (normal -1, 0, 0)
//   index 2: +Y  (normal  0, 1, 0)
//   index 3: -Y  (normal  0,-1, 0)
//   index 4: +Z  (normal  0, 0, 1)
//   index 5: -Z  (normal  0, 0,-1)
//
// Validates:
//   - center is finite and within coordinate magnitude limit
//   - halfExtents are finite and strictly positive
//   - the resulting box corners stay within coordinate magnitude limit
//   - source ID allocator has at least 7 IDs remaining (1 brush + 6 sides)
//
// On failure the brush is left uninitialized and the allocator is not
// advanced, so the caller can retry or report without cleanup. IDs are
// drawn from a staged copy of the allocator and committed only after the
// brush is fully built.
CYPHER_NODISCARD geometry_status_t BrushGenerator_TryMakeBox(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    math::vec3d_t center,
    math::vec3d_t halfExtents ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_GENERATOR_H
