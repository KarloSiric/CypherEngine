//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SubdivisionSurface.h
//  Purpose: Declares retained subdivision: a descriptor evaluated from an
//           authored mesh without changing it, with creases, open-mesh
//           boundaries, interpolated surface data, and source mapping; and
//           an explicit collapse that turns the result into a new mesh.
//  Details: A modeller keeps the cage (the authored mesh) and looks at the
//           smooth surface; editing the cage re-evaluates it. That is why
//           evaluation is separate from the destructive
//           MeshSubdivision_TryCatmullClark, which also accepts only closed
//           meshes and drops UVs, materials and identity.
//
//           Rules (Catmull-Clark, generalised to any polygon):
//             face point   centroid of the face;
//             edge point   (ends + adjacent face points) / 4 on a smooth
//                          interior edge, the midpoint on a boundary edge,
//                          blended by the edge's crease weight w in [0, 1]
//                          (0 smooth, 1 sharp) between the two;
//             vertex point smooth vertex (fewer than two sharp edges):
//                          (F + 2R + (n - 3)P) / n; crease vertex (exactly
//                          two sharp edges): (a + 6P + b) / 8 over the two
//                          sharp neighbours; corner (three or more): P.
//                          Crease and corner results blend with the smooth
//                          one by the mean / max weight of the sharp edges.
//           Boundary edges count as sharp with weight 1, so an open mesh
//           keeps its outline curve (the cubic B-spline of the boundary).
//           With PIN_CORNERS a boundary vertex with only two edges stays
//           where it is, so a subdivided plane keeps its corners.
//
//           Crease weights persist: both halves of a creased edge carry
//           the same weight to the next level, so w = 1 stays perfectly
//           sharp at any level (the same meaning the editable mesh's
//           creaseWeight has). Edge flags (hard, seam) follow the halves
//           too. Hard edges can optionally act as fully sharp creases.
//
//           Surface data per corner (both UV sets and colour) is
//           interpolated linearly within each source face, so UV seams stay
//           put. Every output face keeps its source face's material and
//           smoothing groups.
//
//           LINEAR splits the same way but leaves every point where the
//           polygon already was (adds density without changing shape).
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SUBDIVISION_SURFACE_H
#define CYPHER_EDITOR_GEOMETRY_SUBDIVISION_SURFACE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_MeshSource.h"

namespace cypher::editor::geometry
{

// Bounds: every level multiplies the face count by about four; five
// levels of a 6-face cube is 6144 faces, of a modest cage millions.
inline constexpr common::u32 kSubdivisionLevelsMax = 5u;
inline constexpr common::usize kSubdivisionVerticesMax = 1u << 20; // 1 048 576
inline constexpr common::usize kSubdivisionCornersMax = 1u << 22;  // 4 194 304

enum class subdivision_scheme_t : common::u8 {
    CATMULL_CLARK = 0u,
    LINEAR
};

enum class subdivision_boundary_t : common::u8 {
    SMOOTH = 0u,  // boundary corners round off with the outline
    PIN_CORNERS   // boundary vertices with two edges stay fixed
};

struct subdivision_descriptor_t {
    subdivision_scheme_t scheme{ subdivision_scheme_t::CATMULL_CLARK };
    common::u32 cLevels{ 1u };
    subdivision_boundary_t boundary{ subdivision_boundary_t::PIN_CORNERS };
    bool bHardEdgesAreCreases{ false };
};

// Where an output vertex came from, in the source description's indexing
// (MeshSource_TryDescribe order).
enum class subdivision_origin_kind_t : common::u8 {
    VERTEX = 0u, // descends from source vertex iSource (moved by smoothing)
    EDGE,        // lies on the source edge between vertices iSource and iSourceB
    FACE         // lies inside source face iSource
};

struct subdivision_origin_t {
    subdivision_origin_kind_t kind{ subdivision_origin_kind_t::VERTEX };
    common::u32 iSource{ 0u };
    common::u32 iSourceB{ 0u }; // EDGE only: iSource < iSourceB
};

struct subdivision_result_t {
    // The evaluated surface as a mesh description. Vertices that descend
    // from a source vertex keep its ID; every other vertex and every face
    // has GEOMETRY_SOURCE_ID_INVALID (they have no authored identity until
    // collapsed). The root ID is the source mesh's.
    mesh_source_description_t mesh{};
    common::vector_t<subdivision_origin_t> vertexOrigins{}; // one per mesh vertex
    common::vector_t<common::u32> faceSources{};             // source face index per mesh face
};

CYPHER_NODISCARD geometry_status_t SubdivisionResult_Init( subdivision_result_t *pResult, const common::allocator_t *pAllocator ) noexcept;
void SubdivisionResult_Shutdown( subdivision_result_t *pResult ) noexcept;

// INVALID_ARGUMENT for an unknown scheme/boundary or cLevels above
// kSubdivisionLevelsMax.
CYPHER_NODISCARD geometry_status_t Subdivision_ValidateDescriptor( const subdivision_descriptor_t &descriptor ) noexcept;

// Exact element counts after evaluation, without evaluating (for budget
// checks and UI readouts). LIMIT_EXCEEDED past the bounds above.
CYPHER_NODISCARD geometry_status_t Subdivision_TryPredictCounts(
    const mesh_source_t *pSource,
    const subdivision_descriptor_t &descriptor,
    common::usize *pcVerticesOut,
    common::usize *pcFacesOut ) noexcept;

// Evaluates the descriptor over the source into *pResult (initialized; its
// previous contents are replaced). The source is not changed. Level 0 is
// the cage itself. Failure leaves *pResult empty but initialized.
CYPHER_NODISCARD geometry_status_t Subdivision_TryEvaluate(
    const mesh_source_t *pSource,
    const subdivision_descriptor_t &descriptor,
    subdivision_result_t *pResult ) noexcept;

// The same over a mesh description, e.g. an intermediate stage of a
// modifier stack. IDs are carried as given (invalid ones allowed);
// vertexOrigins and faceSources index that description. The description
// must describe a manifold polygon mesh (NON_MANIFOLD otherwise).
CYPHER_NODISCARD geometry_status_t Subdivision_TryEvaluateDescription(
    const mesh_source_description_t *pCage,
    const subdivision_descriptor_t &descriptor,
    subdivision_result_t *pResult ) noexcept;

// Collapse: evaluates and builds the result as a new authored mesh in a
// canonical-empty *pOut. The root keeps the source's ID (so the document
// can replace the cage), vertices descending from source vertices keep
// theirs, every other vertex and every face gets a fresh ID from
// *pIdAllocator. Also bounded by the mesh source limits. Failure leaves
// *pOut empty and the allocator unchanged.
CYPHER_NODISCARD geometry_status_t Subdivision_TryCollapse(
    const mesh_source_t *pSource,
    const subdivision_descriptor_t &descriptor,
    const common::allocator_t *pAllocator,
    geometry_source_id_allocator_t *pIdAllocator,
    mesh_source_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SUBDIVISION_SURFACE_H
