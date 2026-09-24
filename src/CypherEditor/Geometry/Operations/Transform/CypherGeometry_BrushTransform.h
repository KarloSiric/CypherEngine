//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushTransform.h
//  Purpose: Declares rigid, affine, and side-drag transforms of brushes
//           inside a geometry transaction.
//  Details: Brushes transform by transforming their planes (normals by the
//           inverse transpose, which keeps each half-space mapped onto its
//           image even through reflections), so a transformed brush is
//           exactly the image of the original and every side keeps its ID
//           and attribute binding. Surfacing follows the chosen texture
//           lock (see Attributes/Propagation).
//
//           Side drag moves one side's plane along its own normal, the
//           core map-editor face-drag gesture. Dragging so far that the
//           brush would collapse, or that the side would stop contributing
//           a face, is refused and the last valid preview stays.
//
//           Builders for common transforms are provided so hosts never
//           assemble affine matrices by hand for routine edits.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_BRUSH_TRANSFORM_H
#define CYPHER_EDITOR_GEOMETRY_BRUSH_TRANSFORM_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_Propagation.h"
#include "CypherGeometry_Transaction.h"

namespace cypher::editor::geometry
{

// ---------------------------------------------------------------------------
// Transform builders (all pivot-relative where a pivot applies)
// ---------------------------------------------------------------------------

CYPHER_NODISCARD math::affine3d_t BrushTransform_MakeTranslation( math::vec3d_t offset ) noexcept;

// Rotation by `radians` about a unit axis through `pivot` (right-handed).
// INVALID_ARGUMENT for a zero or non-finite axis.
CYPHER_NODISCARD geometry_status_t BrushTransform_TryMakeRotation(
    math::vec3d_t pivot,
    math::vec3d_t axis,
    math::f64 radians,
    math::affine3d_t *pTransformOut ) noexcept;

// Per-axis scale about a pivot. Negative factors mirror; zero is refused.
CYPHER_NODISCARD geometry_status_t BrushTransform_TryMakeScale(
    math::vec3d_t pivot,
    math::vec3d_t factors,
    math::affine3d_t *pTransformOut ) noexcept;

// Mirror across the plane through `pivot` with the given unit normal.
CYPHER_NODISCARD geometry_status_t BrushTransform_TryMakeReflection(
    math::vec3d_t pivot,
    math::vec3d_t normal,
    math::affine3d_t *pTransformOut ) noexcept;

// Shear: p' = p + factor * dot(p - pivot, sourceAxis) * shearAxis, for unit
// axes that are orthogonal.
CYPHER_NODISCARD geometry_status_t BrushTransform_TryMakeShear(
    math::vec3d_t pivot,
    math::vec3d_t shearAxis,
    math::vec3d_t sourceAxis,
    math::f64 factor,
    math::affine3d_t *pTransformOut ) noexcept;

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

// Transforms every listed brush as one atomic preview step. Singular or
// non-finite transforms are refused (DEGENERATE / NUMERIC_FAILURE); a
// result that leaves the coordinate limit fails validation. On failure the
// previews are unchanged.
CYPHER_NODISCARD geometry_status_t BrushTransform_TryApply(
    geometry_transaction_t *pTransaction,
    common::span_t<const geometry_source_id_t> brushIds,
    const math::affine3d_t &transform,
    geometry_texture_lock_t lock ) noexcept;

// Moves one side `distance` along its outward normal (positive grows the
// brush). With GEOMETRY_LOCKED the side's projection moves with it;
// WORLD_LOCKED leaves it in place. INVALID_ARGUMENT when the side does not
// belong to the brush.
CYPHER_NODISCARD geometry_status_t BrushTransform_TryMoveSide(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    geometry_source_id_t sideId,
    math::f64 distance,
    geometry_texture_lock_t lock ) noexcept;

// Replaces one side's plane outright (arbitrary face tilt/rotation). The
// side keeps its ID; its projection is re-based onto the new orientation,
// carried with the plane change under GEOMETRY_LOCKED.
CYPHER_NODISCARD geometry_status_t BrushTransform_TrySetSidePlane(
    geometry_transaction_t *pTransaction,
    geometry_source_id_t brushId,
    geometry_source_id_t sideId,
    math::planed_t plane,
    geometry_texture_lock_t lock ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_BRUSH_TRANSFORM_H
