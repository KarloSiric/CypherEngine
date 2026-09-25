//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushPatches.cpp
//  Purpose: Implements brush face to patch conversion.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushPatches.h"

#include "CypherGeometry_BrushBoundary.h"
#include "CypherMath_UV.h"

#include <new>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using math::vec3d_t;

namespace
{

bool PatchEmpty( const patch_surface_t &p ) noexcept
{
    return p.controls.pData == nullptr && p.controls.pAllocator == nullptr && p.cColumns == 0u && p.cRows == 0u &&
           !GeometrySourceId_IsValid( p.sourceId );
}

vec3d_t Lerp( vec3d_t a, vec3d_t b, f64 t ) noexcept { return math::Vec3d_Add( a, math::Vec3d_Scale( math::Vec3d_Subtract( b, a ), t ) ); }

// One flat patch over the quad q[0..3] (counter-clockwise seen from the
// front): u runs q0 -> q1, v runs q0 -> q3, so the patch front faces out.
geometry_status_t BuildQuadPatch(
    const vec3d_t *q, patch_basis_t basis, u32 material, const math::planar_uv_mappingd_t &uv, const allocator_t *pA,
    geometry_source_id_allocator_t *pIds, patch_surface_t *pOut ) noexcept
{
    const u32 d = Patch_Degree( basis ), n = d + 1u;
    const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( pIds );
    if ( id.status != geometry_status_t::OK ) { return id.status; }
    geometry_status_t st = Patch_Init( pOut, pA, basis, n, n, id.id );
    for ( u32 r = 0u; st == geometry_status_t::OK && r < n; ++r ) {
        for ( u32 c = 0u; st == geometry_status_t::OK && c < n; ++c ) {
            const f64 s = static_cast<f64>( c ) / d, t = static_cast<f64>( r ) / d;
            patch_control_t ctl{};
            ctl.position = Lerp( Lerp( q[0], q[1], s ), Lerp( q[3], q[2], s ), t );
            if ( !math::Uvd_TryProjectPlanarPoint( uv, ctl.position, 0.0, &ctl.uv ) ) {
                st = geometry_status_t::NUMERIC_FAILURE;
                break;
            }
            const geometry_source_id_result_t cid = GeometrySourceIdAllocator_Allocate( pIds );
            if ( cid.status != geometry_status_t::OK ) {
                st = cid.status;
                break;
            }
            ctl.sourceId = cid.id;
            st = Patch_TrySetControl( pOut, c, r, ctl );
        }
    }
    if ( st == geometry_status_t::OK ) {
        pOut->materialId = material;
        // Out of scratch memory is not a degenerate patch.
        const patch_fault_t fault = Patch_Validate( pOut, pA ).fault;
        st = fault == patch_fault_t::NONE                    ? geometry_status_t::OK
             : fault == patch_fault_t::VALIDATION_INCOMPLETE ? geometry_status_t::ALLOCATION_FAILED
                                                             : geometry_status_t::DEGENERATE;
    }
    return st;
}

} // namespace

geometry_status_t BrushPatches_TryFromFace(
    const brush_source_t *pSource, u32 iSide, patch_basis_t basis, const allocator_t *pAllocator, const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator, patch_surface_t *pOut, u32 cCapacity, u32 *pCountOut ) noexcept
{
    if ( pCountOut != nullptr ) { *pCountOut = 0u; }
    if ( pSource == nullptr || pOut == nullptr || pCountOut == nullptr || pAllocator == nullptr || pIdAllocator == nullptr ||
         !BrushSource_IsInitialized( pSource ) || !GeometryPolicy_IsValid( policy ) || iSide >= BrushSolid_SideCount( &pSource->solid ) ||
         ( basis != patch_basis_t::BIQUADRATIC_BEZIER && basis != patch_basis_t::BICUBIC_BEZIER ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    geometry_brush_side_attributes_t record{};
    geometry_status_t st =
        BrushSideAttributeStore_TryGet( &pSource->attributes, pSource->solid.sides.pData[iSide].iAttributeIndex, &record );
    if ( st != geometry_status_t::OK ) { return st; }
    if ( record.material.value > CY_U32_MAX ) { return geometry_status_t::LIMIT_EXCEEDED; }
    const u32 material = static_cast<u32>( record.material.value );

    // The face polygon.
    brush_boundary_t bd{};
    st = BrushBoundary_Init( &bd, pAllocator );
    if ( st == geometry_status_t::OK ) { st = BrushBoundary_TryReconstruct( &bd, &pSource->solid, policy ); }
    vec3d_t face[256];
    u32 cFace = 0u;
    for ( usize f = 0u; st == geometry_status_t::OK && f < BrushBoundary_FaceCount( &bd ); ++f ) {
        if ( bd.faces.pData[f].iSide != iSide ) { continue; }
        u32 idx[256];
        usize cIdx = 0u;
        st = BrushBoundary_TryGetFaceVertexIndices( &bd, f, idx, 256u, &cIdx );
        for ( usize k = 0u; st == geometry_status_t::OK && k < cIdx; ++k ) { face[k] = bd.vertices.pData[idx[k]]; }
        cFace = static_cast<u32>( cIdx );
    }
    BrushBoundary_Shutdown( &bd );
    if ( st != geometry_status_t::OK ) { return st; }
    if ( cFace < 3u ) { return geometry_status_t::INVALID_ARGUMENT; }

    const u32 n = cFace == 4u ? 1u : cFace;
    if ( n > cCapacity ) {
        *pCountOut = n;
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }
    for ( u32 i = 0u; i < cCapacity; ++i ) {
        if ( !PatchEmpty( pOut[i] ) ) { return geometry_status_t::ALREADY_INITIALIZED; }
    }
    patch_surface_t *pTemp = Allocator_AllocateArrayStorage<patch_surface_t>( pAllocator, n );
    if ( pTemp == nullptr ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( u32 i = 0u; i < n; ++i ) { ::new ( static_cast<void *>( &pTemp[i] ) ) patch_surface_t{}; }
    const geometry_source_id_allocator_t snapshot = *pIdAllocator;
    if ( cFace == 4u ) {
        st = BuildQuadPatch( face, basis, material, record.uvProjection, pAllocator, pIdAllocator, &pTemp[0] );
    } else {
        // Quads around the centroid: corner, next edge midpoint, centroid,
        // previous edge midpoint (counter-clockwise, like the face).
        vec3d_t centroid{};
        for ( u32 i = 0u; i < cFace; ++i ) { centroid = math::Vec3d_Add( centroid, face[i] ); }
        centroid = math::Vec3d_Scale( centroid, 1.0 / cFace );
        for ( u32 i = 0u; st == geometry_status_t::OK && i < cFace; ++i ) {
            const vec3d_t q[4] = { face[i], Lerp( face[i], face[( i + 1u ) % cFace], 0.5 ), centroid,
                                   Lerp( face[( i + cFace - 1u ) % cFace], face[i], 0.5 ) };
            st = BuildQuadPatch( q, basis, material, record.uvProjection, pAllocator, pIdAllocator, &pTemp[i] );
        }
    }
    if ( st == geometry_status_t::OK ) {
        for ( u32 i = 0u; i < n; ++i ) {
            Vector_Move( &pOut[i].controls, &pTemp[i].controls );
            pOut[i].cColumns = pTemp[i].cColumns;
            pOut[i].cRows = pTemp[i].cRows;
            pOut[i].basis = pTemp[i].basis;
            pOut[i].materialId = pTemp[i].materialId;
            pOut[i].sourceId = pTemp[i].sourceId;
            pTemp[i].sourceId = GEOMETRY_SOURCE_ID_INVALID;
        }
        *pCountOut = n;
    } else {
        *pIdAllocator = snapshot;
    }
    for ( u32 i = 0u; i < n; ++i ) {
        Patch_Shutdown( &pTemp[i] );
        pTemp[i].~patch_surface_t();
    }
    Allocator_FreeArrayStorage( pAllocator, pTemp, n );
    return st;
}

} // namespace cypher::editor::geometry
