//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Constraints.cpp
//  Purpose: Implements grid, angle, axis, plane, and candidate snapping.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Constraints.h"

#include <cmath>

namespace cypher::editor::geometry
{

namespace
{

using common::bool_t;
using common::u32;
using common::usize;
using math::f64;
using math::vec3d_t;

constexpr f64 cExactIntegerLimit = 9007199254740992.0; // 2^53

bool_t RanksBefore( const geometry_snap_candidate_t &a, f64 distA,
                    const geometry_snap_candidate_t &b, f64 distB ) noexcept
{
    if ( distA != distB ) {
        return distA < distB;
    }
    if ( a.priority != b.priority ) {
        return a.priority > b.priority;
    }
    if ( a.kind != b.kind ) {
        return static_cast<u32>( a.kind ) < static_cast<u32>( b.kind );
    }
    if ( a.sourceId.value != b.sourceId.value ) {
        return a.sourceId.value < b.sourceId.value;
    }
    return a.element < b.element;
}

} // namespace

geometry_status_t Constraint_TrySnapScalar(
    f64 value, f64 spacing, f64 origin, f64 *pSnappedOut ) noexcept
{
    if ( pSnappedOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pSnappedOut = value;
    if ( !math::Scalar_IsFinite( value ) || !math::Scalar_IsFinite( spacing ) ||
         !math::Scalar_IsFinite( origin ) || !( spacing > 0.0 ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const f64 cells = ( value - origin ) / spacing;
    if ( !math::Scalar_IsFinite( cells ) || math::Scalar_Abs( cells ) >= cExactIntegerLimit ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    *pSnappedOut = origin + std::floor( cells + 0.5 ) * spacing;
    return geometry_status_t::OK;
}

geometry_status_t Constraint_TrySnapPoint(
    vec3d_t point, vec3d_t spacing, vec3d_t origin, u32 axisMask, vec3d_t *pSnappedOut ) noexcept
{
    if ( pSnappedOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pSnappedOut = point;
    vec3d_t result = point;
    geometry_status_t status = geometry_status_t::OK;
    if ( status == geometry_status_t::OK && ( axisMask & 1u ) != 0u ) {
        status = Constraint_TrySnapScalar( point.x, spacing.x, origin.x, &result.x );
    }
    if ( status == geometry_status_t::OK && ( axisMask & 2u ) != 0u ) {
        status = Constraint_TrySnapScalar( point.y, spacing.y, origin.y, &result.y );
    }
    if ( status == geometry_status_t::OK && ( axisMask & 4u ) != 0u ) {
        status = Constraint_TrySnapScalar( point.z, spacing.z, origin.z, &result.z );
    }
    if ( status == geometry_status_t::OK ) {
        *pSnappedOut = result;
    }
    return status;
}

geometry_status_t Constraint_TrySnapAngle( f64 radians, f64 increment, f64 *pSnappedOut ) noexcept
{
    return Constraint_TrySnapScalar( radians, increment, 0.0, pSnappedOut );
}

u32 Constraint_DominantAxis( vec3d_t delta ) noexcept
{
    const f64 ax = math::Scalar_Abs( delta.x );
    const f64 ay = math::Scalar_Abs( delta.y );
    const f64 az = math::Scalar_Abs( delta.z );
    if ( ax >= ay && ax >= az ) {
        return 0u;
    }
    return ay >= az ? 1u : 2u;
}

vec3d_t Constraint_MaskAxes( vec3d_t delta, u32 axisMask ) noexcept
{
    return math::Vec3d_Make( ( axisMask & 1u ) != 0u ? delta.x : 0.0,
                             ( axisMask & 2u ) != 0u ? delta.y : 0.0,
                             ( axisMask & 4u ) != 0u ? delta.z : 0.0 );
}

geometry_status_t Constraint_TryProjectToPlane(
    const geometry_numerical_policy_t &policy, vec3d_t point, math::planed_t plane,
    vec3d_t *pProjectedOut ) noexcept
{
    if ( pProjectedOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pProjectedOut = point;
    if ( !math::Vec3d_IsFinite( point ) || !math::Planed_IsFinite( plane ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( !math::Planed_IsNormalized( plane, policy.fUnitNormalTolerance ) ) {
        return geometry_status_t::DEGENERATE;
    }
    *pProjectedOut = math::Vec3d_Subtract(
        point, math::Vec3d_Scale( plane.normal, math::Planed_SignedDistance( plane, point ) ) );
    return geometry_status_t::OK;
}

geometry_status_t Constraint_TryResolve(
    vec3d_t query,
    common::span_t<const geometry_snap_candidate_t> candidates,
    f64 maxDistance,
    geometry_snap_result_t *pResultOut ) noexcept
{
    if ( pResultOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pResultOut = {};
    pResultOut->position = query;
    if ( !math::Vec3d_IsFinite( query ) || !common::Span_IsValid( candidates ) ||
         !math::Scalar_IsFinite( maxDistance ) || maxDistance < 0.0 ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const f64 maxSq = maxDistance * maxDistance;
    f64 bestSq = 0.0;
    for ( usize i = 0u; i < candidates.nCount; ++i ) {
        const geometry_snap_candidate_t &candidate = candidates.pData[i];
        if ( !math::Vec3d_IsFinite( candidate.position ) ) {
            continue;
        }
        const f64 distSq = math::Vec3d_DistanceSquared( candidate.position, query );
        if ( distSq > maxSq ) {
            continue;
        }
        if ( !pResultOut->bSnapped ||
             RanksBefore( candidate, distSq, candidates.pData[pResultOut->iCandidate], bestSq ) ) {
            pResultOut->bSnapped = true;
            pResultOut->iCandidate = i;
            pResultOut->position = candidate.position;
            bestSq = distSq;
        }
    }
    pResultOut->distance = pResultOut->bSnapped ? std::sqrt( bestSq ) : 0.0;
    return geometry_status_t::OK;
}

geometry_status_t Constraint_TryGatherBrushCandidates(
    const geometry_brush_value_t *pValue,
    u32 kindMask,
    common::vector_t<geometry_snap_candidate_t> *pOut ) noexcept
{
    if ( pValue == nullptr || pOut == nullptr || pOut->pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const brush_boundary_t &boundary = pValue->boundary;
    const usize cEntry = common::Vector_Count( pOut );
    const geometry_source_id_t id = pValue->brush.sourceId;
    bool_t bOk = true;
    auto push = [&]( vec3d_t position, geometry_snap_kind_t kind, common::u8 priority,
                     usize element ) noexcept {
        if ( bOk ) {
            const geometry_snap_candidate_t candidate{ position, kind, priority, id,
                                                       static_cast<u32>( element ) };
            bOk = common::Vector_PushBack( pOut, candidate );
        }
    };
    if ( ( kindMask & SNAP_KIND_MASK_VERTEX ) != 0u ) {
        for ( usize i = 0u; i < common::Vector_Count( &boundary.vertices ); ++i ) {
            push( boundary.vertices.pData[i], geometry_snap_kind_t::VERTEX, 3u, i );
        }
    }
    if ( ( kindMask & SNAP_KIND_MASK_EDGE_MIDPOINT ) != 0u ) {
        for ( usize i = 0u; i < common::Vector_Count( &boundary.edges ); ++i ) {
            const brush_boundary_edge_t edge = boundary.edges.pData[i];
            push( math::Vec3d_Scale( math::Vec3d_Add( boundary.vertices.pData[edge.iVertex0],
                                                      boundary.vertices.pData[edge.iVertex1] ),
                                     0.5 ),
                  geometry_snap_kind_t::EDGE_MIDPOINT, 2u, i );
        }
    }
    if ( ( kindMask & SNAP_KIND_MASK_FACE_CENTER ) != 0u ) {
        for ( usize f = 0u; f < common::Vector_Count( &boundary.faces ); ++f ) {
            const brush_boundary_face_t &face = boundary.faces.pData[f];
            vec3d_t sum = math::CY_VEC3D_ZERO;
            for ( u32 k = 0u; k < face.cVertices; ++k ) {
                sum = math::Vec3d_Add(
                    sum, boundary.vertices.pData[boundary.faceVertexIndices.pData[face.iFirstIndex + k]] );
            }
            push( math::Vec3d_Scale( sum, 1.0 / static_cast<f64>( face.cVertices ) ),
                  geometry_snap_kind_t::FACE_CENTER, 1u, f );
        }
    }
    if ( ( kindMask & SNAP_KIND_MASK_BRUSH_CENTER ) != 0u ) {
        push( math::Aabbd_Center( pValue->bounds ), geometry_snap_kind_t::BRUSH_CENTER, 0u, 0u );
    }
    if ( !bOk ) {
        while ( common::Vector_Count( pOut ) > cEntry ) {
            common::Vector_PopBack( pOut );
        }
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
