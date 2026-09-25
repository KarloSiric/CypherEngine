//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookSurfaces.cpp
//  Purpose: Implements the shared cook surface soup.
//
//  History:
//  - Created by Karlo Siric on 2026-09-25
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CookSurfaces.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushTessellation.h"
#include "CypherGeometry_HeightFieldTessellation.h"
#include "CypherGeometry_MeshTessellation.h"
#include "CypherGeometry_PatchTessellation.h"

#include <algorithm>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

struct ctx_t {
    cook_surface_soup_t *pSoup{ nullptr };
    const allocator_t *pA{ nullptr };
    bool bOk{ true };
};

bool AddVertices( ctx_t *c, const math::vec3d_t *p, usize n ) noexcept
{
    c->bOk = c->bOk && Vector_Append( &c->pSoup->positions, span_t<const math::vec3d_t>{ p, n } );
    return c->bOk;
}

void AddTriangle( ctx_t *c, u32 iObject, u32 base, u32 a, u32 b, u32 d, geometry_source_id_t element, geometry_material_ref_t material ) noexcept
{
    if ( !c->bOk ) { return; }
    cook_soup_triangle_t t{};
    t.v[0] = base + a;
    t.v[1] = base + b;
    t.v[2] = base + d;
    t.iObject = iObject;
    t.elementId = element;
    t.material = material;
    const math::vec3d_t p0 = c->pSoup->positions.pData[t.v[0]], p1 = c->pSoup->positions.pData[t.v[1]], p2 = c->pSoup->positions.pData[t.v[2]];
    const math::vec3d_t n = math::Vec3d_Cross( math::Vec3d_Subtract( p1, p0 ), math::Vec3d_Subtract( p2, p0 ) );
    const f64 len = std::sqrt( math::Vec3d_LengthSquared( n ) );
    t.area = 0.5 * len;
    t.normal = len > 0.0 ? math::Vec3d_Scale( n, 1.0 / len ) : math::vec3d_t{};
    c->bOk = Vector_PushBack( &c->pSoup->triangles, t );
}

void Problem( ctx_t *c, geometry_source_id_t object, geometry_source_id_t element, geometry_status_t st ) noexcept
{
    c->bOk = c->bOk && Vector_PushBack( &c->pSoup->problems, cook_soup_problem_t{ object, element, st } );
}

bool Brush( ctx_t *c, const geometry_snapshot_t *pSnap, const brush_solid_t *pBrush, u32 iObject, cook_soup_object_t *pObj ) noexcept
{
    brush_boundary_t boundary{};
    brush_tessellation_t tess{};
    geometry_status_t st = BrushBoundary_Init( &boundary, c->pA );
    if ( st == geometry_status_t::OK ) { st = BrushTessellation_Init( &tess, c->pA ); }
    if ( st == geometry_status_t::OK ) { st = BrushBoundary_TryReconstruct( &boundary, pBrush, pSnap->policy ); }
    if ( st == geometry_status_t::OK ) { st = BrushTessellation_TryBuild( &tess, &boundary ); }
    const bool bBuilt = st == geometry_status_t::OK;
    if ( !bBuilt ) {
        if ( st == geometry_status_t::ALLOCATION_FAILED ) { c->bOk = false; }
        Problem( c, pBrush->sourceId, geometry_source_id_t{}, st );
    } else {
        const geometry_brush_side_attribute_store_t *pRecords = GeometrySnapshot_FindBrushAttributes( pSnap, pBrush->sourceId );
        const u32 base = static_cast<u32>( c->pSoup->positions.nCount );
        if ( AddVertices( c, boundary.vertices.pData, boundary.vertices.nCount ) ) {
            pObj->cVertices = static_cast<u32>( boundary.vertices.nCount );
            for ( usize t = 0u; t < tess.triangles.nCount && c->bOk; ++t ) {
                const brush_tessellation_triangle_t &tri = tess.triangles.pData[t];
                geometry_source_id_t side{};
                geometry_material_ref_t material{};
                if ( tri.iSourceSide < pBrush->sides.nCount ) {
                    side = pBrush->sides.pData[tri.iSourceSide].sourceId;
                    geometry_brush_side_attributes_t r{};
                    if ( pRecords != nullptr &&
                         BrushSideAttributeStore_TryGet( pRecords, pBrush->sides.pData[tri.iSourceSide].iAttributeIndex, &r ) == geometry_status_t::OK ) {
                        material = r.material;
                    }
                }
                AddTriangle( c, iObject, base, tri.iVertex0, tri.iVertex1, tri.iVertex2, side, material );
            }
            pObj->bClosed = true;
            pObj->bConvex = true;
        }
    }
    BrushTessellation_Shutdown( &tess );
    BrushBoundary_Shutdown( &boundary );
    return bBuilt;
}

bool Mesh( ctx_t *c, const mesh_source_t *pMesh, u32 iObject, cook_soup_object_t *pObj ) noexcept
{
    mesh_tessellation_t tess{};
    geometry_mesh_face_handle_t hFailed{};
    geometry_status_t st = MeshTessellation_Init( &tess, c->pA );
    if ( st == geometry_status_t::OK ) { st = MeshTessellation_TryBuild( &pMesh->mesh, &tess, &hFailed ); }
    const bool bBuilt = st == geometry_status_t::OK;
    if ( !bBuilt ) {
        if ( st == geometry_status_t::ALLOCATION_FAILED ) { c->bOk = false; }
        Problem( c, pMesh->sourceId, MeshSource_FaceId( pMesh, hFailed ), st );
    } else {
        const u32 base = static_cast<u32>( c->pSoup->positions.nCount );
        if ( AddVertices( c, tess.positions.pData, tess.positions.nCount ) ) {
            pObj->cVertices = static_cast<u32>( tess.positions.nCount );
            const usize cTris = tess.indices.nCount / 3u;
            for ( usize t = 0u; t < cTris && c->bOk; ++t ) {
                const geometry_mesh_face_handle_t h = tess.triangleFace.pData[t];
                AddTriangle( c, iObject, base, tess.indices.pData[3u * t], tess.indices.pData[3u * t + 1u], tess.indices.pData[3u * t + 2u],
                             MeshSource_FaceId( pMesh, h ), MeshAttributeStore_GetFace( &pMesh->attributes, h ).material );
            }
            bool bClosed = true;
            (void)GenerationPool_ForEach( &pMesh->mesh.halfEdges, [&]( geometry_mesh_half_edge_handle_t, const mesh_half_edge_record_t &h ) noexcept -> bool_t {
                bClosed = GeometryHandle_IsValid( h.hTwin );
                return bClosed;
            } );
            pObj->bClosed = bClosed;
        }
    }
    MeshTessellation_Shutdown( &tess );
    return bBuilt;
}

bool Patch( ctx_t *c, const patch_surface_t *pPatch, u32 iObject, cook_soup_object_t *pObj ) noexcept
{
    patch_tessellation_t tess{};
    geometry_status_t st = PatchTessellation_Init( &tess, c->pA );
    if ( st == geometry_status_t::OK ) { st = PatchTessellation_TryBuild( pPatch, patch_tessellation_options_t{}, &tess ); }
    const bool bBuilt = st == geometry_status_t::OK;
    if ( !bBuilt ) {
        if ( st == geometry_status_t::ALLOCATION_FAILED ) { c->bOk = false; }
        Problem( c, pPatch->sourceId, geometry_source_id_t{}, st );
    } else {
        geometry_material_ref_t material{};
        material.value = pPatch->materialId;
        const u32 base = static_cast<u32>( c->pSoup->positions.nCount );
        if ( AddVertices( c, tess.positions.pData, tess.positions.nCount ) ) {
            pObj->cVertices = static_cast<u32>( tess.positions.nCount );
            const usize cTris = tess.indices.nCount / 3u;
            for ( usize t = 0u; t < cTris && c->bOk; ++t ) {
                AddTriangle( c, iObject, base, tess.indices.pData[3u * t], tess.indices.pData[3u * t + 1u], tess.indices.pData[3u * t + 2u],
                             pPatch->sourceId, material );
            }
        }
    }
    PatchTessellation_Shutdown( &tess );
    return bBuilt;
}

bool Field( ctx_t *c, const heightfield_t *pField, u32 iObject, cook_soup_object_t *pObj ) noexcept
{
    const u32 sx = HeightField_SamplesX( pField ), sy = HeightField_SamplesY( pField );
    const u32 base = static_cast<u32>( c->pSoup->positions.nCount );
    if ( !Vector_Reserve( &c->pSoup->positions, base + static_cast<usize>( sx ) * sy ) ) {
        c->bOk = false;
        return false;
    }
    for ( u32 y = 0u; y < sy; ++y ) {
        for ( u32 x = 0u; x < sx; ++x ) { (void)Vector_PushBack( &c->pSoup->positions, HeightField_SamplePosition( pField, x, y ) ); }
    }
    pObj->cVertices = sx * sy;
    heightfield_tile_mesh_t tile{};
    geometry_status_t st = HeightFieldTileMesh_Init( &tile, c->pA );
    for ( u32 t = 0u; st == geometry_status_t::OK && c->bOk && t < pField->tiles.nCount; ++t ) {
        st = HeightFieldTessellation_TryBuildTile( pField, t, heightfield_tile_lods_t{}, &tile );
        if ( st != geometry_status_t::OK ) {
            Problem( c, pField->sourceId, pField->tiles.pData[t].sourceId, st );
            break;
        }
        const usize cTris = tile.indices.nCount / 3u;
        for ( usize k = 0u; k < cTris && c->bOk; ++k ) {
            AddTriangle( c, iObject, base, tile.vertexSample.pData[tile.indices.pData[3u * k]], tile.vertexSample.pData[tile.indices.pData[3u * k + 1u]],
                         tile.vertexSample.pData[tile.indices.pData[3u * k + 2u]], pField->tiles.pData[t].sourceId, geometry_material_ref_t{} );
        }
    }
    if ( st == geometry_status_t::ALLOCATION_FAILED ) { c->bOk = false; }
    HeightFieldTileMesh_Shutdown( &tile );
    return st == geometry_status_t::OK;
}

void Clear( cook_surface_soup_t *p ) noexcept
{
    Vector_Clear( &p->positions );
    Vector_Clear( &p->triangles );
    Vector_Clear( &p->objects );
    Vector_Clear( &p->problems );
    p->lo = p->hi = math::vec3d_t{};
    p->revision = GEOMETRY_REVISION_INITIAL;
}

} // namespace

geometry_status_t CookSurfaces_Init( cook_surface_soup_t *p, const allocator_t *pA ) noexcept
{
    if ( p == nullptr || !Allocator_IsValid( pA ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( !Vector_Init( &p->positions, pA ) || !Vector_Init( &p->triangles, pA ) || !Vector_Init( &p->objects, pA ) || !Vector_Init( &p->problems, pA ) ) {
        CookSurfaces_Shutdown( p );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void CookSurfaces_Shutdown( cook_surface_soup_t *p ) noexcept
{
    if ( p == nullptr ) { return; }
    Vector_Shutdown( &p->positions );
    Vector_Shutdown( &p->triangles );
    Vector_Shutdown( &p->objects );
    Vector_Shutdown( &p->problems );
}

geometry_status_t CookSurfaces_TryBuild( const geometry_snapshot_t *pSnap, cook_surface_soup_t *pSoup ) noexcept
{
    if ( !GeometrySnapshot_IsInitialized( pSnap ) || pSoup == nullptr || pSoup->positions.pAllocator == nullptr ) { return geometry_status_t::INVALID_ARGUMENT; }
    Clear( pSoup );
    ctx_t c{ pSoup, pSoup->positions.pAllocator };
    // Every object, in ascending ID order.
    struct item_t {
        u64 id;
        cook_source_kind_t kind;
        const void *p;
    };
    vector_t<item_t> items{};
    if ( !Vector_Init( &items, c.pA ) ) { return geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0u; c.bOk && i < GeometrySnapshot_BrushCount( pSnap ); ++i ) {
        const brush_solid_t *p = pSnap->brushes.pData[i];
        c.bOk = Vector_PushBack( &items, item_t{ p->sourceId.value, cook_source_kind_t::BRUSH, p } );
    }
    for ( usize i = 0u; c.bOk && i < GeometrySnapshot_MeshCount( pSnap ); ++i ) {
        const mesh_source_t *p = GeometrySnapshot_MeshAt( pSnap, i );
        c.bOk = Vector_PushBack( &items, item_t{ p->sourceId.value, cook_source_kind_t::MESH, p } );
    }
    for ( usize i = 0u; c.bOk && i < GeometrySnapshot_PatchCount( pSnap ); ++i ) {
        const patch_surface_t *p = GeometrySnapshot_PatchAt( pSnap, i );
        c.bOk = Vector_PushBack( &items, item_t{ p->sourceId.value, cook_source_kind_t::PATCH, p } );
    }
    for ( usize i = 0u; c.bOk && i < GeometrySnapshot_HeightFieldCount( pSnap ); ++i ) {
        const heightfield_t *p = GeometrySnapshot_HeightFieldAt( pSnap, i );
        c.bOk = Vector_PushBack( &items, item_t{ p->sourceId.value, cook_source_kind_t::HEIGHTFIELD, p } );
    }
    std::sort( items.pData, items.pData + items.nCount, []( const item_t &x, const item_t &y ) { return x.id < y.id; } );
    for ( usize i = 0u; c.bOk && i < items.nCount; ++i ) {
        const item_t &it = items.pData[i];
        cook_soup_object_t obj{};
        obj.objectId.value = it.id;
        obj.kind = it.kind;
        obj.iFirstVertex = static_cast<u32>( pSoup->positions.nCount );
        obj.iFirstTriangle = static_cast<u32>( pSoup->triangles.nCount );
        const u32 iObject = static_cast<u32>( pSoup->objects.nCount );
        bool bBuilt = false;
        switch ( it.kind ) {
        case cook_source_kind_t::BRUSH: bBuilt = Brush( &c, pSnap, static_cast<const brush_solid_t *>( it.p ), iObject, &obj ); break;
        case cook_source_kind_t::MESH: bBuilt = Mesh( &c, static_cast<const mesh_source_t *>( it.p ), iObject, &obj ); break;
        case cook_source_kind_t::PATCH: bBuilt = Patch( &c, static_cast<const patch_surface_t *>( it.p ), iObject, &obj ); break;
        case cook_source_kind_t::HEIGHTFIELD: bBuilt = Field( &c, static_cast<const heightfield_t *>( it.p ), iObject, &obj ); break;
        default: break;
        }
        if ( !bBuilt ) {
            pSoup->positions.nCount = obj.iFirstVertex;
            pSoup->triangles.nCount = obj.iFirstTriangle;
            continue;
        }
        obj.cTriangles = static_cast<u32>( pSoup->triangles.nCount ) - obj.iFirstTriangle;
        c.bOk = c.bOk && Vector_PushBack( &pSoup->objects, obj );
    }
    if ( !c.bOk ) {
        Clear( pSoup );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize v = 0u; v < pSoup->positions.nCount; ++v ) {
        if ( v == 0u ) { pSoup->lo = pSoup->hi = pSoup->positions.pData[0]; }
        pSoup->lo = math::Vec3d_Min( pSoup->lo, pSoup->positions.pData[v] );
        pSoup->hi = math::Vec3d_Max( pSoup->hi, pSoup->positions.pData[v] );
    }
    pSoup->revision = GeometrySnapshot_GetRevision( pSnap );
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
