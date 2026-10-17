//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_TopologyEdit.cpp
//  Purpose: Implements the mesh component tools and the mirror and solidify
//           object operations over persistent source IDs.
//  Details: Component edits run inside MapMeshEdit_Apply's transaction:
//           geometry lookups use the working copy's description, never the
//           live document, so an edit sees the topology it is changing.
//
//  History:
//  - Created by Karlo Siric on 2026-10-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_TopologyEdit.h"
#include "CypherMap_Wireframe.h"
#include "CypherMap_MeshEdit.h"
#include "CypherMap_BrushEdit.h"
#include "CypherMap_Edit.h"
#include "CypherMap_ObjectCreate.h"

#include "CypherGeometry_BrushAuthoring.h"
#include "CypherGeometry_BrushBoundary.h"
#include "CypherGeometry_BrushVertexOps.h"
#include "CypherGeometry_BrushVertexClump.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentBrushReplacement.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_CsgOperations.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshEditBracket.h"
#include "CypherGeometry_MeshTopologyOps.h"
#include "CypherGeometry_MeshLoopSlide.h"
#include "CypherGeometry_MeshObjectCommands.h"
#include "CypherGeometry_MeshSourceComposition.h"
#include "CypherGeometry_MeshSolidify.h"
#include "CypherGeometry_MeshToBrush.h"
#include "CypherGeometry_UvAlignment.h"
#include "CypherGeometry_MeshSourceBevel.h"
#include "CypherGeometry_MeshSourceBridge.h"
#include "CypherGeometry_MeshSourceComponents.h"
#include "CypherGeometry_MeshSourceMerge.h"
#include "CypherGeometry_MeshSourceModeling.h"
#include "CypherGeometry_MeshSourceSlice.h"
#include "CypherGeometry_MeshUvIslands.h"
#include "CypherGeometry_Repair.h"
#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherGeometry_Patch.h"
#include "CypherGeometry_Planar_Frame.h"
#include "CypherGeometry_Planar_Segment.h"
#include "CypherGeometry_SubdivisionSurface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <unordered_map>

namespace cypher::editor::map
{
using namespace common;
namespace geo = geometry;
namespace
{
using vec3 = math::vec3d_t;
using id_t = geo::geometry_source_id_t;
constexpr auto OK = geo::geometry_status_t::OK;

vec3 Add( vec3 a, vec3 b ) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
vec3 Sub( vec3 a, vec3 b ) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
vec3 Mul( vec3 a, f64 s ) noexcept { return { a.x * s, a.y * s, a.z * s }; }
f64 Dot( vec3 a, vec3 b ) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
vec3 Cross( vec3 a, vec3 b ) noexcept { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
f64 Length( vec3 a ) noexcept { return std::sqrt( Dot( a, a ) ); }
bool Unit( vec3 a, vec3 &out ) noexcept
{
    const f64 length = Length( a );
    if ( !std::isfinite( length ) || length <= 1e-12 ) { return false; }
    out = Mul( a, 1.0 / length );
    return true;
}

map_status_t Status( geo::geometry_status_t status ) noexcept
{
    if ( status == OK ) { return map_status_t::OK; }
    if ( status == geo::geometry_status_t::ALLOCATION_FAILED ) { return map_status_t::OUT_OF_MEMORY; }
    if ( status == geo::geometry_status_t::LIMIT_EXCEEDED || status == geo::geometry_status_t::INSUFFICIENT_CAPACITY ) { return map_status_t::LIMIT_EXCEEDED; }
    return map_status_t::GEOMETRY_FAILED;
}

// Captureless trampoline: MapMeshEdit_Apply takes a C callback so its header
// stays template-free; the lambdas here keep each operation readable.
template <typename Edit>
map_status_t ApplyMesh( map_document_t *map, u64 meshId, Edit edit ) noexcept
{
    return MapMeshEdit_Apply( map, meshId, []( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        return ( *static_cast<Edit *>( context ) )( working, report );
    }, &edit );
}

// The working copy as plain arrays: faces, their corners and positions. Edits
// read their inputs here so they see the topology they are about to change.
struct description_t {
    geo::mesh_source_description_t value{};
    ~description_t() { geo::MeshSourceDescription_Shutdown( &value ); }
    geo::geometry_status_t Read( const geo::mesh_source_t *source ) noexcept
    {
        geo::MeshSourceDescription_Shutdown( &value );
        auto status = geo::MeshSourceDescription_Init( &value, source->mesh.pAllocator, source->sourceId );
        return status == OK ? geo::MeshSource_TryDescribe( source, &value ) : status;
    }
    // Index into vertices by persistent ID; CY_INVALID_INDEX when absent.
    u32 Vertex( id_t id ) const noexcept
    {
        for ( usize i = 0; i < value.vertices.nCount; ++i ) {
            if ( value.vertices.pData[i].sourceId.value == id.value ) { return static_cast<u32>( i ); }
        }
        return CY_INVALID_INDEX;
    }
    u32 CornerVertex( const geo::mesh_source_face_t &face, u32 k ) const noexcept
    {
        return value.corners.pData[face.iFirstCorner + ( k % face.cCorners )].iVertex;
    }
    vec3 Position( u32 vertex ) const noexcept { return value.vertices.pData[vertex].position; }
    // Newell's normal and the corner average of one face.
    void Frame( const geo::mesh_source_face_t &face, vec3 &normal, vec3 &center ) const noexcept
    {
        normal = {}; center = {};
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            const vec3 p = Position( CornerVertex( face, k ) ), q = Position( CornerVertex( face, k + 1 ) );
            normal = Add( normal, { ( p.y - q.y ) * ( p.z + q.z ), ( p.z - q.z ) * ( p.x + q.x ), ( p.x - q.x ) * ( p.y + q.y ) } );
            center = Add( center, p );
        }
        if ( face.cCorners != 0 ) { center = Mul( center, 1.0 / face.cCorners ); }
    }
    // Position of (a, b) in face's loop as consecutive corners; -1 when the
    // face does not use that edge.
    i32 EdgeCorner( const geo::mesh_source_face_t &face, u32 a, u32 b ) const noexcept
    {
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            const u32 p = CornerVertex( face, k ), q = CornerVertex( face, k + 1 );
            if ( ( p == a && q == b ) || ( p == b && q == a ) ) { return static_cast<i32>( k ); }
        }
        return -1;
    }
};

bool HasEdge( const geo::mesh_source_t *source, geo::mesh_edge_ref_t edge ) noexcept
{
    geo::geometry_mesh_edge_handle_t handle{};
    return geo::MeshSourceEdit_TryFindEdge( source, edge.a, edge.b, &handle );
}

bool IsBoundary( const geo::mesh_source_t *source, geo::mesh_edge_ref_t edge ) noexcept
{
    geo::geometry_mesh_edge_handle_t handle{};
    return geo::MeshSourceEdit_TryFindEdge( source, edge.a, edge.b, &handle ) && geo::MeshBoundary_IsBoundaryEdge( &source->mesh, handle );
}

bool VertexPosition( const geo::mesh_source_t *source, id_t id, vec3 &out ) noexcept
{
    geo::geometry_mesh_vertex_handle_t handle{};
    if ( !geo::MeshSource_TryFindVertex( source, id, &handle ) ) { return false; }
    const auto *record = GenerationPool_Get( &source->mesh.vertices, handle );
    if ( record == nullptr ) { return false; }
    out = record->position;
    return true;
}

// Flat (a0, b0, a1, b1, ...) pairs, the layout the source edits take.
std::vector<id_t> Flatten( map_edge_refs_t edges )
{
    std::vector<id_t> flat;
    flat.reserve( edges.nCount * 2 );
    for ( usize i = 0; i < edges.nCount; ++i ) { flat.push_back( edges.pData[i].a ); flat.push_back( edges.pData[i].b ); }
    return flat;
}

bool ValidEdges( map_edge_refs_t edges ) noexcept
{
    if ( !Span_IsValid( edges ) || edges.nCount == 0 ) { return false; }
    for ( usize i = 0; i < edges.nCount; ++i ) {
        if ( edges.pData[i].a.value == 0 || edges.pData[i].b.value == 0 || edges.pData[i].a.value == edges.pData[i].b.value ) { return false; }
    }
    return true;
}

bool ValidVertices( map_vertex_ids_t vertices, usize minimum ) noexcept
{
    if ( !Span_IsValid( vertices ) || vertices.nCount < minimum ) { return false; }
    for ( usize i = 0; i < vertices.nCount; ++i ) {
        if ( vertices.pData[i].value == 0 ) { return false; }
        for ( usize k = 0; k < i; ++k ) { if ( vertices.pData[k].value == vertices.pData[i].value ) { return false; } }
    }
    return true;
}

// Pairs endpoints of two edges so a0 meets its nearer partner: sewing or
// bridging crossed endpoints would twist the result.
void PairByDistance( const geo::mesh_source_t *source, geo::mesh_edge_ref_t first, geo::mesh_edge_ref_t &second ) noexcept
{
    vec3 a0{}, b0{}, a1{}, b1{};
    if ( !VertexPosition( source, first.a, a0 ) || !VertexPosition( source, first.b, b0 ) ||
         !VertexPosition( source, second.a, a1 ) || !VertexPosition( source, second.b, b1 ) ) { return; }
    const f64 straight = Length( Sub( a0, a1 ) ) + Length( Sub( b0, b1 ) );
    const f64 crossed = Length( Sub( a0, b1 ) ) + Length( Sub( b0, a1 ) );
    if ( crossed < straight ) { std::swap( second.a, second.b ); }
}

// Groups of vertex IDs joined by the edges (union-find over the endpoints).
void EdgeGroups( map_edge_refs_t edges, std::vector<id_t> &flat, std::vector<u32> &sizes )
{
    std::vector<u64> ids;
    for ( usize i = 0; i < edges.nCount; ++i ) { ids.push_back( edges.pData[i].a.value ); ids.push_back( edges.pData[i].b.value ); }
    std::sort( ids.begin(), ids.end() ); ids.erase( std::unique( ids.begin(), ids.end() ), ids.end() );
    std::vector<usize> parent( ids.size() );
    for ( usize i = 0; i < parent.size(); ++i ) { parent[i] = i; }
    const auto find = [&]( usize i ) { while ( parent[i] != i ) { parent[i] = parent[parent[i]]; i = parent[i]; } return i; };
    const auto index = [&]( u64 id ) { return static_cast<usize>( std::lower_bound( ids.begin(), ids.end(), id ) - ids.begin() ); };
    for ( usize i = 0; i < edges.nCount; ++i ) {
        const usize a = find( index( edges.pData[i].a.value ) ), b = find( index( edges.pData[i].b.value ) );
        if ( a != b ) { parent[a] = b; }
    }
    for ( usize root = 0; root < ids.size(); ++root ) {
        if ( find( root ) != root ) { continue; }
        u32 count = 0;
        for ( usize i = 0; i < ids.size(); ++i ) {
            if ( find( i ) == root ) { flat.push_back( { ids[i] } ); ++count; }
        }
        sizes.push_back( count );
    }
}

geo::geometry_status_t MergeGroups( geo::mesh_source_t *working, const std::vector<id_t> &flat, const std::vector<u32> &sizes,
    geo::mesh_edit_report_t *report ) noexcept
{
    if ( flat.empty() || sizes.empty() ) { return geo::geometry_status_t::INVALID_ARGUMENT; }
    return geo::MeshSourceEdit_TryMergeVertices( working, { flat.data(), flat.size() }, { sizes.data(), sizes.size() },
        geo::mesh_merge_target_t::CENTER, nullptr, report );
}

f64 Snap( f64 value, f64 grid ) noexcept { return std::round( value / grid ) * grid; }

map_status_t CheckMeshes( const map_document_t *map, span_t<const u64> meshes ) noexcept
{
    if ( map == nullptr || !Span_IsValid( meshes ) || meshes.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    for ( usize i = 0; i < meshes.nCount; ++i ) {
        if ( geo::GeometryDocument_FindMesh( &map->geometry, { meshes.pData[i] } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
        for ( usize k = 0; k < i; ++k ) { if ( meshes.pData[k] == meshes.pData[i] ) { return map_status_t::INVALID_ARGUMENT; } }
    }
    return map_status_t::OK;
}

math::affine3d_t Reflection( math::planed_t plane ) noexcept
{
    const vec3 n = plane.normal;
    const auto column = [&]( vec3 axis, f64 component ) { return Sub( axis, Mul( n, 2.0 * component ) ); };
    return math::Affine3d_FromColumns( column( { 1, 0, 0 }, n.x ), column( { 0, 1, 0 }, n.y ), column( { 0, 0, 1 }, n.z ), Mul( n, -2.0 * plane.d ) );
}
const map_geometry_record_t *Placement( const map_document_t *map, u64 id ) noexcept
{
    for ( usize i = 0; i < map->geometryRecords.nCount; ++i ) { if ( map->geometryRecords.pData[i].id == id ) { return &map->geometryRecords.pData[i]; } }
    return nullptr;
}

u64 FaceRecordId( const key_value_t *face ) noexcept
{
    u64 id = 0;
    return KeyValue_GetU64( KeyValue_Find( face, StringView_FromCString( "id" ) ), &id ) ? id : 0;
}

// A private holder for face records while their objects change: records are
// cloned out before the object that owns them is removed.
struct face_records_t {
    key_value_document_t *document{};
    key_value_t *faces{};
    ~face_records_t() { KeyValue_DestroyDocument( document ); }
    bool Init( const allocator_t *allocator ) noexcept
    {
        key_value_document_desc_t desc{}; desc.pAllocator = allocator;
        document = KeyValue_CreateDocument( desc );
        if ( document == nullptr || !KeyValue_SetRootType( document, key_value_type_t::OBJECT ) ) { return false; }
        faces = KeyValue_ObjectInsert( document, KeyValue_Root( document ), StringView_FromCString( "faces" ), key_value_type_t::ARRAY );
        return faces != nullptr;
    }
};

// Appends records to an object's face list, creating the list when needed.
bool AppendFaces( map_document_t *map, u64 id, const key_value_t *faces ) noexcept
{
    if ( KeyValue_ChildCount( faces ) == 0 ) { return true; }
    map_layer_store_t *layerStore = nullptr;
    auto *record = MapDocument_FindObject( map, id, &layerStore );
    if ( record == nullptr || layerStore == nullptr ) { return false; }
    auto *doc = layerStore->store.pDocument;
    auto *list = KeyValue_Find( record, StringView_FromCString( "faces" ) );
    if ( list == nullptr ) { list = KeyValue_ObjectInsert( doc, record, StringView_FromCString( "faces" ), key_value_type_t::ARRAY ); }
    if ( list == nullptr || KeyValue_Type( list ) != key_value_type_t::ARRAY ) { return false; }
    for ( usize i = 0; i < KeyValue_ChildCount( faces ); ++i ) {
        if ( !KeyValue_CloneInto( doc, list, KeyValue_ChildAt( faces, i ) ) ) { return false; }
    }
    return true;
}
} // namespace

/*
================
Vertices
================
*/

map_status_t MapTopologyEdit_MergeVertices( map_document_t *map, u64 meshId, map_vertex_ids_t vertices, int depthAxis ) noexcept
{
    if ( !ValidVertices( vertices, 2 ) || depthAxis < -1 || depthAxis > 2 ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        struct entry_t { id_t id; f64 depth; };
        vector_t<entry_t> entries{};
        vector_t<id_t> flat{};
        vector_t<u32> sizes{};
        auto *allocator = working->mesh.pAllocator;
        if ( !Vector_Init( &entries, allocator ) || !Vector_Reserve( &entries, vertices.nCount ) ||
             !Vector_Init( &flat, allocator ) || !Vector_Reserve( &flat, vertices.nCount ) ||
             !Vector_Init( &sizes, allocator ) || !Vector_Reserve( &sizes, vertices.nCount ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
        for ( usize i = 0; i < vertices.nCount; ++i ) {
            const auto id = vertices.pData[i];
            vec3 p{};
            if ( !VertexPosition( working, id, p ) ) { return geo::geometry_status_t::INVALID_HANDLE; }
            const f64 depth = depthAxis < 0 ? 0.0 : depthAxis == 0 ? p.x : depthAxis == 1 ? p.y : p.z;
            ( void )Vector_PushBack( &entries, entry_t{ id, depth } );
        }
        if ( depthAxis >= 0 ) {
            std::sort( entries.pData, entries.pData + entries.nCount, []( const entry_t &a, const entry_t &b ) {
                return a.depth != b.depth ? a.depth < b.depth : a.id.value < b.id.value;
            } );
        }
        for ( usize begin = 0; begin < entries.nCount; ) {
            usize end = begin + 1;
            // Compare to the first depth, avoiding transitive epsilon chains.
            while ( end < entries.nCount && std::abs( entries.pData[end].depth - entries.pData[begin].depth ) <= 1e-6 ) { ++end; }
            if ( end - begin >= 2 ) {
                ( void )Vector_PushBack( &sizes, static_cast<u32>( end - begin ) );
                for ( usize i = begin; i < end; ++i ) { ( void )Vector_PushBack( &flat, entries.pData[i].id ); }
            }
            begin = end;
        }
        if ( sizes.nCount == 0 ) { return geo::geometry_status_t::INVALID_ARGUMENT; }
        return geo::MeshSourceEdit_TryMergeVertices( working, { flat.pData, flat.nCount }, { sizes.pData, sizes.nCount },
            geo::mesh_merge_target_t::CENTER, nullptr, report );
    } );
}

map_status_t MapTopologyEdit_WeldVertexPairs( map_document_t *map, u64 meshId, map_vertex_ids_t pairs ) noexcept
{
    if ( !ValidVertices( pairs, 2 ) || pairs.nCount % 2 != 0 ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        vector_t<u32> sizes{};
        if ( !Vector_Init( &sizes, working->mesh.pAllocator ) || !Vector_Resize( &sizes, pairs.nCount / 2 ) ) {
            return geo::geometry_status_t::ALLOCATION_FAILED;
        }
        for ( usize i = 0; i < sizes.nCount; ++i ) { sizes.pData[i] = 2; }
        return geo::MeshSourceEdit_TryMergeVertices( working, pairs, { sizes.pData, sizes.nCount },
            geo::mesh_merge_target_t::FIRST, nullptr, report );
    } );
}

map_status_t MapTopologyEdit_ConnectVertexSet( map_document_t *map, u64 meshId, map_vertex_ids_t vertices ) noexcept
{
    if ( !ValidVertices( vertices, 2 ) ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        description_t mesh;
        auto status = mesh.Read( working );
        if ( status != OK ) { return status; }
        vector_t<u8> selected{};
        struct cut_t { id_t face, a, b; };
        vector_t<cut_t> cuts{};
        if ( !Vector_Init( &selected, working->mesh.pAllocator ) || !Vector_Resize( &selected, mesh.value.vertices.nCount ) ||
             !Vector_Init( &cuts, working->mesh.pAllocator ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
        for ( usize i = 0; i < selected.nCount; ++i ) { selected.pData[i] = 0; }
        for ( usize i = 0; i < vertices.nCount; ++i ) {
            const u32 vertex = mesh.Vertex( vertices.pData[i] );
            if ( vertex == CY_INVALID_INDEX ) { return geo::geometry_status_t::INVALID_HANDLE; }
            selected.pData[vertex] = 1;
        }
        // Plan against the original mesh before the first split. A third
        // selected corner on a later face cannot leave earlier faces cut.
        for ( usize f = 0; f < mesh.value.faces.nCount; ++f ) {
            const auto &face = mesh.value.faces.pData[f];
            u32 pair[2]{}, count = 0;
            for ( u32 k = 0; k < face.cCorners; ++k ) {
                const u32 vertex = mesh.CornerVertex( face, k );
                if ( selected.pData[vertex] == 0 ) { continue; }
                if ( count == 2 ) { return geo::geometry_status_t::INVALID_ARGUMENT; }
                pair[count++] = vertex;
            }
            if ( count != 2 ) { continue; }
            const id_t a = mesh.value.vertices.pData[pair[0]].sourceId, b = mesh.value.vertices.pData[pair[1]].sourceId;
            if ( HasEdge( working, geo::MeshEdgeRef_Make( a, b ) ) ) { continue; }
            if ( !Vector_PushBack( &cuts, cut_t{ face.sourceId, a, b } ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
        }
        if ( cuts.nCount == 0 ) { return geo::geometry_status_t::INVALID_ARGUMENT; }
        // Capture and resolve once for the entire batch. A new face has no
        // persistent ID until this transaction publishes, so starting another
        // identity-addressed edit between cuts would reject that fresh face.
        return geo::MeshEdit_Bracket( working, [&]( vector_t<geo::mesh_edit_face_parent_t> *parents ) noexcept {
            if ( !Vector_Reserve( parents, cuts.nCount ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
            for ( usize i = 0; i < cuts.nCount; ++i ) {
                const auto &cut = cuts.pData[i];
                geo::geometry_mesh_face_handle_t face{};
                geo::geometry_mesh_vertex_handle_t a{}, b{};
                if ( !geo::MeshSource_TryFindFace( working, cut.face, &face ) ||
                     !geo::MeshSource_TryFindVertex( working, cut.a, &a ) || !geo::MeshSource_TryFindVertex( working, cut.b, &b ) ) {
                    return geo::geometry_status_t::INVALID_HANDLE;
                }
                const auto split = geo::MeshOps_SplitFace( &working->mesh, face, a, b );
                if ( split.status != OK ) { return split.status; }
                if ( !geo::GeometryHandle_IsValid( split.hNewFace ) ) { return geo::geometry_status_t::CORRUPT_STATE; }
                ( void )Vector_PushBack( parents, geo::mesh_edit_face_parent_t{ split.hNewFace, face, false } );
            }
            return OK;
        }, report );
    } );
}

map_status_t MapTopologyEdit_ConnectVertices( map_document_t *map, u64 meshId, id_t a, id_t b ) noexcept
{
    const id_t vertices[]{ a, b };
    return MapTopologyEdit_ConnectVertexSet( map, meshId, { vertices, 2 } );
}

map_status_t MapTopologyEdit_SnapVertices( map_document_t *map, u64 meshId, map_vertex_ids_t vertices, f64 grid ) noexcept
{
    if ( !ValidVertices( vertices, 1 ) || !std::isfinite( grid ) || grid <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        for ( usize i = 0; i < vertices.nCount; ++i ) {
            vec3 p{};
            if ( !VertexPosition( working, vertices.pData[i], p ) ) { return geo::geometry_status_t::INVALID_HANDLE; }
            const auto status = geo::MeshSourceEdit_TryMoveVertex( working, vertices.pData[i], { Snap( p.x, grid ), Snap( p.y, grid ), Snap( p.z, grid ) } );
            if ( status != OK ) { return status; }
        }
        return OK;
    } );
}

map_status_t MapTopologyEdit_RadialAlign( map_document_t *map, u64 meshId, map_vertex_ids_t vertices ) noexcept
{
    if ( !ValidVertices( vertices, 3 ) ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        std::vector<vec3> points( vertices.nCount );
        vec3 center{};
        for ( usize i = 0; i < vertices.nCount; ++i ) {
            if ( !VertexPosition( working, vertices.pData[i], points[i] ) ) { return geo::geometry_status_t::INVALID_HANDLE; }
            center = Add( center, points[i] );
        }
        center = Mul( center, 1.0 / static_cast<f64>( points.size() ) );
        // The plane normal: the farthest point against each other, oriented
        // consistently, so the order the user picked the vertices in is irrelevant.
        usize far = 0;
        for ( usize i = 1; i < points.size(); ++i ) {
            if ( Length( Sub( points[i], center ) ) > Length( Sub( points[far], center ) ) ) { far = i; }
        }
        const vec3 spoke = Sub( points[far], center );
        vec3 normal{};
        for ( usize i = 0; i < points.size(); ++i ) {
            vec3 n = Cross( spoke, Sub( points[i], center ) );
            if ( Dot( n, normal ) < 0.0 ) { n = Mul( n, -1.0 ); }
            normal = Add( normal, n );
        }
        if ( !Unit( normal, normal ) ) { return geo::geometry_status_t::DEGENERATE; } // Collinear.
        std::vector<vec3> directions( points.size() );
        f64 radius = 0.0;
        for ( usize i = 0; i < points.size(); ++i ) {
            const vec3 offset = Sub( points[i], center );
            const vec3 inPlane = Sub( offset, Mul( normal, Dot( offset, normal ) ) );
            radius += Length( inPlane );
            if ( !Unit( inPlane, directions[i] ) ) { return geo::geometry_status_t::DEGENERATE; } // A vertex on the axis.
        }
        radius /= static_cast<f64>( points.size() );
        for ( usize i = 0; i < points.size(); ++i ) {
            const auto status = geo::MeshSourceEdit_TryMoveVertex( working, vertices.pData[i], Add( center, Mul( directions[i], radius ) ) );
            if ( status != OK ) { return status; }
        }
        return OK;
    } );
}

map_status_t MapTopologyEdit_WeldUvs( map_document_t *map, u64 meshId, map_vertex_ids_t vertices ) noexcept
{
    if ( !ValidVertices( vertices, 1 ) ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        description_t mesh;
        auto status = mesh.Read( working );
        for ( usize i = 0; status == OK && i < vertices.nCount; ++i ) {
            const u32 vertex = mesh.Vertex( vertices.pData[i] );
            if ( vertex == CY_INVALID_INDEX ) { return geo::geometry_status_t::INVALID_HANDLE; }
            math::vec2d_t sum{}; u32 count = 0;
            for ( usize c = 0; c < mesh.value.corners.nCount; ++c ) {
                if ( mesh.value.corners.pData[c].iVertex != vertex ) { continue; }
                sum.x += mesh.value.corners.pData[c].attributes.uv0.x; sum.y += mesh.value.corners.pData[c].attributes.uv0.y; ++count;
            }
            if ( count < 2 ) { continue; } // A single corner has nothing to weld to.
            const math::vec2d_t average{ sum.x / count, sum.y / count };
            for ( usize f = 0; status == OK && f < mesh.value.faces.nCount; ++f ) {
                const auto &face = mesh.value.faces.pData[f];
                for ( u32 k = 0; status == OK && k < face.cCorners; ++k ) {
                    const auto &corner = mesh.value.corners.pData[face.iFirstCorner + k];
                    if ( corner.iVertex != vertex ) { continue; }
                    auto attributes = corner.attributes; attributes.uv0 = average;
                    status = geo::MeshSourceEdit_TrySetCornerAttributes( working, face.sourceId, vertices.pData[i], attributes );
                }
            }
        }
        return status;
    } );
}

/*
================
Edges
================
*/

map_status_t MapTopologyEdit_CollapseEdges( map_document_t *map, u64 meshId, map_edge_refs_t edges ) noexcept
{
    if ( !ValidEdges( edges ) ) { return map_status_t::INVALID_ARGUMENT; }
    std::vector<id_t> flat; std::vector<u32> sizes;
    EdgeGroups( edges, flat, sizes );
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        return MergeGroups( working, flat, sizes, report );
    } );
}

map_status_t MapTopologyEdit_SewEdges( map_document_t *map, u64 meshId, geo::mesh_edge_ref_t first, geo::mesh_edge_ref_t second ) noexcept
{
    const geo::mesh_edge_ref_t pair[]{ first, second };
    if ( !ValidEdges( { pair, 2 } ) || ( first.a.value == second.a.value && first.b.value == second.b.value ) ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        auto other = second;
        PairByDistance( working, first, other );
        const id_t flat[]{ first.a, first.b, other.a, other.b };
        return geo::MeshSourceEdit_TrySewEdges( working, { flat, 4 }, geo::mesh_merge_target_t::CENTER, nullptr, report );
    } );
}

map_status_t MapTopologyEdit_DissolveEdges( map_document_t *map, u64 meshId, map_edge_refs_t edges ) noexcept
{
    if ( !ValidEdges( edges ) ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        bool any = false;
        for ( usize i = 0; i < edges.nCount; ++i ) {
            if ( !HasEdge( working, edges.pData[i] ) ) { continue; }
            const auto status = geo::MeshSourceEdit_TryDissolveEdge( working, edges.pData[i].a, edges.pData[i].b, report );
            if ( status != OK ) { return status; }
            any = true;
        }
        return any ? OK : geo::geometry_status_t::INVALID_HANDLE;
    } );
}

map_status_t MapTopologyEdit_SplitEdges( map_document_t *map, u64 meshId, map_edge_refs_t edges ) noexcept
{
    if ( !ValidEdges( edges ) ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        for ( usize i = 0; i < edges.nCount; ++i ) {
            const auto status = geo::MeshSourceEdit_TrySplitEdge( working, edges.pData[i].a, edges.pData[i].b, 0.5, nullptr, report );
            if ( status != OK ) { return status; }
        }
        return OK;
    } );
}

map_status_t MapTopologyEdit_ConnectEdges( map_document_t *map, u64 meshId, map_edge_refs_t edges ) noexcept
{
    if ( !ValidEdges( edges ) || edges.nCount < 2 ) { return map_status_t::INVALID_ARGUMENT; }
    const auto *source = map ? geo::GeometryDocument_FindMesh( &map->geometry, { meshId } ) : nullptr;
    if ( source == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    struct edge_t {
        geo::mesh_edge_ref_t ref{};
        geo::geometry_mesh_vertex_handle_t midpoint{};
        id_t midpointId{};
        bool participates{};
    };
    struct cut_t { id_t face; usize a, b; };
    struct pending_t {
        vector_t<edge_t> edges{};
        vector_t<cut_t> cuts{};
        ~pending_t() { Vector_Shutdown( &edges ); Vector_Shutdown( &cuts ); }
    } pending;
    if ( !Vector_Init( &pending.edges, map->pAllocator ) || !Vector_Resize( &pending.edges, edges.nCount ) ||
         !Vector_Init( &pending.cuts, map->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0u; i < edges.nCount; ++i ) {
        pending.edges.pData[i].ref = geo::MeshEdgeRef_Make( edges.pData[i].a, edges.pData[i].b );
    }
    const auto less = []( geo::mesh_edge_ref_t a, geo::mesh_edge_ref_t b ) noexcept {
        return a.a.value < b.a.value || ( a.a.value == b.a.value && a.b.value < b.b.value );
    };
    std::sort( pending.edges.pData, pending.edges.pData + pending.edges.nCount,
        [&]( const edge_t &a, const edge_t &b ) noexcept { return less( a.ref, b.ref ); } );
    for ( usize i = 0u; i < pending.edges.nCount; ++i ) {
        const auto ref = pending.edges.pData[i].ref;
        if ( ( i != 0u && !less( pending.edges.pData[i - 1u].ref, ref ) ) || !HasEdge( source, ref ) ) {
            return map_status_t::INVALID_ARGUMENT;
        }
    }

    // Plan from the original faces. Every input must contribute to at least
    // one unambiguous pair; otherwise even an unused edge would be split.
    {
        description_t mesh;
        const auto read = mesh.Read( source );
        if ( read != OK ) { return Status( read ); }
        if ( !Vector_Reserve( &pending.cuts, mesh.value.faces.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
        for ( usize f = 0u; f < mesh.value.faces.nCount; ++f ) {
            const auto &face = mesh.value.faces.pData[f];
            usize inFace[2]{}, count = 0u;
            for ( u32 k = 0u; k < face.cCorners; ++k ) {
                const auto a = mesh.value.vertices.pData[mesh.CornerVertex( face, k )].sourceId;
                const auto b = mesh.value.vertices.pData[mesh.CornerVertex( face, k + 1u )].sourceId;
                const auto ref = geo::MeshEdgeRef_Make( a, b );
                auto *end = pending.edges.pData + pending.edges.nCount;
                const auto *entry = std::lower_bound( pending.edges.pData, end, ref,
                    [&]( const edge_t &candidate, geo::mesh_edge_ref_t key ) noexcept { return less( candidate.ref, key ); } );
                if ( entry == end || less( ref, entry->ref ) ) { continue; }
                if ( count == 2u ) { return map_status_t::INVALID_ARGUMENT; }
                inFace[count++] = static_cast<usize>( entry - pending.edges.pData );
            }
            if ( count == 2u ) {
                pending.edges.pData[inFace[0]].participates = true;
                pending.edges.pData[inFace[1]].participates = true;
                if ( !Vector_PushBack( &pending.cuts, cut_t{ face.sourceId, inFace[0], inFace[1] } ) ) { return map_status_t::OUT_OF_MEMORY; }
            }
        }
    }
    for ( usize i = 0u; i < pending.edges.nCount; ++i ) {
        if ( !pending.edges.pData[i].participates ) { return map_status_t::INVALID_ARGUMENT; }
    }

    // Resolve the operation's handles after ID assignment, before the split
    // transaction publishes. Position matching confuses coincident shells or
    // old vertices that already occupy a new midpoint's coordinates.
    auto result = MapMeshEdit_Apply( map, meshId,
        []( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
            auto &p = *static_cast<pending_t *>( context );
            for ( usize i = 0u; i < p.edges.nCount; ++i ) {
                auto &edge = p.edges.pData[i];
                const auto status = geo::MeshSourceEdit_TrySplitEdge( working, edge.ref.a, edge.ref.b, 0.5, &edge.midpoint, report );
                if ( status != OK ) { return status; }
            }
            return OK;
        }, &pending,
        []( void *context, const geo::mesh_source_t *working ) noexcept {
            auto &p = *static_cast<pending_t *>( context );
            for ( usize i = 0u; i < p.edges.nCount; ++i ) {
                auto &edge = p.edges.pData[i];
                edge.midpointId = geo::MeshSource_VertexId( working, edge.midpoint );
                if ( edge.midpointId.value == 0u ) { return geo::geometry_status_t::CORRUPT_STATE; }
            }
            return OK;
        } );
    if ( result != map_status_t::OK ) { return result; }
    // A cut's new face has no identity until its transaction commits, and an
    // edit cannot start on a mesh with unidentified faces: one cut each.
    for ( usize i = 0u; i < pending.cuts.nCount; ++i ) {
        const auto &cut = pending.cuts.pData[i];
        result = ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
            return geo::MeshSourceEdit_TrySplitFace( working, cut.face,
                pending.edges.pData[cut.a].midpointId, pending.edges.pData[cut.b].midpointId, report );
        } );
        if ( result != map_status_t::OK ) { return result; }
    }
    return map_status_t::OK;
}

map_status_t MapTopologyEdit_FillHoles( map_document_t *map, u64 meshId, map_edge_refs_t edges ) noexcept
{
    if ( !ValidEdges( edges ) ) { return map_status_t::INVALID_ARGUMENT; }
    // One transaction per hole: a filled hole's face needs its identity
    // before the next edit can start. Several selected edges of one hole
    // are interior once the first fill closes it.
    bool any = false;
    for ( usize i = 0; i < edges.nCount; ++i ) {
        const auto *source = map != nullptr ? geo::GeometryDocument_FindMesh( &map->geometry, { meshId } ) : nullptr;
        if ( source == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
        if ( !IsBoundary( source, edges.pData[i] ) ) { continue; }
        const auto edge = edges.pData[i];
        const auto result = ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
            return geo::MeshSourceEdit_TryFillHole( working, edge.a, edge.b, report );
        } );
        if ( result != map_status_t::OK ) { return result; }
        any = true;
    }
    return any ? map_status_t::OK : map_status_t::INVALID_ARGUMENT;
}

map_status_t MapTopologyEdit_BridgeEdges( map_document_t *map, u64 meshId, geo::mesh_edge_ref_t first, geo::mesh_edge_ref_t second ) noexcept
{
    const geo::mesh_edge_ref_t pair[]{ first, second };
    if ( !ValidEdges( { pair, 2 } ) ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        if ( !IsBoundary( working, first ) || !IsBoundary( working, second ) ) { return geo::geometry_status_t::INVALID_ARGUMENT; }
        auto other = second;
        PairByDistance( working, first, other );
        return geo::MeshSourceEdit_TryBridge( working, first.a, first.b, other.a, other.b, nullptr, report );
    } );
}

map_status_t MapTopologyEdit_BevelEdges( map_document_t *map, u64 meshId, map_edge_refs_t edges, f64 width, u32 cSegments ) noexcept
{
    if ( !ValidEdges( edges ) || !std::isfinite( width ) || width <= 0.0 || cSegments == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    const auto flat = Flatten( edges );
    geo::mesh_bevel_params_t params{}; params.width = width; params.cSegments = cSegments;
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        return geo::MeshSourceEdit_TryBevelEdges( working, { flat.data(), flat.size() }, params, report );
    } );
}

map_status_t MapTopologyEdit_ExtrudeEdges( map_document_t *map, u64 meshId, map_edge_refs_t edges, f64 distance,
    map_edge_extrude_t direction, vector_t<geo::mesh_edge_ref_t> *pOuterEdgesOut ) noexcept
{
    if ( !ValidEdges( edges ) || !std::isfinite( distance ) || distance == 0.0 || direction > map_edge_extrude_t::EXTEND ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    const auto *source = map != nullptr ? geo::GeometryDocument_FindMesh( &map->geometry, { meshId } ) : nullptr;
    if ( source == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    description_t mesh;
    const auto status = mesh.Read( source );
    if ( status != OK ) { return Status( status ); }
    // Each boundary edge has one parent face; average its normal or its
    // outward in-plane direction before invoking the common affine pull.
    vec3 sum{};
    for ( usize i = 0; i < edges.nCount; ++i ) {
        if ( !IsBoundary( source, edges.pData[i] ) ) { return map_status_t::GEOMETRY_FAILED; }
        const u32 a = mesh.Vertex( edges.pData[i].a ), b = mesh.Vertex( edges.pData[i].b );
        for ( usize f = 0; f < mesh.value.faces.nCount; ++f ) {
            const auto &face = mesh.value.faces.pData[f];
            if ( mesh.EdgeCorner( face, a, b ) < 0 ) { continue; }
            vec3 normal{}, center{};
            mesh.Frame( face, normal, center );
            if ( !Unit( normal, normal ) ) { return map_status_t::GEOMETRY_FAILED; }
            if ( direction == map_edge_extrude_t::NORMAL ) { sum = Add( sum, normal ); break; }
            const vec3 pa = mesh.Position( a ), pb = mesh.Position( b );
            vec3 outward = Cross( Sub( pb, pa ), normal );
            if ( Dot( outward, Sub( Mul( Add( pa, pb ), 0.5 ), center ) ) < 0.0 ) { outward = Mul( outward, -1.0 ); }
            if ( !Unit( outward, outward ) ) { return map_status_t::GEOMETRY_FAILED; }
            sum = Add( sum, outward );
            break;
        }
    }
    vec3 unit{};
    if ( !Unit( sum, unit ) ) { return map_status_t::GEOMETRY_FAILED; }
    return MapTopologyEdit_ExtendEdgesBy( map, meshId, edges, Mul( unit, distance ), pOuterEdgesOut );
}

map_status_t MapTopologyEdit_SetEdgeNormals( map_document_t *map, u64 meshId, map_edge_refs_t edges, map_edge_normals_t normals ) noexcept
{
    if ( !ValidEdges( edges ) || normals > map_edge_normals_t::DEFAULT ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        description_t mesh;
        auto status = mesh.Read( working );
        for ( usize i = 0; status == OK && i < edges.nCount; ++i ) {
            const auto edge = edges.pData[i];
            geo::geometry_mesh_edge_handle_t handle{};
            if ( !geo::MeshSourceEdit_TryFindEdge( working, edge.a, edge.b, &handle ) ) { return geo::geometry_status_t::INVALID_HANDLE; }
            auto attributes = geo::MeshAttributeStore_GetEdge( &working->attributes, handle );
            const auto *record = GenerationPool_Get( &working->mesh.edges, handle );
            const f64 crease = record != nullptr ? record->creaseWeight : 0.0;
            attributes.flags = normals == map_edge_normals_t::HARD ? static_cast<u8>( attributes.flags | geo::MESH_EDGE_FLAG_HARD )
                                                                   : static_cast<u8>( attributes.flags & ~geo::MESH_EDGE_FLAG_HARD );
            status = geo::MeshSourceEdit_TrySetEdgeAttributes( working, edge.a, edge.b, attributes, crease );
            if ( status != OK || normals != map_edge_normals_t::SOFT ) { continue; }
            // A non-hard edge shades smoothly only where the two faces share
            // a smoothing group; give them one if they have none in common.
            const u32 a = mesh.Vertex( edge.a ), b = mesh.Vertex( edge.b );
            const geo::mesh_source_face_t *sides[2]{};
            u32 found = 0;
            for ( usize f = 0; f < mesh.value.faces.nCount && found < 2; ++f ) {
                if ( mesh.EdgeCorner( mesh.value.faces.pData[f], a, b ) >= 0 ) { sides[found++] = &mesh.value.faces.pData[f]; }
            }
            if ( found < 2 || ( sides[0]->attributes.smoothingGroups & sides[1]->attributes.smoothingGroups ) != 0 ) { continue; }
            for ( const auto *side : sides ) {
                auto surface = side->attributes; surface.smoothingGroups |= 1u;
                status = geo::MeshSourceEdit_TrySetFaceAttributes( working, side->sourceId, surface );
                if ( status != OK ) { break; }
            }
            if ( status == OK ) { status = mesh.Read( working ); } // Later edges see the new groups.
        }
        return status;
    } );
}

map_status_t MapTopologyEdit_MoveVertices( map_document_t *map, u64 meshId, map_vertex_ids_t vertices, vec3 offset ) noexcept
{
    if ( !ValidVertices( vertices, 1 ) || !std::isfinite( offset.x ) || !std::isfinite( offset.y ) || !std::isfinite( offset.z ) ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        for ( usize i = 0; i < vertices.nCount; ++i ) {
            vec3 p{};
            if ( !VertexPosition( working, vertices.pData[i], p ) ) { return geo::geometry_status_t::INVALID_HANDLE; }
            const auto status = geo::MeshSourceEdit_TryMoveVertex( working, vertices.pData[i], Add( p, offset ) );
            if ( status != OK ) { return status; }
        }
        return OK;
    } );
}

map_status_t MapTopologyEdit_TransformVertices( map_document_t *map, u64 meshId, map_vertex_ids_t vertices, const math::affine3d_t &transform ) noexcept
{
    if ( !ValidVertices( vertices, 1 ) || !math::Affine3d_IsFinite( transform ) ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        // Read every position first: moving one vertex must not change
        // where the next is read from.
        std::vector<vec3> targets( vertices.nCount );
        for ( usize i = 0; i < vertices.nCount; ++i ) {
            vec3 p{};
            if ( !VertexPosition( working, vertices.pData[i], p ) ) { return geo::geometry_status_t::INVALID_HANDLE; }
            targets[i] = math::Affine3d_TransformPoint( transform, p );
        }
        for ( usize i = 0; i < vertices.nCount; ++i ) {
            const auto status = geo::MeshSourceEdit_TryMoveVertex( working, vertices.pData[i], targets[i] );
            if ( status != OK ) { return status; }
        }
        return OK;
    } );
}

map_status_t MapTopologyEdit_ExtrudeFaceBy( map_document_t *map, u64 meshId, u64 faceId, vec3 offset ) noexcept
{
    if ( faceId == 0 || !std::isfinite( offset.x ) || !std::isfinite( offset.y ) || !std::isfinite( offset.z ) ) { return map_status_t::INVALID_ARGUMENT; }
    const auto *source = map != nullptr ? geo::GeometryDocument_FindMesh( &map->geometry, { meshId } ) : nullptr;
    if ( source == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    description_t before;
    if ( before.Read( source ) != OK ) { return map_status_t::GEOMETRY_FAILED; }
    vec3 normal{}, center{};
    bool found = false;
    for ( usize f = 0; f < before.value.faces.nCount && !found; ++f ) {
        if ( before.value.faces.pData[f].sourceId.value != faceId ) { continue; }
        before.Frame( before.value.faces.pData[f], normal, center );
        found = Unit( normal, normal );
    }
    if ( !found ) { return map_status_t::UNKNOWN_OBJECT; }
    const f64 along = Dot( offset, normal );
    vec3 rest = offset;
    if ( along > 1e-9 ) {
        const auto result = MapMeshEdit_ExtrudeFace( map, meshId, faceId, along );
        if ( result != map_status_t::OK ) { return result; }
        rest = Sub( offset, Mul( normal, along ) );
    }
    if ( Length( rest ) <= 1e-9 ) { return map_status_t::OK; }
    // The cap keeps the face's ID; move its corners by what is left.
    description_t after;
    if ( after.Read( geo::GeometryDocument_FindMesh( &map->geometry, { meshId } ) ) != OK ) { return map_status_t::GEOMETRY_FAILED; }
    std::vector<id_t> corners;
    for ( usize f = 0; f < after.value.faces.nCount; ++f ) {
        const auto &face = after.value.faces.pData[f];
        if ( face.sourceId.value != faceId ) { continue; }
        for ( u32 k = 0; k < face.cCorners; ++k ) { corners.push_back( after.value.vertices.pData[after.CornerVertex( face, k )].sourceId ); }
    }
    return MapTopologyEdit_MoveVertices( map, meshId, { corners.data(), corners.size() }, rest );
}

namespace
{
// Shared read-only preparation for connected coplanar face-region operations.
// Canonical description corner order is matched to actual half-edge origins;
// source IDs and raw loop starts are deliberately not interchangeable.
struct face_region_t {
    struct face_t { u32 description{}, first{}, count{}; };
    struct corner_t {
        geo::geometry_mesh_vertex_handle_t vertex{};
        geo::mesh_corner_attributes_t attributes{};
        geo::mesh_edge_attributes_t edgeAttributes{};
        f64 crease{};
        u32 translated{ CY_INVALID_INDEX }, adjacent{ CY_INVALID_INDEX };
    };
    description_t before;
    vector_t<face_t> faces{};
    vector_t<corner_t> corners{};
    vector_t<geo::geometry_mesh_face_handle_t> removed{};
    vec3 normal{}, origin{};
    usize boundaryCount{};
};

geo::geometry_status_t ReadFaceRegion( const geo::mesh_source_t *working, span_t<const u64> faceIds,
    const geo::geometry_numerical_policy_t &policy, face_region_t &region, usize maximumCorners = CY_USIZE_MAX ) noexcept
{
    auto &before = region.before;
    auto &faces = region.faces;
    auto &corners = region.corners;
    auto &removed = region.removed;
    auto status = before.Read( working );
    if ( status != OK ) { return status; }
    if ( faceIds.nCount == 0u || faceIds.nCount > before.value.faces.nCount ) { return geo::geometry_status_t::INVALID_ARGUMENT; }
    struct face_lookup_t { u64 id{}; geo::geometry_mesh_face_handle_t handle{}; };
    vector_t<face_lookup_t> lookup{};
    vector_t<u32> slots{}, queue{};
    vector_t<u8> visited{};
    const auto *allocator = working->mesh.pAllocator;
    if ( !Vector_Init( &faces, allocator ) || !Vector_Resize( &faces, faceIds.nCount ) ||
         !Vector_Init( &removed, allocator ) || !Vector_Resize( &removed, faceIds.nCount ) ||
         !Vector_Init( &corners, allocator ) ||
         !Vector_Init( &lookup, allocator ) || !Vector_Reserve( &lookup, before.value.faces.nCount ) ||
         !Vector_Init( &slots, allocator ) || !Vector_Resize( &slots, working->mesh.faces.cSlots ) ||
         !Vector_Init( &visited, allocator ) || !Vector_Resize( &visited, faceIds.nCount ) ||
         !Vector_Init( &queue, allocator ) || !Vector_Reserve( &queue, faceIds.nCount ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
    std::fill_n( slots.pData, slots.nCount, CY_INVALID_INDEX );
    ( void )GenerationPool_ForEach( &working->mesh.faces,
        [&]( geo::geometry_mesh_face_handle_t handle, const geo::mesh_face_record_t & ) noexcept -> bool_t {
            const u64 id = geo::MeshSource_FaceId( working, handle ).value;
            if ( id == 0u ) { status = geo::geometry_status_t::CORRUPT_STATE; return false; }
            if ( !Vector_PushBack( &lookup, face_lookup_t{ id, handle } ) ) { status = geo::geometry_status_t::ALLOCATION_FAILED; return false; }
            return true;
        } );
    if ( status != OK ) { return status; }
    if ( lookup.nCount != before.value.faces.nCount ) { return geo::geometry_status_t::CORRUPT_STATE; }
    std::sort( lookup.pData, lookup.pData + lookup.nCount,
        []( const face_lookup_t &a, const face_lookup_t &b ) noexcept { return a.id < b.id; } );
    for ( usize i = 1u; i < lookup.nCount; ++i ) {
        if ( lookup.pData[i - 1u].id == lookup.pData[i].id ) { return geo::geometry_status_t::CORRUPT_STATE; }
    }
    usize cornerCount = 0u;
    for ( usize i = 0u; i < faceIds.nCount; ++i ) {
        const u64 id = faceIds.pData[i];
        const auto *begin = lookup.pData;
        const auto *end = begin + lookup.nCount;
        const auto *found = std::lower_bound( begin, end, id,
            []( const face_lookup_t &face, u64 value ) noexcept { return face.id < value; } );
        if ( id == 0u || found == end || found->id != id ) { return geo::geometry_status_t::INVALID_ARGUMENT; }
        const auto handle = found->handle;
        if ( handle.nSlot >= slots.nCount ) { return geo::geometry_status_t::CORRUPT_STATE; }
        if ( slots.pData[handle.nSlot] != CY_INVALID_INDEX ) { return geo::geometry_status_t::INVALID_ARGUMENT; }
        removed.pData[i] = handle; slots.pData[handle.nSlot] = static_cast<u32>( i );
        const auto *descriptionBegin = before.value.faces.pData;
        const auto *descriptionEnd = descriptionBegin + before.value.faces.nCount;
        const auto *face = std::lower_bound( descriptionBegin, descriptionEnd, id,
            []( const geo::mesh_source_face_t &f, u64 value ) noexcept { return f.sourceId.value < value; } );
        if ( face == descriptionEnd || face->sourceId.value != id ) { return geo::geometry_status_t::CORRUPT_STATE; }
        vec3 normal{}, center{};
        before.Frame( *face, normal, center );
        if ( !Unit( normal, normal ) ) { return geo::geometry_status_t::INVALID_ARGUMENT; }
        if ( i == 0u ) { region.normal = normal; region.origin = before.Position( before.CornerVertex( *face, 0u ) ); }
        if ( Dot( normal, region.normal ) <= 0.0 || Length( Cross( normal, region.normal ) ) > policy.fUnitNormalTolerance ) {
            return geo::geometry_status_t::INVALID_ARGUMENT;
        }
        for ( u32 k = 0u; k < face->cCorners; ++k ) {
            const vec3 p = before.Position( before.CornerVertex( *face, k ) );
            const f64 scale = std::max( { 1.0, std::abs( p.x ), std::abs( p.y ), std::abs( p.z ), Length( region.origin ) } );
            if ( std::abs( Dot( Sub( p, region.origin ), region.normal ) ) > policy.fCoplanarDistanceTolerance + policy.fRelativeDistanceTolerance * scale ) {
                return geo::geometry_status_t::INVALID_ARGUMENT;
            }
        }
        faces.pData[i] = { static_cast<u32>( face - before.value.faces.pData ), static_cast<u32>( cornerCount ), face->cCorners };
        cornerCount += face->cCorners;
        if ( cornerCount > maximumCorners ) { return geo::geometry_status_t::LIMIT_EXCEEDED; }
    }
    if ( !Vector_Resize( &corners, cornerCount ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0u; i < faces.nCount; ++i ) {
        const auto &plan = faces.pData[i];
        const auto &face = before.value.faces.pData[plan.description];
        const auto *record = GenerationPool_Get( &working->mesh.faces, removed.pData[i] );
        const auto *loop = record != nullptr ? GenerationPool_Get( &working->mesh.loops, record->hOuterLoop ) : nullptr;
        if ( loop == nullptr || loop->cHalfEdges != plan.count ) { return geo::geometry_status_t::CORRUPT_STATE; }
        for ( u32 k = 0u; k < plan.count; ++k ) {
            auto &corner = corners.pData[plan.first + k];
            corner.attributes = before.value.corners.pData[face.iFirstCorner + k].attributes;
            const auto id = before.value.vertices.pData[before.CornerVertex( face, k )].sourceId;
            auto h = loop->hFirstHalfEdge;
            const geo::mesh_half_edge_record_t *edge = nullptr;
            for ( u32 n = 0u; n < plan.count; ++n ) {
                const auto *candidate = GenerationPool_Get( &working->mesh.halfEdges, h );
                if ( candidate == nullptr ) { return geo::geometry_status_t::CORRUPT_STATE; }
                if ( geo::MeshSource_VertexId( working, candidate->hOrigin ).value == id.value ) { corner.vertex = candidate->hOrigin; edge = candidate; break; }
                h = candidate->hNext;
            }
            const auto *edgeRecord = edge != nullptr ? GenerationPool_Get( &working->mesh.edges, edge->hEdge ) : nullptr;
            if ( edgeRecord == nullptr ) { return geo::geometry_status_t::CORRUPT_STATE; }
            corner.edgeAttributes = geo::MeshAttributeStore_GetEdge( &working->attributes, edge->hEdge );
            corner.crease = edgeRecord->creaseWeight;
            if ( geo::GeometryHandle_IsValid( edge->hTwin ) ) {
                const auto *twin = GenerationPool_Get( &working->mesh.halfEdges, edge->hTwin );
                const auto *other = twin != nullptr ? GenerationPool_Get( &working->mesh.loops, twin->hLoop ) : nullptr;
                if ( other == nullptr || other->hFace.nSlot >= slots.nCount ) { return geo::geometry_status_t::CORRUPT_STATE; }
                corner.adjacent = slots.pData[other->hFace.nSlot];
            }
            if ( corner.adjacent == CY_INVALID_INDEX ) { ++region.boundaryCount; }
        }
    }
    visited.pData[0] = 1u; ( void )Vector_PushBack( &queue, 0u );
    for ( usize q = 0u; q < queue.nCount; ++q ) {
        const auto &face = faces.pData[queue.pData[q]];
        for ( u32 k = 0u; k < face.count; ++k ) {
            const u32 adjacent = corners.pData[face.first + k].adjacent;
            if ( adjacent == CY_INVALID_INDEX || visited.pData[adjacent] != 0u ) { continue; }
            visited.pData[adjacent] = 1u; ( void )Vector_PushBack( &queue, adjacent );
        }
    }
    return queue.nCount == faces.nCount && region.boundaryCount != 0u ? OK : geo::geometry_status_t::INVALID_ARGUMENT;
}
} // namespace

map_status_t MapTopologyEdit_ExtrudeFaceRegionBy( map_document_t *map, u64 meshId, span_t<const u64> faceIds, f64 distance ) noexcept
{
    if ( map == nullptr || meshId == 0u || !Span_IsValid( faceIds ) || faceIds.nCount < 2u ||
         !std::isfinite( distance ) || distance <= map->geometryPolicy.numerical.fMinimumEdgeLength ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( faceIds.nCount > geo::kMeshSourceFacesMax ) { return map_status_t::LIMIT_EXCEEDED; }
    // Domain refusals are distinct from a damaged mesh or allocation failure.
    // All planning below reads the transaction's private source.
    bool invalidRegion = false;
    const auto result = ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        const auto reject = [&]() noexcept { invalidRegion = true; return geo::geometry_status_t::INVALID_ARGUMENT; };
        face_region_t region;
        const auto &policy = map->geometryPolicy.numerical;
        auto status = ReadFaceRegion( working, faceIds, policy, region );
        if ( status == geo::geometry_status_t::INVALID_ARGUMENT ) { return reject(); }
        if ( status != OK ) { return status; }
        auto &before = region.before;
        auto &faces = region.faces;
        auto &corners = region.corners;
        auto &removed = region.removed;
        const usize cornerCount = corners.nCount, boundaryCount = region.boundaryCount;
        struct wall_t { u32 parent{}; std::array<geo::mesh_corner_attributes_t, 4> attributes{}; };
        vector_t<u32> vertexCopies{}, sizes{};
        vector_t<geo::geometry_mesh_face_handle_t> created{};
        vector_t<wall_t> walls{};
        vector_t<vec3> positions{};
        vector_t<geo::mesh_boundary_corner_t> rings{};
        const auto *allocator = working->mesh.pAllocator;
        if ( !Vector_Init( &vertexCopies, allocator ) || !Vector_Resize( &vertexCopies, before.value.vertices.nCount ) ||
             !Vector_Init( &sizes, allocator ) || !Vector_Init( &created, allocator ) || !Vector_Init( &walls, allocator ) ||
             !Vector_Init( &positions, allocator ) || !Vector_Init( &rings, allocator ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
        std::fill_n( vertexCopies.pData, vertexCopies.nCount, CY_INVALID_INDEX );
        const vec3 offset = Mul( region.normal, distance );
        for ( usize i = 0u; i < faces.nCount; ++i ) {
            const auto &plan = faces.pData[i];
            const auto &face = before.value.faces.pData[plan.description];
            for ( u32 k = 0u; k < plan.count; ++k ) {
                const u32 vertex = before.CornerVertex( face, k );
                if ( vertexCopies.pData[vertex] == CY_INVALID_INDEX ) {
                    const vec3 p = Add( before.Position( vertex ), offset );
                    const f64 limit = std::min( policy.fCoordinateMagnitudeLimit, geo::kMeshSourceCoordinateMax );
                    if ( !std::isfinite( p.x ) || !std::isfinite( p.y ) || !std::isfinite( p.z ) ||
                         std::abs( p.x ) > limit || std::abs( p.y ) > limit || std::abs( p.z ) > limit ) { return reject(); }
                    vertexCopies.pData[vertex] = static_cast<u32>( positions.nCount );
                    if ( !Vector_PushBack( &positions, p ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
                }
                corners.pData[plan.first + k].translated = vertexCopies.pData[vertex];
            }
        }
        // Every count derives from a validated source, so these sums fit usize;
        // reject canonical source limits before preparing replacement scratch.
        if ( before.value.faces.nCount + boundaryCount > geo::kMeshSourceFacesMax ||
             before.value.corners.nCount + 4u * boundaryCount > geo::kMeshSourceCornersMax ) {
            return geo::geometry_status_t::LIMIT_EXCEEDED;
        }
        if ( !Vector_Reserve( &sizes, faces.nCount + boundaryCount ) ||
             !Vector_Reserve( &rings, cornerCount + 4u * boundaryCount ) ||
             !Vector_Reserve( &walls, boundaryCount ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
        for ( usize i = 0u; i < faces.nCount; ++i ) {
            const auto &face = faces.pData[i];
            ( void )Vector_PushBack( &sizes, face.count );
            for ( u32 k = 0u; k < face.count; ++k ) {
                ( void )Vector_PushBack( &rings, geo::mesh_boundary_corner_t{ {}, corners.pData[face.first + k].translated } );
            }
        }
        for ( usize i = 0u; i < faces.nCount; ++i ) {
            const auto &face = faces.pData[i];
            for ( u32 k = 0u; k < face.count; ++k ) {
                const auto &a = corners.pData[face.first + k];
                if ( a.adjacent != CY_INVALID_INDEX ) { continue; }
                const auto &b = corners.pData[face.first + ( k + 1u ) % face.count];
                const auto *pa = GenerationPool_Get( &working->mesh.vertices, a.vertex );
                const auto *pb = GenerationPool_Get( &working->mesh.vertices, b.vertex );
                if ( pa == nullptr || pb == nullptr ) { return geo::geometry_status_t::CORRUPT_STATE; }
                const f64 edgeLength = Length( Sub( pb->position, pa->position ) );
                if ( !std::isfinite( edgeLength ) || edgeLength <= policy.fMinimumEdgeLength ) { return reject(); }
                // Each wall gets a local rectangular chart, rather than a
                // projection onto the cap plane that collapses its height.
                // Preserve source-edge density when present; zero-length UV
                // edges use the editor's 128 world-unit repeat convention.
                const auto chart = [&]( math::vec2d_t u, math::vec2d_t v, math::vec2d_t ( &out )[4] ) noexcept {
                    f64 dx = v.x - u.x, dy = v.y - u.y;
                    if ( !std::isfinite( dx ) || !std::isfinite( dy ) ) { return false; }
                    if ( std::hypot( dx, dy ) <= 1e-12 ) {
                        u = {}; v = { edgeLength / 128.0, 0.0 }; dx = v.x; dy = 0.0;
                    }
                    const f64 scale = distance / edgeLength;
                    const math::vec2d_t up{ -dy * scale, dx * scale };
                    out[0] = u; out[1] = v;
                    out[2] = { v.x + up.x, v.y + up.y }; out[3] = { u.x + up.x, u.y + up.y };
                    for ( const auto p : out ) { if ( !std::isfinite( p.x ) || !std::isfinite( p.y ) ) { return false; } }
                    const f64 length = std::hypot( dx, dy );
                    const f64 height = -( dy / length ) * ( out[3].x - out[0].x ) + ( dx / length ) * ( out[3].y - out[0].y );
                    // Very large source offsets can round away a small added
                    // height. Refuse that chart instead of publishing zero area.
                    return std::isfinite( length ) && std::isfinite( height ) && height > 0.0;
                };
                math::vec2d_t uv0[4]{}, uv1[4]{};
                if ( !chart( a.attributes.uv0, b.attributes.uv0, uv0 ) || !chart( a.attributes.uv1, b.attributes.uv1, uv1 ) ) {
                    return geo::geometry_status_t::NUMERIC_FAILURE;
                }
                wall_t wall{}; wall.parent = static_cast<u32>( i );
                for ( u32 j = 0u; j < 4u; ++j ) {
                    wall.attributes[j] = j == 0u || j == 3u ? a.attributes : b.attributes;
                    wall.attributes[j].uv0 = uv0[j]; wall.attributes[j].uv1 = uv1[j];
                }
                ( void )Vector_PushBack( &sizes, 4u );
                ( void )Vector_PushBack( &walls, wall );
                ( void )Vector_PushBack( &rings, geo::mesh_boundary_corner_t{ a.vertex } );
                ( void )Vector_PushBack( &rings, geo::mesh_boundary_corner_t{ b.vertex } );
                ( void )Vector_PushBack( &rings, geo::mesh_boundary_corner_t{ {}, b.translated } );
                ( void )Vector_PushBack( &rings, geo::mesh_boundary_corner_t{ {}, a.translated } );
            }
        }
        status = geo::MeshEdit_Bracket( working, [&]( vector_t<geo::mesh_edit_face_parent_t> *parents ) noexcept {
            if ( !Vector_Reserve( parents, sizes.nCount ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
            const auto replaced = geo::MeshBoundary_ReplaceFaces( &working->mesh, { removed.pData, removed.nCount },
                { rings.pData, rings.nCount }, { sizes.pData, sizes.nCount }, { positions.pData, positions.nCount }, &created );
            if ( replaced.status != OK ) { return replaced.status; }
            for ( usize i = 0u; i < created.nCount; ++i ) {
                const usize parent = i < faces.nCount ? i : walls.pData[i - faces.nCount].parent;
                ( void )Vector_PushBack( parents, geo::mesh_edit_face_parent_t{ created.pData[i], removed.pData[parent], i < faces.nCount } );
            }
            return OK;
        }, report );
        if ( status != OK ) { return status; }
        // Generic transfer projects new corners and defaults new edges. The
        // translated cap has exact authored correspondences, including UV seams.
        for ( usize i = 0u; i < faces.nCount; ++i ) {
            const auto *face = GenerationPool_Get( &working->mesh.faces, created.pData[i] );
            const auto *loop = face != nullptr ? GenerationPool_Get( &working->mesh.loops, face->hOuterLoop ) : nullptr;
            if ( loop == nullptr || loop->cHalfEdges != faces.pData[i].count ) { return geo::geometry_status_t::CORRUPT_STATE; }
            auto h = loop->hFirstHalfEdge;
            for ( u32 k = 0u; k < loop->cHalfEdges; ++k ) {
                const auto &old = corners.pData[faces.pData[i].first + k];
                const auto *corner = GenerationPool_Get( &working->mesh.halfEdges, h );
                auto *edge = corner != nullptr ? GenerationPool_Get( &working->mesh.edges, corner->hEdge ) : nullptr;
                if ( edge == nullptr ) { return geo::geometry_status_t::CORRUPT_STATE; }
                status = geo::MeshAttributeStore_TrySetCorner( &working->attributes, h, old.attributes, working->mesh.halfEdges.cSlots );
                if ( status != OK ) { return status; }
                status = geo::MeshAttributeStore_TrySetEdge( &working->attributes, corner->hEdge, old.edgeAttributes, working->mesh.edges.cSlots );
                if ( status != OK ) { return status; }
                edge->creaseWeight = old.crease;
                h = corner->hNext;
            }
        }
        for ( usize i = 0u; i < walls.nCount; ++i ) {
            const auto *face = GenerationPool_Get( &working->mesh.faces, created.pData[faces.nCount + i] );
            const auto *loop = face != nullptr ? GenerationPool_Get( &working->mesh.loops, face->hOuterLoop ) : nullptr;
            if ( loop == nullptr || loop->cHalfEdges != 4u ) { return geo::geometry_status_t::CORRUPT_STATE; }
            auto h = loop->hFirstHalfEdge;
            for ( u32 k = 0u; k < 4u; ++k ) {
                const auto *corner = GenerationPool_Get( &working->mesh.halfEdges, h );
                if ( corner == nullptr ) { return geo::geometry_status_t::CORRUPT_STATE; }
                status = geo::MeshAttributeStore_TrySetCorner( &working->attributes, h, walls.pData[i].attributes[k], working->mesh.halfEdges.cSlots );
                if ( status != OK ) { return status; }
                h = corner->hNext;
            }
        }
        return OK;
    } );
    return invalidRegion ? map_status_t::INVALID_ARGUMENT : result;
}

namespace
{
using inset_point_t = math::vec2d_t;
inset_point_t InsetSub( inset_point_t a, inset_point_t b ) noexcept { return { a.x - b.x, a.y - b.y }; }
f64 InsetDot( inset_point_t a, inset_point_t b ) noexcept { return a.x * b.x + a.y * b.y; }
f64 InsetCross( inset_point_t a, inset_point_t b ) noexcept { return a.x * b.y - a.y * b.x; }

// Exact segment classification refuses bow-ties and nonadjacent contacts;
// area/length policy separately rejects a geometrically collapsed cap cell.
bool InsetRingValid( span_t<const inset_point_t> points, const geo::geometry_numerical_policy_t &policy ) noexcept
{
    if ( points.nCount < 3u ) { return false; }
    f64 area = 0.0;
    for ( usize i = 0u; i < points.nCount; ++i ) {
        const auto a = points.pData[i], b = points.pData[( i + 1u ) % points.nCount];
        const auto c = points.pData[( i + 2u ) % points.nCount];
        if ( !std::isfinite( a.x ) || !std::isfinite( a.y ) ||
             std::hypot( b.x - a.x, b.y - a.y ) <= policy.fMinimumEdgeLength ) { return false; }
        if ( geo::Planar_ClassifySegments( a, b, b, c ) != geo::planar_segment_relation_t::TOUCH_ENDPOINT ) { return false; }
        area += InsetCross( InsetSub( a, points.pData[0] ), InsetSub( b, points.pData[0] ) );
        for ( usize j = i + 2u; j < points.nCount; ++j ) {
            if ( i == 0u && j + 1u == points.nCount ) { continue; }
            if ( geo::Planar_ClassifySegments( a, b, points.pData[j], points.pData[( j + 1u ) % points.nCount] ) !=
                 geo::planar_segment_relation_t::DISJOINT ) { return false; }
        }
    }
    return std::isfinite( area ) && area > 2.0 * policy.fMinimumFaceArea;
}
} // namespace

map_status_t MapTopologyEdit_InsetFaceRegionByEdgeWidth( map_document_t *map, u64 meshId, span_t<const u64> faceIds, f64 width ) noexcept
{
    if ( map == nullptr || meshId == 0u || !Span_IsValid( faceIds ) || faceIds.nCount == 0u ||
         !std::isfinite( width ) || width <= map->geometryPolicy.numerical.fMinimumEdgeLength ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( faceIds.nCount == 1u ) { return MapMeshEdit_InsetFaceByEdgeWidth( map, meshId, faceIds.pData[0], width ); }
    // Operation limits bound the quadratic geometric checks, not the file format.
    constexpr usize maximumCorners = 16384u, maximumPerimeter = 1024u;
    if ( faceIds.nCount > maximumCorners / 3u ) { return map_status_t::LIMIT_EXCEEDED; }
    bool invalidRegion = false;
    const auto result = ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        const auto reject = [&]() noexcept { invalidRegion = true; return geo::geometry_status_t::INVALID_ARGUMENT; };
        const auto &policy = map->geometryPolicy.numerical;
        const auto *allocator = working->mesh.pAllocator;
        face_region_t region;
        auto status = ReadFaceRegion( working, faceIds, policy, region, maximumCorners );
        if ( status == geo::geometry_status_t::INVALID_ARGUMENT ) { return reject(); }
        if ( status != OK ) { return status; }
        if ( region.boundaryCount > maximumPerimeter ) { return geo::geometry_status_t::LIMIT_EXCEEDED; }
        auto &before = region.before;
        struct boundary_t { u32 corner{}, parent{}; geo::geometry_mesh_vertex_handle_t a{}, b{}; };
        vector_t<boundary_t> boundary{};
        vector_t<u32> outgoing{}, incoming{}, ordered{}, turns{}, insetBySlot{}, sizes{};
        vector_t<u8> visited{};
        vector_t<inset_point_t> points{}, planned{}, directions{}, normals{}, polygon{};
        vector_t<vec3> positions{};
        vector_t<geo::mesh_boundary_corner_t> rings{};
        vector_t<geo::geometry_mesh_face_handle_t> created{};
        const usize count = region.boundaryCount;
        if ( !Vector_Init( &boundary, allocator ) || !Vector_Reserve( &boundary, count ) ||
             !Vector_Init( &outgoing, allocator ) || !Vector_Resize( &outgoing, working->mesh.vertices.cSlots ) ||
             !Vector_Init( &incoming, allocator ) || !Vector_Resize( &incoming, working->mesh.vertices.cSlots ) ||
             !Vector_Init( &insetBySlot, allocator ) || !Vector_Resize( &insetBySlot, working->mesh.vertices.cSlots ) ||
             !Vector_Init( &ordered, allocator ) || !Vector_Reserve( &ordered, count ) ||
             !Vector_Init( &turns, allocator ) || !Vector_Reserve( &turns, count ) ||
             !Vector_Init( &visited, allocator ) || !Vector_Resize( &visited, count ) ||
             !Vector_Init( &points, allocator ) || !Vector_Resize( &points, count ) ||
             !Vector_Init( &planned, allocator ) || !Vector_Resize( &planned, count ) ||
             !Vector_Init( &directions, allocator ) || !Vector_Resize( &directions, count ) ||
             !Vector_Init( &normals, allocator ) || !Vector_Resize( &normals, count ) ||
             !Vector_Init( &positions, allocator ) || !Vector_Resize( &positions, count ) ||
             !Vector_Init( &polygon, allocator ) || !Vector_Init( &sizes, allocator ) ||
             !Vector_Init( &rings, allocator ) || !Vector_Init( &created, allocator ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
        std::fill_n( outgoing.pData, outgoing.nCount, CY_INVALID_INDEX );
        std::fill_n( incoming.pData, incoming.nCount, CY_INVALID_INDEX );
        std::fill_n( insetBySlot.pData, insetBySlot.nCount, CY_INVALID_INDEX );
        for ( usize f = 0u; f < region.faces.nCount; ++f ) {
            const auto &face = region.faces.pData[f];
            for ( u32 k = 0u; k < face.count; ++k ) {
                const auto &a = region.corners.pData[face.first + k];
                if ( a.adjacent != CY_INVALID_INDEX ) { continue; }
                const auto b = region.corners.pData[face.first + ( k + 1u ) % face.count].vertex;
                if ( a.vertex.nSlot >= outgoing.nCount || b.nSlot >= incoming.nCount ) { return geo::geometry_status_t::CORRUPT_STATE; }
                // A simple region has precisely one incoming/outgoing boundary
                // edge at each perimeter vertex. Pinched regions are refused.
                if ( outgoing.pData[a.vertex.nSlot] != CY_INVALID_INDEX || incoming.pData[b.nSlot] != CY_INVALID_INDEX ) { return reject(); }
                outgoing.pData[a.vertex.nSlot] = incoming.pData[b.nSlot] = static_cast<u32>( boundary.nCount );
                ( void )Vector_PushBack( &boundary, boundary_t{ face.first + k, static_cast<u32>( f ), a.vertex, b } );
            }
        }
        u32 next = 0u;
        for ( usize i = 0u; i < count; ++i ) {
            if ( next >= count || visited.pData[next] != 0u ) { return reject(); }
            visited.pData[next] = 1u; ( void )Vector_PushBack( &ordered, next );
            next = outgoing.pData[boundary.pData[next].b.nSlot];
        }
        if ( next != 0u ) { return reject(); } // More than one cycle (including holes) is unsupported.
        geo::planar_frame_t frame{};
        status = geo::PlanarFrame_TryFromPlane( { region.normal, -Dot( region.normal, region.origin ) }, &frame );
        if ( status != OK ) { return status; }
        frame.origin = region.origin; // Keep area and halfplane calculations local to the region.
        f64 extent = 0.0;
        for ( usize i = 0u; i < count; ++i ) {
            const auto &edge = boundary.pData[ordered.pData[i]];
            const auto *vertex = GenerationPool_Get( &working->mesh.vertices, edge.a );
            if ( vertex == nullptr ) { return geo::geometry_status_t::CORRUPT_STATE; }
            points.pData[i] = geo::PlanarFrame_Project( frame, vertex->position );
            insetBySlot.pData[edge.a.nSlot] = static_cast<u32>( i );
            extent = std::max( extent, std::max( std::abs( points.pData[i].x ), std::abs( points.pData[i].y ) ) );
        }
        if ( !InsetRingValid( { points.pData, count }, policy ) ) { return reject(); }
        const f64 tolerance = policy.fAbsoluteDistanceTolerance + policy.fRelativeDistanceTolerance * extent;
        for ( usize i = 0u; i < count; ++i ) {
            const auto delta = InsetSub( points.pData[( i + 1u ) % count], points.pData[i] );
            const f64 length = std::hypot( delta.x, delta.y );
            directions.pData[i] = { delta.x / length, delta.y / length };
        }
        for ( usize i = 0u; i < count; ++i ) {
            const auto previous = directions.pData[( i + count - 1u ) % count], current = directions.pData[i];
            const f64 turn = InsetCross( previous, current );
            if ( turn < -policy.fAngularToleranceRadians || InsetDot( previous, current ) <= -1.0 + policy.fAngularToleranceRadians ) { return reject(); }
            if ( turn > policy.fAngularToleranceRadians ) { ( void )Vector_PushBack( &turns, static_cast<u32>( i ) ); }
        }
        if ( turns.nCount < 3u ) { return reject(); }
        // One straight support line per geometric run; subdivisions remain in
        // the topology and keep their original fractions along the run.
        for ( usize r = 0u; r < turns.nCount; ++r ) {
            const u32 a = turns.pData[r], b = turns.pData[( r + 1u ) % turns.nCount];
            const auto edge = InsetSub( points.pData[b], points.pData[a] );
            const f64 length = std::hypot( edge.x, edge.y );
            if ( !std::isfinite( length ) || length <= policy.fMinimumEdgeLength ) { return reject(); }
            directions.pData[r] = { edge.x / length, edge.y / length };
            normals.pData[r] = { -directions.pData[r].y, directions.pData[r].x };
            f64 previousDistance = 0.0;
            for ( usize j = ( a + 1u ) % count; j != b; j = ( j + 1u ) % count ) {
                const auto relative = InsetSub( points.pData[j], points.pData[a] );
                const f64 along = InsetDot( relative, directions.pData[r] );
                if ( std::abs( InsetDot( relative, normals.pData[r] ) ) > tolerance ||
                     along <= previousDistance || along >= length ) { return reject(); }
                previousDistance = along;
            }
            for ( usize j = 0u; j < count; ++j ) {
                if ( InsetDot( normals.pData[r], InsetSub( points.pData[j], points.pData[a] ) ) < -tolerance ) { return reject(); }
            }
        }
        for ( usize r = 0u; r < turns.nCount; ++r ) {
            const u32 index = turns.pData[r];
            const auto previous = normals.pData[( r + turns.nCount - 1u ) % turns.nCount], current = normals.pData[r];
            const f64 denominator = 1.0 + InsetDot( previous, current );
            if ( denominator <= 64.0 * std::numeric_limits<f64>::epsilon() ) { return reject(); }
            const f64 distance = width / denominator;
            planned.pData[index] = { points.pData[index].x + distance * ( previous.x + current.x ),
                                      points.pData[index].y + distance * ( previous.y + current.y ) };
        }
        for ( usize r = 0u; r < turns.nCount; ++r ) {
            const u32 a = turns.pData[r], b = turns.pData[( r + 1u ) % turns.nCount];
            const f64 length = InsetDot( directions.pData[r], InsetSub( points.pData[b], points.pData[a] ) );
            const auto delta = InsetSub( planned.pData[b], planned.pData[a] );
            if ( InsetDot( directions.pData[r], delta ) <= policy.fMinimumEdgeLength ) { return reject(); }
            for ( usize j = ( a + 1u ) % count; j != b; j = ( j + 1u ) % count ) {
                const f64 t = InsetDot( directions.pData[r], InsetSub( points.pData[j], points.pData[a] ) ) / length;
                planned.pData[j] = { planned.pData[a].x + t * delta.x, planned.pData[a].y + t * delta.y };
            }
        }
        const auto inside = [&]( inset_point_t p ) noexcept {
            for ( usize r = 0u; r < turns.nCount; ++r ) {
                if ( InsetDot( normals.pData[r], InsetSub( p, points.pData[turns.pData[r]] ) ) < width - tolerance ) { return false; }
            }
            return true;
        };
        if ( !InsetRingValid( { planned.pData, count }, policy ) ) { return reject(); }
        const f64 limit = std::min( policy.fCoordinateMagnitudeLimit, geo::kMeshSourceCoordinateMax );
        for ( usize i = 0u; i < count; ++i ) {
            const vec3 p = geo::PlanarFrame_Unproject( frame, planned.pData[i] );
            if ( !inside( planned.pData[i] ) || !std::isfinite( p.x ) || !std::isfinite( p.y ) || !std::isfinite( p.z ) ||
                 std::abs( p.x ) > limit || std::abs( p.y ) > limit || std::abs( p.z ) > limit ) { return reject(); }
            positions.pData[i] = p;
            const usize b = ( i + 1u ) % count;
            const inset_point_t quad[]{ points.pData[i], points.pData[b], planned.pData[b], planned.pData[i] };
            if ( !InsetRingValid( { quad, 4u }, policy ) ) { return reject(); }
        }
        if ( before.value.faces.nCount + count > geo::kMeshSourceFacesMax ||
             before.value.vertices.nCount + count > geo::kMeshSourceVerticesMax ||
             before.value.corners.nCount + 4u * count > geo::kMeshSourceCornersMax ) { return geo::geometry_status_t::LIMIT_EXCEEDED; }
        if ( !Vector_Reserve( &sizes, region.faces.nCount + count ) ||
             !Vector_Reserve( &rings, region.corners.nCount + 4u * count ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
        // Retain every cap cell and every strictly interior authored vertex.
        // A globally valid outer offset may still invert a thin partition cell.
        for ( usize f = 0u; f < region.faces.nCount; ++f ) {
            const auto &face = region.faces.pData[f];
            if ( !Vector_Resize( &polygon, face.count ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
            for ( u32 k = 0u; k < face.count; ++k ) {
                const auto handle = region.corners.pData[face.first + k].vertex;
                const u32 inset = insetBySlot.pData[handle.nSlot];
                const auto *vertex = GenerationPool_Get( &working->mesh.vertices, handle );
                if ( vertex == nullptr ) { return geo::geometry_status_t::CORRUPT_STATE; }
                polygon.pData[k] = inset != CY_INVALID_INDEX ? planned.pData[inset] : geo::PlanarFrame_Project( frame, vertex->position );
                if ( !inside( polygon.pData[k] ) ) { return reject(); }
                ( void )Vector_PushBack( &rings, inset != CY_INVALID_INDEX ? geo::mesh_boundary_corner_t{ {}, inset } : geo::mesh_boundary_corner_t{ handle } );
            }
            if ( !InsetRingValid( { polygon.pData, polygon.nCount }, policy ) ) { return reject(); }
            ( void )Vector_PushBack( &sizes, face.count );
        }
        for ( usize i = 0u; i < count; ++i ) {
            const auto &edge = boundary.pData[ordered.pData[i]];
            ( void )Vector_PushBack( &sizes, 4u );
            ( void )Vector_PushBack( &rings, geo::mesh_boundary_corner_t{ edge.a } );
            ( void )Vector_PushBack( &rings, geo::mesh_boundary_corner_t{ edge.b } );
            ( void )Vector_PushBack( &rings, geo::mesh_boundary_corner_t{ {}, static_cast<u32>( ( i + 1u ) % count ) } );
            ( void )Vector_PushBack( &rings, geo::mesh_boundary_corner_t{ {}, static_cast<u32>( i ) } );
        }
        status = geo::MeshEdit_Bracket( working, [&]( vector_t<geo::mesh_edit_face_parent_t> *parents ) noexcept {
            if ( !Vector_Reserve( parents, sizes.nCount ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
            const auto replaced = geo::MeshBoundary_ReplaceFaces( &working->mesh, { region.removed.pData, region.removed.nCount },
                { rings.pData, rings.nCount }, { sizes.pData, sizes.nCount }, { positions.pData, positions.nCount }, &created );
            if ( replaced.status != OK ) { return replaced.status; }
            for ( usize i = 0u; i < created.nCount; ++i ) {
                const usize parent = i < region.faces.nCount ? i : boundary.pData[ordered.pData[i - region.faces.nCount]].parent;
                ( void )Vector_PushBack( parents, geo::mesh_edit_face_parent_t{ created.pData[i], region.removed.pData[parent], i < region.faces.nCount } );
            }
            return OK;
        }, report );
        if ( status != OK ) { return status; }
        // Transfer samples each new corner from its adjacent parent face's chart.
        // New cap edges inherit the corresponding authored edge's flags/crease;
        // the border's outer edge keeps that data too. Radial border links default.
        for ( usize f = 0u; f < created.nCount; ++f ) {
            const auto *face = GenerationPool_Get( &working->mesh.faces, created.pData[f] );
            const auto *loop = face != nullptr ? GenerationPool_Get( &working->mesh.loops, face->hOuterLoop ) : nullptr;
            const bool cap = f < region.faces.nCount;
            const u32 expected = cap ? region.faces.pData[f].count : 4u;
            if ( loop == nullptr || loop->cHalfEdges != expected ) { return geo::geometry_status_t::CORRUPT_STATE; }
            auto h = loop->hFirstHalfEdge;
            for ( u32 k = 0u; k < ( cap ? expected : 1u ); ++k ) {
                const auto &old = region.corners.pData[cap ? region.faces.pData[f].first + k : boundary.pData[ordered.pData[f - region.faces.nCount]].corner];
                const auto *corner = GenerationPool_Get( &working->mesh.halfEdges, h );
                auto *edge = corner != nullptr ? GenerationPool_Get( &working->mesh.edges, corner->hEdge ) : nullptr;
                if ( edge == nullptr ) { return geo::geometry_status_t::CORRUPT_STATE; }
                status = geo::MeshAttributeStore_TrySetEdge( &working->attributes, corner->hEdge, old.edgeAttributes, working->mesh.edges.cSlots );
                if ( status != OK ) { return status; }
                edge->creaseWeight = old.crease;
                h = corner->hNext;
            }
        }
        return OK;
    } );
    return invalidRegion ? map_status_t::INVALID_ARGUMENT : result;
}

map_status_t MapTopologyEdit_ExtendEdgesBy( map_document_t *map, u64 meshId, map_edge_refs_t edges, vec3 offset,
    vector_t<geo::mesh_edge_ref_t> *pOuterEdgesOut ) noexcept
{
    if ( !ValidEdges( edges ) || Length( offset ) <= 1e-9 || !std::isfinite( Length( offset ) ) ) { return map_status_t::INVALID_ARGUMENT; }
    return MapTopologyEdit_ExtendEdgesTransformed( map, meshId, edges, math::Affine3d_FromTranslation( offset ), pOuterEdgesOut );
}

map_status_t MapTopologyEdit_ExtendEdgesTransformed( map_document_t *map, u64 meshId, map_edge_refs_t edges,
    const math::affine3d_t &transform, vector_t<geo::mesh_edge_ref_t> *pOuterEdgesOut ) noexcept
{
    if ( map == nullptr || !ValidEdges( edges ) || !math::Affine3d_IsFinite( transform ) ||
         ( pOuterEdgesOut != nullptr && ( pOuterEdgesOut->pAllocator == nullptr || !Vector_IsValid( pOuterEdgesOut ) ) ) ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( edges.nCount > CY_INVALID_SIZE / 2u ) { return map_status_t::LIMIT_EXCEEDED; }
    struct pending_t {
        const math::affine3d_t *transform;
        vector_t<id_t> pairs{};
        vector_t<geo::geometry_mesh_edge_handle_t> handles{};
        vector_t<geo::mesh_edge_ref_t> refs{};
        ~pending_t() { Vector_Shutdown( &pairs ); Vector_Shutdown( &handles ); Vector_Shutdown( &refs ); }
    } pending{ &transform };
    const auto *outputAllocator = pOuterEdgesOut != nullptr ? pOuterEdgesOut->pAllocator : map->pAllocator;
    if ( !Vector_Init( &pending.pairs, map->pAllocator ) || !Vector_Resize( &pending.pairs, edges.nCount * 2u ) ||
         !Vector_Init( &pending.handles, map->pAllocator ) || !Vector_Init( &pending.refs, outputAllocator ) ||
         !Vector_Reserve( &pending.refs, edges.nCount ) ) { return map_status_t::OUT_OF_MEMORY; }
    for ( usize i = 0u; i < edges.nCount; ++i ) {
        pending.pairs.pData[2u * i] = edges.pData[i].a;
        pending.pairs.pData[2u * i + 1u] = edges.pData[i].b;
    }
    const auto result = MapMeshEdit_Apply( map, meshId,
        []( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
            auto &p = *static_cast<pending_t *>( context );
            return geo::MeshSourceEdit_TryExtrudeEdgesTransformed( working, { p.pairs.pData, p.pairs.nCount },
                *p.transform, &p.handles, report );
        }, &pending,
        []( void *context, const geo::mesh_source_t *working ) noexcept {
            auto &p = *static_cast<pending_t *>( context );
            for ( usize i = 0u; i < p.handles.nCount; ++i ) {
                const auto *edge = GenerationPool_Get( &working->mesh.edges, p.handles.pData[i] );
                const auto *half = edge != nullptr ? GenerationPool_Get( &working->mesh.halfEdges, edge->hHalfEdge ) : nullptr;
                const auto *next = half != nullptr ? GenerationPool_Get( &working->mesh.halfEdges, half->hNext ) : nullptr;
                if ( half == nullptr || next == nullptr ) { return geo::geometry_status_t::CORRUPT_STATE; }
                const auto a = geo::MeshSource_VertexId( working, half->hOrigin );
                const auto b = geo::MeshSource_VertexId( working, next->hOrigin );
                if ( a.value == 0u || b.value == 0u ) { return geo::geometry_status_t::CORRUPT_STATE; }
                if ( !Vector_PushBack( &p.refs, geo::MeshEdgeRef_Make( a, b ) ) ) { return geo::geometry_status_t::ALLOCATION_FAILED; }
            }
            std::sort( p.refs.pData, p.refs.pData + p.refs.nCount, []( const auto &a, const auto &b ) noexcept {
                return a.a.value < b.a.value || ( a.a.value == b.a.value && a.b.value < b.b.value );
            } );
            return OK;
        } );
    if ( result == map_status_t::OK && pOuterEdgesOut != nullptr ) {
        Vector_Shutdown( pOuterEdgesOut );
        Vector_Move( pOuterEdgesOut, &pending.refs );
    }
    return result;
}

map_status_t MapTopologyEdit_MoveBrushFace( map_document_t *map, u64 brushId, u64 sideId, vec3 offset ) noexcept
{
    if ( map == nullptr || sideId == 0 || !std::isfinite( offset.x ) || !std::isfinite( offset.y ) || !std::isfinite( offset.z ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( geo::GeometryDocument_FindBrush( &map->geometry, { brushId } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    geo::brush_source_t copy{};
    geo::brush_boundary_t boundary{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &map->geometry, { brushId }, map->pAllocator, &copy );
    if ( status == OK ) { status = geo::BrushBoundary_Init( &boundary, map->pAllocator ); }
    if ( status == OK ) { status = geo::BrushBoundary_TryReconstruct( &boundary, &copy.solid, map->geometryPolicy ); }
    usize face = boundary.faces.nCount;
    for ( usize f = 0; status == OK && f < boundary.faces.nCount; ++f ) {
        const u32 side = boundary.faces.pData[f].iSide;
        if ( side < copy.solid.sides.nCount && copy.solid.sides.pData[side].sourceId.value == sideId ) { face = f; }
    }
    if ( status == OK && face == boundary.faces.nCount ) { geo::BrushBoundary_Shutdown( &boundary ); geo::BrushSource_Shutdown( &copy ); return map_status_t::UNKNOWN_OBJECT; }
    geo::geometry_source_id_allocator_t ids{ { std::max( map->nextId, map->geometry.sourceIds.allocator.next.value ) } };
    if ( status == OK ) { status = geo::BrushVertexOps_TryMoveFace( &copy.solid, &boundary, map->pAllocator, map->geometryPolicy, &ids, face, offset ).status; }
    if ( status == OK ) { status = geo::GeometryDocument_TryReplaceBrushSourcesExact( &map->geometry, { &copy.solid.sourceId, 1 }, { &copy, 1 } ); }
    geo::BrushBoundary_Shutdown( &boundary );
    geo::BrushSource_Shutdown( &copy );
    if ( status != OK ) { return Status( status ); }
    map->nextId = std::max( map->nextId, ids.next.value );
    ++map->geometry.revision;
    return map_status_t::OK;
}

map_status_t MapTopologyEdit_Knife( map_document_t *map, u64 meshId, span_t<const geo::mesh_edit_knife_point_t> path ) noexcept
{
    if ( !Span_IsValid( path ) || path.nCount < 2 ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        return geo::MeshSourceEdit_TryKnife( working, path, nullptr, report );
    } );
}

map_status_t MapTopologyEdit_LoopCut( map_document_t *map, u64 meshId, geo::mesh_edge_ref_t edge, f64 t ) noexcept
{
    if ( !ValidEdges( { &edge, 1 } ) || !std::isfinite( t ) || t <= 0.0 || t >= 1.0 ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        return geo::MeshSourceEdit_TryLoopCut( working, edge.a, edge.b, t, nullptr, report );
    } );
}

map_status_t MapTopologyEdit_MoveBrushVertex( map_document_t *map, u64 brushId, vec3 from, vec3 to ) noexcept
{
    if ( map == nullptr || !std::isfinite( to.x ) || !std::isfinite( to.y ) || !std::isfinite( to.z ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( geo::GeometryDocument_FindBrush( &map->geometry, { brushId } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    geo::brush_source_t copy{};
    geo::brush_boundary_t boundary{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &map->geometry, { brushId }, map->pAllocator, &copy );
    if ( status == OK ) { status = geo::BrushBoundary_Init( &boundary, map->pAllocator ); }
    if ( status == OK ) { status = geo::BrushBoundary_TryReconstruct( &boundary, &copy.solid, map->geometryPolicy ); }
    usize nearest = boundary.vertices.nCount;
    f64 best = 1e300;
    for ( usize i = 0; status == OK && i < boundary.vertices.nCount; ++i ) {
        const f64 distance = Length( Sub( boundary.vertices.pData[i], from ) );
        if ( distance < best ) { best = distance; nearest = i; }
    }
    // The vertex must be the one the user grabbed, not merely the closest.
    if ( status == OK && ( nearest == boundary.vertices.nCount || best > 1e-3 ) ) {
        geo::BrushBoundary_Shutdown( &boundary ); geo::BrushSource_Shutdown( &copy );
        return map_status_t::INVALID_ARGUMENT;
    }
    geo::geometry_source_id_allocator_t ids{ { std::max( map->nextId, map->geometry.sourceIds.allocator.next.value ) } };
    if ( status == OK ) { status = geo::BrushVertexOps_TryMoveVertex( &copy.solid, &boundary, map->pAllocator, map->geometryPolicy, &ids, nearest, to ).status; }
    if ( status == OK ) { status = geo::GeometryDocument_TryReplaceBrushSourcesExact( &map->geometry, { &copy.solid.sourceId, 1 }, { &copy, 1 } ); }
    geo::BrushBoundary_Shutdown( &boundary );
    geo::BrushSource_Shutdown( &copy );
    if ( status != OK ) { return Status( status ); }
    map->nextId = std::max( map->nextId, ids.next.value );
    ++map->geometry.revision;
    return map_status_t::OK;
}

map_status_t MapTopologyEdit_MoveBrushVertices( map_document_t *map, u64 brushId, span_t<const vec3> from, vec3 offset ) noexcept
{
    if ( map == nullptr || !Span_IsValid( from ) || from.nCount == 0 || !std::isfinite( offset.x ) || !std::isfinite( offset.y ) || !std::isfinite( offset.z ) ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( geo::GeometryDocument_FindBrush( &map->geometry, { brushId } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    geo::brush_source_t copy{};
    geo::brush_boundary_t boundary{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &map->geometry, { brushId }, map->pAllocator, &copy );
    if ( status == OK ) { status = geo::BrushBoundary_Init( &boundary, map->pAllocator ); }
    if ( status == OK ) { status = geo::BrushBoundary_TryReconstruct( &boundary, &copy.solid, map->geometryPolicy ); }
    // Each position must be a corner the user grabbed, not merely near one.
    std::vector<usize> indices;
    for ( usize k = 0; status == OK && k < from.nCount; ++k ) {
        usize nearest = boundary.vertices.nCount;
        f64 best = 1e300;
        for ( usize i = 0; i < boundary.vertices.nCount; ++i ) {
            const f64 distance = Length( Sub( boundary.vertices.pData[i], from.pData[k] ) );
            if ( distance < best ) { best = distance; nearest = i; }
        }
        if ( nearest == boundary.vertices.nCount || best > 1e-3 || std::find( indices.begin(), indices.end(), nearest ) != indices.end() ) {
            status = geo::geometry_status_t::INVALID_ARGUMENT;
        } else {
            indices.push_back( nearest );
        }
    }
    geo::geometry_source_id_allocator_t ids{ { std::max( map->nextId, map->geometry.sourceIds.allocator.next.value ) } };
    if ( status == OK ) {
        status = geo::BrushVertexOps_TryMoveVertices( &copy.solid, &boundary, map->pAllocator, map->geometryPolicy, &ids, indices.data(), indices.size(), offset ).status;
    }
    if ( status == OK ) { status = geo::GeometryDocument_TryReplaceBrushSourcesExact( &map->geometry, { &copy.solid.sourceId, 1 }, { &copy, 1 } ); }
    geo::BrushBoundary_Shutdown( &boundary );
    geo::BrushSource_Shutdown( &copy );
    if ( status != OK ) { return Status( status ); }
    map->nextId = std::max( map->nextId, ids.next.value );
    ++map->geometry.revision;
    return map_status_t::OK;
}

map_status_t MapTopologyEdit_MoveBrushVertexClump( map_document_t *map, span_t<const u64> brushes, vec3 from, vec3 to, u32 *pMovedOut ) noexcept
{
    if ( pMovedOut != nullptr ) { *pMovedOut = 0; }
    if ( map == nullptr || !Span_IsValid( brushes ) || brushes.nCount == 0 || !std::isfinite( to.x ) || !std::isfinite( to.y ) || !std::isfinite( to.z ) ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    std::vector<geo::brush_source_t> copies( brushes.nCount );
    auto status = OK;
    for ( usize i = 0; status == OK && i < brushes.nCount; ++i ) {
        status = geo::GeometryDocument_TryCopyBrushSource( &map->geometry, { brushes.pData[i] }, map->pAllocator, &copies[i] );
    }
    std::vector<geo::brush_solid_t *> solids;
    for ( auto &copy : copies ) { solids.push_back( &copy.solid ); }
    geo::geometry_source_id_allocator_t ids{ { std::max( map->nextId, map->geometry.sourceIds.allocator.next.value ) } };
    u32 moved = 0;
    if ( status == OK ) { status = geo::BrushVertexOps_TryMoveVertexClump( { solids.data(), solids.size() }, from, to, map->pAllocator, map->geometryPolicy, &ids, &moved ); }
    if ( status == OK && moved == 0 ) { status = geo::geometry_status_t::INVALID_ARGUMENT; } // Nobody had the corner.
    std::vector<geo::geometry_source_id_t> roots;
    for ( const auto &copy : copies ) { roots.push_back( copy.solid.sourceId ); }
    if ( status == OK ) { status = geo::GeometryDocument_TryReplaceBrushSourcesExact( &map->geometry, { roots.data(), roots.size() }, { copies.data(), copies.size() } ); }
    for ( auto &copy : copies ) { geo::BrushSource_Shutdown( &copy ); }
    if ( status != OK ) { return Status( status ); }
    map->nextId = std::max( map->nextId, ids.next.value );
    ++map->geometry.revision;
    if ( pMovedOut != nullptr ) { *pMovedOut = moved; }
    return map_status_t::OK;
}

/*
================
Faces
================
*/

map_status_t MapTopologyEdit_CollapseFace( map_document_t *map, u64 meshId, u64 faceId ) noexcept
{
    if ( faceId == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        description_t mesh;
        auto status = mesh.Read( working );
        if ( status != OK ) { return status; }
        for ( usize f = 0; f < mesh.value.faces.nCount; ++f ) {
            const auto &face = mesh.value.faces.pData[f];
            if ( face.sourceId.value != faceId ) { continue; }
            std::vector<id_t> flat;
            for ( u32 k = 0; k < face.cCorners; ++k ) { flat.push_back( mesh.value.vertices.pData[mesh.CornerVertex( face, k )].sourceId ); }
            const std::vector<u32> sizes{ static_cast<u32>( flat.size() ) };
            return MergeGroups( working, flat, sizes, report );
        }
        return geo::geometry_status_t::INVALID_HANDLE;
    } );
}

map_status_t MapTopologyEdit_DetachFace( map_document_t *map, u64 meshId, u64 faceId ) noexcept
{
    return MapTopologyEdit_DetachFaces( map, meshId, { &faceId, 1 } );
}

map_status_t MapTopologyEdit_DetachFaces( map_document_t *map, u64 meshId, span_t<const u64> faceIds ) noexcept
{
    if ( !Span_IsValid( faceIds ) || faceIds.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    std::vector<id_t> faces;
    for ( usize i = 0; i < faceIds.nCount; ++i ) {
        if ( faceIds.pData[i] == 0 ) { return map_status_t::INVALID_ARGUMENT; }
        faces.push_back( { faceIds.pData[i] } );
    }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        return geo::MeshSourceEdit_TryDetachFaces( working, { faces.data(), faces.size() }, report );
    } );
}

/*
================
Whole meshes and objects
================
*/

map_status_t MapTopologyEdit_Smooth( map_document_t *map, span_t<const u64> meshes, f64 factor, u32 cIterations ) noexcept
{
    auto result = CheckMeshes( map, meshes );
    if ( result != map_status_t::OK ) { return result; }
    if ( !std::isfinite( factor ) || factor <= 0.0 || factor > 1.0 || cIterations == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0; result == map_status_t::OK && i < meshes.nCount; ++i ) {
        result = ApplyMesh( map, meshes.pData[i], [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
            return geo::MeshSourceEdit_TrySmooth( working, factor, cIterations );
        } );
    }
    return result;
}

map_status_t MapTopologyEdit_SnapMeshes( map_document_t *map, span_t<const u64> meshes, f64 grid ) noexcept
{
    auto result = CheckMeshes( map, meshes );
    if ( result != map_status_t::OK ) { return result; }
    if ( !std::isfinite( grid ) || grid <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0; result == map_status_t::OK && i < meshes.nCount; ++i ) {
        result = ApplyMesh( map, meshes.pData[i], [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
            return geo::MeshSourceEdit_TrySnapToGrid( working, grid );
        } );
    }
    return result;
}

map_status_t MapTopologyEdit_SliceMeshes( map_document_t *map, span_t<const u64> meshes, math::planed_t plane, map_slice_keep_t keep ) noexcept
{
    auto result = CheckMeshes( map, meshes );
    if ( result != map_status_t::OK ) { return result; }
    vec3 unit{};
    if ( keep > map_slice_keep_t::BACK || !Unit( plane.normal, unit ) || std::fabs( Length( plane.normal ) - 1.0 ) > 1e-9 || !std::isfinite( plane.d ) ) {
        return map_status_t::INVALID_ARGUMENT;
    }
    geo::mesh_slice_params_t params{};
    params.plane = plane;
    params.keep = keep == map_slice_keep_t::FRONT ? geo::mesh_slice_keep_t::FRONT : keep == map_slice_keep_t::BACK ? geo::mesh_slice_keep_t::BACK : geo::mesh_slice_keep_t::BOTH;
    params.bCap = keep != map_slice_keep_t::BOTH;
    params.onPlaneTolerance = map->geometryPolicy.numerical.fAbsoluteDistanceTolerance;
    for ( usize i = 0; result == map_status_t::OK && i < meshes.nCount; ++i ) {
        result = ApplyMesh( map, meshes.pData[i], [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
            return geo::MeshSourceEdit_TrySliceByPlane( working, params, nullptr, report );
        } );
    }
    return result;
}

namespace
{
// Where a mesh lies against the plane: -1 wholly behind, 1 wholly in front
// (vertices on the plane count for either side), 0 crossing it.
int MeshSide( const geo::mesh_source_t *mesh, math::planed_t plane, f64 tolerance ) noexcept
{
    bool front = false, back = false;
    ( void )GenerationPool_ForEach( &mesh->mesh.vertices, [&]( geo::geometry_mesh_vertex_handle_t, const geo::mesh_vertex_record_t &vertex ) noexcept -> bool_t {
        const f64 distance = Dot( plane.normal, vertex.position ) + plane.d;
        front = front || distance > tolerance; back = back || distance < -tolerance;
        return CY_TRUE;
    } );
    return front && back ? 0 : back ? -1 : 1;
}

geo::mesh_slice_params_t ClipParams( const map_document_t *map, math::planed_t plane, map_slice_keep_t keep, bool cap ) noexcept
{
    geo::mesh_slice_params_t params{};
    params.plane = plane;
    params.keep = keep == map_slice_keep_t::FRONT ? geo::mesh_slice_keep_t::FRONT : keep == map_slice_keep_t::BACK ? geo::mesh_slice_keep_t::BACK : geo::mesh_slice_keep_t::BOTH;
    params.bCap = cap && keep != map_slice_keep_t::BOTH;
    params.bCapClosedShellsOnly = true;
    params.onPlaneTolerance = map->geometryPolicy.numerical.fAbsoluteDistanceTolerance;
    return params;
}

bool ValidPlane( math::planed_t plane ) noexcept
{
    return std::isfinite( plane.d ) && std::isfinite( Length( plane.normal ) ) && std::fabs( Length( plane.normal ) - 1.0 ) <= 1e-9;
}
} // namespace

map_status_t MapTopologyEdit_ClipMeshes( map_document_t *map, span_t<const u64> meshes, math::planed_t plane, map_slice_keep_t keep,
    vector_t<u64> *pIdsOut, bool cap ) noexcept
{
    auto result = CheckMeshes( map, meshes );
    if ( result != map_status_t::OK ) { return result; }
    if ( keep > map_slice_keep_t::BACK || !ValidPlane( plane ) ) { return map_status_t::INVALID_ARGUMENT; }
    const f64 tolerance = map->geometryPolicy.numerical.fAbsoluteDistanceTolerance;
    vector_t<u64> ids{};
    if ( !Vector_Init( &ids, map->pAllocator ) || !Vector_Append( &ids, meshes ) ) { return map_status_t::OUT_OF_MEMORY; }
    const auto keepId = [&]( u64 id ) { return pIdsOut == nullptr || Vector_PushBack( pIdsOut, id ); };
    for ( usize i = 0; result == map_status_t::OK && i < ids.nCount; ++i ) {
        const u64 id = ids.pData[i];
        const auto *mesh = geo::GeometryDocument_FindMesh( &map->geometry, { id } );
        const int side = MeshSide( mesh, plane, tolerance );
        // The library caps originally closed shells while open modelling
        // sheets in this same object retain their own open boundaries.
        if ( keep == map_slice_keep_t::BOTH ) {
            if ( side != 0 ) { result = keepId( id ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY; continue; }
            // The back half starts as an exact copy, then each copy keeps its side.
            vector_t<u64> copies{};
            if ( !Vector_Init( &copies, map->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
            result = MapEdit_Duplicate( map, { &id, 1 }, { 0, 0, 0 }, &copies );
            const u64 back = result == map_status_t::OK && copies.nCount == 1 ? copies.pData[0] : 0;
            Vector_Shutdown( &copies );
            if ( result != map_status_t::OK ) { return result; }
            if ( back == 0 ) { return map_status_t::GEOMETRY_FAILED; }
            const auto front = ClipParams( map, plane, map_slice_keep_t::FRONT, cap ), behind = ClipParams( map, plane, map_slice_keep_t::BACK, cap );
            result = ApplyMesh( map, id, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
                return geo::MeshSourceEdit_TrySliceByPlane( working, front, nullptr, report );
            } );
            if ( result == map_status_t::OK ) {
                result = ApplyMesh( map, back, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
                    return geo::MeshSourceEdit_TrySliceByPlane( working, behind, nullptr, report );
                } );
            }
            if ( result == map_status_t::OK && ( !keepId( id ) || !keepId( back ) ) ) { result = map_status_t::OUT_OF_MEMORY; }
            continue;
        }
        const int kept = keep == map_slice_keep_t::FRONT ? 1 : -1;
        if ( side == -kept ) { result = MapEdit_Delete( map, { &id, 1 } ); continue; }
        if ( side == 0 ) {
            const auto params = ClipParams( map, plane, keep, cap );
            result = ApplyMesh( map, id, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
                return geo::MeshSourceEdit_TrySliceByPlane( working, params, nullptr, report );
            } );
        }
        if ( result == map_status_t::OK && !keepId( id ) ) { result = map_status_t::OUT_OF_MEMORY; }
    }
    return result;
}

map_status_t MapTopologyEdit_AddClipPreview( map_document_t *preview, const map_document_t *source, span_t<const u64> meshes, math::planed_t plane,
    map_slice_keep_t keep, bool cap ) noexcept
{
    if ( preview == nullptr || source == nullptr || !Span_IsValid( meshes ) || keep > map_slice_keep_t::BACK || !ValidPlane( plane ) ) { return map_status_t::INVALID_ARGUMENT; }
    const f64 tolerance = source->geometryPolicy.numerical.fAbsoluteDistanceTolerance;
    geo::geometry_source_id_allocator_t ids{ { std::max( preview->nextId, std::max( source->nextId, source->geometry.sourceIds.allocator.next.value ) ) } };
    for ( usize i = 0; i < meshes.nCount; ++i ) {
        const auto *mesh = geo::GeometryDocument_FindMesh( &source->geometry, { meshes.pData[i] } );
        if ( mesh == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
        const int side = MeshSide( mesh, plane, tolerance );
        if ( ( keep == map_slice_keep_t::FRONT && side < 0 ) || ( keep == map_slice_keep_t::BACK && side > 0 ) ) { continue; }
        const bool split = side == 0 && keep == map_slice_keep_t::BOTH;
        // Preview the same two independently clipped halves as commit,
        // including caps. A seam-only BOTH preview could previously succeed
        // even though a later capped half failed validation.
        for ( int piece = 0; piece < ( split ? 2 : 1 ); ++piece ) {
            geo::mesh_source_t copy{};
            auto status = geo::MeshSource_TryClone( mesh, preview->pAllocator, &copy );
            if ( status == geo::geometry_status_t::OK && side == 0 ) {
                geo::mesh_edit_report_t report{};
                const auto pieceKeep = split ? ( piece == 0 ? map_slice_keep_t::FRONT : map_slice_keep_t::BACK ) : keep;
                status = geo::MeshSourceEdit_TrySliceByPlane( &copy, ClipParams( source, plane, pieceKeep, cap ), nullptr, &report );
            }
            if ( status == geo::geometry_status_t::OK && piece != 0 ) {
                const auto root = geo::GeometrySourceIdAllocator_Allocate( &ids );
                status = root.status;
                if ( status == geo::geometry_status_t::OK ) {
                    copy.sourceId = root.id;
                    // This is an unpublished presentation copy. Retain slot
                    // generations and attributes, replacing only identities
                    // that would collide with its sibling preview half.
                    for ( usize k = 0; k < copy.vertexIds.nCount; ++k ) { copy.vertexIds.pData[k].sourceId = {}; }
                    for ( usize k = 0; k < copy.faceIds.nCount; ++k ) { copy.faceIds.pData[k].sourceId = {}; }
                }
            }
            if ( status == geo::geometry_status_t::OK ) { status = geo::MeshSource_TryAssignMissingIds( &copy, &ids, nullptr ); }
            if ( status == geo::geometry_status_t::OK ) { status = geo::GeometryDocument_TryAddMesh( &preview->geometry, &copy ); }
            geo::MeshSource_Shutdown( &copy );
            if ( status != geo::geometry_status_t::OK ) { return Status( status ); }
        }
    }
    preview->nextId = std::max( preview->nextId, ids.next.value );
    return map_status_t::OK;
}

namespace
{
// A mesh's faces as plain data: corner positions and IDs, per face.
struct face_shape_t {
    u64 id{ 0 };
    std::vector<u64> corners; // Vertex IDs, in order.
    vec3 centroid{};
    f64 area{ 0.0 };
};

bool FaceShapes( const map_document_t *map, u64 meshId, std::vector<face_shape_t> &out ) noexcept
{
    const auto *mesh = geo::GeometryDocument_FindMesh( &map->geometry, { meshId } );
    if ( mesh == nullptr ) { return false; }
    geo::mesh_source_description_t d{};
    if ( geo::MeshSourceDescription_Init( &d, map->pAllocator, { meshId } ) != geo::geometry_status_t::OK ) { return false; }
    const bool ok = geo::MeshSource_TryDescribe( mesh, &d ) == geo::geometry_status_t::OK;
    for ( usize f = 0; ok && f < d.faces.nCount; ++f ) {
        const auto &face = d.faces.pData[f];
        face_shape_t shape{};
        shape.id = face.sourceId.value;
        vec3 normal{};
        std::vector<vec3> points;
        for ( u32 c = 0; c < face.cCorners; ++c ) {
            const auto &vertex = d.vertices.pData[d.corners.pData[face.iFirstCorner + c].iVertex];
            shape.corners.push_back( vertex.sourceId.value );
            points.push_back( vertex.position );
            shape.centroid = Add( shape.centroid, vertex.position );
        }
        for ( usize k = 0; k < points.size(); ++k ) { normal = Add( normal, Cross( points[k], points[( k + 1 ) % points.size()] ) ); }
        shape.area = 0.5 * Length( normal );
        if ( !points.empty() ) { shape.centroid = Mul( shape.centroid, 1.0 / static_cast<f64>( points.size() ) ); }
        out.push_back( std::move( shape ) );
    }
    geo::MeshSourceDescription_Shutdown( &d );
    return ok;
}

bool Has( span_t<const u64> ids, u64 id ) noexcept
{
    for ( usize i = 0; i < ids.nCount; ++i ) { if ( ids.pData[i] == id ) { return true; } }
    return false;
}
} // namespace

map_status_t MapTopologyEdit_ExtractFaces( map_document_t *map, u64 meshId, span_t<const u64> faceIds, u64 *pNewIdOut ) noexcept
{
    if ( pNewIdOut != nullptr ) { *pNewIdOut = 0; }
    auto result = CheckMeshes( map, { &meshId, 1 } );
    if ( result != map_status_t::OK ) { return result; }
    std::vector<face_shape_t> faces;
    if ( !Span_IsValid( faceIds ) || faceIds.nCount == 0 || !FaceShapes( map, meshId, faces ) ) { return map_status_t::INVALID_ARGUMENT; }
    std::vector<vec3> keep;
    for ( const auto &face : faces ) { if ( Has( faceIds, face.id ) ) { keep.push_back( face.centroid ); } }
    if ( keep.size() != faceIds.nCount ) { return map_status_t::INVALID_ARGUMENT; }
    vector_t<u64> copies{};
    if ( !Vector_Init( &copies, map->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    result = MapEdit_Duplicate( map, { &meshId, 1 }, { 0, 0, 0 }, &copies );
    const u64 copy = result == map_status_t::OK && copies.nCount == 1 ? copies.pData[0] : 0;
    Vector_Shutdown( &copies );
    if ( result != map_status_t::OK ) { return result; }
    if ( copy == 0 ) { return map_status_t::GEOMETRY_FAILED; }
    // The copy's faces have fresh IDs but the same places: drop the ones that
    // were not picked.
    std::vector<face_shape_t> copied;
    if ( !FaceShapes( map, copy, copied ) ) { return map_status_t::GEOMETRY_FAILED; }
    std::vector<u64> drop;
    for ( const auto &face : copied ) {
        const bool picked = std::any_of( keep.begin(), keep.end(), [&]( vec3 c ) { return Length( Sub( c, face.centroid ) ) <= 1e-6; } );
        if ( !picked ) { drop.push_back( face.id ); }
    }
    if ( !drop.empty() ) { result = MapTopologyEdit_DeleteFaces( map, copy, { drop.data(), drop.size() } ); }
    if ( result == map_status_t::OK && pNewIdOut != nullptr ) { *pNewIdOut = copy; }
    return result;
}

map_status_t MapTopologyEdit_ThickenFaces( map_document_t *map, u64 meshId, span_t<const u64> faceIds, f64 thickness, u64 *pNewIdOut ) noexcept
{
    if ( pNewIdOut != nullptr ) { *pNewIdOut = 0; }
    if ( !std::isfinite( thickness ) || thickness <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    u64 slab = 0;
    auto result = MapTopologyEdit_ExtractFaces( map, meshId, faceIds, &slab );
    if ( result == map_status_t::OK ) { result = MapTopologyEdit_Solidify( map, { &slab, 1 }, thickness ); }
    if ( result == map_status_t::OK && pNewIdOut != nullptr ) { *pNewIdOut = slab; }
    return result;
}

map_status_t MapTopologyEdit_CombineFaces( map_document_t *map, u64 meshId, span_t<const u64> faceIds ) noexcept
{
    auto result = CheckMeshes( map, { &meshId, 1 } );
    if ( result != map_status_t::OK ) { return result; }
    std::vector<face_shape_t> faces;
    if ( !Span_IsValid( faceIds ) || faceIds.nCount < 2 || !FaceShapes( map, meshId, faces ) ) { return map_status_t::INVALID_ARGUMENT; }
    // Edges used by two picked faces are inside the group: dissolve them.
    std::vector<std::pair<u64, u64>> seen, shared;
    for ( const auto &face : faces ) {
        if ( !Has( faceIds, face.id ) ) { continue; }
        for ( usize k = 0; k < face.corners.size(); ++k ) {
            u64 a = face.corners[k], b = face.corners[( k + 1 ) % face.corners.size()];
            if ( a > b ) { std::swap( a, b ); }
            const auto edge = std::make_pair( a, b );
            if ( std::find( seen.begin(), seen.end(), edge ) != seen.end() ) { shared.push_back( edge ); } else { seen.push_back( edge ); }
        }
    }
    if ( shared.empty() ) { return map_status_t::INVALID_ARGUMENT; }
    std::vector<geo::mesh_edge_ref_t> edges;
    for ( const auto &[a, b] : shared ) { edges.push_back( geo::MeshEdgeRef_Make( { a }, { b } ) ); }
    return MapTopologyEdit_DissolveEdges( map, meshId, { edges.data(), edges.size() } );
}

map_status_t MapTopologyEdit_RemoveBadFaces( map_document_t *map, span_t<const u64> meshes, f64 areaTolerance, usize *pRemovedOut ) noexcept
{
    if ( pRemovedOut != nullptr ) { *pRemovedOut = 0; }
    auto result = CheckMeshes( map, meshes );
    if ( result != map_status_t::OK ) { return result; }
    if ( !std::isfinite( areaTolerance ) || areaTolerance < 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    usize removed = 0;
    for ( usize i = 0; result == map_status_t::OK && i < meshes.nCount; ++i ) {
        // One face at a time: each collapse changes the mesh around it. A
        // collapse that leaves the face in place cannot loop forever.
        usize guard = 0;
        for ( bool again = true; again && result == map_status_t::OK && guard < 4096; ++guard ) {
            again = false;
            std::vector<face_shape_t> faces;
            if ( !FaceShapes( map, meshes.pData[i], faces ) ) { return map_status_t::GEOMETRY_FAILED; }
            for ( const auto &face : faces ) {
                if ( face.area >= areaTolerance && face.corners.size() >= 3 ) { continue; }
                result = MapTopologyEdit_CollapseFace( map, meshes.pData[i], face.id );
                if ( result == map_status_t::OK ) { ++removed; again = true; }
                break;
            }
        }
    }
    if ( result == map_status_t::OK && pRemovedOut != nullptr ) { *pRemovedOut = removed; }
    return result;
}

map_status_t MapTopologyEdit_EditSeams( map_document_t *map, u64 meshId, map_edge_refs_t edges, map_seam_edit_t edit ) noexcept
{
    auto result = CheckMeshes( map, { &meshId, 1 } );
    if ( result != map_status_t::OK ) { return result; }
    if ( !Span_IsValid( edges ) || edges.nCount == 0 || edit > map_seam_edit_t::TOGGLE ) { return map_status_t::INVALID_ARGUMENT; }
    std::vector<geo::mesh_source_edge_key_t> keys;
    for ( usize i = 0; i < edges.nCount; ++i ) { keys.push_back( { edges.pData[i].a, edges.pData[i].b } ); }
    const auto kind = edit == map_seam_edit_t::SET ? geo::mesh_uv_seam_edit_t::SET : edit == map_seam_edit_t::CLEAR ? geo::mesh_uv_seam_edit_t::CLEAR : geo::mesh_uv_seam_edit_t::TOGGLE;
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        return geo::MeshUv_TryEditSeams( working, { keys.data(), keys.size() }, kind );
    } );
}

map_status_t MapTopologyEdit_UvIsland( const map_document_t *map, u64 meshId, u64 faceId, std::vector<u64> &faceIdsOut, usize *pIslandCountOut ) noexcept
{
    faceIdsOut.clear();
    if ( pIslandCountOut != nullptr ) { *pIslandCountOut = 0; }
    const auto *mesh = map != nullptr ? geo::GeometryDocument_FindMesh( &map->geometry, { meshId } ) : nullptr;
    if ( mesh == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    geo::mesh_uv_island_result_t islands{};
    auto status = geo::MeshUvIslandResult_Init( &islands, map->pAllocator );
    if ( status == OK ) { status = geo::MeshUv_TryDiscoverIslands( mesh, geo::mesh_uv_set_t::MATERIAL, 1e-6, &islands ); }
    for ( usize i = 0; status == OK && i < islands.islands.nCount && faceIdsOut.empty(); ++i ) {
        const auto &range = islands.islands.pData[i];
        bool holds = false;
        for ( u32 k = 0; k < range.cFaces; ++k ) { holds = holds || islands.faceIds.pData[range.iFirstFace + k].value == faceId; }
        for ( u32 k = 0; holds && k < range.cFaces; ++k ) { faceIdsOut.push_back( islands.faceIds.pData[range.iFirstFace + k].value ); }
    }
    if ( status == OK && pIslandCountOut != nullptr ) { *pIslandCountOut = islands.islands.nCount; }
    geo::MeshUvIslandResult_Shutdown( &islands );
    if ( status != OK ) { return Status( status ); }
    return faceIdsOut.empty() ? map_status_t::UNKNOWN_OBJECT : map_status_t::OK;
}

map_status_t MapTopologyEdit_Inspect( const map_document_t *map, u64 meshId, map_repair_report_t *pReportOut ) noexcept
{
    if ( pReportOut == nullptr || map == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    *pReportOut = {};
    const auto *mesh = geo::GeometryDocument_FindMesh( &map->geometry, { meshId } );
    if ( mesh == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    geo::repair_plan_t plan{};
    plan.bWeld = true; plan.fWeldDistance = map->geometryPolicy.numerical.fAbsoluteDistanceTolerance;
    plan.bRemoveDegenerateFaces = true; plan.bRemoveDuplicateFaces = true; plan.bOrientConsistently = true;
    plan.bFillHoles = false;
    geo::sanitation_policy_t rebuild{};
    rebuild.bRequireClosed = false;
    geo::editable_mesh_t preview{};
    const auto report = geo::Repair_TryPreviewMesh( &mesh->mesh, plan, rebuild, map->pAllocator, &preview, nullptr );
    geo::EditableMesh_Shutdown( &preview );
    if ( report.status != OK ) { return Status( report.status ); }
    *pReportOut = { report.cVerticesWelded, report.cDegenerateFacesRemoved, report.cDuplicateFacesRemoved, report.cFacesFlipped, 0u, 0u, report.cNonManifoldEdges };
    return map_status_t::OK;
}

map_status_t MapTopologyEdit_Repair( map_document_t *map, u64 meshId, f64 weldDistance, string_view_t material, f64 worldUnitsPerUv,
    map_repair_report_t *pReportOut, bool fillHoles ) noexcept
{
    if ( pReportOut != nullptr ) { *pReportOut = {}; }
    auto result = CheckMeshes( map, { &meshId, 1 } );
    if ( result != map_status_t::OK ) { return result; }
    if ( !std::isfinite( weldDistance ) || weldDistance < 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    geo::repair_plan_t plan{};
    plan.bWeld = true; plan.fWeldDistance = weldDistance;
    plan.bRemoveDegenerateFaces = true; plan.bRemoveDuplicateFaces = true; plan.bOrientConsistently = true;
    plan.bFillHoles = fillHoles; plan.fPlanarityTolerance = std::max( 1e-6, map->geometryPolicy.numerical.fAbsoluteDistanceTolerance );
    geo::sanitation_policy_t rebuild{};
    rebuild.bRequireClosed = false;
    geo::editable_mesh_t repaired{};
    const auto report = geo::Repair_TryPreviewMesh( &geo::GeometryDocument_FindMesh( &map->geometry, { meshId } )->mesh, plan, rebuild, map->pAllocator, &repaired, nullptr );
    if ( report.status != OK ) { geo::EditableMesh_Shutdown( &repaired ); return Status( report.status ); }
    // The repaired polygons, as plain loops.
    std::vector<vec3> points;
    std::vector<std::vector<u32>> faces;
    std::unordered_map<u32, u32> index;
    ( void )GenerationPool_ForEach( &repaired.vertices, [&]( geo::geometry_mesh_vertex_handle_t h, const geo::mesh_vertex_record_t &v ) noexcept -> bool_t {
        index[h.nSlot] = static_cast<u32>( points.size() ); points.push_back( v.position ); return CY_TRUE;
    } );
    ( void )GenerationPool_ForEach( &repaired.faces, [&]( geo::geometry_mesh_face_handle_t, const geo::mesh_face_record_t &face ) noexcept -> bool_t {
        const auto *loop = geo::EditableMesh_GetLoop( &repaired, face.hOuterLoop );
        std::vector<u32> corners;
        auto edge = loop != nullptr ? loop->hFirstHalfEdge : geo::geometry_mesh_half_edge_handle_t{};
        for ( u32 c = 0; loop != nullptr && c < loop->cHalfEdges; ++c ) {
            const auto *record = geo::EditableMesh_GetHalfEdge( &repaired, edge );
            if ( record == nullptr ) { break; }
            corners.push_back( index[record->hOrigin.nSlot] ); edge = record->hNext;
        }
        if ( corners.size() >= 3 ) { faces.push_back( std::move( corners ) ); }
        return CY_TRUE;
    } );
    geo::EditableMesh_Shutdown( &repaired );
    result = MapCreate_ReplaceMeshPolygons( map, meshId, points, faces, material, worldUnitsPerUv );
    if ( result == map_status_t::OK && pReportOut != nullptr ) {
        *pReportOut = { report.cVerticesWelded, report.cDegenerateFacesRemoved, report.cDuplicateFacesRemoved, report.cFacesFlipped, report.cHolesFilled,
                        report.cHolesLeftOpen, report.cNonManifoldEdges };
    }
    return result;
}

bool_t MapTopologyEdit_HasFaceRecords( map_document_t *map, u64 meshId ) noexcept
{
    const auto *record = map != nullptr ? MapDocument_FindObject( map, meshId, nullptr ) : nullptr;
    return KeyValue_ChildCount( KeyValue_Find( record, StringView_FromCString( "faces" ) ) ) != 0 ? CY_TRUE : CY_FALSE;
}

map_status_t MapTopologyEdit_Subdivide( map_document_t *map, span_t<const u64> meshes ) noexcept
{
    auto result = CheckMeshes( map, meshes );
    if ( result != map_status_t::OK ) { return result; }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ||
         map->geometry.revision == CY_U64_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    for ( usize i = 0; i < meshes.nCount; ++i ) {
        if ( MapTopologyEdit_HasFaceRecords( map, meshes.pData[i] ) ) { return map_status_t::INVALID_ARGUMENT; }
    }
    geo::subdivision_descriptor_t descriptor{};
    descriptor.scheme = geo::subdivision_scheme_t::CATMULL_CLARK;
    descriptor.cLevels = 1;
    descriptor.boundary = geo::subdivision_boundary_t::PIN_CORNERS;
    descriptor.bHardEdgesAreCreases = true; // Authored hard edges stay sharp.
    geo::geometry_source_id_allocator_t ids{ { std::max( map->nextId, map->geometry.sourceIds.allocator.next.value ) } };
    for ( usize i = 0; result == map_status_t::OK && i < meshes.nCount; ++i ) {
        geo::mesh_source_t smooth{};
        auto status = geo::Subdivision_TryCollapse( geo::GeometryDocument_FindMesh( &map->geometry, { meshes.pData[i] } ),
            descriptor, map->pAllocator, &ids, &smooth );
        if ( status == OK ) { status = geo::GeometryDocument_TryReplaceMesh( &map->geometry, &smooth ); }
        geo::MeshSource_Shutdown( &smooth );
        result = Status( status );
    }
    if ( result == map_status_t::OK ) {
        map->nextId = std::max( map->nextId, ids.next.value );
        ++map->geometry.revision;
    }
    return result;
}

map_status_t MapTopologyEdit_Solidify( map_document_t *map, span_t<const u64> meshes, f64 thickness ) noexcept
{
    auto result = CheckMeshes( map, meshes );
    if ( result != map_status_t::OK ) { return result; }
    if ( !std::isfinite( thickness ) || thickness == 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ||
         map->geometry.revision == CY_U64_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    // Fresh IDs start above both high-water marks, as the brush conversion does.
    geo::geometry_source_id_allocator_t ids{ { std::max( map->nextId, map->geometry.sourceIds.allocator.next.value ) } };
    for ( usize i = 0; result == map_status_t::OK && i < meshes.nCount; ++i ) {
        geo::mesh_source_t solid{};
        auto status = geo::MeshSolidify_TryBuild( geo::GeometryDocument_FindMesh( &map->geometry, { meshes.pData[i] } ),
            map->pAllocator, &ids, map->geometryPolicy, thickness, &solid );
        // The stationary layer keeps every face ID, so the record's residual
        // face data stays attached without a lineage walk.
        if ( status == OK ) { status = geo::GeometryDocument_TryReplaceMesh( &map->geometry, &solid ); }
        geo::MeshSource_Shutdown( &solid );
        result = Status( status );
    }
    if ( result == map_status_t::OK ) {
        map->nextId = std::max( map->nextId, ids.next.value );
        ++map->geometry.revision;
    }
    return result;
}

map_status_t MapTopologyEdit_JoinMeshes( map_document_t *map, span_t<const u64> meshes, u64 *pRetainedOut ) noexcept
{
    auto result = CheckMeshes( map, meshes );
    if ( result != map_status_t::OK ) { return result; }
    if ( meshes.nCount < 2 || pRetainedOut == nullptr ) { return map_status_t::INVALID_ARGUMENT; }
    const auto *first = Placement( map, meshes.pData[0] );
    u64 retained = 0;
    bool recorded = false;
    for ( usize i = 0; i < meshes.nCount; ++i ) {
        const u64 id = meshes.pData[i];
        const auto *placement = Placement( map, id );
        // Tied geometry belongs to its entity; joining across owners would
        // silently move geometry between entities.
        if ( ( placement == nullptr ) != ( first == nullptr ) || ( placement != nullptr && placement->owner != first->owner ) ) { return map_status_t::INVALID_ARGUMENT; }
        const bool hasRecord = MapDocument_FindObject( map, id, nullptr ) != nullptr;
        if ( retained == 0 || ( hasRecord && !recorded ) || ( hasRecord == recorded && id < retained ) ) { retained = id; recorded = hasRecord; }
    }
    std::vector<const geo::mesh_source_t *> inputs;
    for ( usize i = 0; i < meshes.nCount; ++i ) { inputs.push_back( geo::GeometryDocument_FindMesh( &map->geometry, { meshes.pData[i] } ) ); }
    geo::mesh_source_t joined{};
    face_records_t donors{};
    auto status = geo::MeshSource_TryJoinExact( { inputs.data(), inputs.size() }, { retained }, map->pAllocator, &joined );
    if ( status == OK && !donors.Init( map->pAllocator ) ) { status = geo::geometry_status_t::ALLOCATION_FAILED; }
    for ( usize i = 0; status == OK && i < meshes.nCount; ++i ) {
        if ( meshes.pData[i] == retained ) { continue; }
        const auto *faces = KeyValue_Find( MapDocument_FindObject( map, meshes.pData[i], nullptr ), StringView_FromCString( "faces" ) );
        for ( usize k = 0; status == OK && k < KeyValue_ChildCount( faces ); ++k ) {
            if ( !KeyValue_CloneInto( donors.document, donors.faces, KeyValue_ChildAt( faces, k ) ) ) { status = geo::geometry_status_t::ALLOCATION_FAILED; }
        }
    }
    result = Status( status );
    // Donors go first; the joined mesh then brings their retired vertex and
    // face identities back under the survivor (as undo restores them).
    for ( usize i = 0; result == map_status_t::OK && i < meshes.nCount; ++i ) {
        if ( meshes.pData[i] != retained ) { result = MapDocument_RemoveObject( map, meshes.pData[i] ); }
    }
    if ( result == map_status_t::OK ) { result = Status( geo::GeometryDocument_TryReplaceMesh( &map->geometry, &joined ) ); }
    if ( result == map_status_t::OK && recorded && !AppendFaces( map, retained, donors.faces ) ) { result = map_status_t::OUT_OF_MEMORY; }
    geo::MeshSource_Shutdown( &joined );
    if ( result == map_status_t::OK ) { ++map->geometry.revision; *pRetainedOut = retained; }
    return result;
}

map_status_t MapTopologyEdit_SeparateShells( map_document_t *map, u64 meshId, vector_t<u64> *pRootsOut ) noexcept
{
    auto result = CheckMeshes( map, { &meshId, 1 } );
    if ( result != map_status_t::OK ) { return result; }
    if ( pRootsOut == nullptr || !Vector_IsValid( pRootsOut ) ) { return map_status_t::INVALID_ARGUMENT; }
    const auto *source = geo::GeometryDocument_FindMesh( &map->geometry, { meshId } );
    if ( GenerationPool_Count( &source->mesh.shells ) < 2 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    description_t mesh;
    auto status = mesh.Read( source );
    if ( status != OK ) { return Status( status ); }
    const id_t keep = mesh.value.faces.pData[0].sourceId; // Described faces are sorted by ID.
    const auto *placement = Placement( map, meshId );
    const u64 owner = placement != nullptr ? placement->owner : 0;
    const string_view_t layer = placement != nullptr && placement->iLayer < map->layers.nCount
        ? StringView_FromCString( map->layers.pData[placement->iLayer].id ) : string_view_t{};
    // The face records of the source, cloned before its record changes.
    face_records_t records{};
    if ( !records.Init( map->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    const auto *faces = KeyValue_Find( MapDocument_FindObject( map, meshId, nullptr ), StringView_FromCString( "faces" ) );
    for ( usize k = 0; k < KeyValue_ChildCount( faces ); ++k ) {
        if ( !KeyValue_CloneInto( records.document, records.faces, KeyValue_ChildAt( faces, k ) ) ) { return map_status_t::OUT_OF_MEMORY; }
    }
    // New roots must clear map-only IDs as well as geometry ones.
    if ( map->nextId > 1 ) {
        status = geo::GeometrySourceIdAllocator_AdvancePast( &map->geometry.sourceIds.allocator, { map->nextId - 1 } );
        if ( status != OK ) { return Status( status ); }
    }
    geo::geometry_mesh_set_delta_t delta{};
    geo::mesh_object_command_report_t report{};
    geo::geometry_revision_t revision{};
    status = geo::GeometryMeshSetDelta_Init( &delta, map->pAllocator );
    if ( status == OK ) { status = geo::GeometryMeshObjectCommand_TrySeparateByShell( &map->geometry, { meshId }, keep, &delta, &report, &revision ); }
    geo::GeometryMeshSetDelta_Shutdown( &delta );
    result = Status( status );
    vector_t<u64> roots{};
    if ( result == map_status_t::OK && ( !Vector_Init( &roots, map->pAllocator, report.cNewRoots + 1u ) || !Vector_PushBack( &roots, meshId ) ) ) { result = map_status_t::OUT_OF_MEMORY; }
    for ( u32 i = 0; result == map_status_t::OK && i < report.cNewRoots; ++i ) {
        const u64 root = report.firstNewRootId.value + i;
        if ( !Vector_PushBack( &roots, root ) ) { result = map_status_t::OUT_OF_MEMORY; break; }
        if ( layer.cchLength != 0 ) { result = MapDocument_SetGeometryLayer( map, root, layer ); }
        if ( result == map_status_t::OK && owner != 0 ) { result = MapDocument_SetGeometryOwner( map, root, owner ); }
        if ( result != map_status_t::OK ) { break; }
        // Records of faces that moved go with them into a record of their own.
        const auto *moved = geo::GeometryDocument_FindMesh( &map->geometry, { root } );
        face_records_t own{};
        if ( moved == nullptr || !own.Init( map->pAllocator ) ) { result = map_status_t::OUT_OF_MEMORY; break; }
        for ( usize k = 0; k < KeyValue_ChildCount( records.faces ); ++k ) {
            const auto *face = KeyValue_ChildAt( records.faces, k );
            geo::geometry_mesh_face_handle_t handle{};
            if ( geo::MeshSource_TryFindFace( moved, { FaceRecordId( face ) }, &handle ) && !KeyValue_CloneInto( own.document, own.faces, face ) ) { result = map_status_t::OUT_OF_MEMORY; }
        }
        if ( result != map_status_t::OK || KeyValue_ChildCount( own.faces ) == 0 ) { continue; }
        auto *rootId = KeyValue_ObjectInsert( own.document, KeyValue_Root( own.document ), StringView_FromCString( "id" ), key_value_type_t::U64 );
        if ( rootId == nullptr || !KeyValue_SetU64( own.document, rootId, root ) ) { result = map_status_t::OUT_OF_MEMORY; break; }
        result = MapDocument_InsertGeometryRecord( map, map_geometry_kind_t::MESH, KeyValue_Root( own.document ) );
    }
    if ( result == map_status_t::OK ) {
        const u64 next = map->geometry.sourceIds.allocator.next.value;
        map->nextId = next == 0 ? 0 : std::max( map->nextId, next );
        ++map->geometry.revision;
        Vector_Shutdown( pRootsOut ); Vector_Move( pRootsOut, &roots );
    }
    Vector_Shutdown( &roots );
    return result;
}

namespace
{
key_value_t *ParentOf( key_value_t *root, const key_value_t *child, usize depth = 0 ) noexcept
{
    if ( depth >= CY_KEY_VALUE_MAX_DEPTH ) { return nullptr; }
    for ( usize i = 0; i < KeyValue_ChildCount( root ); ++i ) {
        auto *item = KeyValue_ChildAt( root, i );
        if ( item == child ) { return root; }
        if ( auto *found = ParentOf( item, child, depth + 1 ) ) { return found; }
    }
    return nullptr;
}

// Fits a planar projection (orthonormal axes, per-axis scale) to a face's
// corner UVs by least squares; false when they do not determine one.
bool FitProjection( const std::vector<vec3> &points, const std::vector<math::vec2d_t> &uvs, vec3 normal,
    const geo::geometry_policy_t &policy, geo::geometry_brush_side_attributes_t &out ) noexcept
{
    if ( points.size() < 3 ) { return false; }
    vec3 e1{};
    for ( usize i = 1; i < points.size() && !Unit( Sub( points[i], points[0] ), e1 ); ++i ) {}
    if ( Length( e1 ) < 0.5 ) { return false; }
    const vec3 e2 = Cross( normal, e1 );
    // A = D X^T (X X^T)^-1 over the corners relative to corner 0.
    f64 xx = 0, xy = 0, yy = 0, ux = 0, uy = 0, vx = 0, vy = 0;
    for ( usize i = 1; i < points.size(); ++i ) {
        const f64 x = Dot( Sub( points[i], points[0] ), e1 ), y = Dot( Sub( points[i], points[0] ), e2 );
        const f64 du = uvs[i].x - uvs[0].x, dv = uvs[i].y - uvs[0].y;
        xx += x * x; xy += x * y; yy += y * y; ux += du * x; uy += du * y; vx += dv * x; vy += dv * y;
    }
    const f64 det = xx * yy - xy * xy;
    if ( std::fabs( det ) < 1e-12 ) { return false; }
    const f64 r00 = ( ux * yy - uy * xy ) / det, r01 = ( uy * xx - ux * xy ) / det;
    const f64 r10 = ( vx * yy - vy * xy ) / det, r11 = ( vy * xx - vx * xy ) / det;
    const vec3 uRaw = Add( Mul( e1, r00 ), Mul( e2, r01 ) ), vRaw = Add( Mul( e1, r10 ), Mul( e2, r11 ) );
    vec3 u{}, v{};
    if ( !Unit( uRaw, u ) || !Unit( Sub( vRaw, Mul( u, Dot( vRaw, u ) ) ), v ) ) { return false; }
    const f64 su = 1.0 / Length( uRaw ), sv = 1.0 / Dot( vRaw, v );
    if ( !std::isfinite( su ) || !std::isfinite( sv ) ) { return false; }
    auto &m = out.uvProjection;
    m.origin = {}; m.uAxis = u; m.vAxis = v; m.normal = normal; m.worldUnitsPerUv = { su, sv }; m.rotationRadians = 0.0;
    m.offset = { uvs[0].x - Dot( points[0], u ) / su, uvs[0].y - Dot( points[0], v ) / sv };
    return geo::BrushSideAttributes_Validate( policy.numerical, out ) == OK;
}

map_status_t MeshToBrush( map_document_t *map, u64 id ) noexcept
{
    if ( MapTopologyEdit_HasFaceRecords( map, id ) ) { return map_status_t::INVALID_ARGUMENT; }
    const auto *source = geo::GeometryDocument_FindMesh( &map->geometry, { id } );
    if ( source == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    description_t mesh;
    auto status = mesh.Read( source );
    geo::geometry_source_id_allocator_t ids{ { std::max( map->nextId, map->geometry.sourceIds.allocator.next.value ) } };
    geo::brush_source_t brush{};
    if ( status == OK ) { status = geo::MeshToBrush_TryConvert( &source->mesh, &brush.solid, map->pAllocator, map->geometryPolicy, &ids ); }
    if ( status == OK ) { status = geo::BrushSideAttributeStore_Init( &brush.attributes, map->pAllocator ); }
    // Each side takes the surfacing of the face lying in its plane.
    for ( usize i = 0; status == OK && i < brush.solid.sides.nCount; ++i ) {
        auto &side = brush.solid.sides.pData[i];
        geo::geometry_brush_side_attributes_t attributes = geo::BrushSideAttributes_MakeDefault();
        bool fitted = false;
        for ( usize f = 0; f < mesh.value.faces.nCount && !fitted; ++f ) {
            const auto &face = mesh.value.faces.pData[f];
            vec3 normal{}, center{};
            mesh.Frame( face, normal, center );
            if ( !Unit( normal, normal ) || Dot( normal, side.plane.normal ) < 0.9999 ||
                 std::fabs( Dot( side.plane.normal, center ) + side.plane.d ) > 1e-3 ) { continue; }
            attributes.material = face.attributes.material;
            std::vector<vec3> points; std::vector<math::vec2d_t> uvs;
            for ( u32 k = 0; k < face.cCorners; ++k ) {
                points.push_back( mesh.Position( mesh.CornerVertex( face, k ) ) );
                uvs.push_back( mesh.value.corners.pData[face.iFirstCorner + k].attributes.uv0 );
            }
            if ( !FitProjection( points, uvs, side.plane.normal, map->geometryPolicy, attributes ) ) {
                // World-aligned at the default repeat when UVs cannot be fitted.
                const auto material = attributes.material;
                attributes = geo::BrushSideAttributes_MakeDefault(); attributes.material = material;
                const f64 x = std::fabs( normal.x ), y = std::fabs( normal.y ), z = std::fabs( normal.z );
                const vec3 axis = z >= x && z >= y ? vec3{ 0, 0, normal.z < 0 ? -1.0 : 1.0 } : x >= y ? vec3{ normal.x < 0 ? -1.0 : 1.0, 0, 0 } : vec3{ 0, normal.y < 0 ? -1.0 : 1.0, 0 };
                ( void )geo::UvAlign_Reset( &attributes, axis );
                attributes.uvProjection.worldUnitsPerUv = { 128.0, 128.0 };
            }
            fitted = true;
        }
        usize record = 0;
        status = geo::BrushSideAttributeStore_TryAppend( &brush.attributes, map->geometryPolicy, attributes, &record );
        side.iAttributeIndex = static_cast<u32>( record );
    }
    brush.solid.sourceId = { id }; // The root survives, as brush-to-mesh keeps it.
    // The record moves from meshes to brushes in its container.
    map_layer_store_t *layerStore = nullptr;
    key_value_t *record = status == OK ? MapDocument_FindObject( map, id, &layerStore ) : nullptr;
    key_value_document_t *copy = nullptr;
    key_value_t *parent = nullptr, *container = nullptr;
    auto result = Status( status );
    if ( result == map_status_t::OK && record != nullptr ) {
        parent = ParentOf( KeyValue_Root( layerStore->store.pDocument ), record );
        container = parent != nullptr ? ParentOf( KeyValue_Root( layerStore->store.pDocument ), parent ) : nullptr;
        key_value_document_desc_t desc{}; desc.pAllocator = map->pAllocator;
        copy = KeyValue_CreateDocument( desc );
        if ( parent == nullptr || container == nullptr || copy == nullptr || !KeyValue_SetRootType( copy, key_value_type_t::OBJECT ) ) { result = map_status_t::GEOMETRY_FAILED; }
        for ( usize i = 0; result == map_status_t::OK && i < KeyValue_ChildCount( record ); ++i ) {
            if ( !KeyValue_CloneInto( copy, KeyValue_Root( copy ), KeyValue_ChildAt( record, i ) ) ) { result = map_status_t::OUT_OF_MEMORY; }
        }
        if ( result == map_status_t::OK && ( !MapGeometry_StripRecord( map_geometry_kind_t::MESH, copy, KeyValue_Root( copy ) ) ||
             !MapGeometry_StripRecord( map_geometry_kind_t::BRUSH, copy, KeyValue_Root( copy ) ) ) ) { result = map_status_t::OUT_OF_MEMORY; }
    }
    if ( result == map_status_t::OK ) { result = Status( geo::GeometryDocument_TryRemoveMesh( &map->geometry, { id } ) ); }
    if ( result == map_status_t::OK ) { result = Status( geo::GeometryDocument_TryAddBrushSource( &map->geometry, &brush ) ); }
    if ( result == map_status_t::OK && record != nullptr ) {
        auto *doc = layerStore->store.pDocument;
        auto *brushes = KeyValue_Find( container, StringView_FromCString( "brushes" ) );
        if ( brushes == nullptr ) { brushes = KeyValue_ObjectInsert( doc, container, StringView_FromCString( "brushes" ), key_value_type_t::ARRAY ); }
        if ( brushes == nullptr || !KeyValue_CloneInto( doc, brushes, KeyValue_Root( copy ) ) || !KeyValue_Remove( doc, parent, record ) ) { result = map_status_t::OUT_OF_MEMORY; }
    }
    KeyValue_DestroyDocument( copy );
    geo::BrushSource_Shutdown( &brush );
    if ( result == map_status_t::OK ) { map->nextId = std::max( map->nextId, ids.next.value ); }
    return result;
}

map_status_t ToMeshes( map_document_t *map, span_t<const u64> brushes, vector_t<u64> *rootsOut ) noexcept
{
    return MapMeshEdit_ConvertBrushes( map, brushes, rootsOut );
}

map_status_t Roots( map_document_t *map, vector_t<u64> &roots ) noexcept
{
    return Vector_Init( &roots, map->pAllocator ) ? map_status_t::OK : map_status_t::OUT_OF_MEMORY;
}
} // namespace

map_status_t MapTopologyEdit_MeshesToBrushes( map_document_t *map, span_t<const u64> meshes ) noexcept
{
    auto result = CheckMeshes( map, meshes );
    if ( result != map_status_t::OK ) { return result; }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    for ( usize i = 0; result == map_status_t::OK && i < meshes.nCount; ++i ) { result = MeshToBrush( map, meshes.pData[i] ); }
    if ( result == map_status_t::OK ) { ++map->geometry.revision; }
    return result;
}

map_status_t MapTopologyEdit_Boolean( map_document_t *map, span_t<const u64> meshes, map_boolean_t op, vector_t<u64> *pRootsOut ) noexcept
{
    auto result = CheckMeshes( map, meshes );
    if ( result != map_status_t::OK ) { return result; }
    if ( pRootsOut == nullptr || !Vector_IsValid( pRootsOut ) || op > map_boolean_t::SPLIT || meshes.nCount < 2 ||
         ( op != map_boolean_t::UNION && meshes.nCount != 2 ) ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ||
         map->geometry.revision == CY_U64_MAX ) { return map_status_t::LIMIT_EXCEEDED; }
    std::vector<u64> ids( meshes.pData, meshes.pData + meshes.nCount );
    std::sort( ids.begin(), ids.end() );
    // Fresh IDs the CSG hands out must not collide with map-only objects.
    if ( map->nextId > 1 && geo::GeometrySourceIdAllocator_AdvancePast( &map->geometry.sourceIds.allocator, { map->nextId - 1 } ) != OK ) {
        return map_status_t::LIMIT_EXCEEDED;
    }
    std::vector<u64> roots;
    geo::csg_diagnostics_t diagnostics{};
    if ( op == map_boolean_t::SPLIT ) {
        geo::csg_mesh_options_t options{};
        geo::geometry_source_id_t inside{}, outside{};
        result = Status( geo::GeometryDocument_TryCsgSplitMesh( &map->geometry, { ids[0] }, { ids[1] }, options, &inside, &outside, &diagnostics ) );
        if ( result == map_status_t::OK && geo::GeometrySourceId_IsValid( inside ) ) { roots.push_back( inside.value ); }
        if ( result == map_status_t::OK && geo::GeometrySourceId_IsValid( outside ) ) { roots.push_back( outside.value ); }
    } else if ( op == map_boolean_t::EXCLUSIVE ) {
        // (A - B) and (B - A) as two objects: one mesh holding both would
        // be non-manifold wherever the pieces touch along an edge. A copy of
        // A cuts B, so B is cut by A as it was.
        vector_t<u64> copies{};
        if ( !Vector_Init( &copies, map->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
        result = MapEdit_Duplicate( map, { &ids[0], 1u }, {}, &copies );
        const u64 copyOfA = result == map_status_t::OK && copies.nCount == 1 ? copies.pData[0] : 0u;
        Vector_Shutdown( &copies );
        if ( result == map_status_t::OK && copyOfA == 0u ) { result = map_status_t::GEOMETRY_FAILED; }
        const auto cut = [&]( u64 target, u64 tool ) {
            geo::csg_document_options_t options{};
            options.mesh.op = geo::csg_operator_t::DIFFERENCE;
            options.bKeepB = true;
            geo::geometry_source_id_t resultId{};
            const auto status = Status( geo::GeometryDocument_TryCsgMeshes( &map->geometry, { target }, { tool }, options, &resultId, &diagnostics ) );
            if ( status == map_status_t::OK && geo::GeometrySourceId_IsValid( resultId ) ) { roots.push_back( resultId.value ); }
            return status;
        };
        if ( result == map_status_t::OK ) { result = cut( ids[0], ids[1] ); }
        if ( result == map_status_t::OK ) { result = cut( ids[1], copyOfA ); }
        if ( result == map_status_t::OK ) { result = MapDocument_RemoveObject( map, copyOfA ); }
        if ( result == map_status_t::OK && roots.empty() ) { result = map_status_t::GEOMETRY_FAILED; } // Nothing left of either.
    } else {
        // A takes each B in turn; B is kept by the CSG and removed through the
        // map so its record goes too.
        u64 current = ids[0];
        for ( usize i = 1; result == map_status_t::OK && i < ids.size(); ++i ) {
            geo::csg_document_options_t options{};
            options.mesh.op = op == map_boolean_t::UNION ? geo::csg_operator_t::UNION : op == map_boolean_t::SUBTRACT ? geo::csg_operator_t::DIFFERENCE :
                              op == map_boolean_t::INTERSECT ? geo::csg_operator_t::INTERSECTION : geo::csg_operator_t::SYMMETRIC_DIFFERENCE;
            options.bKeepB = true;
            geo::geometry_source_id_t resultId{};
            result = Status( geo::GeometryDocument_TryCsgMeshes( &map->geometry, { current }, { ids[i] }, options, &resultId, &diagnostics ) );
            if ( result == map_status_t::OK ) { result = MapDocument_RemoveObject( map, ids[i] ); }
            if ( result == map_status_t::OK && !geo::GeometrySourceId_IsValid( resultId ) ) { result = map_status_t::GEOMETRY_FAILED; } // Nothing left.
            if ( result == map_status_t::OK ) { current = resultId.value; }
        }
        if ( result == map_status_t::OK ) { roots.push_back( current ); }
    }
    if ( result != map_status_t::OK ) { return result; }
    map->nextId = std::max( map->nextId, map->geometry.sourceIds.allocator.next.value );
    ++map->geometry.revision;
    Vector_Clear( pRootsOut );
    for ( u64 root : roots ) { if ( !Vector_PushBack( pRootsOut, root ) ) { return map_status_t::OUT_OF_MEMORY; } }
    return map_status_t::OK;
}

map_status_t MapTopologyEdit_Mirror( map_document_t *map, span_t<const u64> ids, math::planed_t plane, bool textureLock ) noexcept
{
    if ( map == nullptr || !Span_IsValid( ids ) || ids.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    vec3 normal{};
    if ( !Unit( plane.normal, normal ) || std::fabs( Length( plane.normal ) - 1.0 ) > 1e-9 || !std::isfinite( plane.d ) ) { return map_status_t::INVALID_ARGUMENT; }
    const auto reflection = Reflection( plane );
    for ( usize i = 0; i < ids.nCount; ++i ) {
        const u64 id = ids.pData[i];
        for ( usize k = 0; k < i; ++k ) { if ( ids.pData[k] == id ) { return map_status_t::INVALID_ARGUMENT; } }
        const geo::geometry_source_id_t sid{ id };
        geo::geometry_status_t status = geo::geometry_status_t::INVALID_ARGUMENT;
        if ( geo::GeometryDocument_FindBrush( &map->geometry, sid ) != nullptr ) {
            geo::brush_source_t copy{};
            status = geo::GeometryDocument_TryCopyBrushSource( &map->geometry, sid, map->pAllocator, &copy );
            if ( status == OK ) { status = geo::BrushAuthoring_TryMirror( &copy, plane, textureLock, map->geometryPolicy ); }
            if ( status == OK ) { status = geo::GeometryDocument_TryReplaceBrushSourcesExact( &map->geometry, { &sid, 1 }, { &copy, 1 } ); }
            geo::BrushSource_Shutdown( &copy );
        } else if ( geo::GeometryDocument_FindMesh( &map->geometry, sid ) != nullptr ) {
            const auto result = ApplyMesh( map, id, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
                return geo::MeshSourceEdit_TryMirror( working, plane, report );
            } );
            if ( result != map_status_t::OK ) { return result; }
            continue;
        } else if ( const auto *patch = geo::GeometryDocument_FindPatch( &map->geometry, sid ) ) {
            // A reflection turns the surface inside out; reversing the
            // columns turns its front face back toward the same side.
            geo::patch_surface_t copy{};
            status = geo::Patch_TryClone( patch, map->pAllocator, &copy );
            if ( status == OK ) { status = geo::Patch_TryTransform( &copy, reflection ); }
            if ( status == OK ) { geo::Patch_ReverseColumns( &copy ); status = geo::GeometryDocument_TryReplacePatch( &map->geometry, &copy ); }
            geo::Patch_Shutdown( &copy );
        } else {
            return geo::GeometryDocument_FindHeightField( &map->geometry, sid ) != nullptr || MapDocument_FindObject( map, id, nullptr ) != nullptr
                ? map_status_t::INVALID_ARGUMENT : map_status_t::UNKNOWN_OBJECT;
        }
        if ( status != OK ) { return Status( status ); }
    }
    ++map->geometry.revision;
    return map_status_t::OK;
}

/*
================
Cleanup, component offset and flatten, loop slide, face deletion
================
*/

namespace
{
using weld_fn = geo::geometry_status_t ( * )( geo::mesh_source_t *, f64, u32 *, geo::mesh_edit_report_t * ) noexcept;

map_status_t WeldEach( map_document_t *map, span_t<const u64> meshes, f64 tolerance, usize *pMergedOut, weld_fn weld ) noexcept
{
    if ( pMergedOut != nullptr ) { *pMergedOut = 0; }
    auto result = CheckMeshes( map, meshes );
    if ( result != map_status_t::OK ) { return result; }
    if ( !std::isfinite( tolerance ) || tolerance <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    usize total = 0;
    for ( usize i = 0; result == map_status_t::OK && i < meshes.nCount; ++i ) {
        // A dry count first: a mesh with nothing to weld keeps its revision.
        const auto *source = geo::GeometryDocument_FindMesh( &map->geometry, { meshes.pData[i] } );
        geo::mesh_source_t probe{};
        u32 merged = 0;
        auto status = geo::MeshSource_TryClone( source, map->pAllocator, &probe );
        if ( status == OK ) { status = weld( &probe, tolerance, &merged, nullptr ); }
        geo::MeshSource_Shutdown( &probe );
        if ( status != OK ) { return Status( status ); }
        if ( merged == 0 ) { continue; }
        result = ApplyMesh( map, meshes.pData[i], [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
            return weld( working, tolerance, nullptr, report );
        } );
        total += merged;
    }
    if ( result == map_status_t::OK && pMergedOut != nullptr ) { *pMergedOut = total; }
    return result;
}

// A component set as the geometry library's selection: faces if any, else
// edges, else vertices.
struct component_set_t {
    geo::mesh_selection_t value{};
    geo::mesh_selection_mode_t mode{ geo::mesh_selection_mode_t::VERTEX };
    ~component_set_t() { geo::MeshSelection_Shutdown( &value ); }
    geo::geometry_status_t Build( const allocator_t *allocator, u64 meshId, const map_mesh_components_t &c ) noexcept
    {
        auto status = geo::MeshSelection_Init( &value, allocator, { meshId } );
        for ( usize i = 0; status == OK && i < c.vertices.nCount; ++i ) { status = geo::MeshSelection_TryAddVertex( &value, c.vertices.pData[i] ); }
        for ( usize i = 0; status == OK && i < c.edges.nCount; ++i ) { status = geo::MeshSelection_TryAddEdge( &value, c.edges.pData[i] ); }
        for ( usize i = 0; status == OK && i < c.faces.nCount; ++i ) { status = geo::MeshSelection_TryAddFace( &value, c.faces.pData[i] ); }
        mode = c.faces.nCount != 0 ? geo::mesh_selection_mode_t::FACE : c.edges.nCount != 0 ? geo::mesh_selection_mode_t::EDGE : geo::mesh_selection_mode_t::VERTEX;
        return status;
    }
};

bool ValidComponents( const map_mesh_components_t &c ) noexcept
{
    if ( !Span_IsValid( c.vertices ) || !Span_IsValid( c.edges ) || !Span_IsValid( c.faces ) ) { return false; }
    return c.vertices.nCount + c.edges.nCount + c.faces.nCount != 0 && ( c.edges.nCount == 0 || ValidEdges( c.edges ) );
}
} // namespace

map_status_t MapTopologyEdit_Weld( map_document_t *map, span_t<const u64> meshes, f64 tolerance, usize *pMergedOut ) noexcept
{
    return WeldEach( map, meshes, tolerance, pMergedOut, &geo::MeshSourceEdit_TryWeld );
}

map_status_t MapTopologyEdit_MergeByDistance( map_document_t *map, span_t<const u64> meshes, f64 tolerance, usize *pMergedOut ) noexcept
{
    return WeldEach( map, meshes, tolerance, pMergedOut, &geo::MeshSourceEdit_TryMergeByDistance );
}

map_status_t MapTopologyEdit_OffsetComponents( map_document_t *map, u64 meshId, const map_mesh_components_t &components, f64 distance ) noexcept
{
    if ( !ValidComponents( components ) || !std::isfinite( distance ) || distance == 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    component_set_t set{};
    if ( map == nullptr || set.Build( map->pAllocator, meshId, components ) != OK ) { return map == nullptr ? map_status_t::INVALID_ARGUMENT : map_status_t::OUT_OF_MEMORY; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        return geo::MeshSourceEdit_TryOffsetComponents( working, &set.value, set.mode, distance );
    } );
}

map_status_t MapTopologyEdit_FlattenComponents( map_document_t *map, u64 meshId, const map_mesh_components_t &components, map_flatten_t plane ) noexcept
{
    if ( !ValidComponents( components ) || plane > map_flatten_t::AXIS_Z ) { return map_status_t::INVALID_ARGUMENT; }
    component_set_t set{};
    if ( map == nullptr || set.Build( map->pAllocator, meshId, components ) != OK ) { return map == nullptr ? map_status_t::INVALID_ARGUMENT : map_status_t::OUT_OF_MEMORY; }
    // map_flatten_t mirrors mesh_flatten_plane_t value for value.
    const auto how = static_cast<geo::mesh_flatten_plane_t>( plane );
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        return geo::MeshSourceEdit_TryFlattenComponents( working, &set.value, set.mode, how, nullptr );
    } );
}

map_status_t MapTopologyEdit_SlideEdgeLoop( map_document_t *map, u64 meshId, geo::mesh_edge_ref_t directed, f64 factor ) noexcept
{
    if ( directed.a.value == 0 || directed.b.value == 0 || directed.a.value == directed.b.value || !std::isfinite( factor ) ||
         factor == 0.0 || factor <= -1.0 || factor >= 1.0 ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        return geo::MeshSourceEdit_TrySlideEdgeLoop( working, directed.a, directed.b, factor, nullptr );
    } );
}

map_status_t MapTopologyEdit_DeleteFaces( map_document_t *map, u64 meshId, span_t<const u64> faceIds ) noexcept
{
    if ( !Span_IsValid( faceIds ) || faceIds.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    std::vector<id_t> ids;
    for ( usize i = 0; i < faceIds.nCount; ++i ) {
        if ( faceIds.pData[i] == 0 ) { return map_status_t::INVALID_ARGUMENT; }
        ids.push_back( { faceIds.pData[i] } );
    }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        return geo::MeshSourceEdit_TryDeleteFaces( working, { ids.data(), ids.size() }, report );
    } );
}

map_status_t MapTopologyEdit_BridgeFaces( map_document_t *map, u64 meshId, u64 faceA, u64 faceB, u32 cSegments ) noexcept
{
    if ( faceA == 0 || faceB == 0 || faceA == faceB || cSegments == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        return geo::MeshSourceEdit_TryBridgeFaces( working, { faceA }, { faceB }, cSegments, nullptr, report );
    } );
}

map_status_t MapTopologyEdit_ExtrudeBrushFaceToNew( map_document_t *map, u64 brushId, u64 sideId, f64 distance, u64 *pNewOut ) noexcept
{
    if ( map == nullptr || pNewOut == nullptr || sideId == 0 || !std::isfinite( distance ) || distance == 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    if ( geo::GeometryDocument_FindBrush( &map->geometry, { brushId } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    if ( map->nextId == 0 || geo::GeometrySourceIdAllocator_IsExhausted( &map->geometry.sourceIds.allocator ) ) { return map_status_t::LIMIT_EXCEEDED; }
    const auto *placement = Placement( map, brushId );
    const u64 owner = placement != nullptr ? placement->owner : 0;
    const string_view_t layer = placement != nullptr && placement->iLayer < map->layers.nCount
        ? StringView_FromCString( map->layers.pData[placement->iLayer].id ) : string_view_t{};
    geo::brush_source_t copy{}, added{};
    auto status = geo::GeometryDocument_TryCopyBrushSource( &map->geometry, { brushId }, map->pAllocator, &copy );
    usize side = copy.solid.sides.nCount;
    for ( usize i = 0; status == OK && i < copy.solid.sides.nCount; ++i ) { if ( copy.solid.sides.pData[i].sourceId.value == sideId ) { side = i; } }
    if ( status == OK && side == copy.solid.sides.nCount ) { geo::BrushSource_Shutdown( &copy ); return map_status_t::UNKNOWN_OBJECT; }
    geo::geometry_source_id_allocator_t ids{ { std::max( map->nextId, map->geometry.sourceIds.allocator.next.value ) } };
    if ( status == OK ) { status = geo::BrushAuthoring_TryExtrudeSplit( &copy, side, distance, map->pAllocator, map->geometryPolicy, &ids, &added ); }
    // Inward the original keeps the inner part: it changes too.
    if ( status == OK && distance < 0.0 ) { status = geo::GeometryDocument_TryReplaceBrushSourcesExact( &map->geometry, { &copy.solid.sourceId, 1 }, { &copy, 1 } ); }
    if ( status == OK ) {
        status = geo::GeometrySourceIdAllocator_AdvancePast( &map->geometry.sourceIds.allocator, { ids.next.value - 1 } );
    }
    if ( status == OK ) { status = geo::GeometryDocument_TryAddBrushSource( &map->geometry, &added ); }
    const u64 root = added.solid.sourceId.value;
    geo::BrushSource_Shutdown( &added );
    geo::BrushSource_Shutdown( &copy );
    auto result = Status( status );
    if ( result == map_status_t::OK && layer.cchLength != 0 ) { result = MapDocument_SetGeometryLayer( map, root, layer ); }
    if ( result == map_status_t::OK && owner != 0 ) { result = MapDocument_SetGeometryOwner( map, root, owner ); }
    if ( result != map_status_t::OK ) { return result; }
    map->nextId = std::max( map->nextId, ids.next.value );
    ++map->geometry.revision;
    *pNewOut = root;
    return map_status_t::OK;
}

map_status_t MapTopologyEdit_SetVertexPositions( map_document_t *map, u64 meshId, map_vertex_ids_t vertices, span_t<const vec3> positions ) noexcept
{
    if ( !Span_IsValid( vertices ) || !Span_IsValid( positions ) || vertices.nCount == 0 || vertices.nCount != positions.nCount ) { return map_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0; i < positions.nCount; ++i ) {
        const vec3 p = positions.pData[i];
        if ( vertices.pData[i].value == 0 || !std::isfinite( p.x ) || !std::isfinite( p.y ) || !std::isfinite( p.z ) ) { return map_status_t::INVALID_ARGUMENT; }
    }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        for ( usize i = 0; i < vertices.nCount; ++i ) {
            const auto status = geo::MeshSourceEdit_TryMoveVertex( working, vertices.pData[i], positions.pData[i] );
            if ( status != OK ) { return status; }
        }
        return OK;
    } );
}

map_status_t MapTopologyEdit_SnapEdgeToEdge( map_document_t *map, u64 meshId, geo::mesh_edge_ref_t source, geo::mesh_edge_ref_t target ) noexcept
{
    const geo::mesh_edge_ref_t pair[]{ source, target };
    if ( !ValidEdges( { pair, 2 } ) || ( source.a.value == target.a.value && source.b.value == target.b.value ) ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        description_t mesh;
        auto status = mesh.Read( working );
        if ( status != OK ) { return status; }
        const u32 ia = mesh.Vertex( source.a ), ib = mesh.Vertex( source.b ), ta = mesh.Vertex( target.a ), tb = mesh.Vertex( target.b );
        if ( ia == CY_INVALID_INDEX || ib == CY_INVALID_INDEX || ta == CY_INVALID_INDEX || tb == CY_INVALID_INDEX ) { return geo::geometry_status_t::INVALID_HANDLE; }
        const auto at = [&]( u32 v ) { return mesh.value.vertices.pData[v].position; };
        // Pair the ends the shorter way round, so the edge does not twist.
        const f64 straight = Length( Sub( at( ia ), at( ta ) ) ) + Length( Sub( at( ib ), at( tb ) ) );
        const f64 crossed = Length( Sub( at( ia ), at( tb ) ) ) + Length( Sub( at( ib ), at( ta ) ) );
        const id_t onA = straight <= crossed ? target.a : target.b, onB = straight <= crossed ? target.b : target.a;
        // Each source end merges into its target end, which keeps its place;
        // an end the two edges already share stays.
        std::vector<id_t> groups;
        std::vector<u32> sizes;
        if ( source.a.value != onA.value ) { groups.insert( groups.end(), { onA, source.a } ); sizes.push_back( 2 ); }
        if ( source.b.value != onB.value ) { groups.insert( groups.end(), { onB, source.b } ); sizes.push_back( 2 ); }
        if ( sizes.empty() ) { return geo::geometry_status_t::INVALID_ARGUMENT; }
        return geo::MeshSourceEdit_TryMergeVertices( working, { groups.data(), groups.size() }, { sizes.data(), sizes.size() },
            geo::mesh_merge_target_t::FIRST, nullptr, report );
    } );
}

map_status_t MapTopologyEdit_BridgeEdgeChains( map_document_t *map, u64 meshId, map_vertex_ids_t pathA, map_vertex_ids_t pathB, u32 cSegments ) noexcept
{
    if ( !Span_IsValid( pathA ) || !Span_IsValid( pathB ) || pathA.nCount < 2 || pathA.nCount != pathB.nCount || cSegments == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        return geo::MeshSourceEdit_TryBridgeEdgeChains( working, pathA, pathB, cSegments, nullptr, report );
    } );
}

map_status_t MapTopologyEdit_MakeRoom( map_document_t *map, span_t<const u64> brushes, f64 thickness, vector_t<u64> *pRootsOut ) noexcept
{
    if ( map == nullptr || !Span_IsValid( brushes ) || brushes.nCount == 0 || !std::isfinite( thickness ) || thickness <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( map->bReadOnly ) { return map_status_t::READ_ONLY; }
    // Every side moves out by the thickness: the hollow's walls then stand
    // outside the original volume, which becomes the room.
    for ( usize b = 0; b < brushes.nCount; ++b ) {
        if ( geo::GeometryDocument_FindBrush( &map->geometry, { brushes.pData[b] } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
        geo::brush_source_t copy{};
        auto status = geo::GeometryDocument_TryCopyBrushSource( &map->geometry, { brushes.pData[b] }, map->pAllocator, &copy );
        for ( usize i = 0; status == OK && i < copy.solid.sides.nCount; ++i ) { copy.solid.sides.pData[i].plane.d -= thickness; }
        if ( status == OK ) { status = geo::GeometryDocument_TryReplaceBrushSourcesExact( &map->geometry, { &copy.solid.sourceId, 1 }, { &copy, 1 } ); }
        geo::BrushSource_Shutdown( &copy );
        if ( status != OK ) { return Status( status ); }
    }
    ++map->geometry.revision;
    return MapBrushEdit_Hollow( map, brushes, thickness, pRootsOut );
}

/*
================
Vertex dissolve and bevel: built from face deletion, splits and hole filling
================
*/

namespace
{
// The faces around an interior vertex, and one edge of the ring around it
// (the hole's rim once those faces are gone). False on the mesh's border.
bool VertexFan( const geo::mesh_source_t *source, id_t vertex, std::vector<u64> &faces, geo::mesh_edge_ref_t &rim ) noexcept
{
    faces.clear(); rim = {};
    description_t mesh;
    if ( mesh.Read( source ) != OK ) { return false; }
    const u32 v = mesh.Vertex( vertex );
    if ( v == CY_INVALID_INDEX ) { return false; }
    for ( usize f = 0; f < mesh.value.faces.nCount; ++f ) {
        const auto &face = mesh.value.faces.pData[f];
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            if ( mesh.CornerVertex( face, k ) != v ) { continue; }
            const id_t prev = mesh.value.vertices.pData[mesh.CornerVertex( face, k + face.cCorners - 1 )].sourceId;
            const id_t next = mesh.value.vertices.pData[mesh.CornerVertex( face, k + 1 )].sourceId;
            if ( IsBoundary( source, geo::MeshEdgeRef_Make( vertex, prev ) ) || IsBoundary( source, geo::MeshEdgeRef_Make( vertex, next ) ) ) { return false; }
            faces.push_back( face.sourceId.value );
            if ( rim.a.value == 0 && face.cCorners >= 3 ) {
                // The face's next edge that does not touch the vertex.
                const id_t a = mesh.value.vertices.pData[mesh.CornerVertex( face, k + 1 )].sourceId;
                const id_t b = mesh.value.vertices.pData[mesh.CornerVertex( face, k + 2 )].sourceId;
                if ( a.value != vertex.value && b.value != vertex.value ) { rim = geo::MeshEdgeRef_Make( a, b ); }
            }
            break;
        }
    }
    return faces.size() >= 2 && rim.a.value != 0;
}

// Deletes the faces around the vertex and closes the hole with one face.
map_status_t ReplaceFan( map_document_t *map, u64 meshId, id_t vertex ) noexcept
{
    std::vector<u64> faces;
    geo::mesh_edge_ref_t rim{};
    if ( !VertexFan( geo::GeometryDocument_FindMesh( &map->geometry, { meshId } ), vertex, faces, rim ) ) { return map_status_t::INVALID_ARGUMENT; }
    auto result = MapTopologyEdit_DeleteFaces( map, meshId, { faces.data(), faces.size() } );
    if ( result != map_status_t::OK ) { return result; }
    return ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
        return geo::MeshSourceEdit_TryFillHole( working, rim.a, rim.b, report );
    } );
}
} // namespace

map_status_t MapTopologyEdit_DissolveVertices( map_document_t *map, u64 meshId, map_vertex_ids_t vertices ) noexcept
{
    if ( map == nullptr || !Span_IsValid( vertices ) || vertices.nCount == 0 ) { return map_status_t::INVALID_ARGUMENT; }
    if ( geo::GeometryDocument_FindMesh( &map->geometry, { meshId } ) == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
    for ( usize i = 0; i < vertices.nCount; ++i ) {
        const auto result = ReplaceFan( map, meshId, vertices.pData[i] );
        if ( result != map_status_t::OK ) { return result; }
    }
    return map_status_t::OK;
}

map_status_t MapTopologyEdit_BevelVertices( map_document_t *map, u64 meshId, map_vertex_ids_t vertices, f64 width ) noexcept
{
    if ( map == nullptr || !Span_IsValid( vertices ) || vertices.nCount == 0 || !std::isfinite( width ) || width <= 0.0 ) { return map_status_t::INVALID_ARGUMENT; }
    for ( usize i = 0; i < vertices.nCount; ++i ) {
        const id_t vertex = vertices.pData[i];
        const auto *source = geo::GeometryDocument_FindMesh( &map->geometry, { meshId } );
        if ( source == nullptr ) { return map_status_t::UNKNOWN_OBJECT; }
        std::vector<u64> fan;
        geo::mesh_edge_ref_t rim{};
        if ( !VertexFan( source, vertex, fan, rim ) ) { return map_status_t::INVALID_ARGUMENT; } // Border vertices are not bevelled.
        // 1. A cut point on every edge, `width` from the vertex (at most
        //    45% along, so neighbouring bevels cannot cross).
        std::vector<vec3> cuts;
        auto result = ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
            description_t mesh;
            auto status = mesh.Read( working );
            if ( status != OK ) { return status; }
            const u32 v = mesh.Vertex( vertex );
            std::vector<u32> neighbours;
            for ( usize f = 0; f < mesh.value.faces.nCount; ++f ) {
                const auto &face = mesh.value.faces.pData[f];
                for ( u32 k = 0; k < face.cCorners; ++k ) {
                    if ( mesh.CornerVertex( face, k ) != v ) { continue; }
                    for ( u32 n : { mesh.CornerVertex( face, k + 1 ), mesh.CornerVertex( face, k + face.cCorners - 1 ) } ) {
                        if ( std::find( neighbours.begin(), neighbours.end(), n ) == neighbours.end() ) { neighbours.push_back( n ); }
                    }
                }
            }
            const vec3 p = mesh.Position( v );
            for ( u32 n : neighbours ) {
                const vec3 q = mesh.Position( n );
                const f64 length = Length( Sub( q, p ) );
                const f64 t = std::min( width / std::max( length, 1e-9 ), 0.45 );
                cuts.push_back( Add( Mul( p, 1.0 - t ), Mul( q, t ) ) );
                status = geo::MeshSourceEdit_TrySplitEdge( working, vertex, mesh.value.vertices.pData[n].sourceId, t, nullptr, report );
                if ( status != OK ) { return status; }
            }
            return OK;
        } );
        if ( result != map_status_t::OK ) { return result; }
        // 2. Cut the corner off every face around the vertex, one face per
        //    transaction: each cut adds an edge at the shared cut points.
        struct cut_t { id_t face, a, b; };
        std::vector<cut_t> splits;
        {
            const auto *now = geo::GeometryDocument_FindMesh( &map->geometry, { meshId } );
            description_t mesh;
            if ( now == nullptr || mesh.Read( now ) != OK ) { return map_status_t::GEOMETRY_FAILED; }
            const u32 v = mesh.Vertex( vertex );
            for ( usize f = 0; f < mesh.value.faces.nCount; ++f ) {
                const auto &face = mesh.value.faces.pData[f];
                for ( u32 k = 0; k < face.cCorners; ++k ) {
                    if ( mesh.CornerVertex( face, k ) != v ) { continue; }
                    if ( face.cCorners > 3 ) {
                        splits.push_back( { face.sourceId, mesh.value.vertices.pData[mesh.CornerVertex( face, k + face.cCorners - 1 )].sourceId,
                                            mesh.value.vertices.pData[mesh.CornerVertex( face, k + 1 )].sourceId } );
                    }
                    break;
                }
            }
        }
        for ( const cut_t &cut : splits ) {
            result = ApplyMesh( map, meshId, [&]( geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
                return geo::MeshSourceEdit_TrySplitFace( working, cut.face, cut.a, cut.b, report );
            } );
            if ( result != map_status_t::OK ) { break; }
        }
        if ( result != map_status_t::OK ) { return result; }
        // 3. The corner triangles become one cap.
        result = ReplaceFan( map, meshId, vertex );
        if ( result != map_status_t::OK ) { return result; }
    }
    return map_status_t::OK;
}

map_status_t MapTopologyEdit_Project( map_document_t *map, u64 meshId, map_vertex_ids_t vertices, vec3 direction, f64 offset, usize *pMovedOut ) noexcept
{
    if ( pMovedOut != nullptr ) { *pMovedOut = 0; }
    auto result = CheckMeshes( map, { &meshId, 1 } );
    if ( result != map_status_t::OK ) { return result; }
    const f64 length = std::sqrt( direction.x * direction.x + direction.y * direction.y + direction.z * direction.z );
    if ( !Span_IsValid( vertices ) || !std::isfinite( length ) || length < 1e-12 || !std::isfinite( offset ) ) { return map_status_t::INVALID_ARGUMENT; }
    const vec3 d{ direction.x / length, direction.y / length, direction.z / length };
    // Every other object's faces, as triangles.
    map_wireframe_t wire{};
    if ( !MapWireframe_Init( &wire, map->pAllocator ) ) { return map_status_t::OUT_OF_MEMORY; }
    result = MapWireframe_Build( &wire, *map );
    std::vector<std::array<vec3, 3>> triangles;
    for ( usize f = 0; result == map_status_t::OK && f < wire.faces.nCount; ++f ) {
        const auto &face = wire.faces.pData[f];
        if ( face.id == meshId || face.nIndices < 3 ) { continue; }
        const vec3 a = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex]];
        for ( u32 k = 1; k + 1 < face.nIndices; ++k ) {
            triangles.push_back( { a, wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k]], wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k + 1]] } );
        }
    }
    MapWireframe_Shutdown( &wire );
    if ( result != map_status_t::OK ) { return result; }
    geo::mesh_source_description_t description{};
    auto status = geo::MeshSourceDescription_Init( &description, map->pAllocator, { meshId } );
    if ( status == OK ) { status = geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &map->geometry, { meshId } ), &description ); }
    std::vector<geo::geometry_source_id_t> ids;
    std::vector<vec3> positions;
    for ( usize v = 0; status == OK && v < description.vertices.nCount; ++v ) {
        const auto &vertex = description.vertices.pData[v];
        bool wanted = vertices.nCount == 0;
        for ( usize i = 0; !wanted && i < vertices.nCount; ++i ) { wanted = vertices.pData[i].value == vertex.sourceId.value; }
        if ( !wanted ) { continue; }
        // The nearest triangle ahead (Moller-Trumbore).
        const vec3 o = vertex.position;
        f64 nearest = 1e300;
        for ( const auto &t : triangles ) {
            const vec3 e1{ t[1].x - t[0].x, t[1].y - t[0].y, t[1].z - t[0].z }, e2{ t[2].x - t[0].x, t[2].y - t[0].y, t[2].z - t[0].z };
            const vec3 p{ d.y * e2.z - d.z * e2.y, d.z * e2.x - d.x * e2.z, d.x * e2.y - d.y * e2.x };
            const f64 det = e1.x * p.x + e1.y * p.y + e1.z * p.z;
            if ( std::fabs( det ) < 1e-12 ) { continue; }
            const vec3 s{ o.x - t[0].x, o.y - t[0].y, o.z - t[0].z };
            const f64 u = ( s.x * p.x + s.y * p.y + s.z * p.z ) / det;
            if ( u < 0.0 || u > 1.0 ) { continue; }
            const vec3 q{ s.y * e1.z - s.z * e1.y, s.z * e1.x - s.x * e1.z, s.x * e1.y - s.y * e1.x };
            const f64 w = ( d.x * q.x + d.y * q.y + d.z * q.z ) / det;
            if ( w < 0.0 || u + w > 1.0 ) { continue; }
            const f64 hit = ( e2.x * q.x + e2.y * q.y + e2.z * q.z ) / det;
            if ( hit > 1e-6 && hit < nearest ) { nearest = hit; }
        }
        if ( nearest >= 1e299 ) { continue; }
        const f64 travel = std::max( 0.0, nearest - offset );
        if ( travel < 1e-9 ) { continue; }
        ids.push_back( vertex.sourceId );
        positions.push_back( { o.x + d.x * travel, o.y + d.y * travel, o.z + d.z * travel } );
    }
    geo::MeshSourceDescription_Shutdown( &description );
    if ( status != OK ) { return Status( status ); }
    if ( ids.empty() ) { return map_status_t::OK; }
    result = MapTopologyEdit_SetVertexPositions( map, meshId, { ids.data(), ids.size() }, { positions.data(), positions.size() } );
    if ( result == map_status_t::OK && pMovedOut != nullptr ) { *pMovedOut = ids.size(); }
    return result;
}

} // namespace cypher::editor::map

