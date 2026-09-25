//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PatchTessellation.h
//  Purpose: Declares deterministic, crack-free triangulation of a Patch
//           with per-vertex and per-triangle provenance.
//  Details: Every sub-patch column shares one subdivision count, and every
//           sub-patch row shares one, so the whole patch tessellates to a
//           single regular vertex grid. That is what makes the result
//           watertight inside the patch: neighbouring sub-patches always
//           sample their shared boundary at identical parameters.
//
//           Adaptive levels come from an a-priori bound, not sampling.
//           For a C2 surface split into a ku x kv parameter grid, the
//           piecewise-linear interpolant deviates by at most
//             (1/8) (Muu/ku^2 + 2 Muv/(ku kv) + Mvv/kv^2)
//           (Filip, Magedson, Markot 1986), where the M terms bound the
//           second partials. For Bézier surfaces the M terms are bounded by
//           scaled second differences of the control net, so the level is
//           a pure function of the control points: deterministic, cheap,
//           and conservative.
//
//           Seams with *other* patches are not handled here: two patches
//           sharing an edge only match if they pick the same levels along
//           it. Use fixedSubdivisions when a guaranteed match is needed;
//           cross-patch stitching belongs to Cook.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PATCH_TESSELLATION_H
#define CYPHER_EDITOR_GEOMETRY_PATCH_TESSELLATION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Patch.h"
#include "CypherCommon_Vector.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kPatchTessellationVerticesMax = 1u << 20;
inline constexpr common::u32 kPatchTessellationSubdivisionsMax = 256u;

struct patch_tessellation_options_t {
    // > 0: every sub-patch uses exactly this many segments per axis and the
    // chord tolerance is ignored. Gives identical boundaries across patches
    // with the same sub-patch counts.
    common::u32 fixedSubdivisions{ 0u };
    // Adaptive target: maximum distance between the surface and its
    // triangulation, in world units. Must be > 0 when adaptive.
    common::f64 fMaxChordError{ 0.25 };
    // Per sub-patch, per axis cap for the adaptive search.
    common::u32 maxSubdivisions{ 64u };
    // Collapsed edges (cone apex, end caps) produce triangles with two
    // identical corners. Dropping them keeps downstream consumers from
    // seeing zero-area triangles; the count is reported either way.
    bool bDropCollapsedTriangles{ true };
};

struct patch_tessellation_t {
    // Vertex grid, row-major, cGridColumns x cGridRows.
    common::vector_t<math::vec3d_t> positions{};
    common::vector_t<math::vec3d_t> normals{};   // unit or zero (unresolved)
    common::vector_t<math::vec2d_t> uvs{};
    common::vector_t<math::vec2d_t> params{};    // global (u, v)
    // Grid index of the control point a vertex coincides with (sub-patch
    // corners interpolate their corner control), else CY_INVALID_INDEX.
    // Lets picking and snapping map a tessellated vertex back to an
    // authored control without a geometric search.
    common::vector_t<common::u32> vertexControl{};
    // Triangles (front face = dPdu x dPdv) and the row-major sub-patch
    // index each came from.
    common::vector_t<common::u32> indices{};
    common::vector_t<common::u32> triangleSubPatch{};
    // Chosen segment counts per sub-patch column / row.
    common::vector_t<common::u32> columnSubdivisions{};
    common::vector_t<common::u32> rowSubdivisions{};
    common::u32 cGridColumns{ 0u };
    common::u32 cGridRows{ 0u };
    // Triangles with two identical corners, whether dropped or kept.
    common::u32 cCollapsedTriangles{ 0u };
    common::u32 cNormalFallbacks{ 0u };   // normals taken from a nudged sample
    common::u32 cUnresolvedNormals{ 0u }; // normals left zero
    // False when the adaptive search hit maxSubdivisions before meeting
    // fMaxChordError on some sub-patch. The mesh is still produced; the
    // caller decides whether a coarser-than-requested result is acceptable.
    bool bToleranceMet{ true };
};

CYPHER_NODISCARD geometry_status_t PatchTessellation_Init(
    patch_tessellation_t *pOut,
    const common::allocator_t *pAllocator ) noexcept;

void PatchTessellation_Shutdown( patch_tessellation_t *pOut ) noexcept;

// Rebuilds *pOut from the patch. The patch must pass Patch_Validate's
// structural checks (dimensions, finite, range); IDs are not required.
// On failure *pOut is left logically empty (all array counts and metadata are
// cleared). Successfully grown backing capacities may remain for reuse.
CYPHER_NODISCARD geometry_status_t PatchTessellation_TryBuild(
    const patch_surface_t *pPatch,
    const patch_tessellation_options_t &options,
    patch_tessellation_t *pOut ) noexcept;

// The a-priori chord error bound for one sub-patch tessellated with ku x kv
// segments. Exposed so tools and tests can reason about the chosen levels.
// Returns +inf for invalid input.
CYPHER_NODISCARD common::f64 PatchTessellation_ErrorBound(
    const patch_surface_t *pPatch,
    common::u32 iSubU,
    common::u32 iSubV,
    common::u32 ku,
    common::u32 kv ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PATCH_TESSELLATION_H
