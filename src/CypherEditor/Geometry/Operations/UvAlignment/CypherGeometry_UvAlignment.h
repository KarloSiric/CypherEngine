//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_UvAlignment.h
//  Purpose: Declares UV alignment and justification operations for brush
//           side attributes.
//  Details: Provides the texture coordinate manipulation tools that every
//           level editor needs: reset to default projection, flip along
//           U or V, fit one tile to a face, justify within face bounds,
//           and seamless alignment across adjacent faces.
//
//           Every operation mutates the UV mapping inside a
//           geometry_brush_side_attributes_t in place. Operations that
//           need face geometry accept an array of world-space vertices
//           defining the face polygon.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_UV_ALIGNMENT_H
#define CYPHER_EDITOR_GEOMETRY_UV_ALIGNMENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_Schema.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherMath.h"

namespace cypher::editor::geometry
{

// Justification anchor within the face's projected UV bounds.
enum class uv_justify_mode_t : common::u8 {
    MIN,     // Align to minimum bound (left for U, bottom for V).
    CENTER,  // Center texture within face bounds.
    MAX      // Align to maximum bound (right for U, top for V).
};

// Resets UV mapping to a default axis-aligned projection derived from the
// face normal. Unit scale (1 world unit = 1 UV unit), zero rotation, zero
// offset. The resulting basis is deterministic: given the same normal, the
// same mapping is produced.
CYPHER_NODISCARD geometry_status_t UvAlign_Reset(
    geometry_brush_side_attributes_t *pAttribs,
    math::vec3d_t faceNormal ) noexcept;

// Mirrors the texture along the U axis by negating the U direction vector
// and adjusting the offset so the visual anchor stays in place.
CYPHER_NODISCARD geometry_status_t UvAlign_FlipU(
    geometry_brush_side_attributes_t *pAttribs ) noexcept;

// Mirrors the texture along the V axis.
CYPHER_NODISCARD geometry_status_t UvAlign_FlipV(
    geometry_brush_side_attributes_t *pAttribs ) noexcept;

// Scales and offsets the UV mapping so that exactly one tile covers the
// face polygon. pVertices points to cVertices world-space positions that
// define the face boundary.
CYPHER_NODISCARD geometry_status_t UvAlign_FitToFace(
    geometry_brush_side_attributes_t *pAttribs,
    const math::vec3d_t *pVertices,
    common::usize cVertices ) noexcept;

// Shifts the U offset so the texture is justified to the given anchor
// within the face's projected U extent. Does not change scale or V.
CYPHER_NODISCARD geometry_status_t UvAlign_JustifyU(
    geometry_brush_side_attributes_t *pAttribs,
    const math::vec3d_t *pVertices,
    common::usize cVertices,
    uv_justify_mode_t mode ) noexcept;

// Shifts the V offset so the texture is justified to the given anchor
// within the face's projected V extent. Does not change scale or U.
CYPHER_NODISCARD geometry_status_t UvAlign_JustifyV(
    geometry_brush_side_attributes_t *pAttribs,
    const math::vec3d_t *pVertices,
    common::usize cVertices,
    uv_justify_mode_t mode ) noexcept;

// Aligns the target face's UV mapping to continue seamlessly from a
// reference face across their shared edge. The target inherits the
// reference face's UV projection so texture coordinates are continuous
// at the shared edge vertices.
//
// sharedEdgeA / sharedEdgeB are the two world-space endpoints of the
// edge shared between the two faces. Both must lie on both faces.
CYPHER_NODISCARD geometry_status_t UvAlign_AlignToAdjacentFace(
    geometry_brush_side_attributes_t *pTargetAttribs,
    math::vec3d_t targetNormal,
    const geometry_brush_side_attributes_t &refAttribs,
    math::vec3d_t sharedEdgeA,
    math::vec3d_t sharedEdgeB ) noexcept;

// Copies all face attributes (material ref, UV projection) from a source
// brush side to a destination brush side. This is TrenchBroom's "paste
// face attributes" feature — paint one face's look onto another.
CYPHER_NODISCARD geometry_status_t UvAlign_CopyFaceAttributes(
    brush_solid_t *pBrush,
    common::usize iDstSide,
    const geometry_brush_side_attributes_t &srcAttribs ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_UV_ALIGNMENT_H
