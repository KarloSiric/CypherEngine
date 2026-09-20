//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Mathlib/CypherMath_UV.cpp
//  Purpose: Implements planar UV projection for material authoring tools.
//  Details: Basis construction honors an up hint when possible and projection
//           supports mirroring through signed world-units-per-UV components.
//
//  History:
//  - Created by Karlo Siric on 2026-08-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMath_UV.h"

#include "CypherCommon_Assert.h"

#include <cmath>

namespace cypher::math
{

namespace
{

bool_t MappingIsFinite( planar_uv_mapping_t mapping ) noexcept
{
    return Vec3_IsFinite( mapping.origin ) && Vec3_IsFinite( mapping.uAxis ) &&
           Vec3_IsFinite( mapping.vAxis ) && Vec3_IsFinite( mapping.normal ) &&
           Vec2_IsFinite( mapping.worldUnitsPerUv ) &&
           Scalar_IsFinite( mapping.rotation.radians ) &&
           Vec2_IsFinite( mapping.offset );
}

vec2_t Rotate2D( vec2_t value, f32 sine, f32 cosine ) noexcept
{
    return Vec2_Make(
        cosine * value.x - sine * value.y,
        sine * value.x + cosine * value.y );
}

bool_t MappingIsFiniteD( planar_uv_mappingd_t mapping ) noexcept
{
    return Vec3d_IsFinite( mapping.origin ) && Vec3d_IsFinite( mapping.uAxis ) &&
           Vec3d_IsFinite( mapping.vAxis ) && Vec3d_IsFinite( mapping.normal ) &&
           Vec2d_IsFinite( mapping.worldUnitsPerUv ) &&
           Scalar_IsFinite( mapping.rotationRadians ) &&
           Vec2d_IsFinite( mapping.offset );
}

vec2d_t Rotate2DD( vec2d_t value, f64 sine, f64 cosine ) noexcept
{
    return Vec2d_Make(
        cosine * value.x - sine * value.y,
        sine * value.x + cosine * value.y );
}

} // namespace

bool_t Uv_TryBuildPlanarMapping(
    vec3_t origin,
    vec3_t surfaceNormal,
    vec3_t upHint,
    vec2_t worldUnitsPerUv,
    angle_t rotation,
    vec2_t offset,
    f32 minimumLength,
    planar_uv_mapping_t *pMapping ) noexcept
{
    const bool_t bValidOutput = pMapping != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Uv_TryBuildPlanarMapping requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pMapping = {};
    if ( !Vec3_IsFinite( origin ) || !Vec3_IsFinite( surfaceNormal ) ||
         !Vec3_IsFinite( upHint ) || !Vec2_IsFinite( worldUnitsPerUv ) ||
         !Scalar_IsFinite( rotation.radians ) || !Vec2_IsFinite( offset ) ||
         minimumLength < 0.0f || !Scalar_IsFinite( minimumLength ) ||
         std::abs( worldUnitsPerUv.x ) <= minimumLength ||
         std::abs( worldUnitsPerUv.y ) <= minimumLength ) {
        return false;
    }

    vec3_t normal{};
    if ( !Vec3_TryNormalize(
             surfaceNormal, minimumLength, &normal, nullptr ) ) {
        return false;
    }
    vec3_t vAxis{};

    // Remove the normal component from the hint so V lies in the surface
    // plane. Degenerate hints fall back to a deterministic orthonormal basis.
    const vec3_t projectedUp = Vec3_RejectFromUnit( upHint, normal );
    vec3_t uAxis{};
    if ( Vec3_TryNormalize(
             projectedUp, minimumLength, &vAxis, nullptr ) ) {
        uAxis = Vec3_Cross( vAxis, normal );
    } else {
        Vec3_BuildOrthonormalBasis( normal, &uAxis, &vAxis );
    }
    *pMapping = {
        origin,
        uAxis,
        vAxis,
        normal,
        worldUnitsPerUv,
        rotation,
        offset
    };
    return MappingIsFinite( *pMapping );
}

bool_t Uv_TryProjectPlanarPoint(
    planar_uv_mapping_t mapping,
    vec3_t worldPoint,
    f32 minimumAbsWorldUnitsPerUv,
    vec2_t *pUv ) noexcept
{
    const bool_t bValidOutput = pUv != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Uv_TryProjectPlanarPoint requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pUv = CY_VEC2_ZERO;
    if ( !MappingIsFinite( mapping ) || !Vec3_IsFinite( worldPoint ) ||
         minimumAbsWorldUnitsPerUv < 0.0f ||
         !Scalar_IsFinite( minimumAbsWorldUnitsPerUv ) ||
         std::abs( mapping.worldUnitsPerUv.x ) <= minimumAbsWorldUnitsPerUv ||
         std::abs( mapping.worldUnitsPerUv.y ) <= minimumAbsWorldUnitsPerUv ) {
        return false;
    }

    const vec3_t relative = Vec3_Subtract( worldPoint, mapping.origin );

    // Signed world-units-per-UV values intentionally support mirrored mappings.
    const vec2_t base = Vec2_Make(
        Vec3_Dot( relative, mapping.uAxis ) / mapping.worldUnitsPerUv.x,
        Vec3_Dot( relative, mapping.vAxis ) / mapping.worldUnitsPerUv.y );
    f32 sine = 0.0f;
    f32 cosine = 0.0f;
    Scalar_SinCos( mapping.rotation.radians, &sine, &cosine );
    *pUv = Vec2_Add( Rotate2D( base, sine, cosine ), mapping.offset );
    return Vec2_IsFinite( *pUv );
}

bool_t Uv_TryUnprojectPlanarPoint(
    planar_uv_mapping_t mapping,
    vec2_t uv,
    f32 normalOffset,
    f32 minimumAbsWorldUnitsPerUv,
    vec3_t *pWorldPoint ) noexcept
{
    const bool_t bValidOutput = pWorldPoint != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Uv_TryUnprojectPlanarPoint requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pWorldPoint = CY_VEC3_ZERO;
    if ( !MappingIsFinite( mapping ) || !Vec2_IsFinite( uv ) ||
         !Scalar_IsFinite( normalOffset ) || minimumAbsWorldUnitsPerUv < 0.0f ||
         !Scalar_IsFinite( minimumAbsWorldUnitsPerUv ) ||
         std::abs( mapping.worldUnitsPerUv.x ) <= minimumAbsWorldUnitsPerUv ||
         std::abs( mapping.worldUnitsPerUv.y ) <= minimumAbsWorldUnitsPerUv ) {
        return false;
    }

    f32 sine = 0.0f;
    f32 cosine = 0.0f;
    Scalar_SinCos( -mapping.rotation.radians, &sine, &cosine );

    // Undo authored offset and rotation before rebuilding the world-space
    // position from the planar basis.
    const vec2_t base = Rotate2D(
        Vec2_Subtract( uv, mapping.offset ), sine, cosine );
    vec3_t world = Vec3_MulAdd(
        mapping.origin, mapping.uAxis,
        base.x * mapping.worldUnitsPerUv.x );
    world = Vec3_MulAdd(
        world, mapping.vAxis, base.y * mapping.worldUnitsPerUv.y );
    world = Vec3_MulAdd( world, mapping.normal, normalOffset );
    if ( !Vec3_IsFinite( world ) ) {
        return false;
    }
    *pWorldPoint = world;
    return true;
}

//==========================================================================
// Binary64 authoring UV mapping
//==========================================================================

bool_t Uvd_TryBuildPlanarMapping(
    vec3d_t origin,
    vec3d_t surfaceNormal,
    vec3d_t upHint,
    vec2d_t worldUnitsPerUv,
    f64 rotationRadians,
    vec2d_t offset,
    f64 minimumLength,
    planar_uv_mappingd_t *pMapping ) noexcept
{
    const bool_t bValidOutput = pMapping != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Uvd_TryBuildPlanarMapping requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pMapping = {};
    if ( !Vec3d_IsFinite( origin ) || !Vec3d_IsFinite( surfaceNormal ) ||
         !Vec3d_IsFinite( upHint ) || !Vec2d_IsFinite( worldUnitsPerUv ) ||
         !Scalar_IsFinite( rotationRadians ) || !Vec2d_IsFinite( offset ) ||
         minimumLength < 0.0 || !Scalar_IsFinite( minimumLength ) ||
         std::abs( worldUnitsPerUv.x ) <= minimumLength ||
         std::abs( worldUnitsPerUv.y ) <= minimumLength ) {
        return false;
    }

    vec3d_t normal{};
    if ( !Vec3d_TryNormalize( surfaceNormal, minimumLength, &normal, nullptr ) ) {
        return false;
    }

    // Remove the normal component from the hint so V lies in the surface
    // plane. A hint parallel to the normal leaves nothing to project, so fall
    // back to a deterministic basis rather than an arbitrary one -- the same
    // face must produce the same UV basis on every rebuild or its texture
    // would jump between sessions.
    const vec3d_t projectedUp = Vec3d_RejectFromUnit( upHint, normal );
    vec3d_t vAxis{};
    vec3d_t uAxis{};
    if ( Vec3d_TryNormalize( projectedUp, minimumLength, &vAxis, nullptr ) ) {
        uAxis = Vec3d_Cross( vAxis, normal );
    } else {
        Vec3d_BuildOrthonormalBasis( normal, &uAxis, &vAxis );
    }
    *pMapping = {
        origin,
        uAxis,
        vAxis,
        normal,
        worldUnitsPerUv,
        rotationRadians,
        offset
    };
    return MappingIsFiniteD( *pMapping );
}

bool_t Uvd_TryProjectPlanarPoint(
    planar_uv_mappingd_t mapping,
    vec3d_t worldPoint,
    f64 minimumAbsWorldUnitsPerUv,
    vec2d_t *pUv ) noexcept
{
    const bool_t bValidOutput = pUv != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Uvd_TryProjectPlanarPoint requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pUv = CY_VEC2D_ZERO;
    if ( !MappingIsFiniteD( mapping ) || !Vec3d_IsFinite( worldPoint ) ||
         minimumAbsWorldUnitsPerUv < 0.0 ||
         !Scalar_IsFinite( minimumAbsWorldUnitsPerUv ) ||
         std::abs( mapping.worldUnitsPerUv.x ) <= minimumAbsWorldUnitsPerUv ||
         std::abs( mapping.worldUnitsPerUv.y ) <= minimumAbsWorldUnitsPerUv ) {
        return false;
    }

    // Subtracting the origin first is what preserves world-locked precision:
    // the difference is small even when the brush sits far from the world
    // origin, so the division below never operates on cancelled-out magnitudes.
    const vec3d_t relative = Vec3d_Subtract( worldPoint, mapping.origin );

    // Signed world-units-per-UV values intentionally support mirrored mappings.
    const vec2d_t base = Vec2d_Make(
        Vec3d_Dot( relative, mapping.uAxis ) / mapping.worldUnitsPerUv.x,
        Vec3d_Dot( relative, mapping.vAxis ) / mapping.worldUnitsPerUv.y );
    f64 sine = 0.0;
    f64 cosine = 0.0;
    Scalar_SinCos( mapping.rotationRadians, &sine, &cosine );
    *pUv = Vec2d_Add( Rotate2DD( base, sine, cosine ), mapping.offset );
    return Vec2d_IsFinite( *pUv );
}

bool_t Uvd_TryUnprojectPlanarPoint(
    planar_uv_mappingd_t mapping,
    vec2d_t uv,
    f64 normalOffset,
    f64 minimumAbsWorldUnitsPerUv,
    vec3d_t *pWorldPoint ) noexcept
{
    const bool_t bValidOutput = pWorldPoint != nullptr;
    CY_ASSERT_MSG( bValidOutput,
        "Uvd_TryUnprojectPlanarPoint requires output storage." );
    if ( !bValidOutput ) {
        return false;
    }
    *pWorldPoint = CY_VEC3D_ZERO;
    if ( !MappingIsFiniteD( mapping ) || !Vec2d_IsFinite( uv ) ||
         !Scalar_IsFinite( normalOffset ) || minimumAbsWorldUnitsPerUv < 0.0 ||
         !Scalar_IsFinite( minimumAbsWorldUnitsPerUv ) ||
         std::abs( mapping.worldUnitsPerUv.x ) <= minimumAbsWorldUnitsPerUv ||
         std::abs( mapping.worldUnitsPerUv.y ) <= minimumAbsWorldUnitsPerUv ) {
        return false;
    }

    f64 sine = 0.0;
    f64 cosine = 0.0;
    Scalar_SinCos( -mapping.rotationRadians, &sine, &cosine );

    // Undo authored offset and rotation before rebuilding the world-space
    // position from the planar basis.
    const vec2d_t base = Rotate2DD(
        Vec2d_Subtract( uv, mapping.offset ), sine, cosine );
    vec3d_t world = Vec3d_MulAdd(
        mapping.origin, mapping.uAxis, base.x * mapping.worldUnitsPerUv.x );
    world = Vec3d_MulAdd(
        world, mapping.vAxis, base.y * mapping.worldUnitsPerUv.y );
    world = Vec3d_MulAdd( world, mapping.normal, normalOffset );
    if ( !Vec3d_IsFinite( world ) ) {
        return false;
    }
    *pWorldPoint = world;
    return true;
}

} // namespace cypher::math
