//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_PlanarRegion.h
//  Purpose: Declares the PlanarRegion source representation: one or more
//           polygons with holes lying in a shared plane frame.
//  Details: Used for floor plans, cap faces, sweep profiles, and tile
//           collision regions. Storage is three flat arrays:
//
//             points   — every contour vertex, contour after contour;
//             contours — {first point, count, source ID};
//             polygons — {first contour, contour count, source ID}.
//
//           A polygon's contours are contiguous: the outer contour first,
//           then its holes. The builder enforces that by only allowing a
//           hole to be appended to the most recently added polygon, which
//           keeps every lookup O(1) without per-polygon hole arrays.
//
//           Winding convention (in frame 2D space): outer contours CCW,
//           holes CW. The builder does not reorient input — orientation is
//           authored data and a wrong orientation is a Validation finding,
//           not something to silently "fix".
//
//           Every mutation is failure-atomic: capacity is reserved before
//           the first push, so a failed call leaves the region unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-09-23
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PLANAR_REGION_H
#define CYPHER_EDITOR_GEOMETRY_PLANAR_REGION_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Planar_Frame.h"
#include "CypherCommon_Vector.h"
#include "CypherCommon_Span.h"

namespace cypher::editor::geometry
{

// Hard bounds. They are generous for authoring (a floor plan with a few
// hundred rooms) but keep the O(E^2) exact validation pass and the O(n^2)
// ear clipper within interactive time.
inline constexpr common::usize kPlanarRegionContourPointsMax = 16384u;
inline constexpr common::usize kPlanarRegionContoursMax = 4096u;
inline constexpr common::usize kPlanarRegionPointsMax = 262144u;

struct planar_region_contour_t {
    common::u32 iFirstPoint{ 0u };
    common::u32 cPoints{ 0u };
    geometry_source_id_t sourceId{};
};

struct planar_region_polygon_t {
    common::u32 iFirstContour{ 0u };  // outer contour
    common::u32 cContours{ 0u };      // 1 + hole count
    geometry_source_id_t sourceId{};
};

struct planar_region_t {
    planar_frame_t frame{};
    common::vector_t<math::vec2d_t> points{};
    common::vector_t<planar_region_contour_t> contours{};
    common::vector_t<planar_region_polygon_t> polygons{};
    geometry_source_id_t sourceId{};
};

// Point containment against a region or a single polygon.
enum class planar_containment_t : common::u8 {
    OUTSIDE  = 0u,
    INSIDE   = 1u,
    BOUNDARY = 2u
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

// Binds storage and fixes the frame and region identity. The frame must be
// valid (PlanarFrame_IsValid with fUnitTolerance 1e-9) and regionId must be
// a valid source ID. Returns ALREADY_INITIALIZED if called twice.
CYPHER_NODISCARD geometry_status_t PlanarRegion_Init(
    planar_region_t *pRegion,
    const common::allocator_t *pAllocator,
    const planar_frame_t &frame,
    geometry_source_id_t regionId ) noexcept;

// Releases storage. Safe on zero-initialized or already shut-down regions.
void PlanarRegion_Shutdown( planar_region_t *pRegion ) noexcept;

CYPHER_NODISCARD bool PlanarRegion_IsInitialized(
    const planar_region_t *pRegion ) noexcept;

// ---------------------------------------------------------------------------
// Building
// ---------------------------------------------------------------------------

// Appends a new polygon whose outer contour is `outer`. Rejects:
//   INVALID_ARGUMENT — invalid IDs or fewer than 3 points;
//   NUMERIC_FAILURE  — a non-finite coordinate;
//   LIMIT_EXCEEDED   — any kPlanarRegion* bound would be exceeded.
// On success writes the new polygon index to *pPolygonIndexOut if non-null.
CYPHER_NODISCARD geometry_status_t PlanarRegion_TryAddPolygon(
    planar_region_t *pRegion,
    geometry_source_id_t polygonId,
    geometry_source_id_t outerContourId,
    common::span_t<const math::vec2d_t> outer,
    common::u32 *pPolygonIndexOut ) noexcept;

// Appends a hole to the most recently added polygon (see file header for
// why only the last polygon). Same rejections as TryAddPolygon plus
// INVALID_ARGUMENT when the region has no polygon yet.
CYPHER_NODISCARD geometry_status_t PlanarRegion_TryAddHole(
    planar_region_t *pRegion,
    geometry_source_id_t holeContourId,
    common::span_t<const math::vec2d_t> hole ) noexcept;

// Removes every polygon; keeps frame, identity, and capacity.
void PlanarRegion_Clear( planar_region_t *pRegion ) noexcept;

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

CYPHER_NODISCARD common::usize PlanarRegion_PolygonCount(
    const planar_region_t *pRegion ) noexcept;
CYPHER_NODISCARD common::usize PlanarRegion_ContourCount(
    const planar_region_t *pRegion ) noexcept;
CYPHER_NODISCARD common::usize PlanarRegion_PointCount(
    const planar_region_t *pRegion ) noexcept;

// Points of one contour, or an empty span for an out-of-range index.
CYPHER_NODISCARD common::span_t<const math::vec2d_t> PlanarRegion_ContourPoints(
    const planar_region_t *pRegion,
    common::usize iContour ) noexcept;

// Signed area of one contour in frame units (positive = CCW). Uses the
// shoelace sum relative to the contour's first point to limit
// cancellation for contours far from the frame origin.
CYPHER_NODISCARD common::f64 PlanarRegion_ContourSignedArea(
    const planar_region_t *pRegion,
    common::usize iContour ) noexcept;

// Filled area of one polygon: |outer| - sum |holes|. Assumes a valid
// polygon (holes inside the outer, not overlapping).
CYPHER_NODISCARD common::f64 PlanarRegion_PolygonArea(
    const planar_region_t *pRegion,
    common::usize iPolygon ) noexcept;

// Sum of PolygonArea over all polygons.
CYPHER_NODISCARD common::f64 PlanarRegion_Area(
    const planar_region_t *pRegion ) noexcept;

// Exact containment of a 2D point in one contour, using exact Orient2D
// for every edge decision so boundary hits are never misreported.
CYPHER_NODISCARD planar_containment_t PlanarRegion_ContourContains(
    const planar_region_t *pRegion,
    common::usize iContour,
    math::vec2d_t point ) noexcept;

// Containment in a polygon's filled area (inside outer, outside holes).
CYPHER_NODISCARD planar_containment_t PlanarRegion_PolygonContains(
    const planar_region_t *pRegion,
    common::usize iPolygon,
    math::vec2d_t point ) noexcept;

// Containment in the union of all polygons.
CYPHER_NODISCARD planar_containment_t PlanarRegion_Contains(
    const planar_region_t *pRegion,
    math::vec2d_t point ) noexcept;

// Axis-aligned 2D bounds of all points. Returns false for an empty region.
CYPHER_NODISCARD bool PlanarRegion_TryBounds(
    const planar_region_t *pRegion,
    math::vec2d_t *pMinOut,
    math::vec2d_t *pMaxOut ) noexcept;

// Exact containment of a point in an arbitrary closed 2D ring (no region
// needed). Exposed because Planar algorithms test intermediate rings.
CYPHER_NODISCARD planar_containment_t Planar_RingContains(
    common::span_t<const math::vec2d_t> ring,
    math::vec2d_t point ) noexcept;

// Signed shoelace area of an arbitrary closed 2D ring (positive = CCW).
CYPHER_NODISCARD common::f64 Planar_RingSignedArea(
    common::span_t<const math::vec2d_t> ring ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PLANAR_REGION_H
