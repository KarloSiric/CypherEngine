//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgReconstruct.h
//  Purpose: Declares CSG reconstruction: the kept boundary triangles become
//           a polygon mesh description again - one polygon per surviving
//           piece of each source face where possible - with transferred
//           attributes, identity, and provenance.
//  Details: Triangles are only the pipeline's working form. The triangles
//           kept from one source face (same operand, same facing) are
//           merged back by cancelling their shared edges; if what remains is
//           one simple loop, that loop is the new face, so a box minus a
//           box comes back as quads and L-shapes, not a triangle soup. A
//           piece with a hole or several loops stays triangles (a mesh face
//           has one loop).
//
//           Identity: input vertices keep their IDs; constructed vertices
//           have none. The first result face from each source face keeps
//           that face's ID; the rest have none. Missing IDs are assigned
//           when the result is built into an authored mesh.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_RECONSTRUCT_H
#define CYPHER_EDITOR_GEOMETRY_CSG_RECONSTRUCT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgAttributes.h"
#include "CypherGeometry_CsgBoundary.h"

namespace cypher::editor::geometry
{

struct csg_mesh_result_t {
    mesh_source_description_t mesh{};
    common::vector_t<common::u32> faceOperand{};  // per face
    common::vector_t<common::u32> faceSource{};   // source face index in that operand
    common::vector_t<common::u8> faceFlipped{};   // 1 for cut faces
    common::vector_t<csg_point_key_t> vertexKeys{}; // per vertex: what produced it
};

CYPHER_NODISCARD geometry_status_t CsgResult_Init( csg_mesh_result_t *pResult, const common::allocator_t *pAllocator ) noexcept;
void CsgResult_Shutdown( csg_mesh_result_t *pResult ) noexcept;
void CsgResult_Clear( csg_mesh_result_t *pResult ) noexcept;

CYPHER_NODISCARD geometry_status_t CsgReconstruct_TryBuild(
    const common::vector_t<csg_boundary_triangle_t> &triangles,
    const csg_intersection_t *pX,
    const csg_operand_t *pA,
    const csg_operand_t *pB,
    const csg_attribute_policy_t &policy,
    csg_mesh_result_t *pResult,
    csg_diagnostics_t *pDiag ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_RECONSTRUCT_H
