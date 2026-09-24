//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushTransform.h
//  Purpose: Declares affine transform operations on brush side planes.
//  Details: Each function modifies the side planes of a brush_solid_t
//           in place. Translation shifts the plane distance. Rotation
//           and non-uniform scale transform the plane normal through
//           the inverse-transpose of the linear part, then renormalize.
//           The math library's Planed_TryTransform handles all cases.
//
//           These are pure plane-math operations — they do not interact
//           with the document, transaction, spatial index, or selection.
//           The edit pipeline composes them with those systems.
//
//           A transform that would produce a degenerate plane (normal
//           too short after non-uniform scale) fails without modifying
//           any planes. The brush is either fully transformed or
//           unchanged — no partial application.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_TRANSFORM_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_TRANSFORM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_Attributes_BrushSideStore.h"

namespace cypher::editor::geometry
{

// Translates all side planes by the given offset. The plane normals
// are unchanged; only the distance terms shift. Finite inputs that overflow
// binary64 return NUMERIC_FAILURE without changing any side.
CYPHER_NODISCARD geometry_status_t BrushTransform_TryTranslate(
    brush_solid_t *pBrush,
    math::vec3d_t offset ) noexcept;

// Rotates all side planes around a world-space pivot using the
// given affine3d rotation transform. The transform must be a pure
// rotation (orthonormal linear part) — scale or shear components
// would produce incorrect plane normals.
//
// Internally builds: translate(-pivot) → rotate → translate(+pivot)
CYPHER_NODISCARD geometry_status_t BrushTransform_TryRotate(
    brush_solid_t *pBrush,
    math::vec3d_t pivot,
    math::affine3d_t rotation ) noexcept;

// Scales all side planes relative to a world-space pivot by per-axis
// factors. Uniform scale preserves plane normals; non-uniform scale
// transforms them through the inverse-transpose and renormalizes.
// Zero or negative scale factors return INVALID_ARGUMENT.
CYPHER_NODISCARD geometry_status_t BrushTransform_TryScale(
    brush_solid_t *pBrush,
    math::vec3d_t pivot,
    math::vec3d_t scale ) noexcept;

// Applies a general affine3d transform to all side planes. The
// transform must have a non-degenerate linear part (determinant
// above the tolerance threshold). Builds the combined
// translate-to-origin → linear → translate-back internally if a
// pivot is implicit in the affine.
CYPHER_NODISCARD geometry_status_t BrushTransform_TryApplyAffine(
    brush_solid_t *pBrush,
    math::affine3d_t transform ) noexcept;

// ---------------------------------------------------------------------------
// Texture lock (Gate 8)
// ---------------------------------------------------------------------------

// Adjusts the UV projections referenced by a brush so textures remain
// stationary on its surfaces after a translation. Attribute records are
// addressed through each side's iAttributeIndex. A shared record is updated
// exactly once and an unreferenced record is left untouched.
CYPHER_NODISCARD geometry_status_t BrushTransform_TextureLockTranslate(
    const brush_solid_t *pBrush,
    geometry_brush_side_attribute_store_t *pStore,
    math::vec3d_t offset,
    const geometry_policy_t &policy ) noexcept;

// Adjusts UV projections so textures remain stationary after a rotation.
// The UV axes (uAxis, vAxis, normal) are rotated by the same rotation
// applied to the brush planes. The UV origin is transformed through the
// full pivot-relative rotation.
CYPHER_NODISCARD geometry_status_t BrushTransform_TextureLockRotate(
    const brush_solid_t *pBrush,
    geometry_brush_side_attribute_store_t *pStore,
    math::vec3d_t pivot,
    math::affine3d_t rotation,
    const geometry_policy_t &policy ) noexcept;

// Adjusts UV projections so textures remain stationary after a non-uniform
// scale. The UV origin is scaled relative to the pivot, and worldUnitsPerUv
// is adjusted by the corresponding axis scale factors projected onto the
// UV axes.
CYPHER_NODISCARD geometry_status_t BrushTransform_TextureLockScale(
    const brush_solid_t *pBrush,
    geometry_brush_side_attribute_store_t *pStore,
    math::vec3d_t pivot,
    math::vec3d_t scale,
    const geometry_policy_t &policy ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_TRANSFORM_H
