//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Attributes_Schema.cpp
//  Purpose: Implements brush-side attribute defaults and validation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Attributes_Schema.h"

namespace cypher::editor::geometry
{

namespace
{

using cypher::common::bool_t;
using cypher::math::f64;
using cypher::math::vec2d_t;
using cypher::math::vec3d_t;

bool_t IsWithinMagnitudeLimit( f64 value, f64 limit ) noexcept
{
    return cypher::math::Scalar_Abs( value ) <= limit;
}

bool_t IsWithinMagnitudeLimit( vec3d_t value, f64 limit ) noexcept
{
    return IsWithinMagnitudeLimit( value.x, limit ) &&
           IsWithinMagnitudeLimit( value.y, limit ) &&
           IsWithinMagnitudeLimit( value.z, limit );
}

} // namespace

geometry_brush_side_attributes_t BrushSideAttributes_MakeDefault() noexcept
{
    geometry_brush_side_attributes_t attributes{};
    attributes.material = GEOMETRY_MATERIAL_REF_UNASSIGNED;

    // A world-axis-aligned projection at one world unit per UV unit. Built
    // explicitly rather than trusting Uvd_TryBuildPlanarMapping so the default
    // cannot silently become invalid if that builder's fallback basis changes;
    // the axes below are already orthonormal and need no derivation.
    attributes.uvProjection.origin = cypher::math::CY_VEC3D_ZERO;
    attributes.uvProjection.uAxis = cypher::math::CY_VEC3D_FORWARD;
    attributes.uvProjection.vAxis = cypher::math::CY_VEC3D_LEFT;
    attributes.uvProjection.normal = cypher::math::CY_VEC3D_UP;
    attributes.uvProjection.worldUnitsPerUv = cypher::math::Vec2d_Make( 1.0, 1.0 );
    attributes.uvProjection.rotationRadians = 0.0;
    attributes.uvProjection.offset = cypher::math::CY_VEC2D_ZERO;
    return attributes;
}

geometry_status_t BrushSideAttributes_Validate(
    const geometry_numerical_policy_t &policy,
    const geometry_brush_side_attributes_t &attributes ) noexcept
{
    const cypher::math::planar_uv_mappingd_t &mapping = attributes.uvProjection;

    if ( !cypher::math::Vec3d_IsFinite( mapping.origin ) ||
         !cypher::math::Vec3d_IsFinite( mapping.uAxis ) ||
         !cypher::math::Vec3d_IsFinite( mapping.vAxis ) ||
         !cypher::math::Vec3d_IsFinite( mapping.normal ) ||
         !cypher::math::Vec2d_IsFinite( mapping.worldUnitsPerUv ) ||
         !cypher::math::Scalar_IsFinite( mapping.rotationRadians ) ||
         !cypher::math::Vec2d_IsFinite( mapping.offset ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    // Only the origin is a world position; the axes are unit directions and the
    // offset is in UV space, so neither is bounded by the coordinate limit.
    if ( !IsWithinMagnitudeLimit( mapping.origin, policy.fCoordinateMagnitudeLimit ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    // A zero or near-zero UV scale would divide by zero during projection.
    // Signed values stay legal: a negative scale is how a mirrored mapping is
    // authored, and rejecting it would forbid a legitimate authoring result.
    if ( cypher::math::Scalar_Abs( mapping.worldUnitsPerUv.x ) <=
             policy.fAbsoluteDistanceTolerance ||
         cypher::math::Scalar_Abs( mapping.worldUnitsPerUv.y ) <=
             policy.fAbsoluteDistanceTolerance ) {
        return geometry_status_t::DEGENERATE;
    }

    // The three axes must be usable as a basis. Checking unit length catches a
    // record that was hand-assembled or deserialized rather than produced by
    // Uvd_TryBuildPlanarMapping, which is exactly where a bad basis comes from.
    if ( !cypher::math::Vec3d_IsUnitLength( mapping.uAxis, policy.fUnitNormalTolerance ) ||
         !cypher::math::Vec3d_IsUnitLength( mapping.vAxis, policy.fUnitNormalTolerance ) ||
         !cypher::math::Vec3d_IsUnitLength( mapping.normal, policy.fUnitNormalTolerance ) ) {
        return geometry_status_t::DEGENERATE;
    }

    // Non-orthogonal axes still project, but unprojection would no longer be
    // its inverse, so a round trip through UV space would move the point.
    if ( cypher::math::Scalar_Abs( cypher::math::Vec3d_Dot( mapping.uAxis, mapping.vAxis ) ) >
             policy.fUnitNormalTolerance ||
         cypher::math::Scalar_Abs( cypher::math::Vec3d_Dot( mapping.uAxis, mapping.normal ) ) >
             policy.fUnitNormalTolerance ||
         cypher::math::Scalar_Abs( cypher::math::Vec3d_Dot( mapping.vAxis, mapping.normal ) ) >
             policy.fUnitNormalTolerance ) {
        return geometry_status_t::DEGENERATE;
    }

    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
