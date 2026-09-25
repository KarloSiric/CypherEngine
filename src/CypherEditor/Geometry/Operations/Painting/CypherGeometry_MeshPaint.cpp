//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_MeshPaint.cpp
//  Purpose: Implements the blend-painting brush.
//  Details: Validation (parameters, every face handle, duplicates) and
//           store growth happen before the first write; after that the
//           writes cannot fail, which is what makes a dab all-or-nothing.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_MeshPaint.h"

#include "CypherCommon_Vector.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

f64 Falloff( mesh_paint_falloff_t f, f64 t ) noexcept
{
    switch ( f ) {
        case mesh_paint_falloff_t::CONSTANT: return 1.0;
        case mesh_paint_falloff_t::LINEAR: return 1.0 - t;
        case mesh_paint_falloff_t::SMOOTH: return 1.0 - t * t * ( 3.0 - 2.0 * t );
    }
    return 0.0;
}

// Channel c (0 = R .. 3 = A) of 0xRRGGBBAA.
u32 ShiftOf( u32 c ) noexcept
{
    return 24u - 8u * c;
}

} // namespace

geometry_status_t MeshPaint_TryBrush(
    mesh_attribute_store_t *pStore,
    const editable_mesh_t *pMesh,
    span_t<const geometry_mesh_face_handle_t> faces,
    const mesh_paint_brush_t &brush,
    u32 *pCornersChangedOut ) noexcept
{
    if ( pCornersChangedOut ) { *pCornersChangedOut = 0u; }
    if ( pStore == nullptr || pMesh == nullptr || ( faces.nCount > 0u && faces.pData == nullptr ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !MeshAttributeStore_IsInitialized( pStore ) || !EditableMesh_IsInitialized( pMesh ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Vec3d_IsFinite( brush.center ) || !std::isfinite( brush.radius ) || !std::isfinite( brush.strength ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }
    if ( !( brush.radius > 0.0 ) || brush.strength < 0.0 || brush.strength > 1.0 || brush.channels == 0u || brush.channels > 0x0Fu ||
         brush.falloff > mesh_paint_falloff_t::SMOOTH ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    vector_t<u8> seen{};
    if ( !Vector_Init( &seen, pMesh->pAllocator ) || !Vector_Resize( &seen, pMesh->faces.cSlots ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < seen.nCount; ++i ) { seen.pData[i] = 0u; }
    for ( usize i = 0u; i < faces.nCount; ++i ) {
        if ( !GenerationPool_Contains( &pMesh->faces, faces.pData[i] ) ) { return geometry_status_t::STALE_HANDLE; }
        if ( seen.pData[faces.pData[i].nSlot] != 0u ) { return geometry_status_t::INVALID_ARGUMENT; }
        seen.pData[faces.pData[i].nSlot] = 1u;
    }
    const usize cCapacity = GenerationPool_Capacity( &pMesh->halfEdges );
    if ( cCapacity > 0u && ( !Vector_Reserve( &pStore->corners, cCapacity ) ||
                             ( pStore->corners.nCount < cCapacity && !Vector_Resize( &pStore->corners, cCapacity ) ) ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    u32 cChanged = 0u;
    auto paintFace = [&]( const mesh_face_record_t &f ) noexcept {
        const mesh_loop_record_t *pL = GenerationPool_Get( &pMesh->loops, f.hOuterLoop );
        geometry_mesh_half_edge_handle_t h = pL->hFirstHalfEdge;
        for ( u32 k = 0u; k < pL->cHalfEdges; ++k ) {
            const mesh_half_edge_record_t *pH = GenerationPool_Get( &pMesh->halfEdges, h );
            const math::vec3d_t p = GenerationPool_Get( &pMesh->vertices, pH->hOrigin )->position;
            const f64 d = std::sqrt( math::Vec3d_LengthSquared( math::Vec3d_Subtract( p, brush.center ) ) );
            if ( d <= brush.radius ) {
                const f64 w = brush.strength * Falloff( brush.falloff, d / brush.radius );
                mesh_corner_attributes_t c = MeshAttributeStore_GetCorner( pStore, h );
                u32 color = c.colorRgba;
                for ( u32 ch = 0u; ch < 4u; ++ch ) {
                    if ( ( brush.channels & ( 1u << ch ) ) == 0u ) { continue; }
                    const f64 old = static_cast<f64>( ( color >> ShiftOf( ch ) ) & 0xFFu );
                    const f64 next = old + ( static_cast<f64>( brush.value ) - old ) * w;
                    const u32 byte = static_cast<u32>( std::lround( next < 0.0 ? 0.0 : ( next > 255.0 ? 255.0 : next ) ) );
                    color = ( color & ~( 0xFFu << ShiftOf( ch ) ) ) | ( byte << ShiftOf( ch ) );
                }
                if ( color != c.colorRgba ) {
                    c.colorRgba = color;
                    (void)MeshAttributeStore_TrySetCorner( pStore, h, c, cCapacity ); // grown above: cannot fail
                    ++cChanged;
                }
            }
            h = pH->hNext;
        }
    };
    if ( faces.nCount == 0u ) {
        (void)GenerationPool_ForEach( &pMesh->faces, [&]( geometry_mesh_face_handle_t, const mesh_face_record_t &f ) noexcept -> bool_t {
            paintFace( f );
            return true;
        } );
    } else {
        for ( usize i = 0u; i < faces.nCount; ++i ) { paintFace( *GenerationPool_Get( &pMesh->faces, faces.pData[i] ) ); }
    }
    if ( pCornersChangedOut ) { *pCornersChangedOut = cChanged; }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
