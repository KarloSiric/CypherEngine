//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CsgAttributes.h
//  Purpose: Declares mesh-CSG attribute transfer: materials and smoothing
//           from the source face, corner UVs and colours interpolated at
//           the new corners, crease/flags along source edges, the cut-face
//           material policy, and hard edges along the seams.
//  Details: Every result triangle lies inside one input triangle of one
//           source face, so its corner data is the barycentric blend of that
//           input triangle's corners: texture stays exactly where it was on
//           every surviving piece of surface. "Cut faces" are the pieces of
//           B kept inside out (the walls a subtraction carves); they keep
//           B's texture unless the policy gives them their own material, the
//           way Hammer lets a carve use a chosen material.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_CSG_ATTRIBUTES_H
#define CYPHER_EDITOR_GEOMETRY_CSG_ATTRIBUTES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_CsgIntersections.h"

namespace cypher::editor::geometry
{

struct csg_attribute_policy_t {
    bool bOverrideCutMaterial{ false };
    geometry_material_ref_t cutMaterial{};
    // Mark edges where faces of different origin meet (the seam of the
    // operation) as hard, so they shade as the crisp edges they are.
    bool bHardSeams{ true };
};

// Corner data at p (a point inside the source triangle's plane).
CYPHER_NODISCARD mesh_corner_attributes_t CsgAttributes_Interpolate(
    const csg_operand_t *pOperand, const csg_source_triangle_t &source, math::vec3d_t p ) noexcept;

// Face data for a result face from this source triangle.
CYPHER_NODISCARD mesh_face_attributes_t CsgAttributes_Face(
    const csg_operand_t *pOperand, const csg_source_triangle_t &source, bool bFlipped, const csg_attribute_policy_t &policy ) noexcept;

// The authored edge data a result edge between points p and q inherits:
// the crease and flags of the operand edge both lie on, if any. Returns
// false when the edge inherits nothing.
CYPHER_NODISCARD bool CsgAttributes_SourceEdge(
    const csg_intersection_t *pX, const csg_operand_t *pA, const csg_operand_t *pB, common::u32 p, common::u32 q, mesh_source_edge_t *pOut ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_CSG_ATTRIBUTES_H
