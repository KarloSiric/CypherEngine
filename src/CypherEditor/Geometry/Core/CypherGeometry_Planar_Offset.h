//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Planar_Offset.h
//  Purpose: Declares polyline stroking and region offsetting (inset and
//           outset) in a plane frame.
//  Details: Both operations are built from one primitive, the stroke: the
//           set of points within `halfWidth` of a polyline, approximated by
//
//             one rectangle per segment (the segment swept +-halfWidth
//             along its normal), plus
//             one join piece per interior corner on the side where the two
//             rectangles diverge: a mitre kite, a bevel triangle, or a
//             round fan of `roundSegments` wedges.
//
//           The pieces overlap, so they are merged with the exact-decision
//           Boolean overlay (balanced pairwise union, so the cost grows as
//           O(log n) rounds instead of n sequential unions). Then
//
//             outset(R, d) = R ∪ stroke(∂R, d)
//             inset(R, d)  = R \ stroke(∂R, d)
//
//           which handles holes, corners, and collapse (an inset larger
//           than the region's half-width yields an empty region, not an
//           inverted one) without any special cases, because regularized
//           Booleans cannot produce negative area.
//
//           Mitre joins fall back to bevel beyond fMiterLimit (ratio of
//           mitre length to halfWidth), the conventional guard against
//           spikes at very sharp corners.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PLANAR_OFFSET_H
#define CYPHER_EDITOR_GEOMETRY_PLANAR_OFFSET_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Planar_Overlay.h"

namespace cypher::editor::geometry
{

enum class planar_join_t : common::u8 {
    MITER = 0u,
    BEVEL = 1u,
    ROUND = 2u
};

struct planar_offset_options_t {
    planar_join_t join{ planar_join_t::MITER };
    common::f64 fMiterLimit{ 4.0 };   // mitre length / halfWidth before bevel fallback
    common::u32 roundSegments{ 8u };  // wedges per 90 degrees for ROUND joins
    bool bRoundCaps{ false };         // open polylines: round (true) or butt (false) ends
};

// Bound on stroke pieces (segments + joins) per call.
inline constexpr common::usize kPlanarOffsetPiecesMax = 8192u;

// Strokes a polyline (frame coordinates) into pOut (zero-initialized;
// initialized here with `frame`). bClosed joins the last point to the
// first. halfWidth > policy.fMinimumEdgeLength. Consecutive duplicate
// points are ignored. New source IDs come from pIdAllocator; on failure
// neither pOut nor the allocator changes.
CYPHER_NODISCARD geometry_status_t Planar_TryStrokePolyline(
    const planar_frame_t &frame,
    common::span_t<const math::vec2d_t> points,
    bool bClosed,
    common::f64 halfWidth,
    const planar_offset_options_t &options,
    const geometry_policy_t &policy,
    geometry_source_id_t resultRegionId,
    geometry_source_id_allocator_t *pIdAllocator,
    const common::allocator_t *pAllocator,
    planar_region_t *pOut ) noexcept;

// Offsets every contour of pRegion by `distance` (> 0 outset, < 0 inset,
// |distance| > policy.fMinimumEdgeLength) into pOut (zero-initialized).
// pRegion must be valid. Same failure contract as TryStroke.
CYPHER_NODISCARD geometry_status_t Planar_TryOffsetRegion(
    const planar_region_t *pRegion,
    common::f64 distance,
    const planar_offset_options_t &options,
    const geometry_policy_t &policy,
    geometry_source_id_t resultRegionId,
    geometry_source_id_allocator_t *pIdAllocator,
    const common::allocator_t *pAllocator,
    planar_region_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PLANAR_OFFSET_H
