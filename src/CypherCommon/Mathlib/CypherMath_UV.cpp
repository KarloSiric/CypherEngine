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

} // namespace cypher::math
