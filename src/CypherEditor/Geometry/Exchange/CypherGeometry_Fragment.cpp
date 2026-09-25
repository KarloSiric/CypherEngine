//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Fragment.cpp
//  Purpose: Implements geometry fragments (copy / paste / duplicate).
//  Details: Remapping rebuilds each object with new IDs into private
//           storage and swaps the whole fragment only when every object
//           succeeded, so a failed remap leaves the fragment as it was.
//           Meshes are remapped through their canonical description, which
//           carries every attribute along unchanged.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Fragment.h"
#include "CypherGeometry_BrushSerialization.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentRollback.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_IdAllocator.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

template <typename object_t> object_t *AllocateObject( const allocator_t *pA ) noexcept
{
    void *pMemory = Allocator_AllocateZeroed( pA, sizeof( object_t ), alignof( object_t ) );
    return pMemory != nullptr ? new ( pMemory ) object_t{} : nullptr;
}

template <typename object_t, typename shutdown_t> void FreeObject( const allocator_t *pA, object_t *p, shutdown_t &&shutdown ) noexcept
{
    if ( p == nullptr ) { return; }
    shutdown( p );
    p->~object_t();
    Allocator_Free( pA, p, sizeof( object_t ), alignof( object_t ) );
}

void FreeBrushSource( const allocator_t *pA, brush_source_t *p ) noexcept
{
    FreeObject( pA, p, []( brush_source_t *q ) noexcept { BrushSource_Shutdown( q ); } );
}
void FreeMesh( const allocator_t *pA, mesh_source_t *p ) noexcept
{
    FreeObject( pA, p, []( mesh_source_t *q ) noexcept { MeshSource_Shutdown( q ); } );
}
void FreePatch( const allocator_t *pA, patch_surface_t *p ) noexcept
{
    FreeObject( pA, p, []( patch_surface_t *q ) noexcept { Patch_Shutdown( q ); } );
}
void FreeField( const allocator_t *pA, heightfield_t *p ) noexcept
{
    FreeObject( pA, p, []( heightfield_t *q ) noexcept { HeightField_Shutdown( q ); } );
}

// Appends a freshly allocated deep copy, or fails leaving the store as it
// was. clone: geometry_status_t( object_t *pOut ).
template <typename object_t, typename clone_t, typename free_t>
geometry_status_t AppendCopy( const allocator_t *pA, vector_t<object_t *> *pStore, clone_t &&clone, free_t &&freeFn ) noexcept
{
    if ( !Vector_Reserve( pStore, pStore->nCount + 1u ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    object_t *pCopy = AllocateObject<object_t>( pA );
    if ( pCopy == nullptr ) { return geometry_status_t::ALLOCATION_FAILED; }
    const geometry_status_t st = clone( pCopy );
    if ( st != geometry_status_t::OK ) {
        freeFn( pA, pCopy );
        return st;
    }
    (void)Vector_PushBack( pStore, pCopy );
    return geometry_status_t::OK;
}

struct fragment_counts_t {
    usize b, m, p, f;
};

fragment_counts_t CountsOf( const geometry_fragment_t *pF ) noexcept
{
    return fragment_counts_t{ pF->brushes.nCount, pF->meshes.nCount, pF->patches.nCount, pF->heightFields.nCount };
}

// Frees everything appended since `counts` (restores a fragment after a
// failed multi-object append).
void TruncateFragment( geometry_fragment_t *pF, const fragment_counts_t &counts ) noexcept
{
    while ( pF->brushes.nCount > counts.b ) {
        FreeBrushSource( pF->pAllocator, pF->brushes.pData[pF->brushes.nCount - 1u] );
        Vector_PopBack( &pF->brushes );
    }
    while ( pF->meshes.nCount > counts.m ) {
        FreeMesh( pF->pAllocator, pF->meshes.pData[pF->meshes.nCount - 1u] );
        Vector_PopBack( &pF->meshes );
    }
    while ( pF->patches.nCount > counts.p ) {
        FreePatch( pF->pAllocator, pF->patches.pData[pF->patches.nCount - 1u] );
        Vector_PopBack( &pF->patches );
    }
    while ( pF->heightFields.nCount > counts.f ) {
        FreeField( pF->pAllocator, pF->heightFields.pData[pF->heightFields.nCount - 1u] );
        Vector_PopBack( &pF->heightFields );
    }
}

// Copies the document object with root ID `id` into the fragment.
geometry_status_t ExtractOne( const geometry_document_t *pDoc, geometry_source_id_t id, geometry_fragment_t *pF ) noexcept
{
    const allocator_t *pA = pF->pAllocator;
    if ( GeometryDocument_FindBrush( pDoc, id ) != nullptr ) {
        return AppendCopy( pA, &pF->brushes,
                           [&]( brush_source_t *pOut ) noexcept { return GeometryDocument_TryCopyBrushSource( pDoc, id, pA, pOut ); },
                           FreeBrushSource );
    }
    if ( const mesh_source_t *pMesh = GeometryDocument_FindMesh( pDoc, id ) ) {
        return AppendCopy( pA, &pF->meshes, [&]( mesh_source_t *pOut ) noexcept { return MeshSource_TryClone( pMesh, pA, pOut ); },
                           FreeMesh );
    }
    if ( const patch_surface_t *pPatch = GeometryDocument_FindPatch( pDoc, id ) ) {
        return AppendCopy( pA, &pF->patches, [&]( patch_surface_t *pOut ) noexcept { return Patch_TryClone( pPatch, pA, pOut ); },
                           FreePatch );
    }
    if ( const heightfield_t *pField = GeometryDocument_FindHeightField( pDoc, id ) ) {
        return AppendCopy( pA, &pF->heightFields,
                           [&]( heightfield_t *pOut ) noexcept { return HeightField_TryClone( pField, pA, pOut ); }, FreeField );
    }
    return geometry_status_t::INVALID_HANDLE;
}

bool InRange( math::vec3d_t p, f64 limit ) noexcept
{
    return math::Vec3d_IsFinite( p ) && std::fabs( p.x ) <= limit && std::fabs( p.y ) <= limit && std::fabs( p.z ) <= limit;
}

} // namespace

geometry_status_t GeometryFragment_Init( geometry_fragment_t *pFragment, const allocator_t *pAllocator ) noexcept
{
    if ( pFragment == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( pFragment->pAllocator != nullptr ) { return geometry_status_t::ALREADY_INITIALIZED; }
    if ( !Vector_Init( &pFragment->brushes, pAllocator ) || !Vector_Init( &pFragment->meshes, pAllocator ) ||
         !Vector_Init( &pFragment->patches, pAllocator ) || !Vector_Init( &pFragment->heightFields, pAllocator ) ) {
        Vector_Shutdown( &pFragment->brushes );
        Vector_Shutdown( &pFragment->meshes );
        Vector_Shutdown( &pFragment->patches );
        Vector_Shutdown( &pFragment->heightFields );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pFragment->pAllocator = pAllocator;
    return geometry_status_t::OK;
}

void GeometryFragment_Clear( geometry_fragment_t *pFragment ) noexcept
{
    if ( !GeometryFragment_IsInitialized( pFragment ) ) { return; }
    TruncateFragment( pFragment, fragment_counts_t{ 0u, 0u, 0u, 0u } );
}

void GeometryFragment_Shutdown( geometry_fragment_t *pFragment ) noexcept
{
    if ( pFragment == nullptr || pFragment->pAllocator == nullptr ) { return; }
    GeometryFragment_Clear( pFragment );
    Vector_Shutdown( &pFragment->brushes );
    Vector_Shutdown( &pFragment->meshes );
    Vector_Shutdown( &pFragment->patches );
    Vector_Shutdown( &pFragment->heightFields );
    pFragment->pAllocator = nullptr;
}

bool GeometryFragment_IsInitialized( const geometry_fragment_t *pFragment ) noexcept
{
    return pFragment != nullptr && pFragment->pAllocator != nullptr;
}

usize GeometryFragment_ObjectCount( const geometry_fragment_t *pFragment ) noexcept
{
    if ( !GeometryFragment_IsInitialized( pFragment ) ) { return 0u; }
    return pFragment->brushes.nCount + pFragment->meshes.nCount + pFragment->patches.nCount + pFragment->heightFields.nCount;
}

geometry_status_t GeometryFragment_TryExtract(
    const geometry_document_t *pDocument,
    span_t<const geometry_source_id_t> rootIds,
    geometry_fragment_t *pFragment ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) || !GeometryFragment_IsInitialized( pFragment ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( rootIds.nCount > 0u && rootIds.pData == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0u; i < rootIds.nCount; ++i ) {
        for ( usize j = i + 1u; j < rootIds.nCount; ++j ) {
            if ( rootIds.pData[i].value == rootIds.pData[j].value ) { return geometry_status_t::INVALID_ARGUMENT; }
        }
    }
    const fragment_counts_t before = CountsOf( pFragment );
    for ( usize i = 0u; i < rootIds.nCount; ++i ) {
        const geometry_status_t st = ExtractOne( pDocument, rootIds.pData[i], pFragment );
        if ( st != geometry_status_t::OK ) {
            TruncateFragment( pFragment, before );
            return st;
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometryFragment_TryExtractAll( const geometry_document_t *pDocument, geometry_fragment_t *pFragment ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) || !GeometryFragment_IsInitialized( pFragment ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    vector_t<geometry_source_id_t> roots{};
    if ( !Vector_Init( &roots, pFragment->pAllocator ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    bool bOk = true;
    for ( usize i = 0u; bOk && i < pDocument->brushes.nCount; ++i ) { bOk = Vector_PushBack( &roots, pDocument->brushes.pData[i]->sourceId ); }
    for ( usize i = 0u; bOk && i < GeometryDocument_MeshCount( pDocument ); ++i ) {
        bOk = Vector_PushBack( &roots, GeometryDocument_MeshAt( pDocument, i )->sourceId );
    }
    for ( usize i = 0u; bOk && i < GeometryDocument_PatchCount( pDocument ); ++i ) {
        bOk = Vector_PushBack( &roots, GeometryDocument_PatchAt( pDocument, i )->sourceId );
    }
    for ( usize i = 0u; bOk && i < GeometryDocument_HeightFieldCount( pDocument ); ++i ) {
        bOk = Vector_PushBack( &roots, GeometryDocument_HeightFieldAt( pDocument, i )->sourceId );
    }
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    // Every root is distinct (one document identity domain), so the
    // quadratic duplicate scan in TryExtract is skipped by extracting here.
    const fragment_counts_t before = CountsOf( pFragment );
    for ( usize i = 0u; i < roots.nCount; ++i ) {
        const geometry_status_t st = ExtractOne( pDocument, roots.pData[i], pFragment );
        if ( st != geometry_status_t::OK ) {
            TruncateFragment( pFragment, before );
            return st;
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometryFragment_TryAppendCopies(
    geometry_fragment_t *pFragment,
    const geometry_policy_t &policy,
    span_t<const brush_source_t> brushes,
    span_t<const mesh_source_t> meshes,
    span_t<const patch_surface_t> patches,
    span_t<const heightfield_t> heightFields ) noexcept
{
    if ( !GeometryFragment_IsInitialized( pFragment ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( ( brushes.nCount > 0u && brushes.pData == nullptr ) || ( meshes.nCount > 0u && meshes.pData == nullptr ) ||
         ( patches.nCount > 0u && patches.pData == nullptr ) || ( heightFields.nCount > 0u && heightFields.pData == nullptr ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const allocator_t *pA = pFragment->pAllocator;
    const fragment_counts_t before = CountsOf( pFragment );
    geometry_status_t st = geometry_status_t::OK;
    for ( usize i = 0u; st == geometry_status_t::OK && i < brushes.nCount; ++i ) {
        st = AppendCopy( pA, &pFragment->brushes,
                         [&]( brush_source_t *pOut ) noexcept { return BrushSource_TryClone( &brushes.pData[i], pA, policy, pOut ); },
                         FreeBrushSource );
    }
    for ( usize i = 0u; st == geometry_status_t::OK && i < meshes.nCount; ++i ) {
        st = AppendCopy( pA, &pFragment->meshes,
                         [&]( mesh_source_t *pOut ) noexcept { return MeshSource_TryClone( &meshes.pData[i], pA, pOut ); }, FreeMesh );
    }
    for ( usize i = 0u; st == geometry_status_t::OK && i < patches.nCount; ++i ) {
        st = AppendCopy( pA, &pFragment->patches,
                         [&]( patch_surface_t *pOut ) noexcept { return Patch_TryClone( &patches.pData[i], pA, pOut ); }, FreePatch );
    }
    for ( usize i = 0u; st == geometry_status_t::OK && i < heightFields.nCount; ++i ) {
        st = AppendCopy( pA, &pFragment->heightFields,
                         [&]( heightfield_t *pOut ) noexcept { return HeightField_TryClone( &heightFields.pData[i], pA, pOut ); },
                         FreeField );
    }
    if ( st != geometry_status_t::OK ) { TruncateFragment( pFragment, before ); }
    return st;
}

geometry_status_t GeometryFragment_TryRemapForDocument(
    geometry_fragment_t *pFragment,
    const geometry_document_t *pDestination,
    geometry_source_id_remap_t *pRemapOut ) noexcept
{
    if ( !GeometryFragment_IsInitialized( pFragment ) || !GeometryDocument_IsInitialized( pDestination ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pRemapOut != nullptr && pRemapOut->entries.pAllocator != nullptr ) { return geometry_status_t::ALREADY_INITIALIZED; }
    const allocator_t *pA = pFragment->pAllocator;

    // Every ID in the fragment, sorted; each gets the next fresh ID of the
    // destination in that order (deterministic for a given fragment).
    vector_t<geometry_source_id_t> ids{};
    if ( !Vector_Init( &ids, pA ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    bool bOk = true;
    for ( usize i = 0u; bOk && i < pFragment->brushes.nCount; ++i ) {
        const brush_solid_t &s = pFragment->brushes.pData[i]->solid;
        bOk = Vector_PushBack( &ids, s.sourceId );
        for ( usize k = 0u; bOk && k < s.sides.nCount; ++k ) { bOk = Vector_PushBack( &ids, s.sides.pData[k].sourceId ); }
    }
    for ( usize i = 0u; bOk && i < pFragment->meshes.nCount; ++i ) {
        bOk = MeshSource_TryCollectSourceIds( pFragment->meshes.pData[i], &ids ) == geometry_status_t::OK;
    }
    for ( usize i = 0u; bOk && i < pFragment->patches.nCount; ++i ) {
        const patch_surface_t &p = *pFragment->patches.pData[i];
        bOk = Vector_PushBack( &ids, p.sourceId );
        for ( usize k = 0u; bOk && k < p.controls.nCount; ++k ) { bOk = Vector_PushBack( &ids, p.controls.pData[k].sourceId ); }
    }
    for ( usize i = 0u; bOk && i < pFragment->heightFields.nCount; ++i ) {
        const heightfield_t &f = *pFragment->heightFields.pData[i];
        bOk = Vector_PushBack( &ids, f.sourceId );
        for ( usize k = 0u; bOk && k < f.tiles.nCount; ++k ) { bOk = Vector_PushBack( &ids, f.tiles.pData[k].sourceId ); }
    }
    if ( !bOk ) { return geometry_status_t::ALLOCATION_FAILED; }
    std::sort( ids.pData, ids.pData + ids.nCount, []( geometry_source_id_t a, geometry_source_id_t b ) { return a.value < b.value; } );
    for ( usize i = 1u; i < ids.nCount; ++i ) {
        if ( ids.pData[i].value == ids.pData[i - 1u].value ) { return geometry_status_t::IDENTITY_CONFLICT; }
    }
    geometry_source_id_remap_t remap{};
    if ( !Vector_Init( &remap.entries, pA ) || !Vector_Resize( &remap.entries, ids.nCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    geometry_source_id_allocator_t fresh = pDestination->sourceIds.allocator; // a copy: the document is not advanced
    for ( usize i = 0u; i < ids.nCount; ++i ) {
        const geometry_source_id_result_t r = GeometrySourceIdAllocator_Allocate( &fresh );
        if ( r.status != geometry_status_t::OK ) { return r.status; }
        remap.entries.pData[i] = geometry_source_id_remap_entry_t{ ids.pData[i], r.id };
    }
    auto lookup = [&]( geometry_source_id_t old ) noexcept {
        geometry_source_id_t out{};
        (void)GeometrySourceIdRemap_Find( &remap, old, &out );
        return out;
    };

    // Rebuild every object with the new IDs privately, then swap.
    geometry_fragment_t rebuilt{};
    geometry_status_t st = GeometryFragment_Init( &rebuilt, pA );
    for ( usize i = 0u; st == geometry_status_t::OK && i < pFragment->brushes.nCount; ++i ) {
        st = AppendCopy( pA, &rebuilt.brushes,
                         [&]( brush_source_t *pOut ) noexcept {
                             const geometry_status_t cs = BrushSource_TryClone( pFragment->brushes.pData[i], pA, pDestination->policy, pOut );
                             if ( cs != geometry_status_t::OK ) { return cs; }
                             pOut->solid.sourceId = lookup( pOut->solid.sourceId );
                             for ( usize k = 0u; k < pOut->solid.sides.nCount; ++k ) {
                                 pOut->solid.sides.pData[k].sourceId = lookup( pOut->solid.sides.pData[k].sourceId );
                             }
                             return geometry_status_t::OK;
                         },
                         FreeBrushSource );
    }
    mesh_source_description_t desc{};
    if ( st == geometry_status_t::OK && MeshSourceDescription_Init( &desc, pA, GEOMETRY_SOURCE_ID_INVALID ) != geometry_status_t::OK ) {
        st = geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; st == geometry_status_t::OK && i < pFragment->meshes.nCount; ++i ) {
        st = MeshSource_TryDescribe( pFragment->meshes.pData[i], &desc );
        if ( st != geometry_status_t::OK ) { break; }
        desc.sourceId = lookup( desc.sourceId );
        for ( usize k = 0u; k < desc.vertices.nCount; ++k ) { desc.vertices.pData[k].sourceId = lookup( desc.vertices.pData[k].sourceId ); }
        for ( usize k = 0u; k < desc.faces.nCount; ++k ) { desc.faces.pData[k].sourceId = lookup( desc.faces.pData[k].sourceId ); }
        st = AppendCopy( pA, &rebuilt.meshes, [&]( mesh_source_t *pOut ) noexcept { return MeshSource_TryBuild( &desc, pA, pOut ); },
                         FreeMesh );
    }
    MeshSourceDescription_Shutdown( &desc );
    for ( usize i = 0u; st == geometry_status_t::OK && i < pFragment->patches.nCount; ++i ) {
        st = AppendCopy( pA, &rebuilt.patches,
                         [&]( patch_surface_t *pOut ) noexcept {
                             const geometry_status_t cs = Patch_TryClone( pFragment->patches.pData[i], pA, pOut );
                             if ( cs != geometry_status_t::OK ) { return cs; }
                             pOut->sourceId = lookup( pOut->sourceId );
                             for ( usize k = 0u; k < pOut->controls.nCount; ++k ) {
                                 pOut->controls.pData[k].sourceId = lookup( pOut->controls.pData[k].sourceId );
                             }
                             return geometry_status_t::OK;
                         },
                         FreePatch );
    }
    for ( usize i = 0u; st == geometry_status_t::OK && i < pFragment->heightFields.nCount; ++i ) {
        st = AppendCopy( pA, &rebuilt.heightFields,
                         [&]( heightfield_t *pOut ) noexcept {
                             const geometry_status_t cs = HeightField_TryClone( pFragment->heightFields.pData[i], pA, pOut );
                             if ( cs != geometry_status_t::OK ) { return cs; }
                             pOut->sourceId = lookup( pOut->sourceId );
                             for ( usize k = 0u; k < pOut->tiles.nCount; ++k ) {
                                 pOut->tiles.pData[k].sourceId = lookup( pOut->tiles.pData[k].sourceId );
                             }
                             return geometry_status_t::OK;
                         },
                         FreeField );
    }
    if ( st != geometry_status_t::OK ) {
        GeometryFragment_Shutdown( &rebuilt );
        return st;
    }
    // Swap the rebuilt objects in (allocation-free).
    GeometryFragment_Clear( pFragment );
    Vector_Shutdown( &pFragment->brushes );
    Vector_Shutdown( &pFragment->meshes );
    Vector_Shutdown( &pFragment->patches );
    Vector_Shutdown( &pFragment->heightFields );
    Vector_Move( &pFragment->brushes, &rebuilt.brushes );
    Vector_Move( &pFragment->meshes, &rebuilt.meshes );
    Vector_Move( &pFragment->patches, &rebuilt.patches );
    Vector_Move( &pFragment->heightFields, &rebuilt.heightFields );
    rebuilt.pAllocator = nullptr;
    if ( pRemapOut != nullptr ) { Vector_Move( &pRemapOut->entries, &remap.entries ); }
    return geometry_status_t::OK;
}

geometry_status_t GeometryFragment_TryTranslate( geometry_fragment_t *pFragment, math::vec3d_t offset ) noexcept
{
    if ( !GeometryFragment_IsInitialized( pFragment ) ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !math::Vec3d_IsFinite( offset ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    // Check every moved coordinate first so a failure changes nothing.
    // Brushes: planes shift, so check the moved projection origins and the
    // plane offsets stay finite; the brush vertices move by exactly offset.
    for ( usize i = 0u; i < pFragment->brushes.nCount; ++i ) {
        const brush_source_t &b = *pFragment->brushes.pData[i];
        for ( usize k = 0u; k < b.solid.sides.nCount; ++k ) {
            const math::planed_t &pl = b.solid.sides.pData[k].plane;
            if ( !std::isfinite( pl.d - math::Vec3d_Dot( pl.normal, offset ) ) ) { return geometry_status_t::NUMERIC_FAILURE; }
        }
        for ( usize k = 0u; k < b.attributes.records.nCount; ++k ) {
            if ( !math::Vec3d_IsFinite( math::Vec3d_Add( b.attributes.records.pData[k].uvProjection.origin, offset ) ) ) {
                return geometry_status_t::NUMERIC_FAILURE;
            }
        }
    }
    for ( usize i = 0u; i < pFragment->meshes.nCount; ++i ) {
        bool bOk = true;
        (void)GenerationPool_ForEach( &pFragment->meshes.pData[i]->mesh.vertices,
                                      [&]( geometry_mesh_vertex_handle_t, const mesh_vertex_record_t &v ) noexcept -> bool_t {
                                          bOk = InRange( math::Vec3d_Add( v.position, offset ), kMeshSourceCoordinateMax );
                                          return bOk;
                                      } );
        if ( !bOk ) { return geometry_status_t::NUMERIC_FAILURE; }
    }
    for ( usize i = 0u; i < pFragment->patches.nCount; ++i ) {
        const patch_surface_t &p = *pFragment->patches.pData[i];
        for ( usize k = 0u; k < p.controls.nCount; ++k ) {
            if ( !InRange( math::Vec3d_Add( p.controls.pData[k].position, offset ), kPatchCoordinateMax ) ) {
                return geometry_status_t::NUMERIC_FAILURE;
            }
        }
    }
    for ( usize i = 0u; i < pFragment->heightFields.nCount; ++i ) {
        const heightfield_t &f = *pFragment->heightFields.pData[i];
        const math::vec3d_t o = math::Vec3d_Add( f.origin, offset );
        // Samples sit at origin.z + h, so the z extent moves with the origin too.
        f64 hMin = 0.0;
        f64 hMax = 0.0;
        for ( usize k = 0u; k < f.heights.nCount; ++k ) {
            hMin = std::min( hMin, f.heights.pData[k] );
            hMax = std::max( hMax, f.heights.pData[k] );
        }
        const math::vec3d_t lo = math::Vec3d_Make( o.x, o.y, o.z + hMin );
        const math::vec3d_t hi = math::Vec3d_Make( o.x + f.cellSize * f.cCellsX, o.y + f.cellSize * f.cCellsY, o.z + hMax );
        if ( !InRange( lo, kHeightFieldCoordinateMax ) || !InRange( hi, kHeightFieldCoordinateMax ) ) {
            return geometry_status_t::NUMERIC_FAILURE;
        }
    }
    // Apply.
    for ( usize i = 0u; i < pFragment->brushes.nCount; ++i ) {
        brush_source_t &b = *pFragment->brushes.pData[i];
        for ( usize k = 0u; k < b.solid.sides.nCount; ++k ) {
            math::planed_t &pl = b.solid.sides.pData[k].plane;
            pl.d -= math::Vec3d_Dot( pl.normal, offset );
        }
        for ( usize k = 0u; k < b.attributes.records.nCount; ++k ) {
            math::planar_uv_mappingd_t &m = b.attributes.records.pData[k].uvProjection;
            m.origin = math::Vec3d_Add( m.origin, offset );
        }
    }
    for ( usize i = 0u; i < pFragment->meshes.nCount; ++i ) {
        (void)GenerationPool_ForEach( &pFragment->meshes.pData[i]->mesh.vertices,
                                      [&]( geometry_mesh_vertex_handle_t, mesh_vertex_record_t &v ) noexcept -> bool_t {
                                          v.position = math::Vec3d_Add( v.position, offset );
                                          return true;
                                      } );
    }
    for ( usize i = 0u; i < pFragment->patches.nCount; ++i ) {
        patch_surface_t &p = *pFragment->patches.pData[i];
        for ( usize k = 0u; k < p.controls.nCount; ++k ) { p.controls.pData[k].position = math::Vec3d_Add( p.controls.pData[k].position, offset ); }
    }
    for ( usize i = 0u; i < pFragment->heightFields.nCount; ++i ) {
        heightfield_t &f = *pFragment->heightFields.pData[i];
        f.origin = math::Vec3d_Add( f.origin, offset );
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometryFragment_TryInsert(
    const geometry_fragment_t *pFragment,
    geometry_document_t *pDocument,
    vector_t<geometry_source_id_t> *pRootIdsOut ) noexcept
{
    if ( !GeometryFragment_IsInitialized( pFragment ) || !GeometryDocument_IsInitialized( pDocument ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( pRootIdsOut != nullptr && pRootIdsOut->pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    const usize cObjects = GeometryFragment_ObjectCount( pFragment );
    if ( pRootIdsOut != nullptr && !Vector_Reserve( pRootIdsOut, pRootIdsOut->nCount + cObjects ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    // Registry state to restore on failure, and the store sizes to cut back to.
    geometry_source_id_registry_t before{};
    geometry_status_t st = GeometrySourceIdRegistry_TryClone( &pDocument->sourceIds, &before );
    if ( st != geometry_status_t::OK ) { return st; }
    const geometry_document_counts_t counts = GeometryDocument_InternalCounts( pDocument );

    for ( usize i = 0u; st == geometry_status_t::OK && i < pFragment->brushes.nCount; ++i ) {
        st = GeometryDocument_TryAddBrushSource( pDocument, pFragment->brushes.pData[i] );
    }
    for ( usize i = 0u; st == geometry_status_t::OK && i < pFragment->meshes.nCount; ++i ) {
        st = GeometryDocument_TryAddMesh( pDocument, pFragment->meshes.pData[i] );
    }
    for ( usize i = 0u; st == geometry_status_t::OK && i < pFragment->patches.nCount; ++i ) {
        st = GeometryDocument_TryAddPatch( pDocument, pFragment->patches.pData[i] );
    }
    for ( usize i = 0u; st == geometry_status_t::OK && i < pFragment->heightFields.nCount; ++i ) {
        st = GeometryDocument_TryAddHeightField( pDocument, pFragment->heightFields.pData[i] );
    }
    if ( st != geometry_status_t::OK ) {
        GeometryDocument_InternalTruncate( pDocument, counts );
        GeometryDocument_InternalSwapRegistry( pDocument, &before );
        GeometrySourceIdRegistry_Shutdown( &before ); // now holds the abandoned state
        return st;
    }
    GeometrySourceIdRegistry_Shutdown( &before );
    if ( pRootIdsOut != nullptr ) {
        for ( usize i = 0u; i < pFragment->brushes.nCount; ++i ) { (void)Vector_PushBack( pRootIdsOut, pFragment->brushes.pData[i]->solid.sourceId ); }
        for ( usize i = 0u; i < pFragment->meshes.nCount; ++i ) { (void)Vector_PushBack( pRootIdsOut, pFragment->meshes.pData[i]->sourceId ); }
        for ( usize i = 0u; i < pFragment->patches.nCount; ++i ) { (void)Vector_PushBack( pRootIdsOut, pFragment->patches.pData[i]->sourceId ); }
        for ( usize i = 0u; i < pFragment->heightFields.nCount; ++i ) {
            (void)Vector_PushBack( pRootIdsOut, pFragment->heightFields.pData[i]->sourceId );
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometryDocument_TryDuplicate(
    geometry_document_t *pDocument,
    span_t<const geometry_source_id_t> rootIds,
    math::vec3d_t offset,
    vector_t<geometry_source_id_t> *pNewRootIdsOut,
    geometry_source_id_remap_t *pRemapOut ) noexcept
{
    if ( !GeometryDocument_IsInitialized( pDocument ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_fragment_t fragment{};
    geometry_source_id_remap_t remap{};
    geometry_status_t st = GeometryFragment_Init( &fragment, pDocument->pAllocator );
    if ( st == geometry_status_t::OK ) { st = GeometryFragment_TryExtract( pDocument, rootIds, &fragment ); }
    if ( st == geometry_status_t::OK ) { st = GeometryFragment_TryRemapForDocument( &fragment, pDocument, &remap ); }
    if ( st == geometry_status_t::OK ) { st = GeometryFragment_TryTranslate( &fragment, offset ); }
    if ( st == geometry_status_t::OK ) { st = GeometryFragment_TryInsert( &fragment, pDocument, pNewRootIdsOut ); }
    if ( st == geometry_status_t::OK && pRemapOut != nullptr && pRemapOut->entries.pAllocator == nullptr ) {
        Vector_Move( &pRemapOut->entries, &remap.entries );
    }
    GeometrySourceIdRemap_Shutdown( &remap );
    GeometryFragment_Shutdown( &fragment );
    return st;
}

geometry_status_t GeometryFragment_TrySaveToText(
    const geometry_fragment_t *pFragment,
    const geometry_policy_t &policy,
    text_buffer_t *pTextOut ) noexcept
{
    if ( !GeometryFragment_IsInitialized( pFragment ) || pTextOut == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    geometry_document_t document{};
    geometry_status_t st = GeometryDocument_Init( &document, pFragment->pAllocator, policy );
    if ( st == geometry_status_t::OK ) { st = GeometryFragment_TryInsert( pFragment, &document, nullptr ); }
    if ( st == geometry_status_t::OK ) {
        const geometry_serialization_result_t r = GeometrySerialization_SaveToText( &document, pTextOut );
        st = r.status == geometry_serialization_status_t::OK ? geometry_status_t::OK
             : r.status == geometry_serialization_status_t::OUT_OF_MEMORY ? geometry_status_t::ALLOCATION_FAILED
                                                                          : geometry_status_t::CORRUPT_STATE;
    }
    GeometryDocument_Shutdown( &document );
    return st;
}

geometry_status_t GeometryFragment_TryLoadFromText(
    string_view_t text,
    const geometry_policy_t &policy,
    geometry_fragment_t *pFragment ) noexcept
{
    if ( !GeometryFragment_IsInitialized( pFragment ) ) { return geometry_status_t::NOT_INITIALIZED; }
    geometry_document_t document{};
    const geometry_serialization_result_t r = GeometrySerialization_LoadFromText( text, pFragment->pAllocator, policy, &document );
    if ( r.status != geometry_serialization_status_t::OK ) {
        return r.status == geometry_serialization_status_t::OUT_OF_MEMORY ? geometry_status_t::ALLOCATION_FAILED
                                                                          : geometry_status_t::INVALID_ARGUMENT;
    }
    const geometry_status_t st = GeometryFragment_TryExtractAll( &document, pFragment );
    GeometryDocument_Shutdown( &document );
    return st;
}

} // namespace cypher::editor::geometry
