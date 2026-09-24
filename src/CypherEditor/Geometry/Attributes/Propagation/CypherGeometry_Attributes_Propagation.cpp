//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Attributes_Propagation.cpp
//  Purpose: Implements texture lock, projection re-basing, and donor
//           selection for brush-side attributes.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Attributes_Propagation.h"

namespace cypher::editor::geometry
{

namespace
{

using math::f64;
using math::vec3d_t;

// Faces whose normal is within ~0.5 degrees of the mapping normal keep the
// mapping's in-plane axes untouched, so repeated re-basing is idempotent.
constexpr f64 cAlignedCosine = 0.99996;

// Projections this close to edge-on are rebuilt from V instead of U.
constexpr f64 cUsableAxisLengthSq = 1.0e-6;

bool_t TryUnit( vec3d_t v, vec3d_t *pOut ) noexcept
{
    return math::Vec3d_TryNormalize( v, 1.0e-12, pOut, nullptr );
}

vec3d_t RejectFrom( vec3d_t v, vec3d_t unitAxis ) noexcept
{
    return math::Vec3d_Subtract( v, math::Vec3d_Scale( unitAxis, math::Vec3d_Dot( v, unitAxis ) ) );
}

} // namespace

geometry_status_t AttributePropagation_TryRebaseProjection(
    const geometry_numerical_policy_t &policy,
    const math::planar_uv_mappingd_t &source,
    vec3d_t faceNormal,
    math::planar_uv_mappingd_t *pMappingOut ) noexcept
{
    if ( pMappingOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pMappingOut = source;
    vec3d_t n{};
    if ( !math::Vec3d_IsFinite( faceNormal ) || !TryUnit( faceNormal, &n ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    math::planar_uv_mappingd_t mapping = source;
    const f64 alignment = math::Vec3d_Dot( source.normal, n );
    if ( math::Scalar_Abs( alignment ) >= cAlignedCosine ) {
        // Same plane orientation (or its reverse): keep U and V, and make
        // the basis exactly orthonormal to the face.
        vec3d_t u{};
        vec3d_t v{};
        if ( !TryUnit( RejectFrom( source.uAxis, n ), &u ) ) {
            return geometry_status_t::DEGENERATE;
        }
        if ( !TryUnit( RejectFrom( RejectFrom( source.vAxis, n ), u ), &v ) ) {
            return geometry_status_t::DEGENERATE;
        }
        mapping.uAxis = u;
        mapping.vAxis = v;
        mapping.normal = n;
    } else {
        // Different orientation: project U into the face, or V if U is
        // edge-on, and rebuild the other axis keeping handedness.
        const vec3d_t uIn = RejectFrom( source.uAxis, n );
        const vec3d_t vIn = RejectFrom( source.vAxis, n );
        const f64 handedness = math::Vec3d_Dot(
            math::Vec3d_Cross( source.uAxis, source.vAxis ), source.normal ) < 0.0 ? -1.0 : 1.0;
        vec3d_t u{};
        vec3d_t v{};
        if ( math::Vec3d_LengthSquared( uIn ) >= cUsableAxisLengthSq && TryUnit( uIn, &u ) ) {
            v = math::Vec3d_Scale( math::Vec3d_Cross( n, u ), handedness );
        } else if ( math::Vec3d_LengthSquared( vIn ) >= cUsableAxisLengthSq && TryUnit( vIn, &v ) ) {
            u = math::Vec3d_Scale( math::Vec3d_Cross( v, n ), handedness );
        } else {
            return geometry_status_t::DEGENERATE;
        }
        mapping.uAxis = u;
        mapping.vAxis = v;
        mapping.normal = n;
    }

    geometry_brush_side_attributes_t probe{};
    probe.uvProjection = mapping;
    const geometry_status_t status = BrushSideAttributes_Validate( policy, probe );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    *pMappingOut = mapping;
    return geometry_status_t::OK;
}

geometry_status_t AttributePropagation_TryTransformProjection(
    const geometry_numerical_policy_t &policy,
    const math::planar_uv_mappingd_t &source,
    const math::affine3d_t &transform,
    geometry_texture_lock_t lock,
    vec3d_t transformedFaceNormal,
    math::planar_uv_mappingd_t *pMappingOut ) noexcept
{
    if ( pMappingOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pMappingOut = source;
    if ( lock == geometry_texture_lock_t::WORLD_LOCKED ) {
        return AttributePropagation_TryRebaseProjection(
            policy, source, transformedFaceNormal, pMappingOut );
    }
    if ( lock != geometry_texture_lock_t::GEOMETRY_LOCKED ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Affine3d_IsFinite( transform ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    math::planar_uv_mappingd_t moved = source;
    moved.origin = math::Affine3d_TransformPoint( transform, source.origin );

    const vec3d_t u = math::Affine3d_TransformDirection( transform, source.uAxis );
    const vec3d_t v = math::Affine3d_TransformDirection( transform, source.vAxis );
    f64 uLength = 0.0;
    f64 vLength = 0.0;
    vec3d_t uUnit{};
    vec3d_t vUnit{};
    if ( !math::Vec3d_TryNormalize( u, 1.0e-12, &uUnit, &uLength ) ||
         !math::Vec3d_TryNormalize( v, 1.0e-12, &vUnit, &vLength ) ) {
        return geometry_status_t::DEGENERATE;
    }
    // A texel that covered w world units now covers w * stretch.
    moved.worldUnitsPerUv.x = source.worldUnitsPerUv.x * uLength;
    moved.worldUnitsPerUv.y = source.worldUnitsPerUv.y * vLength;

    vec3d_t transformedNormal{};
    if ( !math::Affine3d_TryTransformNormal(
             transform, source.normal, policy.fAbsoluteDistanceTolerance, &transformedNormal ) ||
         !TryUnit( transformedNormal, &transformedNormal ) ) {
        return geometry_status_t::DEGENERATE;
    }
    vec3d_t vOrtho{};
    if ( !TryUnit( RejectFrom( RejectFrom( vUnit, transformedNormal ), uUnit ), &vOrtho ) ) {
        return geometry_status_t::DEGENERATE;
    }
    moved.uAxis = uUnit;
    moved.vAxis = vOrtho;
    moved.normal = transformedNormal;
    return AttributePropagation_TryRebaseProjection(
        policy, moved, transformedFaceNormal, pMappingOut );
}

geometry_status_t AttributePropagation_TrySelectForFace(
    const geometry_numerical_policy_t &policy,
    const geometry_attribute_candidate_t *pCandidates,
    common::usize cCandidates,
    geometry_source_id_t preferredSideId,
    bool_t bFlipped,
    vec3d_t faceNormal,
    geometry_brush_side_attributes_t *pAttributesOut ) noexcept
{
    if ( pAttributesOut == nullptr || ( pCandidates == nullptr && cCandidates > 0u ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pAttributesOut = BrushSideAttributes_MakeDefault();

    const geometry_attribute_candidate_t *pChosen = nullptr;
    if ( GeometrySourceId_IsValid( preferredSideId ) ) {
        for ( common::usize i = 0u; i < cCandidates; ++i ) {
            if ( pCandidates[i].sideId.value == preferredSideId.value &&
                 pCandidates[i].pAttributes != nullptr ) {
                pChosen = &pCandidates[i];
                break;
            }
        }
    }
    if ( pChosen == nullptr ) {
        const vec3d_t target = bFlipped ? math::Vec3d_Negate( faceNormal ) : faceNormal;
        f64 bestDot = -2.0;
        for ( common::usize i = 0u; i < cCandidates; ++i ) {
            if ( pCandidates[i].pAttributes == nullptr ) {
                continue;
            }
            const f64 dot = math::Vec3d_Dot( pCandidates[i].outwardNormal, target );
            if ( dot > bestDot ) {
                bestDot = dot;
                pChosen = &pCandidates[i];
            }
        }
    }

    geometry_brush_side_attributes_t attributes =
        pChosen != nullptr ? *pChosen->pAttributes : BrushSideAttributes_MakeDefault();
    const geometry_status_t status = AttributePropagation_TryRebaseProjection(
        policy, attributes.uvProjection, faceNormal, &attributes.uvProjection );
    if ( status != geometry_status_t::OK ) {
        return status;
    }
    *pAttributesOut = attributes;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
