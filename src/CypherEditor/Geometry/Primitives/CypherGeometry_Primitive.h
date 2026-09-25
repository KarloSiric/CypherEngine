//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Primitive.h
//  Purpose: Declares the primitive creation front end: one parameter
//           record for every shape the create tools offer (box, ramp,
//           pyramid, cylinder, cone, sphere, torus, plane, arch, stairs),
//           built as brushes, as one editable mesh, or as patches.
//  Details: A primitive fills a dragged box, the way both Hammer and
//           TrenchBroom create shapes. The record is transient: it is the
//           input of one creation, not a retained recipe (see README).
//
//           One record, three outputs, because the same create tool serves
//           brush work (Quake-style), mesh work (Hammer 5-style) and curved
//           detail (Radiant patches). Brushes and meshes share the circle
//           modes (BrushShapes_TryMakeCircle), so a brush cylinder and a
//           mesh cylinder made from one record have the same vertices (also
//           cones and spheres). Two exceptions: the mesh arch follows a
//           uniform half ellipse with inner curve = outer scaled by the
//           wall thickness, and patches are true (quadratic) ellipses
//           inscribed in the box, since circle modes describe polygons.
//           Not every shape exists in every output:
//
//             kind      brushes          mesh   patches
//             BOX       1                yes    -
//             RAMP      1                yes    -
//             PYRAMID   1                yes    -
//             CYLINDER  1                yes    1 open tube
//             CONE      1                yes    1 open tube
//             SPHERE    1                yes    1
//             TORUS     -                yes    1
//             PLANE     -                yes    1
//             ARCH      one per segment  yes    -
//             STAIRS    one per step     yes    -
//
//           A missing combination is UNSUPPORTED. A torus is not convex
//           and a plane has no volume, so neither can be a brush; patches
//           exist only for the curved shapes and the plane.
//
//           Results are appended to a geometry fragment (Exchange) with
//           fresh IDs from the caller's allocator, so creating into a
//           document is: copy the document's allocator, build, insert.
//           Inserting registers the IDs and advances the document:
//
//             geometry_source_id_allocator_t ids = pDoc->sourceIds.allocator;
//             Primitive_TryBuild( prim, output, pDoc->policy, &ids, &frag );
//             GeometryFragment_TryInsert( &frag, pDoc, &newRoots );
//
//           Mesh output: every face gets the record's material and a
//           world-aligned box projection (worldUnitsPerUv, origin at the
//           box's low corner), matching how brushes texture by default.
//           Flat parts (box sides, caps, treads) are smoothing group 0,
//           curved parts group 1, so a cylinder shades round with sharp
//           rims. Faces wind counter-clockwise seen from outside.
//
//           Failure-atomic: on any failure the fragment and the ID
//           allocator are unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_PRIMITIVE_H
#define CYPHER_EDITOR_GEOMETRY_PRIMITIVE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushShapes.h"
#include "CypherGeometry_Fragment.h"

namespace cypher::editor::geometry
{

// Upper bounds for the record's counts (the brush side limit already caps
// cSides at kBrushCircleSidesMax). They keep every mesh output under the
// mesh description limits.
inline constexpr common::u32 kPrimitiveRingsMax = 128u;
inline constexpr common::u32 kPrimitiveSegmentsMax = 64u;
inline constexpr common::u32 kPrimitiveStepsMax = 256u;

enum class geometry_primitive_kind_t : common::u8 {
    BOX = 0u,
    RAMP,     // a wedge rising in stairsDirection: floor, back wall, slope
    PYRAMID,  // box base at the low end of `axis`, apex centred on the high end
    CYLINDER,
    CONE,     // base at the low end of `axis`, apex centred on the high end
    SPHERE,
    TORUS,
    PLANE,
    ARCH,
    STAIRS
};

enum class geometry_primitive_output_t : common::u8 {
    BRUSHES = 0u,
    MESH,
    PATCHES
};

struct geometry_primitive_t {
    geometry_primitive_kind_t kind{ geometry_primitive_kind_t::BOX };
    // The dragged box, lo < hi on every axis. PLANE alone may be flat
    // along `axis`; it lies at box.lo on that axis, facing +axis.
    brush_shape_box_t box{};
    // Main axis (0 = X, 1 = Y, 2 = Z): the cylinder/cone/pyramid/torus
    // axis, the sphere's pole axis, the plane's normal, and for ARCH the
    // direction the arch runs through (0 or 1; arches stand along +Z).
    common::u32 axis{ 2u };
    // Around the axis: cylinder, cone, sphere and torus ring, arch curve
    // (a full circle's worth; the arch uses the upper half).
    common::u32 cSides{ 16u };
    // Sphere bands pole to pole (>= 2); torus tube segments (>= 3).
    common::u32 cRings{ 8u };
    brush_circle_mode_t circleMode{ brush_circle_mode_t::VERTEX_ALIGNED };
    // Mesh subdivisions along X, Y, Z for BOX and PLANE (each in
    // [1, kPrimitiveSegmentsMax]); for CYLINDER and CONE the entry of
    // `axis` is the number of height bands. Brushes ignore them.
    common::u32 cSegments[3]{ 1u, 1u, 1u };
    common::f64 archThickness{ 16.0 };
    common::f64 stepHeight{ 8.0 };
    brush_stairs_direction_t stairsDirection{ brush_stairs_direction_t::POS_X };
    geometry_material_ref_t material{};
    math::vec2d_t worldUnitsPerUv{ 1.0, 1.0 };
};

// Checks the record for `output` without building anything: INVALID_ARGUMENT
// for a bad box, axis, count, or dimension (an arch thicker than half its
// width, a torus box less than twice as wide as tall), UNSUPPORTED for a
// kind the output cannot represent, LIMIT_EXCEEDED when a mesh would exceed
// the mesh description limits.
CYPHER_NODISCARD geometry_status_t Primitive_Validate(
    const geometry_primitive_t &primitive,
    geometry_primitive_output_t output ) noexcept;

// Builds the primitive and appends the result to *pFragment: brushes with
// one default surface record per side (carrying the material and UV
// density), one mesh, or one patch. IDs come from *pIdAllocator. Failure
// leaves the fragment and the allocator unchanged.
CYPHER_NODISCARD geometry_status_t Primitive_TryBuild(
    const geometry_primitive_t &primitive,
    geometry_primitive_output_t output,
    const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator,
    geometry_fragment_t *pFragment ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_PRIMITIVE_H
