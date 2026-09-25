//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_DocumentRollback.cpp
//  Purpose: Implements the allocation-free batch rollback helpers.
//  Details: The per-type frees mirror the document stores' allocation
//           (placement-new into allocator memory of the object's exact size
//           and alignment).
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_DocumentRollback.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentSurfaces.h"

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

template <typename object_t, typename shutdown_t>
void FreeObject( const allocator_t *pA, object_t *p, shutdown_t &&shutdown ) noexcept
{
    if ( p == nullptr ) { return; }
    shutdown( p );
    p->~object_t();
    Allocator_Free( pA, p, sizeof( object_t ), alignof( object_t ) );
}

template <typename object_t, typename shutdown_t>
void TruncateStore( const allocator_t *pA, vector_t<object_t *> *pStore, usize cKeep, shutdown_t &&shutdown ) noexcept
{
    while ( pStore->nCount > cKeep ) {
        FreeObject( pA, pStore->pData[pStore->nCount - 1u], shutdown );
        Vector_PopBack( pStore );
    }
}

template <typename t> void SwapValues( t &a, t &b ) noexcept
{
    t tmp = a;
    a = b;
    b = tmp;
}

void SwapIdSets( geometry_source_id_set_t &a, geometry_source_id_set_t &b ) noexcept
{
    SwapValues( a.pSlots, b.pSlots );
    SwapValues( a.nCount, b.nCount );
    SwapValues( a.nCapacity, b.nCapacity );
    SwapValues( a.pAllocator, b.pAllocator );
}

} // namespace

geometry_document_counts_t GeometryDocument_InternalCounts( const geometry_document_t *pDocument ) noexcept
{
    geometry_document_counts_t c{};
    if ( pDocument == nullptr ) { return c; }
    c.cBrushes = pDocument->brushes.nCount;
    c.cMeshes = pDocument->meshes.nCount;
    c.cPatches = pDocument->patches.nCount;
    c.cHeightFields = pDocument->heightFields.nCount;
    return c;
}

void GeometryDocument_InternalTruncate( geometry_document_t *pDocument, const geometry_document_counts_t &counts ) noexcept
{
    if ( pDocument == nullptr || pDocument->pAllocator == nullptr ) { return; }
    const allocator_t *pA = pDocument->pAllocator;
    TruncateStore( pA, &pDocument->brushes, counts.cBrushes, []( brush_solid_t *p ) noexcept { BrushSolid_Shutdown( p ); } );
    while ( pDocument->brushAttributes.nCount > counts.cBrushes ) {
        BrushAttributes_Free( pA, pDocument->brushAttributes.pData[pDocument->brushAttributes.nCount - 1u] );
        Vector_PopBack( &pDocument->brushAttributes );
    }
    TruncateStore( pA, &pDocument->meshes, counts.cMeshes, []( mesh_source_t *p ) noexcept { MeshSource_Shutdown( p ); } );
    TruncateStore( pA, &pDocument->patches, counts.cPatches, []( patch_surface_t *p ) noexcept { Patch_Shutdown( p ); } );
    TruncateStore( pA, &pDocument->heightFields, counts.cHeightFields, []( heightfield_t *p ) noexcept { HeightField_Shutdown( p ); } );
}

void GeometryDocument_InternalSwapRegistry( geometry_document_t *pDocument, geometry_source_id_registry_t *pRegistry ) noexcept
{
    if ( pDocument == nullptr || pRegistry == nullptr ) { return; }
    geometry_source_id_registry_t &a = pDocument->sourceIds;
    geometry_source_id_registry_t &b = *pRegistry;
    SwapIdSets( a.claimedIds, b.claimedIds );
    SwapIdSets( a.liveIds, b.liveIds );
    SwapValues( a.allocator, b.allocator );
    SwapValues( a.cEntriesMax, b.cEntriesMax );
    SwapValues( a.pAllocator, b.pAllocator );
    SwapValues( a.bLoadRegistrationOpen, b.bLoadRegistrationOpen );
}

} // namespace cypher::editor::geometry
