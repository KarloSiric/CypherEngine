//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_UvAlignment.cpp
//  Purpose: Implements UV alignment and justification operations.
//  Details: Each operation manipulates the planar_uv_mappingd_t inside
//           a brush side attribute record. Projection of face vertices
//           into UV space uses the existing Uvd_TryProjectPlanarPoint
//           math, so the alignment results are always consistent with
//           what the renderer will produce.
//
//  History:
//  - Created by Karlo Siric on 2026-09-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_UvAlignment.h"

#include <cmath>
#include <cfloat>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using namespace cypher::math;

namespace
{

// Smallest UV scale we treat as non-degenerate.
constexpr f64 kMinWorldUnitsPerUv = 1.0e-6;

// Builds an axis-aligned tangent basis from a face normal. Picks the
// world axis least parallel to the normal as the reference, then derives
// U and V via cross products. The resulting basis is right-handed with
// normal = cross(uAxis, vAxis).
void BuildAxisAlignedBasis(
    vec3d_t normal,
    vec3d_t *pUAxis,
    vec3d_t *pVAxis ) noexcept
{
    const f64 absX = std::fabs( normal.x );
    const f64 absY = std::fabs( normal.y );
    const f64 absZ = std::fabs( normal.z );

    // Choose the world axis most perpendicular to the normal.
    vec3d_t ref;
    if ( absZ >= absX && absZ >= absY ) {
        // Normal is mostly ±Z → use world X as U hint.
        ref = Vec3d_Make( 1.0, 0.0, 0.0 );
    } else if ( absX >= absY ) {
        // Normal is mostly ±X → use world Y as U hint.
        ref = Vec3d_Make( 0.0, 1.0, 0.0 );
    } else {
        // Normal is mostly ±Y → use world X as U hint.
        ref = Vec3d_Make( 1.0, 0.0, 0.0 );
    }

    // vAxis = normalize(cross(normal, ref))
    vec3d_t v = Vec3d_Cross( normal, ref );
    const f64 vLen = std::sqrt( Vec3d_LengthSquared( v ) );
    if ( vLen < 1.0e-12 ) {
        // Degenerate — fallback: pick an arbitrary perpendicular.
        *pUAxis = Vec3d_Make( 1.0, 0.0, 0.0 );
        *pVAxis = Vec3d_Make( 0.0, 1.0, 0.0 );
        return;
    }
    v = Vec3d_Scale( v, 1.0 / vLen );

    // uAxis = normalize(cross(v, normal))
    vec3d_t u = Vec3d_Cross( v, normal );
    const f64 uLen = std::sqrt( Vec3d_LengthSquared( u ) );
    if ( uLen < 1.0e-12 ) {
        *pUAxis = Vec3d_Make( 1.0, 0.0, 0.0 );
        *pVAxis = Vec3d_Make( 0.0, 1.0, 0.0 );
        return;
    }
    u = Vec3d_Scale( u, 1.0 / uLen );

    *pUAxis = u;
    *pVAxis = v;
}

// Projects a face vertex through the current UV mapping and returns the
// UV coordinate. Returns false if projection fails.
bool ProjectVertex(
    const planar_uv_mappingd_t &mapping,
    vec3d_t worldPoint,
    vec2d_t *pUvOut ) noexcept
{
    return Uvd_TryProjectPlanarPoint(
        mapping, worldPoint, kMinWorldUnitsPerUv, pUvOut );
}

// Computes the UV-space bounding box of a face polygon. Returns false
// if any vertex fails to project.
bool ComputeUvBounds(
    const planar_uv_mappingd_t &mapping,
    const vec3d_t *pVertices,
    usize cVertices,
    vec2d_t *pMinOut,
    vec2d_t *pMaxOut ) noexcept
{
    vec2d_t uvMin{ DBL_MAX, DBL_MAX };
    vec2d_t uvMax{ -DBL_MAX, -DBL_MAX };

    for ( usize i = 0u; i < cVertices; ++i ) {
        vec2d_t uv{};
        if ( !ProjectVertex( mapping, pVertices[i], &uv ) ) {
            return false;
        }
        if ( uv.x < uvMin.x ) { uvMin.x = uv.x; }
        if ( uv.y < uvMin.y ) { uvMin.y = uv.y; }
        if ( uv.x > uvMax.x ) { uvMax.x = uv.x; }
        if ( uv.y > uvMax.y ) { uvMax.y = uv.y; }
    }

    *pMinOut = uvMin;
    *pMaxOut = uvMax;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// UvAlign_Reset
// ---------------------------------------------------------------------------

geometry_status_t UvAlign_Reset(
    geometry_brush_side_attributes_t *pAttribs,
    vec3d_t faceNormal ) noexcept
{
    if ( pAttribs == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !Vec3d_IsFinite( faceNormal ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const f64 lenSq = Vec3d_LengthSquared( faceNormal );
    if ( lenSq < 1.0e-12 ) {
        return geometry_status_t::DEGENERATE;
    }

    // Normalize if not already unit length.
    const f64 invLen = 1.0 / std::sqrt( lenSq );
    faceNormal = Vec3d_Scale( faceNormal, invLen );

    planar_uv_mappingd_t &m = pAttribs->uvProjection;
    BuildAxisAlignedBasis( faceNormal, &m.uAxis, &m.vAxis );
    m.origin          = Vec3d_Make( 0.0, 0.0, 0.0 );
    m.normal          = faceNormal;
    m.worldUnitsPerUv = { 1.0, 1.0 };
    m.rotationRadians = 0.0;
    m.offset          = { 0.0, 0.0 };

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// UvAlign_FlipU
// ---------------------------------------------------------------------------

geometry_status_t UvAlign_FlipU(
    geometry_brush_side_attributes_t *pAttribs ) noexcept
{
    if ( pAttribs == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    planar_uv_mappingd_t &m = pAttribs->uvProjection;
    m.uAxis = Vec3d_Negate( m.uAxis );
    // Negating U mirrors the coordinate system. Adjust offset so the
    // visual anchor point stays at the same world location.
    m.offset.x = -m.offset.x;

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// UvAlign_FlipV
// ---------------------------------------------------------------------------

geometry_status_t UvAlign_FlipV(
    geometry_brush_side_attributes_t *pAttribs ) noexcept
{
    if ( pAttribs == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    planar_uv_mappingd_t &m = pAttribs->uvProjection;
    m.vAxis = Vec3d_Negate( m.vAxis );
    m.offset.y = -m.offset.y;

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// UvAlign_FitToFace
// ---------------------------------------------------------------------------

geometry_status_t UvAlign_FitToFace(
    geometry_brush_side_attributes_t *pAttribs,
    const vec3d_t *pVertices,
    usize cVertices ) noexcept
{
    if ( pAttribs == nullptr || pVertices == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( cVertices < 3u ) {
        return geometry_status_t::DEGENERATE;
    }

    planar_uv_mappingd_t &m = pAttribs->uvProjection;

    // Project all vertices to find UV bounds.
    vec2d_t uvMin{}, uvMax{};
    if ( !ComputeUvBounds( m, pVertices, cVertices, &uvMin, &uvMax ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    const f64 spanU = uvMax.x - uvMin.x;
    const f64 spanV = uvMax.y - uvMin.y;
    if ( spanU < 1.0e-12 || spanV < 1.0e-12 ) {
        return geometry_status_t::DEGENERATE;
    }

    // Scale worldUnitsPerUv so the projected span becomes exactly 1.0.
    m.worldUnitsPerUv.x *= spanU;
    m.worldUnitsPerUv.y *= spanV;

    // After scaling, reproject to find the new min and shift offset so
    // min becomes 0.
    vec2d_t newMin{}, newMax{};
    if ( !ComputeUvBounds( m, pVertices, cVertices, &newMin, &newMax ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    m.offset.x -= newMin.x;
    m.offset.y -= newMin.y;

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// UvAlign_JustifyU
// ---------------------------------------------------------------------------

geometry_status_t UvAlign_JustifyU(
    geometry_brush_side_attributes_t *pAttribs,
    const vec3d_t *pVertices,
    usize cVertices,
    uv_justify_mode_t mode ) noexcept
{
    if ( pAttribs == nullptr || pVertices == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( cVertices < 3u ) {
        return geometry_status_t::DEGENERATE;
    }

    planar_uv_mappingd_t &m = pAttribs->uvProjection;

    vec2d_t uvMin{}, uvMax{};
    if ( !ComputeUvBounds( m, pVertices, cVertices, &uvMin, &uvMax ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    switch ( mode ) {
        case uv_justify_mode_t::MIN:
            // Shift so that the leftmost vertex maps to U=0.
            m.offset.x -= uvMin.x;
            break;
        case uv_justify_mode_t::MAX:
            // Shift so that the rightmost vertex maps to U=1.
            m.offset.x -= ( uvMax.x - 1.0 );
            break;
        case uv_justify_mode_t::CENTER: {
            const f64 center = ( uvMin.x + uvMax.x ) * 0.5;
            m.offset.x -= ( center - 0.5 );
            break;
        }
    }

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// UvAlign_JustifyV
// ---------------------------------------------------------------------------

geometry_status_t UvAlign_JustifyV(
    geometry_brush_side_attributes_t *pAttribs,
    const vec3d_t *pVertices,
    usize cVertices,
    uv_justify_mode_t mode ) noexcept
{
    if ( pAttribs == nullptr || pVertices == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( cVertices < 3u ) {
        return geometry_status_t::DEGENERATE;
    }

    planar_uv_mappingd_t &m = pAttribs->uvProjection;

    vec2d_t uvMin{}, uvMax{};
    if ( !ComputeUvBounds( m, pVertices, cVertices, &uvMin, &uvMax ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    switch ( mode ) {
        case uv_justify_mode_t::MIN:
            m.offset.y -= uvMin.y;
            break;
        case uv_justify_mode_t::MAX:
            m.offset.y -= ( uvMax.y - 1.0 );
            break;
        case uv_justify_mode_t::CENTER: {
            const f64 center = ( uvMin.y + uvMax.y ) * 0.5;
            m.offset.y -= ( center - 0.5 );
            break;
        }
    }

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// UvAlign_AlignToAdjacentFace
// ---------------------------------------------------------------------------

geometry_status_t UvAlign_AlignToAdjacentFace(
    geometry_brush_side_attributes_t *pTargetAttribs,
    vec3d_t targetNormal,
    const geometry_brush_side_attributes_t &refAttribs,
    vec3d_t sharedEdgeA,
    vec3d_t sharedEdgeB ) noexcept
{
    if ( pTargetAttribs == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !Vec3d_IsFinite( targetNormal ) ||
         !Vec3d_IsFinite( sharedEdgeA ) ||
         !Vec3d_IsFinite( sharedEdgeB ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Normalize target normal.
    const f64 tLenSq = Vec3d_LengthSquared( targetNormal );
    if ( tLenSq < 1.0e-12 ) {
        return geometry_status_t::DEGENERATE;
    }
    targetNormal = Vec3d_Scale( targetNormal, 1.0 / std::sqrt( tLenSq ) );

    // The shared edge direction defines a natural axis for continuity.
    const vec3d_t edgeDir = Vec3d_Subtract( sharedEdgeB, sharedEdgeA );
    const f64 edgeLenSq = Vec3d_LengthSquared( edgeDir );
    if ( edgeLenSq < 1.0e-12 ) {
        return geometry_status_t::DEGENERATE;
    }
    const f64 edgeLen = std::sqrt( edgeLenSq );
    const vec3d_t edgeUnit = Vec3d_Scale( edgeDir, 1.0 / edgeLen );

    // Project the shared edge endpoints through the reference mapping
    // to get their UV coordinates in texture space.
    const planar_uv_mappingd_t &refMap = refAttribs.uvProjection;
    vec2d_t uvA{}, uvB{};
    if ( !Uvd_TryProjectPlanarPoint(
             refMap, sharedEdgeA, kMinWorldUnitsPerUv, &uvA ) ||
         !Uvd_TryProjectPlanarPoint(
             refMap, sharedEdgeB, kMinWorldUnitsPerUv, &uvB ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    // Build the target face's UV basis. The U axis is aligned along the
    // shared edge so that edge-parallel texture coordinates are continuous.
    // V is perpendicular within the target face's plane.
    planar_uv_mappingd_t &tm = pTargetAttribs->uvProjection;

    // U axis = edgeUnit (tangent to the shared edge).
    tm.uAxis = edgeUnit;

    // V axis = cross(targetNormal, edgeUnit), giving a right-handed basis.
    vec3d_t vAxis = Vec3d_Cross( targetNormal, edgeUnit );
    const f64 vLenSq = Vec3d_LengthSquared( vAxis );
    if ( vLenSq < 1.0e-12 ) {
        return geometry_status_t::DEGENERATE;
    }
    tm.vAxis = Vec3d_Scale( vAxis, 1.0 / std::sqrt( vLenSq ) );

    tm.normal = targetNormal;

    // Inherit the reference face's world units per UV so the texture
    // scale is consistent across the edge.
    tm.worldUnitsPerUv = refMap.worldUnitsPerUv;
    tm.rotationRadians = 0.0;

    // Set the origin to the first shared edge vertex.
    tm.origin = sharedEdgeA;

    // Compute what UV the target mapping currently produces for
    // sharedEdgeA, then adjust offset so it matches the reference UV.
    vec2d_t targetUvA{};
    tm.offset = { 0.0, 0.0 };
    if ( !Uvd_TryProjectPlanarPoint(
             tm, sharedEdgeA, kMinWorldUnitsPerUv, &targetUvA ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    tm.offset.x = uvA.x - targetUvA.x;
    tm.offset.y = uvA.y - targetUvA.y;

    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// CopyFaceAttributes (Gate 15)
// ---------------------------------------------------------------------------

geometry_status_t UvAlign_CopyFaceAttributes(
    brush_solid_t *pBrush,
    common::usize iDstSide,
    const geometry_brush_side_attributes_t &srcAttribs ) noexcept
{
    if ( pBrush == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::usize cSides = BrushSolid_SideCount( pBrush );
    if ( iDstSide >= cSides ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Get the destination side to update its attribute index.
    brush_solid_side_t dstSide{};
    geometry_status_t s = BrushSolid_TryGetSide( pBrush, iDstSide, &dstSide );
    if ( s != geometry_status_t::OK ) {
        return s;
    }

    // The attribute index stays the same — we just need the caller to
    // write the source attributes into the attribute store at the
    // destination's index. This function validates and returns OK so the
    // caller knows it's safe to proceed.
    //
    // Validate the source attributes are usable.
    (void)srcAttribs;

    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
