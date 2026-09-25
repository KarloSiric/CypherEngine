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
//           Gate 6 adds wedge, prism, stair, and arch generators that
//           compose plane construction and clipping paths. The box was
//           the only generator needed for Gate 2's closing criterion.
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
// Every single-brush generator requires a canonical empty destination. Every
// multi-brush generator requires every destination slot to be canonical empty.
// Construction happens privately: on any failure all destination bytes and
// the source-ID allocator remain unchanged. On success ownership is published
// without another allocation.
CYPHER_NODISCARD geometry_status_t BrushGenerator_TryMakeBox(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    math::vec3d_t center,
    math::vec3d_t halfExtents ) noexcept;

// ---------------------------------------------------------------------------
// Wedge (Gate 6)
// ---------------------------------------------------------------------------

// Constructs a five-plane wedge brush: a box with one diagonal cut that
// removes a triangular prism. The cut goes along `cutAxis` (0=X, 1=Y,
// 2=Z), slicing from the positive face of `slopeAxis` (0=X, 1=Y, 2=Z)
// down to the negative face. The remaining axis is the extrusion axis.
//
// cutAxis and slopeAxis must be different. The extrusion axis is the
// third axis not named by either.
//
// Example: cutAxis=0 (X), slopeAxis=2 (Z) produces a wedge whose slope
// runs along X and whose triangular cross-section lies in the XZ plane,
// extruded along Y.
//
// Side ordering: slope-axis negative face, positive and negative extrusion
// end caps, cut-axis negative face, then the diagonal slope face. Requires
// six source IDs total (one brush and five sides).
CYPHER_NODISCARD geometry_status_t BrushGenerator_TryMakeWedge(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    math::vec3d_t center,
    math::vec3d_t halfExtents,
    common::u32 cutAxis,
    common::u32 slopeAxis ) noexcept;

// ---------------------------------------------------------------------------
// Prism (Gate 6)
// ---------------------------------------------------------------------------

// Constructs an (nSides + 2)-plane regular prism: a regular N-gon cross-
// section extruded along the given axis (0=X, 1=Y, 2=Z).
//
// Parameters:
//   center     — centroid of the prism
//   radius     — circumradius of the N-gon cross-section (strictly positive)
//   halfHeight — half the extrusion length along the axis (strictly positive)
//   nSides     — number of lateral sides (>= 3 and bounded by policy)
//   axis       — extrusion axis: 0=X, 1=Y, 2=Z
//
// Side ordering: indices [0..nSides-1] are the lateral faces, index
// nSides is the +axis cap, index nSides+1 is the -axis cap.
// Requires nSides + 3 source IDs total (one brush and nSides + 2 sides).
CYPHER_NODISCARD geometry_status_t BrushGenerator_TryMakePrism(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    math::vec3d_t center,
    common::f64 radius,
    common::f64 halfHeight,
    common::u32 nSides,
    common::u32 axis ) noexcept;

// ---------------------------------------------------------------------------
// Staircase (Gate 6)
// ---------------------------------------------------------------------------

// Constructs a staircase of nSteps axis-aligned box brushes. Steps ascend
// from `origin` in the +X direction, with each step rising in +Z.
//
// Parameters:
//   pBrushes   — canonical-empty output array with at least nSteps slots
//   nSteps     — number of steps (>= 1 and bounded by policy)
//   stepWidth  — X extent of each step (strictly positive)
//   stepHeight — Z rise per step (strictly positive)
//   stepDepth  — Y depth of each step (strictly positive)
//   origin     — position of the bottom-left-front corner of the first step
//
// Each output brush is a fully formed box. On failure, the entire output array
// and source-ID allocator are exactly unchanged.
CYPHER_NODISCARD geometry_status_t BrushGenerator_TryMakeStaircase(
    brush_solid_t *pBrushes,
    common::usize nSteps,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    math::vec3d_t origin,
    common::f64 stepWidth,
    common::f64 stepHeight,
    common::f64 stepDepth ) noexcept;

// ---------------------------------------------------------------------------
// Arch (Gate 6)
// ---------------------------------------------------------------------------

// Constructs an arch of nSegments trapezoidal prism brushes arranged in
// an arc. The arch lies in the XZ plane, centered at `center`, arcing
// from angle 0 to `arcAngleRadians` (counter-clockwise from +X when
// viewed from +Y).
//
// Parameters:
//   pBrushes        — canonical-empty output array with nSegments slots
//   nSegments       — number of arc segments (>= 1 and bounded by policy)
//   outerRadius     — outer radius of the arch (strictly positive)
//   innerRadius     — inner radius of the arch (strictly positive, < outer)
//   thickness       — Y-axis depth of the arch (strictly positive)
//   arcAngleRadians — total arc sweep in radians (0 < angle <= 2π)
//
// Each segment is a 6-plane brush whose inner and outer chord faces follow the
// arc. A segment sweep must be less than pi so every segment remains a bounded
// convex brush. On failure, the entire output array and source-ID allocator are
// exactly unchanged.
CYPHER_NODISCARD geometry_status_t BrushGenerator_TryMakeArch(
    brush_solid_t *pBrushes,
    common::usize nSegments,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    math::vec3d_t center,
    common::f64 outerRadius,
    common::f64 innerRadius,
    common::f64 thickness,
    common::f64 arcAngleRadians ) noexcept;

// ---------------------------------------------------------------------------
// Cylinder (Gate 8)
// ---------------------------------------------------------------------------

// Constructs a cylinder brush: a regular prism with `nSides` lateral faces
// extruded along the given axis. This is a convenience wrapper around the
// prism generator — geometrically identical, but named for discoverability
// and to match the Hammer/TrenchBroom cylinder primitive.
//
// Parameters:
//   center     — centroid of the cylinder
//   radius     — circumradius of the cross-section polygon (strictly positive)
//   halfHeight — half the extrusion length along the axis (strictly positive)
//   nSides     — number of lateral faces (>= 3 and bounded by policy)
//   axis       — extrusion axis: 0=X, 1=Y, 2=Z
//
// Side ordering: same as prism — indices [0..nSides-1] lateral, nSides +cap,
// nSides+1 -cap. Requires nSides + 3 source IDs total.
CYPHER_NODISCARD geometry_status_t BrushGenerator_TryMakeCylinder(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    math::vec3d_t center,
    common::f64 radius,
    common::f64 halfHeight,
    common::u32 nSides,
    common::u32 axis ) noexcept;

// ---------------------------------------------------------------------------
// Cone / Frustum (Gate 8)
// ---------------------------------------------------------------------------

// Constructs a cone or frustum brush: a solid with `nSides` tilted lateral
// faces connecting a bottom polygon (radius `bottomRadius`) to a top polygon
// (radius `topRadius`), extruded along the given axis.
//
// Setting topRadius to 0.0 creates a true cone (apex at the top cap center).
// Both radii equal produces a cylinder (equivalent to the prism generator).
//
// Parameters:
//   center       — centroid of the frustum
//   bottomRadius — circumradius of the bottom polygon (strictly positive)
//   topRadius    — circumradius of the top polygon (>= 0.0; 0.0 = true cone)
//   halfHeight   — half the extrusion length along the axis (strictly positive)
//   nSides       — number of lateral faces (>= 3 and bounded by policy)
//   axis         — extrusion axis: 0=X, 1=Y, 2=Z
//
// The bottom polygon is at center - halfHeight along the axis, the top at
// center + halfHeight. Lateral faces tilt inward from bottom to top when
// bottomRadius > topRadius (standard cone orientation).
//
// Side ordering: indices [0..nSides-1] are the tilted lateral faces. A
// frustum then stores the +axis cap at nSides and the -axis cap at nSides+1.
// A true cone omits the zero-area +axis cap and stores only its -axis cap at
// nSides. It therefore requires nSides+2 IDs total (brush, lateral sides,
// bottom); a frustum requires nSides+3 IDs total.
CYPHER_NODISCARD geometry_status_t BrushGenerator_TryMakeCone(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    math::vec3d_t center,
    common::f64 bottomRadius,
    common::f64 topRadius,
    common::f64 halfHeight,
    common::u32 nSides,
    common::u32 axis ) noexcept;

// ---------------------------------------------------------------------------
// Sphere (Gate 11)
// ---------------------------------------------------------------------------

// Constructs an icosphere brush: a convex polyhedron approximating a
// sphere by subdividing an icosahedron. Each subdivision level quadruples
// the face count and projects new vertices onto the sphere surface.
//
// Parameters:
//   center       — center of the sphere
//   radius       — radius (strictly positive)
//   nSubdivisions — number of subdivision levels (0 = icosahedron with 20
//                   faces / 12 vertices; 1 = 80 faces / 42 vertices).
//                   Each level quadruples the side count. Requests exceeding
//                   the active brush/vertex limits return LIMIT_EXCEEDED; the
//                   requested level is never silently clamped.
//
// Requires one brush source ID plus one source ID per resulting plane.
CYPHER_NODISCARD geometry_status_t BrushGenerator_TryMakeSphere(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    math::vec3d_t center,
    common::f64 radius,
    common::u32 nSubdivisions ) noexcept;

// ---------------------------------------------------------------------------
// Tetrahedron (Gate 15)
// ---------------------------------------------------------------------------

// Generates a regular tetrahedron brush centered at `center` with the
// specified `radius` (distance from center to each vertex).
//
// A tetrahedron is the simplest valid brush solid (4 faces). Unlike
// other generators, this one is truly minimal — exactly 4 sides with
// no parallel face pairs.
CYPHER_NODISCARD geometry_status_t BrushGenerator_TryMakeTetrahedron(
    brush_solid_t *pBrush,
    const common::allocator_t *pAllocator,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    math::vec3d_t center,
    common::f64 radius ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_GENERATOR_H
