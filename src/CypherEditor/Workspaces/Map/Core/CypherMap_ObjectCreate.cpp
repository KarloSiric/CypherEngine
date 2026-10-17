//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_ObjectCreate.cpp
//  Purpose: Implements object creation for the interactive tools and the
//           terrain and mesh paint strokes.
//  Details: New geometry is built complete (identities, materials, UVs)
//           before it is inserted, so a failure inserts nothing.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_ObjectCreate.h"
#include "CypherMap_ImportObj.h"
#include "CypherMap_Wireframe.h"
#include "CypherGeometry_Displacement.h"
#include "CypherGeometry_Sweep.h"
#include "CypherGeometry_BrushSweep.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_Planar_Offset.h"
#include "CypherGeometry_PlanarExtrude.h"
#include "CypherGeometry_CurveNetwork.h"
#include <unordered_map>
#include "CypherMap_EntityEdit.h"
#include "CypherMap_MeshEdit.h"

#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_MeshPaint.h"
#include "CypherGeometry_PatchPrimitives.h"
#include "CypherGeometry_UvAlignment.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <cmath>
#include <vector>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;
namespace
{
using vec3 = math::vec3d_t;
constexpr auto OK = geo::geometry_status_t::OK;
constexpr f64 kCoordinateMax = 1048576.0;

vec3 Add( vec3 a, vec3 b ) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
vec3 Sub( vec3 a, vec3 b ) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
vec3 Mul( vec3 a, f64 s ) noexcept { return { a.x * s, a.y * s, a.z * s }; }
f64 Dot( vec3 a, vec3 b ) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
vec3 Cross( vec3 a, vec3 b ) noexcept { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
f64 Length( vec3 a ) noexcept { return std::sqrt( Dot( a, a ) ); }
f64 Axis( vec3 p, u32 axis ) noexcept { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }
vec3 AxisVector( u32 axis ) noexcept { return { axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0 }; }
bool InRange( vec3 p ) noexcept
{
    return std::isfinite( p.x ) && std::isfinite( p.y ) && std::isfinite( p.z ) &&
        std::fabs( p.x ) <= kCoordinateMax && std::fabs( p.y ) <= kCoordinateMax && std::fabs( p.z ) <= kCoordinateMax;
}

map_status_t Status( geo::geometry_status_t status ) noexcept
{
    if ( status == OK ) { return map_status_t::OK; }
    if ( status == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
    if ( status == geo::geometry_status_t::LIMIT_EXCEEDED || status == geo::geometry_status_t::INSUFFICIENT_CAPACITY ) { return map_status_t::LIMIT_EXCEEDED; }
    return map_status_t::GEOMETRY_FAILED;
}

// New objects go to the editor's current layer (the first when none is set).
string_view_t FirstLayer( const map_document_t *map ) noexcept
{
    return MapDocument_CreationLayer( map );
}

map_status_t Writable( const map_document_t *map ) noexcept
{
    if ( map == nullptr || map->pAllocator == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ||
         map->geometry.revision == CY_U64_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    return map_status_t::OK;
}

// IDs above the map's own high-water mark as well as the geometry one.
geo::geometry_source_id_allocator_t Ids( const map_document_t *map ) noexcept
{
    return { { std::max( map->nextId, map->geometry.sourceIds.allocator.next.value ) } };
}

// Placement and bookkeeping once a new root is in the geometry document.
map_status_t Adopt( map_document_t *map, u64 id, const geo::geometry_source_id_allocator_t &ids ) noexcept
{
    const auto result = MapDocument_SetGeometryLayer( map, id, FirstLayer( map ) );
    if ( result != map_status_t::OK ) { return result; }
    map->nextId = std::max( map->nextId, ids.next.value );
    ++map->geometry.revision;
    return map_status_t::OK;
}

geo::geometry_source_id_t Next( geo::geometry_source_id_allocator_t &ids ) noexcept
{
    const auto fresh = geo::GeometrySourceIdAllocator_Allocate( &ids );
    return fresh.status == OK ? fresh.id : geo::geometry_source_id_t{};
}

// World-aligned projection of one face, as Align to World makes it.
math::vec2d_t WorldUv( vec3 normal, vec3 p, f64 worldUnitsPerUv ) noexcept
{
    const f64 x = std::fabs( normal.x ), y = std::fabs( normal.y ), z = std::fabs( normal.z );
    const vec3 axis = z >= x && z >= y ? vec3{ 0, 0, normal.z < 0 ? -1.0 : 1.0 } : x >= y ? vec3{ normal.x < 0 ? -1.0 : 1.0, 0, 0 } : vec3{ 0, normal.y < 0 ? -1.0 : 1.0, 0 };
    geo::geometry_brush_side_attributes_t basis{};
    if ( geo::UvAlign_Reset( &basis, axis ) != OK ) { return {}; }
    return { Dot( p, basis.uvProjection.uAxis ) / worldUnitsPerUv, Dot( p, basis.uvProjection.vAxis ) / worldUnitsPerUv };
}
} // namespace

map_status_t MapEntity_SetString( map_document_t *map, u64 id, string_view_t key, string_view_t value ) noexcept
{
    if ( map == nullptr || id == 0 || !StringView_IsValid( key ) || key.cchLength == 0 || !StringView_IsValid( value ) ) { return map_status_t::INVALID_ARGUMENT; }
    key_value_document_desc_t desc{}; desc.pAllocator = map->pAllocator;
    key_value_document_t *document = KeyValue_CreateDocument( desc );
    key_value_t *node = nullptr;
    if ( document != nullptr && KeyValue_SetRootType( document, key_value_type_t::OBJECT ) ) {
        node = KeyValue_ObjectInsert( document, KeyValue_Root( document ), StringView_FromCString( "value" ), key_value_type_t::STRING );
    }
    bool changed = false;
    const auto result = node != nullptr && KeyValue_SetString( document, node, value )
        ? MapEntityEdit_SetProperty( map, { &id, 1 }, key, node, &changed ) : map_status_t::OUT_OF_MEMORY;
    KeyValue_DestroyDocument( document );
    return result;
}

map_status_t MapCreate_Entity( map_document_t *map, string_view_t className, vec3 origin, span_t<const map_property_t> properties, u64 *pIdOut ) noexcept
{
    if ( map == nullptr || pIdOut == nullptr || !StringView_IsValid( className ) || className.cchLength == 0 || !InRange( origin ) || !Span_IsValid( properties ) ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    u64 id = 0;
    auto result = MapDocument_AddEntity( map, FirstLayer( map ), className, origin, &id );
    if ( result != map_status_t::OK ) { return result; }
    for ( usize i = 0; result == map_status_t::OK && i < properties.nCount; ++i ) {
        result = MapEntity_SetString( map, id, properties.pData[i].key, properties.pData[i].value );
    }
    if ( result == map_status_t::OK ) { *pIdOut = id; }
    return result;
}

namespace
{
// Builds and inserts a closed mesh from positions and outward-wound faces:
// fresh IDs, the material, flat shading, world-aligned UVs.
map_status_t InsertMesh( map_document_t *map, const std::vector<vec3> &points, const std::vector<std::vector<u32>> &faces,
    string_view_t material, f64 worldUnitsPerUv, u64 *pIdOut, u64 replace = 0 ) noexcept
{
    u64 ref = 0;
    auto result = MapDocument_MaterialRef( map, material, &ref );
    if ( result != map_status_t::OK ) { return result; }
    auto ids = Ids( map );
    const auto root = replace != 0 ? geo::geometry_source_id_t{ replace } : Next( ids );
    geo::mesh_source_description_t description{};
    geo::mesh_source_t source{};
    auto status = root.value != 0 ? geo::MeshSourceDescription_Init( &description, map->pAllocator, root ) : geo::geometry_status_t::LIMIT_EXCEEDED;
    for ( usize i = 0; status == OK && i < points.size(); ++i ) {
        status = geo::MeshSourceDescription_TryAddVertex( &description, points[i], Next( ids ), nullptr );
    }
    geo::mesh_face_attributes_t surface{}; surface.material.value = ref; surface.smoothingGroups = 0u;
    for ( usize f = 0; status == OK && f < faces.size(); ++f ) {
        u32 face = 0;
        status = geo::MeshSourceDescription_TryAddFace( &description, { faces[f].data(), faces[f].size() }, Next( ids ), surface, &face );
        if ( status != OK ) { break; }
        const auto &record = description.faces.pData[face];
        vec3 normal{};
        for ( u32 k = 0; k < record.cCorners; ++k ) {
            const vec3 p = description.vertices.pData[description.corners.pData[record.iFirstCorner + k].iVertex].position;
            const vec3 q = description.vertices.pData[description.corners.pData[record.iFirstCorner + ( k + 1 ) % record.cCorners].iVertex].position;
            normal = Add( normal, { ( p.y - q.y ) * ( p.z + q.z ), ( p.z - q.z ) * ( p.x + q.x ), ( p.x - q.x ) * ( p.y + q.y ) } );
        }
        for ( u32 k = 0; k < record.cCorners; ++k ) {
            auto &corner = description.corners.pData[record.iFirstCorner + k];
            corner.attributes.uv0 = WorldUv( normal, description.vertices.pData[corner.iVertex].position, worldUnitsPerUv );
        }
    }
    if ( status == OK ) { status = geo::MeshSource_TryBuild( &description, map->pAllocator, &source ); }
    if ( status == OK ) { status = replace != 0 ? geo::GeometryDocument_TryReplaceMesh( &map->geometry, &source ) : geo::GeometryDocument_TryAddMesh( &map->geometry, &source ); }
    geo::MeshSource_Shutdown( &source );
    geo::MeshSourceDescription_Shutdown( &description );
    result = Status( status );
    if ( result == map_status_t::OK && replace != 0 ) {
        map->nextId = std::max( map->nextId, ids.next.value );
        ++map->geometry.revision;
    } else if ( result == map_status_t::OK ) { result = Adopt( map, root.value, ids ); }
    if ( result == map_status_t::OK && pIdOut != nullptr ) { *pIdOut = root.value; }
    return result;
}

vec3 Newell( const std::vector<vec3> &points, const std::vector<u32> &face ) noexcept
{
    vec3 normal{};
    for ( usize k = 0; k < face.size(); ++k ) {
        const vec3 p = points[face[k]], q = points[face[( k + 1 ) % face.size()]];
        normal = Add( normal, { ( p.y - q.y ) * ( p.z + q.z ), ( p.z - q.z ) * ( p.x + q.x ), ( p.x - q.x ) * ( p.y + q.y ) } );
    }
    return normal;
}

} // namespace

map_status_t MapCreate_PolygonMesh( map_document_t *map, span_t<const vec3> polygon, vec3 extrusion, string_view_t material, f64 worldUnitsPerUv, u64 *pIdOut ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( pIdOut == nullptr || !Span_IsValid( polygon ) || polygon.nCount < 3 || polygon.nCount > 256 || !InRange( extrusion ) ||
         !std::isfinite( worldUnitsPerUv ) || worldUnitsPerUv <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    std::vector<vec3> base( polygon.pData, polygon.pData + polygon.nCount );
    std::vector<u32> loop;
    for ( u32 i = 0; i < base.size(); ++i ) {
        if ( !InRange( base[i] ) || !InRange( Add( base[i], extrusion ) ) ) { return map_status_t::INVALID_ARGUMENT; }
        loop.push_back( i );
    }
    // Wind the base so it faces along the extrusion; it must be flat.
    vec3 normal = Newell( base, loop );
    const f64 area = Length( normal );
    if ( area <= 1e-9 || std::fabs( Dot( normal, extrusion ) ) <= 1e-9 * area * Length( extrusion ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( Dot( normal, extrusion ) < 0.0 ) { std::reverse( base.begin(), base.end() ); normal = Mul( normal, -1.0 ); }
    for ( const vec3 &p : base ) {
        if ( std::fabs( Dot( Sub( p, base[0] ), normal ) ) / area > 1e-6 * std::max( 1.0, Length( Sub( p, base[0] ) ) ) ) { return map_status_t::INVALID_ARGUMENT; }
    }
    const u32 n = static_cast<u32>( base.size() );
    std::vector<vec3> points = base;
    for ( u32 i = 0; i < n; ++i ) { points.push_back( Add( base[i], extrusion ) ); }
    std::vector<std::vector<u32>> faces;
    std::vector<u32> bottom, top;
    for ( u32 i = 0; i < n; ++i ) { bottom.push_back( n - 1 - i ); top.push_back( n + i ); }
    faces.push_back( bottom ); // Faces away from the extrusion.
    faces.push_back( top );
    for ( u32 i = 0; i < n; ++i ) { faces.push_back( { i, ( i + 1 ) % n, n + ( i + 1 ) % n, n + i } ); }
    return InsertMesh( map, points, faces, material, worldUnitsPerUv, pIdOut );
}

namespace
{
// An evaluator's editable mesh (a sweep or a lathe) as a new map mesh:
// every face as its loop of corners, IDs and world-aligned UVs added.
map_status_t InsertEditable( map_document_t *map, const geo::editable_mesh_t &mesh, string_view_t material, f64 worldUnitsPerUv, u64 *pIdOut ) noexcept
{
    std::vector<vec3> points;
    std::unordered_map<u32, u32> index;
    ( void )GenerationPool_ForEach( &mesh.vertices, [&]( geo::geometry_mesh_vertex_handle_t handle, const geo::mesh_vertex_record_t &vertex ) noexcept -> bool_t {
        index[handle.nSlot] = static_cast<u32>( points.size() ); points.push_back( vertex.position ); return CY_TRUE;
    } );
    std::vector<std::vector<u32>> faces;
    bool ok = true;
    ( void )GenerationPool_ForEach( &mesh.faces, [&]( geo::geometry_mesh_face_handle_t, const geo::mesh_face_record_t &face ) noexcept -> bool_t {
        const auto *loop = geo::EditableMesh_GetLoop( &mesh, face.hOuterLoop );
        if ( loop == nullptr ) { ok = false; return CY_FALSE; }
        std::vector<u32> corners;
        auto edge = loop->hFirstHalfEdge;
        for ( u32 c = 0; c < loop->cHalfEdges; ++c ) {
            const auto *record = geo::EditableMesh_GetHalfEdge( &mesh, edge );
            if ( record == nullptr ) { ok = false; return CY_FALSE; }
            corners.push_back( index[record->hOrigin.nSlot] );
            edge = record->hNext;
        }
        faces.push_back( std::move( corners ) );
        return CY_TRUE;
    } );
    if ( !ok || points.empty() || faces.empty() ) { return map_status_t::GEOMETRY_FAILED; }
    return InsertMesh( map, points, faces, material, worldUnitsPerUv, pIdOut );
}

map_status_t SweepStatus( geo::geometry_status_t status ) noexcept
{
    return status == geo::geometry_status_t::OK ? map_status_t::OK : status == geo::geometry_status_t::ALLOCATION_FAILED ? map_status_t::OUT_OF_MEMORY :
           status == geo::geometry_status_t::INVALID_ARGUMENT ? map_status_t::INVALID_ARGUMENT : map_status_t::GEOMETRY_FAILED;
}
} // namespace

map_status_t MapCreate_Sweep( map_document_t *map, span_t<const vec3> points, bool bClosed, span_t<const math::vec2d_t> profile, bool bProfileClosed,
    string_view_t material, f64 worldUnitsPerUv, u64 *pIdOut ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( pIdOut == nullptr || !Span_IsValid( points ) || points.nCount < 2 || points.nCount > 256 || !Span_IsValid( profile ) || profile.nCount < 2 ||
         !std::isfinite( worldUnitsPerUv ) || worldUnitsPerUv <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0; i < points.nCount; ++i ) { if ( !InRange( points.pData[i] ) ) { return map_status_t::INVALID_ARGUMENT; } }
    // The curve is a scratch network: its IDs never reach the map.
    geo::curve_network_t network{};
    geo::geometry_source_id_allocator_t scratch{ { 1u } };
    auto status = geo::CurveNetwork_Init( &network, map->pAllocator, { 1u } );
    u32 path = 0;
    if ( status == OK ) { status = geo::CurveNetwork_TryAddCatmullRomPath( &network, points, bClosed, &scratch, &path ); }
    geo::sweep_options_t options{};
    options.sampling.fMaxSpacing = 8.0;
    options.bProfileClosed = bProfileClosed;
    options.bCapEnds = true;
    geo::editable_mesh_t mesh{};
    if ( status == OK ) {
        const auto report = geo::Sweep_TryAlongPath( &network, path, profile, options, map->pAllocator, &mesh );
        status = report.status;
    }
    result = status == OK ? InsertEditable( map, mesh, material, worldUnitsPerUv, pIdOut ) : SweepStatus( status );
    geo::EditableMesh_Shutdown( &mesh );
    geo::CurveNetwork_Shutdown( &network );
    return result;
}

namespace
{
// This route retains the evaluator's per-corner charts. Materials and
// placement are prepared privately, so adding the checked source is the
// last fallible step before publication.
map_status_t InsertHollowSurface( map_document_t *map, const geo::editable_mesh_t &mesh,
    const geo::mesh_attribute_store_t &attributes, string_view_t material, u64 *pIdOut ) noexcept
{
    geo::mesh_source_description_t description{};
    geo::mesh_source_t source{};
    map_materials_t materials{};
    vector_t<map_geometry_record_t> records{};
    vector_t<u32> vertexIndices{};
    const auto finish = [&]( map_status_t result ) noexcept {
        geo::MeshSource_Shutdown( &source );
        geo::MeshSourceDescription_Shutdown( &description );
        MapMaterials_Shutdown( &materials );
        Vector_Shutdown( &records );
        Vector_Shutdown( &vertexIndices );
        return result;
    };
    u32 layer = CY_U32_MAX;
    const auto targetLayer = FirstLayer( map );
    for ( usize i = 0; i < map->layers.nCount; ++i ) {
        if ( StringView_Equals( StringView_FromCString( map->layers.pData[i].id ), targetLayer ) ) {
            layer = static_cast<u32>( i ); break;
        }
    }
    if ( layer == CY_U32_MAX ) { return finish( map_status_t::UNKNOWN_LAYER ); }
    if ( !MapMaterials_Init( &materials, map->pAllocator ) ||
         !Vector_Append( &materials.text, span_t<const char>{ map->materials.text.pData, map->materials.text.nCount } ) ||
         !Vector_Append( &materials.entries, span_t<const map_material_entry_t>{ map->materials.entries.pData, map->materials.entries.nCount } ) ||
         !Vector_Append( &materials.sorted, span_t<const u32>{ map->materials.sorted.pData, map->materials.sorted.nCount } ) ||
         !Vector_Init( &records, map->pAllocator ) ||
         !Vector_Append( &records, span_t<const map_geometry_record_t>{ map->geometryRecords.pData, map->geometryRecords.nCount } ) ||
         !Vector_Init( &vertexIndices, map->pAllocator ) ||
         !Vector_Resize( &vertexIndices, mesh.vertices.cSlots ) ) {
        return finish( map_status_t::OUT_OF_MEMORY );
    }
    for ( usize i = 0; i < vertexIndices.nCount; ++i ) { vertexIndices.pData[i] = CY_U32_MAX; }
    u64 materialRef = 0;
    if ( !MapMaterials_Intern( &materials, material, &materialRef ) ) { return finish( map_status_t::OUT_OF_MEMORY ); }
    auto ids = Ids( map );
    const auto root = Next( ids );
    auto status = root.value != 0 ? geo::MeshSourceDescription_Init( &description, map->pAllocator, root )
                                  : geo::geometry_status_t::LIMIT_EXCEEDED;
    if ( status == OK ) {
        (void)GenerationPool_ForEach( &mesh.vertices,
            [&]( geo::geometry_mesh_vertex_handle_t handle, const geo::mesh_vertex_record_t &vertex ) noexcept -> bool_t {
                if ( !InRange( vertex.position ) ) { status = geo::geometry_status_t::INVALID_ARGUMENT; return false; }
                const auto id = Next( ids );
                if ( id.value == 0 ) { status = geo::geometry_status_t::LIMIT_EXCEEDED; return false; }
                status = geo::MeshSourceDescription_TryAddVertex( &description, vertex.position, id, &vertexIndices.pData[handle.nSlot] );
                return status == OK;
            } );
    }
    if ( status == OK ) {
        (void)GenerationPool_ForEach( &mesh.faces,
            [&]( geo::geometry_mesh_face_handle_t handle, const geo::mesh_face_record_t &face ) noexcept -> bool_t {
                const auto *loop = geo::EditableMesh_GetLoop( &mesh, face.hOuterLoop );
                if ( loop == nullptr || loop->cHalfEdges < 3 || loop->cHalfEdges > 256 ) {
                    status = geo::geometry_status_t::INVALID_TOPOLOGY; return false;
                }
                u32 indices[256]{};
                geo::geometry_mesh_half_edge_handle_t corners[256]{};
                auto current = loop->hFirstHalfEdge;
                for ( u32 k = 0; k < loop->cHalfEdges; ++k ) {
                    const auto *half = geo::EditableMesh_GetHalfEdge( &mesh, current );
                    if ( half == nullptr || half->hOrigin.nSlot >= vertexIndices.nCount ||
                         vertexIndices.pData[half->hOrigin.nSlot] == CY_U32_MAX ) {
                        status = geo::geometry_status_t::CORRUPT_STATE; return false;
                    }
                    indices[k] = vertexIndices.pData[half->hOrigin.nSlot]; corners[k] = current; current = half->hNext;
                }
                if ( current.nSlot != loop->hFirstHalfEdge.nSlot || current.nGeneration != loop->hFirstHalfEdge.nGeneration ) {
                    status = geo::geometry_status_t::CORRUPT_STATE; return false;
                }
                const auto id = Next( ids );
                if ( id.value == 0 ) { status = geo::geometry_status_t::LIMIT_EXCEEDED; return false; }
                auto surface = geo::MeshAttributeStore_GetFace( &attributes, handle ); surface.material.value = materialRef;
                u32 index = 0;
                status = geo::MeshSourceDescription_TryAddFace( &description, { indices, loop->cHalfEdges }, id, surface, &index );
                if ( status != OK ) { return false; }
                const auto first = description.faces.pData[index].iFirstCorner;
                for ( u32 k = 0; k < loop->cHalfEdges; ++k ) {
                    description.corners.pData[first + k].attributes = geo::MeshAttributeStore_GetCorner( &attributes, corners[k] );
                }
                return true;
            } );
    }
    if ( status == OK ) {
        (void)GenerationPool_ForEach( &mesh.edges,
            [&]( geo::geometry_mesh_edge_handle_t handle, const geo::mesh_edge_record_t &edge ) noexcept -> bool_t {
                const auto *half = geo::EditableMesh_GetHalfEdge( &mesh, edge.hHalfEdge );
                const auto *next = half != nullptr ? geo::EditableMesh_GetHalfEdge( &mesh, half->hNext ) : nullptr;
                if ( next == nullptr || half->hOrigin.nSlot >= vertexIndices.nCount || next->hOrigin.nSlot >= vertexIndices.nCount ) {
                    status = geo::geometry_status_t::CORRUPT_STATE; return false;
                }
                status = geo::MeshSourceDescription_TrySetEdge( &description, vertexIndices.pData[half->hOrigin.nSlot],
                    vertexIndices.pData[next->hOrigin.nSlot], geo::MeshAttributeStore_GetEdge( &attributes, handle ), edge.creaseWeight );
                return status == OK;
            } );
    }
    if ( status != OK ) { return finish( Status( status ) ); }
    // A saved map needs a representable next ID above every created part.
    if ( geo::GeometrySourceIdAllocator_IsExhausted( &ids ) ) { return finish( map_status_t::LIMIT_EXCEEDED ); }
    usize insertion = 0;
    while ( insertion < records.nCount && records.pData[insertion].id < root.value ) { ++insertion; }
    if ( insertion < records.nCount && records.pData[insertion].id == root.value ) { return finish( map_status_t::INVALID_ARGUMENT ); }
    if ( !Vector_Insert( &records, insertion, map_geometry_record_t{ root.value, layer, 0u } ) ) { return finish( map_status_t::OUT_OF_MEMORY ); }
    status = geo::MeshSource_TryBuild( &description, map->pAllocator, &source );
    if ( status == OK ) { status = geo::GeometryDocument_TryAddMesh( &map->geometry, &source ); }
    if ( status != OK ) { return finish( Status( status ) ); }
    // Vector_Move transfers into canonical-empty storage. Release the old
    // tables only after the final fallible operation has committed.
    MapMaterials_Shutdown( &map->materials );
    Vector_Shutdown( &map->geometryRecords );
    Vector_Move( &map->materials.text, &materials.text );
    Vector_Move( &map->materials.entries, &materials.entries );
    Vector_Move( &map->materials.sorted, &materials.sorted );
    Vector_Move( &map->geometryRecords, &records );
    map->nextId = ids.next.value;
    ++map->geometry.revision;
    *pIdOut = root.value;
    return finish( map_status_t::OK );
}
} // namespace

map_status_t MapCreate_HollowPipe( map_document_t *map, span_t<const vec3> points, f64 outerRadius, f64 wallThickness,
    u32 sides, string_view_t material, f64 worldUnitsPerUv, u64 *pIdOut ) noexcept
{
    const auto writable = Writable( map );
    if ( writable != map_status_t::OK ) { return writable; }
    if ( pIdOut == nullptr || !Span_IsValid( points ) || points.nCount < 2 || points.nCount > 256 || sides < 3 || sides > 128 ||
         !std::isfinite( outerRadius ) || !std::isfinite( wallThickness ) || outerRadius <= 0.0 || wallThickness <= 0.0 || wallThickness >= outerRadius ||
         !std::isfinite( worldUnitsPerUv ) || worldUnitsPerUv <= 0.0 || !StringView_IsValid( material ) || material.cchLength > MAP_MATERIAL_PATH_MAX ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    for ( usize i = 0; i < points.nCount; ++i ) { if ( !InRange( points.pData[i] ) ) { return map_status_t::INVALID_ARGUMENT; } }
    geo::curve_network_t network{};
    geo::geometry_source_id_allocator_t scratch{ { 1u } };
    geo::editable_mesh_t mesh{};
    geo::mesh_attribute_store_t attributes{};
    auto status = geo::CurveNetwork_Init( &network, map->pAllocator, { 1u } );
    u32 path = 0;
    if ( status == OK ) { status = geo::CurveNetwork_TryAddCatmullRomPath( &network, points, false, &scratch, &path ); }
    geo::hollow_sweep_options_t options{};
    options.sampling.fMaxSpacing = 8.0;
    options.fOuterRadius = outerRadius; options.fWallThickness = wallThickness;
    options.cSides = sides; options.fWorldUnitsPerUv = worldUnitsPerUv;
    if ( status == OK ) { status = geo::Sweep_TryHollowCircularPath( &network, path, options, map->pAllocator, &mesh, &attributes ).status; }
    const auto result = status == OK ? InsertHollowSurface( map, mesh, attributes, material, pIdOut ) :
        status == geo::geometry_status_t::INVALID_ARGUMENT ? map_status_t::INVALID_ARGUMENT : Status( status );
    geo::MeshAttributeStore_Shutdown( &attributes );
    geo::EditableMesh_Shutdown( &mesh );
    geo::CurveNetwork_Shutdown( &network );
    return result;
}

map_status_t MapCreate_Walls( map_document_t *map, span_t<const vec3> points, vec3 up, bool bClosed, f64 thickness, f64 height, map_wall_join_t join,
    string_view_t material, f64 worldUnitsPerUv, u64 *pIdOut ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    const f64 upLength = Length( up );
    if ( pIdOut == nullptr || !Span_IsValid( points ) || points.nCount < 2 || points.nCount > 1024 || !std::isfinite( thickness ) || thickness <= 0.0 ||
         !std::isfinite( height ) || height <= 0.0 || !( upLength > 1e-9 ) || !std::isfinite( worldUnitsPerUv ) || worldUnitsPerUv <= 0.0 ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    const vec3 n = Mul( up, 1.0 / upLength );
    for ( usize i = 0; i < points.nCount; ++i ) { if ( !InRange( points.pData[i] ) ) { return map_status_t::INVALID_ARGUMENT; } }
    // The path in its plane's own 2D frame.
    geo::planar_frame_t frame{};
    auto status = geo::PlanarFrame_TryFromPlane( { n, -Dot( n, points.pData[0] ) }, &frame );
    std::vector<math::vec2d_t> flat;
    for ( usize i = 0; status == OK && i < points.nCount; ++i ) { flat.push_back( geo::PlanarFrame_Project( frame, points.pData[i] ) ); }
    geo::planar_offset_options_t options{};
    options.join = join == map_wall_join_t::ROUND ? geo::planar_join_t::ROUND : join == map_wall_join_t::BEVEL ? geo::planar_join_t::BEVEL : geo::planar_join_t::MITER;
    geo::geometry_source_id_allocator_t scratch{ { 1u } };
    geo::planar_region_t region{};
    if ( status == OK ) {
        status = geo::Planar_TryStrokePolyline( frame, { flat.data(), flat.size() }, bClosed, 0.5 * thickness, options, map->geometryPolicy, { 1u }, &scratch,
                                                map->pAllocator, &region );
    }
    geo::editable_mesh_t mesh{};
    if ( status == OK ) { status = geo::PlanarExtrude_TryExtrude( &region, height, map->geometryPolicy, map->pAllocator, &mesh, nullptr ); }
    result = status == OK ? InsertEditable( map, mesh, material, worldUnitsPerUv, pIdOut ) : SweepStatus( status );
    geo::EditableMesh_Shutdown( &mesh );
    geo::PlanarRegion_Shutdown( &region );
    return result;
}

map_status_t MapCreate_BrushSweep( map_document_t *map, span_t<const map_brush_face_t> faces, map_sweep_path_t path, vec3 offset, vec3 axisOrigin, vec3 axisDir,
    f64 angle, u32 segments, u32 iterations, vector_t<u64> *pIdsOut ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( pIdsOut == nullptr || !Vector_IsValid( pIdsOut ) || !Span_IsValid( faces ) || faces.nCount == 0 || faces.nCount > 64 || segments == 0 || segments > 64 ||
         iterations == 0 || iterations > 64 ) { return map_status_t::INVALID_ARGUMENT; }
    std::vector<geo::brush_source_t> sources( faces.nCount );
    std::vector<geo::brush_sweep_face_t> sweep( faces.nCount );
    auto status = OK;
    for ( usize i = 0; status == OK && i < faces.nCount; ++i ) {
        status = geo::GeometryDocument_TryCopyBrushSource( &map->geometry, { faces.pData[i].brush }, map->pAllocator, &sources[i] );
        u32 iSide = CY_U32_MAX;
        for ( usize k = 0; status == OK && k < sources[i].solid.sides.nCount; ++k ) {
            if ( sources[i].solid.sides.pData[k].sourceId.value == faces.pData[i].side ) { iSide = static_cast<u32>( k ); }
        }
        if ( status == OK && iSide == CY_U32_MAX ) { status = geo::geometry_status_t::INVALID_ARGUMENT; }
        sweep[i] = { &sources[i], iSide };
    }
    geo::brush_sweep_params_t params{};
    params.path = path == map_sweep_path_t::ARC ? geo::brush_sweep_path_t::ARC : path == map_sweep_path_t::S_BEND ? geo::brush_sweep_path_t::S_BEND : geo::brush_sweep_path_t::STRAIGHT;
    params.offset = offset; params.axisOrigin = axisOrigin; params.axisDirection = axisDir; params.angleRadians = angle;
    params.cSegments = segments; params.cIterations = iterations;
    auto ids = Ids( map );
    const usize capacity = faces.nCount * segments * iterations;
    std::vector<geo::brush_source_t> made( capacity );
    u32 count = 0;
    if ( status == OK ) { status = geo::BrushSweep_TryBuild( { sweep.data(), sweep.size() }, params, map->pAllocator, map->geometryPolicy, &ids, made.data(), static_cast<u32>( capacity ), &count ); }
    std::vector<u64> roots;
    for ( u32 i = 0; status == OK && i < count; ++i ) {
        status = geo::GeometryDocument_TryAddBrushSource( &map->geometry, &made[i] );
        roots.push_back( made[i].solid.sourceId.value );
    }
    for ( auto &source : made ) { geo::BrushSource_Shutdown( &source ); }
    for ( auto &source : sources ) { geo::BrushSource_Shutdown( &source ); }
    result = Status( status );
    for ( usize i = 0; result == map_status_t::OK && i < roots.size(); ++i ) {
        result = Adopt( map, roots[i], ids );
        if ( result == map_status_t::OK && !Vector_PushBack( pIdsOut, roots[i] ) ) { result = map_status_t::OUT_OF_MEMORY; }
    }
    return result;
}

map_status_t MapCreate_Lathe( map_document_t *map, span_t<const math::vec2d_t> profile, vec3 axisOrigin, vec3 axisDir, f64 angle, u32 segments,
    string_view_t material, f64 worldUnitsPerUv, u64 *pIdOut ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( pIdOut == nullptr || !Span_IsValid( profile ) || profile.nCount < 2 || segments < 3 || segments > 256 || !std::isfinite( angle ) || angle <= 0.0 ||
         !InRange( axisOrigin ) || !std::isfinite( worldUnitsPerUv ) || worldUnitsPerUv <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0; i < profile.nCount; ++i ) { if ( !( profile.pData[i].x >= 0.0 ) ) { return map_status_t::INVALID_ARGUMENT; } }
    geo::lathe_options_t options{};
    options.angle = angle; options.segments = segments;
    geo::editable_mesh_t mesh{};
    const auto report = geo::Sweep_TryLathe( profile, axisOrigin, axisDir, options, map->pAllocator, &mesh );
    result = report.status == OK ? InsertEditable( map, mesh, material, worldUnitsPerUv, pIdOut ) : SweepStatus( report.status );
    geo::EditableMesh_Shutdown( &mesh );
    return result;
}

map_status_t MapCreate_ReplaceMeshPolygons( map_document_t *map, u64 meshId, const std::vector<vec3> &points, const std::vector<std::vector<u32>> &faces,
    string_view_t material, f64 worldUnitsPerUv ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( geo::GeometryDocument_FindMesh( &map->geometry, { meshId } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    if ( points.empty() || faces.empty() || !std::isfinite( worldUnitsPerUv ) || worldUnitsPerUv <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    return InsertMesh( map, points, faces, material, worldUnitsPerUv, nullptr, meshId );
}

map_status_t MapCreate_Displacement( map_document_t *map, const map_displacement_desc_t &desc, string_view_t material, f64 worldUnitsPerUv, u64 *pIdOut ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( pIdOut == nullptr || desc.power < geo::kDisplacementPowerMin || desc.power > geo::kDisplacementPowerMax || !std::isfinite( desc.elevation ) ||
         !std::isfinite( desc.noiseAmplitude ) || !std::isfinite( desc.noiseScale ) || desc.noiseScale <= 0.0 || desc.noiseOctaves == 0 ||
         desc.noiseOctaves > geo::kDisplacementOctavesMax || !std::isfinite( worldUnitsPerUv ) || worldUnitsPerUv <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    geo::displacement_quad_t quad{};
    for ( int i = 0; i < 4; ++i ) {
        if ( !InRange( desc.corners[i] ) ) { return map_status_t::INVALID_ARGUMENT; }
        quad.corners[i] = desc.corners[i];
    }
    if ( geo::Displacement_ValidateQuad( quad ) != OK ) { return map_status_t::INVALID_ARGUMENT; }
    const std::vector<vec3> corners( quad.corners, quad.corners + 4 );
    const vec3 normal = Newell( corners, { 0, 1, 2, 3 } );
    for ( int i = 0; i < 4; ++i ) { quad.uvs[i] = WorldUv( normal, quad.corners[i], worldUnitsPerUv ); }
    u64 ref = 0;
    result = MapDocument_MaterialRef( map, material, &ref );
    if ( result != map_status_t::OK ) { return result; }
    geo::displacement_t displacement{};
    auto status = geo::Displacement_Init( &displacement, map->pAllocator, desc.power );
    displacement.elevation = desc.elevation;
    displacement.noise.amplitude = desc.noiseAmplitude;
    displacement.noise.frequency = 1.0 / desc.noiseScale;
    displacement.noise.cOctaves = desc.noiseOctaves;
    displacement.noise.seed = desc.seed;
    auto ids = Ids( map );
    geo::mesh_source_t mesh{};
    if ( status == OK ) { status = geo::Displacement_Validate( &displacement ); }
    if ( status == OK ) { status = geo::Displacement_TryBuildMesh( &displacement, quad, { ref }, map->pAllocator, &ids, &mesh ); }
    const u64 root = mesh.sourceId.value;
    if ( status == OK ) { status = geo::GeometryDocument_TryAddMesh( &map->geometry, &mesh ); }
    geo::MeshSource_Shutdown( &mesh );
    geo::Displacement_Shutdown( &displacement );
    result = Status( status );
    if ( result == map_status_t::OK ) { result = Adopt( map, root, ids ); }
    if ( result == map_status_t::OK ) { *pIdOut = root; }
    return result;
}

map_status_t MapCreate_PathMesh( map_document_t *map, span_t<const vec3> path, f64 width, f64 height, string_view_t material, f64 worldUnitsPerUv, u64 *pIdOut ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( pIdOut == nullptr || !Span_IsValid( path ) || path.nCount < 2 || path.nCount > 256 || !std::isfinite( width ) || width <= 0.0 ||
         !std::isfinite( height ) || height <= 0.0 || !std::isfinite( worldUnitsPerUv ) || worldUnitsPerUv <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    // Drop repeated points: a zero-length segment has no direction.
    std::vector<vec3> line;
    for ( usize i = 0; i < path.nCount; ++i ) {
        if ( !InRange( path.pData[i] ) ) { return map_status_t::INVALID_ARGUMENT; }
        if ( line.empty() || Length( Sub( path.pData[i], line.back() ) ) > 1e-6 ) { line.push_back( path.pData[i] ); }
    }
    if ( line.size() < 2 ) { return map_status_t::INVALID_ARGUMENT; }
    // One rectangle ring per point: across = horizontal perpendicular to the
    // local direction (averaged at joints), standing up along world Z.
    std::vector<vec3> points;
    const u32 n = static_cast<u32>( line.size() );
    for ( u32 i = 0; i < n; ++i ) {
        vec3 direction = Sub( line[std::min( i + 1, n - 1 )], line[i == 0 ? 0 : i - 1] );
        direction.z = 0.0;
        if ( Length( direction ) <= 1e-9 ) { return map_status_t::INVALID_ARGUMENT; } // A vertical path has no "across".
        direction = Mul( direction, 1.0 / Length( direction ) );
        const vec3 across = Mul( Cross( { 0, 0, 1 }, direction ), width * 0.5 );
        points.push_back( Sub( line[i], across ) );
        points.push_back( Add( line[i], across ) );
        points.push_back( Add( Add( line[i], across ), { 0, 0, height } ) );
        points.push_back( Add( Sub( line[i], across ), { 0, 0, height } ) );
    }
    for ( const vec3 &p : points ) { if ( !InRange( p ) ) { return map_status_t::INVALID_ARGUMENT; } }
    std::vector<std::vector<u32>> faces;
    faces.push_back( { 0, 1, 2, 3 } );
    faces.push_back( { 4 * ( n - 1 ) + 0, 4 * ( n - 1 ) + 1, 4 * ( n - 1 ) + 2, 4 * ( n - 1 ) + 3 } );
    for ( u32 i = 0; i + 1 < n; ++i ) {
        for ( u32 k = 0; k < 4; ++k ) { faces.push_back( { 4 * i + k, 4 * i + ( k + 1 ) % 4, 4 * ( i + 1 ) + ( k + 1 ) % 4, 4 * ( i + 1 ) + k } ); }
    }
    // Per-segment orientation: each wall against its own segment's centre.
    std::vector<vec3> ringCenters;
    for ( u32 i = 0; i < n; ++i ) { ringCenters.push_back( Mul( Add( Add( points[4 * i], points[4 * i + 1] ), Add( points[4 * i + 2], points[4 * i + 3] ) ), 0.25 ) ); }
    for ( usize f = 0; f < faces.size(); ++f ) {
        auto &face = faces[f];
        vec3 inside{};
        if ( f == 0 ) { inside = ringCenters[1]; }
        else if ( f == 1 ) { inside = ringCenters[n - 2]; }
        else { const u32 segment = static_cast<u32>( ( f - 2 ) / 4 ); inside = Mul( Add( ringCenters[segment], ringCenters[segment + 1] ), 0.5 ); }
        vec3 middle{};
        for ( u32 i : face ) { middle = Add( middle, points[i] ); }
        middle = Mul( middle, 1.0 / static_cast<f64>( face.size() ) );
        if ( Dot( Newell( points, face ), Sub( middle, inside ) ) < 0.0 ) { std::reverse( face.begin(), face.end() ); }
    }
    return InsertMesh( map, points, faces, material, worldUnitsPerUv, pIdOut );
}

geo::geometry_status_t MapCreate_BuildPatch( map_patch_shape_t shape, math::aabbd_t bounds, u32 axis, u32 segments, u32 materialId,
    geo::geometry_source_id_t root, geo::geometry_source_id_allocator_t *pIds, const allocator_t *pAllocator, geo::patch_surface_t *pOut ) noexcept
{
    if ( pIds == nullptr || pOut == nullptr || pAllocator == nullptr || shape >= map_patch_shape_t::COUNT || axis > 2 || root.value == 0 ||
         !InRange( bounds.minimum ) || !InRange( bounds.maximum ) ) { return geo::geometry_status_t::INVALID_ARGUMENT; }
    const u32 u = ( axis + 1 ) % 3, v = ( axis + 2 ) % 3;
    const f64 extentU = Axis( bounds.maximum, u ) - Axis( bounds.minimum, u ), extentV = Axis( bounds.maximum, v ) - Axis( bounds.minimum, v );
    const f64 extentA = Axis( bounds.maximum, axis ) - Axis( bounds.minimum, axis );
    if ( extentU <= 0.0 || extentV <= 0.0 ||
         ( shape != map_patch_shape_t::PLANE && shape != map_patch_shape_t::DISC && shape != map_patch_shape_t::TORUS && extentA <= 0.0 ) ) {
        return geo::geometry_status_t::INVALID_ARGUMENT;
    }
    const vec3 center = Mul( Add( bounds.minimum, bounds.maximum ), 0.5 );
    vec3 base = center; base = Add( base, Mul( AxisVector( axis ), Axis( bounds.minimum, axis ) - Axis( center, axis ) ) );
    geo::patch_revolve_frame_t frame{}; frame.center = base; frame.axis = AxisVector( axis ); frame.reference = AxisVector( u );
    geo::patch_primitive_common_t common{};
    common.cSegments = std::clamp<u32>( segments, geo::kPatchPrimitiveSegmentsMin, geo::kPatchPrimitiveSegmentsMax );
    common.materialId = materialId;
    const f64 radius = std::min( extentU, extentV ) * 0.5;
    geo::geometry_status_t status = geo::geometry_status_t::INVALID_ARGUMENT;
    switch ( shape ) {
        case map_patch_shape_t::PLANE:
            status = geo::Patch_TryInitFlat( pOut, pAllocator, geo::patch_basis_t::BIQUADRATIC_BEZIER, 3, 3,
                Add( bounds.minimum, Mul( AxisVector( axis ), extentA ) ), Mul( AxisVector( u ), extentU ), Mul( AxisVector( v ), extentV ), root, pIds );
            pOut->materialId = materialId;
            break;
        case map_patch_shape_t::CYLINDER:
            status = geo::PatchPrimitive_TryMakeCone( frame, radius, radius, extentA, common, root, pIds, pAllocator, pOut ); break;
        case map_patch_shape_t::CONE:
            status = geo::PatchPrimitive_TryMakeCone( frame, radius, 0.0, extentA, common, root, pIds, pAllocator, pOut ); break;
        case map_patch_shape_t::SPHERE:
            frame.center = center;
            status = geo::PatchPrimitive_TryMakeSphere( frame, std::min( radius, extentA * 0.5 ), std::max<u32>( 2u, common.cSegments / 2u ), common, root, pIds, pAllocator, pOut );
            break;
        case map_patch_shape_t::DISC:
            status = geo::PatchPrimitive_TryMakeDisc( frame, radius, common, root, pIds, pAllocator, pOut ); break;
        case map_patch_shape_t::BEVEL:
            // Radiant's bevel: a quarter round in the box's U/V corner, along its axis.
            status = geo::PatchPrimitive_TryMakeBevel( bounds.minimum, Mul( AxisVector( u ), extentU ), Mul( AxisVector( v ), extentV ),
                Mul( AxisVector( axis ), extentA ), false, root, pIds, pAllocator, pOut );
            pOut->materialId = materialId;
            break;
        case map_patch_shape_t::TORUS: {
            // A ring lying in the box's U/V plane at the middle of its depth;
            // the tube fills the depth, or a quarter of the ring when flat.
            frame.center = center;
            const f64 tube = std::min( extentA > 0.0 ? extentA * 0.5 : radius * 0.25, radius * 0.5 );
            status = geo::PatchPrimitive_TryMakeTorus( frame, radius - tube, tube, std::max<u32>( 3u, common.cSegments / 2u ), common, root, pIds, pAllocator, pOut );
            break;
        }
        case map_patch_shape_t::COUNT: break;
    }
    return status;
}

map_status_t MapCreate_Patch( map_document_t *map, map_patch_shape_t shape, math::aabbd_t bounds, u32 axis, u32 segments, string_view_t material, u64 *pIdOut ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( pIdOut == nullptr || shape >= map_patch_shape_t::COUNT || axis > 2 || !InRange( bounds.minimum ) || !InRange( bounds.maximum ) ) { return map_status_t::INVALID_ARGUMENT; }
    u64 ref = 0;
    result = MapDocument_MaterialRef( map, material, &ref );
    if ( result != map_status_t::OK ) { return result; }
    auto ids = Ids( map );
    const auto root = Next( ids );
    if ( root.value == 0 ) { return map_status_t::LIMIT_EXCEEDED; }
    geo::patch_surface_t patch{};
    auto status = MapCreate_BuildPatch( shape, bounds, axis, segments, static_cast<u32>( ref ), root, &ids, map->pAllocator, &patch );
    if ( status == geo::geometry_status_t::INVALID_ARGUMENT ) { geo::Patch_Shutdown( &patch ); return map_status_t::INVALID_ARGUMENT; }
    if ( status == OK ) { status = geo::GeometryDocument_TryAddPatch( &map->geometry, &patch ); }
    geo::Patch_Shutdown( &patch );
    result = Status( status );
    if ( result == map_status_t::OK ) { result = Adopt( map, root.value, ids ); }
    if ( result == map_status_t::OK ) { *pIdOut = root.value; }
    return result;
}

namespace
{
// Clicked points as the 2k + 1 controls of a chain of quadratic spans (see
// MapCreate_PatchStrip). Empty when there are too few or too many.
template <typename point_t, typename mid_t>
std::vector<point_t> QuadraticControls( span_t<const point_t> points, bool smooth, bool closed, mid_t mid )
{
    std::vector<point_t> controls;
    const usize n = points.nCount;
    if ( n < 2 || ( closed && n < 3 ) ) { return controls; }
    const auto at = [&]( usize i ) { return points.pData[i % n]; };
    if ( smooth && closed ) {
        // Each point is a middle control; the spans meet halfway between.
        controls.push_back( mid( at( n - 1 ), at( 0 ) ) );
        for ( usize i = 0; i < n; ++i ) { controls.push_back( at( i ) ); controls.push_back( mid( at( i ), at( i + 1 ) ) ); }
    } else if ( smooth && n >= 3 ) {
        controls.push_back( at( 0 ) );
        for ( usize i = 1; i + 1 < n; ++i ) {
            controls.push_back( at( i ) );
            controls.push_back( i + 2 < n ? mid( at( i ), at( i + 1 ) ) : at( n - 1 ) );
        }
    } else {
        const usize ends = closed ? n + 1 : n;
        controls.push_back( at( 0 ) );
        for ( usize i = 1; i < ends; ++i ) { controls.push_back( mid( at( i - 1 ), at( i ) ) ); controls.push_back( at( i ) ); }
    }
    if ( controls.size() > geo::kPatchControlsPerAxisMax ) { controls.clear(); }
    return controls;
}

// Shared tail of the curve-patch creators: the id, the material, publish.
template <typename build_t>
map_status_t PublishPatch( map_document_t *map, string_view_t material, u64 *pIdOut, build_t build ) noexcept
{
    u64 ref = 0;
    auto result = MapDocument_MaterialRef( map, material, &ref );
    if ( result != map_status_t::OK ) { return result; }
    auto ids = Ids( map );
    const auto root = Next( ids );
    if ( root.value == 0 ) { return map_status_t::LIMIT_EXCEEDED; }
    geo::patch_surface_t patch{};
    auto status = build( static_cast<u32>( ref ), root, &ids, &patch );
    if ( status == OK ) { status = geo::GeometryDocument_TryAddPatch( &map->geometry, &patch ); }
    geo::Patch_Shutdown( &patch );
    result = Status( status );
    if ( result == map_status_t::OK ) { result = Adopt( map, root.value, ids ); }
    if ( result == map_status_t::OK ) { *pIdOut = root.value; }
    return result;
}
} // namespace

map_status_t MapCreate_PatchStrip( map_document_t *map, span_t<const vec3> points, bool smooth, bool closed, vec3 direction, string_view_t material, u64 *pIdOut ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( pIdOut == nullptr || !Span_IsValid( points ) || !InRange( direction ) || Length( direction ) < 1e-6 ) { return map_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0; i < points.nCount; ++i ) { if ( !InRange( points.pData[i] ) ) { return map_status_t::INVALID_ARGUMENT; } }
    const auto controls = QuadraticControls( points, smooth, closed, []( vec3 a, vec3 b ) { return Mul( Add( a, b ), 0.5 ); } );
    if ( controls.empty() ) { return points.nCount < ( closed ? 3u : 2u ) ? map_status_t::INVALID_ARGUMENT : map_status_t::LIMIT_EXCEEDED; }
    return PublishPatch( map, material, pIdOut, [&]( u32 materialId, geo::geometry_source_id_t root, geo::geometry_source_id_allocator_t *pIds, geo::patch_surface_t *pOut ) noexcept {
        return geo::PatchPrimitive_TryExtrude( { controls.data(), controls.size() }, direction, 1u, false, materialId, root, pIds, map->pAllocator, pOut );
    } );
}

map_status_t MapCreate_PatchLathe( map_document_t *map, span_t<const math::vec2d_t> profile, bool smooth, vec3 axisOrigin, vec3 axisDir, u32 segments, string_view_t material, u64 *pIdOut ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( pIdOut == nullptr || !Span_IsValid( profile ) || !InRange( axisOrigin ) || Length( axisDir ) < 1e-9 ) { return map_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0; i < profile.nCount; ++i ) {
        if ( !std::isfinite( profile.pData[i].x ) || !std::isfinite( profile.pData[i].y ) || profile.pData[i].x < 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    }
    const auto controls = QuadraticControls( profile, smooth, false, []( math::vec2d_t a, math::vec2d_t b ) { return math::vec2d_t{ ( a.x + b.x ) * 0.5, ( a.y + b.y ) * 0.5 }; } );
    if ( controls.empty() ) { return profile.nCount < 2 ? map_status_t::INVALID_ARGUMENT : map_status_t::LIMIT_EXCEEDED; }
    // The seam sits along whichever world axis is least parallel to the turn axis.
    const vec3 axis = Mul( axisDir, 1.0 / Length( axisDir ) );
    const vec3 reference = std::fabs( axis.x ) < 0.9 ? vec3{ 1, 0, 0 } : vec3{ 0, 1, 0 };
    return PublishPatch( map, material, pIdOut, [&]( u32 materialId, geo::geometry_source_id_t root, geo::geometry_source_id_allocator_t *pIds, geo::patch_surface_t *pOut ) noexcept {
        geo::patch_revolve_frame_t frame{}; frame.center = axisOrigin; frame.axis = axis; frame.reference = reference;
        geo::patch_primitive_common_t common{};
        common.cSegments = std::clamp<u32>( segments, geo::kPatchPrimitiveSegmentsMin, geo::kPatchPrimitiveSegmentsMax );
        common.materialId = materialId;
        return geo::PatchPrimitive_TryRevolve( { controls.data(), controls.size() }, frame, common, root, pIds, map->pAllocator, pOut );
    } );
}

namespace
{
u64 OwnerOf( const map_document_t *map, u64 id ) noexcept
{
    for ( usize i = 0; i < map->geometryRecords.nCount; ++i ) { if ( map->geometryRecords.pData[i].id == id ) { return map->geometryRecords.pData[i].owner; } }
    return 0;
}
bool IsGeometry( map_document_t *map, u64 id ) noexcept
{
    return geo::GeometryDocument_FindBrush( &map->geometry, { id } ) != nullptr || geo::GeometryDocument_FindMesh( &map->geometry, { id } ) != nullptr ||
           geo::GeometryDocument_FindPatch( &map->geometry, { id } ) != nullptr;
}
} // namespace

map_status_t MapCreate_TieToEntity( map_document_t *map, span_t<const u64> geometry, u64 entity, string_view_t className, u64 *pEntityOut ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( pEntityOut == nullptr || !Span_IsValid( geometry ) || geometry.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0; i < geometry.nCount; ++i ) { if ( !IsGeometry( map, geometry.pData[i] ) ) { return map_status_t::INVALID_ARGUMENT; } }
    if ( entity == 0 ) {
        if ( !StringView_IsValid( className ) || className.cchLength == 0 ) { return map_status_t::INVALID_ARGUMENT; }
        // The new entity stands at the centre of what it owns.
        map_wireframe_t wire{};
        if ( !MapWireframe_Init( &wire, map->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
        result = MapWireframe_Build( &wire, *map );
        map_bounds_t bounds{};
        for ( usize i = 0; result == map_status_t::OK && i < geometry.nCount; ++i ) {
            if ( const auto *object = MapWireframe_FindObject( wire, geometry.pData[i] ) ) { MapBounds_AddBounds( bounds, object->bounds ); }
        }
        MapWireframe_Shutdown( &wire );
        if ( result != map_status_t::OK ) { return result; }
        const vec3 centre = bounds.bHas ? Mul( Add( bounds.box.minimum, bounds.box.maximum ), 0.5 ) : vec3{};
        result = MapCreate_Entity( map, className, centre, {}, &entity );
        if ( result != map_status_t::OK ) { return result; }
    } else if ( MapDocument_FindObject( map, entity, nullptr ) == nullptr || IsGeometry( map, entity ) ) {
        return map_status_t::UNKNOWN_OBJECT;
    }
    for ( usize i = 0; i < geometry.nCount; ++i ) {
        if ( OwnerOf( map, geometry.pData[i] ) == entity ) { continue; }
        result = MapDocument_SetGeometryOwner( map, geometry.pData[i], entity );
        if ( result != map_status_t::OK ) { return result; }
    }
    ++map->geometry.revision;
    *pEntityOut = entity;
    return map_status_t::OK;
}

map_status_t MapCreate_MoveToWorld( map_document_t *map, span_t<const u64> ids, usize *pMovedOut ) noexcept
{
    if ( pMovedOut != nullptr ) { *pMovedOut = 0; }
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( !Span_IsValid( ids ) ) { return map_status_t::INVALID_ARGUMENT; }
    // The geometry named, and every piece of any entity named.
    std::vector<u64> moving, owners;
    for ( usize i = 0; i < ids.nCount; ++i ) {
        const u64 id = ids.pData[i];
        if ( IsGeometry( map, id ) ) {
            const u64 owner = OwnerOf( map, id );
            if ( owner != 0 ) { moving.push_back( id ); owners.push_back( owner ); }
            continue;
        }
        for ( usize k = 0; k < map->geometryRecords.nCount; ++k ) {
            if ( map->geometryRecords.pData[k].owner == id ) { moving.push_back( map->geometryRecords.pData[k].id ); owners.push_back( id ); }
        }
    }
    if ( moving.empty() ) { return map_status_t::OK; }
    for ( u64 id : moving ) {
        result = MapDocument_SetGeometryOwner( map, id, 0 );
        if ( result != map_status_t::OK ) { return result; }
    }
    // A brush entity with nothing left goes (Hammer).
    std::sort( owners.begin(), owners.end() );
    owners.erase( std::unique( owners.begin(), owners.end() ), owners.end() );
    for ( u64 owner : owners ) {
        bool empty = true;
        for ( usize k = 0; empty && k < map->geometryRecords.nCount; ++k ) { empty = map->geometryRecords.pData[k].owner != owner; }
        if ( empty ) { result = MapDocument_RemoveObject( map, owner ); if ( result != map_status_t::OK && result != map_status_t::UNKNOWN_OBJECT ) { return result; } }
    }
    ++map->geometry.revision;
    if ( pMovedOut != nullptr ) { *pMovedOut = moving.size(); }
    return map_status_t::OK;
}

namespace
{
// A closed mesh being built: each face is turned to face `outward`.
struct shape_t {
    std::vector<vec3> points;
    std::vector<std::vector<u32>> faces;
    u32 Point( vec3 p ) { points.push_back( p ); return static_cast<u32>( points.size() - 1 ); }
    void Face( std::vector<u32> loop, vec3 outward )
    {
        if ( Dot( Newell( points, loop ), outward ) < 0.0 ) { std::reverse( loop.begin(), loop.end() ); }
        // A quad that is not flat (an elliptical torus) is two triangles:
        // mesh faces must be planar.
        if ( loop.size() == 4 ) {
            const vec3 n = Newell( points, { loop[0], loop[1], loop[2] } );
            const f64 length = Length( n ), size = Length( Sub( points[loop[2]], points[loop[0]] ) );
            if ( length > 0.0 && std::fabs( Dot( Mul( n, 1.0 / length ), Sub( points[loop[3]], points[loop[0]] ) ) ) > 1e-10 * std::max( 1.0, size ) ) {
                faces.push_back( { loop[0], loop[1], loop[2] } );
                faces.push_back( { loop[0], loop[2], loop[3] } );
                return;
            }
        }
        faces.push_back( std::move( loop ) );
    }
};
} // namespace

map_status_t MapCreate_BlockShape( map_document_t *map, const map_primitive_desc_t &desc, string_view_t material, u64 *pIdOut ) noexcept
{
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    const auto &box = desc.bounds;
    if ( pIdOut == nullptr || !MapPrimitive_IsMesh( desc.kind ) || desc.axis > 2 || !InRange( box.minimum ) || !InRange( box.maximum ) ||
         !std::isfinite( desc.worldUnitsPerUv ) || desc.worldUnitsPerUv <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    const u32 up = desc.axis, a1 = ( up + 1 ) % 3, a2 = ( up + 2 ) % 3;
    const f64 lo[3]{ box.minimum.x, box.minimum.y, box.minimum.z }, hi[3]{ box.maximum.x, box.maximum.y, box.maximum.z };
    for ( u32 k = 0; k < 3; ++k ) { if ( !( hi[k] > lo[k] ) ) { return map_status_t::INVALID_ARGUMENT; } }
    // (across 1, across 2, up) -> world.
    const auto at = [&]( f64 c1, f64 c2, f64 h ) { vec3 p{}; f64 *c[3]{ &p.x, &p.y, &p.z }; *c[a1] = c1; *c[a2] = c2; *c[up] = h; return p; };
    const auto dir = [&]( f64 d1, f64 d2, f64 dh ) { return at( d1, d2, dh ); };
    const f64 m1 = ( lo[a1] + hi[a1] ) * 0.5, m2 = ( lo[a2] + hi[a2] ) * 0.5, r1 = ( hi[a1] - lo[a1] ) * 0.5, r2 = ( hi[a2] - lo[a2] ) * 0.5;
    const f64 bottom = lo[up], top = hi[up], height = top - bottom;
    const u32 sides = std::clamp<u32>( desc.nSides, 3u, 128u );
    constexpr f64 kPi = 3.14159265358979323846;
    shape_t shape;
    switch ( desc.kind ) {
        case map_primitive_kind_t::ARCH:
        case map_primitive_kind_t::PIPE: {
            const bool ring = desc.kind == map_primitive_kind_t::PIPE || std::fabs( desc.arcDegrees ) >= 360.0 - 1e-9;
            const f64 t = desc.wallThickness;
            if ( !std::isfinite( t ) || t <= 0.0 || t >= std::min( r1, r2 ) || !std::isfinite( desc.arcDegrees ) || std::fabs( desc.arcDegrees ) < 1.0 ||
                 !std::isfinite( desc.startDegrees ) ) { return map_status_t::INVALID_ARGUMENT; }
            const f64 arc = ring ? 360.0 : std::clamp( desc.arcDegrees, -359.0, 359.0 );
            const u32 n = ring ? sides : std::max<u32>( 1u, static_cast<u32>( std::lround( sides * std::fabs( arc ) / 360.0 ) ) );
            const u32 rings = ring ? n : n + 1;
            std::vector<std::array<u32, 4>> rim( rings ); // outer bottom, outer top, inner bottom, inner top
            const auto angle = [&]( u32 k ) { return ( desc.startDegrees + arc * k / n ) * kPi / 180.0; };
            for ( u32 k = 0; k < rings; ++k ) {
                const f64 c = std::cos( angle( k ) ), s = std::sin( angle( k ) );
                rim[k] = { shape.Point( at( m1 + r1 * c, m2 + r2 * s, bottom ) ), shape.Point( at( m1 + r1 * c, m2 + r2 * s, top ) ),
                           shape.Point( at( m1 + ( r1 - t ) * c, m2 + ( r2 - t ) * s, bottom ) ), shape.Point( at( m1 + ( r1 - t ) * c, m2 + ( r2 - t ) * s, top ) ) };
            }
            for ( u32 k = 0; k < n; ++k ) {
                const auto &A = rim[k], &B = rim[( k + 1 ) % rings];
                const f64 mid = ( angle( k ) + angle( k + 1 ) ) * 0.5;
                const vec3 radial = dir( std::cos( mid ), std::sin( mid ), 0.0 );
                shape.Face( { A[0], B[0], B[1], A[1] }, radial );
                shape.Face( { A[2], A[3], B[3], B[2] }, Mul( radial, -1.0 ) );
                shape.Face( { A[1], B[1], B[3], A[3] }, dir( 0, 0, 1 ) );
                shape.Face( { A[0], A[2], B[2], B[0] }, dir( 0, 0, -1 ) );
            }
            if ( !ring ) {
                // The ends face back along the arc.
                const f64 s0 = angle( 0 ), s1 = angle( n ), sign = arc > 0 ? 1.0 : -1.0;
                shape.Face( { rim[0][0], rim[0][1], rim[0][3], rim[0][2] }, Mul( dir( std::sin( s0 ), -std::cos( s0 ), 0 ), sign ) );
                shape.Face( { rim[n][0], rim[n][2], rim[n][3], rim[n][1] }, Mul( dir( -std::sin( s1 ), std::cos( s1 ), 0 ), sign ) );
            }
            break;
        }
        case map_primitive_kind_t::STAIRS: {
            const u32 steps = std::clamp<u32>( desc.nSteps, 1u, 256u );
            // Rising along `run` (a world axis across `up`), extruded along the other.
            i32 code = desc.stairsDirection;
            u32 run = code == 0 ? a1 : static_cast<u32>( std::abs( code ) - 1 );
            if ( run > 2 || run == up ) { run = a1; code = 1; }
            const f64 sign = code < 0 ? -1.0 : 1.0;
            const u32 side = 3 - up - run;
            const f64 length = hi[run] - lo[run];
            const auto P = [&]( f64 along, f64 across, f64 h ) {
                vec3 p{}; f64 *c[3]{ &p.x, &p.y, &p.z };
                *c[run] = sign > 0 ? lo[run] + along : hi[run] - along; *c[side] = across; *c[up] = h; return p;
            };
            const auto D = [&]( f64 along, f64 across, f64 h ) { vec3 d{}; f64 *c[3]{ &d.x, &d.y, &d.z }; *c[run] = along * sign; *c[side] = across; *c[up] = h; return d; };
            const f64 depth = length / steps, rise = height / steps;
            // Points per side (low/high across): the column lines x_i at
            // heights 0, i*rise and (i+1)*rise.
            std::vector<std::array<u32, 2>> foot( steps + 1 ), lower( steps + 1 ), upper( steps + 1 ); // [side]
            for ( u32 i = 0; i <= steps; ++i ) {
                for ( u32 s = 0; s < 2; ++s ) {
                    const f64 across = s == 0 ? lo[side] : hi[side];
                    foot[i][s] = shape.Point( P( depth * i, across, bottom ) );
                    lower[i][s] = i == 0 ? foot[i][s] : shape.Point( P( depth * i, across, bottom + rise * i ) );
                    upper[i][s] = i == steps ? lower[i][s] : shape.Point( P( depth * i, across, bottom + rise * ( i + 1 ) ) );
                }
            }
            for ( u32 s = 0; s < 2; ++s ) {
                const vec3 out = D( 0, s == 0 ? -1.0 : 1.0, 0 );
                for ( u32 i = 0; i < steps; ++i ) {
                    // Column i: its left line carries the vertex where the step below ends.
                    std::vector<u32> column{ foot[i][s], foot[i + 1][s] };
                    column.push_back( lower[i + 1][s] );
                    column.push_back( upper[i][s] );
                    if ( i > 0 ) { column.push_back( lower[i][s] ); }
                    shape.Face( column, out );
                }
            }
            std::vector<u32> base;
            for ( u32 i = 0; i <= steps; ++i ) { base.push_back( foot[i][0] ); }
            for ( u32 i = steps + 1; i-- > 0; ) { base.push_back( foot[i][1] ); }
            shape.Face( base, D( 0, 0, -1 ) );
            shape.Face( { foot[steps][0], foot[steps][1], lower[steps][1], lower[steps][0] }, D( 1, 0, 0 ) ); // Back.
            for ( u32 i = 0; i < steps; ++i ) {
                shape.Face( { upper[i][0], upper[i][1], i + 1 == steps ? lower[steps][1] : lower[i + 1][1], i + 1 == steps ? lower[steps][0] : lower[i + 1][0] }, D( 0, 0, 1 ) ); // Tread.
                shape.Face( { lower[i][0], lower[i][1], upper[i][1], upper[i][0] }, D( -1, 0, 0 ) ); // Riser (the front at i = 0).
            }
            break;
        }
        case map_primitive_kind_t::SPIRAL_STAIRS: {
            const u32 steps = std::clamp<u32>( desc.nSteps, 1u, 256u );
            const f64 inner = desc.innerRadius;
            if ( !std::isfinite( desc.turnDegrees ) || std::fabs( desc.turnDegrees ) < 1.0 || !std::isfinite( inner ) || inner < 0.0 || inner >= std::min( r1, r2 ) - 1.0 ||
                 std::fabs( desc.turnDegrees ) / steps >= 179.0 ) { return map_status_t::INVALID_ARGUMENT; }
            const f64 rise = height / steps, inner1 = std::max( inner, 0.5 ), inner2 = inner1;
            for ( u32 i = 0; i < steps; ++i ) {
                const f64 t0 = desc.turnDegrees * i / steps * kPi / 180.0, t1 = desc.turnDegrees * ( i + 1 ) / steps * kPi / 180.0;
                const f64 z0 = bottom + rise * i, z1 = z0 + rise;
                u32 p[8];
                for ( u32 k = 0; k < 8; ++k ) {
                    const f64 t = ( k & 1 ) ? t1 : t0, z = ( k & 4 ) ? z1 : z0;
                    const bool outer = ( k & 2 ) != 0;
                    p[k] = shape.Point( at( m1 + ( outer ? r1 : inner1 ) * std::cos( t ), m2 + ( outer ? r2 : inner2 ) * std::sin( t ), z ) );
                }
                vec3 centre{};
                for ( u32 k : p ) { centre = Add( centre, Mul( shape.points[k], 1.0 / 8.0 ) ); }
                const auto face = [&]( std::vector<u32> loop ) {
                    vec3 mid{};
                    for ( u32 k : loop ) { mid = Add( mid, Mul( shape.points[k], 1.0 / loop.size() ) ); }
                    shape.Face( std::move( loop ), Sub( mid, centre ) );
                };
                face( { p[0], p[1], p[3], p[2] } ); face( { p[4], p[5], p[7], p[6] } ); // Bottom, top.
                face( { p[2], p[3], p[7], p[6] } ); face( { p[0], p[1], p[5], p[4] } ); // Outer, inner.
                face( { p[0], p[2], p[6], p[4] } ); face( { p[1], p[3], p[7], p[5] } ); // Ends.
            }
            break;
        }
        case map_primitive_kind_t::TORUS: {
            const f64 half = std::min( r1, r2 );
            const f64 tube = std::min( half * std::clamp( std::isfinite( desc.tubeRatio ) ? desc.tubeRatio : 0.25, 0.02, 0.49 ), height * 0.5 );
            const u32 around = sides, across = std::max<u32>( 4u, sides / 2 );
            const f64 mid = ( bottom + top ) * 0.5;
            std::vector<u32> grid( around * across );
            for ( u32 i = 0; i < around; ++i ) {
                const f64 theta = 2.0 * kPi * i / around, c = std::cos( theta ), s = std::sin( theta );
                for ( u32 j = 0; j < across; ++j ) {
                    const f64 phi = 2.0 * kPi * j / across;
                    const f64 reach = tube * std::cos( phi );
                    grid[i * across + j] = shape.Point( at( m1 + ( r1 - tube + reach ) * c, m2 + ( r2 - tube + reach ) * s, mid + tube * std::sin( phi ) ) );
                }
            }
            const auto ringCentre = [&]( u32 i ) {
                const f64 theta = 2.0 * kPi * ( i % around ) / around;
                return at( m1 + ( r1 - tube ) * std::cos( theta ), m2 + ( r2 - tube ) * std::sin( theta ), mid );
            };
            for ( u32 i = 0; i < around; ++i ) {
                // The tube's middle between this face's two rings (a coarse
                // ring's chord, not its arc).
                const vec3 ringPoint = Mul( Add( ringCentre( i ), ringCentre( i + 1 ) ), 0.5 );
                for ( u32 j = 0; j < across; ++j ) {
                    const u32 i1 = ( i + 1 ) % around, j1 = ( j + 1 ) % across;
                    std::vector<u32> quad{ grid[i * across + j], grid[i1 * across + j], grid[i1 * across + j1], grid[i * across + j1] };
                    vec3 centre{};
                    for ( u32 k : quad ) { centre = Add( centre, Mul( shape.points[k], 0.25 ) ); }
                    shape.Face( std::move( quad ), Sub( centre, ringPoint ) );
                }
            }
            break;
        }
        default: return map_status_t::INVALID_ARGUMENT;
    }
    return InsertMesh( map, shape.points, shape.faces, material, desc.worldUnitsPerUv, pIdOut );
}

namespace
{
// One OBJ face corner: position, and texture coordinate (or none).
struct obj_corner_t { usize position; i64 uv{ -1 }; };
struct obj_face_t { std::vector<obj_corner_t> corners; std::string material; };
struct obj_group_t { std::string name; std::vector<obj_face_t> faces; };

bool ParseIndex( const std::string &token, usize count, i64 &out ) noexcept
{
    if ( token.empty() ) { return false; }
    char *end = nullptr;
    const long long value = std::strtoll( token.c_str(), &end, 10 );
    if ( end == token.c_str() || value == 0 ) { return false; }
    out = value > 0 ? value - 1 : static_cast<i64>( count ) + value; // Negative: relative to the end.
    return out >= 0 && static_cast<usize>( out ) < count;
}
} // namespace

map_status_t MapImport_Obj( map_document_t *map, string_view_t text, const map_import_options_t &options, std::vector<u64> &meshesOut, map_import_report_t *pReport )
{
    meshesOut.clear();
    if ( pReport != nullptr ) { *pReport = {}; }
    auto result = Writable( map );
    if ( result != map_status_t::OK ) { return result; }
    if ( !StringView_IsValid( text ) || !std::isfinite( options.scale ) || options.scale <= 0.0 || !InRange( options.offset ) ||
         !std::isfinite( options.worldUnitsPerUv ) || options.worldUnitsPerUv <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    // Read: positions and UVs are file-global; faces belong to the current group.
    std::vector<vec3> positions;
    std::vector<math::vec2d_t> uvs;
    std::vector<obj_group_t> groups( 1 );
    std::string material;
    map_import_report_t report{};
    usize start = 0;
    while ( start < text.cchLength ) {
        usize end = start;
        while ( end < text.cchLength && text.pData[end] != '\n' ) { ++end; }
        std::string line( text.pData + start, end - start );
        start = end + 1;
        if ( !line.empty() && line.back() == '\r' ) { line.pop_back(); }
        std::vector<std::string> tokens;
        for ( usize i = 0; i < line.size(); ) {
            while ( i < line.size() && ( line[i] == ' ' || line[i] == '\t' ) ) { ++i; }
            usize j = i;
            while ( j < line.size() && line[j] != ' ' && line[j] != '\t' ) { ++j; }
            if ( j > i ) { tokens.emplace_back( line.substr( i, j - i ) ); }
            i = j;
        }
        if ( tokens.empty() || tokens[0][0] == '#' ) { continue; }
        const std::string &kind = tokens[0];
        if ( kind == "v" && tokens.size() >= 4 ) {
            vec3 p{ std::strtod( tokens[1].c_str(), nullptr ), std::strtod( tokens[2].c_str(), nullptr ), std::strtod( tokens[3].c_str(), nullptr ) };
            if ( options.bYUp ) { p = { p.x, -p.z, p.y }; } // Y up -> Z up, keeping handedness.
            p = Add( Mul( p, options.scale ), options.offset );
            positions.push_back( p );
        } else if ( kind == "vt" && tokens.size() >= 3 ) {
            uvs.push_back( { std::strtod( tokens[1].c_str(), nullptr ), 1.0 - std::strtod( tokens[2].c_str(), nullptr ) } ); // OBJ's V runs up.
        } else if ( ( kind == "o" || kind == "g" ) && tokens.size() >= 2 ) {
            if ( !groups.back().faces.empty() ) { groups.push_back( {} ); }
            groups.back().name = tokens[1];
        } else if ( kind == "usemtl" && tokens.size() >= 2 ) {
            material = tokens[1];
        } else if ( kind == "f" && tokens.size() >= 4 ) {
            obj_face_t face; face.material = material;
            bool ok = true;
            for ( usize t = 1; ok && t < tokens.size(); ++t ) {
                const std::string &token = tokens[t];
                const usize slash = token.find( '/' );
                obj_corner_t corner{};
                i64 index = 0;
                ok = ParseIndex( token.substr( 0, slash ), positions.size(), index );
                corner.position = static_cast<usize>( index );
                if ( ok && slash != std::string::npos ) {
                    const usize second = token.find( '/', slash + 1 );
                    const std::string uv = token.substr( slash + 1, second == std::string::npos ? std::string::npos : second - slash - 1 );
                    i64 uvIndex = -1;
                    if ( !uv.empty() && ParseIndex( uv, uvs.size(), uvIndex ) ) { corner.uv = uvIndex; }
                }
                // A corner repeating its neighbour adds nothing.
                if ( ok && ( face.corners.empty() || face.corners.back().position != corner.position ) ) { face.corners.push_back( corner ); }
            }
            if ( ok && face.corners.size() > 3 && face.corners.front().position == face.corners.back().position ) { face.corners.pop_back(); }
            if ( !ok || face.corners.size() < 3 ) { ++report.skipped; continue; }
            groups.back().faces.push_back( std::move( face ) );
        }
    }
    // Build: one mesh per group, its own vertices, flat faces only.
    u64 fallback = 0;
    result = MapDocument_MaterialRef( map, options.defaultMaterial.cchLength != 0 ? options.defaultMaterial : StringView_FromCString( "materials/dev/dev_grid.cymat" ), &fallback );
    if ( result != map_status_t::OK ) { return result; }
    for ( const auto &group : groups ) {
        if ( group.faces.empty() ) { continue; }
        auto ids = Ids( map );
        const auto root = Next( ids );
        geo::mesh_source_description_t description{};
        geo::mesh_source_t source{};
        auto status = root.value != 0 ? geo::MeshSourceDescription_Init( &description, map->pAllocator, root ) : geo::geometry_status_t::LIMIT_EXCEEDED;
        // Corners at one place are one vertex (exporters often repeat a
        // position per face, which would leave the mesh in pieces).
        std::unordered_map<std::string, u32> local;
        const auto vertex = [&]( usize position ) {
            const vec3 p = positions[position];
            char key[96];
            std::snprintf( key, sizeof( key ), "%.6f %.6f %.6f", p.x, p.y, p.z );
            const auto it = local.find( key );
            if ( it != local.end() ) { return it->second; }
            u32 index = static_cast<u32>( local.size() );
            if ( status == OK ) { status = geo::MeshSourceDescription_TryAddVertex( &description, p, Next( ids ), nullptr ); }
            local.emplace( key, index );
            return index;
        };
        const auto addFace = [&]( const obj_face_t &face, const std::vector<usize> &order ) {
            std::vector<u32> loop; std::vector<vec3> points;
            for ( usize k : order ) { loop.push_back( vertex( face.corners[k].position ) ); points.push_back( positions[face.corners[k].position] ); }
            if ( status != OK ) { return; }
            u64 ref = fallback;
            if ( !face.material.empty() && face.material.find( '/' ) != std::string::npos ) {
                u64 named = 0;
                if ( MapDocument_MaterialRef( map, { face.material.data(), face.material.size() }, &named ) == map_status_t::OK ) { ref = named; }
            }
            geo::mesh_face_attributes_t surface{}; surface.material.value = ref;
            u32 index = 0;
            status = geo::MeshSourceDescription_TryAddFace( &description, { loop.data(), loop.size() }, Next( ids ), surface, &index );
            if ( status != OK ) { return; }
            const auto &record = description.faces.pData[index];
            vec3 normal{};
            for ( usize k = 0; k < points.size(); ++k ) {
                const vec3 a = points[k], b = points[( k + 1 ) % points.size()];
                normal = Add( normal, { ( a.y - b.y ) * ( a.z + b.z ), ( a.z - b.z ) * ( a.x + b.x ), ( a.x - b.x ) * ( a.y + b.y ) } );
            }
            for ( u32 k = 0; k < record.cCorners; ++k ) {
                auto &corner = description.corners.pData[record.iFirstCorner + k];
                const i64 uv = face.corners[order[k]].uv;
                corner.attributes.uv0 = uv >= 0 ? uvs[static_cast<usize>( uv )] : WorldUv( normal, description.vertices.pData[corner.iVertex].position, options.worldUnitsPerUv );
            }
            ++report.faces;
        };
        for ( const auto &face : group.faces ) {
            if ( status != OK ) { break; }
            // Flat? Every corner on the plane of the face's Newell normal.
            std::vector<vec3> points;
            for ( const auto &corner : face.corners ) { points.push_back( positions[corner.position] ); }
            std::vector<u32> all( points.size() );
            for ( u32 k = 0; k < all.size(); ++k ) { all[k] = k; }
            const vec3 n = Newell( points, all );
            const f64 length = Length( n );
            if ( length < 1e-12 ) { ++report.skipped; continue; }
            const vec3 unit = Mul( n, 1.0 / length );
            f64 size = 0.0, off = 0.0;
            for ( const vec3 &p : points ) { size = std::max( size, Length( Sub( p, points[0] ) ) ); off = std::max( off, std::fabs( Dot( unit, Sub( p, points[0] ) ) ) ); }
            if ( off <= 1e-7 * std::max( 1.0, size ) ) {
                std::vector<usize> order( face.corners.size() );
                for ( usize k = 0; k < order.size(); ++k ) { order[k] = k; }
                addFace( face, order );
            } else {
                ++report.triangulated;
                for ( usize k = 1; status == OK && k + 1 < face.corners.size(); ++k ) { addFace( face, { 0, k, k + 1 } ); }
            }
        }
        if ( status == OK ) { status = geo::MeshSource_TryBuild( &description, map->pAllocator, &source ); }
        if ( status == OK ) { status = geo::GeometryDocument_TryAddMesh( &map->geometry, &source ); }
        geo::MeshSource_Shutdown( &source );
        geo::MeshSourceDescription_Shutdown( &description );
        result = Status( status );
        if ( result == map_status_t::OK ) { result = Adopt( map, root.value, ids ); }
        if ( result != map_status_t::OK ) { return result; }
        meshesOut.push_back( root.value );
        ++report.meshes;
    }
    if ( pReport != nullptr ) { *pReport = report; }
    return meshesOut.empty() ? map_status_t::INVALID_ARGUMENT : map_status_t::OK;
}

map_status_t MapMesh_PaintStroke( map_document_t *map, u64 meshId, span_t<const vec3> centers, f64 radius, f64 strength, u8 channels, u8 value ) noexcept
{
    if ( !Span_IsValid( centers ) || centers.nCount == 0 || !std::isfinite( radius ) || radius <= 0.0 || !std::isfinite( strength ) ||
         strength <= 0.0 || strength > 1.0 || channels == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    struct job_t { span_t<const vec3> centers; geo::mesh_paint_brush_t brush; } job{ centers, {} };
    job.brush.radius = radius; job.brush.strength = strength; job.brush.channels = channels; job.brush.value = value;
    job.brush.falloff = geo::mesh_paint_falloff_t::SMOOTH;
    return MapMeshEdit_Apply( map, meshId, []( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        auto &stroke = *static_cast<job_t *>( context );
        for ( usize i = 0; i < stroke.centers.nCount; ++i ) {
            stroke.brush.center = stroke.centers.pData[i];
            const auto status = geo::MeshPaint_TryBrush( &working->attributes, &working->mesh, {}, stroke.brush, nullptr );
            if ( status != OK ) { return status; }
        }
        return OK;
    }, &job );
}

} // namespace cypher::editor::map
