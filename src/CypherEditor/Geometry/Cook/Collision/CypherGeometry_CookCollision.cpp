//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookCollision.cpp
//  Purpose: Implements the collision cook with checked float conversion,
//           per-element diagnostics, and key-based surface reuse.
//  Details: Each surface's data hash covers exactly the bytes a consumer
//           reads (float bits, indices, element IDs, flags), so the combined
//           content hash is equal for equal outputs whether surfaces were
//           reused or rebuilt.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CookCollision.h"
#include "CypherGeometry_HeightFieldTessellation.h"
#include "CypherGeometry_PatchTessellation.h"

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushTessellation.h"
#include "CypherGeometry_CookBytes.h"
#include "CypherGeometry_MeshTessellation.h"

#include <cfloat>
#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

bool TryToFloat( math::vec3d_t p, cook_float3_t *pOut ) noexcept
{
    if ( !math::Vec3d_IsFinite( p ) || std::fabs( p.x ) > FLT_MAX || std::fabs( p.y ) > FLT_MAX ||
         std::fabs( p.z ) > FLT_MAX ) {
        return false;
    }
    *pOut = cook_float3_t{ static_cast<f32>( p.x ), static_cast<f32>( p.y ), static_cast<f32>( p.z ) };
    return true;
}

bool FloatTriangleDegenerate( cook_float3_t a, cook_float3_t b, cook_float3_t c ) noexcept
{
    // Evaluated in double from the float values: the question is whether
    // the *float* triangle has area, not whether the double one had.
    const f64 ux = static_cast<f64>( b.x ) - a.x, uy = static_cast<f64>( b.y ) - a.y, uz = static_cast<f64>( b.z ) - a.z;
    const f64 vx = static_cast<f64>( c.x ) - a.x, vy = static_cast<f64>( c.y ) - a.y, vz = static_cast<f64>( c.z ) - a.z;
    const f64 nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    return nx == 0.0 && ny == 0.0 && nz == 0.0;
}

struct build_context_t {
    cook_collision_t *pOut;
    usize cDiagnosticsMax;
    bool bOk{ true };

    void Diagnose( cook_diagnostic_kind_t kind, geometry_source_id_t object, geometry_source_id_t element ) noexcept
    {
        if ( pOut->diagnostics.nCount >= cDiagnosticsMax ) { return; } // bounded, never fatal
        bOk = bOk && Vector_PushBack( &pOut->diagnostics, cook_diagnostic_t{ kind, object, element } );
    }
};

content_hash_t HashSurfaceData( const cook_collision_t *pOut, const cook_collision_surface_t &s, cook_byte_writer_t *pW ) noexcept
{
    CookBytes_Clear( pW );
    CookBytes_U32( pW, kCookCollisionVersion );
    CookBytes_U64( pW, s.objectId.value );
    CookBytes_U32( pW, static_cast<u32>( s.kind ) );
    CookBytes_U32( pW, ( s.bConvex ? 1u : 0u ) | ( s.bClosed ? 2u : 0u ) );
    CookBytes_U32( pW, s.cVertices );
    for ( u32 i = 0u; i < s.cVertices; ++i ) {
        const cook_float3_t &p = pOut->positions.pData[s.iFirstVertex + i];
        CookBytes_F32( pW, p.x );
        CookBytes_F32( pW, p.y );
        CookBytes_F32( pW, p.z );
    }
    CookBytes_U32( pW, s.cTriangles );
    for ( u32 t = 0u; t < s.cTriangles; ++t ) {
        const usize iTri = s.iFirstTriangle + t;
        CookBytes_U32( pW, pOut->indices.pData[iTri * 3u] );
        CookBytes_U32( pW, pOut->indices.pData[iTri * 3u + 1u] );
        CookBytes_U32( pW, pOut->indices.pData[iTri * 3u + 2u] );
        CookBytes_U64( pW, pOut->triangleElement.pData[iTri].value );
    }
    return CookBytes_Hash( pW );
}

// Appends vertices; returns false (with a diagnostic) on float range.
bool AppendVertices( build_context_t *pCtx, geometry_source_id_t objectId, const math::vec3d_t *pSrc, usize cSrc ) noexcept
{
    cook_collision_t *pOut = pCtx->pOut;
    const usize cBefore = pOut->positions.nCount;
    if ( !Vector_Reserve( &pOut->positions, cBefore + cSrc ) ) {
        pCtx->bOk = false;
        return false;
    }
    for ( usize i = 0u; i < cSrc; ++i ) {
        cook_float3_t f{};
        if ( !TryToFloat( pSrc[i], &f ) ) {
            pOut->positions.nCount = cBefore;
            pCtx->Diagnose( cook_diagnostic_kind_t::FLOAT_RANGE, objectId, geometry_source_id_t{} );
            return false;
        }
        (void)Vector_PushBack( &pOut->positions, f );
    }
    return true;
}

void AppendTriangle(
    build_context_t *pCtx,
    cook_collision_surface_t *pSurface,
    u32 a,
    u32 b,
    u32 c,
    geometry_source_id_t elementId ) noexcept
{
    cook_collision_t *pOut = pCtx->pOut;
    const cook_float3_t *pP = pOut->positions.pData + pSurface->iFirstVertex;
    if ( FloatTriangleDegenerate( pP[a], pP[b], pP[c] ) ) {
        pCtx->Diagnose( cook_diagnostic_kind_t::DEGENERATE_AFTER_FLOAT, pSurface->objectId, elementId );
        return;
    }
    pCtx->bOk = pCtx->bOk && Vector_PushBack( &pOut->indices, a ) && Vector_PushBack( &pOut->indices, b ) &&
                Vector_PushBack( &pOut->indices, c ) && Vector_PushBack( &pOut->triangleElement, elementId );
    ++pSurface->cTriangles;
}

bool BuildBrush(
    build_context_t *pCtx,
    const brush_solid_t *pBrush,
    const geometry_policy_t &policy,
    cook_collision_surface_t *pSurface ) noexcept
{
    const allocator_t *pAllocator = pCtx->pOut->positions.pAllocator;
    brush_boundary_t boundary{};
    brush_tessellation_t tess{};
    bool bBuilt = false;
    if ( BrushBoundary_Init( &boundary, pAllocator ) == geometry_status_t::OK &&
         BrushTessellation_Init( &tess, pAllocator ) == geometry_status_t::OK &&
         BrushBoundary_TryReconstruct( &boundary, pBrush, policy ) == geometry_status_t::OK &&
         BrushTessellation_TryBuild( &tess, &boundary ) == geometry_status_t::OK ) {
        bBuilt = true;
    }
    if ( !bBuilt ) {
        pCtx->Diagnose( cook_diagnostic_kind_t::BOUNDARY_FAILED, pBrush->sourceId, geometry_source_id_t{} );
    } else if ( AppendVertices( pCtx, pBrush->sourceId, boundary.vertices.pData, boundary.vertices.nCount ) ) {
        pSurface->cVertices = static_cast<u32>( boundary.vertices.nCount );
        for ( usize t = 0u; t < tess.triangles.nCount && pCtx->bOk; ++t ) {
            const brush_tessellation_triangle_t &tri = tess.triangles.pData[t];
            const geometry_source_id_t side =
                tri.iSourceSide < pBrush->sides.nCount ? pBrush->sides.pData[tri.iSourceSide].sourceId : geometry_source_id_t{};
            AppendTriangle( pCtx, pSurface, tri.iVertex0, tri.iVertex1, tri.iVertex2, side );
        }
        pSurface->bConvex = true;
        pSurface->bClosed = true;
    } else {
        bBuilt = false;
    }
    BrushTessellation_Shutdown( &tess );
    BrushBoundary_Shutdown( &boundary );
    return bBuilt;
}

bool BuildMesh( build_context_t *pCtx, const mesh_source_t *pMesh, cook_collision_surface_t *pSurface ) noexcept
{
    const allocator_t *pAllocator = pCtx->pOut->positions.pAllocator;
    mesh_tessellation_t tess{};
    geometry_mesh_face_handle_t hFailed{};
    bool bBuilt = MeshTessellation_Init( &tess, pAllocator ) == geometry_status_t::OK &&
                  MeshTessellation_TryBuild( &pMesh->mesh, &tess, &hFailed ) == geometry_status_t::OK;
    if ( !bBuilt ) {
        pCtx->Diagnose( cook_diagnostic_kind_t::TESSELLATION_FAILED, pMesh->sourceId, MeshSource_FaceId( pMesh, hFailed ) );
    } else if ( AppendVertices( pCtx, pMesh->sourceId, tess.positions.pData, tess.positions.nCount ) ) {
        pSurface->cVertices = static_cast<u32>( tess.positions.nCount );
        const usize cTris = tess.indices.nCount / 3u;
        for ( usize t = 0u; t < cTris && pCtx->bOk; ++t ) {
            AppendTriangle( pCtx, pSurface, tess.indices.pData[t * 3u], tess.indices.pData[t * 3u + 1u],
                            tess.indices.pData[t * 3u + 2u], MeshSource_FaceId( pMesh, tess.triangleFace.pData[t] ) );
        }
        // Closed = every half-edge has a twin.
        bool bClosed = true;
        (void)GenerationPool_ForEach( &pMesh->mesh.halfEdges,
            [&]( geometry_mesh_half_edge_handle_t, const mesh_half_edge_record_t &h ) noexcept -> bool_t {
                bClosed = GeometryHandle_IsValid( h.hTwin );
                return bClosed;
            } );
        pSurface->bClosed = bClosed;
    } else {
        bBuilt = false;
    }
    MeshTessellation_Shutdown( &tess );
    return bBuilt;
}

// Patches: the adaptive tessellation (chord error within the patch
// tessellator's default tolerance), an open surface; every triangle names
// the patch, which has no finer authored element.
bool BuildPatch( build_context_t *pCtx, const patch_surface_t *pPatch, cook_collision_surface_t *pSurface ) noexcept
{
    const allocator_t *pAllocator = pCtx->pOut->positions.pAllocator;
    patch_tessellation_t tess{};
    patch_tessellation_options_t options{};
    bool bBuilt = PatchTessellation_Init( &tess, pAllocator ) == geometry_status_t::OK &&
                  PatchTessellation_TryBuild( pPatch, options, &tess ) == geometry_status_t::OK;
    if ( !bBuilt ) {
        pCtx->Diagnose( cook_diagnostic_kind_t::TESSELLATION_FAILED, pPatch->sourceId, geometry_source_id_t{} );
    } else if ( AppendVertices( pCtx, pPatch->sourceId, tess.positions.pData, tess.positions.nCount ) ) {
        pSurface->cVertices = static_cast<u32>( tess.positions.nCount );
        const usize cTris = tess.indices.nCount / 3u;
        for ( usize t = 0u; t < cTris && pCtx->bOk; ++t ) {
            AppendTriangle( pCtx, pSurface, tess.indices.pData[t * 3u], tess.indices.pData[t * 3u + 1u], tess.indices.pData[t * 3u + 2u],
                            pPatch->sourceId );
        }
    } else {
        bBuilt = false;
    }
    PatchTessellation_Shutdown( &tess );
    return bBuilt;
}

// Heightfields: one vertex per field sample (so tiles share their edges
// and the surface is watertight), every tile at full resolution, holes
// left open; each triangle names its tile.
bool BuildHeightField( build_context_t *pCtx, const heightfield_t *pField, cook_collision_surface_t *pSurface ) noexcept
{
    const allocator_t *pAllocator = pCtx->pOut->positions.pAllocator;
    const u32 sx = HeightField_SamplesX( pField ), sy = HeightField_SamplesY( pField );
    vector_t<math::vec3d_t> samples{};
    if ( !Vector_Init( &samples, pAllocator ) || !Vector_Resize( &samples, static_cast<usize>( sx ) * sy ) ) {
        pCtx->bOk = false;
        return false;
    }
    for ( u32 y = 0u; y < sy; ++y ) {
        for ( u32 x = 0u; x < sx; ++x ) { samples.pData[static_cast<usize>( y ) * sx + x] = HeightField_SamplePosition( pField, x, y ); }
    }
    if ( !AppendVertices( pCtx, pField->sourceId, samples.pData, samples.nCount ) ) { return false; }
    pSurface->cVertices = static_cast<u32>( samples.nCount );
    heightfield_tile_mesh_t tile{};
    bool bBuilt = HeightFieldTileMesh_Init( &tile, pAllocator ) == geometry_status_t::OK;
    for ( u32 t = 0u; bBuilt && pCtx->bOk && t < pField->tiles.nCount; ++t ) {
        if ( HeightFieldTessellation_TryBuildTile( pField, t, heightfield_tile_lods_t{}, &tile ) != geometry_status_t::OK ) {
            pCtx->Diagnose( cook_diagnostic_kind_t::TESSELLATION_FAILED, pField->sourceId, pField->tiles.pData[t].sourceId );
            bBuilt = false;
            break;
        }
        const usize cTris = tile.indices.nCount / 3u;
        for ( usize k = 0u; k < cTris && pCtx->bOk; ++k ) {
            AppendTriangle( pCtx, pSurface, tile.vertexSample.pData[tile.indices.pData[k * 3u]], tile.vertexSample.pData[tile.indices.pData[k * 3u + 1u]],
                            tile.vertexSample.pData[tile.indices.pData[k * 3u + 2u]], pField->tiles.pData[t].sourceId );
        }
    }
    HeightFieldTileMesh_Shutdown( &tile );
    return bBuilt;
}

const patch_surface_t *FindPatch( const geometry_snapshot_t *pSnap, geometry_source_id_t id ) noexcept
{
    for ( usize i = 0u; i < GeometrySnapshot_PatchCount( pSnap ); ++i ) {
        const patch_surface_t *p = GeometrySnapshot_PatchAt( pSnap, i );
        if ( p != nullptr && p->sourceId.value == id.value ) { return p; }
    }
    return nullptr;
}

const heightfield_t *FindHeightField( const geometry_snapshot_t *pSnap, geometry_source_id_t id ) noexcept
{
    for ( usize i = 0u; i < GeometrySnapshot_HeightFieldCount( pSnap ); ++i ) {
        const heightfield_t *p = GeometrySnapshot_HeightFieldAt( pSnap, i );
        if ( p != nullptr && p->sourceId.value == id.value ) { return p; }
    }
    return nullptr;
}

const cook_collision_surface_t *FindPrevious( const cook_collision_t *pPrev, geometry_source_id_t id ) noexcept
{
    if ( pPrev == nullptr ) { return nullptr; }
    // Surfaces are in ascending ID order: binary search.
    usize lo = 0u, hi = pPrev->surfaces.nCount;
    while ( lo < hi ) {
        const usize mid = ( lo + hi ) / 2u;
        if ( pPrev->surfaces.pData[mid].objectId.value < id.value ) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return ( lo < pPrev->surfaces.nCount && pPrev->surfaces.pData[lo].objectId.value == id.value ) ? &pPrev->surfaces.pData[lo]
                                                                                                  : nullptr;
}

void ClearOutput( cook_collision_t *pOut ) noexcept
{
    Vector_Clear( &pOut->positions );
    Vector_Clear( &pOut->indices );
    Vector_Clear( &pOut->triangleElement );
    Vector_Clear( &pOut->surfaces );
    Vector_Clear( &pOut->diagnostics );
    pOut->contentHash = CY_CONTENT_HASH_INVALID;
    pOut->revision = GEOMETRY_REVISION_INITIAL;
    pOut->cSurfacesReused = 0u;
    pOut->cSurfacesRebuilt = 0u;
}

} // namespace

geometry_status_t CookCollision_Init( cook_collision_t *pOut, const allocator_t *pAllocator ) noexcept
{
    if ( pOut == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( pOut->positions.pAllocator != nullptr ) { return geometry_status_t::ALREADY_INITIALIZED; }
    if ( !Vector_Init( &pOut->positions, pAllocator ) || !Vector_Init( &pOut->indices, pAllocator ) ||
         !Vector_Init( &pOut->triangleElement, pAllocator ) || !Vector_Init( &pOut->surfaces, pAllocator ) ||
         !Vector_Init( &pOut->diagnostics, pAllocator ) ) {
        CookCollision_Shutdown( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    ClearOutput( pOut );
    return geometry_status_t::OK;
}

void CookCollision_Shutdown( cook_collision_t *pOut ) noexcept
{
    if ( pOut == nullptr ) { return; }
    Vector_Shutdown( &pOut->positions );
    Vector_Shutdown( &pOut->indices );
    Vector_Shutdown( &pOut->triangleElement );
    Vector_Shutdown( &pOut->surfaces );
    Vector_Shutdown( &pOut->diagnostics );
}

geometry_status_t CookCollision_TryBuild(
    const geometry_snapshot_t *pSnapshot,
    const cook_key_set_t *pKeys,
    const cook_collision_t *pPrevious,
    cook_collision_t *pOut ) noexcept
{
    if ( pOut == nullptr || pOut->positions.pAllocator == nullptr ) { return geometry_status_t::NOT_INITIALIZED; }
    if ( !GeometrySnapshot_IsInitialized( pSnapshot ) || pKeys == nullptr || pPrevious == pOut ||
         pKeys->revision != GeometrySnapshot_GetRevision( pSnapshot ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    ClearOutput( pOut );
    const allocator_t *pAllocator = pOut->positions.pAllocator;
    cook_byte_writer_t w{};
    if ( !CookBytes_Init( &w, pAllocator ) ) { return geometry_status_t::ALLOCATION_FAILED; }

    build_context_t ctx{ pOut, static_cast<usize>( pSnapshot->policy.limits.cDiagnosticsMax ) };
    content_hash_t combined = ContentHash_Data( binary_block_t{ nullptr, 0u } );
    for ( usize k = 0u; k < pKeys->keys.nCount && ctx.bOk; ++k ) {
        const cook_source_key_t &key = pKeys->keys.pData[k];
        cook_collision_surface_t surface{};
        surface.objectId = key.sourceId;
        surface.kind = key.kind;
        surface.productKey = CookKeys_ProductKey( pKeys, key, cook_product_kind_t::COLLISION );
        surface.iFirstVertex = static_cast<u32>( pOut->positions.nCount );
        surface.iFirstTriangle = static_cast<u32>( pOut->triangleElement.nCount );

        const cook_collision_surface_t *pOld = FindPrevious( pPrevious, key.sourceId );
        if ( pOld != nullptr && ContentHash_Equals( pOld->productKey, surface.productKey ) ) {
            // Unchanged product: copy the previous bytes verbatim.
            ctx.bOk = Vector_Append( &pOut->positions, span_t<const cook_float3_t>{ pPrevious->positions.pData + pOld->iFirstVertex,
                                                                                    pOld->cVertices } ) &&
                      Vector_Append( &pOut->indices, span_t<const u32>{ pPrevious->indices.pData + pOld->iFirstTriangle * 3u,
                                                                        static_cast<usize>( pOld->cTriangles ) * 3u } ) &&
                      Vector_Append( &pOut->triangleElement,
                                     span_t<const geometry_source_id_t>{ pPrevious->triangleElement.pData + pOld->iFirstTriangle,
                                                                         pOld->cTriangles } );
            surface.cVertices = pOld->cVertices;
            surface.cTriangles = pOld->cTriangles;
            surface.bConvex = pOld->bConvex;
            surface.bClosed = pOld->bClosed;
            surface.dataHash = pOld->dataHash;
            // Replay the object's diagnostics so a reused cook reports
            // exactly what a rebuild would.
            for ( usize d = 0u; d < pPrevious->diagnostics.nCount; ++d ) {
                const cook_diagnostic_t &diag = pPrevious->diagnostics.pData[d];
                if ( diag.objectId.value == key.sourceId.value ) { ctx.Diagnose( diag.kind, diag.objectId, diag.elementId ); }
            }
            ++pOut->cSurfacesReused;
        } else {
            bool bBuilt = false;
            if ( key.kind == cook_source_kind_t::BRUSH ) {
                const brush_solid_t *pBrush = GeometrySnapshot_FindBrush( pSnapshot, key.sourceId );
                bBuilt = pBrush != nullptr && BuildBrush( &ctx, pBrush, pSnapshot->policy, &surface );
                if ( pBrush == nullptr ) { ctx.Diagnose( cook_diagnostic_kind_t::SOURCE_MISSING, key.sourceId, {} ); }
            } else if ( key.kind == cook_source_kind_t::MESH ) {
                const mesh_source_t *pMesh = GeometrySnapshot_FindMesh( pSnapshot, key.sourceId );
                bBuilt = pMesh != nullptr && BuildMesh( &ctx, pMesh, &surface );
                if ( pMesh == nullptr ) { ctx.Diagnose( cook_diagnostic_kind_t::SOURCE_MISSING, key.sourceId, {} ); }
            } else if ( key.kind == cook_source_kind_t::PATCH ) {
                const patch_surface_t *pPatch = FindPatch( pSnapshot, key.sourceId );
                bBuilt = pPatch != nullptr && BuildPatch( &ctx, pPatch, &surface );
                if ( pPatch == nullptr ) { ctx.Diagnose( cook_diagnostic_kind_t::SOURCE_MISSING, key.sourceId, {} ); }
            } else if ( key.kind == cook_source_kind_t::HEIGHTFIELD ) {
                const heightfield_t *pField = FindHeightField( pSnapshot, key.sourceId );
                bBuilt = pField != nullptr && BuildHeightField( &ctx, pField, &surface );
                if ( pField == nullptr ) { ctx.Diagnose( cook_diagnostic_kind_t::SOURCE_MISSING, key.sourceId, {} ); }
            }
            if ( !bBuilt ) {
                // Roll back anything the failed object appended.
                pOut->positions.nCount = surface.iFirstVertex;
                pOut->indices.nCount = static_cast<usize>( surface.iFirstTriangle ) * 3u;
                pOut->triangleElement.nCount = surface.iFirstTriangle;
                continue;
            }
            surface.dataHash = HashSurfaceData( pOut, surface, &w );
            ++pOut->cSurfacesRebuilt;
        }
        combined = ContentHash_Combine( combined, surface.dataHash );
        ctx.bOk = ctx.bOk && Vector_PushBack( &pOut->surfaces, surface );
    }
    CookBytes_Shutdown( &w );
    if ( !ctx.bOk ) {
        ClearOutput( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    pOut->contentHash = combined;
    pOut->revision = pKeys->revision;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
