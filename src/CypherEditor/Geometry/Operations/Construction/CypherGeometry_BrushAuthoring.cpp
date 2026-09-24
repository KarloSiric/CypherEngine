//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_BrushAuthoring.cpp
//  Purpose: Implements extrude-split and mirror on authored brushes.
//  Details: Both are staged: the new planes are assembled into a private
//           solid whose sides temporarily point at records of the ORIGINAL
//           store, redundant sides are canonicalized away, the brush is deep
//           validated, and only then is a compact store built from the
//           records the surviving sides use. BrushSource_TryBuildWithStore
//           checks the bindings, and publication (moving the staged vectors
//           into place) cannot fail.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_BrushAuthoring.h"

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushValidation.h"
#include "CypherMath_UV.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;
using math::planed_t;
using math::vec3d_t;

namespace
{

struct side_spec_t {
    planed_t plane{};
    geometry_source_id_t id{};
    u32 iRecord{ 0u }; // record index in the ORIGINAL store
};

bool SourceEmpty( const brush_source_t &s ) noexcept
{
    return s.solid.sides.pData == nullptr && s.solid.sides.pAllocator == nullptr && s.solid.sides.nCount == 0u &&
           !GeometrySourceId_IsValid( s.solid.sourceId ) && s.attributes.records.pData == nullptr &&
           s.attributes.records.pAllocator == nullptr;
}

// Builds a staged authored brush from side specs that reference records of
// pFrom's store: canonicalize, deep-validate, compact the store, bind.
geometry_status_t BuildDerived(
    const brush_source_t *pFrom, const side_spec_t *pSides, usize n, geometry_source_id_t brushId,
    const allocator_t *pA, const geometry_policy_t &policy, brush_source_t *pStagedOut ) noexcept
{
    brush_solid_t solid{};
    geometry_brush_side_attribute_store_t store{};
    vector_t<u32> remap{};
    auto cleanup = [&]() noexcept {
        BrushSolid_Shutdown( &solid );
        BrushSideAttributeStore_Shutdown( &store );
        Vector_Shutdown( &remap );
    };
    geometry_status_t st = BrushSolid_Init( &solid, pA, brushId );
    if ( st == geometry_status_t::OK ) { st = BrushSolid_TryReserve( &solid, policy.limits, n ); }
    for ( usize i = 0u; st == geometry_status_t::OK && i < n; ++i ) {
        brush_solid_side_t side{};
        side.plane = pSides[i].plane;
        side.sourceId = pSides[i].id;
        side.iAttributeIndex = pSides[i].iRecord;
        st = BrushSolid_TryAddSide( &solid, policy.limits, side, nullptr );
    }
    if ( st == geometry_status_t::OK ) { st = BrushSolid_TryCanonicalizeSides( &solid, policy ); }
    if ( st == geometry_status_t::OK ) { st = BrushValidation_Deep( &solid, policy, pA ).status; }
    // Compact store: each original record used by a surviving side, once.
    const usize cRecords = BrushSideAttributeStore_Count( &pFrom->attributes );
    if ( st == geometry_status_t::OK ) {
        st = BrushSideAttributeStore_Init( &store, pA );
        if ( st == geometry_status_t::OK && ( !Vector_Init( &remap, pA ) || !Vector_Resize( &remap, cRecords ) ) ) {
            st = geometry_status_t::ALLOCATION_FAILED;
        }
    }
    for ( usize i = 0u; st == geometry_status_t::OK && i < remap.nCount; ++i ) { remap.pData[i] = CY_INVALID_INDEX; }
    for ( usize i = 0u; st == geometry_status_t::OK && i < solid.sides.nCount; ++i ) {
        brush_solid_side_t &side = solid.sides.pData[i];
        if ( side.iAttributeIndex >= cRecords ) {
            st = geometry_status_t::CORRUPT_STATE;
            break;
        }
        if ( remap.pData[side.iAttributeIndex] == CY_INVALID_INDEX ) {
            geometry_brush_side_attributes_t record{};
            usize iNew = 0u;
            st = BrushSideAttributeStore_TryGet( &pFrom->attributes, side.iAttributeIndex, &record );
            if ( st == geometry_status_t::OK ) { st = BrushSideAttributeStore_TryAppend( &store, policy, record, &iNew ); }
            if ( st == geometry_status_t::OK ) { remap.pData[side.iAttributeIndex] = static_cast<u32>( iNew ); }
        }
        side.iAttributeIndex = remap.pData[side.iAttributeIndex];
    }
    if ( st == geometry_status_t::OK ) { st = BrushSource_TryBuildWithStore( &solid, &store, pA, policy, pStagedOut ); }
    cleanup();
    return st;
}

// Moves a staged source into place (destination shut down first). Cannot
// fail: only pointers move.
void Publish( brush_source_t *pDst, brush_source_t *pStaged ) noexcept
{
    BrushSource_Shutdown( pDst );
    Vector_Move( &pDst->solid.sides, &pStaged->solid.sides );
    pDst->solid.sourceId = pStaged->solid.sourceId;
    pStaged->solid.sourceId = GEOMETRY_SOURCE_ID_INVALID;
    Vector_Move( &pDst->attributes.records, &pStaged->attributes.records );
}

planed_t Offset( planed_t p, f64 distance ) noexcept { return planed_t{ p.normal, p.d - distance }; }
planed_t Flip( planed_t p ) noexcept { return planed_t{ math::Vec3d_Scale( p.normal, -1.0 ), -p.d }; }

vec3d_t ReflectDir( vec3d_t v, vec3d_t m ) noexcept
{
    return math::Vec3d_Subtract( v, math::Vec3d_Scale( m, 2.0 * math::Vec3d_Dot( v, m ) ) );
}

vec3d_t ReflectPoint( vec3d_t p, planed_t mirror ) noexcept
{
    return math::Vec3d_Subtract( p, math::Vec3d_Scale( mirror.normal, 2.0 * ( math::Vec3d_Dot( mirror.normal, p ) + mirror.d ) ) );
}

} // namespace

geometry_status_t BrushAuthoring_TryExtrudeSplit(
    brush_source_t *pSource, usize iSide, f64 distance, const allocator_t *pAllocator, const geometry_policy_t &policy,
    geometry_source_id_allocator_t *pIdAllocator, brush_source_t *pNewOut ) noexcept
{
    if ( pSource == nullptr || pNewOut == nullptr || pAllocator == nullptr || pIdAllocator == nullptr ||
         !BrushSource_IsInitialized( pSource ) || !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !SourceEmpty( *pNewOut ) ) { return geometry_status_t::ALREADY_INITIALIZED; }
    if ( !std::isfinite( distance ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    const usize n = BrushSolid_SideCount( &pSource->solid );
    if ( iSide >= n || distance == 0.0 ) { return geometry_status_t::INVALID_ARGUMENT; }
    const brush_solid_side_t *pS = pSource->solid.sides.pData;
    const planed_t face = pS[iSide].plane;
    const u32 faceRecord = pS[iSide].iAttributeIndex;

    const geometry_source_id_allocator_t snapshot = *pIdAllocator;
    vector_t<side_spec_t> spec{};
    brush_source_t stagedNew{}, stagedOld{};
    auto cleanup = [&]() noexcept {
        Vector_Shutdown( &spec );
        BrushSource_Shutdown( &stagedNew );
        BrushSource_Shutdown( &stagedOld );
    };
    auto fail = [&]( geometry_status_t st ) noexcept {
        cleanup();
        *pIdAllocator = snapshot;
        return st;
    };
    if ( !Vector_Init( &spec, pAllocator ) || !Vector_Reserve( &spec, n + 1u ) ) { return fail( geometry_status_t::ALLOCATION_FAILED ); }
    const geometry_source_id_result_t brushId = GeometrySourceIdAllocator_Allocate( pIdAllocator );
    if ( brushId.status != geometry_status_t::OK ) { return fail( brushId.status ); }

    // The new brush: every side (fresh IDs, same surfaces), with the face
    // either pushed out (outward) or kept (inward), plus the internal face.
    const bool bOutward = distance > 0.0;
    const planed_t cut = bOutward ? face : Offset( face, distance ); // distance < 0 moves it in
    for ( usize i = 0u; i < n; ++i ) {
        const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( pIdAllocator );
        if ( id.status != geometry_status_t::OK ) { return fail( id.status ); }
        const planed_t plane = ( bOutward && i == iSide ) ? Offset( face, distance ) : pS[i].plane;
        (void)Vector_PushBack( &spec, side_spec_t{ plane, id.id, pS[i].iAttributeIndex } );
    }
    const geometry_source_id_result_t capId = GeometrySourceIdAllocator_Allocate( pIdAllocator );
    if ( capId.status != geometry_status_t::OK ) { return fail( capId.status ); }
    (void)Vector_PushBack( &spec, side_spec_t{ Flip( cut ), capId.id, faceRecord } );
    geometry_status_t st = BuildDerived( pSource, spec.pData, spec.nCount, brushId.id, pAllocator, policy, &stagedNew );
    if ( st != geometry_status_t::OK ) { return fail( st ); }

    // Inward: the original keeps the inner part, its face (same identity and
    // surface) moved to the cut.
    if ( !bOutward ) {
        Vector_Clear( &spec );
        for ( usize i = 0u; i < n; ++i ) {
            (void)Vector_PushBack( &spec, side_spec_t{ i == iSide ? cut : pS[i].plane, pS[i].sourceId, pS[i].iAttributeIndex } );
        }
        st = BuildDerived( pSource, spec.pData, spec.nCount, pSource->solid.sourceId, pAllocator, policy, &stagedOld );
        if ( st != geometry_status_t::OK ) { return fail( st ); }
        Publish( pSource, &stagedOld );
    }
    Publish( pNewOut, &stagedNew );
    cleanup();
    return geometry_status_t::OK;
}

geometry_status_t BrushAuthoring_TryMirror(
    brush_source_t *pSource, planed_t mirror, bool bTextureLock, const geometry_policy_t &policy ) noexcept
{
    if ( pSource == nullptr || !BrushSource_IsInitialized( pSource ) || !GeometryPolicy_IsValid( policy ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Vec3d_IsFinite( mirror.normal ) || !std::isfinite( mirror.d ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    if ( !math::Vec3d_IsUnitLength( mirror.normal, policy.numerical.fUnitNormalTolerance ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const allocator_t *pA = pSource->solid.sides.pAllocator;
    const usize n = BrushSolid_SideCount( &pSource->solid );
    const usize cRecords = BrushSideAttributeStore_Count( &pSource->attributes );
    const vec3d_t m = mirror.normal;

    brush_solid_t solid{};
    geometry_brush_side_attribute_store_t store{};
    vector_t<u32> remap{};
    brush_source_t staged{};
    auto cleanup = [&]() noexcept {
        BrushSolid_Shutdown( &solid );
        BrushSideAttributeStore_Shutdown( &store );
        Vector_Shutdown( &remap );
        BrushSource_Shutdown( &staged );
    };
    geometry_status_t st = BrushSolid_Init( &solid, pA, pSource->solid.sourceId );
    if ( st == geometry_status_t::OK ) { st = BrushSolid_TryReserve( &solid, policy.limits, n ); }
    if ( st == geometry_status_t::OK ) { st = BrushSideAttributeStore_Init( &store, pA ); }
    if ( st == geometry_status_t::OK && ( !Vector_Init( &remap, pA ) || !Vector_Resize( &remap, cRecords ) ) ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; st == geometry_status_t::OK && i < remap.nCount; ++i ) { remap.pData[i] = CY_INVALID_INDEX; }
    for ( usize i = 0u; st == geometry_status_t::OK && i < n; ++i ) {
        const brush_solid_side_t &src = pSource->solid.sides.pData[i];
        // n.x + d <= 0 reflects to (n - 2(n.m)m).y + (d - 2 e (n.m)) <= 0.
        const f64 nm = math::Vec3d_Dot( src.plane.normal, m );
        brush_solid_side_t side = src;
        side.plane = planed_t{ ReflectDir( src.plane.normal, m ), src.plane.d - 2.0 * mirror.d * nm };
        if ( src.iAttributeIndex >= cRecords ) {
            st = geometry_status_t::CORRUPT_STATE;
            break;
        }
        geometry_brush_side_attributes_t record{};
        st = BrushSideAttributeStore_TryGet( &pSource->attributes, src.iAttributeIndex, &record );
        if ( st != geometry_status_t::OK ) { break; }
        usize iNew = 0u;
        if ( bTextureLock ) {
            // Reflect the projection frame with the geometry: every mirrored
            // point then projects to exactly the UV it had. A shared record
            // stays shared (and is reflected once).
            if ( remap.pData[src.iAttributeIndex] == CY_INVALID_INDEX ) {
                math::planar_uv_mappingd_t &uv = record.uvProjection;
                uv.origin = ReflectPoint( uv.origin, mirror );
                uv.uAxis = ReflectDir( uv.uAxis, m );
                uv.vAxis = ReflectDir( uv.vAxis, m );
                uv.normal = ReflectDir( uv.normal, m );
                st = BrushSideAttributeStore_TryAppend( &store, policy, record, &iNew );
                if ( st == geometry_status_t::OK ) { remap.pData[src.iAttributeIndex] = static_cast<u32>( iNew ); }
            }
            side.iAttributeIndex = remap.pData[src.iAttributeIndex];
        } else {
            // World-aligned for the side's new orientation, same material and
            // texel scale. Sides that shared a record may now face different
            // ways, so each gets its own.
            const vec3d_t up = std::fabs( side.plane.normal.z ) > 0.9 ? math::Vec3d_Make( 0, 1, 0 ) : math::Vec3d_Make( 0, 0, 1 );
            const math::vec2d_t scale{ std::fabs( record.uvProjection.worldUnitsPerUv.x ), std::fabs( record.uvProjection.worldUnitsPerUv.y ) };
            math::planar_uv_mappingd_t uv{};
            if ( !math::Uvd_TryBuildPlanarMapping( math::Vec3d_Make( 0, 0, 0 ), side.plane.normal, up, scale, 0.0, math::vec2d_t{ 0, 0 },
                                                   policy.numerical.fAbsoluteDistanceTolerance, &uv ) ) {
                st = geometry_status_t::NUMERIC_FAILURE;
                break;
            }
            record.uvProjection = uv;
            st = BrushSideAttributeStore_TryAppend( &store, policy, record, &iNew );
            side.iAttributeIndex = static_cast<u32>( iNew );
        }
        if ( st == geometry_status_t::OK ) { st = BrushSolid_TryAddSide( &solid, policy.limits, side, nullptr ); }
    }
    if ( st == geometry_status_t::OK ) { st = BrushValidation_Deep( &solid, policy, pA ).status; }
    if ( st == geometry_status_t::OK ) { st = BrushSource_TryBuildWithStore( &solid, &store, pA, policy, &staged ); }
    if ( st == geometry_status_t::OK ) { Publish( pSource, &staged ); }
    cleanup();
    return st;
}

} // namespace cypher::editor::geometry
