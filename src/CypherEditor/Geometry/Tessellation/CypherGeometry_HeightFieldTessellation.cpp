//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_HeightFieldTessellation.cpp
//  Purpose: Implements per-tile HeightField tessellation with LOD and
//           edge stitching.
//  Details: Snapping works in global sample coordinates. Tile origins are
//           multiples of tileCells, which is a multiple of every LOD step,
//           so "is this edge vertex on the neighbour's grid" is simply
//           "is its coordinate a multiple of the neighbour's step".
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_HeightFieldTessellation.h"

#include <cmath>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

geometry_status_t MeshStorageStatus( const heightfield_tile_mesh_t *pMesh ) noexcept
{
    if ( pMesh == nullptr || pMesh->positions.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    const allocator_t *pAllocator = pMesh->positions.pAllocator;
    if ( !Vector_IsValid( &pMesh->positions ) ||
         !Vector_IsValid( &pMesh->normals ) ||
         !Vector_IsValid( &pMesh->vertexSample ) ||
         !Vector_IsValid( &pMesh->vertexSnapped ) ||
         !Vector_IsValid( &pMesh->indices ) ||
         !Vector_IsValid( &pMesh->triangleCell ) ||
         pMesh->normals.pAllocator != pAllocator ||
         pMesh->vertexSample.pAllocator != pAllocator ||
         pMesh->vertexSnapped.pAllocator != pAllocator ||
         pMesh->indices.pAllocator != pAllocator ||
         pMesh->triangleCell.pAllocator != pAllocator ||
         pMesh->normals.nCount != pMesh->positions.nCount ||
         pMesh->vertexSample.nCount != pMesh->positions.nCount ||
         pMesh->vertexSnapped.nCount != pMesh->positions.nCount ||
         pMesh->indices.nCount % 3u != 0u ||
         pMesh->triangleCell.nCount != pMesh->indices.nCount / 3u ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

void ClearMesh( heightfield_tile_mesh_t *pMesh ) noexcept
{
    Vector_Clear( &pMesh->positions );
    Vector_Clear( &pMesh->normals );
    Vector_Clear( &pMesh->vertexSample );
    Vector_Clear( &pMesh->vertexSnapped );
    Vector_Clear( &pMesh->indices );
    Vector_Clear( &pMesh->triangleCell );
    pMesh->iTile = CY_INVALID_INDEX;
    pMesh->tileId = GEOMETRY_SOURCE_ID_INVALID;
    pMesh->revision = 0u;
    pMesh->lod = 0u;
    pMesh->cHoleQuadsSkipped = 0u;
}

// Height at an edge vertex, snapped onto a coarser neighbour's edge when
// the vertex is not on that neighbour's grid. `along` is the coordinate
// that varies along the edge; `fixed` the one that does not.
f64 EdgeHeight( const heightfield_t *pField, bool bAlongY, u32 fixed, u32 along, u32 step, bool *pSnapped ) noexcept
{
    const u32 rem = along % step;
    if ( rem == 0u ) {
        *pSnapped = false;
        return bAlongY ? HeightField_Height( pField, fixed, along ) : HeightField_Height( pField, along, fixed );
    }
    const u32 a0 = along - rem, a1 = a0 + step;
    const f64 h0 = bAlongY ? HeightField_Height( pField, fixed, a0 ) : HeightField_Height( pField, a0, fixed );
    const f64 h1 = bAlongY ? HeightField_Height( pField, fixed, a1 ) : HeightField_Height( pField, a1, fixed );
    *pSnapped = true;
    return h0 + ( h1 - h0 ) * ( static_cast<f64>( rem ) / static_cast<f64>( step ) );
}

bool QuadHasHole( const heightfield_t *pField, u32 cx, u32 cy, u32 step ) noexcept
{
    for ( u32 j = 0u; j < step; ++j ) {
        for ( u32 i = 0u; i < step; ++i ) {
            if ( HeightField_IsHole( pField, cx + i, cy + j ) ) { return true; }
        }
    }
    return false;
}

} // namespace

geometry_status_t HeightFieldTileMesh_Init( heightfield_tile_mesh_t *pMesh, const allocator_t *pAllocator ) noexcept
{
    if ( pMesh == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( pMesh->positions.pAllocator != nullptr || pMesh->normals.pAllocator != nullptr ||
         pMesh->vertexSample.pAllocator != nullptr || pMesh->vertexSnapped.pAllocator != nullptr ||
         pMesh->indices.pAllocator != nullptr || pMesh->triangleCell.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !Vector_Init( &pMesh->positions, pAllocator ) || !Vector_Init( &pMesh->normals, pAllocator ) ||
         !Vector_Init( &pMesh->vertexSample, pAllocator ) || !Vector_Init( &pMesh->vertexSnapped, pAllocator ) ||
         !Vector_Init( &pMesh->indices, pAllocator ) || !Vector_Init( &pMesh->triangleCell, pAllocator ) ) {
        HeightFieldTileMesh_Shutdown( pMesh );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    ClearMesh( pMesh );
    return geometry_status_t::OK;
}

void HeightFieldTileMesh_Shutdown( heightfield_tile_mesh_t *pMesh ) noexcept
{
    if ( pMesh == nullptr ) { return; }
    Vector_Shutdown( &pMesh->positions );
    Vector_Shutdown( &pMesh->normals );
    Vector_Shutdown( &pMesh->vertexSample );
    Vector_Shutdown( &pMesh->vertexSnapped );
    Vector_Shutdown( &pMesh->indices );
    Vector_Shutdown( &pMesh->triangleCell );
    pMesh->iTile = CY_INVALID_INDEX;
    pMesh->tileId = GEOMETRY_SOURCE_ID_INVALID;
    pMesh->revision = 0u;
    pMesh->lod = 0u;
    pMesh->cHoleQuadsSkipped = 0u;
}

u32 HeightFieldTessellation_MaxLod( const heightfield_t *pField ) noexcept
{
    if ( HeightField_ValidateStructure( pField ) != geometry_status_t::OK ) { return 0u; }
    u32 lod = 0u;
    while ( ( 1u << ( lod + 1u ) ) <= pField->tileCells ) { ++lod; }
    return lod;
}

geometry_status_t HeightFieldTessellation_TryBuildTile(
    const heightfield_t *pField,
    u32 iTile,
    const heightfield_tile_lods_t &lods,
    heightfield_tile_mesh_t *pOut ) noexcept
{
    const geometry_status_t outputStatus = MeshStorageStatus( pOut );
    if ( outputStatus != geometry_status_t::OK ) { return outputStatus; }
    ClearMesh( pOut );
    const geometry_status_t fieldStatus = HeightField_ValidateStructure( pField );
    if ( fieldStatus != geometry_status_t::OK ) { return fieldStatus; }
    if ( iTile >= pField->tiles.nCount ) { return geometry_status_t::INVALID_HANDLE; }
    const u32 maxLod = HeightFieldTessellation_MaxLod( pField );
    if ( lods.lod > maxLod ) { return geometry_status_t::INVALID_ARGUMENT; }
    for ( u32 e = 0u; e < HEIGHTFIELD_EDGE_COUNT; ++e ) {
        if ( lods.neighbourLod[e] > maxLod ) { return geometry_status_t::INVALID_ARGUMENT; }
    }

    const u32 tc = pField->tileCells;
    const u32 tx = iTile % pField->cTilesX, ty = iTile / pField->cTilesX;
    const u32 x0 = tx * tc, y0 = ty * tc;
    const u32 step = 1u << lods.lod;
    const u32 q = tc / step;
    const u32 cVertsAxis = q + 1u;

    // Effective neighbour step per edge: only coarser neighbours force
    // snapping; the field border and finer neighbours leave the edge as is.
    const bool bHas[HEIGHTFIELD_EDGE_COUNT] = { tx > 0u, tx + 1u < pField->cTilesX, ty > 0u,
                                                ty + 1u < pField->cTilesY };
    u32 edgeStep[HEIGHTFIELD_EDGE_COUNT];
    for ( u32 e = 0u; e < HEIGHTFIELD_EDGE_COUNT; ++e ) {
        const u32 ns = 1u << lods.neighbourLod[e];
        edgeStep[e] = ( bHas[e] && ns > step ) ? ns : step;
    }

    const usize cVerts = static_cast<usize>( cVertsAxis ) * cVertsAxis;
    const usize cQuads = static_cast<usize>( q ) * q;
    if ( !Vector_Reserve( &pOut->positions, cVerts ) || !Vector_Reserve( &pOut->normals, cVerts ) ||
         !Vector_Reserve( &pOut->vertexSample, cVerts ) || !Vector_Reserve( &pOut->vertexSnapped, cVerts ) ||
         !Vector_Reserve( &pOut->indices, cQuads * 6u ) || !Vector_Reserve( &pOut->triangleCell, cQuads * 2u ) ) {
        ClearMesh( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }

    const u32 cSamplesX = pField->cCellsX + 1u;
    bool bOk = true;
    for ( u32 j = 0u; j < cVertsAxis; ++j ) {
        for ( u32 i = 0u; i < cVertsAxis; ++i ) {
            const u32 sx = x0 + i * step, sy = y0 + j * step;
            f64 h = HeightField_Height( pField, sx, sy );
            bool bSnapped = false;
            // A vertex lies on at most one non-corner edge; corners are on
            // every neighbour's grid and never snap.
            if ( i == 0u && edgeStep[HEIGHTFIELD_EDGE_WEST] > step ) {
                h = EdgeHeight( pField, true, sx, sy, edgeStep[HEIGHTFIELD_EDGE_WEST], &bSnapped );
            } else if ( i == q && edgeStep[HEIGHTFIELD_EDGE_EAST] > step ) {
                h = EdgeHeight( pField, true, sx, sy, edgeStep[HEIGHTFIELD_EDGE_EAST], &bSnapped );
            } else if ( j == 0u && edgeStep[HEIGHTFIELD_EDGE_SOUTH] > step ) {
                h = EdgeHeight( pField, false, sy, sx, edgeStep[HEIGHTFIELD_EDGE_SOUTH], &bSnapped );
            } else if ( j == q && edgeStep[HEIGHTFIELD_EDGE_NORTH] > step ) {
                h = EdgeHeight( pField, false, sy, sx, edgeStep[HEIGHTFIELD_EDGE_NORTH], &bSnapped );
            }
            math::vec3d_t p = HeightField_SamplePosition( pField, sx, sy );
            p.z = pField->origin.z + h;
            bOk = bOk && Vector_PushBack( &pOut->positions, p ) &&
                  Vector_PushBack( &pOut->normals, HeightField_SampleNormal( pField, sx, sy ) ) &&
                  Vector_PushBack( &pOut->vertexSample, sy * cSamplesX + sx ) &&
                  Vector_PushBack( &pOut->vertexSnapped, static_cast<u8>( bSnapped ? 1u : 0u ) );
        }
    }

    for ( u32 j = 0u; j < q; ++j ) {
        for ( u32 i = 0u; i < q; ++i ) {
            const u32 cx = x0 + i * step, cy = y0 + j * step;
            if ( QuadHasHole( pField, cx, cy, step ) ) {
                ++pOut->cHoleQuadsSkipped;
                continue;
            }
            const u32 a = j * cVertsAxis + i, b = a + 1u, c = a + cVertsAxis + 1u, d = a + cVertsAxis;
            // Same rule as HeightField_CellUsesMainDiagonal, applied to the
            // quad's corner samples (identical to it at LOD 0).
            const f64 h00 = HeightField_Height( pField, cx, cy );
            const f64 h10 = HeightField_Height( pField, cx + step, cy );
            const f64 h01 = HeightField_Height( pField, cx, cy + step );
            const f64 h11 = HeightField_Height( pField, cx + step, cy + step );
            const bool bMain = std::fabs( h11 - h00 ) <= std::fabs( h01 - h10 );
            const u32 tris[2][3] = { { a, b, bMain ? c : d }, { bMain ? a : b, c, d } };
            const u32 cell = cy * pField->cCellsX + cx;
            for ( const auto &t : tris ) {
                bOk = bOk && Vector_PushBack( &pOut->indices, t[0] ) && Vector_PushBack( &pOut->indices, t[1] ) &&
                      Vector_PushBack( &pOut->indices, t[2] ) && Vector_PushBack( &pOut->triangleCell, cell );
            }
        }
    }
    if ( !bOk ) {
        ClearMesh( pOut );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const heightfield_tile_t &tile = pField->tiles.pData[iTile];
    pOut->iTile = iTile;
    pOut->tileId = tile.sourceId;
    pOut->revision = tile.revision;
    pOut->lod = lods.lod;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
