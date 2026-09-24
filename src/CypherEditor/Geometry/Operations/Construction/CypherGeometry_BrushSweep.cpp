//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushSweep.cpp
//  Purpose: Implements the sweep tool.
//  Details: The path is sampled at g = k / cSegments for g in
//           [0, cIterations]; each sample is a rigid transform (rotation R,
//           translation t) of the face. A segment is the convex hull of the
//           face polygon at two consecutive samples. Hull sides are then
//           classified: the side facing back along the face normal at the
//           first sample is the start cap, the one facing along it at the
//           second sample the end cap, everything else a wall.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushSweep.h"

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherGeometry_Kernel_ConvexHull.h"
#include "CypherMath_UV.h"

#include <cmath>
#include <new>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using math::vec3d_t;

namespace
{

constexpr f64 kTwoPi = 6.28318530717958647692;

struct rigid_t {
    f64 r[9]{ 1, 0, 0, 0, 1, 0, 0, 0, 1 }; // row-major rotation
    vec3d_t t{};
};

vec3d_t Rotate( const rigid_t &x, vec3d_t v ) noexcept
{
    return math::Vec3d_Make( x.r[0] * v.x + x.r[1] * v.y + x.r[2] * v.z, x.r[3] * v.x + x.r[4] * v.y + x.r[5] * v.z,
                             x.r[6] * v.x + x.r[7] * v.y + x.r[8] * v.z );
}

vec3d_t Apply( const rigid_t &x, vec3d_t p ) noexcept { return math::Vec3d_Add( Rotate( x, p ), x.t ); }

// Rotation by angle about the unit axis through `origin` (Rodrigues).
rigid_t AboutAxis( vec3d_t origin, vec3d_t a, f64 angle ) noexcept
{
    const f64 c = std::cos( angle ), s = std::sin( angle ), k = 1.0 - c;
    rigid_t x{};
    x.r[0] = c + a.x * a.x * k;
    x.r[1] = a.x * a.y * k - a.z * s;
    x.r[2] = a.x * a.z * k + a.y * s;
    x.r[3] = a.y * a.x * k + a.z * s;
    x.r[4] = c + a.y * a.y * k;
    x.r[5] = a.y * a.z * k - a.x * s;
    x.r[6] = a.z * a.x * k - a.y * s;
    x.r[7] = a.z * a.y * k + a.x * s;
    x.r[8] = c + a.z * a.z * k;
    x.t = math::Vec3d_Subtract( origin, Rotate( x, origin ) );
    return x;
}

f64 Smoothstep( f64 s ) noexcept { return s * s * ( 3.0 - 2.0 * s ); }

// The face's transform at global path parameter g in [0, cIterations].
rigid_t At( const brush_sweep_params_t &p, vec3d_t axis, vec3d_t faceNormal, f64 g ) noexcept
{
    switch ( p.path ) {
        case brush_sweep_path_t::ARC: return AboutAxis( p.axisOrigin, axis, p.angleRadians * g );
        case brush_sweep_path_t::S_BEND: {
            // Whole iterations stack full offsets; within one, the normal part
            // is linear and the sideways part follows the S.
            f64 whole = std::floor( g );
            if ( whole >= static_cast<f64>( p.cIterations ) ) { whole = static_cast<f64>( p.cIterations ) - 1.0; }
            const f64 s = g - whole;
            const vec3d_t dn = math::Vec3d_Scale( faceNormal, math::Vec3d_Dot( p.offset, faceNormal ) );
            const vec3d_t dl = math::Vec3d_Subtract( p.offset, dn );
            rigid_t x{};
            x.t = math::Vec3d_Add( math::Vec3d_Scale( p.offset, whole ),
                                   math::Vec3d_Add( math::Vec3d_Scale( dn, s ), math::Vec3d_Scale( dl, Smoothstep( s ) ) ) );
            return x;
        }
        default: {
            rigid_t x{};
            x.t = math::Vec3d_Scale( p.offset, g );
            return x;
        }
    }
}

geometry_brush_side_attributes_t Moved( geometry_brush_side_attributes_t rec, const rigid_t &x ) noexcept
{
    // Texture lock for a rigid motion: every moved point keeps its UV.
    rec.uvProjection.origin = Apply( x, rec.uvProjection.origin );
    rec.uvProjection.uAxis = Rotate( x, rec.uvProjection.uAxis );
    rec.uvProjection.vAxis = Rotate( x, rec.uvProjection.vAxis );
    rec.uvProjection.normal = Rotate( x, rec.uvProjection.normal );
    return rec;
}

bool SourceEmpty( const brush_source_t &s ) noexcept
{
    return s.solid.sides.pData == nullptr && s.solid.sides.pAllocator == nullptr && !GeometrySourceId_IsValid( s.solid.sourceId ) &&
           s.attributes.records.pData == nullptr && s.attributes.records.pAllocator == nullptr;
}

// One segment: hull of the face at two samples, surfaces assigned, staged
// as an authored brush.
geometry_status_t BuildSegment(
    const vec3d_t *pFace, u32 cFace, vec3d_t faceNormal, const geometry_brush_side_attributes_t &faceRecord, const rigid_t &a,
    const rigid_t &b, const allocator_t *pA, const geometry_policy_t &policy, geometry_source_id_allocator_t *pIds,
    brush_source_t *pOut ) noexcept
{
    vector_t<vec3d_t> pts{};
    brush_solid_t solid{};
    geometry_brush_side_attribute_store_t store{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &pts );
        BrushSolid_Shutdown( &solid );
        BrushSideAttributeStore_Shutdown( &store );
    };
    if ( !Vector_Init( &pts, pA ) || !Vector_Reserve( &pts, 2u * cFace ) ) {
        cleanup();
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 i = 0u; i < cFace; ++i ) { (void)Vector_PushBack( &pts, Apply( a, pFace[i] ) ); }
    for ( u32 i = 0u; i < cFace; ++i ) { (void)Vector_PushBack( &pts, Apply( b, pFace[i] ) ); }
    geometry_status_t st = ConvexHull_TryBuildBrush( &solid, pA, policy, pIds, pts.pData, pts.nCount );
    if ( st == geometry_status_t::OK ) { st = BrushSideAttributeStore_Init( &store, pA ); }
    const vec3d_t na = Rotate( a, faceNormal ), nb = Rotate( b, faceNormal );
    const vec3d_t pa = Apply( a, pFace[0] ), pb = Apply( b, pFace[0] );
    const f64 tol = policy.numerical.fCoplanarDistanceTolerance;
    for ( usize i = 0u; st == geometry_status_t::OK && i < solid.sides.nCount; ++i ) {
        brush_solid_side_t &side = solid.sides.pData[i];
        const math::planed_t pl = side.plane;
        geometry_brush_side_attributes_t rec = faceRecord;
        const bool bStart = math::Vec3d_Dot( pl.normal, na ) < -1.0 + 1e-9 && std::fabs( math::Vec3d_Dot( pl.normal, pa ) + pl.d ) <= tol;
        const bool bEnd = math::Vec3d_Dot( pl.normal, nb ) > 1.0 - 1e-9 && std::fabs( math::Vec3d_Dot( pl.normal, pb ) + pl.d ) <= tol;
        if ( bStart ) {
            rec = Moved( faceRecord, a );
        } else if ( bEnd ) {
            rec = Moved( faceRecord, b );
        } else {
            const vec3d_t up = std::fabs( pl.normal.z ) > 0.9 ? math::Vec3d_Make( 0, 1, 0 ) : math::Vec3d_Make( 0, 0, 1 );
            const math::vec2d_t scale{ std::fabs( faceRecord.uvProjection.worldUnitsPerUv.x ),
                                       std::fabs( faceRecord.uvProjection.worldUnitsPerUv.y ) };
            if ( !math::Uvd_TryBuildPlanarMapping( math::Vec3d_Make( 0, 0, 0 ), pl.normal, up, scale, 0.0, math::vec2d_t{ 0, 0 },
                                                   policy.numerical.fAbsoluteDistanceTolerance, &rec.uvProjection ) ) {
                st = geometry_status_t::NUMERIC_FAILURE;
                break;
            }
        }
        usize iRec = 0u;
        st = BrushSideAttributeStore_TryAppend( &store, policy, rec, &iRec );
        side.iAttributeIndex = static_cast<u32>( iRec );
    }
    if ( st == geometry_status_t::OK ) { st = BrushValidation_Deep( &solid, policy, pA ).status; }
    if ( st == geometry_status_t::OK ) { st = BrushSource_TryBuildWithStore( &solid, &store, pA, policy, pOut ); }
    cleanup();
    return st;
}

} // namespace

geometry_status_t BrushSweep_TryBuild(
    span_t<const brush_sweep_face_t> faces, const brush_sweep_params_t &params, const allocator_t *pAllocator,
    const geometry_policy_t &policy, geometry_source_id_allocator_t *pIdAllocator, brush_source_t *pOut, u32 cCapacity,
    u32 *pCountOut ) noexcept
{
    if ( pCountOut != nullptr ) { *pCountOut = 0u; }
    if ( pCountOut == nullptr || pOut == nullptr || pAllocator == nullptr || pIdAllocator == nullptr || faces.nCount == 0u ||
         faces.pData == nullptr || !GeometryPolicy_IsValid( policy ) || params.cSegments == 0u || params.cIterations == 0u ||
         params.cSegments > 1024u || params.cIterations > 1024u ||
         static_cast<u8>( params.path ) > static_cast<u8>( brush_sweep_path_t::S_BEND ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Vec3d_IsFinite( params.offset ) || !math::Vec3d_IsFinite( params.axisOrigin ) ||
         !math::Vec3d_IsFinite( params.axisDirection ) || !std::isfinite( params.angleRadians ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    vec3d_t axis{};
    if ( params.path == brush_sweep_path_t::ARC &&
         ( !math::Vec3d_TryNormalize( params.axisDirection, 0.0, &axis, nullptr ) || !( std::fabs( params.angleRadians ) < kTwoPi ) ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const u64 cTotal = static_cast<u64>( faces.nCount ) * params.cSegments * params.cIterations;
    if ( cTotal > cCapacity ) {
        *pCountOut = cTotal > CY_U32_MAX ? CY_U32_MAX : static_cast<u32>( cTotal );
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }
    for ( u32 i = 0u; i < cCapacity; ++i ) {
        if ( !SourceEmpty( pOut[i] ) ) { return geometry_status_t::ALREADY_INITIALIZED; }
    }
    const u32 n = static_cast<u32>( cTotal );
    brush_source_t *pTemp = Allocator_AllocateArrayStorage<brush_source_t>( pAllocator, n );
    if ( pTemp == nullptr ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( u32 i = 0u; i < n; ++i ) { ::new ( static_cast<void *>( &pTemp[i] ) ) brush_source_t{}; }
    const geometry_source_id_allocator_t snapshot = *pIdAllocator;
    brush_boundary_t bd{};
    geometry_status_t st = BrushBoundary_Init( &bd, pAllocator );
    vec3d_t face[256];
    u32 iOut = 0u;
    for ( usize f = 0u; st == geometry_status_t::OK && f < faces.nCount; ++f ) {
        const brush_sweep_face_t &sf = faces.pData[f];
        if ( sf.pSource == nullptr || !BrushSource_IsInitialized( sf.pSource ) || sf.iSide >= BrushSolid_SideCount( &sf.pSource->solid ) ) {
            st = geometry_status_t::INVALID_ARGUMENT;
            break;
        }
        st = BrushBoundary_TryReconstruct( &bd, &sf.pSource->solid, policy );
        if ( st != geometry_status_t::OK ) { break; }
        // The side's face polygon.
        u32 cFace = 0u;
        for ( usize bf = 0u; bf < BrushBoundary_FaceCount( &bd ); ++bf ) {
            if ( bd.faces.pData[bf].iSide != sf.iSide ) { continue; }
            u32 idx[256];
            usize cIdx = 0u;
            st = BrushBoundary_TryGetFaceVertexIndices( &bd, bf, idx, 256u, &cIdx );
            for ( usize k = 0u; st == geometry_status_t::OK && k < cIdx; ++k ) { face[k] = bd.vertices.pData[idx[k]]; }
            cFace = static_cast<u32>( cIdx );
        }
        if ( st != geometry_status_t::OK ) { break; }
        if ( cFace < 3u ) {
            st = geometry_status_t::INVALID_ARGUMENT;
            break;
        }
        const brush_solid_side_t &side = sf.pSource->solid.sides.pData[sf.iSide];
        geometry_brush_side_attributes_t record{};
        st = BrushSideAttributeStore_TryGet( &sf.pSource->attributes, side.iAttributeIndex, &record );
        const u32 cSteps = params.cSegments * params.cIterations;
        for ( u32 k = 0u; st == geometry_status_t::OK && k < cSteps; ++k ) {
            const f64 g0 = static_cast<f64>( k ) / params.cSegments, g1 = static_cast<f64>( k + 1u ) / params.cSegments;
            const rigid_t a = At( params, axis, side.plane.normal, g0 ), b = At( params, axis, side.plane.normal, g1 );
            st = BuildSegment( face, cFace, side.plane.normal, record, a, b, pAllocator, policy, pIdAllocator, &pTemp[iOut] );
            if ( st == geometry_status_t::OK ) { ++iOut; }
        }
    }
    if ( st == geometry_status_t::OK ) {
        for ( u32 i = 0u; i < n; ++i ) {
            Vector_Move( &pOut[i].solid.sides, &pTemp[i].solid.sides );
            pOut[i].solid.sourceId = pTemp[i].solid.sourceId;
            pTemp[i].solid.sourceId = GEOMETRY_SOURCE_ID_INVALID;
            Vector_Move( &pOut[i].attributes.records, &pTemp[i].attributes.records );
        }
        *pCountOut = n;
    } else {
        *pIdAllocator = snapshot;
    }
    for ( u32 i = 0u; i < n; ++i ) {
        BrushSource_Shutdown( &pTemp[i] );
        pTemp[i].~brush_source_t();
    }
    Allocator_FreeArrayStorage( pAllocator, pTemp, n );
    BrushBoundary_Shutdown( &bd );
    return st;
}

} // namespace cypher::editor::geometry
