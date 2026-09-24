//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushPatches.h
//  Purpose: Declares "create patches from brush faces": turning a brush face
//           into flat Bezier patches that cover it exactly, as the starting
//           point for curved detail.
//  Details: A quadrilateral face becomes one patch (its control grid is the
//           bilinear net of the four corners, which a Bezier patch
//           reproduces exactly). Any other convex face is cut into quads
//           around its centroid - corner, the two adjacent edge midpoints,
//           and the centroid - one patch per corner, so the patches tile the
//           face exactly with shared edges. Each patch faces the same way as
//           the face, takes its material, and gets control UVs from the
//           face's own projection, so the texture looks the same.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_PATCHES_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_PATCHES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSource.h"
#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_Patch.h"

namespace cypher::editor::geometry
{

// Builds the patches for side iSide of an authored brush into pOut
// (canonical-empty patches): 1 for a quad face, one per corner otherwise.
// *pCountOut receives the count (or the needed count with
// INSUFFICIENT_CAPACITY). A material that does not fit the patch's 32-bit
// material field is LIMIT_EXCEEDED. Failure-atomic.
CYPHER_NODISCARD geometry_status_t BrushPatches_TryFromFace(
    const brush_source_t *pSource,
    common::u32 iSide,
    patch_basis_t basis,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    patch_surface_t *pOut,
    common::u32 cCapacity,
    common::u32 *pCountOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_PATCHES_H
