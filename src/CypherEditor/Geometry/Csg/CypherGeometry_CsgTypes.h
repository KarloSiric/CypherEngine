//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgTypes.h
//  Purpose: Declares the records shared by the mesh CSG pipeline stages
//           (InputNormalization -> BroadPhase -> Intersections ->
//           Corefinement -> CellComplex/Classification/CoplanarOverlay ->
//           Expression -> BoundaryExtraction -> Reconstruction/Stitching/
//           Cleanup/AttributeTransfer), so each stage's file owns its
//           algorithm and none owns another's data.
//  Details: Topology is decided only by exact predicates on input
//           coordinates (CypherMath Orient2D/Orient3D). Intersection points
//           are constructed in binary64, but each has a canonical key naming
//           the input elements that produced it (an operand edge crossing an
//           operand triangle, two coplanar edges crossing, or an input
//           vertex), and one point exists per key. Every triangle that meets
//           that point therefore uses the same index, so the two operands'
//           refined surfaces conform exactly without any distance welding.
//
//           Indexing: points are one table - operand A's vertices, then B's,
//           then constructed points. Triangles are one range too - A's input
//           triangles, then B's.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_TYPES_H
#define CYPHER_EDITOR_GEOMETRY_CSG_TYPES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshSource.h"

namespace cypher::editor::geometry
{

inline constexpr common::u32 kCsgOperandA = 0u;
inline constexpr common::u32 kCsgOperandB = 1u;
// Bounds per evaluation: they keep the quadratic corners of the pipeline
// (per-triangle arrangements, patch searches) interactive.
inline constexpr common::usize kCsgTrianglesMax = 262144u;
inline constexpr common::usize kCsgPointsMax = 1u << 20;

enum class csg_operator_t : common::u8 {
    UNION = 0u,
    INTERSECTION,
    DIFFERENCE,           // A minus B
    SYMMETRIC_DIFFERENCE, // (A - B) and (B - A); fails NON_MANIFOLD where they touch
    CLIP                  // A's surface outside B, B's surface not added (result is open)
};

// Pipeline stage, for diagnostics.
enum class csg_stage_t : common::u8 {
    NONE = 0u,
    INPUT,
    BROAD_PHASE,
    INTERSECTION,
    COREFINEMENT,
    CLASSIFICATION,
    EXTRACTION,
    RECONSTRUCTION
};

struct csg_diagnostics_t {
    geometry_status_t status{ geometry_status_t::OK };
    csg_stage_t stage{ csg_stage_t::NONE };
    math::vec3d_t witness{};        // where it went wrong, when there is a place
    common::u32 iWitnessTriangle{ CY_INVALID_INDEX };
    common::usize cCandidatePairs{ 0u };
    common::usize cIntersectionPoints{ 0u };
    common::usize cSegments{ 0u };
    common::usize cRefinedTriangles{ 0u };
    common::usize cPatches{ 0u };
    common::usize cNearDuplicatePoints{ 0u }; // distinct points closer than the stitch report distance
};

// One input triangle of an operand, from tessellating a source face.
struct csg_source_triangle_t {
    common::u32 v[3]{ 0u, 0u, 0u }; // operand-local vertex indices
    common::u32 iFace{ 0u };        // source face (description order)
    mesh_corner_attributes_t corners[3]{};
};

struct csg_operand_t {
    common::vector_t<math::vec3d_t> positions{};
    common::vector_t<geometry_source_id_t> vertexIds{};
    common::vector_t<csg_source_triangle_t> triangles{};
    common::vector_t<mesh_face_attributes_t> faceAttributes{};
    common::vector_t<geometry_source_id_t> faceIds{};
    common::vector_t<mesh_source_edge_t> edges{}; // non-default edges, local indices
    math::vec3d_t lo{}, hi{};
    geometry_source_id_t rootId{};
    bool bClosed{ false };
};

enum class csg_point_kind_t : common::u8 {
    VERTEX = 0u, // a = operand, b = local vertex
    EDGE_FACE,   // a = operand of the edge, b < c = its local vertices, d = global triangle of the other operand
    EDGE_EDGE    // coplanar crossing: a < b = A's edge (local), c < d = B's edge (local)
};

struct csg_point_key_t {
    csg_point_kind_t kind{ csg_point_kind_t::VERTEX };
    common::u32 a{ 0u }, b{ 0u }, c{ 0u }, d{ 0u };
};

// A segment of the intersection (or of a coplanar operand's boundary) that
// must become an edge inside global triangle iTriangle.
struct csg_constraint_t {
    common::u32 iTriangle{ 0u };
    common::u32 p0{ 0u }, p1{ 0u };
};

// A point that must become a vertex of global triangle iTriangle (on its
// boundary or inside).
struct csg_triangle_point_t {
    common::u32 iTriangle{ 0u };
    common::u32 p{ 0u };
};

// A refined triangle: global point indices, counter-clockwise seen from
// outside its operand, and the input triangle it lies in.
struct csg_refined_triangle_t {
    common::u32 v[3]{ 0u, 0u, 0u };
    common::u32 iSource{ 0u }; // global input triangle
};

enum class csg_label_t : common::u8 {
    UNKNOWN = 0u,
    INSIDE,        // inside the other operand
    OUTSIDE,
    SHARED_SAME,   // coincides with a piece of the other surface, same facing
    SHARED_OPPOSITE
};

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_TYPES_H
