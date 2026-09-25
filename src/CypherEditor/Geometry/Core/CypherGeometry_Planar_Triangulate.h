//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Triangulate.h
//  Purpose: Declares triangulation of PlanarRegion polygons with holes.
//  Details: Two stages, both driven only by exact Orient2D decisions:
//
//             1. Hole bridging — each hole is joined to the outer ring by
//                a zero-width channel to a mutually visible vertex, turning
//                a polygon with h holes into one weakly simple ring
//                (Eberly, "Triangulation by Ear Clipping", 2002 describes
//                the idea; the visibility test here is an exact
//                segment-crossing + cone test rather than his ray cast, so
//                no constructed point ever decides topology).
//             2. Ear clipping on that ring.
//
//           A polygon with n total corners and h bridged holes yields
//           n + 2h - 2 triangles. A hole that already touches the boundary
//           is spliced at the contact instead of bridged, which saves one
//           duplicate (contact mid-edge) or two (contact at a vertex), and
//           one triangle per saved duplicate. Every triangle is strictly
//           CCW (positive area) in frame space and indexes the region's own
//           point array, so Tessellation/Cook can map it back to its polygon.
//
//           Cost: O(n^2) typical, O(n^3) worst case for pathological
//           reflex-heavy input, hence the corner bound below.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PLANAR_TRIANGULATE_H
#define CYPHER_EDITOR_GEOMETRY_PLANAR_TRIANGULATE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_PlanarRegion.h"

namespace cypher::editor::geometry
{

// Upper bound on corners (outer + all holes) of a single polygon. Keeps
// the worst-case cubic ear search bounded for interactive use.
inline constexpr common::usize kPlanarTriangulateCornersMax = 4096u;

// One output triangle. Indices refer to planar_region_t::points.
struct planar_triangle_t {
    common::u32 a{ 0u };
    common::u32 b{ 0u };
    common::u32 c{ 0u };
    common::u32 iPolygon{ 0u };
};

// Triangulates one polygon and appends its triangles to pTrianglesOut,
// which must be initialized. The polygon should be valid per
// PlanarRegion_Validate; invalid input yields DEGENERATE rather than a
// wrong triangulation. On any failure pTrianglesOut is unchanged.
//
// Returns NOT_INITIALIZED, INVALID_ARGUMENT (bad index / output),
// LIMIT_EXCEEDED (corner bound), ALLOCATION_FAILED, or DEGENERATE (no
// valid bridge or ear exists).
CYPHER_NODISCARD geometry_status_t Planar_TryTriangulatePolygon(
    const planar_region_t *pRegion,
    common::usize iPolygon,
    common::vector_t<planar_triangle_t> *pTrianglesOut ) noexcept;

// One triangle of a ring triangulation: indices into the input ring.
struct planar_ring_triangle_t {
    common::u32 a{ 0u };
    common::u32 b{ 0u };
    common::u32 c{ 0u };
};

// Ear-clips a single simple ring (no holes) given as 2D points. The ring
// may wind either way; triangles are emitted CCW relative to the ring's
// own winding (i.e. same orientation as the ring). Collinear corners are
// allowed. Appends n - 2 triangles to pOut (initialized) or leaves it
// unchanged and returns DEGENERATE / LIMIT_EXCEEDED / ALLOCATION_FAILED.
// Used for mesh face tessellation and validation, where faces are simple
// rings projected into their own plane.
CYPHER_NODISCARD geometry_status_t Planar_TryTriangulateRing(
    common::span_t<const math::vec2d_t> ring,
    const common::allocator_t *pScratchAllocator,
    common::vector_t<planar_ring_triangle_t> *pOut ) noexcept;

// Triangulates every polygon in order. Failure-atomic across the whole
// region: either every polygon's triangles are appended or none are.
CYPHER_NODISCARD geometry_status_t Planar_TryTriangulateRegion(
    const planar_region_t *pRegion,
    common::vector_t<planar_triangle_t> *pTrianglesOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PLANAR_TRIANGULATE_H
