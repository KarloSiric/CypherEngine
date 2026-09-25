//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Workplane.cpp
//  Purpose: Implements workplane construction, coordinate mapping,
//           snapping, and ray placement.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Workplane.h"
#include "CypherGeometry_Snap.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using math::vec3d_t;

namespace
{

// Completes a frame from a normal and an in-plane direction (made exactly
// perpendicular to the normal first).
bool Frame( vec3d_t origin, vec3d_t normal, vec3d_t along, geometry_workplane_t *pOut ) noexcept
{
    vec3d_t n{}, u{};
    if ( !math::Vec3d_TryNormalize( normal, 1.0e-300, &n, nullptr ) ) { return false; }
    const vec3d_t inPlane = math::Vec3d_Subtract( along, math::Vec3d_Scale( n, math::Vec3d_Dot( along, n ) ) );
    if ( !math::Vec3d_TryNormalize( inPlane, 1.0e-300, &u, nullptr ) ) { return false; }
    pOut->origin = origin;
    pOut->normal = n;
    pOut->u = u;
    pOut->v = math::Vec3d_Cross( n, u );
    return true;
}

} // namespace

bool Workplane_IsValid( const geometry_workplane_t &p, f64 tolerance ) noexcept
{
    if ( !math::Vec3d_IsFinite( p.origin ) || !math::Vec3d_IsFinite( p.u ) || !math::Vec3d_IsFinite( p.v ) ||
         !math::Vec3d_IsFinite( p.normal ) || !std::isfinite( tolerance ) ) {
        return false;
    }
    auto unit = [&]( vec3d_t a ) noexcept { return std::fabs( math::Vec3d_LengthSquared( a ) - 1.0 ) <= tolerance; };
    const vec3d_t cross = math::Vec3d_Subtract( math::Vec3d_Cross( p.u, p.v ), p.normal );
    return unit( p.u ) && unit( p.v ) && unit( p.normal ) && std::fabs( math::Vec3d_Dot( p.u, p.v ) ) <= tolerance &&
           std::fabs( math::Vec3d_Dot( p.u, p.normal ) ) <= tolerance && math::Vec3d_LengthSquared( cross ) <= tolerance * tolerance;
}

geometry_status_t Workplane_TryFromFace( const editable_mesh_t *pMesh, geometry_mesh_face_handle_t hFace, geometry_workplane_t *pOut ) noexcept
{
    if ( pMesh == nullptr || pOut == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !EditableMesh_IsInitialized( pMesh ) ) { return geometry_status_t::NOT_INITIALIZED; }
    const mesh_face_record_t *pF = GenerationPool_Get( &pMesh->faces, hFace );
    if ( pF == nullptr ) { return geometry_status_t::STALE_HANDLE; }
    const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, pF->hOuterLoop );
    if ( pL == nullptr || pL->cHalfEdges < 3u ) { return geometry_status_t::CORRUPT_STATE; }
    vec3d_t first{}, longest{};
    f64 bestLength = -1.0;
    geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
    for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
        const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, h );
        const mesh_half_edge_record_t *pN = pH ? GenerationPool_Get( &pMesh->halfEdges, pH->hNext ) : nullptr;
        if ( pN == nullptr ) { return geometry_status_t::CORRUPT_STATE; }
        const vec3d_t a = GenerationPool_Get( &pMesh->vertices, pH->hOrigin )->position;
        const vec3d_t b = GenerationPool_Get( &pMesh->vertices, pN->hOrigin )->position;
        if ( k == 0u ) { first = a; }
        const vec3d_t e = math::Vec3d_Subtract( b, a );
        if ( math::Vec3d_LengthSquared( e ) > bestLength ) {
            bestLength = math::Vec3d_LengthSquared( e );
            longest = e;
        }
        h = pH->hNext;
    }
    geometry_workplane_t w{};
    if ( !Frame( first, pF->normal, longest, &w ) ) { return geometry_status_t::DEGENERATE; }
    *pOut = w;
    return geometry_status_t::OK;
}

geometry_status_t Workplane_TryFromPoints( vec3d_t a, vec3d_t b, vec3d_t c, geometry_workplane_t *pOut ) noexcept
{
    if ( pOut == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !math::Vec3d_IsFinite( a ) || !math::Vec3d_IsFinite( b ) || !math::Vec3d_IsFinite( c ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    const vec3d_t ab = math::Vec3d_Subtract( b, a ), ac = math::Vec3d_Subtract( c, a );
    const vec3d_t n = math::Vec3d_Cross( ab, ac );
    // Collinear relative to the triangle's size, not an absolute epsilon.
    if ( !( math::Vec3d_LengthSquared( n ) > 1.0e-24 * math::Vec3d_LengthSquared( ab ) * math::Vec3d_LengthSquared( ac ) ) ) {
        return geometry_status_t::DEGENERATE;
    }
    geometry_workplane_t w{};
    if ( !Frame( a, n, ab, &w ) ) { return geometry_status_t::DEGENERATE; }
    *pOut = w;
    return geometry_status_t::OK;
}

vec3d_t Workplane_ToLocal( const geometry_workplane_t &p, vec3d_t world ) noexcept
{
    const vec3d_t d = math::Vec3d_Subtract( world, p.origin );
    return math::Vec3d_Make( math::Vec3d_Dot( d, p.u ), math::Vec3d_Dot( d, p.v ), math::Vec3d_Dot( d, p.normal ) );
}

vec3d_t Workplane_ToWorld( const geometry_workplane_t &p, vec3d_t local ) noexcept
{
    return math::Vec3d_Add( p.origin, math::Vec3d_Add( math::Vec3d_Add( math::Vec3d_Scale( p.u, local.x ), math::Vec3d_Scale( p.v, local.y ) ),
                                                       math::Vec3d_Scale( p.normal, local.z ) ) );
}

vec3d_t Workplane_Project( const geometry_workplane_t &p, vec3d_t world ) noexcept
{
    vec3d_t local = Workplane_ToLocal( p, world );
    local.z = 0.0;
    return Workplane_ToWorld( p, local );
}

vec3d_t Workplane_SnapPoint( const geometry_workplane_t &p, vec3d_t world, f64 gridSpacing, bool bOntoPlane ) noexcept
{
    vec3d_t local = Workplane_ToLocal( p, world );
    local.x = Snap_GridScalar( local.x, gridSpacing );
    local.y = Snap_GridScalar( local.y, gridSpacing );
    local.z = bOntoPlane ? 0.0 : Snap_GridScalar( local.z, gridSpacing );
    return Workplane_ToWorld( p, local );
}

bool Workplane_TryIntersectRay( const geometry_workplane_t &p, vec3d_t rayOrigin, vec3d_t rayDirection, f64 *pTOut, vec3d_t *pHitOut ) noexcept
{
    if ( !math::Vec3d_IsFinite( rayOrigin ) || !math::Vec3d_IsFinite( rayDirection ) ) { return false; }
    const f64 denom = math::Vec3d_Dot( rayDirection, p.normal );
    if ( !( std::fabs( denom ) >= 1.0e-12 * std::sqrt( math::Vec3d_LengthSquared( rayDirection ) ) ) ) { return false; }
    const f64 t = -math::Vec3d_Dot( math::Vec3d_Subtract( rayOrigin, p.origin ), p.normal ) / denom;
    if ( !( t >= 0.0 ) || !std::isfinite( t ) ) { return false; }
    if ( pTOut ) { *pTOut = t; }
    if ( pHitOut ) { *pHitOut = math::Vec3d_Add( rayOrigin, math::Vec3d_Scale( rayDirection, t ) ); }
    return true;
}

} // namespace cypher::editor::geometry
