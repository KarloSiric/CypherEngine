//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_UvTools.h
//  Purpose: Declares the UV-view manipulations for a face's planar
//           projection: shift in UV space, and rotate / scale about a pivot
//           point on the face that keeps its texel fixed (the UV editor's
//           origin handle).
//  Details: A projection maps a point p to R(theta) * base(p) + offset with
//           base(p) = ((p - origin).u / su, (p - origin).v / sv). Rotating or
//           scaling changes theta or (su, sv); the offset is then solved so
//           that the pivot still maps to the UV it had, which is what makes
//           the texture turn or grow around the handle instead of around
//           the projection origin.
//
//           Shear is not offered: projections keep orthogonal u/v axes
//           (unprojection would otherwise not invert projection), so a
//           sheared texture cannot be represented; callers get UNSUPPORTED
//           from UvTools_TryShear rather than a silent approximation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_UV_TOOLS_H
#define CYPHER_EDITOR_GEOMETRY_UV_TOOLS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherGeometry_Attributes_Schema.h"
#include "CypherGeometry_Policy.h"

namespace cypher::editor::geometry
{

// Moves the texture by (du, dv) texels.
CYPHER_NODISCARD geometry_status_t UvTools_TryShift(
    geometry_brush_side_attributes_t *pRecord,
    math::vec2d_t deltaUv,
    const geometry_numerical_policy_t &policy ) noexcept;

// Rotates the texture by angleRadians about the world point `pivot` (on
// the face), whose UV is unchanged.
CYPHER_NODISCARD geometry_status_t UvTools_TryRotateAbout(
    geometry_brush_side_attributes_t *pRecord,
    math::vec3d_t pivot,
    common::f64 angleRadians,
    const geometry_numerical_policy_t &policy ) noexcept;

// Scales the texture by `factor` per UV axis about `pivot` (factor 2 makes
// the texture twice as large on the surface); negative factors mirror it.
CYPHER_NODISCARD geometry_status_t UvTools_TryScaleAbout(
    geometry_brush_side_attributes_t *pRecord,
    math::vec3d_t pivot,
    math::vec2d_t factor,
    const geometry_numerical_policy_t &policy ) noexcept;

// Always UNSUPPORTED (see the file comment).
CYPHER_NODISCARD geometry_status_t UvTools_TryShear(
    geometry_brush_side_attributes_t *pRecord,
    math::vec2d_t shear ) noexcept;

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_UV_TOOLS_H
