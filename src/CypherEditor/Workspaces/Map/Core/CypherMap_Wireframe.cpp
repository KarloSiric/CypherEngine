//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Wireframe.cpp
//  Purpose: Implements the map wireframe: edge extraction per geometry
//           kind, entity boxes, and 2D picking.
//  Details: Objects are appended kind by kind and sorted by ID once at the
//           end, so lookups are binary searches and the order never depends
//           on how the geometry document happens to store things.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Wireframe.h"

#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_DocumentBrushAttributes.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier0/CypherCommon_Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cypher::editor::map
{

using namespace cypher::common;
namespace geo = cypher::editor::geometry;

namespace
{

CYPHER_NODISCARD string_view_t SV( const char *pText ) noexcept
{
    return StringView_FromCString( pText );
}

CYPHER_NODISCARD f64 Axis( const math::vec3d_t &point, u32 axis ) noexcept
{
    CY_ASSERT( axis < 3u );
    return axis == 0u ? point.x : ( axis == 1u ? point.y : point.z );
}

// The layer and owner the document recorded for a geometry ID; geometry a
// tool added without a record belongs to the first layer and the world.
CYPHER_NODISCARD map_geometry_record_t RecordFor( const map_document_t &map, u64 id ) noexcept
{
    const map_geometry_record_t *pBegin = map.geometryRecords.pData;
    const map_geometry_record_t *pEnd = pBegin + map.geometryRecords.nCount;
    const map_geometry_record_t *pFound = std::lower_bound( pBegin, pEnd, id,
        []( const map_geometry_record_t &record, u64 key ) noexcept { return record.id < key; } );
    if ( pFound != pEnd && pFound->id == id ) { return *pFound; }
    return map_geometry_record_t{ id, 0u, 0u };
}

CYPHER_NODISCARD u32 LayerIndex( const map_document_t &map, const char *pLayer ) noexcept
{
    for ( usize i = 0u; i < map.layers.nCount; ++i ) {
        if ( std::strcmp( map.layers.pData[i].id, pLayer ) == 0 ) { return static_cast<u32>( i ); }
    }
    return 0u;
}

struct builder_t {
    map_wireframe_t *pWire{ nullptr };
    map_wire_object_t object{};
    bool_t bFailed{ CY_FALSE };
};

void Begin( builder_t &builder, u64 id, map_wire_kind_t kind, const map_geometry_record_t &record ) noexcept
{
    builder.object = map_wire_object_t{};
    builder.object.id = id;
    builder.object.kind = kind;
    builder.object.owner = record.owner;
    builder.object.iLayer = record.iLayer;
    builder.object.iFirstLine = static_cast<u32>( builder.pWire->lines.nCount );
    builder.object.iFirstPoint = static_cast<u32>( builder.pWire->points.nCount );
}

CYPHER_NODISCARD u32 AddPoint( builder_t &builder, math::vec3d_t point, u64 sourceId = 0u ) noexcept
{
    if ( builder.bFailed ) { return 0u; }
    MapBounds_AddPoint( builder.object.bounds, point );
    if ( !Vector_PushBack( &builder.pWire->points, point ) ) { builder.bFailed = CY_TRUE; return 0u; }
    if ( !Vector_PushBack( &builder.pWire->pointSourceIds, sourceId ) ) { builder.bFailed = CY_TRUE; return 0u; }
    return static_cast<u32>( builder.pWire->points.nCount - 1u );
}

void AddLine( builder_t &builder, u32 iA, u32 iB ) noexcept
{
    if ( builder.bFailed ) { return; }
    if ( !Vector_PushBack( &builder.pWire->lines, map_wire_line_t{ iA, iB } ) ) { builder.bFailed = CY_TRUE; }
}

// A polygon over points already added. The normal is Newell's, from the
// winding; bOutwardFrom flips it away from a solid's centre (brush
// boundaries do not promise a winding).
void AddFace( builder_t &builder, const u32 *pIndices, u32 nIndices, u64 material, bool_t bTwoSided, const math::vec3d_t *pOutwardFrom = nullptr, u64 sideId = 0, u64 faceId = 0 ) noexcept
{
    // A failed point append leaves subsequent face indices unpublished. Stop
    // before dereferencing them; failure cleanup happens once in Build.
    if ( builder.bFailed || nIndices < 3u ) { return; }
    map_wire_face_t face{};
    face.id = builder.object.id;
    face.material = material;
    face.sideId = sideId;
    face.faceId = faceId;
    face.bTwoSided = bTwoSided;
    face.iFirstIndex = static_cast<u32>( builder.pWire->faceIndices.nCount );
    face.nIndices = nIndices;
    f64 nx = 0.0, ny = 0.0, nz = 0.0, cx = 0.0, cy = 0.0, cz = 0.0;
    for ( u32 k = 0u; k < nIndices; ++k ) {
        const math::vec3d_t &a = builder.pWire->points.pData[pIndices[k]];
        const math::vec3d_t &b = builder.pWire->points.pData[pIndices[( k + 1u ) % nIndices]];
        nx += ( a.y - b.y ) * ( a.z + b.z );
        ny += ( a.z - b.z ) * ( a.x + b.x );
        nz += ( a.x - b.x ) * ( a.y + b.y );
        cx += a.x; cy += a.y; cz += a.z;
        if ( !Vector_PushBack( &builder.pWire->faceIndices, pIndices[k] ) ) { builder.bFailed = CY_TRUE; return; }
    }
    const f64 length = std::sqrt( nx * nx + ny * ny + nz * nz );
    if ( length < 1e-12 ) {
        builder.pWire->faceIndices.nCount = face.iFirstIndex; // Degenerate: nothing to fill.
        return;
    }
    face.normal = math::Vec3d_Make( nx / length, ny / length, nz / length );
    if ( pOutwardFrom != nullptr ) {
        const f64 dot = ( cx / nIndices - pOutwardFrom->x ) * face.normal.x + ( cy / nIndices - pOutwardFrom->y ) * face.normal.y +
                        ( cz / nIndices - pOutwardFrom->z ) * face.normal.z;
        if ( dot < 0.0 ) {
            face.normal = math::Vec3d_Make( -face.normal.x, -face.normal.y, -face.normal.z );
            std::reverse( builder.pWire->faceIndices.pData + face.iFirstIndex, builder.pWire->faceIndices.pData + face.iFirstIndex + nIndices );
        }
    }
    if ( !Vector_PushBack( &builder.pWire->faces, face ) ) { builder.bFailed = CY_TRUE; }
}

void End( builder_t &builder ) noexcept
{
    if ( builder.bFailed ) { return; }
    builder.object.nLines = static_cast<u32>( builder.pWire->lines.nCount ) - builder.object.iFirstLine;
    builder.object.nPoints = static_cast<u32>( builder.pWire->points.nCount ) - builder.object.iFirstPoint;
    MapBounds_AddBounds( builder.pWire->bounds, builder.object.bounds );
    if ( !Vector_PushBack( &builder.pWire->objects, builder.object ) ) { builder.bFailed = CY_TRUE; }
}

void AddBrushes( builder_t &builder, const map_document_t &map ) noexcept
{
    geo::brush_boundary_t boundary{};
    if ( geo::BrushBoundary_Init( &boundary, builder.pWire->pAllocator ) != geo::geometry_status_t::OK ) {
        builder.bFailed = CY_TRUE;
        return;
    }
    vector_t<u32> indices{};
    if ( !Vector_Init( &indices, builder.pWire->pAllocator ) ) {
        geo::BrushBoundary_Shutdown( &boundary );
        builder.bFailed = CY_TRUE;
        return;
    }
    for ( usize i = 0u; i < map.geometry.brushes.nCount && !builder.bFailed; ++i ) {
        const geo::brush_solid_t *pBrush = map.geometry.brushes.pData[i];
        const u64 id = pBrush->sourceId.value;
        Begin( builder, id, map_wire_kind_t::BRUSH, RecordFor( map, id ) );
        const auto status = geo::BrushBoundary_TryReconstruct( &boundary, pBrush, map.geometryPolicy );
        if ( status == geo::geometry_status_t::ALLOCATION_FAILED ) {
            // An allocation failure does not make authored geometry invalid.
            // Propagate it so edit/undo preparation cannot publish a partial view.
            builder.bFailed = CY_TRUE;
            break;
        }
        if ( status != geo::geometry_status_t::OK ) {
            // Still listed, so the outliner shows it; it just has nothing to draw.
            ++builder.pWire->nBrokenBrushes;
            End( builder );
            continue;
        }
        const u32 iBase = static_cast<u32>( builder.pWire->points.nCount );
        math::vec3d_t centre{};
        for ( usize v = 0u; v < boundary.vertices.nCount; ++v ) {
            ( void )AddPoint( builder, boundary.vertices.pData[v] );
            centre = math::Vec3d_Make( centre.x + boundary.vertices.pData[v].x, centre.y + boundary.vertices.pData[v].y, centre.z + boundary.vertices.pData[v].z );
        }
        if ( boundary.vertices.nCount != 0u ) {
            const f64 inv = 1.0 / static_cast<f64>( boundary.vertices.nCount );
            centre = math::Vec3d_Make( centre.x * inv, centre.y * inv, centre.z * inv );
        }
        for ( usize e = 0u; e < boundary.edges.nCount; ++e ) {
            AddLine( builder, iBase + boundary.edges.pData[e].iVertex0, iBase + boundary.edges.pData[e].iVertex1 );
        }
        // Sides as filled polygons, each with its side's material.
        const auto *pAttributes = geo::GeometryDocument_FindBrushAttributes( &map.geometry, pBrush->sourceId );
        for ( usize f = 0u; f < boundary.faces.nCount && !builder.bFailed; ++f ) {
            const geo::brush_boundary_face_t &face = boundary.faces.pData[f];
            // Reconstruction already bounds the face ring by geometry policy.
            // Preserve the complete ring; a fixed 64-slot array truncated valid
            // imported cylinder/prism caps and produced incomplete filled faces.
            const u32 nCorners = face.cVertices;
            if ( !Vector_Resize( &indices, nCorners ) ) { builder.bFailed = CY_TRUE; break; }
            for ( u32 c = 0u; c < nCorners; ++c ) { indices.pData[c] = iBase + boundary.faceVertexIndices.pData[face.iFirstIndex + c]; }
            u64 material = 0u;
            if ( pAttributes != nullptr && face.iSide < pBrush->sides.nCount ) {
                const usize iAttribute = pBrush->sides.pData[face.iSide].iAttributeIndex;
                if ( iAttribute < pAttributes->records.nCount ) { material = pAttributes->records.pData[iAttribute].material.value; }
            }
            AddFace( builder, indices.pData, nCorners, material, CY_FALSE, &centre, pBrush->sides.pData[face.iSide].sourceId.value );
        }
        End( builder );
    }
    geo::BrushBoundary_Shutdown( &boundary );
}

void AddMeshes( builder_t &builder, const map_document_t &map ) noexcept
{
    for ( usize i = 0u; i < map.geometry.meshes.nCount && !builder.bFailed; ++i ) {
        const geo::mesh_source_t *pMesh = map.geometry.meshes.pData[i];
        geo::mesh_source_description_t desc{};
        auto status = geo::MeshSourceDescription_Init( &desc, builder.pWire->pAllocator, pMesh->sourceId );
        if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSource_TryDescribe( pMesh, &desc ); }
        if ( status != geo::geometry_status_t::OK ) {
            geo::MeshSourceDescription_Shutdown( &desc );
            if ( status == geo::geometry_status_t::ALLOCATION_FAILED ) { builder.bFailed = CY_TRUE; }
            continue;
        }
        const u64 id = pMesh->sourceId.value;
        Begin( builder, id, map_wire_kind_t::MESH, RecordFor( map, id ) );
        const u32 iBase = static_cast<u32>( builder.pWire->points.nCount );
        for ( usize v = 0u; v < desc.vertices.nCount; ++v ) {
            ( void )AddPoint( builder, desc.vertices.pData[v].position, desc.vertices.pData[v].sourceId.value );
        }
        // Only authored polygon-ring edges are pickable. A filled face's fan
        // diagonals are presentation triangulation, never editable topology.
        vector_t<u32> indices{};
        if ( !Vector_Init( &indices, builder.pWire->pAllocator ) ) { builder.bFailed = CY_TRUE; }
        for ( usize f = 0u; f < desc.faces.nCount && !builder.bFailed; ++f ) {
            const geo::mesh_source_face_t &face = desc.faces.pData[f];
            Vector_Clear( &indices );
            if ( !Vector_Reserve( &indices, face.cCorners ) ) { builder.bFailed = CY_TRUE; break; }
            for ( u32 c = 0u; c < face.cCorners && !builder.bFailed; ++c ) {
                const u32 iA = desc.corners.pData[face.iFirstCorner + c].iVertex;
                const u32 iB = desc.corners.pData[face.iFirstCorner + ( c + 1u ) % face.cCorners].iVertex;
                AddLine( builder, iBase + std::min( iA, iB ), iBase + std::max( iA, iB ) );
                if ( !Vector_PushBack( &indices, iBase + iA ) ) { builder.bFailed = CY_TRUE; }
            }
            // Open meshes (a ramp, a sheet) are seen from both sides.
            AddFace( builder, indices.pData, static_cast<u32>( indices.nCount ), face.attributes.material.value, CY_TRUE, nullptr, 0u, face.sourceId.value );
        }
        if ( !builder.bFailed && builder.pWire->lines.nCount > builder.object.iFirstLine ) {
            // Describe orders vertices by source ID. Normalized point pairs
            // therefore also sort by authored endpoint identities. Compact in
            // place: shared half-edges become one selectable edge, no scratch
            // allocation or closed-solid prerequisite is needed.
            auto *pFirst = builder.pWire->lines.pData + builder.object.iFirstLine;
            auto *pEnd = builder.pWire->lines.pData + builder.pWire->lines.nCount;
            std::sort( pFirst, pEnd, []( const map_wire_line_t &a, const map_wire_line_t &b ) noexcept {
                return a.iA == b.iA ? a.iB < b.iB : a.iA < b.iA;
            } );
            const auto *pUniqueEnd = std::unique( pFirst, pEnd, []( const map_wire_line_t &a, const map_wire_line_t &b ) noexcept {
                return a.iA == b.iA && a.iB == b.iB;
            } );
            builder.pWire->lines.nCount = static_cast<usize>( pUniqueEnd - builder.pWire->lines.pData );
        }
        End( builder );
        geo::MeshSourceDescription_Shutdown( &desc );
    }
}

// Control-net rows and columns: what Hammer and Radiant show for patches in
// 2D, and enough to see the patch's reach.
void AddPatches( builder_t &builder, const map_document_t &map ) noexcept
{
    for ( usize i = 0u; i < map.geometry.patches.nCount && !builder.bFailed; ++i ) {
        const geo::patch_surface_t *pPatch = map.geometry.patches.pData[i];
        const u64 id = pPatch->sourceId.value;
        Begin( builder, id, map_wire_kind_t::PATCH, RecordFor( map, id ) );
        const u32 iBase = static_cast<u32>( builder.pWire->points.nCount );
        for ( usize c = 0u; c < pPatch->controls.nCount; ++c ) { ( void )AddPoint( builder, pPatch->controls.pData[c].position ); }
        const u32 nColumns = pPatch->cColumns;
        const u32 nRows = pPatch->cRows;
        if ( static_cast<usize>( nColumns ) * nRows == pPatch->controls.nCount ) {
            for ( u32 r = 0u; r < nRows; ++r ) {
                for ( u32 c = 0u; c < nColumns; ++c ) {
                    const u32 iPoint = iBase + r * nColumns + c;
                    if ( c + 1u < nColumns ) { AddLine( builder, iPoint, iPoint + 1u ); }
                    if ( r + 1u < nRows ) { AddLine( builder, iPoint, iPoint + nColumns ); }
                    if ( c + 1u < nColumns && r + 1u < nRows ) {
                        const u32 quad[4]{ iPoint, iPoint + 1u, iPoint + nColumns + 1u, iPoint + nColumns };
                        AddFace( builder, quad, 4u, pPatch->materialId, CY_TRUE );
                    }
                }
            }
        }
        End( builder );
    }
}

void AddTerrains( builder_t &builder, const map_document_t &map ) noexcept
{
    for ( usize i = 0u; i < map.geometry.heightFields.nCount && !builder.bFailed; ++i ) {
        const geo::heightfield_t *pField = map.geometry.heightFields.pData[i];
        const u64 id = pField->sourceId.value;
        Begin( builder, id, map_wire_kind_t::TERRAIN, RecordFor( map, id ) );
        const u32 nX = geo::HeightField_SamplesX( pField );
        const u32 nY = geo::HeightField_SamplesY( pField );
        if ( nX >= 2u && nY >= 2u ) {
            // Grid lines every stepX/stepY samples, always including the
            // last row and column so the outline is closed.
            const u32 stepX = std::max( 1u, ( nX - 1u + MAP_WIRE_TERRAIN_LINES - 1u ) / MAP_WIRE_TERRAIN_LINES );
            const u32 stepY = std::max( 1u, ( nY - 1u + MAP_WIRE_TERRAIN_LINES - 1u ) / MAP_WIRE_TERRAIN_LINES );
            auto next = []( u32 index, u32 step, u32 count ) noexcept { return index + step < count ? index + step : count - 1u; };
            for ( u32 y = 0u;; y = next( y, stepY, nY ) ) {
                u32 iPrevious = AddPoint( builder, geo::HeightField_SamplePosition( pField, 0u, y ) );
                for ( u32 x = next( 0u, stepX, nX );; x = next( x, stepX, nX ) ) {
                    const u32 iPoint = AddPoint( builder, geo::HeightField_SamplePosition( pField, x, y ) );
                    AddLine( builder, iPrevious, iPoint );
                    iPrevious = iPoint;
                    if ( x == nX - 1u ) { break; }
                }
                if ( y == nY - 1u ) { break; }
            }
            for ( u32 x = 0u;; x = next( x, stepX, nX ) ) {
                u32 iPrevious = AddPoint( builder, geo::HeightField_SamplePosition( pField, x, 0u ) );
                for ( u32 y = next( 0u, stepY, nY );; y = next( y, stepY, nY ) ) {
                    const u32 iPoint = AddPoint( builder, geo::HeightField_SamplePosition( pField, x, y ) );
                    AddLine( builder, iPrevious, iPoint );
                    iPrevious = iPoint;
                    if ( y == nY - 1u ) { break; }
                }
                if ( x == nX - 1u ) { break; }
            }
            // Cells on the same thinned lattice, filled in the shaded modes.
            vector_t<u32> columns{}, rows{};
            if ( !Vector_Init( &columns, builder.pWire->pAllocator ) || !Vector_Init( &rows, builder.pWire->pAllocator ) ) {
                builder.bFailed = CY_TRUE;
                return;
            }
            for ( u32 x = 0u; !builder.bFailed; x = next( x, stepX, nX ) ) {
                if ( !Vector_PushBack( &columns, x ) ) { builder.bFailed = CY_TRUE; }
                if ( x == nX - 1u ) { break; }
            }
            for ( u32 y = 0u; !builder.bFailed; y = next( y, stepY, nY ) ) {
                if ( !Vector_PushBack( &rows, y ) ) { builder.bFailed = CY_TRUE; }
                if ( y == nY - 1u ) { break; }
            }
            if ( builder.bFailed ) { return; }
            const u32 iLattice = static_cast<u32>( builder.pWire->points.nCount );
            for ( usize r = 0u; r < rows.nCount && !builder.bFailed; ++r ) {
                for ( usize c = 0u; c < columns.nCount && !builder.bFailed; ++c ) {
                    ( void )AddPoint( builder, geo::HeightField_SamplePosition( pField, columns.pData[c], rows.pData[r] ) );
                }
            }
            const u32 nColumns = static_cast<u32>( columns.nCount );
            for ( u32 r = 0u; r + 1u < rows.nCount && !builder.bFailed; ++r ) {
                for ( u32 c = 0u; c + 1u < nColumns && !builder.bFailed; ++c ) {
                    const u32 i0 = iLattice + r * nColumns + c;
                    const u32 quad[4]{ i0, i0 + 1u, i0 + nColumns + 1u, i0 + nColumns };
                    AddFace( builder, quad, 4u, 0u, CY_TRUE );
                }
            }
        }
        End( builder );
    }
}

void CopyDisplayText( char ( &destination )[MAP_WIRE_TEXT_CAPACITY], const key_value_t *pValue ) noexcept
{
    string_view_t text{};
    if ( !KeyValue_GetString( pValue, &text ) ) { return; }
    const usize cch = std::min( text.cchLength, MAP_WIRE_TEXT_CAPACITY - 1u );
    std::memcpy( destination, text.pData, cch );
    destination[cch] = '\0';
}

// IDs are u64; a hand-typed "12" parses as signed and is accepted when
// positive, matching the document's own reader.
CYPHER_NODISCARD u64 ReadId( const key_value_t *pValue ) noexcept
{
    u64 id = 0u;
    i64 nSigned = 0;
    if ( KeyValue_GetU64( pValue, &id ) ) { return id; }
    return KeyValue_GetI64( pValue, &nSigned ) && nSigned > 0 ? static_cast<u64>( nSigned ) : 0u;
}

CYPHER_NODISCARD bool_t ReadOrigin( const key_value_t *pValue, math::vec3d_t &out ) noexcept
{
    if ( KeyValue_Type( pValue ) != key_value_type_t::ARRAY || KeyValue_ChildCount( pValue ) != 3u ) { return CY_FALSE; }
    f64 v[3]{};
    for ( usize i = 0u; i < 3u; ++i ) {
        const key_value_t *pChild = KeyValue_ChildAt( pValue, i );
        i64 n = 0;
        if ( KeyValue_GetF64( pChild, &v[i] ) ) { continue; }
        if ( !KeyValue_GetI64( pChild, &n ) ) { return CY_FALSE; }
        v[i] = static_cast<f64>( n );
    }
    if ( !std::isfinite( v[0] ) || !std::isfinite( v[1] ) || !std::isfinite( v[2] ) ) { return CY_FALSE; }
    out = math::Vec3d_Make( v[0], v[1], v[2] );
    return CY_TRUE;
}

// Entities are read from the chunk trees: the document keeps them there
// rather than in a parallel structure, so this reflects exactly what saves.
void AddEntities( builder_t &builder, const map_document_t &map ) noexcept
{
    for ( usize iChunk = 0u; iChunk < map.chunks.nCount && !builder.bFailed; ++iChunk ) {
        const map_chunk_t *pChunk = map.chunks.pData[iChunk];
        if ( pChunk->bDamaged ) { continue; }
        const key_value_t *pRoot = KeyValue_Root( static_cast<const key_value_document_t *>( pChunk->store.pDocument ) );
        const key_value_t *pEntities = KeyValue_Find( pRoot, SV( "entities" ) );
        const u32 iLayer = LayerIndex( map, pChunk->layer );
        for ( usize i = 0u; i < KeyValue_ChildCount( pEntities ) && !builder.bFailed; ++i ) {
            const key_value_t *pRecord = KeyValue_ChildAt( pEntities, i );
            map_wire_entity_t entity{};
            const u64 id = KeyValue_Type( pRecord ) == key_value_type_t::OBJECT ? ReadId( KeyValue_Find( pRecord, SV( "id" ) ) ) : 0u;
            if ( id == 0u ) { continue; }
            entity.id = id;
            entity.iLayer = iLayer;
            CopyDisplayText( entity.className, KeyValue_Find( pRecord, SV( "class" ) ) );
            CopyDisplayText( entity.name, KeyValue_Find( pRecord, SV( "name" ) ) );
            const bool_t bHasOrigin = ReadOrigin( KeyValue_Find( pRecord, SV( "origin" ) ), entity.origin );
            entity.bHasOrigin = bHasOrigin;
            if ( !Vector_PushBack( &builder.pWire->entities, entity ) ) {
                builder.bFailed = CY_TRUE;
                return;
            }
            // Only positioned entities are pickable in a view; the rest
            // (worldspawn-style settings holders) live in the outliner.
            if ( !bHasOrigin ) { continue; }
            Begin( builder, id, map_wire_kind_t::ENTITY, map_geometry_record_t{ id, iLayer, 0u } );
            const math::vec3d_t half = math::Vec3d_Make( MAP_WIRE_POINT_ENTITY_HALF, MAP_WIRE_POINT_ENTITY_HALF, MAP_WIRE_POINT_ENTITY_HALF );
            MapBounds_AddPoint( builder.object.bounds, math::Vec3d_Make( entity.origin.x - half.x, entity.origin.y - half.y, entity.origin.z - half.z ) );
            MapBounds_AddPoint( builder.object.bounds, math::Vec3d_Make( entity.origin.x + half.x, entity.origin.y + half.y, entity.origin.z + half.z ) );
            End( builder );
        }
    }
}

struct connection_entity_t {
    const key_value_t *pOutputs{ nullptr }; // Borrowed only during this build.
    string_view_t name{};                  // Full authored name, never the display truncation.
    u64 id{ 0u };
    u32 iLayer{ 0u };
    math::vec3d_t origin{};
    bool_t bHasOrigin{ CY_FALSE };
};

void AddConnections( builder_t &builder, const map_document_t &map ) noexcept
{
    vector_t<connection_entity_t> entities{};
    if ( !Vector_Init( &entities, builder.pWire->pAllocator ) ) {
        builder.bFailed = CY_TRUE;
        return;
    }
    for ( usize c = 0u; c < map.chunks.nCount; ++c ) {
        const map_chunk_t &chunk = *map.chunks.pData[c];
        if ( chunk.bDamaged ) { continue; }
        const key_value_t *pRoot = KeyValue_Root( static_cast<const key_value_document_t *>( chunk.store.pDocument ) );
        const key_value_t *pRecords = KeyValue_Find( pRoot, SV( "entities" ) );
        for ( usize i = 0u; i < KeyValue_ChildCount( pRecords ); ++i ) {
            const key_value_t *pRecord = KeyValue_ChildAt( pRecords, i );
            if ( KeyValue_Type( pRecord ) != key_value_type_t::OBJECT ) { continue; }
            connection_entity_t entity{};
            entity.id = ReadId( KeyValue_Find( pRecord, SV( "id" ) ) );
            if ( entity.id == 0u ) { continue; }
            ( void )KeyValue_GetString( KeyValue_Find( pRecord, SV( "name" ) ), &entity.name );
            entity.iLayer = LayerIndex( map, chunk.layer );
            entity.bHasOrigin = ReadOrigin( KeyValue_Find( pRecord, SV( "origin" ) ), entity.origin );
            entity.pOutputs = KeyValue_Find( pRecord, SV( "outputs" ) );
            if ( !Vector_PushBack( &entities, entity ) ) { builder.bFailed = CY_TRUE; return; }
        }
    }
    if ( entities.nCount == 0u ) { return; }
    // A name index makes exact resolution O(log E + matches), rather than
    // rescanning all entities for every authored output in a large map.
    std::sort( entities.pData, entities.pData + entities.nCount,
        []( const connection_entity_t &a, const connection_entity_t &b ) noexcept {
            const i32 comparison = StringView_Compare( a.name, b.name );
            return comparison == 0 ? a.id < b.id : comparison < 0;
        } );
    const connection_entity_t *pBegin = entities.pData;
    const connection_entity_t *pEnd = pBegin + entities.nCount;
    for ( usize s = 0u; s < entities.nCount && !builder.bFailed; ++s ) {
        const connection_entity_t &source = entities.pData[s];
        if ( KeyValue_Type( source.pOutputs ) != key_value_type_t::ARRAY ) { continue; }
        for ( usize o = 0u; o < KeyValue_ChildCount( source.pOutputs ) && !builder.bFailed; ++o ) {
            const key_value_t *pOutput = KeyValue_ChildAt( source.pOutputs, o );
            map_wire_connection_t connection{};
            connection.sourceId = source.id;
            connection.sourceOrigin = source.origin;
            connection.bHasSourceOrigin = source.bHasOrigin;
            connection.iSourceLayer = source.iLayer;
            connection.iOutput = o;
            CopyDisplayText( connection.output, KeyValue_Find( pOutput, SV( "output" ) ) );
            CopyDisplayText( connection.input, KeyValue_Find( pOutput, SV( "input" ) ) );
            CopyDisplayText( connection.target, KeyValue_Find( pOutput, SV( "target" ) ) );
            string_view_t target{};
            ( void )KeyValue_GetString( KeyValue_Find( pOutput, SV( "target" ) ), &target );
            const auto append = [&]( const connection_entity_t *pTarget ) noexcept {
                map_wire_connection_t resolved = connection;
                if ( pTarget != nullptr ) {
                    resolved.status = map_wire_connection_status_t::RESOLVED;
                    resolved.targetId = pTarget->id;
                    resolved.targetOrigin = pTarget->origin;
                    resolved.bHasTargetOrigin = pTarget->bHasOrigin;
                    resolved.iTargetLayer = pTarget->iLayer;
                }
                if ( !Vector_PushBack( &builder.pWire->connections, resolved ) ) { builder.bFailed = CY_TRUE; }
            };
            if ( connection.output[0] == '\0' || connection.input[0] == '\0' || target.cchLength == 0u ) {
                append( nullptr );
                continue;
            }
            if ( StringView_Equals( target, SV( "!self" ) ) ) { append( &source ); continue; }
            if ( target.pData[0] == '!' ) {
                if ( StringView_Equals( target, SV( "!activator" ) ) || StringView_Equals( target, SV( "!caller" ) ) ||
                     StringView_Equals( target, SV( "!player" ) ) ) { connection.status = map_wire_connection_status_t::RUNTIME_TARGET; }
                append( nullptr );
                continue;
            }
            const bool bPrefix = target.pData[target.cchLength - 1u] == '*';
            string_view_t match = target;
            if ( bPrefix ) { --match.cchLength; }
            if ( StringView_Contains( match, SV( "*" ) ) ) { append( nullptr ); continue; }
            const connection_entity_t *pMatch = std::lower_bound( pBegin, pEnd, match,
                []( const connection_entity_t &entity, string_view_t name ) noexcept { return StringView_Compare( entity.name, name ) < 0; } );
            bool bMatched = false;
            for ( ; pMatch != pEnd && !builder.bFailed; ++pMatch ) {
                const bool bNameMatches = bPrefix ? StringView_StartsWith( pMatch->name, match ) : StringView_Equals( pMatch->name, match );
                if ( !bNameMatches ) { break; }
                if ( pMatch->name.cchLength == 0u ) { continue; }
                bMatched = true;
                append( pMatch );
            }
            if ( !bMatched ) {
                connection.status = map_wire_connection_status_t::MISSING_TARGET;
                append( nullptr );
            }
        }
    }
    auto &connections = builder.pWire->connections;
    if ( connections.nCount > 1u ) {
        std::sort( connections.pData, connections.pData + connections.nCount,
            []( const map_wire_connection_t &a, const map_wire_connection_t &b ) noexcept {
                if ( a.sourceId != b.sourceId ) { return a.sourceId < b.sourceId; }
                return a.iOutput == b.iOutput ? a.targetId < b.targetId : a.iOutput < b.iOutput;
            } );
    }
}

CYPHER_NODISCARD f64 DistanceToSegment( f64 u, f64 v, f64 aU, f64 aV, f64 bU, f64 bV ) noexcept
{
    const f64 dU = bU - aU;
    const f64 dV = bV - aV;
    const f64 lengthSquared = dU * dU + dV * dV;
    f64 t = lengthSquared > 0.0 ? ( ( u - aU ) * dU + ( v - aV ) * dV ) / lengthSquared : 0.0;
    t = std::clamp( t, 0.0, 1.0 );
    const f64 closestU = aU + t * dU - u;
    const f64 closestV = aV + t * dV - v;
    return std::sqrt( closestU * closestU + closestV * closestV );
}

} // namespace

map_wireframe_t::~map_wireframe_t() noexcept
{
    MapWireframe_Shutdown( this );
}

bool_t MapWireframe_Init( map_wireframe_t *pWire, const allocator_t *pAllocator ) noexcept
{
    if ( pWire == nullptr || pAllocator == nullptr ) { return CY_FALSE; }
    pWire->pAllocator = pAllocator;
    return Vector_Init( &pWire->points, pAllocator ) && Vector_Init( &pWire->pointSourceIds, pAllocator ) && Vector_Init( &pWire->lines, pAllocator ) &&
           Vector_Init( &pWire->objects, pAllocator ) && Vector_Init( &pWire->entities, pAllocator ) &&
           Vector_Init( &pWire->connections, pAllocator ) && Vector_Init( &pWire->faces, pAllocator ) &&
           Vector_Init( &pWire->faceIndices, pAllocator );
}

void MapWireframe_Shutdown( map_wireframe_t *pWire ) noexcept
{
    if ( pWire == nullptr ) { return; }
    Vector_Shutdown( &pWire->points );
    Vector_Shutdown( &pWire->pointSourceIds );
    Vector_Shutdown( &pWire->lines );
    Vector_Shutdown( &pWire->objects );
    Vector_Shutdown( &pWire->entities );
    Vector_Shutdown( &pWire->connections );
    Vector_Shutdown( &pWire->faces );
    Vector_Shutdown( &pWire->faceIndices );
    pWire->bounds = map_bounds_t{};
    pWire->nBrokenBrushes = 0u;
}

map_status_t MapWireframe_Build( map_wireframe_t *pWire, const map_document_t &map ) noexcept
{
    if ( pWire == nullptr || pWire->pAllocator == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    Vector_Clear( &pWire->points );
    Vector_Clear( &pWire->pointSourceIds );
    Vector_Clear( &pWire->lines );
    Vector_Clear( &pWire->objects );
    Vector_Clear( &pWire->entities );
    Vector_Clear( &pWire->connections );
    Vector_Clear( &pWire->faces );
    Vector_Clear( &pWire->faceIndices );
    pWire->bounds = map_bounds_t{};
    pWire->nBrokenBrushes = 0u;

    builder_t builder{};
    builder.pWire = pWire;
    AddBrushes( builder, map );
    AddMeshes( builder, map );
    AddPatches( builder, map );
    AddTerrains( builder, map );
    AddEntities( builder, map );
    if ( !builder.bFailed ) { AddConnections( builder, map ); }
    if ( builder.bFailed ) {
        CY_LOG_WRITE( Error, Editor, "Map wireframe build ran out of memory" );
        Vector_Clear( &pWire->points );
        Vector_Clear( &pWire->pointSourceIds );
        Vector_Clear( &pWire->lines );
        Vector_Clear( &pWire->objects );
        Vector_Clear( &pWire->entities );
        Vector_Clear( &pWire->connections );
        Vector_Clear( &pWire->faces );
        Vector_Clear( &pWire->faceIndices );
        pWire->bounds = map_bounds_t{};
        pWire->nBrokenBrushes = 0u;
        return map_status_t::OUT_OF_MEMORY;
    }

    // Owned-geometry counts, now that every object is known.
    std::sort( pWire->entities.pData, pWire->entities.pData + pWire->entities.nCount,
        []( const map_wire_entity_t &a, const map_wire_entity_t &b ) noexcept { return a.id < b.id; } );
    std::sort( pWire->objects.pData, pWire->objects.pData + pWire->objects.nCount,
        []( const map_wire_object_t &a, const map_wire_object_t &b ) noexcept { return a.id < b.id; } );
    map_wire_entity_t *pEntitiesEnd = pWire->entities.pData + pWire->entities.nCount;
    for ( usize i = 0u; i < pWire->objects.nCount; ++i ) {
        const u64 owner = pWire->objects.pData[i].owner;
        if ( owner == 0u ) { continue; }
        map_wire_entity_t *pOwner = std::lower_bound( pWire->entities.pData, pEntitiesEnd, owner,
            []( const map_wire_entity_t &entity, u64 key ) noexcept { return entity.id < key; } );
        if ( pOwner != pEntitiesEnd && pOwner->id == owner ) { ++pOwner->nOwned; }
    }
    if ( pWire->nBrokenBrushes != 0u ) { CY_LOG_WRITE( Warning, Editor, "Some brushes have no valid shape and are not drawn" ); }
    return map_status_t::OK;
}

const map_wire_object_t *MapWireframe_FindObject( const map_wireframe_t &wire, u64 id ) noexcept
{
    const map_wire_object_t *pBegin = wire.objects.pData;
    const map_wire_object_t *pEnd = pBegin + wire.objects.nCount;
    const map_wire_object_t *pFound = std::lower_bound( pBegin, pEnd, id,
        []( const map_wire_object_t &object, u64 key ) noexcept { return object.id < key; } );
    return pFound != pEnd && pFound->id == id ? pFound : nullptr;
}

const map_wire_entity_t *MapWireframe_FindEntity( const map_wireframe_t &wire, u64 id ) noexcept
{
    const map_wire_entity_t *pBegin = wire.entities.pData;
    const map_wire_entity_t *pEnd = pBegin + wire.entities.nCount;
    const map_wire_entity_t *pFound = std::lower_bound( pBegin, pEnd, id,
        []( const map_wire_entity_t &entity, u64 key ) noexcept { return entity.id < key; } );
    return pFound != pEnd && pFound->id == id ? pFound : nullptr;
}

u64 MapWireframe_Pick2D( const map_wireframe_t &wire, u32 axisU, u32 axisV, f64 u, f64 v, f64 tolerance ) noexcept
{
    CY_ASSERT( axisU < 3u && axisV < 3u && axisU != axisV );
    if ( axisU >= 3u || axisV >= 3u || axisU == axisV || !( tolerance >= 0.0 ) ) { return 0u; }
    u64 best = 0u;
    f64 bestArea = 0.0;
    for ( usize i = 0u; i < wire.objects.nCount; ++i ) {
        const map_wire_object_t &object = wire.objects.pData[i];
        if ( !object.bounds.bHas ) { continue; }
        const f64 minU = Axis( object.bounds.box.minimum, axisU );
        const f64 maxU = Axis( object.bounds.box.maximum, axisU );
        const f64 minV = Axis( object.bounds.box.minimum, axisV );
        const f64 maxV = Axis( object.bounds.box.maximum, axisV );
        // Cheap reject before walking lines.
        if ( u < minU - tolerance || u > maxU + tolerance || v < minV - tolerance || v > maxV + tolerance ) { continue; }
        bool_t bHit = object.kind == map_wire_kind_t::ENTITY;
        for ( u32 l = 0u; l < object.nLines && !bHit; ++l ) {
            const map_wire_line_t &line = wire.lines.pData[object.iFirstLine + l];
            const math::vec3d_t &a = wire.points.pData[line.iA];
            const math::vec3d_t &b = wire.points.pData[line.iB];
            bHit = DistanceToSegment( u, v, Axis( a, axisU ), Axis( a, axisV ), Axis( b, axisU ), Axis( b, axisV ) ) <= tolerance;
        }
        if ( !bHit ) { continue; }
        const f64 area = ( maxU - minU ) * ( maxV - minV );
        if ( best == 0u || area < bestArea ) {
            best = object.id;
            bestArea = area;
        }
    }
    return best;
}

} // namespace cypher::editor::map
