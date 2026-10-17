//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_ObjectCreate.h
//  Purpose: Declares creation of the map objects the interactive tools
//           make: point entities with properties (Entity, Path, Decal and
//           Overlay tools), extruded polygon meshes (Polygon tool), patch
//           primitives (Patch tool), swept path meshes (Curve tool); and
//           the Paint tool's stroke. Terrain belongs to the workspace's own
//           terrain creation.
//  Details: Every new object takes the first layer, fresh IDs above both
//           high-water marks, and the material given by path. Geometry
//           objects revise the geometry document; entity creation does not
//           (it is metadata), which the GUI publishes as such.
//
//           PRIVATE working-copy contract as CypherMap_MeshEdit.h.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_MAP_OBJECT_CREATE_H
#define CYPHER_EDITOR_MAP_OBJECT_CREATE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMap_Document.h"
#include "CypherMap_Edit.h"

#include <vector>

#include "CypherGeometry_IdAllocator.h"
#include "CypherGeometry_Patch.h"

namespace cypher::editor::map
{

struct map_property_t {
    common::string_view_t key{};
    common::string_view_t value{};
};

// A point entity of className at origin with string properties.
CYPHER_NODISCARD map_status_t MapCreate_Entity( map_document_t *pMap, common::string_view_t className, math::vec3d_t origin,
    common::span_t<const map_property_t> properties, common::u64 *pIdOut ) noexcept;

// Sets one string property of an existing entity.
CYPHER_NODISCARD map_status_t MapEntity_SetString( map_document_t *pMap, common::u64 entityId, common::string_view_t key,
    common::string_view_t value ) noexcept;

// A closed mesh: the planar polygon (three or more points in order, either
// winding) swept along `extrusion`. Faces are flat-shaded and projected
// world-aligned at worldUnitsPerUv.
CYPHER_NODISCARD map_status_t MapCreate_PolygonMesh( map_document_t *pMap, common::span_t<const math::vec3d_t> polygon,
    math::vec3d_t extrusion, common::string_view_t material, common::f64 worldUnitsPerUv, common::u64 *pIdOut ) noexcept;

// Patch primitives fitted to a box. axis (0..2) is the cylinder, cone and
// disc axis and the plane's normal; segments the arcs around a circle.
// A torus rings the axis in the box's middle, its tube as thick as the box
// is deep (a quarter of the ring when the box is flat).
enum class map_patch_shape_t : common::u8 { PLANE = 0u, CYLINDER, CONE, SPHERE, DISC, BEVEL, TORUS, COUNT };
CYPHER_NODISCARD map_status_t MapCreate_Patch( map_document_t *pMap, map_patch_shape_t shape, math::aabbd_t bounds,
    common::u32 axis, common::u32 segments, common::string_view_t material, common::u64 *pIdOut ) noexcept;

// The same patch built outside any document, for previews: control IDs from
// *pIds after `root`, into a canonical empty *pOut (shut it down after).
// INVALID_ARGUMENT for a box without the extent the shape needs.
CYPHER_NODISCARD geometry::geometry_status_t MapCreate_BuildPatch( map_patch_shape_t shape, math::aabbd_t bounds, common::u32 axis,
    common::u32 segments, common::u32 materialId, geometry::geometry_source_id_t root, geometry::geometry_source_id_allocator_t *pIds,
    const common::allocator_t *pAllocator, geometry::patch_surface_t *pOut ) noexcept;

// A mesh swept along a polyline (two or more points): a width x height
// rectangle standing on the path, centred across it, with end caps.
// A Source-style displacement on a four-cornered face (Create
// Displacement): a (2^power + 1)^2 grid mesh over the quad (corners counter-
// clockwise seen from the side it pushes out of), raised by `elevation`
// along the quad's normal plus deterministic fractal noise (amplitude 0:
// none; `noiseScale` world units per cycle). Power 1..4. The new mesh is
// ready to sculpt with the Displacement tool.
struct map_displacement_desc_t {
    math::vec3d_t corners[4]{};
    common::u32 power{ 3u };
    common::f64 elevation{ 0.0 };
    common::f64 noiseAmplitude{ 0.0 };
    common::f64 noiseScale{ 64.0 };
    common::u32 noiseOctaves{ 3u };
    common::u32 seed{ 1u };
};
CYPHER_NODISCARD map_status_t MapCreate_Displacement( map_document_t *pMap, const map_displacement_desc_t &desc, common::string_view_t material,
    common::f64 worldUnitsPerUv, common::u64 *pIdOut ) noexcept;

// Sweep: `profile` (2D, x across the path, y up from it; counter-clockwise
// when closed) carried along a smooth curve through `points` (Catmull-Rom,
// closed when bClosed). Closed profiles on open paths get end caps.
CYPHER_NODISCARD map_status_t MapCreate_Sweep( map_document_t *pMap, common::span_t<const math::vec3d_t> points, bool bClosed,
    common::span_t<const math::vec2d_t> profile, bool bProfileClosed, common::string_view_t material, common::f64 worldUnitsPerUv,
    common::u64 *pIdOut ) noexcept;

// A hollow circular tube along an open Catmull-Rom path. Outer and inner
// walls join through annular end rims; the passage remains open. Points:
// 2..256, sides: 3..128, 0 < wallThickness < outerRadius. Self-intersecting
// sweeps are refused. UV0 and UV1 retain the generated distance charts
// (UV1 is not a packed lightmap). Failure leaves geometry, materials,
// placement, allocation cursors and *pIdOut unchanged.
CYPHER_NODISCARD map_status_t MapCreate_HollowPipe( map_document_t *pMap, common::span_t<const math::vec3d_t> points,
    common::f64 outerRadius, common::f64 wallThickness, common::u32 sides, common::string_view_t material,
    common::f64 worldUnitsPerUv, common::u64 *pIdOut ) noexcept;

// Walls along a path: the path (points on one plane, normal `up` pointing
// where the walls stand) thickened to `thickness` with mitred, bevelled or
// round corners, then raised `height` along `up`. Closed joins the last
// point back to the first (a room outline).
enum class map_wall_join_t : common::u8 { MITER = 0u, BEVEL, ROUND };
CYPHER_NODISCARD map_status_t MapCreate_Walls( map_document_t *pMap, common::span_t<const math::vec3d_t> points, math::vec3d_t up, bool bClosed,
    common::f64 thickness, common::f64 height, map_wall_join_t join, common::string_view_t material, common::f64 worldUnitsPerUv,
    common::u64 *pIdOut ) noexcept;

// Lathe: `profile` (x = distance from the axis >= 0, y = height along it)
// revolved about the line through axisOrigin along axisDir by `angle`
// radians (2 pi: a full turn) in `segments` steps. Points on the axis close
// the solid with a pole.
CYPHER_NODISCARD map_status_t MapCreate_Lathe( map_document_t *pMap, common::span_t<const math::vec2d_t> profile, math::vec3d_t axisOrigin,
    math::vec3d_t axisDir, common::f64 angle, common::u32 segments, common::string_view_t material, common::f64 worldUnitsPerUv,
    common::u64 *pIdOut ) noexcept;

// Hammer's Tie to Entity: world geometry becomes part of a brush entity -
// `entity` when it names one, else a new entity of `className` at the
// geometry's centre (*pEntityOut receives it). Geometry already owned by
// another entity moves to this one.
CYPHER_NODISCARD map_status_t MapCreate_TieToEntity( map_document_t *pMap, common::span_t<const common::u64> geometry, common::u64 entity,
    common::string_view_t className, common::u64 *pEntityOut ) noexcept;
// Hammer's Move to World: the geometry leaves its entity; an entity left
// owning nothing (and having been a brush entity) is removed with it.
// *pMovedOut counts the geometry that moved.
CYPHER_NODISCARD map_status_t MapCreate_MoveToWorld( map_document_t *pMap, common::span_t<const common::u64> ids, common::usize *pMovedOut ) noexcept;

// Hammer 5's other Block tool shapes, fitted to desc.bounds, as one closed
// mesh each with world-aligned UVs (desc.worldUnitsPerUv):
// - ARCH: a curved wall, wallThickness thick, from startDegrees round
//   arcDegrees in the plane across `axis` (an elliptical ring segment).
// - PIPE: the full ring (a tube with a wallThickness wall).
// - STAIRS: nSteps steps rising toward stairsDirection.
// - SPIRAL_STAIRS: nSteps wedge treads round `axis` from innerRadius out,
//   turnDegrees in all, each one step higher.
// - TORUS: a ring of tube tubeRatio of the half footprint (at most half
//   the height), nSides round and half as many across.
// INVALID_ARGUMENT for a wall or radius that does not fit the bounds.
CYPHER_NODISCARD map_status_t MapCreate_BlockShape( map_document_t *pMap, const map_primitive_desc_t &desc, common::string_view_t material,
    common::u64 *pIdOut ) noexcept;

// Curve patches (Radiant's arches and curved strips) from clicked points.
// The points become quadratic controls: with bSmooth a quadratic B-spline
// that starts and ends on the first and last point and is pulled toward the
// inner ones (a closed one is pulled toward all of them); otherwise straight
// spans with a sharp corner at every point. Strip: that curve extruded along
// `direction`. Lathe: the (distance >= 0, height) profile spun a full turn
// about the axis in `segments` arcs. Each publishes one biquadratic patch;
// too many points for a patch row is LIMIT_EXCEEDED.
CYPHER_NODISCARD map_status_t MapCreate_PatchStrip( map_document_t *pMap, common::span_t<const math::vec3d_t> points, bool bSmooth, bool bClosed,
    math::vec3d_t direction, common::string_view_t material, common::u64 *pIdOut ) noexcept;
CYPHER_NODISCARD map_status_t MapCreate_PatchLathe( map_document_t *pMap, common::span_t<const math::vec2d_t> profile, bool bSmooth,
    math::vec3d_t axisOrigin, math::vec3d_t axisDir, common::u32 segments, common::string_view_t material, common::u64 *pIdOut ) noexcept;

// Brush Sweep: each listed brush face (brush, side) moved along a path -
// straight by `offset`, an arc of `angle` radians about the axis through
// axisOrigin along axisDir, or an S-bend reaching `offset` - in `segments`
// steps, repeated `iterations` times; every step is a new convex brush
// (the hull of the face at its two ends), so the strip is watertight.
// pIdsOut (initialized) receives the new brushes.
enum class map_sweep_path_t : common::u8 { STRAIGHT = 0u, ARC, S_BEND };
struct map_brush_face_t { common::u64 brush{ 0u }, side{ 0u }; };
CYPHER_NODISCARD map_status_t MapCreate_BrushSweep( map_document_t *pMap, common::span_t<const map_brush_face_t> faces, map_sweep_path_t path,
    math::vec3d_t offset, math::vec3d_t axisOrigin, math::vec3d_t axisDir, common::f64 angle, common::u32 segments, common::u32 iterations,
    common::vector_t<common::u64> *pIdsOut ) noexcept;

// Rebuilds mesh `meshId` from plain polygons (each face a loop of point
// indices), keeping its root ID, placement and record; vertices and faces
// get fresh IDs, the material and world-aligned UVs. For repairs and
// conversions that start from geometry without identities.
CYPHER_NODISCARD map_status_t MapCreate_ReplaceMeshPolygons( map_document_t *pMap, common::u64 meshId, const std::vector<math::vec3d_t> &points,
    const std::vector<std::vector<common::u32>> &faces, common::string_view_t material, common::f64 worldUnitsPerUv ) noexcept;

CYPHER_NODISCARD map_status_t MapCreate_PathMesh( map_document_t *pMap, common::span_t<const math::vec3d_t> path, common::f64 width,
    common::f64 height, common::string_view_t material, common::f64 worldUnitsPerUv, common::u64 *pIdOut ) noexcept;

// One paint stroke on a mesh's corner colours: a dab at every centre,
// painting `channels` (mesh_paint_channel_bits_t) toward value.
CYPHER_NODISCARD map_status_t MapMesh_PaintStroke( map_document_t *pMap, common::u64 meshId, common::span_t<const math::vec3d_t> centers,
    common::f64 radius, common::f64 strength, common::u8 channels, common::u8 value ) noexcept;

} // namespace cypher::editor::map

#endif // CYPHER_EDITOR_MAP_OBJECT_CREATE_H
