//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_HeightField.cpp
//  Purpose: Implements HeightField storage, bounded edits with dirty
//           tracking, triangle-consistent height queries, DDA ray casts,
//           and validation.
//  Details: Ray casting follows Amanatides & Woo (1987): the ray is
//           clipped to the field rectangle, then walks cells in the order
//           it crosses them. A cell's triangles project exactly onto the
//           cell, so any hit inside a cell lies within that cell's ray
//           interval and the first cell with a hit holds the nearest hit.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_HeightField.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace cypher::editor::geometry
{

using namespace cypher::common;

namespace
{

// Barycentric slack for ray/triangle tests. Rays that pass exactly along a
// shared triangle edge would otherwise fall through the crack between two
// rounded edge tests; a tiny overlap makes the shared edge belong to both.
constexpr f64 kBarycentricSlack = 1e-12;

bool IsPowerOfTwo( u32 v ) noexcept { return v != 0u && ( v & ( v - 1u ) ) == 0u; }

u32 SamplesXOf( const heightfield_t *pField ) noexcept { return pField->cCellsX + 1u; }

usize SampleIndex( const heightfield_t *pField, u32 ix, u32 iy ) noexcept
{
    return static_cast<usize>( iy ) * SamplesXOf( pField ) + ix;
}

usize CellIndex( const heightfield_t *pField, u32 cx, u32 cy ) noexcept
{
    return static_cast<usize>( cy ) * pField->cCellsX + cx;
}

bool CoordinateOk( f64 v ) noexcept
{
    return std::isfinite( v ) && std::fabs( v ) <= kHeightFieldCoordinateMax;
}

// A stored height is acceptable when the resulting world z is in range.
bool HeightOk( const heightfield_t *pField, f64 h ) noexcept
{
    return std::isfinite( h ) && CoordinateOk( pField->origin.z + h );
}

f64 H( const heightfield_t *pField, u32 ix, u32 iy ) noexcept
{
    return pField->heights.pData[SampleIndex( pField, ix, iy )];
}

// Marks every tile whose cells touch the given sample rectangle. A sample
// (ix, iy) is a corner of cells ix-1..ix by iy-1..iy, so a rectangle of
// samples grows by one cell towards the origin.
void TouchSamples( heightfield_t *pField, heightfield_rect_t rect ) noexcept
{
    if ( rect.cx == 0u || rect.cy == 0u ) { return; }
    const u32 c0x = rect.x0 > 0u ? rect.x0 - 1u : 0u;
    const u32 c0y = rect.y0 > 0u ? rect.y0 - 1u : 0u;
    const u32 c1x = std::min( rect.x0 + rect.cx - 1u, pField->cCellsX - 1u );
    const u32 c1y = std::min( rect.y0 + rect.cy - 1u, pField->cCellsY - 1u );
    ++pField->revision;
    for ( u32 ty = c0y / pField->tileCells; ty <= c1y / pField->tileCells; ++ty ) {
        for ( u32 tx = c0x / pField->tileCells; tx <= c1x / pField->tileCells; ++tx ) {
            heightfield_tile_t &tile = pField->tiles.pData[static_cast<usize>( ty ) * pField->cTilesX + tx];
            tile.revision = pField->revision;
            tile.bDirty = true;
        }
    }
}

bool RectInSamples( const heightfield_t *pField, heightfield_rect_t rect ) noexcept
{
    const u64 x1 = static_cast<u64>( rect.x0 ) + rect.cx;
    const u64 y1 = static_cast<u64>( rect.y0 ) + rect.cy;
    return x1 <= SamplesXOf( pField ) && y1 <= pField->cCellsY + 1u;
}

// Moller-Trumbore, double precision, with a symmetric barycentric slack.
bool RayTriangle( math::vec3d_t o, math::vec3d_t d, math::vec3d_t a, math::vec3d_t b, math::vec3d_t c, f64 *pT ) noexcept
{
    const math::vec3d_t e1 = math::Vec3d_Subtract( b, a );
    const math::vec3d_t e2 = math::Vec3d_Subtract( c, a );
    const math::vec3d_t p = math::Vec3d_Cross( d, e2 );
    const f64 det = math::Vec3d_Dot( e1, p );
    if ( det == 0.0 || !std::isfinite( det ) ) { return false; }
    const f64 inv = 1.0 / det;
    const math::vec3d_t s = math::Vec3d_Subtract( o, a );
    const f64 u = math::Vec3d_Dot( s, p ) * inv;
    if ( u < -kBarycentricSlack || u > 1.0 + kBarycentricSlack ) { return false; }
    const math::vec3d_t q = math::Vec3d_Cross( s, e1 );
    const f64 v = math::Vec3d_Dot( d, q ) * inv;
    if ( v < -kBarycentricSlack || u + v > 1.0 + kBarycentricSlack ) { return false; }
    *pT = math::Vec3d_Dot( e2, q ) * inv;
    return std::isfinite( *pT );
}

// Tests both triangles of a cell; returns the nearer hit in [0, maxT].
bool RayCell(
    const heightfield_t *pField,
    u32 cx,
    u32 cy,
    math::vec3d_t o,
    math::vec3d_t d,
    f64 maxT,
    heightfield_ray_hit_t *pHit ) noexcept
{
    if ( HeightField_IsHole( pField, cx, cy ) ) { return false; }
    const math::vec3d_t p00 = HeightField_SamplePosition( pField, cx, cy );
    const math::vec3d_t p10 = HeightField_SamplePosition( pField, cx + 1u, cy );
    const math::vec3d_t p01 = HeightField_SamplePosition( pField, cx, cy + 1u );
    const math::vec3d_t p11 = HeightField_SamplePosition( pField, cx + 1u, cy + 1u );
    math::vec3d_t tris[2][3];
    if ( HeightField_CellUsesMainDiagonal( pField, cx, cy ) ) {
        tris[0][0] = p00; tris[0][1] = p10; tris[0][2] = p11;
        tris[1][0] = p00; tris[1][1] = p11; tris[1][2] = p01;
    } else {
        tris[0][0] = p00; tris[0][1] = p10; tris[0][2] = p01;
        tris[1][0] = p10; tris[1][1] = p11; tris[1][2] = p01;
    }
    bool bHit = false;
    for ( const auto &tri : tris ) {
        f64 t = 0.0;
        if ( RayTriangle( o, d, tri[0], tri[1], tri[2], &t ) && t >= 0.0 && t <= maxT && ( !bHit || t < pHit->t ) ) {
            bHit = true;
            pHit->t = t;
            math::vec3d_t n{};
            const math::vec3d_t cross = math::Vec3d_Cross( math::Vec3d_Subtract( tri[1], tri[0] ),
                                                           math::Vec3d_Subtract( tri[2], tri[0] ) );
            pHit->normal = math::Vec3d_TryNormalize( cross, 0.0, &n, nullptr ) ? n : math::vec3d_t{ 0.0, 0.0, 1.0 };
        }
    }
    if ( bHit ) {
        pHit->position = math::Vec3d_Add( o, math::Vec3d_Scale( d, pHit->t ) );
        pHit->cx = cx;
        pHit->cy = cy;
        pHit->iTile = HeightField_TileOfCell( pField, cx, cy );
    }
    return bHit;
}

f64 Smoothstep01( f64 d ) noexcept
{
    if ( d <= 0.0 ) { return 1.0; }
    if ( d >= 1.0 ) { return 0.0; }
    return 1.0 - d * d * ( 3.0 - 2.0 * d );
}

} // namespace

bool HeightField_IsInitialized( const heightfield_t *pField ) noexcept
{
    return pField != nullptr && pField->heights.pAllocator != nullptr;
}

geometry_status_t HeightField_ValidateStructure(
    const heightfield_t *pField ) noexcept
{
    if ( !HeightField_IsInitialized( pField ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !Vector_IsValid( &pField->heights ) ||
         !Vector_IsValid( &pField->holes ) ||
         !Vector_IsValid( &pField->tiles ) ||
         pField->holes.pAllocator != pField->heights.pAllocator ||
         pField->tiles.pAllocator != pField->heights.pAllocator ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const u32 tileCells = pField->tileCells;
    if ( pField->cCellsX == 0u || pField->cCellsY == 0u ||
         pField->cCellsX > kHeightFieldCellsPerAxisMax ||
         pField->cCellsY > kHeightFieldCellsPerAxisMax ||
         !IsPowerOfTwo( tileCells ) ||
         tileCells > kHeightFieldTileCellsMax ||
         pField->cCellsX % tileCells != 0u ||
         pField->cCellsY % tileCells != 0u ||
         pField->cTilesX != pField->cCellsX / tileCells ||
         pField->cTilesY != pField->cCellsY / tileCells ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    const u64 cSamplesX = static_cast<u64>( pField->cCellsX ) + 1u;
    const u64 cSamplesY = static_cast<u64>( pField->cCellsY ) + 1u;
    const u64 cSamples = cSamplesX * cSamplesY;
    const u64 cCells = static_cast<u64>( pField->cCellsX ) *
                       pField->cCellsY;
    const u64 cTiles = static_cast<u64>( pField->cTilesX ) *
                       pField->cTilesY;
    if ( cSamples > kHeightFieldSamplesMax ||
         cSamples > static_cast<u64>( CY_USIZE_MAX ) ||
         cCells > static_cast<u64>( CY_USIZE_MAX ) ||
         cTiles > static_cast<u64>( CY_USIZE_MAX ) ||
         pField->heights.nCount != static_cast<usize>( cSamples ) ||
         pField->holes.nCount != static_cast<usize>( cCells ) ||
         pField->tiles.nCount != static_cast<usize>( cTiles ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    if ( !GeometrySourceId_IsValid( pField->sourceId ) ||
         !( pField->cellSize > 0.0 ) ||
         !std::isfinite( pField->cellSize ) ||
         !CoordinateOk( pField->origin.x ) ||
         !CoordinateOk( pField->origin.y ) ||
         !CoordinateOk( pField->origin.z ) ||
         !CoordinateOk(
             pField->origin.x + pField->cellSize *
                 static_cast<f64>( pField->cCellsX ) ) ||
         !CoordinateOk(
             pField->origin.y + pField->cellSize *
                 static_cast<f64>( pField->cCellsY ) ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }
    return geometry_status_t::OK;
}

u32 HeightField_SamplesX( const heightfield_t *pField ) noexcept
{
    return HeightField_ValidateStructure( pField ) == geometry_status_t::OK
        ? pField->cCellsX + 1u
        : 0u;
}

u32 HeightField_SamplesY( const heightfield_t *pField ) noexcept
{
    return HeightField_ValidateStructure( pField ) == geometry_status_t::OK
        ? pField->cCellsY + 1u
        : 0u;
}

geometry_status_t HeightField_TryInit(
    heightfield_t *pField,
    const allocator_t *pAllocator,
    math::vec3d_t origin,
    f64 cellSize,
    u32 cCellsX,
    u32 cCellsY,
    u32 tileCells,
    geometry_source_id_t fieldId,
    geometry_source_id_allocator_t *pIdAllocator ) noexcept
{
    if ( pField == nullptr || !Allocator_IsValid( pAllocator ) || !GeometrySourceId_IsValid( fieldId ) ||
         pIdAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( HeightField_IsInitialized( pField ) ) { return geometry_status_t::ALREADY_INITIALIZED; }
    if ( cCellsX == 0u || cCellsY == 0u || !IsPowerOfTwo( tileCells ) || tileCells > kHeightFieldTileCellsMax ||
         cCellsX % tileCells != 0u || cCellsY % tileCells != 0u ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( cCellsX > kHeightFieldCellsPerAxisMax || cCellsY > kHeightFieldCellsPerAxisMax ||
         static_cast<u64>( cCellsX + 1u ) * ( cCellsY + 1u ) > kHeightFieldSamplesMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !( cellSize > 0.0 ) || !std::isfinite( cellSize ) || !CoordinateOk( origin.x ) ||
         !CoordinateOk( origin.y ) || !CoordinateOk( origin.z ) ||
         !CoordinateOk( origin.x + cellSize * static_cast<f64>( cCellsX ) ) ||
         !CoordinateOk( origin.y + cellSize * static_cast<f64>( cCellsY ) ) ) {
        return geometry_status_t::NUMERIC_FAILURE;
    }

    const u32 cTilesX = cCellsX / tileCells, cTilesY = cCellsY / tileCells;
    const usize cSamples = static_cast<usize>( cCellsX + 1u ) * ( cCellsY + 1u );
    const usize cCells = static_cast<usize>( cCellsX ) * cCellsY;
    const usize cTiles = static_cast<usize>( cTilesX ) * cTilesY;
    geometry_source_id_allocator_t ids = *pIdAllocator;
    const geometry_status_t advanceStatus =
        GeometrySourceIdAllocator_AdvancePast( &ids, fieldId );
    if ( advanceStatus != geometry_status_t::OK ) { return advanceStatus; }
    if ( !Vector_Init( &pField->heights, pAllocator ) || !Vector_Init( &pField->holes, pAllocator ) ||
         !Vector_Init( &pField->tiles, pAllocator ) || !Vector_Resize( &pField->heights, cSamples ) ||
         !Vector_Resize( &pField->holes, cCells ) || !Vector_Resize( &pField->tiles, cTiles ) ) {
        HeightField_Shutdown( pField );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < cTiles; ++i ) {
        const geometry_source_id_result_t id = GeometrySourceIdAllocator_Allocate( &ids );
        if ( id.status != geometry_status_t::OK ) {
            HeightField_Shutdown( pField );
            return id.status;
        }
        pField->tiles.pData[i] = heightfield_tile_t{ id.id, 0u, true };
    }
    for ( usize i = 0u; i < cSamples; ++i ) { pField->heights.pData[i] = 0.0; }
    for ( usize i = 0u; i < cCells; ++i ) { pField->holes.pData[i] = 0u; }
    pField->origin = origin;
    pField->cellSize = cellSize;
    pField->cCellsX = cCellsX;
    pField->cCellsY = cCellsY;
    pField->tileCells = tileCells;
    pField->cTilesX = cTilesX;
    pField->cTilesY = cTilesY;
    pField->revision = 0u;
    pField->sourceId = fieldId;
    *pIdAllocator = ids;
    return geometry_status_t::OK;
}

void HeightField_Shutdown( heightfield_t *pField ) noexcept
{
    if ( pField == nullptr ) { return; }
    Vector_Shutdown( &pField->heights );
    Vector_Shutdown( &pField->holes );
    Vector_Shutdown( &pField->tiles );
    pField->cCellsX = pField->cCellsY = 0u;
    pField->tileCells = pField->cTilesX = pField->cTilesY = 0u;
    pField->revision = 0u;
    pField->sourceId = GEOMETRY_SOURCE_ID_INVALID;
}

geometry_status_t HeightField_TryClone( const heightfield_t *pSource, const allocator_t *pAllocator, heightfield_t *pOut ) noexcept
{
    if ( pOut == nullptr || !Allocator_IsValid( pAllocator ) ) { return geometry_status_t::INVALID_ARGUMENT; }
    if ( HeightField_IsInitialized( pOut ) ) { return geometry_status_t::ALREADY_INITIALIZED; }
    const geometry_status_t structure = HeightField_ValidateStructure( pSource );
    if ( structure != geometry_status_t::OK ) {
        return structure == geometry_status_t::NOT_INITIALIZED ? geometry_status_t::INVALID_ARGUMENT : structure;
    }
    heightfield_t copy{};
    if ( !Vector_Init( &copy.heights, pAllocator ) || !Vector_Init( &copy.holes, pAllocator ) || !Vector_Init( &copy.tiles, pAllocator ) ||
         !Vector_Resize( &copy.heights, pSource->heights.nCount ) || !Vector_Resize( &copy.holes, pSource->holes.nCount ) ||
         !Vector_Resize( &copy.tiles, pSource->tiles.nCount ) ) {
        HeightField_Shutdown( &copy );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( usize i = 0u; i < copy.heights.nCount; ++i ) { copy.heights.pData[i] = pSource->heights.pData[i]; }
    for ( usize i = 0u; i < copy.holes.nCount; ++i ) { copy.holes.pData[i] = pSource->holes.pData[i]; }
    for ( usize i = 0u; i < copy.tiles.nCount; ++i ) { copy.tiles.pData[i] = pSource->tiles.pData[i]; }
    copy.origin = pSource->origin;
    copy.cellSize = pSource->cellSize;
    copy.cCellsX = pSource->cCellsX;
    copy.cCellsY = pSource->cCellsY;
    copy.tileCells = pSource->tileCells;
    copy.cTilesX = pSource->cTilesX;
    copy.cTilesY = pSource->cTilesY;
    copy.revision = pSource->revision;
    copy.sourceId = pSource->sourceId;
    Vector_Move( &pOut->heights, &copy.heights );
    Vector_Move( &pOut->holes, &copy.holes );
    Vector_Move( &pOut->tiles, &copy.tiles );
    pOut->origin = copy.origin;
    pOut->cellSize = copy.cellSize;
    pOut->cCellsX = copy.cCellsX;
    pOut->cCellsY = copy.cCellsY;
    pOut->tileCells = copy.tileCells;
    pOut->cTilesX = copy.cTilesX;
    pOut->cTilesY = copy.cTilesY;
    pOut->revision = copy.revision;
    pOut->sourceId = copy.sourceId;
    return geometry_status_t::OK;
}

f64 HeightField_Height( const heightfield_t *pField, u32 ix, u32 iy ) noexcept
{
    if ( HeightField_ValidateStructure( pField ) != geometry_status_t::OK ||
         ix > pField->cCellsX || iy > pField->cCellsY ) {
        return 0.0;
    }
    return H( pField, ix, iy );
}

math::vec3d_t HeightField_SamplePosition( const heightfield_t *pField, u32 ix, u32 iy ) noexcept
{
    if ( HeightField_ValidateStructure( pField ) != geometry_status_t::OK ||
         ix > pField->cCellsX || iy > pField->cCellsY ) {
        return math::vec3d_t{};
    }
    return math::vec3d_t{ pField->origin.x + pField->cellSize * static_cast<f64>( ix ),
                          pField->origin.y + pField->cellSize * static_cast<f64>( iy ),
                          pField->origin.z + H( pField, ix, iy ) };
}

math::vec3d_t HeightField_SampleNormal( const heightfield_t *pField, u32 ix, u32 iy ) noexcept
{
    if ( HeightField_ValidateStructure( pField ) != geometry_status_t::OK ||
         ix > pField->cCellsX || iy > pField->cCellsY ) {
        return math::vec3d_t{};
    }
    const u32 x0 = ix > 0u ? ix - 1u : ix, x1 = ix < pField->cCellsX ? ix + 1u : ix;
    const u32 y0 = iy > 0u ? iy - 1u : iy, y1 = iy < pField->cCellsY ? iy + 1u : iy;
    const f64 dzdx = ( H( pField, x1, iy ) - H( pField, x0, iy ) ) / ( pField->cellSize * static_cast<f64>( x1 - x0 ) );
    const f64 dzdy = ( H( pField, ix, y1 ) - H( pField, ix, y0 ) ) / ( pField->cellSize * static_cast<f64>( y1 - y0 ) );
    math::vec3d_t n{};
    if ( !math::Vec3d_TryNormalize( math::vec3d_t{ -dzdx, -dzdy, 1.0 }, 0.0, &n, nullptr ) ) {
        return math::vec3d_t{ 0.0, 0.0, 1.0 };
    }
    return n;
}

bool HeightField_IsHole( const heightfield_t *pField, u32 cx, u32 cy ) noexcept
{
    if ( HeightField_ValidateStructure( pField ) != geometry_status_t::OK ||
         cx >= pField->cCellsX || cy >= pField->cCellsY ) {
        return false;
    }
    return pField->holes.pData[CellIndex( pField, cx, cy )] != 0u;
}

u32 HeightField_TileOfCell( const heightfield_t *pField, u32 cx, u32 cy ) noexcept
{
    if ( HeightField_ValidateStructure( pField ) != geometry_status_t::OK ||
         cx >= pField->cCellsX || cy >= pField->cCellsY ) {
        return CY_INVALID_INDEX;
    }
    return ( cy / pField->tileCells ) * pField->cTilesX + cx / pField->tileCells;
}

bool HeightField_CellUsesMainDiagonal( const heightfield_t *pField, u32 cx, u32 cy ) noexcept
{
    if ( HeightField_ValidateStructure( pField ) != geometry_status_t::OK ||
         cx >= pField->cCellsX || cy >= pField->cCellsY ) {
        return true;
    }
    const f64 dMain = std::fabs( H( pField, cx + 1u, cy + 1u ) - H( pField, cx, cy ) );
    const f64 dOther = std::fabs( H( pField, cx, cy + 1u ) - H( pField, cx + 1u, cy ) );
    return dMain <= dOther;
}

bool HeightField_TryHeightAt( const heightfield_t *pField, f64 x, f64 y, f64 *pWorldZ ) noexcept
{
    if ( pWorldZ != nullptr ) { *pWorldZ = 0.0; }
    if ( pWorldZ == nullptr ||
         HeightField_ValidateStructure( pField ) != geometry_status_t::OK ||
         !std::isfinite( x ) || !std::isfinite( y ) ) {
        return false;
    }
    const f64 fx = ( x - pField->origin.x ) / pField->cellSize;
    const f64 fy = ( y - pField->origin.y ) / pField->cellSize;
    if ( fx < 0.0 || fy < 0.0 || fx > static_cast<f64>( pField->cCellsX ) ||
         fy > static_cast<f64>( pField->cCellsY ) ) {
        return false;
    }
    // The far border belongs to the last cell.
    const u32 cx = std::min( static_cast<u32>( fx ), pField->cCellsX - 1u );
    const u32 cy = std::min( static_cast<u32>( fy ), pField->cCellsY - 1u );
    if ( HeightField_IsHole( pField, cx, cy ) ) { return false; }
    const f64 s = fx - static_cast<f64>( cx ), t = fy - static_cast<f64>( cy );
    const f64 h00 = H( pField, cx, cy ), h10 = H( pField, cx + 1u, cy );
    const f64 h01 = H( pField, cx, cy + 1u ), h11 = H( pField, cx + 1u, cy + 1u );
    f64 h = 0.0;
    if ( HeightField_CellUsesMainDiagonal( pField, cx, cy ) ) {
        h = s >= t ? h00 + s * ( h10 - h00 ) + t * ( h11 - h10 ) : h00 + t * ( h01 - h00 ) + s * ( h11 - h01 );
    } else {
        h = s + t <= 1.0 ? h00 + s * ( h10 - h00 ) + t * ( h01 - h00 )
                         : h11 + ( 1.0 - s ) * ( h01 - h11 ) + ( 1.0 - t ) * ( h10 - h11 );
    }
    *pWorldZ = pField->origin.z + h;
    return true;
}

geometry_status_t HeightField_TryReadHeights(
    const heightfield_t *pField,
    heightfield_rect_t rect,
    span_t<f64> out ) noexcept
{
    const geometry_status_t structure = HeightField_ValidateStructure( pField );
    if ( structure != geometry_status_t::OK ) { return structure; }
    if ( !RectInSamples( pField, rect ) ) { return geometry_status_t::INVALID_HANDLE; }
    if ( out.nCount != static_cast<usize>( rect.cx ) * rect.cy || ( out.nCount > 0u && out.pData == nullptr ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    for ( u32 j = 0u; j < rect.cy; ++j ) {
        for ( u32 i = 0u; i < rect.cx; ++i ) {
            out.pData[static_cast<usize>( j ) * rect.cx + i] = H( pField, rect.x0 + i, rect.y0 + j );
        }
    }
    return geometry_status_t::OK;
}

geometry_status_t HeightField_TryWriteHeights(
    heightfield_t *pField,
    heightfield_rect_t rect,
    span_t<const f64> values ) noexcept
{
    const geometry_status_t structure = HeightField_ValidateStructure( pField );
    if ( structure != geometry_status_t::OK ) { return structure; }
    if ( !RectInSamples( pField, rect ) ) { return geometry_status_t::INVALID_HANDLE; }
    if ( values.nCount != static_cast<usize>( rect.cx ) * rect.cy || ( values.nCount > 0u && values.pData == nullptr ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    for ( usize i = 0u; i < values.nCount; ++i ) {
        if ( !HeightOk( pField, values.pData[i] ) ) { return geometry_status_t::NUMERIC_FAILURE; }
    }
    if ( values.nCount > 0u && pField->revision == CY_U64_MAX ) {
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }
    for ( u32 j = 0u; j < rect.cy; ++j ) {
        for ( u32 i = 0u; i < rect.cx; ++i ) {
            pField->heights.pData[SampleIndex( pField, rect.x0 + i, rect.y0 + j )] =
                values.pData[static_cast<usize>( j ) * rect.cx + i];
        }
    }
    TouchSamples( pField, rect );
    return geometry_status_t::OK;
}

geometry_status_t HeightField_TrySetHole( heightfield_t *pField, u32 cx, u32 cy, bool bHole ) noexcept
{
    const geometry_status_t structure = HeightField_ValidateStructure( pField );
    if ( structure != geometry_status_t::OK ) { return structure; }
    if ( cx >= pField->cCellsX || cy >= pField->cCellsY ) { return geometry_status_t::INVALID_HANDLE; }
    u8 &cell = pField->holes.pData[CellIndex( pField, cx, cy )];
    const u8 value = bHole ? 1u : 0u;
    // A no-op does not dirty anything: re-cooking unchanged tiles would make
    // the dirty set a poor signal of what actually changed.
    if ( cell == value ) { return geometry_status_t::OK; }
    if ( pField->revision == CY_U64_MAX ) {
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }
    cell = value;
    ++pField->revision;
    heightfield_tile_t &tile = pField->tiles.pData[HeightField_TileOfCell( pField, cx, cy )];
    tile.revision = pField->revision;
    tile.bDirty = true;
    return geometry_status_t::OK;
}

geometry_status_t HeightField_TryApplyBrush(
    heightfield_t *pField,
    const heightfield_brush_t &brush,
    heightfield_rect_t *pTouchedOut ) noexcept
{
    if ( pTouchedOut ) { *pTouchedOut = heightfield_rect_t{}; }
    const geometry_status_t structure = HeightField_ValidateStructure( pField );
    if ( structure != geometry_status_t::OK ) { return structure; }
    if ( brush.op != heightfield_brush_op_t::RAISE && brush.op != heightfield_brush_op_t::FLATTEN &&
         brush.op != heightfield_brush_op_t::SMOOTH ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !std::isfinite( brush.centerX ) || !std::isfinite( brush.centerY ) || !( brush.radius > 0.0 ) ||
         !std::isfinite( brush.radius ) || !std::isfinite( brush.amount ) || !std::isfinite( brush.targetHeight ) ||
         !( brush.strength >= 0.0 && brush.strength <= 1.0 ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Sample rectangle covering the brush disc, clipped to the field.
    const f64 cs = pField->cellSize;
    const f64 fx0 = std::ceil( ( brush.centerX - brush.radius - pField->origin.x ) / cs );
    const f64 fx1 = std::floor( ( brush.centerX + brush.radius - pField->origin.x ) / cs );
    const f64 fy0 = std::ceil( ( brush.centerY - brush.radius - pField->origin.y ) / cs );
    const f64 fy1 = std::floor( ( brush.centerY + brush.radius - pField->origin.y ) / cs );
    const f64 maxX = static_cast<f64>( pField->cCellsX ), maxY = static_cast<f64>( pField->cCellsY );
    if ( fx1 < 0.0 || fy1 < 0.0 || fx0 > maxX || fy0 > maxY || fx0 > fx1 || fy0 > fy1 ) {
        return geometry_status_t::OK; // brush entirely outside: nothing to do
    }
    heightfield_rect_t rect{};
    rect.x0 = static_cast<u32>( std::max( fx0, 0.0 ) );
    rect.y0 = static_cast<u32>( std::max( fy0, 0.0 ) );
    rect.cx = static_cast<u32>( std::min( fx1, maxX ) ) - rect.x0 + 1u;
    rect.cy = static_cast<u32>( std::min( fy1, maxY ) ) - rect.y0 + 1u;

    vector_t<f64> next{};
    if ( !Vector_Init( &next, pField->heights.pAllocator ) ||
         !Vector_Resize( &next, static_cast<usize>( rect.cx ) * rect.cy ) ) {
        Vector_Shutdown( &next );
        return geometry_status_t::ALLOCATION_FAILED;
    }
    for ( u32 j = 0u; j < rect.cy; ++j ) {
        for ( u32 i = 0u; i < rect.cx; ++i ) {
            const u32 ix = rect.x0 + i, iy = rect.y0 + j;
            const math::vec3d_t p = HeightField_SamplePosition( pField, ix, iy );
            const f64 dx = p.x - brush.centerX, dy = p.y - brush.centerY;
            const f64 w = Smoothstep01( std::sqrt( dx * dx + dy * dy ) / brush.radius );
            const f64 h = H( pField, ix, iy );
            f64 value = h;
            switch ( brush.op ) {
            case heightfield_brush_op_t::RAISE:
                value = h + brush.amount * w;
                break;
            case heightfield_brush_op_t::FLATTEN:
                value = h + ( brush.targetHeight - h ) * brush.strength * w;
                break;
            case heightfield_brush_op_t::SMOOTH: {
                // Reads stored (pre-edit) heights only, so the result is
                // independent of traversal order.
                f64 sum = 0.0;
                u32 cN = 0u;
                if ( ix > 0u ) { sum += H( pField, ix - 1u, iy ); ++cN; }
                if ( ix < pField->cCellsX ) { sum += H( pField, ix + 1u, iy ); ++cN; }
                if ( iy > 0u ) { sum += H( pField, ix, iy - 1u ); ++cN; }
                if ( iy < pField->cCellsY ) { sum += H( pField, ix, iy + 1u ); ++cN; }
                value = h + ( sum / static_cast<f64>( cN ) - h ) * brush.strength * w;
                break;
            }
            }
            if ( !HeightOk( pField, value ) ) {
                Vector_Shutdown( &next );
                return geometry_status_t::NUMERIC_FAILURE;
            }
            next.pData[static_cast<usize>( j ) * rect.cx + i] = value;
        }
    }
    const geometry_status_t st =
        HeightField_TryWriteHeights( pField, rect, span_t<const f64>{ next.pData, next.nCount } );
    Vector_Shutdown( &next );
    if ( st == geometry_status_t::OK && pTouchedOut ) { *pTouchedOut = rect; }
    return st;
}

geometry_status_t HeightField_TryCollectDirtyTiles( const heightfield_t *pField, vector_t<u32> *pOut ) noexcept
{
    const geometry_status_t structure = HeightField_ValidateStructure( pField );
    if ( structure != geometry_status_t::OK ) { return structure; }
    if ( pOut == nullptr || !Vector_IsValid( pOut ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    usize cDirty = 0u;
    for ( usize i = 0u; i < pField->tiles.nCount; ++i ) {
        cDirty += pField->tiles.pData[i].bDirty ? 1u : 0u;
    }
    if ( cDirty > CY_USIZE_MAX - pOut->nCount ) {
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }
    if ( !Vector_Reserve( pOut, pOut->nCount + cDirty ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    const usize oldCount = pOut->nCount;
    for ( usize i = 0u; i < pField->tiles.nCount; ++i ) {
        if ( pField->tiles.pData[i].bDirty && !Vector_PushBack( pOut, static_cast<u32>( i ) ) ) {
            (void)Vector_Resize( pOut, oldCount );
            return geometry_status_t::ALLOCATION_FAILED;
        }
    }
    return geometry_status_t::OK;
}

bool HeightField_ClearDirty( heightfield_t *pField, u32 iTile, u64 processedRevision ) noexcept
{
    if ( HeightField_ValidateStructure( pField ) != geometry_status_t::OK ||
         iTile >= pField->tiles.nCount ) {
        return false;
    }
    heightfield_tile_t &tile = pField->tiles.pData[iTile];
    if ( tile.revision != processedRevision ) { return false; }
    tile.bDirty = false;
    return true;
}

bool HeightField_TryRaycast(
    const heightfield_t *pField,
    math::vec3d_t origin,
    math::vec3d_t direction,
    f64 maxT,
    heightfield_ray_hit_t *pHitOut ) noexcept
{
    if ( pHitOut != nullptr ) { *pHitOut = heightfield_ray_hit_t{}; }
    if ( pHitOut == nullptr ||
         HeightField_ValidateStructure( pField ) != geometry_status_t::OK ||
         !math::Vec3d_IsFinite( origin ) ||
         !math::Vec3d_IsFinite( direction ) || !( maxT >= 0.0 ) || math::Vec3d_LengthSquared( direction ) == 0.0 ) {
        return false;
    }
    const f64 cs = pField->cellSize;
    const f64 minX = pField->origin.x, minY = pField->origin.y;
    const f64 maxX = minX + cs * static_cast<f64>( pField->cCellsX );
    const f64 maxY = minY + cs * static_cast<f64>( pField->cCellsY );

    // Clip the ray to the field rectangle (slab test on x and y).
    f64 t0 = 0.0, t1 = maxT;
    const f64 o[2] = { origin.x, origin.y }, d[2] = { direction.x, direction.y };
    const f64 lo[2] = { minX, minY }, hi[2] = { maxX, maxY };
    for ( int a = 0; a < 2; ++a ) {
        if ( d[a] == 0.0 ) {
            if ( o[a] < lo[a] || o[a] > hi[a] ) { return false; }
            continue;
        }
        f64 ta = ( lo[a] - o[a] ) / d[a], tb = ( hi[a] - o[a] ) / d[a];
        if ( ta > tb ) { std::swap( ta, tb ); }
        t0 = std::max( t0, ta );
        t1 = std::min( t1, tb );
        if ( t0 > t1 ) { return false; }
    }

    // Starting cell from the entry point, clamped against rounding.
    const math::vec3d_t entry = math::Vec3d_Add( origin, math::Vec3d_Scale( direction, t0 ) );
    auto cellOf = []( f64 v, f64 base, f64 size, u32 count ) noexcept -> u32 {
        const f64 f = std::floor( ( v - base ) / size );
        if ( !( f >= 0.0 ) ) { return 0u; }
        return f >= static_cast<f64>( count ) ? count - 1u : static_cast<u32>( f );
    };
    u32 cx = cellOf( entry.x, minX, cs, pField->cCellsX );
    u32 cy = cellOf( entry.y, minY, cs, pField->cCellsY );

    const int stepX = direction.x > 0.0 ? 1 : ( direction.x < 0.0 ? -1 : 0 );
    const int stepY = direction.y > 0.0 ? 1 : ( direction.y < 0.0 ? -1 : 0 );
    const f64 inf = std::numeric_limits<f64>::infinity();
    auto nextBoundary = [&]( u32 c, int step, f64 base, f64 org, f64 dir ) noexcept -> f64 {
        if ( step == 0 ) { return inf; }
        const f64 edge = base + cs * static_cast<f64>( step > 0 ? c + 1u : c );
        return ( edge - org ) / dir;
    };
    f64 tMaxX = nextBoundary( cx, stepX, minX, origin.x, direction.x );
    f64 tMaxY = nextBoundary( cy, stepY, minY, origin.y, direction.y );
    const f64 tDeltaX = stepX != 0 ? cs / std::fabs( direction.x ) : inf;
    const f64 tDeltaY = stepY != 0 ? cs / std::fabs( direction.y ) : inf;

    // Every step moves to a new cell, so the walk is bounded by the number
    // of cells a straight line can cross in this grid.
    const u64 cStepsMax = static_cast<u64>( pField->cCellsX ) + pField->cCellsY + 2u;
    for ( u64 step = 0u; step < cStepsMax; ++step ) {
        if ( RayCell( pField, cx, cy, origin, direction, maxT, pHitOut ) ) { return true; }
        if ( tMaxX < tMaxY ) {
            if ( tMaxX > t1 ) { break; }
            if ( ( stepX < 0 && cx == 0u ) || ( stepX > 0 && cx + 1u >= pField->cCellsX ) ) { break; }
            cx = static_cast<u32>( static_cast<i64>( cx ) + stepX );
            tMaxX += tDeltaX;
        } else {
            if ( tMaxY > t1 || stepY == 0 ) { break; }
            if ( ( stepY < 0 && cy == 0u ) || ( stepY > 0 && cy + 1u >= pField->cCellsY ) ) { break; }
            cy = static_cast<u32>( static_cast<i64>( cy ) + stepY );
            tMaxY += tDeltaY;
        }
    }
    return false;
}

heightfield_validation_result_t HeightField_Validate(
    const heightfield_t *pField,
    const allocator_t *pScratchAllocator ) noexcept
{
    heightfield_validation_result_t r{};
    if ( !HeightField_IsInitialized( pField ) ) {
        r.fault = heightfield_fault_t::NOT_INITIALIZED;
        return r;
    }
    if ( !Vector_IsValid( &pField->heights ) ||
         !Vector_IsValid( &pField->holes ) ||
         !Vector_IsValid( &pField->tiles ) ||
         pField->holes.pAllocator != pField->heights.pAllocator ||
         pField->tiles.pAllocator != pField->heights.pAllocator ) {
        r.fault = heightfield_fault_t::INVALID_DIMENSIONS;
        return r;
    }
    const u32 tc = pField->tileCells;
    if ( pField->cCellsX == 0u || pField->cCellsY == 0u || !IsPowerOfTwo( tc ) || tc > kHeightFieldTileCellsMax ||
         pField->cCellsX % tc != 0u || pField->cCellsY % tc != 0u || pField->cTilesX != pField->cCellsX / tc ||
         pField->cTilesY != pField->cCellsY / tc ||
         pField->heights.nCount != static_cast<usize>( pField->cCellsX + 1u ) * ( pField->cCellsY + 1u ) ||
         pField->holes.nCount != static_cast<usize>( pField->cCellsX ) * pField->cCellsY ||
         pField->tiles.nCount != static_cast<usize>( pField->cTilesX ) * pField->cTilesY ||
         !( pField->cellSize > 0.0 ) || !std::isfinite( pField->cellSize ) ) {
        r.fault = heightfield_fault_t::INVALID_DIMENSIONS;
        return r;
    }
    if ( !GeometrySourceId_IsValid( pField->sourceId ) ) {
        r.fault = heightfield_fault_t::INVALID_SOURCE_ID;
        return r;
    }
    for ( usize i = 0u; i < pField->tiles.nCount; ++i ) {
        if ( !GeometrySourceId_IsValid( pField->tiles.pData[i].sourceId ) ) {
            r.fault = heightfield_fault_t::INVALID_SOURCE_ID;
            r.index = static_cast<u32>( i );
            return r;
        }
    }
    vector_t<u64> ids{};
    if ( !Allocator_IsValid( pScratchAllocator ) || !Vector_Init( &ids, pScratchAllocator ) ||
         pField->tiles.nCount == CY_USIZE_MAX ||
         !Vector_Resize( &ids, pField->tiles.nCount + 1u ) ) {
        Vector_Shutdown( &ids );
        r.fault = heightfield_fault_t::VALIDATION_INCOMPLETE;
        return r;
    }
    ids.pData[0] = pField->sourceId.value;
    for ( usize i = 0u; i < pField->tiles.nCount; ++i ) {
        ids.pData[i + 1u] = pField->tiles.pData[i].sourceId.value;
    }
    std::sort( ids.pData, ids.pData + ids.nCount );
    for ( usize i = 1u; i < ids.nCount; ++i ) {
        if ( ids.pData[i] == ids.pData[i - 1u] ) {
            // Report the first tile (in grid order) carrying the repeated ID.
            const u64 dup = ids.pData[i];
            Vector_Shutdown( &ids );
            r.fault = heightfield_fault_t::DUPLICATE_SOURCE_ID;
            u32 seen = pField->sourceId.value == dup ? 1u : 0u;
            for ( usize k = 0u; k < pField->tiles.nCount; ++k ) {
                if ( pField->tiles.pData[k].sourceId.value == dup && ++seen == 2u ) {
                    r.index = static_cast<u32>( k );
                    break;
                }
            }
            return r;
        }
    }
    Vector_Shutdown( &ids );

    if ( !CoordinateOk( pField->origin.x ) || !CoordinateOk( pField->origin.y ) || !CoordinateOk( pField->origin.z ) ) {
        r.fault = heightfield_fault_t::NON_FINITE;
        return r;
    }
    for ( usize i = 0u; i < pField->heights.nCount; ++i ) {
        const f64 h = pField->heights.pData[i];
        if ( !std::isfinite( h ) ) {
            r.fault = heightfield_fault_t::NON_FINITE;
            r.index = static_cast<u32>( i );
            return r;
        }
        if ( !HeightOk( pField, h ) ) {
            r.fault = heightfield_fault_t::COORDINATE_RANGE;
            r.index = static_cast<u32>( i );
            return r;
        }
    }
    for ( usize i = 0u; i < pField->holes.nCount; ++i ) {
        if ( pField->holes.pData[i] > 1u ) {
            r.fault = heightfield_fault_t::INVALID_HOLE_VALUE;
            r.index = static_cast<u32>( i );
            return r;
        }
    }
    return r;
}

} // namespace cypher::editor::geometry
