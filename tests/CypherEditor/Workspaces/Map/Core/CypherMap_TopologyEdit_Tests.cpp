//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_TopologyEdit_Tests.cpp
//  Purpose: Contract tests for the map's mesh component tools and the mirror
//           and solidify object operations.
//  Details: Every case starts from a converted box (eight vertices, six
//           quads) or a Quad primitive (one open face) so counts are exact.
//
//  History:
//  - Created by Karlo Siric on 2026-10-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_TopologyEdit.h"
#include "CypherMap_Edit.h"
#include "CypherMap_MeshEdit.h"
#include "CypherMap_Groups.h"
#include "CypherMap_Modifiers.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_MeshSourceModeling.h"
#include "CypherGeometry_MeshSourceTopology.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherMath_UV.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <map>
#include <limits>
#include <memory>
#include <string>
#include <cmath>
#include <set>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;

namespace
{
string_view_t SV( const char *p ) { return StringView_FromCString( p ); }

struct fixture_t {
    map_document_t map{};
    fixture_t() { REQUIRE( MapDocument_Create( &map, Allocator_GetSystem(), { SV( "Topology fixture" ), SV( "reap" ), {} } ) == map_status_t::OK ); }
    u64 Primitive( map_primitive_kind_t kind, math::aabbd_t bounds, u32 axis = 2 )
    {
        map_primitive_desc_t desc{}; desc.kind = kind; desc.bounds = bounds; desc.axis = axis;
        u64 id = 0;
        REQUIRE( MapEdit_CreatePrimitive( &map, desc, SV( "materials/source.cymat" ), {}, &id ) == map_status_t::OK );
        return id;
    }
    u64 BoxMesh()
    {
        const u64 id = Primitive( map_primitive_kind_t::BOX, { { -64, -48, 16 }, { 96, 80, 112 } } );
        vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
        REQUIRE( MapMeshEdit_ConvertBrushes( &map, { &id, 1 }, &roots ) == map_status_t::OK );
        Vector_Shutdown( &roots );
        return id;
    }
    u64 Quad() { return Primitive( map_primitive_kind_t::QUAD, { { 0, 0, 0 }, { 128, 64, 0 } }, 2 ); }
};

struct description_t {
    geo::mesh_source_description_t value{};
    description_t( const map_document_t &map, u64 id )
    {
        REQUIRE( geo::MeshSourceDescription_Init( &value, map.pAllocator, { id } ) == geo::geometry_status_t::OK );
        const auto *source = geo::GeometryDocument_FindMesh( &map.geometry, { id } ); REQUIRE( source );
        REQUIRE( geo::MeshSource_TryDescribe( source, &value ) == geo::geometry_status_t::OK );
    }
    ~description_t() { geo::MeshSourceDescription_Shutdown( &value ); }
    usize Vertices() const { return value.vertices.nCount; }
    usize Faces() const { return value.faces.nCount; }
    math::vec3d_t Position( geo::geometry_source_id_t id ) const
    {
        for ( usize i = 0; i < value.vertices.nCount; ++i ) { if ( value.vertices.pData[i].sourceId.value == id.value ) { return value.vertices.pData[i].position; } }
        FAIL( "Missing vertex" ); return {};
    }
    bool HasVertex( geo::geometry_source_id_t id ) const
    {
        for ( usize i = 0; i < value.vertices.nCount; ++i ) { if ( value.vertices.pData[i].sourceId.value == id.value ) { return true; } }
        return false;
    }
    // The corner vertex IDs of face f, in winding order.
    std::vector<geo::geometry_source_id_t> Corners( usize f ) const
    {
        std::vector<geo::geometry_source_id_t> out;
        const auto &face = value.faces.pData[f];
        for ( u32 k = 0; k < face.cCorners; ++k ) { out.push_back( value.vertices.pData[value.corners.pData[face.iFirstCorner + k].iVertex].sourceId ); }
        return out;
    }
    // The face whose corners all have this z (the top or bottom of the box).
    usize FaceAtZ( f64 z ) const
    {
        for ( usize f = 0; f < value.faces.nCount; ++f ) {
            bool all = true;
            for ( const auto &id : Corners( f ) ) { all &= Position( id ).z == z; }
            if ( all ) { return f; }
        }
        FAIL( "Missing face" ); return 0;
    }
};

geo::mesh_edge_ref_t Edge( geo::geometry_source_id_t a, geo::geometry_source_id_t b ) { return geo::MeshEdgeRef_Make( a, b ); }

const geo::mesh_source_t *Source( const map_document_t &map, u64 id ) { return geo::GeometryDocument_FindMesh( &map.geometry, { id } ); }
usize BoundaryEdges( const map_document_t &map, u64 id ) { return geo::MeshBoundary_CountBoundaryEdges( &Source( map, id )->mesh ); }
bool IsHard( const map_document_t &map, u64 id, geo::mesh_edge_ref_t edge )
{
    const auto *source = Source( map, id );
    geo::geometry_mesh_edge_handle_t handle{};
    REQUIRE( geo::MeshSourceEdit_TryFindEdge( source, edge.a, edge.b, &handle ) );
    return ( geo::MeshAttributeStore_GetEdge( &source->attributes, handle ).flags & geo::MESH_EDGE_FLAG_HARD ) != 0;
}
// Saves to memory and loads back: what persists is what the file holds.
struct saved_t {
    std::string root;
    map_status_t Save( map_document_t *pMap )
    {
        text_buffer_t text{};
        if ( !TextBuffer_Init( &text, Allocator_GetSystem() ) ) { return map_status_t::OUT_OF_MEMORY; }
        const map_status_t status = MapDocument_Save( pMap, &text );
        if ( status == map_status_t::OK ) { root.assign( TextBuffer_Data( &text ), TextBuffer_Length( &text ) ); }
        return status;
    }
    void Load( map_document_t &map ) const
    {
        REQUIRE( MapDocument_Load( &map, Allocator_GetSystem(), { root.data(), root.size() } ) == map_status_t::OK );
    }
};
} // namespace

TEST_CASE( "Vertex merge, connect, snap and radial align edit the mesh through persistent IDs", "[map][topology-edit][vertices]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh();
    description_t before( f.map, mesh );
    const usize top = before.FaceAtZ( 112 );
    const auto corners = before.Corners( top );
    REQUIRE( corners.size() == 4 );

    SECTION( "Merge keeps the first vertex at the group's average" )
    {
        const geo::geometry_source_id_t pair[]{ corners[0], corners[1] };
        const auto expected = before.Position( corners[0] ), other = before.Position( corners[1] );
        const auto revision = f.map.geometry.revision;
        REQUIRE( MapTopologyEdit_MergeVertices( &f.map, mesh, { pair, 2 } ) == map_status_t::OK );
        description_t after( f.map, mesh );
        CHECK( after.Vertices() == 7 ); CHECK( f.map.geometry.revision != revision );
        REQUIRE( after.HasVertex( corners[0] ) ); CHECK_FALSE( after.HasVertex( corners[1] ) );
        CHECK( after.Position( corners[0] ).x == Catch::Approx( ( expected.x + other.x ) / 2 ) );
        CHECK( after.Position( corners[0] ).y == Catch::Approx( ( expected.y + other.y ) / 2 ) );
        const geo::geometry_source_id_t one[]{ corners[0] };
        CHECK( MapTopologyEdit_MergeVertices( &f.map, mesh, { one, 1 } ) == map_status_t::INVALID_ARGUMENT );
    }
    SECTION( "Connect cuts the face two diagonal vertices share" )
    {
        REQUIRE( MapTopologyEdit_ConnectVertices( &f.map, mesh, corners[0], corners[2] ) == map_status_t::OK );
        description_t after( f.map, mesh );
        CHECK( after.Faces() == 7 ); CHECK( after.Vertices() == 8 );
        // Adjacent vertices already share an edge: nothing to cut.
        CHECK( MapTopologyEdit_ConnectVertices( &f.map, mesh, corners[0], corners[1] ) != map_status_t::OK );
    }
    SECTION( "Snap moves each vertex to the nearest grid point" )
    {
        const geo::geometry_source_id_t one[]{ corners[0] };
        REQUIRE( MapTopologyEdit_SnapVertices( &f.map, mesh, { one, 1 }, 64.0 ) == map_status_t::OK );
        description_t after( f.map, mesh );
        const auto p = after.Position( corners[0] );
        CHECK( std::fmod( p.x, 64.0 ) == 0.0 ); CHECK( std::fmod( p.y, 64.0 ) == 0.0 ); CHECK( std::fmod( p.z, 64.0 ) == 0.0 );
    }
    SECTION( "Radial align places the vertices at one distance from their centre" )
    {
        const geo::geometry_source_id_t three[]{ corners[0], corners[1], corners[2] };
        REQUIRE( MapTopologyEdit_RadialAlign( &f.map, mesh, { three, 3 } ) == map_status_t::OK );
        description_t after( f.map, mesh );
        // The circle is centred on where the vertices were, not where they end up.
        math::vec3d_t c{};
        for ( const auto &id : three ) { const auto p = before.Position( id ); c = { c.x + p.x / 3, c.y + p.y / 3, c.z + p.z / 3 }; }
        std::vector<f64> radii;
        for ( const auto &id : three ) {
            const auto p = after.Position( id );
            radii.push_back( std::sqrt( ( p.x - c.x ) * ( p.x - c.x ) + ( p.y - c.y ) * ( p.y - c.y ) + ( p.z - c.z ) * ( p.z - c.z ) ) );
        }
        CHECK( radii[0] == Catch::Approx( radii[1] ) ); CHECK( radii[1] == Catch::Approx( radii[2] ) );
        CHECK( after.Position( corners[0] ).z == Catch::Approx( 112 ) ); // Still in the face's plane.
    }
}

TEST_CASE( "Edge collapse, split, connect, dissolve and bevel change exact counts", "[map][topology-edit][edges]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh();
    description_t before( f.map, mesh );
    const auto top = before.Corners( before.FaceAtZ( 112 ) );
    const auto edge = Edge( top[0], top[1] ), opposite = Edge( top[2], top[3] );

    SECTION( "Collapse" )
    {
        REQUIRE( MapTopologyEdit_CollapseEdges( &f.map, mesh, { &edge, 1 } ) == map_status_t::OK );
        CHECK( description_t( f.map, mesh ).Vertices() == 7 );
    }
    SECTION( "Split inserts midpoints" )
    {
        const geo::mesh_edge_ref_t both[]{ edge, opposite };
        REQUIRE( MapTopologyEdit_SplitEdges( &f.map, mesh, { both, 2 } ) == map_status_t::OK );
        description_t after( f.map, mesh );
        CHECK( after.Vertices() == 10 ); CHECK( after.Faces() == 6 );
    }
    SECTION( "Connect joins midpoints of two edges across their face, as one private edit" )
    {
        const geo::mesh_edge_ref_t both[]{ edge, opposite };
        const u64 next = f.map.nextId;
        REQUIRE( MapTopologyEdit_ConnectEdges( &f.map, mesh, { both, 2 } ) == map_status_t::OK );
        description_t after( f.map, mesh );
        CHECK( after.Vertices() == 10 ); CHECK( after.Faces() == 7 );
        CHECK( f.map.nextId > next ); // The midpoints received fresh IDs above the old high-water mark.
    }
    SECTION( "Dissolve removes an interior edge a connect created" )
    {
        REQUIRE( MapTopologyEdit_ConnectVertices( &f.map, mesh, top[0], top[2] ) == map_status_t::OK );
        const auto diagonal = Edge( top[0], top[2] );
        REQUIRE( MapTopologyEdit_DissolveEdges( &f.map, mesh, { &diagonal, 1 } ) == map_status_t::OK );
        CHECK( description_t( f.map, mesh ).Faces() == 6 );
    }
    SECTION( "Bevel chamfers one edge into a new face" )
    {
        REQUIRE( MapTopologyEdit_BevelEdges( &f.map, mesh, { &edge, 1 }, 8.0, 1 ) == map_status_t::OK );
        CHECK( description_t( f.map, mesh ).Faces() == 7 );
        CHECK( MapTopologyEdit_BevelEdges( &f.map, mesh, { &opposite, 1 }, 0.0, 1 ) == map_status_t::INVALID_ARGUMENT );
    }
}

TEST_CASE( "Edge normals set, soften and clear the authored hard flag", "[map][topology-edit][normals]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh();
    description_t before( f.map, mesh );
    const auto top = before.Corners( before.FaceAtZ( 112 ) );
    const auto edge = Edge( top[0], top[1] );
    REQUIRE( MapTopologyEdit_SetEdgeNormals( &f.map, mesh, { &edge, 1 }, map_edge_normals_t::HARD ) == map_status_t::OK );
    CHECK( IsHard( f.map, mesh, edge ) );
    REQUIRE( MapTopologyEdit_SetEdgeNormals( &f.map, mesh, { &edge, 1 }, map_edge_normals_t::SOFT ) == map_status_t::OK );
    CHECK_FALSE( IsHard( f.map, mesh, edge ) );
    // Converted brushes shade flat (group 0); softening gives both faces a group.
    description_t soft( f.map, mesh );
    u32 sharing = 0;
    for ( usize i = 0; i < soft.Faces(); ++i ) {
        const auto ids = soft.Corners( i );
        const bool hasA = std::find_if( ids.begin(), ids.end(), [&]( auto id ) { return id.value == edge.a.value; } ) != ids.end();
        const bool hasB = std::find_if( ids.begin(), ids.end(), [&]( auto id ) { return id.value == edge.b.value; } ) != ids.end();
        if ( hasA && hasB ) { CHECK( soft.value.faces.pData[i].attributes.smoothingGroups != 0 ); ++sharing; }
    }
    CHECK( sharing == 2 );
    REQUIRE( MapTopologyEdit_SetEdgeNormals( &f.map, mesh, { &edge, 1 }, map_edge_normals_t::HARD ) == map_status_t::OK );
    REQUIRE( MapTopologyEdit_SetEdgeNormals( &f.map, mesh, { &edge, 1 }, map_edge_normals_t::DEFAULT ) == map_status_t::OK );
    CHECK_FALSE( IsHard( f.map, mesh, edge ) );
}

TEST_CASE( "Face collapse and detach keep identities and change topology", "[map][topology-edit][faces]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh();
    description_t before( f.map, mesh );
    const usize top = before.FaceAtZ( 112 );
    const u64 faceId = before.value.faces.pData[top].sourceId.value;
    SECTION( "Collapse" )
    {
        REQUIRE( MapTopologyEdit_CollapseFace( &f.map, mesh, faceId ) == map_status_t::OK );
        description_t after( f.map, mesh );
        CHECK( after.Vertices() == 5 ); CHECK( after.Faces() == 5 );
    }
    SECTION( "Detach" )
    {
        REQUIRE( MapTopologyEdit_DetachFace( &f.map, mesh, faceId ) == map_status_t::OK );
        description_t after( f.map, mesh );
        CHECK( after.Vertices() == 12 ); CHECK( after.Faces() == 6 );
        CHECK( BoundaryEdges( f.map, mesh ) == 8 );
    }
    CHECK( MapTopologyEdit_CollapseFace( &f.map, mesh, 0 ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Open edges extrude off the surface, extend in its plane and holes fill", "[map][topology-edit][open]" )
{
    fixture_t f; const u64 quad = f.Quad();
    description_t before( f.map, quad );
    REQUIRE( before.Faces() == 1 ); REQUIRE( BoundaryEdges( f.map, quad ) == 4 );
    const auto corners = before.Corners( 0 );
    const auto edge = Edge( corners[0], corners[1] );
    SECTION( "Extrude rises along the normal" )
    {
        REQUIRE( MapTopologyEdit_ExtrudeEdges( &f.map, quad, { &edge, 1 }, 16.0, map_edge_extrude_t::NORMAL ) == map_status_t::OK );
        description_t after( f.map, quad );
        CHECK( after.Faces() == 2 );
        f64 high = 0; for ( usize i = 0; i < after.Vertices(); ++i ) { high = std::max( high, std::fabs( after.value.vertices.pData[i].position.z ) ); }
        CHECK( high == Catch::Approx( 16.0 ) );
    }
    SECTION( "Extend stays in the plane" )
    {
        REQUIRE( MapTopologyEdit_ExtrudeEdges( &f.map, quad, { &edge, 1 }, 16.0, map_edge_extrude_t::EXTEND ) == map_status_t::OK );
        description_t after( f.map, quad );
        CHECK( after.Faces() == 2 );
        for ( usize i = 0; i < after.Vertices(); ++i ) { CHECK( after.value.vertices.pData[i].position.z == Catch::Approx( 0.0 ) ); }
    }
    SECTION( "Solidify closes the open quad into a slab" )
    {
        REQUIRE( MapTopologyEdit_Solidify( &f.map, { &quad, 1 }, 8.0 ) == map_status_t::OK );
        description_t after( f.map, quad );
        CHECK( after.Faces() == 6 ); CHECK( BoundaryEdges( f.map, quad ) == 0 );
        CHECK( after.value.faces.pData[0].sourceId.value != 0 );
        bool kept = false;
        for ( usize i = 0; i < after.Faces(); ++i ) { kept |= after.value.faces.pData[i].sourceId.value == before.value.faces.pData[0].sourceId.value; }
        CHECK( kept );
        CHECK( f.map.nextId >= f.map.geometry.sourceIds.allocator.next.value );
        // A closed mesh has no inside to grow toward.
        CHECK( MapTopologyEdit_Solidify( &f.map, { &quad, 1 }, 8.0 ) != map_status_t::OK );
    }
    SECTION( "Fill Hole closes an opening" )
    {
        const u64 box = f.BoxMesh();
        description_t closed( f.map, box );
        const u64 top = closed.value.faces.pData[closed.FaceAtZ( 112 )].sourceId.value;
        const auto rim = closed.Corners( closed.FaceAtZ( 112 ) );
        REQUIRE( MapMeshEdit_Apply( &f.map, box, []( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t *report ) noexcept {
            const geo::geometry_source_id_t face{ *static_cast<const u64 *>( context ) };
            return geo::MeshSourceEdit_TryDeleteFaces( working, { &face, 1 }, report );
        }, const_cast<u64 *>( &top ) ) == map_status_t::OK );
        REQUIRE( BoundaryEdges( f.map, box ) == 4 );
        const auto open = Edge( rim[0], rim[1] );
        REQUIRE( MapTopologyEdit_FillHoles( &f.map, box, { &open, 1 } ) == map_status_t::OK );
        CHECK( BoundaryEdges( f.map, box ) == 0 ); CHECK( description_t( f.map, box ).Faces() == 6 );
    }
}

TEST_CASE( "Mirror reflects brushes and meshes in place and rejects entities", "[map][topology-edit][mirror]" )
{
    fixture_t f;
    const u64 brush = f.Primitive( map_primitive_kind_t::BOX, { { 32, 0, 0 }, { 96, 64, 64 } } );
    const u64 mesh = f.BoxMesh();
    const math::planed_t plane{ { 1, 0, 0 }, 0.0 }; // x = 0
    const u64 ids[]{ brush, mesh };
    const auto *source = geo::GeometryDocument_FindBrush( &f.map.geometry, { brush } ); REQUIRE( source );
    std::set<u64> sides;
    for ( usize i = 0; i < source->sides.nCount; ++i ) { sides.insert( source->sides.pData[i].sourceId.value ); }
    REQUIRE( MapTopologyEdit_Mirror( &f.map, { ids, 2 }, plane, true ) == map_status_t::OK );
    geo::brush_source_t copy{};
    REQUIRE( geo::GeometryDocument_TryCopyBrushSource( &f.map.geometry, { brush }, f.map.pAllocator, &copy ) == geo::geometry_status_t::OK );
    const auto bounds = MapGeometry_BrushBounds( copy, f.map.geometryPolicy, f.map.pAllocator );
    CHECK( bounds.box.minimum.x == Catch::Approx( -96 ) ); CHECK( bounds.box.maximum.x == Catch::Approx( -32 ) );
    std::set<u64> mirrored;
    for ( usize i = 0; i < copy.solid.sides.nCount; ++i ) { mirrored.insert( copy.solid.sides.pData[i].sourceId.value ); }
    CHECK( mirrored == sides );
    geo::BrushSource_Shutdown( &copy );
    const auto meshBounds = MapGeometry_MeshBounds( *Source( f.map, mesh ), f.map.pAllocator );
    CHECK( meshBounds.box.minimum.x == Catch::Approx( -96 ) ); CHECK( meshBounds.box.maximum.x == Catch::Approx( 64 ) );
    const math::planed_t skewed{ { 2, 0, 0 }, 0.0 };
    CHECK( MapTopologyEdit_Mirror( &f.map, { ids, 1 }, skewed, true ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Whole mesh smoothing relaxes every vertex and invalid inputs leave the map unchanged", "[map][topology-edit][smooth]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh();
    const auto before = MapGeometry_MeshBounds( *Source( f.map, mesh ), f.map.pAllocator );
    REQUIRE( MapTopologyEdit_Smooth( &f.map, { &mesh, 1 }, 0.5, 1 ) == map_status_t::OK );
    const auto after = MapGeometry_MeshBounds( *Source( f.map, mesh ), f.map.pAllocator );
    CHECK( after.box.maximum.x - after.box.minimum.x < before.box.maximum.x - before.box.minimum.x );
    const auto revision = f.map.geometry.revision;
    CHECK( MapTopologyEdit_Smooth( &f.map, { &mesh, 1 }, 0.0, 1 ) == map_status_t::INVALID_ARGUMENT );
    const u64 missing = 999999;
    CHECK( MapTopologyEdit_Smooth( &f.map, { &missing, 1 }, 0.5, 1 ) == map_status_t::UNKNOWN_OBJECT );
    f.map.bReadOnly = CY_TRUE;
    CHECK( MapTopologyEdit_Smooth( &f.map, { &mesh, 1 }, 0.5, 1 ) == map_status_t::READ_ONLY );
    CHECK( f.map.geometry.revision == revision );
}

TEST_CASE( "Joining meshes keeps every identity and separating restores separate objects", "[map][topology-edit][join]" )
{
    fixture_t f; const u64 a = f.BoxMesh(); const u64 b = f.BoxMesh();
    std::set<u64> faces;
    for ( u64 id : { a, b } ) { description_t d( f.map, id ); for ( usize i = 0; i < d.Faces(); ++i ) { faces.insert( d.value.faces.pData[i].sourceId.value ); } }
    const u64 ids[]{ b, a };
    u64 retained = 0;
    REQUIRE( MapTopologyEdit_JoinMeshes( &f.map, { ids, 2 }, &retained ) == map_status_t::OK );
    CHECK( retained == std::min( a, b ) );
    CHECK( f.map.geometry.meshes.nCount == 1 );
    description_t joined( f.map, retained );
    std::set<u64> after;
    for ( usize i = 0; i < joined.Faces(); ++i ) { after.insert( joined.value.faces.pData[i].sourceId.value ); }
    CHECK( after == faces ); CHECK( joined.Vertices() == 16 ); // Coincident boxes are not welded.
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
    const u64 next = f.map.nextId;
    REQUIRE( MapTopologyEdit_SeparateShells( &f.map, retained, &roots ) == map_status_t::OK );
    REQUIRE( roots.nCount == 2 ); CHECK( roots.pData[0] == retained ); CHECK( roots.pData[1] >= next );
    CHECK( f.map.geometry.meshes.nCount == 2 ); CHECK( f.map.nextId > roots.pData[1] );
    CHECK( MapTopologyEdit_SeparateShells( &f.map, retained, &roots ) == map_status_t::INVALID_ARGUMENT ); // One shell now.
    Vector_Shutdown( &roots );
    const u64 one[]{ a };
    CHECK( MapTopologyEdit_JoinMeshes( &f.map, { one, 1 }, &retained ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Joined, separated, bevelled and hardened meshes survive save and reload", "[map][topology-edit][persistence]" )
{
    fixture_t f; const u64 a = f.BoxMesh(); const u64 b = f.BoxMesh();
    const u64 ids[]{ a, b };
    u64 retained = 0;
    REQUIRE( MapTopologyEdit_JoinMeshes( &f.map, { ids, 2 }, &retained ) == map_status_t::OK );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
    REQUIRE( MapTopologyEdit_SeparateShells( &f.map, retained, &roots ) == map_status_t::OK );
    REQUIRE( roots.nCount == 2 );
    const u64 piece = roots.pData[1];
    Vector_Shutdown( &roots );
    description_t before( f.map, piece );
    const auto edge = Edge( before.Corners( 0 )[0], before.Corners( 0 )[1] );
    REQUIRE( MapTopologyEdit_BevelEdges( &f.map, piece, { &edge, 1 }, 4.0, 2 ) == map_status_t::OK );
    description_t bevelled( f.map, piece );
    const auto hard = Edge( bevelled.Corners( 0 )[0], bevelled.Corners( 0 )[1] );
    REQUIRE( MapTopologyEdit_SetEdgeNormals( &f.map, piece, { &hard, 1 }, map_edge_normals_t::HARD ) == map_status_t::OK );
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded; saved.Load( loaded );
    CHECK( loaded.geometry.meshes.nCount == 2 );
    description_t reloaded( loaded, piece );
    CHECK( reloaded.Faces() == bevelled.Faces() ); CHECK( reloaded.Vertices() == bevelled.Vertices() );
    CHECK( IsHard( loaded, piece, hard ) );
    CHECK( loaded.nextId >= f.map.nextId );
}

TEST_CASE( "Subdivision replaces a mesh by its Catmull-Clark level keeping corner identities", "[map][topology-edit][subdivide]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh();
    description_t before( f.map, mesh );
    const auto corner = before.value.vertices.pData[0].sourceId;
    const u64 next = f.map.nextId;
    REQUIRE( MapTopologyEdit_Subdivide( &f.map, { &mesh, 1 } ) == map_status_t::OK );
    description_t after( f.map, mesh );
    CHECK( after.Faces() == 24 ); CHECK( after.Vertices() == 26 );
    CHECK( after.HasVertex( corner ) ); CHECK( f.map.nextId > next );
    for ( usize i = 0; i < after.Faces(); ++i ) { CHECK( after.value.faces.pData[i].attributes.material.value == before.value.faces.pData[0].attributes.material.value ); }
}

TEST_CASE( "Mesh slicing splits faces or clips and caps one side", "[map][topology-edit][slice]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh(); // z from 16 to 112
    const math::planed_t plane{ { 0, 0, 1 }, -64.0 };
    REQUIRE( MapTopologyEdit_SliceMeshes( &f.map, { &mesh, 1 }, plane, map_slice_keep_t::BOTH ) == map_status_t::OK );
    CHECK( description_t( f.map, mesh ).Faces() == 10 );
    REQUIRE( MapTopologyEdit_SliceMeshes( &f.map, { &mesh, 1 }, plane, map_slice_keep_t::BACK ) == map_status_t::OK );
    const auto bounds = MapGeometry_MeshBounds( *Source( f.map, mesh ), f.map.pAllocator );
    CHECK( bounds.box.maximum.z == Catch::Approx( 64 ) ); CHECK( BoundaryEdges( f.map, mesh ) == 0 ); // Capped.
    const math::planed_t skewed{ { 0, 0, 2 }, 0.0 };
    CHECK( MapTopologyEdit_SliceMeshes( &f.map, { &mesh, 1 }, skewed, map_slice_keep_t::BOTH ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Vertex moves, loop cuts, knife cuts and brush vertex drags", "[map][topology-edit][tools]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh();
    description_t before( f.map, mesh );
    const auto top = before.Corners( before.FaceAtZ( 112 ) );
    SECTION( "Move" )
    {
        const geo::geometry_source_id_t two[]{ top[0], top[1] };
        const auto p = before.Position( top[0] );
        REQUIRE( MapTopologyEdit_MoveVertices( &f.map, mesh, { two, 2 }, { 0, 0, 16 } ) == map_status_t::OK );
        CHECK( description_t( f.map, mesh ).Position( top[0] ).z == Catch::Approx( p.z + 16 ) );
    }
    SECTION( "Loop cut across the ring of a side edge" )
    {
        // A vertical edge: its ring runs around the box horizontally.
        geo::mesh_edge_ref_t vertical{};
        for ( usize i = 0; i < before.Faces() && vertical.a.value == 0; ++i ) {
            const auto c = before.Corners( i );
            for ( usize k = 0; k < c.size(); ++k ) {
                const auto a = before.Position( c[k] ), b = before.Position( c[( k + 1 ) % c.size()] );
                if ( a.x == b.x && a.y == b.y ) { vertical = Edge( c[k], c[( k + 1 ) % c.size()] ); break; }
            }
        }
        REQUIRE( vertical.a.value != 0 );
        REQUIRE( MapTopologyEdit_LoopCut( &f.map, mesh, vertical, 0.5 ) == map_status_t::OK );
        CHECK( description_t( f.map, mesh ).Faces() == 10 );
        CHECK( MapTopologyEdit_LoopCut( &f.map, mesh, vertical, 1.0 ) == map_status_t::INVALID_ARGUMENT );
    }
    SECTION( "Knife between two corners of one face" )
    {
        geo::mesh_edit_knife_point_t path[2]{};
        path[0].kind = geo::mesh_knife_point_kind_t::VERTEX; path[0].vertexId = top[0]; path[0].position = before.Position( top[0] );
        path[1].kind = geo::mesh_knife_point_kind_t::VERTEX; path[1].vertexId = top[2]; path[1].position = before.Position( top[2] );
        REQUIRE( MapTopologyEdit_Knife( &f.map, mesh, { path, 2 } ) == map_status_t::OK );
        CHECK( description_t( f.map, mesh ).Faces() == 7 );
    }
    SECTION( "Brush vertex drag" )
    {
        const u64 brush = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 64, 64, 64 } } );
        REQUIRE( MapTopologyEdit_MoveBrushVertex( &f.map, brush, { 64, 64, 64 }, { 64, 64, 96 } ) == map_status_t::OK );
        geo::brush_source_t copy{};
        REQUIRE( geo::GeometryDocument_TryCopyBrushSource( &f.map.geometry, { brush }, f.map.pAllocator, &copy ) == geo::geometry_status_t::OK );
        CHECK( MapGeometry_BrushBounds( copy, f.map.geometryPolicy, f.map.pAllocator ).box.maximum.z == Catch::Approx( 96 ) );
        geo::BrushSource_Shutdown( &copy );
        CHECK( MapTopologyEdit_MoveBrushVertex( &f.map, brush, { 1, 2, 3 }, { 0, 0, 0 } ) == map_status_t::INVALID_ARGUMENT ); // No vertex there.
    }
}

namespace
{
u64 BoxMeshAt( fixture_t &f, math::aabbd_t bounds )
{
    const u64 id = f.Primitive( map_primitive_kind_t::BOX, bounds );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &f.map, { &id, 1 }, &roots ) == map_status_t::OK );
    Vector_Shutdown( &roots );
    return id;
}
math::aabbd_t MeshBox( const map_document_t &map, u64 id ) { return MapGeometry_MeshBounds( *Source( map, id ), map.pAllocator ).box; }
} // namespace

TEST_CASE( "Hammer 5 face operations: extract, thicken, combine, remove bad faces", "[map][topology-edit][faces]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh(); // z from 16 to 112
    description_t before( f.map, mesh );
    const u64 top = before.value.faces.pData[before.FaceAtZ( 112 )].sourceId.value;
    u64 extracted = 0;
    REQUIRE( MapTopologyEdit_ExtractFaces( &f.map, mesh, { &top, 1 }, &extracted ) == map_status_t::OK );
    REQUIRE( extracted != 0 ); CHECK( extracted != mesh );
    CHECK( description_t( f.map, extracted ).Faces() == 1 ); CHECK( description_t( f.map, mesh ).Faces() == 6 );
    u64 slab = 0;
    REQUIRE( MapTopologyEdit_ThickenFaces( &f.map, mesh, { &top, 1 }, 8.0, &slab ) == map_status_t::OK );
    CHECK( description_t( f.map, slab ).Faces() == 6 ); CHECK( BoundaryEdges( f.map, slab ) == 0 );
    CHECK( MeshBox( f.map, slab ).maximum.z - MeshBox( f.map, slab ).minimum.z == Catch::Approx( 8 ) );
    // Split the sides at z = 64, then join one side's halves back into one face.
    REQUIRE( MapTopologyEdit_SliceMeshes( &f.map, { &mesh, 1 }, { { 0, 0, 1 }, -64.0 }, map_slice_keep_t::BOTH ) == map_status_t::OK );
    description_t sliced( f.map, mesh ); REQUIRE( sliced.Faces() == 10 );
    std::vector<u64> halves;
    for ( usize i = 0; i < sliced.Faces(); ++i ) {
        const auto corners = sliced.Corners( i );
        if ( std::all_of( corners.begin(), corners.end(), [&]( auto id ) { return sliced.Position( id ).x == Catch::Approx( -64 ); } ) ) { halves.push_back( sliced.value.faces.pData[i].sourceId.value ); }
    }
    REQUIRE( halves.size() == 2 );
    REQUIRE( MapTopologyEdit_CombineFaces( &f.map, mesh, { halves.data(), halves.size() } ) == map_status_t::OK );
    CHECK( description_t( f.map, mesh ).Faces() == 9 );
    CHECK( MapTopologyEdit_CombineFaces( &f.map, mesh, { &top, 1 } ) == map_status_t::INVALID_ARGUMENT );
    usize removed = 99;
    REQUIRE( MapTopologyEdit_RemoveBadFaces( &f.map, { &mesh, 1 }, 1e-6, &removed ) == map_status_t::OK );
    CHECK( removed == 0 ); // A clean mesh has none.
}

TEST_CASE( "Clipping meshes matches brush clipping: capped halves, discarded meshes removed", "[map][topology-edit][slice][clip]" )
{
    const math::planed_t plane{ { 0, 0, 1 }, -64.0 }; // z = 64; the box spans z 16..112.
    const auto bounds = [&]( fixture_t &f, u64 id ) { return MapGeometry_MeshBounds( *Source( f.map, id ), f.map.pAllocator ).box; };
    SECTION( "Both: two closed meshes, the back one new" )
    {
        fixture_t f; const u64 mesh = f.BoxMesh();
        vector_t<u64> ids{}; REQUIRE( Vector_Init( &ids, Allocator_GetSystem() ) );
        REQUIRE( MapTopologyEdit_ClipMeshes( &f.map, { &mesh, 1 }, plane, map_slice_keep_t::BOTH, &ids ) == map_status_t::OK );
        REQUIRE( ids.nCount == 2 ); CHECK( ids.pData[0] == mesh ); const u64 back = ids.pData[1]; CHECK( back != mesh );
        CHECK( bounds( f, mesh ).minimum.z == Catch::Approx( 64 ) ); CHECK( bounds( f, mesh ).maximum.z == Catch::Approx( 112 ) );
        CHECK( bounds( f, back ).minimum.z == Catch::Approx( 16 ) ); CHECK( bounds( f, back ).maximum.z == Catch::Approx( 64 ) );
        CHECK( BoundaryEdges( f.map, mesh ) == 0 ); CHECK( BoundaryEdges( f.map, back ) == 0 );
        CHECK( description_t( f.map, mesh ).Faces() == 6 ); CHECK( description_t( f.map, back ).Faces() == 6 );
        Vector_Shutdown( &ids );
    }
    SECTION( "Front and Back keep one capped side; a mesh wholly on the other side is deleted, one wholly kept untouched" )
    {
        fixture_t f; const u64 mesh = f.BoxMesh();
        const u64 high = BoxMeshAt( f, { { 200, 0, 80 }, { 264, 64, 144 } } ); // Wholly above z = 64.
        const u64 pair[]{ mesh, high };
        const description_t untouched( f.map, high );
        vector_t<u64> ids{}; REQUIRE( Vector_Init( &ids, Allocator_GetSystem() ) );
        REQUIRE( MapTopologyEdit_ClipMeshes( &f.map, { pair, 2 }, plane, map_slice_keep_t::FRONT, &ids ) == map_status_t::OK );
        CHECK( ids.nCount == 2 );
        CHECK( bounds( f, mesh ).minimum.z == Catch::Approx( 64 ) ); CHECK( BoundaryEdges( f.map, mesh ) == 0 );
        const description_t after( f.map, high );
        CHECK( geo::MeshSourceDescription_Equal( &untouched.value, &after.value ) );
        Vector_Clear( &ids );
        REQUIRE( MapTopologyEdit_ClipMeshes( &f.map, { pair, 2 }, { { 0, 0, 1 }, -100.0 }, map_slice_keep_t::BACK, &ids ) == map_status_t::OK );
        CHECK( Source( f.map, mesh ) != nullptr ); CHECK( bounds( f, mesh ).maximum.z == Catch::Approx( 100 ) );
        CHECK( Source( f.map, high ) != nullptr ); CHECK( bounds( f, high ).maximum.z == Catch::Approx( 100 ) );
        REQUIRE( MapTopologyEdit_ClipMeshes( &f.map, { &high, 1 }, { { 0, 0, 1 }, -70.0 }, map_slice_keep_t::BACK, nullptr ) == map_status_t::OK );
        CHECK( Source( f.map, high ) == nullptr ); // Wholly above z = 70: removed.
        Vector_Shutdown( &ids );
    }
    SECTION( "Cap new surfaces off leaves the cut open" )
    {
        fixture_t f; const u64 mesh = f.BoxMesh();
        REQUIRE( MapTopologyEdit_ClipMeshes( &f.map, { &mesh, 1 }, plane, map_slice_keep_t::FRONT, nullptr, false ) == map_status_t::OK );
        CHECK( bounds( f, mesh ).minimum.z == Catch::Approx( 64 ) ); CHECK( BoundaryEdges( f.map, mesh ) == 4 );
    }
    SECTION( "The preview adds the kept halves to a presentation document" )
    {
        fixture_t f; const u64 mesh = f.BoxMesh();
        map_document_t preview{};
        REQUIRE( MapDocument_Create( &preview, Allocator_GetSystem(), { SV( "Clip preview" ), SV( "reap" ), {} } ) == map_status_t::OK );
        REQUIRE( MapTopologyEdit_AddClipPreview( &preview, &f.map, { &mesh, 1 }, plane, map_slice_keep_t::BACK ) == map_status_t::OK );
        const auto *kept = geo::GeometryDocument_FindMesh( &preview.geometry, { mesh } ); REQUIRE( kept != nullptr );
        CHECK( MapGeometry_MeshBounds( *kept, preview.pAllocator ).box.maximum.z == Catch::Approx( 64 ) );
        CHECK( bounds( f, mesh ).maximum.z == Catch::Approx( 112 ) ); // The map itself is untouched.
        MapDocument_Shutdown( &preview );
    }
}

TEST_CASE( "Convex meshes convert back to brushes with their materials and UVs", "[map][topology-edit][to-brush]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh();
    description_t before( f.map, mesh );
    REQUIRE( MapTopologyEdit_MeshesToBrushes( &f.map, { &mesh, 1 } ) == map_status_t::OK );
    CHECK( geo::GeometryDocument_FindMesh( &f.map.geometry, { mesh } ) == nullptr );
    geo::brush_source_t brush{};
    REQUIRE( geo::GeometryDocument_TryCopyBrushSource( &f.map.geometry, { mesh }, f.map.pAllocator, &brush ) == geo::geometry_status_t::OK );
    CHECK( brush.solid.sides.nCount == 6 );
    const auto bounds = MapGeometry_BrushBounds( brush, f.map.geometryPolicy, f.map.pAllocator );
    CHECK( bounds.box.minimum.x == Catch::Approx( -64 ) ); CHECK( bounds.box.maximum.z == Catch::Approx( 112 ) );
    // Every corner of the mesh projects through the fitted side to the UV it had.
    usize checked = 0;
    for ( usize f2 = 0; f2 < before.Faces(); ++f2 ) {
        const auto &face = before.value.faces.pData[f2];
        for ( usize i = 0; i < brush.solid.sides.nCount; ++i ) {
            geo::geometry_brush_side_attributes_t attributes{};
            REQUIRE( geo::BrushSideAttributeStore_TryGet( &brush.attributes, brush.solid.sides.pData[i].iAttributeIndex, &attributes ) == geo::geometry_status_t::OK );
            const auto &corner = before.value.corners.pData[face.iFirstCorner];
            const auto p = before.value.vertices.pData[corner.iVertex].position;
            const auto &plane = brush.solid.sides.pData[i].plane;
            if ( std::fabs( plane.normal.x * p.x + plane.normal.y * p.y + plane.normal.z * p.z + plane.d ) > 1e-6 ) { continue; }
            if ( attributes.material.value != face.attributes.material.value ) { continue; }
            math::vec2d_t uv{};
            REQUIRE( math::Uvd_TryProjectPlanarPoint( attributes.uvProjection, p, 0.0, &uv ) );
            if ( std::fabs( uv.x - corner.attributes.uv0.x ) < 1e-6 && std::fabs( uv.y - corner.attributes.uv0.y ) < 1e-6 ) { ++checked; break; }
        }
    }
    CHECK( checked == before.Faces() );
    geo::BrushSource_Shutdown( &brush );
    // An open mesh cannot become a brush.
    const u64 quad = f.Quad();
    CHECK( MapTopologyEdit_MeshesToBrushes( &f.map, { &quad, 1 } ) != map_status_t::OK );
}

TEST_CASE( "Mesh booleans union, subtract and intersect convex meshes", "[map][topology-edit][boolean]" )
{
    fixture_t f;
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
    SECTION( "Union of two touching boxes" )
    {
        const u64 ids[]{ BoxMeshAt( f, { { 0, 0, 0 }, { 64, 64, 64 } } ), BoxMeshAt( f, { { 64, 0, 0 }, { 128, 64, 64 } } ) };
        REQUIRE( MapTopologyEdit_Boolean( &f.map, { ids, 2 }, map_boolean_t::UNION, &roots ) == map_status_t::OK );
        REQUIRE( roots.nCount == 1 );
        CHECK( f.map.geometry.meshes.nCount == 1 ); CHECK( f.map.geometry.brushes.nCount == 0 );
        CHECK( MeshBox( f.map, roots.pData[0] ).maximum.x == Catch::Approx( 128 ) );
    }
    SECTION( "Subtract the newer from the older" )
    {
        const u64 big = BoxMeshAt( f, { { 0, 0, 0 }, { 128, 128, 128 } } ), corner = BoxMeshAt( f, { { 64, 64, 64 }, { 192, 192, 192 } } );
        const u64 ids[]{ corner, big };
        REQUIRE( MapTopologyEdit_Boolean( &f.map, { ids, 2 }, map_boolean_t::SUBTRACT, &roots ) == map_status_t::OK );
        REQUIRE( roots.nCount == 1 );
        CHECK( f.map.geometry.meshes.nCount == 1 ); CHECK( f.map.geometry.brushes.nCount == 0 );
        CHECK( MeshBox( f.map, roots.pData[0] ).maximum.x == Catch::Approx( 128 ) );
        CHECK( geo::GeometryDocument_FindMesh( &f.map.geometry, { corner } ) == nullptr );
    }
    SECTION( "Intersect two overlapping boxes" )
    {
        const u64 ids[]{ BoxMeshAt( f, { { 0, 0, 0 }, { 128, 128, 128 } } ), BoxMeshAt( f, { { 64, 32, 0 }, { 192, 96, 64 } } ) };
        REQUIRE( MapTopologyEdit_Boolean( &f.map, { ids, 2 }, map_boolean_t::INTERSECT, &roots ) == map_status_t::OK );
        REQUIRE( roots.nCount == 1 );
        const auto box = MeshBox( f.map, roots.pData[0] );
        CHECK( box.minimum.x == Catch::Approx( 64 ) ); CHECK( box.maximum.x == Catch::Approx( 128 ) );
        CHECK( box.minimum.y == Catch::Approx( 32 ) ); CHECK( box.maximum.z == Catch::Approx( 64 ) );
        CHECK( f.map.geometry.meshes.nCount == 1 );
    }
    Vector_Shutdown( &roots );
}

TEST_CASE( "Mesh CSG handles concave results: an L-shaped union, a tunnel, exclusive, split", "[map][topology-edit][boolean][csg]" )
{
    fixture_t f;
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
    SECTION( "An L-shaped union (concave: the brush path refused it)" )
    {
        const u64 ids[]{ BoxMeshAt( f, { { 0, 0, 0 }, { 128, 64, 64 } } ), BoxMeshAt( f, { { 0, 0, 0 }, { 64, 128, 64 } } ) };
        REQUIRE( MapTopologyEdit_Boolean( &f.map, { ids, 2 }, map_boolean_t::UNION, &roots ) == map_status_t::OK );
        REQUIRE( roots.nCount == 1 );
        CHECK( f.map.geometry.meshes.nCount == 1 ); CHECK( BoundaryEdges( f.map, roots.pData[0] ) == 0 );
        const auto box = MeshBox( f.map, roots.pData[0] );
        CHECK( box.maximum.x == Catch::Approx( 128 ) ); CHECK( box.maximum.y == Catch::Approx( 128 ) );
    }
    SECTION( "A tunnel through a box" )
    {
        const u64 block = BoxMeshAt( f, { { 0, 0, 0 }, { 128, 128, 128 } } ), bore = BoxMeshAt( f, { { -32, 32, 32 }, { 160, 96, 96 } } );
        const u64 ids[]{ block, bore };
        REQUIRE( MapTopologyEdit_Boolean( &f.map, { ids, 2 }, map_boolean_t::SUBTRACT, &roots ) == map_status_t::OK );
        REQUIRE( roots.nCount == 1 ); CHECK( roots.pData[0] == block );
        CHECK( f.map.geometry.meshes.nCount == 1 ); CHECK( BoundaryEdges( f.map, block ) == 0 );
        CHECK( MeshBox( f.map, block ).maximum.x == Catch::Approx( 128 ) );
        CHECK( description_t( f.map, block ).Faces() > 6 ); // The tunnel's walls.
    }
    SECTION( "Exclusive keeps what only one covers" )
    {
        const u64 ids[]{ BoxMeshAt( f, { { 0, 0, 0 }, { 128, 128, 128 } } ), BoxMeshAt( f, { { 64, 0, 0 }, { 192, 128, 128 } } ) };
        // Two closed pieces as two objects: A without the overlap, B without it.
        REQUIRE( MapTopologyEdit_Boolean( &f.map, { ids, 2 }, map_boolean_t::EXCLUSIVE, &roots ) == map_status_t::OK );
        REQUIRE( roots.nCount == 2 );
        CHECK( f.map.geometry.meshes.nCount == 2 );
        for ( usize i = 0; i < 2; ++i ) { CHECK( BoundaryEdges( f.map, roots.pData[i] ) == 0 ); }
        const auto a = MeshBox( f.map, roots.pData[0] ), b = MeshBox( f.map, roots.pData[1] );
        CHECK( a.maximum.x - a.minimum.x == Catch::Approx( 64 ) );
        CHECK( b.maximum.x - b.minimum.x == Catch::Approx( 64 ) );
        CHECK( std::min( a.minimum.x, b.minimum.x ) == Catch::Approx( 0 ) );
        CHECK( std::max( a.maximum.x, b.maximum.x ) == Catch::Approx( 192 ) );
    }
    SECTION( "Split cuts the older along the newer: inside and outside, the cutter stays" )
    {
        const u64 slab = BoxMeshAt( f, { { 0, 0, 0 }, { 256, 64, 64 } } ), knife = BoxMeshAt( f, { { 96, -32, -32 }, { 160, 96, 96 } } );
        const u64 ids[]{ slab, knife };
        REQUIRE( MapTopologyEdit_Boolean( &f.map, { ids, 2 }, map_boolean_t::SPLIT, &roots ) == map_status_t::OK );
        REQUIRE( roots.nCount == 2 );
        CHECK( f.map.geometry.meshes.nCount == 3 );
        CHECK( geo::GeometryDocument_FindMesh( &f.map.geometry, { knife } ) != nullptr );
        CHECK( MeshBox( f.map, roots.pData[0] ).maximum.x - MeshBox( f.map, roots.pData[0] ).minimum.x == Catch::Approx( 64 ) ); // The middle piece.
    }
    Vector_Shutdown( &roots );
}

TEST_CASE( "Component gizmo edits: transform vertices, extrude a face by a drag, extend edges, move a brush face", "[map][topology-edit][gizmo]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh(); // z 16..112
    description_t before( f.map, mesh );
    const usize topIndex = before.FaceAtZ( 112 );
    const u64 top = before.value.faces.pData[topIndex].sourceId.value;
    const auto corners = before.Corners( topIndex );
    SECTION( "Scale the top face's corners about its centre" )
    {
        const math::vec3d_t c{ 16, 16, 112 };
        const auto affine = math::Affine3d_Multiply( math::Affine3d_FromTranslation( c ),
            math::Affine3d_Multiply( math::Affine3d_FromScale( { 0.5, 0.5, 1 } ), math::Affine3d_FromTranslation( { -c.x, -c.y, -c.z } ) ) );
        REQUIRE( MapTopologyEdit_TransformVertices( &f.map, mesh, { corners.data(), corners.size() }, affine ) == map_status_t::OK );
        const auto box = MapGeometry_MeshBounds( *Source( f.map, mesh ), f.map.pAllocator ).box; // The base keeps its size.
        CHECK( box.maximum.x == Catch::Approx( 96 ) );
        description_t after( f.map, mesh );
        CHECK( after.Position( corners[0] ).z == Catch::Approx( 112 ) );
    }
    SECTION( "Shift-drag extrudes and slides the cap" )
    {
        REQUIRE( MapTopologyEdit_ExtrudeFaceBy( &f.map, mesh, top, { 16, 0, 32 } ) == map_status_t::OK );
        description_t after( f.map, mesh );
        CHECK( after.Faces() == 10 );
        const auto box = MapGeometry_MeshBounds( *Source( f.map, mesh ), f.map.pAllocator ).box;
        CHECK( box.maximum.z == Catch::Approx( 144 ) ); CHECK( box.maximum.x == Catch::Approx( 112 ) );
    }
    SECTION( "Extend open edges" )
    {
        const u64 quad = f.Quad();
        description_t q( f.map, quad );
        const auto c = q.Corners( 0 );
        const auto edge = Edge( c[0], c[1] );
        REQUIRE( MapTopologyEdit_ExtendEdgesBy( &f.map, quad, { &edge, 1 }, { 0, 0, 32 } ) == map_status_t::OK );
        CHECK( description_t( f.map, quad ).Faces() == 2 );
    }
    SECTION( "Move a brush face" )
    {
        const u64 brush = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 64, 64, 64 } } );
        const auto *solid = geo::GeometryDocument_FindBrush( &f.map.geometry, { brush } ); REQUIRE( solid );
        u64 topSide = 0;
        for ( usize i = 0; i < solid->sides.nCount; ++i ) { if ( solid->sides.pData[i].plane.normal.z > 0.99 ) { topSide = solid->sides.pData[i].sourceId.value; } }
        REQUIRE( topSide != 0 );
        REQUIRE( MapTopologyEdit_MoveBrushFace( &f.map, brush, topSide, { 32, 0, 0 } ) == map_status_t::OK ); // A shear: top slides along X.
        geo::brush_source_t copy{};
        REQUIRE( geo::GeometryDocument_TryCopyBrushSource( &f.map.geometry, { brush }, f.map.pAllocator, &copy ) == geo::geometry_status_t::OK );
        CHECK( MapGeometry_BrushBounds( copy, f.map.geometryPolicy, f.map.pAllocator ).box.maximum.x == Catch::Approx( 96 ) );
        geo::BrushSource_Shutdown( &copy );
    }
}

namespace
{
math::aabbd_t BrushBox( const map_document_t &map, u64 id )
{
    geo::brush_source_t copy{};
    REQUIRE( geo::GeometryDocument_TryCopyBrushSource( &map.geometry, { id }, map.pAllocator, &copy ) == geo::geometry_status_t::OK );
    const auto box = MapGeometry_BrushBounds( copy, map.geometryPolicy, map.pAllocator ).box;
    geo::BrushSource_Shutdown( &copy );
    return box;
}
u64 TopSide( const map_document_t &map, u64 brush )
{
    const auto *solid = geo::GeometryDocument_FindBrush( &map.geometry, { brush } ); REQUIRE( solid );
    for ( usize i = 0; i < solid->sides.nCount; ++i ) { if ( solid->sides.pData[i].plane.normal.z > 0.99 ) { return solid->sides.pData[i].sourceId.value; } }
    FAIL( "no top side" ); return 0;
}
} // namespace

TEST_CASE( "Weld and Merge by Distance join close vertices; nothing to join changes nothing", "[map][topology-edit][cleanup]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh();
    const auto revision = f.map.geometry.revision;
    usize merged = 99;
    REQUIRE( MapTopologyEdit_Weld( &f.map, { &mesh, 1 }, 1.0, &merged ) == map_status_t::OK );
    CHECK( merged == 0 ); CHECK( f.map.geometry.revision == revision );
    CHECK( MapTopologyEdit_Weld( &f.map, { &mesh, 1 }, 0.0, &merged ) == map_status_t::INVALID_ARGUMENT );
    // Pull one top corner to within half a unit of its neighbour.
    description_t before( f.map, mesh );
    const auto top = before.Corners( before.FaceAtZ( 112 ) );
    const auto a = before.Position( top[0] ), b = before.Position( top[1] );
    const math::vec3d_t near{ b.x - a.x + ( a.x < b.x ? -0.5 : a.x > b.x ? 0.5 : 0.0 ), b.y - a.y + ( a.y < b.y ? -0.5 : a.y > b.y ? 0.5 : 0.0 ), 0 };
    REQUIRE( MapTopologyEdit_MoveVertices( &f.map, mesh, { &top[0], 1 }, near ) == map_status_t::OK );
    SECTION( "Weld" )
    {
        REQUIRE( MapTopologyEdit_Weld( &f.map, { &mesh, 1 }, 1.0, &merged ) == map_status_t::OK );
        CHECK( merged == 1 ); CHECK( description_t( f.map, mesh ).Vertices() == 7 );
    }
    SECTION( "Merge by Distance" )
    {
        REQUIRE( MapTopologyEdit_MergeByDistance( &f.map, { &mesh, 1 }, 1.0, &merged ) == map_status_t::OK );
        CHECK( merged == 1 ); CHECK( description_t( f.map, mesh ).Vertices() == 7 );
    }
}

TEST_CASE( "Offset and Flatten move component vertices; Delete Faces opens the mesh", "[map][topology-edit][components]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh(); // z 16..112
    description_t before( f.map, mesh );
    const usize topIndex = before.FaceAtZ( 112 );
    const geo::geometry_source_id_t topFace = before.value.faces.pData[topIndex].sourceId;
    const auto top = before.Corners( topIndex );
    SECTION( "Offset the top face outward" )
    {
        map_mesh_components_t c{}; c.faces = { &topFace, 1 };
        REQUIRE( MapTopologyEdit_OffsetComponents( &f.map, mesh, c, 16.0 ) == map_status_t::OK );
        CHECK( description_t( f.map, mesh ).Position( top[0] ).z == Catch::Approx( 128 ) );
        CHECK( MapTopologyEdit_OffsetComponents( &f.map, mesh, c, 0.0 ) == map_status_t::INVALID_ARGUMENT );
    }
    SECTION( "Flatten Z levels a warped top" )
    {
        REQUIRE( MapTopologyEdit_MoveVertices( &f.map, mesh, { &top[0], 1 }, { 0, 0, 32 } ) == map_status_t::OK );
        map_mesh_components_t c{}; c.vertices = { top.data(), top.size() };
        REQUIRE( MapTopologyEdit_FlattenComponents( &f.map, mesh, c, map_flatten_t::AXIS_Z ) == map_status_t::OK );
        description_t after( f.map, mesh );
        for ( const auto &id : top ) { CHECK( after.Position( id ).z == Catch::Approx( after.Position( top[0] ).z ) ); }
        CHECK( after.Position( top[1] ).z > 112.0 ); // Levelled between the corners, not dropped back.
    }
    SECTION( "Delete the top face" )
    {
        const u64 face = topFace.value;
        REQUIRE( MapTopologyEdit_DeleteFaces( &f.map, mesh, { &face, 1 } ) == map_status_t::OK );
        CHECK( description_t( f.map, mesh ).Faces() == 5 ); CHECK( BoundaryEdges( f.map, mesh ) == 4 );
    }
}

TEST_CASE( "Slide Edge Loop moves a loop between its neighbours", "[map][topology-edit][slide]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh(); // z 16..112
    description_t before( f.map, mesh );
    // A vertical edge: its loop cut makes a ring at z 64 between two quad strips.
    geo::mesh_edge_ref_t vertical{};
    const auto top = before.Corners( before.FaceAtZ( 112 ) );
    for ( usize v = 0; v < before.Vertices(); ++v ) {
        const auto &p = before.value.vertices.pData[v];
        const auto q = before.Position( top[0] );
        if ( p.position.x == q.x && p.position.y == q.y && p.position.z == 16 ) { vertical = Edge( top[0], p.sourceId ); }
    }
    REQUIRE( vertical.a.value != 0 );
    REQUIRE( MapTopologyEdit_LoopCut( &f.map, mesh, vertical, 0.5 ) == map_status_t::OK );
    description_t cut( f.map, mesh );
    std::vector<geo::geometry_source_id_t> ring;
    for ( usize v = 0; v < cut.Vertices(); ++v ) { if ( cut.value.vertices.pData[v].position.z == Catch::Approx( 64 ) ) { ring.push_back( cut.value.vertices.pData[v].sourceId ); } }
    REQUIRE( ring.size() == 4 );
    // Two ring vertices joined by an edge.
    geo::mesh_edge_ref_t seed{};
    for ( usize i = 0; i < ring.size() && seed.a.value == 0; ++i ) {
        for ( usize j = i + 1; j < ring.size(); ++j ) {
            geo::geometry_mesh_edge_handle_t handle{};
            if ( geo::MeshSourceEdit_TryFindEdge( Source( f.map, mesh ), ring[i], ring[j], &handle ) ) { seed = { ring[i], ring[j] }; break; }
        }
    }
    REQUIRE( seed.a.value != 0 );
    REQUIRE( MapTopologyEdit_SlideEdgeLoop( &f.map, mesh, seed, 0.5 ) == map_status_t::OK );
    description_t slid( f.map, mesh );
    const f64 z = slid.Position( ring[0] ).z;
    CHECK( std::fabs( z - 64.0 ) == Catch::Approx( 24 ) ); // Halfway to the top or the bottom.
    for ( const auto &id : ring ) { CHECK( slid.Position( id ).z == Catch::Approx( z ) ); }
    CHECK( slid.Faces() == cut.Faces() );
    CHECK( MapTopologyEdit_SlideEdgeLoop( &f.map, mesh, seed, 1.0 ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Bridge Faces joins two faces of one mesh with a tube", "[map][topology-edit][bridge]" )
{
    fixture_t f;
    const u64 a = f.BoxMesh(); // x -64..96
    const u64 b = f.Primitive( map_primitive_kind_t::BOX, { { 200, -48, 16 }, { 300, 80, 112 } } );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &f.map, { &b, 1 }, &roots ) == map_status_t::OK );
    Vector_Shutdown( &roots );
    const u64 both[]{ a, b };
    u64 retained = 0;
    REQUIRE( MapTopologyEdit_JoinMeshes( &f.map, { both, 2 }, &retained ) == map_status_t::OK );
    description_t joined( f.map, retained );
    REQUIRE( joined.Faces() == 12 );
    u64 facing[2]{};
    for ( usize i = 0; i < joined.Faces(); ++i ) {
        bool atA = true, atB = true;
        for ( const auto &id : joined.Corners( i ) ) { atA &= joined.Position( id ).x == 96; atB &= joined.Position( id ).x == 200; }
        if ( atA ) { facing[0] = joined.value.faces.pData[i].sourceId.value; }
        if ( atB ) { facing[1] = joined.value.faces.pData[i].sourceId.value; }
    }
    REQUIRE( facing[0] != 0 ); REQUIRE( facing[1] != 0 );
    REQUIRE( MapTopologyEdit_BridgeFaces( &f.map, retained, facing[0], facing[1], 1 ) == map_status_t::OK );
    CHECK( description_t( f.map, retained ).Faces() == 12 - 2 + 4 );
    CHECK( BoundaryEdges( f.map, retained ) == 0 );
    CHECK( GenerationPool_Count( &Source( f.map, retained )->mesh.shells ) == 1 );
}

TEST_CASE( "Extruding a brush face to a new brush keeps or cuts the original", "[map][topology-edit][extrude-split]" )
{
    fixture_t f;
    const u64 brush = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 64, 64, 64 } } );
    u64 added = 0;
    REQUIRE( MapTopologyEdit_ExtrudeBrushFaceToNew( &f.map, brush, TopSide( f.map, brush ), 32.0, &added ) == map_status_t::OK );
    REQUIRE( added != 0 ); CHECK( added != brush );
    CHECK( BrushBox( f.map, brush ).maximum.z == Catch::Approx( 64 ) );
    CHECK( BrushBox( f.map, added ).minimum.z == Catch::Approx( 64 ) ); CHECK( BrushBox( f.map, added ).maximum.z == Catch::Approx( 96 ) );
    CHECK( f.map.nextId > added );
    u64 slab = 0;
    REQUIRE( MapTopologyEdit_ExtrudeBrushFaceToNew( &f.map, brush, TopSide( f.map, brush ), -16.0, &slab ) == map_status_t::OK );
    CHECK( BrushBox( f.map, brush ).maximum.z == Catch::Approx( 48 ) );
    CHECK( BrushBox( f.map, slab ).minimum.z == Catch::Approx( 48 ) ); CHECK( BrushBox( f.map, slab ).maximum.z == Catch::Approx( 64 ) );
    CHECK( MapTopologyEdit_ExtrudeBrushFaceToNew( &f.map, brush, TopSide( f.map, brush ), -64.0, &slab ) != map_status_t::OK ); // No volume left.
}

TEST_CASE( "Snap Edge to Edge welds one edge onto another; Set Vertex Positions moves each vertex", "[map][topology-edit][snap-edge]" )
{
    fixture_t f;
    const u64 left = f.Quad(); // x 0..128
    const u64 right = f.Primitive( map_primitive_kind_t::QUAD, { { 136, 0, 0 }, { 264, 64, 0 } }, 2 );
    const u64 both[]{ left, right };
    u64 joined = 0;
    REQUIRE( MapTopologyEdit_JoinMeshes( &f.map, { both, 2 }, &joined ) == map_status_t::OK );
    description_t before( f.map, joined );
    REQUIRE( before.Vertices() == 8 );
    std::vector<geo::geometry_source_id_t> at128, at136;
    for ( usize v = 0; v < before.Vertices(); ++v ) {
        const auto &vertex = before.value.vertices.pData[v];
        if ( vertex.position.x == 128 ) { at128.push_back( vertex.sourceId ); }
        if ( vertex.position.x == 136 ) { at136.push_back( vertex.sourceId ); }
    }
    REQUIRE( at128.size() == 2 ); REQUIRE( at136.size() == 2 );
    const auto source = Edge( at128[0], at128[1] ), target = Edge( at136[0], at136[1] );
    REQUIRE( MapTopologyEdit_SnapEdgeToEdge( &f.map, joined, source, target ) == map_status_t::OK );
    description_t after( f.map, joined );
    CHECK( after.Vertices() == 6 );
    CHECK( after.Position( at136[0] ).x == Catch::Approx( 136 ) ); // The target keeps its place.
    CHECK_FALSE( after.HasVertex( at128[0] ) );
    CHECK( BoundaryEdges( f.map, joined ) == 6 ); // Sewn: the shared edge is interior.
    CHECK( MapTopologyEdit_SnapEdgeToEdge( &f.map, joined, target, target ) == map_status_t::INVALID_ARGUMENT );

    const auto corner = after.Corners( 0 )[0];
    const math::vec3d_t to{ -16, -16, 8 };
    REQUIRE( MapTopologyEdit_SetVertexPositions( &f.map, joined, { &corner, 1 }, { &to, 1 } ) == map_status_t::OK );
    CHECK( description_t( f.map, joined ).Position( corner ).z == Catch::Approx( 8 ) );
    CHECK( MapTopologyEdit_SetVertexPositions( &f.map, joined, { &corner, 1 }, {} ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Split Edges splits several edges in one edit", "[map][topology-edit][split-many]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh();
    description_t before( f.map, mesh );
    const auto top = before.Corners( before.FaceAtZ( 112 ) );
    const geo::mesh_edge_ref_t edges[]{ Edge( top[0], top[1] ), Edge( top[2], top[3] ) };
    const auto status = MapTopologyEdit_SplitEdges( &f.map, mesh, { edges, 2 } );
    CHECK( status == map_status_t::OK );
    CHECK( description_t( f.map, mesh ).Vertices() == 10 );
}

TEST_CASE( "Split Edges splits open border edges, alone and in a joined mesh", "[map][topology-edit][split-border]" )
{
    fixture_t f; const u64 quad = f.Quad();
    description_t q( f.map, quad );
    const auto c = q.Corners( 0 );
    const auto edge = Edge( c[0], c[1] );
    REQUIRE( MapTopologyEdit_SplitEdges( &f.map, quad, { &edge, 1 } ) == map_status_t::OK );
    description_t after( f.map, quad );
    CHECK( after.Vertices() == 5 ); CHECK( after.Corners( 0 ).size() == 5 );
    CHECK( BoundaryEdges( f.map, quad ) == 5 ); // Both halves stay open.
    const u64 other = f.Primitive( map_primitive_kind_t::QUAD, { { 192, 0, 0 }, { 320, 64, 0 } }, 2 );
    const u64 both[]{ quad, other };
    u64 joined = 0;
    REQUIRE( MapTopologyEdit_JoinMeshes( &f.map, { both, 2 }, &joined ) == map_status_t::OK );
    description_t j( f.map, joined );
    usize f0 = 0;
    for ( usize i = 0; i < j.Faces(); ++i ) { if ( j.Corners( i ).size() == 4 ) { f0 = i; } }
    const auto d = j.Corners( f0 );
    const auto e2 = Edge( d[0], d[1] );
    CHECK( MapTopologyEdit_SplitEdges( &f.map, joined, { &e2, 1 } ) == map_status_t::OK );
}

TEST_CASE( "Make Room hollows a brush with its walls outside", "[map][topology-edit][make-room]" )
{
    fixture_t f;
    const u64 brush = f.Primitive( map_primitive_kind_t::BOX, { { 0, 0, 0 }, { 64, 64, 64 } } );
    vector_t<u64> walls{}; REQUIRE( Vector_Init( &walls, Allocator_GetSystem() ) );
    REQUIRE( MapTopologyEdit_MakeRoom( &f.map, { &brush, 1 }, 16.0, &walls ) == map_status_t::OK );
    REQUIRE( walls.nCount >= 6 );
    math::aabbd_t all{ { 1e9, 1e9, 1e9 }, { -1e9, -1e9, -1e9 } };
    for ( usize i = 0; i < walls.nCount; ++i ) {
        const auto box = BrushBox( f.map, walls.pData[i] );
        all.minimum.x = std::min( all.minimum.x, box.minimum.x ); all.maximum.x = std::max( all.maximum.x, box.maximum.x );
        all.minimum.z = std::min( all.minimum.z, box.minimum.z ); all.maximum.z = std::max( all.maximum.z, box.maximum.z );
        // No wall reaches into the room.
        const bool intoRoom = box.minimum.x < 64 - 1e-6 && box.maximum.x > 0 + 1e-6 && box.minimum.y < 64 - 1e-6 && box.maximum.y > 0 + 1e-6 &&
                              box.minimum.z < 64 - 1e-6 && box.maximum.z > 0 + 1e-6;
        CHECK_FALSE( intoRoom );
    }
    CHECK( all.minimum.x == Catch::Approx( -16 ) ); CHECK( all.maximum.x == Catch::Approx( 80 ) );
    CHECK( all.minimum.z == Catch::Approx( -16 ) ); CHECK( all.maximum.z == Catch::Approx( 80 ) );
    Vector_Shutdown( &walls );
}

TEST_CASE( "Dissolve and Bevel work on vertices", "[map][topology-edit][vertex-ops]" )
{
    SECTION( "Dissolving a box corner leaves a slanted triangle in its place" )
    {
        fixture_t f; const u64 mesh = f.BoxMesh(); // 8 vertices, 6 quads
        description_t before( f.map, mesh );
        const auto corner = before.Corners( before.FaceAtZ( 112 ) )[0];
        REQUIRE( MapTopologyEdit_DissolveVertices( &f.map, mesh, { &corner, 1 } ) == map_status_t::OK );
        description_t after( f.map, mesh );
        CHECK( after.Vertices() == 7 );
        CHECK_FALSE( after.HasVertex( corner ) );
        CHECK( BoundaryEdges( f.map, mesh ) == 0 ); // Still closed.
    }
    SECTION( "Bevelling a box corner cuts it off with a three-sided cap" )
    {
        fixture_t f; const u64 mesh = f.BoxMesh();
        description_t before( f.map, mesh );
        const auto corner = before.Corners( before.FaceAtZ( 112 ) )[0];
        REQUIRE( MapTopologyEdit_BevelVertices( &f.map, mesh, { &corner, 1 }, 16.0 ) == map_status_t::OK );
        description_t after( f.map, mesh );
        CHECK( after.Vertices() == 8 - 1 + 3 );
        CHECK( after.Faces() == 6 + 1 );
        CHECK_FALSE( after.HasVertex( corner ) );
        CHECK( BoundaryEdges( f.map, mesh ) == 0 );
        bool triangle = false;
        for ( usize i = 0; i < after.Faces(); ++i ) { triangle |= after.Corners( i ).size() == 3; }
        CHECK( triangle );
    }
    SECTION( "Border vertices are refused" )
    {
        fixture_t f; const u64 quad = f.Quad();
        description_t q( f.map, quad );
        const auto corner = q.Corners( 0 )[0];
        CHECK( MapTopologyEdit_DissolveVertices( &f.map, quad, { &corner, 1 } ) == map_status_t::INVALID_ARGUMENT );
        CHECK( MapTopologyEdit_BevelVertices( &f.map, quad, { &corner, 1 }, 8.0 ) == map_status_t::INVALID_ARGUMENT );
    }
}

TEST_CASE( "Connect cuts every face its edges share; Fill Hole closes several holes at once", "[map][topology-edit][multi-face]" )
{
    SECTION( "Connecting the four vertical edges of a box cuts its four sides" )
    {
        fixture_t f; const u64 mesh = f.BoxMesh(); // z 16..112
        description_t before( f.map, mesh );
        const auto top = before.Corners( before.FaceAtZ( 112 ) );
        std::vector<geo::mesh_edge_ref_t> verticals;
        for ( const auto &t : top ) {
            const auto p = before.Position( t );
            for ( usize v = 0; v < before.Vertices(); ++v ) {
                const auto &q = before.value.vertices.pData[v];
                if ( q.position.x == p.x && q.position.y == p.y && q.position.z == 16 ) { verticals.push_back( Edge( t, q.sourceId ) ); }
            }
        }
        REQUIRE( verticals.size() == 4 );
        REQUIRE( MapTopologyEdit_ConnectEdges( &f.map, mesh, { verticals.data(), verticals.size() } ) == map_status_t::OK );
        CHECK( description_t( f.map, mesh ).Faces() == 6 + 4 );
        CHECK( BoundaryEdges( f.map, mesh ) == 0 );
    }
    SECTION( "Two holes fill in one call" )
    {
        fixture_t f; const u64 mesh = f.BoxMesh();
        description_t before( f.map, mesh );
        const u64 caps[]{ before.value.faces.pData[before.FaceAtZ( 112 )].sourceId.value, before.value.faces.pData[before.FaceAtZ( 16 )].sourceId.value };
        const auto top = before.Corners( before.FaceAtZ( 112 ) ), bottom = before.Corners( before.FaceAtZ( 16 ) );
        REQUIRE( MapTopologyEdit_DeleteFaces( &f.map, mesh, { caps, 2 } ) == map_status_t::OK );
        REQUIRE( BoundaryEdges( f.map, mesh ) == 8 );
        const geo::mesh_edge_ref_t rims[]{ Edge( top[0], top[1] ), Edge( bottom[0], bottom[1] ) };
        REQUIRE( MapTopologyEdit_FillHoles( &f.map, mesh, { rims, 2 } ) == map_status_t::OK );
        CHECK( description_t( f.map, mesh ).Faces() == 6 );
        CHECK( BoundaryEdges( f.map, mesh ) == 0 );
    }
}

TEST_CASE( "Clip preview and commit agree for open modelling surfaces and closed solids", "[map][topology-edit][clip-regression]" )
{
    for ( const auto keep : { map_slice_keep_t::FRONT, map_slice_keep_t::BACK, map_slice_keep_t::BOTH } ) {
        for ( const bool cap : { false, true } ) {
            for ( const int shape : { 0, 1, 2 } ) {
                INFO( "keep=" << static_cast<int>( keep ) << " cap=" << cap << " shape=" << shape );
                fixture_t f;
                const u64 mesh = shape == 2 ? f.BoxMesh() : f.Quad();
                if ( shape == 1 ) {
                    description_t quad( f.map, mesh );
                    const auto corners = quad.Corners( 0 );
                    const auto edge = Edge( corners[0], corners[1] );
                    REQUIRE( MapTopologyEdit_ExtendEdgesBy( &f.map, mesh, { &edge, 1 }, { 0, 0, 32 } ) == map_status_t::OK );
                }
                description_t before( f.map, mesh );
                const math::planed_t plane{ { 1, 0, 0 }, -32.0 };
                fixture_t preview;
                REQUIRE( MapTopologyEdit_AddClipPreview( &preview.map, &f.map, { &mesh, 1 }, plane, keep, cap ) == map_status_t::OK );
                description_t unchanged( f.map, mesh );
                CHECK( geo::MeshSourceDescription_Equal( &before.value, &unchanged.value ) );
                vector_t<u64> retained{};
                REQUIRE( Vector_Init( &retained, f.map.pAllocator ) );
                REQUIRE( MapTopologyEdit_ClipMeshes( &f.map, { &mesh, 1 }, plane, keep, &retained, cap ) == map_status_t::OK );
                const usize expected = keep == map_slice_keep_t::BOTH ? 2u : 1u;
                REQUIRE( retained.nCount == expected );
                REQUIRE( geo::GeometryDocument_MeshCount( &preview.map.geometry ) == expected );
                for ( usize i = 0; i < expected; ++i ) {
                    const auto *ghost = geo::GeometryDocument_MeshAt( &preview.map.geometry, i );
                    REQUIRE( ghost != nullptr );
                    description_t actual( f.map, retained.pData[i] ), shown( preview.map, ghost->sourceId.value );
                    CHECK( actual.Faces() == shown.Faces() );
                    CHECK( actual.Vertices() == shown.Vertices() );
                    CHECK( BoundaryEdges( f.map, retained.pData[i] ) == BoundaryEdges( preview.map, ghost->sourceId.value ) );
                    if ( shape == 2 && cap ) { CHECK( BoundaryEdges( f.map, retained.pData[i] ) == 0 ); }
                    else { CHECK( BoundaryEdges( f.map, retained.pData[i] ) != 0 ); }
                    const auto positions = []( const description_t &d ) {
                        std::vector<math::vec3d_t> result;
                        for ( usize v = 0; v < d.value.vertices.nCount; ++v ) { result.push_back( d.value.vertices.pData[v].position ); }
                        std::sort( result.begin(), result.end(), []( const auto &a, const auto &b ) {
                            return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
                        } );
                        return result;
                    };
                    const auto a = positions( actual ), b = positions( shown );
                    REQUIRE( a.size() == b.size() );
                    for ( usize v = 0; v < a.size(); ++v ) {
                        CHECK( a[v].x == Catch::Approx( b[v].x ) ); CHECK( a[v].y == Catch::Approx( b[v].y ) ); CHECK( a[v].z == Catch::Approx( b[v].z ) );
                    }
                }
                Vector_Shutdown( &retained );
            }
        }
    }
}

TEST_CASE( "Clip caps closed shells even when the same mesh contains an open crossing sheet", "[map][topology-edit][clip-shell-caps]" )
{
    for ( const auto keep : { map_slice_keep_t::FRONT, map_slice_keep_t::BACK, map_slice_keep_t::BOTH } ) {
        for ( const bool cap : { false, true } ) {
            DYNAMIC_SECTION( "keep=" << static_cast<int>( keep ) << " cap=" << cap ) {
                fixture_t f;
                const u64 box = f.BoxMesh();
                const u64 sheet = f.Primitive( map_primitive_kind_t::QUAD, { { 0, 160, 48 }, { 128, 224, 48 } } );
                const u64 roots[]{ box, sheet }; u64 mesh = 0;
                REQUIRE( MapTopologyEdit_JoinMeshes( &f.map, { roots, 2 }, &mesh ) == map_status_t::OK );
                REQUIRE( BoundaryEdges( f.map, mesh ) == 4 );
                REQUIRE( geo::EditableMesh_ShellCount( &Source( f.map, mesh )->mesh ) == 2 );
                description_t before( f.map, mesh ); saved_t savedBefore; REQUIRE( savedBefore.Save( &f.map ) == map_status_t::OK );
                const auto revision = f.map.geometry.revision, nextId = f.map.nextId, nextSourceId = f.map.geometry.sourceIds.allocator.next.value;
                const math::planed_t plane{ { 1, 0, 0 }, -32.0 };
                fixture_t preview;
                REQUIRE( MapTopologyEdit_AddClipPreview( &preview.map, &f.map, { &mesh, 1 }, plane, keep, cap ) == map_status_t::OK );
                CHECK( f.map.geometry.revision == revision ); CHECK( f.map.nextId == nextId ); CHECK( f.map.geometry.sourceIds.allocator.next.value == nextSourceId );
                { saved_t stillSaved; REQUIRE( stillSaved.Save( &f.map ) == map_status_t::OK ); CHECK( stillSaved.root == savedBefore.root ); }
                vector_t<u64> kept{}; REQUIRE( Vector_Init( &kept, f.map.pAllocator ) );
                REQUIRE( MapTopologyEdit_ClipMeshes( &f.map, { &mesh, 1 }, plane, keep, &kept, cap ) == map_status_t::OK );
                const usize count = keep == map_slice_keep_t::BOTH ? 2u : 1u;
                REQUIRE( kept.nCount == count ); REQUIRE( geo::GeometryDocument_MeshCount( &preview.map.geometry ) == count );
                for ( usize i = 0; i < count; ++i ) {
                    const auto *shown = geo::GeometryDocument_MeshAt( &preview.map.geometry, i ); REQUIRE( shown != nullptr );
                    description_t actual( f.map, kept.pData[i] ), ghost( preview.map, shown->sourceId.value );
                    CHECK( actual.Faces() == ( cap ? 7u : 6u ) ); CHECK( ghost.Faces() == actual.Faces() );
                    CHECK( actual.Vertices() == 12u ); CHECK( ghost.Vertices() == actual.Vertices() );
                    CHECK( BoundaryEdges( f.map, kept.pData[i] ) == ( cap ? 4u : 8u ) );
                    CHECK( BoundaryEdges( preview.map, shown->sourceId.value ) == BoundaryEdges( f.map, kept.pData[i] ) );
                    usize caps = 0;
                    for ( usize face = 0; face < actual.Faces(); ++face ) {
                        bool inPlane = true;
                        for ( const auto vertex : actual.Corners( face ) ) { inPlane &= actual.Position( vertex ).x == 32.0; }
                        caps += inPlane ? 1u : 0u;
                    }
                    CHECK( caps == ( cap ? 1u : 0u ) );
                }
                saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK ); map_document_t loaded; saved.Load( loaded );
                for ( usize i = 0; i < count; ++i ) {
                    description_t actual( f.map, kept.pData[i] ), persisted( loaded, kept.pData[i] );
                    CHECK( geo::MeshSourceDescription_Equal( &actual.value, &persisted.value ) );
                }
            }
        }
    }
}

TEST_CASE( "A clipped closed shell preserves an untouched open shell's identities and surfacing", "[map][topology-edit][clip-shell-caps][metadata]" )
{
    fixture_t f; const u64 box = f.BoxMesh();
    const u64 sheet = f.Primitive( map_primitive_kind_t::QUAD, { { -64, 160, 48 }, { 0, 224, 48 } } );
    description_t originalSheet( f.map, sheet ); u64 material = 0;
    REQUIRE( MapDocument_MaterialRef( &f.map, SV( "materials/untouched_sheet.cymat" ), &material ) == map_status_t::OK );
    struct setup_t { const geo::mesh_source_description_t *source; u64 material; } setup{ &originalSheet.value, material };
    REQUIRE( MapMeshEdit_Apply( &f.map, sheet, []( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        const auto &setup = *static_cast<setup_t *>( context ); const auto &face = setup.source->faces.pData[0];
        auto attributes = face.attributes; attributes.material.value = setup.material; attributes.smoothingGroups = 16;
        auto status = geo::MeshSourceEdit_TrySetFaceAttributes( working, face.sourceId, attributes );
        for ( u32 k = 0; status == geo::geometry_status_t::OK && k < face.cCorners; ++k ) {
            const auto &corner = setup.source->corners.pData[face.iFirstCorner + k];
            auto uv = corner.attributes; uv.uv0 = { 3.0 + k, -7.0 + k }; uv.uv1 = { 0.125 * k, 0.25 * k }; uv.colorRgba = 0x204060FFu + ( k << 8 );
            status = geo::MeshSourceEdit_TrySetCornerAttributes( working, face.sourceId, setup.source->vertices.pData[corner.iVertex].sourceId, uv );
        }
        return status;
    }, &setup ) == map_status_t::OK );
    description_t before( f.map, sheet ); const auto &faceBefore = before.value.faces.pData[0];
    const u64 roots[]{ box, sheet }; u64 mesh = 0;
    REQUIRE( MapTopologyEdit_JoinMeshes( &f.map, { roots, 2 }, &mesh ) == map_status_t::OK );
    REQUIRE( MapTopologyEdit_ClipMeshes( &f.map, { &mesh, 1 }, { { 1, 0, 0 }, -32 }, map_slice_keep_t::BACK, nullptr, true ) == map_status_t::OK );
    CHECK( BoundaryEdges( f.map, mesh ) == 4u );
    description_t after( f.map, mesh ); const geo::mesh_source_face_t *faceAfter = nullptr;
    for ( usize i = 0; i < after.Faces(); ++i ) { if ( after.value.faces.pData[i].sourceId.value == faceBefore.sourceId.value ) { faceAfter = &after.value.faces.pData[i]; } }
    REQUIRE( faceAfter != nullptr ); REQUIRE( faceAfter->cCorners == faceBefore.cCorners );
    CHECK( faceAfter->attributes.material.value == material ); CHECK( faceAfter->attributes.smoothingGroups == 16u );
    for ( u32 k = 0; k < faceBefore.cCorners; ++k ) {
        const auto &old = before.value.corners.pData[faceBefore.iFirstCorner + k], &now = after.value.corners.pData[faceAfter->iFirstCorner + k];
        const auto &oldVertex = before.value.vertices.pData[old.iVertex], &nowVertex = after.value.vertices.pData[now.iVertex];
        CHECK( nowVertex.sourceId.value == oldVertex.sourceId.value ); CHECK( math::Vec3d_EqualsExact( nowVertex.position, oldVertex.position ) );
        CHECK( now.attributes.uv0.x == old.attributes.uv0.x ); CHECK( now.attributes.uv0.y == old.attributes.uv0.y );
        CHECK( now.attributes.uv1.x == old.attributes.uv1.x ); CHECK( now.attributes.uv1.y == old.attributes.uv1.y ); CHECK( now.attributes.colorRgba == old.attributes.colorRgba );
    }
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK ); map_document_t loaded; saved.Load( loaded );
    description_t persisted( loaded, mesh ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &persisted.value ) );
}

TEST_CASE( "Boundary edge pulls publish new persistent tips for repeated translation and pivot rotation", "[map][topology-edit][edge-pull]" )
{
    fixture_t f; const u64 mesh = f.Quad();
    description_t before( f.map, mesh );
    const auto corners = before.Corners( 0u );
    const auto edge = Edge( corners[0], corners[1] );
    vector_t<geo::mesh_edge_ref_t> outer{}; REQUIRE( Vector_Init( &outer, f.map.pAllocator ) );
    REQUIRE( Vector_PushBack( &outer, Edge( { 991u }, { 992u } ) ) );
    const auto nextId = f.map.nextId;
    REQUIRE( MapTopologyEdit_ExtendEdgesBy( &f.map, mesh, { &edge, 1u }, { 0, 0, 32 }, &outer ) == map_status_t::OK );
    REQUIRE( outer.nCount == 1u );
    CHECK( outer.pData[0].a.value >= nextId ); CHECK( outer.pData[0].b.value >= nextId );
    const auto firstTip = outer.pData[0];
    // Output may be reused as input; it is replaced only after all IDs resolve.
    REQUIRE( MapTopologyEdit_ExtendEdgesBy( &f.map, mesh, { outer.pData, outer.nCount }, { 0, 32, 0 }, &outer ) == map_status_t::OK );
    REQUIRE( outer.nCount == 1u );
    CHECK( outer.pData[0].a.value != firstTip.a.value ); CHECK( outer.pData[0].b.value != firstTip.b.value );
    description_t twice( f.map, mesh );
    CHECK( twice.Faces() == 3u ); CHECK( twice.Vertices() == 8u );
    for ( const auto id : corners ) {
        const auto old = before.Position( id ), now = twice.Position( id );
        CHECK( now.x == old.x ); CHECK( now.y == old.y ); CHECK( now.z == old.z );
    }
    const auto tip = outer.pData[0];
    const auto a = twice.Position( tip.a ), b = twice.Position( tip.b );
    const math::vec3d_t middle{ ( a.x + b.x ) * 0.5, ( a.y + b.y ) * 0.5, ( a.z + b.z ) * 0.5 };
    // Turn around a pivot 256 units outside the edge, keeping the strip
    // away from its source instead of folding a rotation through itself.
    const double edgeLength = std::hypot( b.x - a.x, b.y - a.y );
    REQUIRE( edgeLength > 0.0 );
    const math::vec3d_t pivot{ middle.x - ( b.x - a.x ) * 256.0 / edgeLength,
        middle.y - ( b.y - a.y ) * 256.0 / edgeLength, middle.z };
    constexpr double angle = 0.2;
    const auto rotation = math::Affine3d_FromColumns( { std::cos( angle ), std::sin( angle ), 0 },
        { -std::sin( angle ), std::cos( angle ), 0 }, { 0, 0, 1 }, {} );
    const auto affine = math::Affine3d_Multiply( math::Affine3d_FromTranslation( pivot ),
        math::Affine3d_Multiply( rotation, math::Affine3d_FromTranslation( { -pivot.x, -pivot.y, -pivot.z } ) ) );
    REQUIRE( MapTopologyEdit_ExtendEdgesTransformed( &f.map, mesh, { &tip, 1u }, affine, &outer ) == map_status_t::OK );
    description_t turned( f.map, mesh ); CHECK( turned.Faces() == 4u ); CHECK( turned.Vertices() == 10u );
    REQUIRE( outer.nCount == 1u );
    const auto newA = turned.Position( outer.pData[0].a ), newB = turned.Position( outer.pData[0].b );
    const auto expectedA = math::Affine3d_TransformPoint( affine, a ), expectedB = math::Affine3d_TransformPoint( affine, b );
    const auto matches = []( math::vec3d_t p, math::vec3d_t q ) {
        return std::abs( p.x - q.x ) < 1e-8 && std::abs( p.y - q.y ) < 1e-8 && std::abs( p.z - q.z ) < 1e-8;
    };
    CHECK( ( ( matches( newA, expectedA ) && matches( newB, expectedB ) ) || ( matches( newA, expectedB ) && matches( newB, expectedA ) ) ) );
    CHECK( matches( turned.Position( tip.a ), a ) ); CHECK( matches( turned.Position( tip.b ), b ) );
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded; saved.Load( loaded );
    geo::geometry_mesh_edge_handle_t handle{};
    REQUIRE( geo::MeshSourceEdit_TryFindEdge( Source( loaded, mesh ), outer.pData[0].a, outer.pData[0].b, &handle ) );
    CHECK( geo::MeshBoundary_IsBoundaryEdge( &Source( loaded, mesh )->mesh, handle ) );
    Vector_Shutdown( &outer );
}

TEST_CASE( "Boundary edge pull outputs are canonical sorted unique and preserved on rejection", "[map][topology-edit][edge-pull]" )
{
    fixture_t f; const u64 mesh = f.Quad(); description_t before( f.map, mesh );
    const auto c = before.Corners( 0u );
    const geo::mesh_edge_ref_t chain[]{ Edge( c[1], c[2] ), Edge( c[0], c[1] ) };
    vector_t<geo::mesh_edge_ref_t> outer{}; REQUIRE( Vector_Init( &outer, f.map.pAllocator ) );
    REQUIRE( MapTopologyEdit_ExtendEdgesBy( &f.map, mesh, { chain, 2u }, { 0, 0, 32 }, &outer ) == map_status_t::OK );
    REQUIRE( outer.nCount == 2u );
    CHECK( outer.pData[0].a.value < outer.pData[0].b.value ); CHECK( outer.pData[1].a.value < outer.pData[1].b.value );
    CHECK( ( outer.pData[0].a.value < outer.pData[1].a.value ||
        ( outer.pData[0].a.value == outer.pData[1].a.value && outer.pData[0].b.value < outer.pData[1].b.value ) ) );
    CHECK( description_t( f.map, mesh ).Vertices() == 7u ); // Shared corner gets one copy.
    const auto tip = outer.pData[0];
    const auto nextId = f.map.nextId;
    const auto revision = f.map.geometry.revision;
    description_t extended( f.map, mesh );
    CHECK( MapTopologyEdit_ExtendEdgesTransformed( &f.map, mesh, { &tip, 1u }, math::CY_AFFINE3D_IDENTITY, &outer ) == map_status_t::GEOMETRY_FAILED );
    REQUIRE( outer.nCount == 2u ); CHECK( outer.pData[0].a.value == tip.a.value );
    CHECK( f.map.nextId == nextId ); CHECK( f.map.geometry.revision == revision );
    description_t rejected( f.map, mesh ); CHECK( geo::MeshSourceDescription_Equal( &extended.value, &rejected.value ) );
    f.map.bReadOnly = CY_TRUE;
    CHECK( MapTopologyEdit_ExtendEdgesBy( &f.map, mesh, { &tip, 1u }, { 0, 0, 16 }, &outer ) == map_status_t::READ_ONLY );
    CHECK( outer.nCount == 2u );
    Vector_Shutdown( &outer );
}

namespace {
struct edge_pull_audit_t {
    usize calls{}, bytes{}, failAt{ CY_USIZE_MAX };
    allocator_t allocator{};
    edge_pull_audit_t() {
        allocator.pUserData = this;
        allocator.pfnAllocate = []( void *context, usize size, usize alignment ) noexcept -> void * {
            auto &audit = *static_cast<edge_pull_audit_t *>( context );
            if ( audit.calls++ == audit.failAt ) { return nullptr; }
            void *memory = Allocator_Allocate( Allocator_GetSystem(), size, alignment );
            if ( memory != nullptr ) { audit.bytes += size; }
            return memory;
        };
        allocator.pfnFree = []( void *context, void *memory, usize size, usize alignment ) noexcept {
            if ( memory != nullptr ) { static_cast<edge_pull_audit_t *>( context )->bytes -= size; }
            Allocator_Free( Allocator_GetSystem(), memory, size, alignment );
        };
    }
};
}

TEST_CASE( "Each edge pull allocation failure preserves original map and selected-tip output", "[map][topology-edit][edge-pull][oom]" )
{
    edge_pull_audit_t audit;
    {
        map_document_t original;
        REQUIRE( MapDocument_Create( &original, &audit.allocator, { SV( "Edge pull failures" ), SV( "reap" ), {} } ) == map_status_t::OK );
        map_primitive_desc_t primitive{}; primitive.kind = map_primitive_kind_t::QUAD;
        primitive.bounds = { { 0, 0, 0 }, { 128, 64, 0 } }; primitive.axis = 2;
        u64 mesh = 0u;
        REQUIRE( MapEdit_CreatePrimitive( &original, primitive, SV( "materials/source.cymat" ), {}, &mesh ) == map_status_t::OK );
        description_t before( original, mesh ); const auto c = before.Corners( 0u );
        const auto edge = Edge( c[0], c[1] );
        const auto nextId = original.nextId, revision = original.geometry.revision;
        const usize liveBytes = audit.bytes;
        usize allocationCount = 0u;
        {
            map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
            vector_t<geo::mesh_edge_ref_t> output{}; REQUIRE( Vector_Init( &output, &audit.allocator ) );
            REQUIRE( Vector_PushBack( &output, Edge( { 991u }, { 992u } ) ) );
            audit.calls = 0u;
            REQUIRE( MapTopologyEdit_ExtendEdgesBy( copy.get(), mesh, { &edge, 1u }, { 0, 0, 32 }, &output ) == map_status_t::OK );
            allocationCount = audit.calls;
            Vector_Shutdown( &output );
        }
        REQUIRE( allocationCount > 16u ); CHECK( audit.bytes == liveBytes );
        for ( usize failure = 0u; failure < allocationCount; ++failure ) {
            CAPTURE( failure, allocationCount );
            {
                audit.failAt = CY_USIZE_MAX;
                map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                vector_t<geo::mesh_edge_ref_t> output{}; REQUIRE( Vector_Init( &output, &audit.allocator ) );
                REQUIRE( Vector_PushBack( &output, Edge( { 991u }, { 992u } ) ) );
                audit.calls = 0u; audit.failAt = failure;
                CHECK( MapTopologyEdit_ExtendEdgesBy( copy.get(), mesh, { &edge, 1u }, { 0, 0, 32 }, &output ) != map_status_t::OK );
                audit.failAt = CY_USIZE_MAX;
                REQUIRE( output.nCount == 1u ); CHECK( output.pData[0].a.value == 991u ); CHECK( output.pData[0].b.value == 992u );
                Vector_Shutdown( &output );
            }
            CHECK( audit.bytes == liveBytes );
            CHECK( original.nextId == nextId ); CHECK( original.geometry.revision == revision );
            description_t after( original, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
        }
    }
    CHECK( audit.bytes == 0u );
}

namespace {
// Two visible handles in Top view each stand for a top and bottom vertex.
std::vector<geo::geometry_source_id_t> OppositeVertexColumns( const description_t &mesh, f64 topZ )
{
    std::vector<geo::geometry_source_id_t> top;
    for ( usize f = 0; f < mesh.Faces(); ++f ) {
        const auto corners = mesh.Corners( f );
        if ( std::all_of( corners.begin(), corners.end(), [&]( auto id ) { return std::abs( mesh.Position( id ).z - topZ ) < 1e-6; } ) ) {
            top = corners; break;
        }
    }
    REQUIRE( top.size() >= 4u );
    const auto a = mesh.Position( top[0] ), b = mesh.Position( top[top.size() / 2] );
    std::vector<geo::geometry_source_id_t> selected;
    for ( usize i = 0; i < mesh.Vertices(); ++i ) {
        const auto &v = mesh.value.vertices.pData[i];
        if ( ( std::abs( v.position.x - a.x ) < 1e-6 && std::abs( v.position.y - a.y ) < 1e-6 ) ||
             ( std::abs( v.position.x - b.x ) < 1e-6 && std::abs( v.position.y - b.y ) < 1e-6 ) ) { selected.push_back( v.sourceId ); }
    }
    REQUIRE( selected.size() == 4u );
    return selected;
}
}

TEST_CASE( "Vertex Connect cuts both caps selected through orthographic columns without flattening them", "[map][topology-edit][vertex-connect]" )
{
    fixture_t f;
    const u64 mesh = f.Primitive( map_primitive_kind_t::CYLINDER, { { -128, -128, 0 }, { 128, 128, 16 } } );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, f.map.pAllocator ) );
    REQUIRE( MapMeshEdit_ConvertBrushes( &f.map, { &mesh, 1 }, &roots ) == map_status_t::OK );
    description_t initial( f.map, mesh );
    struct cap_setup_t { const geo::mesh_source_description_t *source; u64 materials[2]{}; usize caps{}; } setup{ &initial.value };
    REQUIRE( MapDocument_MaterialRef( &f.map, SV( "materials/connect_bottom.cymat" ), &setup.materials[0] ) == map_status_t::OK );
    REQUIRE( MapDocument_MaterialRef( &f.map, SV( "materials/connect_top.cymat" ), &setup.materials[1] ) == map_status_t::OK );
    REQUIRE( MapMeshEdit_Apply( &f.map, mesh, []( void *context, geo::mesh_source_t *working, geo::mesh_edit_report_t * ) noexcept {
        auto &setup = *static_cast<cap_setup_t *>( context );
        const auto &source = *setup.source;
        for ( usize faceIndex = 0; faceIndex < source.faces.nCount; ++faceIndex ) {
            const auto &face = source.faces.pData[faceIndex];
            for ( usize cap = 0; cap < 2u; ++cap ) {
                bool onCap = true;
                for ( u32 k = 0; k < face.cCorners; ++k ) {
                    const auto &corner = source.corners.pData[face.iFirstCorner + k];
                    onCap &= std::abs( source.vertices.pData[corner.iVertex].position.z - cap * 16.0 ) < 1e-6;
                }
                if ( !onCap ) { continue; }
                ++setup.caps;
                auto surface = face.attributes; surface.material.value = setup.materials[cap]; surface.smoothingGroups = 4u << cap;
                auto status = geo::MeshSourceEdit_TrySetFaceAttributes( working, face.sourceId, surface );
                if ( status != geo::geometry_status_t::OK ) { return status; }
                for ( u32 k = 0; k < face.cCorners; ++k ) {
                    const auto &corner = source.corners.pData[face.iFirstCorner + k];
                    const auto &vertex = source.vertices.pData[corner.iVertex];
                    auto attributes = corner.attributes;
                    attributes.uv0 = { vertex.position.x / 37.0 + cap * 7.0 + 2.0, vertex.position.y / 53.0 - cap * 3.0 - 1.0 };
                    status = geo::MeshSourceEdit_TrySetCornerAttributes( working, face.sourceId, vertex.sourceId, attributes );
                    if ( status != geo::geometry_status_t::OK ) { return status; }
                }
            }
        }
        return geo::geometry_status_t::OK;
    }, &setup ) == map_status_t::OK );
    REQUIRE( setup.caps == 2u );
    description_t before( f.map, mesh );
    const auto selected = OppositeVertexColumns( before, 16.0 );
    const auto revision = f.map.geometry.revision;
    REQUIRE( MapTopologyEdit_ConnectVertexSet( &f.map, mesh, { selected.data(), selected.size() } ) == map_status_t::OK );
    description_t after( f.map, mesh );
    CHECK( after.Faces() == before.Faces() + 2u );
    CHECK( after.Vertices() == before.Vertices() );
    CHECK( BoundaryEdges( f.map, mesh ) == 0u );
    CHECK( f.map.geometry.revision == revision + 1u );
    for ( usize i = 0; i < before.Vertices(); ++i ) {
        const auto &v = before.value.vertices.pData[i];
        const auto p = after.Position( v.sourceId );
        CHECK( p.x == v.position.x ); CHECK( p.y == v.position.y ); CHECK( p.z == v.position.z );
    }
    for ( f64 z : { 0.0, 16.0 } ) {
        std::vector<geo::geometry_source_id_t> pair;
        for ( const auto vertex : selected ) { if ( std::abs( after.Position( vertex ).z - z ) < 1e-6 ) { pair.push_back( vertex ); } }
        REQUIRE( pair.size() == 2u );
        geo::geometry_mesh_edge_handle_t edge{};
        REQUIRE( geo::MeshSourceEdit_TryFindEdge( Source( f.map, mesh ), pair[0], pair[1], &edge ) );
    }
    const auto checkCapAttributes = [&]( const map_document_t &document, const description_t &description ) {
        usize children[2]{};
        for ( usize faceIndex = 0; faceIndex < description.Faces(); ++faceIndex ) {
            const auto &face = description.value.faces.pData[faceIndex];
            for ( usize cap = 0; cap < 2u; ++cap ) {
                const auto corners = description.Corners( faceIndex );
                if ( !std::all_of( corners.begin(), corners.end(), [&]( auto id ) { return std::abs( description.Position( id ).z - cap * 16.0 ) < 1e-6; } ) ) { continue; }
                ++children[cap];
                CHECK( StringView_Equals( MapMaterials_Path( &document.materials, face.attributes.material.value ),
                    SV( cap == 0 ? "materials/connect_bottom.cymat" : "materials/connect_top.cymat" ) ) );
                CHECK( face.attributes.smoothingGroups == ( 4u << cap ) );
                for ( u32 k = 0; k < face.cCorners; ++k ) {
                    const auto &corner = description.value.corners.pData[face.iFirstCorner + k];
                    const auto p = description.value.vertices.pData[corner.iVertex].position;
                    CHECK( corner.attributes.uv0.x == Catch::Approx( p.x / 37.0 + cap * 7.0 + 2.0 ) );
                    CHECK( corner.attributes.uv0.y == Catch::Approx( p.y / 53.0 - cap * 3.0 - 1.0 ) );
                }
            }
        }
        CHECK( children[0] == 2u ); CHECK( children[1] == 2u );
    };
    checkCapAttributes( f.map, after );
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded; saved.Load( loaded );
    description_t persisted( loaded, mesh );
    checkCapAttributes( loaded, persisted );
    REQUIRE( persisted.Faces() == after.Faces() );
    for ( usize i = 0; i < persisted.Faces(); ++i ) {
        auto &face = persisted.value.faces.pData[i]; const auto &expected = after.value.faces.pData[i];
        REQUIRE( face.sourceId.value == expected.sourceId.value );
        // Files store paths; refs index each document's encounter-ordered
        // intern table. Normalize only this test description after checking
        // semantic paths, then compare all geometry and surfacing exactly.
        CHECK( StringView_Equals( MapMaterials_Path( &loaded.materials, face.attributes.material.value ),
            MapMaterials_Path( &f.map.materials, expected.attributes.material.value ) ) );
        face.attributes.material = expected.attributes.material;
    }
    CHECK( geo::MeshSourceDescription_Equal( &after.value, &persisted.value ) );
    const auto afterRevision = f.map.geometry.revision, nextId = f.map.nextId;
    CHECK( MapTopologyEdit_ConnectVertexSet( &f.map, mesh, { selected.data(), selected.size() } ) != map_status_t::OK );
    CHECK( f.map.geometry.revision == afterRevision ); CHECK( f.map.nextId == nextId );
    description_t noOp( f.map, mesh ); CHECK( geo::MeshSourceDescription_Equal( &after.value, &noOp.value ) );
}

TEST_CASE( "Vertex Connect rejects ambiguous and invalid selections before changing a face", "[map][topology-edit][vertex-connect]" )
{
    fixture_t f; const u64 mesh = f.Quad(); description_t before( f.map, mesh );
    const auto corners = before.Corners( 0u );
    const auto nextId = f.map.nextId, revision = f.map.geometry.revision;
    SECTION( "Two opposite vertices split the quad" ) {
        const geo::geometry_source_id_t pair[]{ corners[0], corners[2] };
        REQUIRE( MapTopologyEdit_ConnectVertexSet( &f.map, mesh, { pair, 2u } ) == map_status_t::OK );
        CHECK( description_t( f.map, mesh ).Faces() == 2u );
        CHECK( description_t( f.map, mesh ).Vertices() == 4u );
        return;
    }
    SECTION( "Existing edge" ) {
        const geo::geometry_source_id_t pair[]{ corners[0], corners[1] };
        CHECK( MapTopologyEdit_ConnectVertexSet( &f.map, mesh, { pair, 2u } ) != map_status_t::OK );
    }
    SECTION( "Three corners on a face are ambiguous" ) {
        CHECK( MapTopologyEdit_ConnectVertexSet( &f.map, mesh, { corners.data(), 3u } ) != map_status_t::OK );
    }
    SECTION( "Duplicate selection" ) {
        const geo::geometry_source_id_t pair[]{ corners[0], corners[0] };
        CHECK( MapTopologyEdit_ConnectVertexSet( &f.map, mesh, { pair, 2u } ) == map_status_t::INVALID_ARGUMENT );
    }
    SECTION( "Unknown selected vertex cannot be silently ignored" ) {
        const geo::geometry_source_id_t ids[]{ corners[0], corners[2], { CY_U64_MAX } };
        CHECK( MapTopologyEdit_ConnectVertexSet( &f.map, mesh, { ids, 3u } ) != map_status_t::OK );
    }
    SECTION( "Read-only" ) {
        f.map.bReadOnly = CY_TRUE;
        const geo::geometry_source_id_t pair[]{ corners[0], corners[2] };
        CHECK( MapTopologyEdit_ConnectVertexSet( &f.map, mesh, { pair, 2u } ) == map_status_t::READ_ONLY );
    }
    CHECK( f.map.geometry.revision == revision ); CHECK( f.map.nextId == nextId );
    description_t after( f.map, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
}

TEST_CASE( "Vertex Connect allocation failures discard the complete multi-face edit", "[map][topology-edit][vertex-connect][oom]" )
{
    edge_pull_audit_t audit;
    {
        map_document_t original;
        REQUIRE( MapDocument_Create( &original, &audit.allocator, { SV( "Vertex Connect failures" ), SV( "reap" ), {} } ) == map_status_t::OK );
        map_primitive_desc_t primitive{}; primitive.kind = map_primitive_kind_t::BOX;
        primitive.bounds = { { -64, -48, 16 }, { 96, 80, 112 } };
        u64 mesh = 0u;
        REQUIRE( MapEdit_CreatePrimitive( &original, primitive, SV( "materials/source.cymat" ), {}, &mesh ) == map_status_t::OK );
        vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, &audit.allocator ) );
        REQUIRE( MapMeshEdit_ConvertBrushes( &original, { &mesh, 1u }, &roots ) == map_status_t::OK );
        description_t before( original, mesh ); const auto selected = OppositeVertexColumns( before, 112.0 );
        const auto nextId = original.nextId, revision = original.geometry.revision;
        const usize liveBytes = audit.bytes;
        usize allocationCount = 0u;
        {
            map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
            audit.calls = 0u;
            REQUIRE( MapTopologyEdit_ConnectVertexSet( copy.get(), mesh, { selected.data(), selected.size() } ) == map_status_t::OK );
            allocationCount = audit.calls;
        }
        REQUIRE( allocationCount > 16u ); CHECK( audit.bytes == liveBytes );
        for ( usize failure = 0u; failure < allocationCount; ++failure ) {
            CAPTURE( failure, allocationCount );
            {
                audit.failAt = CY_USIZE_MAX;
                map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                audit.calls = 0u; audit.failAt = failure;
                CHECK( MapTopologyEdit_ConnectVertexSet( copy.get(), mesh, { selected.data(), selected.size() } ) != map_status_t::OK );
                audit.failAt = CY_USIZE_MAX;
            }
            CHECK( audit.bytes == liveBytes );
            CHECK( original.nextId == nextId ); CHECK( original.geometry.revision == revision );
            description_t after( original, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
        }
    }
    CHECK( audit.bytes == 0u );
}

namespace {
f64 VertexAxis( math::vec3d_t p, int axis ) { return axis == 0 ? p.x : axis == 1 ? p.y : p.z; }

// Two adjacent visible box columns, expressed as stationary target / source
// pairs at each depth. All four lie on one side face; welding removes that
// collapsed face and leaves a closed triangular prism.
std::vector<geo::geometry_source_id_t> AdjacentColumnPairs( const description_t &mesh, int depthAxis )
{
    const int across = ( depthAxis + 1 ) % 3, fixed = ( depthAxis + 2 ) % 3;
    f64 low = CY_F64_MAX, high = -CY_F64_MAX, fixedValue = CY_F64_MAX;
    for ( usize i = 0; i < mesh.Vertices(); ++i ) {
        const auto p = mesh.value.vertices.pData[i].position;
        low = std::min( low, VertexAxis( p, depthAxis ) ); high = std::max( high, VertexAxis( p, depthAxis ) );
        fixedValue = std::min( fixedValue, VertexAxis( p, fixed ) );
    }
    std::vector<geo::geometry_source_id_t> pairs;
    for ( f64 depth : { low, high } ) {
        std::vector<geo::geometry_source_id_t> layer;
        for ( usize i = 0; i < mesh.Vertices(); ++i ) {
            const auto &v = mesh.value.vertices.pData[i];
            if ( VertexAxis( v.position, fixed ) == fixedValue && VertexAxis( v.position, depthAxis ) == depth ) { layer.push_back( v.sourceId ); }
        }
        REQUIRE( layer.size() == 2u );
        if ( VertexAxis( mesh.Position( layer[0] ), across ) < VertexAxis( mesh.Position( layer[1] ), across ) ) { std::swap( layer[0], layer[1] ); }
        pairs.insert( pairs.end(), layer.begin(), layer.end() );
    }
    return pairs;
}
}

TEST_CASE( "Orthographic vertex Merge preserves each depth layer on all three view axes", "[map][topology-edit][vertex-weld]" )
{
    for ( int axis : { 0, 1, 2 } ) {
        CAPTURE( axis );
        fixture_t f; const u64 mesh = f.BoxMesh(); description_t before( f.map, mesh );
        auto selected = AdjacentColumnPairs( before, axis );
        const auto revision = f.map.geometry.revision;
        // Input order does not choose the depth grouping or survivor.
        std::reverse( selected.begin(), selected.end() );
        REQUIRE( MapTopologyEdit_MergeVertices( &f.map, mesh, { selected.data(), selected.size() }, axis ) == map_status_t::OK );
        description_t after( f.map, mesh );
        CHECK( after.Vertices() == 6u ); CHECK( after.Faces() == 5u ); CHECK( BoundaryEdges( f.map, mesh ) == 0u );
        CHECK( f.map.geometry.revision == revision + 1u );
        for ( usize i = 0; i < selected.size(); i += 2 ) {
            const auto a = before.Position( selected[i] ), b = before.Position( selected[i + 1] );
            const auto kept = selected[i].value < selected[i + 1].value ? selected[i] : selected[i + 1];
            const auto removed = selected[i].value < selected[i + 1].value ? selected[i + 1] : selected[i];
            REQUIRE( after.HasVertex( kept ) ); CHECK_FALSE( after.HasVertex( removed ) );
            const auto p = after.Position( kept );
            CHECK( p.x == Catch::Approx( ( a.x + b.x ) * 0.5 ) );
            CHECK( p.y == Catch::Approx( ( a.y + b.y ) * 0.5 ) );
            CHECK( p.z == Catch::Approx( ( a.z + b.z ) * 0.5 ) );
            CHECK( VertexAxis( p, axis ) == VertexAxis( a, axis ) );
        }
        for ( usize i = 0; i < before.Vertices(); ++i ) {
            const auto &v = before.value.vertices.pData[i];
            if ( std::any_of( selected.begin(), selected.end(), [&]( auto id ) { return id.value == v.sourceId.value; } ) ) { continue; }
            const auto p = after.Position( v.sourceId ); CHECK( p.x == v.position.x ); CHECK( p.y == v.position.y ); CHECK( p.z == v.position.z );
        }
        saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
        map_document_t loaded; saved.Load( loaded ); description_t persisted( loaded, mesh );
        CHECK( geo::MeshSourceDescription_Equal( &after.value, &persisted.value ) );
    }
}

TEST_CASE( "Orthographic vertex Merge leaves singleton depths unchanged and rejects empty groups", "[map][topology-edit][vertex-weld]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh(); description_t before( f.map, mesh );
    const auto pairs = AdjacentColumnPairs( before, 2 );
    SECTION( "One merge group and one singleton" ) {
        const geo::geometry_source_id_t selected[]{ pairs[0], pairs[1], pairs[2] };
        REQUIRE( MapTopologyEdit_MergeVertices( &f.map, mesh, { selected, 3u }, 2 ) == map_status_t::OK );
        description_t after( f.map, mesh ); CHECK( after.Vertices() == 7u );
        const auto old = before.Position( pairs[2] ), kept = after.Position( pairs[2] );
        CHECK( kept.x == old.x ); CHECK( kept.y == old.y ); CHECK( kept.z == old.z );
    }
    SECTION( "Only singleton depths have nothing to merge" ) {
        const geo::geometry_source_id_t selected[]{ pairs[0], pairs[2] };
        const auto revision = f.map.geometry.revision, nextId = f.map.nextId;
        CHECK( MapTopologyEdit_MergeVertices( &f.map, mesh, { selected, 2u }, 2 ) != map_status_t::OK );
        CHECK( MapTopologyEdit_MergeVertices( &f.map, mesh, { selected, 2u }, 3 ) == map_status_t::INVALID_ARGUMENT );
        CHECK( MapTopologyEdit_MergeVertices( &f.map, mesh, { selected, 2u }, -2 ) == map_status_t::INVALID_ARGUMENT );
        CHECK( f.map.geometry.revision == revision ); CHECK( f.map.nextId == nextId );
        description_t after( f.map, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
    }
}

TEST_CASE( "Vertex-pair welding retains every stationary target and its persistent identity", "[map][topology-edit][vertex-weld]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh(); description_t before( f.map, mesh );
    const auto pairs = AdjacentColumnPairs( before, 2 );
    const auto revision = f.map.geometry.revision;
    REQUIRE( MapTopologyEdit_WeldVertexPairs( &f.map, mesh, { pairs.data(), pairs.size() } ) == map_status_t::OK );
    description_t after( f.map, mesh );
    CHECK( after.Vertices() == 6u ); CHECK( after.Faces() == 5u ); CHECK( BoundaryEdges( f.map, mesh ) == 0u );
    CHECK( f.map.geometry.revision == revision + 1u );
    for ( usize i = 0; i < pairs.size(); i += 2 ) {
        REQUIRE( after.HasVertex( pairs[i] ) ); CHECK_FALSE( after.HasVertex( pairs[i + 1] ) );
    }
    for ( usize i = 0; i < after.Vertices(); ++i ) {
        const auto &v = after.value.vertices.pData[i]; const auto old = before.Position( v.sourceId );
        CHECK( v.position.x == old.x ); CHECK( v.position.y == old.y ); CHECK( v.position.z == old.z );
    }
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded; saved.Load( loaded ); description_t persisted( loaded, mesh );
    CHECK( geo::MeshSourceDescription_Equal( &after.value, &persisted.value ) );
}

TEST_CASE( "Vertex-pair welding rejects overlapping invalid and stale pairs without publication", "[map][topology-edit][vertex-weld]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh(); description_t before( f.map, mesh );
    const auto pairs = AdjacentColumnPairs( before, 2 );
    const auto revision = f.map.geometry.revision, nextId = f.map.nextId;
    SECTION( "Odd number of IDs" ) { CHECK( MapTopologyEdit_WeldVertexPairs( &f.map, mesh, { pairs.data(), 3u } ) == map_status_t::INVALID_ARGUMENT ); }
    SECTION( "Target and source are the same" ) {
        const geo::geometry_source_id_t bad[]{ pairs[0], pairs[0] };
        CHECK( MapTopologyEdit_WeldVertexPairs( &f.map, mesh, { bad, 2u } ) == map_status_t::INVALID_ARGUMENT );
    }
    SECTION( "Two pairs share a target" ) {
        const geo::geometry_source_id_t bad[]{ pairs[0], pairs[1], pairs[0], pairs[3] };
        CHECK( MapTopologyEdit_WeldVertexPairs( &f.map, mesh, { bad, 4u } ) == map_status_t::INVALID_ARGUMENT );
    }
    SECTION( "A source is also another pair's target" ) {
        const geo::geometry_source_id_t bad[]{ pairs[0], pairs[1], pairs[1], pairs[3] };
        CHECK( MapTopologyEdit_WeldVertexPairs( &f.map, mesh, { bad, 4u } ) == map_status_t::INVALID_ARGUMENT );
    }
    SECTION( "Unknown ID" ) {
        const geo::geometry_source_id_t bad[]{ pairs[0], { CY_U64_MAX } };
        CHECK( MapTopologyEdit_WeldVertexPairs( &f.map, mesh, { bad, 2u } ) != map_status_t::OK );
    }
    SECTION( "Read-only map" ) {
        f.map.bReadOnly = CY_TRUE;
        CHECK( MapTopologyEdit_WeldVertexPairs( &f.map, mesh, { pairs.data(), pairs.size() } ) == map_status_t::READ_ONLY );
    }
    CHECK( f.map.geometry.revision == revision ); CHECK( f.map.nextId == nextId );
    description_t after( f.map, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
}

TEST_CASE( "Vertex merging and pair welding discard private edits on every allocation failure", "[map][topology-edit][vertex-weld][oom]" )
{
    for ( const bool weld : { false, true } ) {
        CAPTURE( weld );
        edge_pull_audit_t audit;
        {
            map_document_t original;
            REQUIRE( MapDocument_Create( &original, &audit.allocator, { SV( "Vertex merge failures" ), SV( "reap" ), {} } ) == map_status_t::OK );
            map_primitive_desc_t primitive{}; primitive.kind = map_primitive_kind_t::BOX;
            primitive.bounds = { { -64, -48, 16 }, { 96, 80, 112 } };
            u64 mesh = 0u;
            REQUIRE( MapEdit_CreatePrimitive( &original, primitive, SV( "materials/source.cymat" ), {}, &mesh ) == map_status_t::OK );
            vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, &audit.allocator ) );
            REQUIRE( MapMeshEdit_ConvertBrushes( &original, { &mesh, 1u }, &roots ) == map_status_t::OK );
            description_t before( original, mesh ); const auto pairs = AdjacentColumnPairs( before, 2 );
            const auto edit = [&]( map_document_t *map ) {
                return weld ? MapTopologyEdit_WeldVertexPairs( map, mesh, { pairs.data(), pairs.size() } ) :
                    MapTopologyEdit_MergeVertices( map, mesh, { pairs.data(), pairs.size() }, 2 );
            };
            const auto nextId = original.nextId, revision = original.geometry.revision;
            const usize liveBytes = audit.bytes;
            usize allocationCount = 0u;
            {
                map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                audit.calls = 0u;
                REQUIRE( edit( copy.get() ) == map_status_t::OK );
                allocationCount = audit.calls;
            }
            REQUIRE( allocationCount > 16u ); CHECK( audit.bytes == liveBytes );
            for ( usize failure = 0u; failure < allocationCount; ++failure ) {
                CAPTURE( failure, allocationCount );
                {
                    audit.failAt = CY_USIZE_MAX;
                    map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                    audit.calls = 0u; audit.failAt = failure;
                    CHECK( edit( copy.get() ) != map_status_t::OK );
                    audit.failAt = CY_USIZE_MAX;
                }
                CHECK( audit.bytes == liveBytes );
                CHECK( original.nextId == nextId ); CHECK( original.geometry.revision == revision );
                description_t after( original, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
            }
        }
        CHECK( audit.bytes == 0u );
    }
}

TEST_CASE( "Edge Connect rejects incomplete or ambiguous batches before splitting anything", "[map][topology-edit][edge-connect-preflight]" )
{
    fixture_t f;
    const u64 first = f.Quad();
    const u64 second = f.Primitive( map_primitive_kind_t::QUAD, { { 256, 0, 0 }, { 384, 64, 0 } } );
    const u64 roots[]{ first, second };
    u64 mesh = 0u;
    REQUIRE( MapTopologyEdit_JoinMeshes( &f.map, { roots, 2u }, &mesh ) == map_status_t::OK );
    description_t before( f.map, mesh );
    REQUIRE( before.Faces() == 2u );
    const auto a = before.Corners( 0u ), b = before.Corners( 1u );
    std::vector<geo::mesh_edge_ref_t> edges{ Edge( a[0], a[1] ), Edge( a[2], a[3] ) };
    SECTION( "An unrelated edge on another shell must not be silently split" ) { edges.push_back( Edge( b[0], b[1] ) ); }
    SECTION( "Three selected edges on one face have no unique pairing" ) { edges.push_back( Edge( a[1], a[2] ) ); }
    SECTION( "Four selected edges on one face have no unique pairing" ) {
        edges.push_back( Edge( a[1], a[2] ) ); edges.push_back( Edge( a[3], a[0] ) );
    }
    SECTION( "A repeated edge is not a new connection input" ) { edges.push_back( edges[0] ); }
    SECTION( "Reversing endpoints does not make a duplicate edge distinct" ) { edges.push_back( { edges[0].b, edges[0].a } ); }
    SECTION( "A missing endpoint invalidates the batch" ) { edges.push_back( Edge( b[0], { CY_U64_MAX } ) ); }
    SECTION( "Existing diagonal endpoints are not a mesh edge" ) { edges.push_back( Edge( b[0], b[2] ) ); }
    SECTION( "Edges on two separate faces do not form a pair" ) { edges[1] = Edge( b[0], b[1] ); }

    saved_t savedBefore; REQUIRE( savedBefore.Save( &f.map ) == map_status_t::OK );
    const auto revision = f.map.geometry.revision, nextId = f.map.nextId;
    const auto nextSourceId = f.map.geometry.sourceIds.allocator.next.value;
    CHECK( MapTopologyEdit_ConnectEdges( &f.map, mesh, { edges.data(), edges.size() } ) == map_status_t::INVALID_ARGUMENT );
    CHECK( f.map.geometry.revision == revision ); CHECK( f.map.nextId == nextId );
    CHECK( f.map.geometry.sourceIds.allocator.next.value == nextSourceId );
    REQUIRE( f.map.geometry.meshes.nCount == 1u );
    description_t after( f.map, mesh );
    CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
    saved_t savedAfter; REQUIRE( savedAfter.Save( &f.map ) == map_status_t::OK );
    CHECK( savedAfter.root == savedBefore.root );
}

TEST_CASE( "Edge Connect pairs every participating shell by identity, including coincident shells", "[map][topology-edit][edge-connect-preflight]" )
{
    for ( const bool coincident : { false, true } ) {
        CAPTURE( coincident );
        fixture_t f;
        const u64 first = f.Quad();
        const f64 offset = coincident ? 0.0 : 256.0;
        const u64 second = f.Primitive( map_primitive_kind_t::QUAD, { { offset, 0, 0 }, { offset + 128, 64, 0 } } );
        const u64 roots[]{ first, second };
        u64 mesh = 0u;
        REQUIRE( MapTopologyEdit_JoinMeshes( &f.map, { roots, 2u }, &mesh ) == map_status_t::OK );
        description_t before( f.map, mesh );
        REQUIRE( before.Faces() == 2u );
        const auto a = before.Corners( 0u ), b = before.Corners( 1u );
        // Reverse and interleave inputs: IDs, rather than position or input
        // order, must distinguish the two disconnected surfaces.
        const geo::mesh_edge_ref_t edges[]{ { b[1], b[0] }, { a[3], a[2] }, { b[3], b[2] }, { a[1], a[0] } };
        REQUIRE( MapTopologyEdit_ConnectEdges( &f.map, mesh, { edges, 4u } ) == map_status_t::OK );
        description_t after( f.map, mesh );
        CHECK( after.Vertices() == 12u ); REQUIRE( after.Faces() == 4u );
        CHECK( BoundaryEdges( f.map, mesh ) == 12u );
        for ( usize i = 0u; i < before.Vertices(); ++i ) {
            const auto &old = before.value.vertices.pData[i];
            REQUIRE( after.HasVertex( old.sourceId ) );
            CHECK( math::Vec3d_EqualsExact( after.Position( old.sourceId ), old.position ) );
        }
        std::set<u64> firstMidpoints, secondMidpoints;
        usize firstFaces = 0u, secondFaces = 0u;
        for ( usize i = 0u; i < after.Faces(); ++i ) {
            const auto corners = after.Corners( i ); REQUIRE( corners.size() == 4u );
            const auto contains = [&]( const auto &ids ) {
                return std::any_of( corners.begin(), corners.end(), [&]( auto vertex ) {
                    return std::any_of( ids.begin(), ids.end(), [&]( auto old ) { return old.value == vertex.value; } );
                } );
            };
            const bool onFirst = contains( a ), onSecond = contains( b );
            REQUIRE( onFirst != onSecond );
            if ( onFirst ) { ++firstFaces; } else { ++secondFaces; }
            for ( auto id : corners ) {
                if ( !before.HasVertex( id ) ) { ( onFirst ? firstMidpoints : secondMidpoints ).insert( id.value ); }
            }
        }
        CHECK( firstFaces == 2u ); CHECK( secondFaces == 2u );
        REQUIRE( firstMidpoints.size() == 2u ); REQUIRE( secondMidpoints.size() == 2u );
        for ( const u64 id : firstMidpoints ) { CHECK_FALSE( secondMidpoints.contains( id ) ); }
        saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
        map_document_t loaded; saved.Load( loaded ); description_t persisted( loaded, mesh );
        CHECK( geo::MeshSourceDescription_Equal( &after.value, &persisted.value ) );
    }
}

TEST_CASE( "Edge Connect preflight allocation failures leave the source and scratch ownership intact", "[map][topology-edit][edge-connect-preflight][oom]" )
{
    edge_pull_audit_t audit;
    {
        map_document_t map;
        REQUIRE( MapDocument_Create( &map, &audit.allocator, { SV( "Connect preflight failures" ), SV( "reap" ), {} } ) == map_status_t::OK );
        map_primitive_desc_t primitive{}; primitive.kind = map_primitive_kind_t::QUAD;
        primitive.bounds = { { 0, 0, 0 }, { 128, 64, 0 } }; primitive.axis = 2;
        u64 mesh = 0u;
        REQUIRE( MapEdit_CreatePrimitive( &map, primitive, SV( "materials/source.cymat" ), {}, &mesh ) == map_status_t::OK );
        description_t before( map, mesh ); const auto corners = before.Corners( 0u );
        const geo::mesh_edge_ref_t ambiguous[]{ Edge( corners[0], corners[1] ), Edge( corners[1], corners[2] ), Edge( corners[2], corners[3] ) };
        saved_t savedBefore; REQUIRE( savedBefore.Save( &map ) == map_status_t::OK );
        const auto revision = map.geometry.revision, nextId = map.nextId;
        const auto nextSourceId = map.geometry.sourceIds.allocator.next.value;
        const usize liveBytes = audit.bytes;
        const geo::mesh_edge_ref_t valid[]{ ambiguous[0], ambiguous[2] };
        map.bReadOnly = CY_TRUE; audit.calls = 0u; audit.failAt = 0u;
        const auto readOnlyStatus = MapTopologyEdit_ConnectEdges( &map, mesh, { valid, 2u } );
        map.bReadOnly = CY_FALSE; audit.failAt = CY_USIZE_MAX;
        CHECK( readOnlyStatus == map_status_t::READ_ONLY ); CHECK( audit.calls == 0u );
        audit.calls = 0u;
        REQUIRE( MapTopologyEdit_ConnectEdges( &map, mesh, { ambiguous, 3u } ) == map_status_t::INVALID_ARGUMENT );
        const usize allocationCount = audit.calls;
        // Exercise only the small preflight, not every allocation in each
        // topology transaction. The rejected batch never reaches mutation.
        REQUIRE( allocationCount > 0u ); REQUIRE( allocationCount <= 32u );
        CHECK( audit.bytes == liveBytes );
        for ( usize failure = 0u; failure < allocationCount; ++failure ) {
            CAPTURE( failure, allocationCount );
            audit.calls = 0u; audit.failAt = failure;
            const auto status = MapTopologyEdit_ConnectEdges( &map, mesh, { ambiguous, 3u } );
            audit.failAt = CY_USIZE_MAX;
            CHECK( status == map_status_t::OUT_OF_MEMORY );
            CHECK( audit.bytes == liveBytes );
            CHECK( map.geometry.revision == revision ); CHECK( map.nextId == nextId );
            CHECK( map.geometry.sourceIds.allocator.next.value == nextSourceId );
            description_t after( map, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
            saved_t savedAfter; REQUIRE( savedAfter.Save( &map ) == map_status_t::OK );
            CHECK( savedAfter.root == savedBefore.root );
        }
    }
    CHECK( audit.bytes == 0u );
}

TEST_CASE( "Clip target snapshot allocation failure preserves all roots before mutation", "[map][topology-edit][clip-shell-caps][oom]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh(); description_t before( f.map, mesh );
    const auto revision = f.map.geometry.revision, next = f.map.nextId, nextSource = f.map.geometry.sourceIds.allocator.next.value;
    vector_t<u64> retained{}; REQUIRE( Vector_Init( &retained, Allocator_GetSystem() ) ); REQUIRE( Vector_PushBack( &retained, u64{ 999 } ) );
    edge_pull_audit_t audit;
    for ( const auto keep : { map_slice_keep_t::FRONT, map_slice_keep_t::BACK, map_slice_keep_t::BOTH } ) {
        audit.calls = 0u; audit.failAt = 0u;
        const auto *allocator = f.map.pAllocator; f.map.pAllocator = &audit.allocator;
        const auto status = MapTopologyEdit_ClipMeshes( &f.map, { &mesh, 1u }, { { 1, 0, 0 }, -32 }, keep, &retained, true );
        f.map.pAllocator = allocator; audit.failAt = CY_USIZE_MAX;
        CHECK( status == map_status_t::OUT_OF_MEMORY ); CHECK( audit.calls == 1u ); CHECK( audit.bytes == 0u );
        CHECK( retained.nCount == 1u ); CHECK( retained.pData[0] == 999 );
        CHECK( f.map.geometry.revision == revision ); CHECK( f.map.nextId == next ); CHECK( f.map.geometry.sourceIds.allocator.next.value == nextSource );
        description_t after( f.map, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
    }
}

namespace {
u64 RegionGrid( map_document_t &map, u32 columns = 2u, u32 rows = 1u )
{
    map_primitive_desc_t primitive{}; primitive.kind = map_primitive_kind_t::QUAD;
    primitive.bounds = { { 0, 0, 0 }, { 128, 64, 0 } }; primitive.axis = 2;
    u64 mesh = 0u;
    REQUIRE( MapEdit_CreatePrimitive( &map, primitive, SV( "materials/source.cymat" ), {}, &mesh ) == map_status_t::OK );
    description_t initial( map, mesh );
    REQUIRE( MapMeshEdit_QuadSliceFace( &map, mesh, initial.value.faces.pData[0].sourceId.value, columns, rows ) == map_status_t::OK );
    return mesh;
}
std::vector<u64> RegionFaces( const description_t &description )
{
    std::vector<u64> faces;
    for ( usize f = 0u; f < description.Faces(); ++f ) { faces.push_back( description.value.faces.pData[f].sourceId.value ); }
    return faces;
}
const geo::mesh_source_face_t *RegionFindFace( const description_t &description, u64 id )
{
    for ( usize f = 0u; f < description.Faces(); ++f ) {
        if ( description.value.faces.pData[f].sourceId.value == id ) { return &description.value.faces.pData[f]; }
    }
    return nullptr;
}
const key_value_t *RegionFaceRecord( const key_value_t *record, u64 id )
{
    const auto *faces = KeyValue_Find( record, SV( "faces" ) );
    for ( usize f = 0u; f < KeyValue_ChildCount( faces ); ++f ) {
        const auto *face = KeyValue_ChildAt( faces, f ); u64 found = 0u;
        if ( KeyValue_GetU64( KeyValue_Find( face, SV( "id" ) ), &found ) && found == id ) { return face; }
    }
    return nullptr;
}
void RegionSetU64( key_value_document_t *document, key_value_t *record, const char *name, u64 value )
{
    auto *node = KeyValue_ObjectInsert( document, record, SV( name ), key_value_type_t::U64 ); REQUIRE( node );
    REQUIRE( KeyValue_SetU64( document, node, value ) );
}
}

TEST_CASE( "Region extrusion raises adjacent caps without an internal wall and preserves exact surfacing lineage", "[map][topology-edit][face-region][metadata]" )
{
    fixture_t f; const u64 mesh = RegionGrid( f.map ); u64 owner = 0u;
    REQUIRE( MapDocument_AddLayer( &f.map, SV( "region" ), SV( "Region" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( &f.map, mesh, SV( "region" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( &f.map, SV( "region" ), SV( "func_structure" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( &f.map, mesh, owner ) == map_status_t::OK );
    description_t authored( f.map, mesh ); REQUIRE( authored.Faces() == 2u ); REQUIRE( authored.Vertices() == 6u );
    const auto ids = RegionFaces( authored );
    // Non-affine UVs and distinct corner colours expose projection or nearest-
    // corner transfer being used where the caps have exact correspondences.
    for ( usize i = 0u; i < authored.Faces(); ++i ) {
        auto &face = authored.value.faces.pData[i];
        REQUIRE( MapDocument_MaterialRef( &f.map, i == 0u ? SV( "materials/region_a.cymat" ) : SV( "materials/region_b.cymat" ),
            &face.attributes.material.value ) == map_status_t::OK );
        face.attributes.smoothingGroups = u32{ 1 } << ( i + 3u );
        for ( u32 k = 0u; k < face.cCorners; ++k ) {
            auto &corner = authored.value.corners.pData[face.iFirstCorner + k];
            corner.attributes.uv0 = { static_cast<f64>( 10u * i + k * k ), 0.125 + 0.75 * k };
            corner.attributes.uv1 = { -0.25 * k, 0.5 + 0.625 * ( i + k * k ) };
            corner.attributes.colorRgba = 0x123456FFu + static_cast<u32>( i * 4u + k ) * 0x10000000u;
        }
    }
    // Mark every cap edge, including the common edge, and retain a crease.
    std::set<std::pair<u32, u32>> edges;
    for ( usize i = 0u; i < authored.Faces(); ++i ) {
        const auto &face = authored.value.faces.pData[i];
        for ( u32 k = 0u; k < face.cCorners; ++k ) {
            const u32 a = authored.value.corners.pData[face.iFirstCorner + k].iVertex;
            const u32 b = authored.value.corners.pData[face.iFirstCorner + ( k + 1u ) % face.cCorners].iVertex;
            edges.emplace( std::min( a, b ), std::max( a, b ) );
        }
    }
    Vector_Clear( &authored.value.edges );
    for ( const auto &[a, b] : edges ) {
        REQUIRE( Vector_PushBack( &authored.value.edges, geo::mesh_source_edge_t{ a, b,
            { static_cast<u8>( geo::MESH_EDGE_FLAG_HARD | geo::MESH_EDGE_FLAG_SEAM ) }, 0.625 } ) );
    }
    geo::mesh_source_t replacement{};
    REQUIRE( geo::MeshSource_TryBuild( &authored.value, f.map.pAllocator, &replacement ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryReplaceMesh( &f.map.geometry, &replacement ) == geo::geometry_status_t::OK );
    geo::MeshSource_Shutdown( &replacement );
    saved_t materialized; REQUIRE( materialized.Save( &f.map ) == map_status_t::OK );
    map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( &f.map, mesh, &store ); REQUIRE( record ); REQUIRE( store );
    RegionSetU64( store->store.pDocument, record, "future_region_root", 891u );
    auto *faceRecords = KeyValue_ObjectInsert( store->store.pDocument, record, SV( "faces" ), key_value_type_t::ARRAY ); REQUIRE( faceRecords );
    for ( usize i = 0u; i < ids.size(); ++i ) {
        auto *face = KeyValue_ArrayAppend( store->store.pDocument, faceRecords, key_value_type_t::OBJECT ); REQUIRE( face );
        RegionSetU64( store->store.pDocument, face, "id", ids[i] );
        RegionSetU64( store->store.pDocument, face, "future_region_face", i + 1u );
    }
    const auto revision = f.map.geometry.revision;
    REQUIRE( MapTopologyEdit_ExtrudeFaceRegionBy( &f.map, mesh, { ids.data(), ids.size() }, 16.0 ) == map_status_t::OK );
    description_t after( f.map, mesh );
    CHECK( after.Faces() == 8u ); CHECK( after.Vertices() == 12u ); CHECK( BoundaryEdges( f.map, mesh ) == 6u );
    CHECK( GenerationPool_Count( &Source( f.map, mesh )->mesh.shells ) == 1u );
    CHECK( f.map.geometry.revision == revision + 1u );
    for ( usize v = 0u; v < authored.Vertices(); ++v ) {
        const auto &old = authored.value.vertices.pData[v]; const auto p = after.Position( old.sourceId );
        CHECK( p.x == old.position.x ); CHECK( p.y == old.position.y ); CHECK( p.z == old.position.z );
    }
    usize sideCount = 0u;
    for ( usize i = 0u; i < after.Faces(); ++i ) {
        const auto &face = after.value.faces.pData[i];
        const auto *parent = RegionFindFace( authored, face.sourceId.value );
        if ( parent != nullptr ) {
            REQUIRE( face.cCorners == parent->cCorners );
            for ( u32 k = 0u; k < parent->cCorners; ++k ) {
                const auto &old = authored.value.corners.pData[parent->iFirstCorner + k];
                const auto oldPosition = authored.value.vertices.pData[old.iVertex].position;
                const geo::mesh_source_corner_t *match = nullptr;
                for ( u32 j = 0u; j < face.cCorners; ++j ) {
                    const auto &candidate = after.value.corners.pData[face.iFirstCorner + j];
                    const auto p = after.value.vertices.pData[candidate.iVertex].position;
                    if ( p.x == oldPosition.x && p.y == oldPosition.y && p.z == oldPosition.z + 16.0 ) { match = &candidate; }
                }
                REQUIRE( match ); CHECK( match->attributes.uv0.x == old.attributes.uv0.x ); CHECK( match->attributes.uv0.y == old.attributes.uv0.y );
                CHECK( match->attributes.uv1.x == old.attributes.uv1.x ); CHECK( match->attributes.uv1.y == old.attributes.uv1.y );
                CHECK( match->attributes.colorRgba == old.attributes.colorRgba );
            }
            const auto cap = after.Corners( i );
            for ( usize k = 0u; k < cap.size(); ++k ) {
                geo::geometry_mesh_edge_handle_t handle{}; const auto *source = Source( f.map, mesh );
                REQUIRE( geo::MeshSourceEdit_TryFindEdge( source, cap[k], cap[( k + 1u ) % cap.size()], &handle ) );
                CHECK( geo::MeshAttributeStore_GetEdge( &source->attributes, handle ).flags == ( geo::MESH_EDGE_FLAG_HARD | geo::MESH_EDGE_FLAG_SEAM ) );
                REQUIRE( GenerationPool_Get( &source->mesh.edges, handle ) );
                CHECK( GenerationPool_Get( &source->mesh.edges, handle )->creaseWeight == 0.625 );
            }
        } else {
            ++sideCount;
            std::vector<u64> base;
            for ( const auto id : after.Corners( i ) ) { if ( after.Position( id ).z == 0.0 ) { base.push_back( id.value ); } }
            REQUIRE( base.size() == 2u );
            for ( usize p = 0u; p < authored.Faces(); ++p ) {
                const auto corners = authored.Corners( p );
                for ( usize k = 0u; k < corners.size(); ++k ) {
                    const auto a = corners[k].value, b = corners[( k + 1u ) % corners.size()].value;
                    if ( ( a == base[0] && b == base[1] ) || ( a == base[1] && b == base[0] ) ) {
                        REQUIRE( parent == nullptr ); parent = &authored.value.faces.pData[p];
                    }
                }
            }
            // An interior wall would match both parents, not one perimeter edge.
            REQUIRE( parent );
            f64 uv0Area = 0.0, uv1Area = 0.0;
            for ( u32 k = 0u; k < face.cCorners; ++k ) {
                const auto &corner = after.value.corners.pData[face.iFirstCorner + k];
                const auto &next = after.value.corners.pData[face.iFirstCorner + ( k + 1u ) % face.cCorners];
                CHECK( std::isfinite( corner.attributes.uv0.x ) ); CHECK( std::isfinite( corner.attributes.uv0.y ) );
                CHECK( std::isfinite( corner.attributes.uv1.x ) ); CHECK( std::isfinite( corner.attributes.uv1.y ) );
                uv0Area += corner.attributes.uv0.x * next.attributes.uv0.y - corner.attributes.uv0.y * next.attributes.uv0.x;
                uv1Area += corner.attributes.uv1.x * next.attributes.uv1.y - corner.attributes.uv1.y * next.attributes.uv1.x;
                const auto p = after.value.vertices.pData[corner.iVertex].position;
                const geo::mesh_corner_attributes_t *original = nullptr;
                for ( u32 j = 0u; j < parent->cCorners; ++j ) {
                    const auto &candidate = authored.value.corners.pData[parent->iFirstCorner + j];
                    const auto old = authored.value.vertices.pData[candidate.iVertex].position;
                    if ( p.x == old.x && p.y == old.y ) { original = &candidate.attributes; }
                }
                REQUIRE( original ); CHECK( corner.attributes.colorRgba == original->colorRgba );
                if ( p.z == 0.0 ) {
                    CHECK( corner.attributes.uv0.x == original->uv0.x ); CHECK( corner.attributes.uv0.y == original->uv0.y );
                    CHECK( corner.attributes.uv1.x == original->uv1.x ); CHECK( corner.attributes.uv1.y == original->uv1.y );
                }
            }
            CHECK( std::abs( uv0Area ) > 1e-10 ); CHECK( std::abs( uv1Area ) > 1e-10 );
        }
        CHECK( face.attributes.material.value == parent->attributes.material.value );
        CHECK( face.attributes.smoothingGroups == parent->attributes.smoothingGroups );
        record = MapDocument_FindObject( &f.map, mesh, nullptr ); REQUIRE( record );
        const auto *extra = RegionFaceRecord( record, face.sourceId.value ); REQUIRE( extra ); u64 tag = 0u;
        REQUIRE( KeyValue_GetU64( KeyValue_Find( extra, SV( "future_region_face" ) ), &tag ) );
        CHECK( tag == ( parent->sourceId.value == ids[0] ? 1u : 2u ) );
    }
    CHECK( sideCount == 6u );
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK ); map_document_t loaded; saved.Load( loaded );
    description_t persisted( loaded, mesh ); REQUIRE( persisted.Faces() == after.Faces() );
    for ( usize i = 0u; i < after.Faces(); ++i ) {
        auto &face = persisted.value.faces.pData[i]; const auto &expected = after.value.faces.pData[i];
        CHECK( StringView_Equals( MapMaterials_Path( &loaded.materials, face.attributes.material.value ),
            MapMaterials_Path( &f.map.materials, expected.attributes.material.value ) ) );
        face.attributes.material = expected.attributes.material;
    }
    CHECK( geo::MeshSourceDescription_Equal( &after.value, &persisted.value ) );
    for ( auto *document : { &f.map, &loaded } ) {
        record = MapDocument_FindObject( document, mesh, nullptr ); REQUIRE( record ); u64 tag = 0u;
        REQUIRE( KeyValue_GetU64( KeyValue_Find( record, SV( "future_region_root" ) ), &tag ) ); CHECK( tag == 891u );
        bool placement = false;
        for ( usize i = 0u; i < document->geometryRecords.nCount; ++i ) {
            const auto &item = document->geometryRecords.pData[i]; if ( item.id != mesh ) { continue; }
            placement = true; CHECK( item.owner == owner );
            REQUIRE( item.iLayer < document->layers.nCount );
            CHECK( StringView_Equals( StringView_FromCString( document->layers.pData[item.iLayer].id ), SV( "region" ) ) );
        }
        CHECK( placement ); CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &document->geometry.sourceIds ) );
    }
}

TEST_CASE( "Region walls use a finite 128-unit UV chart when source edge UVs are collapsed", "[map][topology-edit][face-region][metadata]" )
{
    fixture_t f; const u64 mesh = RegionGrid( f.map ); description_t authored( f.map, mesh ); const auto selected = RegionFaces( authored );
    for ( usize i = 0u; i < authored.value.corners.nCount; ++i ) {
        authored.value.corners.pData[i].attributes.uv0 = {}; authored.value.corners.pData[i].attributes.uv1 = {};
    }
    geo::mesh_source_t replacement{};
    REQUIRE( geo::MeshSource_TryBuild( &authored.value, f.map.pAllocator, &replacement ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryReplaceMesh( &f.map.geometry, &replacement ) == geo::geometry_status_t::OK );
    geo::MeshSource_Shutdown( &replacement );
    REQUIRE( MapTopologyEdit_ExtrudeFaceRegionBy( &f.map, mesh, { selected.data(), selected.size() }, 32.0 ) == map_status_t::OK );
    description_t after( f.map, mesh ); usize walls = 0u;
    for ( usize i = 0u; i < after.Faces(); ++i ) {
        const auto &face = after.value.faces.pData[i]; const bool cap = RegionFindFace( authored, face.sourceId.value ) != nullptr;
        f64 minU = CY_F64_MAX, maxU = -CY_F64_MAX, minV = CY_F64_MAX, maxV = -CY_F64_MAX;
        std::vector<math::vec3d_t> base;
        for ( u32 k = 0u; k < face.cCorners; ++k ) {
            const auto &corner = after.value.corners.pData[face.iFirstCorner + k];
            const auto p = after.value.vertices.pData[corner.iVertex].position;
            CHECK( std::isfinite( corner.attributes.uv0.x ) ); CHECK( std::isfinite( corner.attributes.uv0.y ) );
            CHECK( corner.attributes.uv1.x == corner.attributes.uv0.x ); CHECK( corner.attributes.uv1.y == corner.attributes.uv0.y );
            if ( cap ) { CHECK( corner.attributes.uv0.x == 0.0 ); CHECK( corner.attributes.uv0.y == 0.0 ); }
            else {
                CHECK( corner.attributes.uv0.y == p.z / 128.0 );
                minU = std::min( minU, corner.attributes.uv0.x ); maxU = std::max( maxU, corner.attributes.uv0.x );
                minV = std::min( minV, corner.attributes.uv0.y ); maxV = std::max( maxV, corner.attributes.uv0.y );
                if ( p.z == 0.0 ) { base.push_back( p ); }
            }
        }
        if ( cap ) { continue; }
        ++walls; REQUIRE( base.size() == 2u );
        const f64 length = std::hypot( base[1].x - base[0].x, base[1].y - base[0].y );
        CHECK( minU == 0.0 ); CHECK( maxU == length / 128.0 ); CHECK( minV == 0.0 ); CHECK( maxV == 0.25 );
        CHECK( ( maxU - minU ) * ( maxV - minV ) > 0.0 );
    }
    CHECK( walls == 6u );
}

TEST_CASE( "Region extrusion handles an interior grid vertex and stitches a closed solid", "[map][topology-edit][face-region]" )
{
    fixture_t f; u64 mesh = 0u; std::vector<u64> selected; usize expectedVertices = 0u, expectedFaces = 0u, expectedBoundary = 0u;
    SECTION( "Open two by two grid replaces its unused old center" ) {
        mesh = RegionGrid( f.map, 2u, 2u ); description_t before( f.map, mesh ); selected = RegionFaces( before );
        expectedVertices = 17u; expectedFaces = 12u; expectedBoundary = 8u;
    }
    SECTION( "The split top of a box stays joined to all unselected sides" ) {
        mesh = f.BoxMesh(); description_t initial( f.map, mesh );
        REQUIRE( MapMeshEdit_QuadSliceFace( &f.map, mesh, initial.value.faces.pData[initial.FaceAtZ( 112 )].sourceId.value, 2u, 1u ) == map_status_t::OK );
        description_t before( f.map, mesh );
        for ( usize i = 0u; i < before.Faces(); ++i ) {
            bool top = true; for ( const auto id : before.Corners( i ) ) { top &= before.Position( id ).z == 112.0; }
            if ( top ) { selected.push_back( before.value.faces.pData[i].sourceId.value ); }
        }
        REQUIRE( selected.size() == 2u ); expectedVertices = before.Vertices() + 6u; expectedFaces = before.Faces() + 6u;
    }
    description_t before( f.map, mesh );
    REQUIRE( MapTopologyEdit_ExtrudeFaceRegionBy( &f.map, mesh, { selected.data(), selected.size() }, 24.0 ) == map_status_t::OK );
    description_t after( f.map, mesh ); CHECK( after.Vertices() == expectedVertices ); CHECK( after.Faces() == expectedFaces );
    CHECK( BoundaryEdges( f.map, mesh ) == expectedBoundary ); CHECK( GenerationPool_Count( &Source( f.map, mesh )->mesh.shells ) == 1u );
    for ( const auto id : selected ) { CHECK( RegionFindFace( after, id ) != nullptr ); }
    for ( usize i = 0u; i < before.Vertices(); ++i ) {
        const auto &v = before.value.vertices.pData[i];
        if ( expectedBoundary != 0u && v.position.x == 64.0 && v.position.y == 32.0 ) { CHECK_FALSE( after.HasVertex( v.sourceId ) ); continue; }
        const auto p = after.Position( v.sourceId ); CHECK( p.x == v.position.x ); CHECK( p.y == v.position.y ); CHECK( p.z == v.position.z );
    }
    CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &f.map.geometry.sourceIds ) );
}

TEST_CASE( "Region extrusion refuses unsupported selections and amounts without changing source IDs or text", "[map][topology-edit][face-region]" )
{
    fixture_t f; u64 mesh = RegionGrid( f.map ); description_t initial( f.map, mesh );
    auto selected = RegionFaces( initial ); f64 distance = 16.0; auto expected = map_status_t::INVALID_ARGUMENT;
    SECTION( "Repeated face" ) { selected[1] = selected[0]; }
    SECTION( "Missing face" ) { selected[1] = CY_U64_MAX; }
    SECTION( "One face uses the existing single face API" ) { selected.resize( 1u ); }
    SECTION( "Zero distance" ) { distance = 0.0; }
    SECTION( "Inward distance" ) { distance = -16.0; }
    SECTION( "Nonfinite distance" ) { distance = std::numeric_limits<f64>::infinity(); }
    SECTION( "Coordinate limit" ) { distance = f.map.geometryPolicy.numerical.fCoordinateMagnitudeLimit + 1.0; }
    SECTION( "Read only map" ) { expected = map_status_t::READ_ONLY; }
    SECTION( "Disconnected coplanar shells" ) {
        const u64 second = f.Primitive( map_primitive_kind_t::QUAD, { { 256, 0, 0 }, { 384, 64, 0 } } );
        const u64 roots[]{ mesh, second }; REQUIRE( MapTopologyEdit_JoinMeshes( &f.map, { roots, 2u }, &mesh ) == map_status_t::OK );
        description_t disconnected( f.map, mesh ); selected = RegionFaces( disconnected );
    }
    SECTION( "Adjacent faces with different normals" ) {
        mesh = f.BoxMesh(); description_t box( f.map, mesh ); selected = RegionFaces( box );
    }
    SECTION( "Exhausted map identity space" ) { expected = map_status_t::LIMIT_EXCEEDED; }
    description_t before( f.map, mesh ); saved_t savedBefore; REQUIRE( savedBefore.Save( &f.map ) == map_status_t::OK );
    const auto savedNextId = f.map.nextId;
    f.map.bReadOnly = expected == map_status_t::READ_ONLY;
    // Save synchronizes allocation cursors; inject exhaustion afterwards.
    if ( expected == map_status_t::LIMIT_EXCEEDED ) { f.map.nextId = 0u; }
    const auto revision = f.map.geometry.revision, nextId = f.map.nextId, sourceNext = f.map.geometry.sourceIds.allocator.next.value;
    CHECK( MapTopologyEdit_ExtrudeFaceRegionBy( &f.map, mesh, { selected.data(), selected.size() }, distance ) == expected );
    CHECK( f.map.geometry.revision == revision ); CHECK( f.map.nextId == nextId ); CHECK( f.map.geometry.sourceIds.allocator.next.value == sourceNext );
    description_t after( f.map, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
    f.map.bReadOnly = CY_FALSE; // Saving is itself forbidden while read-only.
    f.map.nextId = savedNextId;
    saved_t savedAfter; REQUIRE( savedAfter.Save( &f.map ) == map_status_t::OK ); CHECK( savedAfter.root == savedBefore.root );
}

TEST_CASE( "Region extrusion releases scratch and preserves the source across sampled allocation failures", "[map][topology-edit][face-region][oom]" )
{
    edge_pull_audit_t audit;
    {
        map_document_t original;
        REQUIRE( MapDocument_Create( &original, &audit.allocator, { SV( "Region failures" ), SV( "reap" ), {} } ) == map_status_t::OK );
        const u64 mesh = RegionGrid( original ); description_t before( original, mesh ); const auto selected = RegionFaces( before );
        saved_t savedBefore; REQUIRE( savedBefore.Save( &original ) == map_status_t::OK );
        const auto nextId = original.nextId, sourceNext = original.geometry.sourceIds.allocator.next.value, revision = original.geometry.revision;
        const usize liveBytes = audit.bytes; usize allocations = 0u;
        {
            map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
            audit.calls = 0u;
            REQUIRE( MapTopologyEdit_ExtrudeFaceRegionBy( copy.get(), mesh, { selected.data(), selected.size() }, 16.0 ) == map_status_t::OK );
            allocations = audit.calls;
        }
        REQUIRE( allocations >= 16u ); CHECK( audit.bytes == liveBytes );
        // Cover early clone/preparation, middle topology/transfer and late
        // publication allocations without repeating the existing exhaustive
        // MeshBoundary and MeshEdit failure sweeps for every scratch buffer.
        const std::set<usize> failures{ 0u, 1u, allocations / 4u, allocations / 2u, allocations * 3u / 4u, allocations - 2u, allocations - 1u };
        for ( const auto failure : failures ) {
            CAPTURE( failure, allocations );
            {
                map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> copy( raw );
                audit.calls = 0u; audit.failAt = failure;
                CHECK( MapTopologyEdit_ExtrudeFaceRegionBy( copy.get(), mesh, { selected.data(), selected.size() }, 16.0 ) == map_status_t::OUT_OF_MEMORY );
                audit.failAt = CY_USIZE_MAX;
            }
            CHECK( audit.bytes == liveBytes ); CHECK( original.nextId == nextId ); CHECK( original.geometry.revision == revision );
            CHECK( original.geometry.sourceIds.allocator.next.value == sourceNext );
            description_t after( original, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
            saved_t savedAfter; REQUIRE( savedAfter.Save( &original ) == map_status_t::OK ); CHECK( savedAfter.root == savedBefore.root );
        }
    }
    CHECK( audit.bytes == 0u );
}

namespace {
using inset_edge_t = std::pair<u64, u64>;
struct inset_edge_use_t { usize count{}; u64 parent{}; };
std::map<inset_edge_t, inset_edge_use_t> InsetRegionEdges( const description_t &before, const std::vector<u64> &selected )
{
    std::map<inset_edge_t, inset_edge_use_t> edges;
    for ( usize i = 0; i < before.Faces(); ++i ) {
        const auto &face = before.value.faces.pData[i];
        if ( std::find( selected.begin(), selected.end(), face.sourceId.value ) == selected.end() ) { continue; }
        const auto ids = before.Corners( i );
        for ( usize j = 0; j < ids.size(); ++j ) {
            const auto a = ids[j].value, b = ids[( j + 1 ) % ids.size()].value;
            auto &use = edges[{ std::min( a, b ), std::max( a, b ) }]; ++use.count; use.parent = face.sourceId.value;
        }
    }
    return edges;
}
math::vec3d_t InsetDifference( math::vec3d_t a, math::vec3d_t b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
math::vec3d_t InsetCross( math::vec3d_t a, math::vec3d_t b ) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
f64 InsetLength( math::vec3d_t p ) { return std::sqrt( p.x * p.x + p.y * p.y + p.z * p.z ); }
f64 InsetDot( math::vec3d_t a, math::vec3d_t b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
void InsetCheckPoint( math::vec3d_t actual, math::vec3d_t expected )
{
    CHECK( actual.x == Catch::Approx( expected.x ).margin( 1e-9 ) );
    CHECK( actual.y == Catch::Approx( expected.y ).margin( 1e-9 ) );
    CHECK( actual.z == Catch::Approx( expected.z ).margin( 1e-9 ) );
}
void InsetReplaceSource( map_document_t &map, const description_t &description )
{
    geo::mesh_source_t replacement{};
    REQUIRE( geo::MeshSource_TryBuild( &description.value, map.pAllocator, &replacement ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryReplaceMesh( &map.geometry, &replacement ) == geo::geometry_status_t::OK );
    geo::MeshSource_Shutdown( &replacement );
}
// Derive old->inset correspondence from actual border adjacency. The oracle
// measures distance to original support lines, independently of offset math.
std::map<u64, u64> InsetCheckBorder( const description_t &before, const description_t &after,
    const std::vector<u64> &selected, f64 width )
{
    const auto edges = InsetRegionEdges( before, selected ); std::map<u64, u64> moved; std::set<inset_edge_t> seen;
    for ( usize i = 0; i < after.Faces(); ++i ) {
        const auto &face = after.value.faces.pData[i]; if ( RegionFindFace( before, face.sourceId.value ) ) { continue; }
        REQUIRE( face.cCorners == 4u ); const auto ids = after.Corners( i ); std::vector<u64> old, inner;
        for ( usize k = 0; k < ids.size(); ++k ) {
            if ( !before.HasVertex( ids[k] ) ) { inner.push_back( ids[k].value ); continue; }
            old.push_back( ids[k].value );
            const auto previous = ids[( k + ids.size() - 1u ) % ids.size()], next = ids[( k + 1u ) % ids.size()];
            REQUIRE( before.HasVertex( previous ) != before.HasVertex( next ) );
            const u64 inset = before.HasVertex( previous ) ? next.value : previous.value;
            const auto inserted = moved.emplace( ids[k].value, inset ); CHECK( inserted.first->second == inset );
        }
        REQUIRE( old.size() == 2u ); REQUIRE( inner.size() == 2u );
        const inset_edge_t edge{ std::min( old[0], old[1] ), std::max( old[0], old[1] ) };
        const auto use = edges.find( edge ); REQUIRE( use != edges.end() ); CHECK( use->second.count == 1u ); CHECK( seen.insert( edge ).second );
        const auto a = before.Position( { old[0] } ), b = before.Position( { old[1] } ), tangent = InsetDifference( b, a );
        const auto *parent = RegionFindFace( before, use->second.parent ); REQUIRE( parent ); math::vec3d_t center{};
        for ( u32 k = 0; k < parent->cCorners; ++k ) {
            const auto p = before.value.vertices.pData[before.value.corners.pData[parent->iFirstCorner + k].iVertex].position;
            center.x += p.x / parent->cCorners; center.y += p.y / parent->cCorners; center.z += p.z / parent->cCorners;
        }
        const auto inside = InsetCross( tangent, InsetDifference( center, a ) );
        for ( const u64 id : inner ) {
            const auto distance = InsetCross( tangent, InsetDifference( after.Position( { id } ), a ) );
            CHECK( InsetLength( distance ) / InsetLength( tangent ) == Catch::Approx( width ).margin( 1e-9 ) );
            CHECK( InsetDot( distance, inside ) > 0.0 );
        }
        CHECK( InsetLength( InsetCross( tangent, InsetDifference( after.Position( { inner[1] } ), after.Position( { inner[0] } ) ) ) ) < 1e-7 );
    }
    usize perimeter = 0;
    for ( const auto &[edge, use] : edges ) { if ( use.count == 1u ) { ++perimeter; CHECK( seen.count( edge ) == 1u ); } }
    CHECK( seen.size() == perimeter ); CHECK( moved.size() == perimeter );
    for ( usize v = 0; v < before.Vertices(); ++v ) { const auto &old = before.value.vertices.pData[v]; InsetCheckPoint( after.Position( old.sourceId ), old.position ); }
    for ( const u64 id : selected ) {
        const auto *old = RegionFindFace( before, id ), *cap = RegionFindFace( after, id ); REQUIRE( old ); REQUIRE( cap );
        CHECK( cap->cCorners == old->cCorners );
        std::set<u64> expected, actual;
        for ( u32 k = 0; k < old->cCorners; ++k ) { const auto source = before.value.vertices.pData[before.value.corners.pData[old->iFirstCorner + k].iVertex].sourceId.value;
            const auto found = moved.find( source ); expected.insert( found == moved.end() ? source : found->second ); }
        for ( u32 k = 0; k < cap->cCorners; ++k ) { actual.insert( after.value.vertices.pData[after.value.corners.pData[cap->iFirstCorner + k].iVertex].sourceId.value ); }
        CHECK( actual == expected );
    }
    return moved;
}
}

TEST_CASE( "Region measured inset offsets only the perimeter and retains cap partitions", "[map][topology-edit][region-inset-width]" )
{
    fixture_t f; const u64 mesh = RegionGrid( f.map ); description_t authored( f.map, mesh ); bool oblique = false;
    SECTION( "Two rectangular cells retain their collinear outer subdivisions" ) {}
    SECTION( "An oblique region tilted in space measures perpendicular width" ) {
        oblique = true;
        for ( usize i = 0; i < authored.Vertices(); ++i ) {
            auto &p = authored.value.vertices.pData[i].position; const f64 y = p.y;
            p = { p.x + 0.5 * y, y * std::sqrt( 0.75 ), y * 0.5 };
        }
        InsetReplaceSource( f.map, authored );
    }
    description_t before( f.map, mesh ); const auto selected = RegionFaces( before ); const auto revision = f.map.geometry.revision;
    REQUIRE( MapTopologyEdit_InsetFaceRegionByEdgeWidth( &f.map, mesh, { selected.data(), selected.size() }, 8.0 ) == map_status_t::OK );
    description_t after( f.map, mesh ); CHECK( after.Vertices() == 12u ); CHECK( after.Faces() == 8u ); CHECK( BoundaryEdges( f.map, mesh ) == 6u );
    CHECK( f.map.geometry.revision == revision + 1u );
    const auto moved = InsetCheckBorder( before, after, selected, 8.0 );
    for ( usize i = 0; i < before.Vertices(); ++i ) {
        const auto &v = before.value.vertices.pData[i]; if ( v.position.x != 64.0 || v.position.y != 0.0 ) { continue; }
        REQUIRE( moved.count( v.sourceId.value ) == 1u );
        InsetCheckPoint( after.Position( { moved.at( v.sourceId.value ) } ), oblique ? math::vec3d_t{ 68, 8 * std::sqrt( 0.75 ), 4 } : math::vec3d_t{ 64, 8, 0 } );
    }
    CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &f.map.geometry.sourceIds ) );
}

TEST_CASE( "Region measured inset keeps interior vertices and an unselected neighbour fixed", "[map][topology-edit][region-inset-width]" )
{
    fixture_t f; u64 mesh = 0u; bool neighbour = false;
    SECTION( "Four cells preserve their original central vertex" ) { mesh = RegionGrid( f.map, 2u, 2u ); }
    SECTION( "Two of three cells leave the third cell and shared outer IDs untouched" ) { neighbour = true; mesh = RegionGrid( f.map, 3u, 1u ); }
    description_t before( f.map, mesh ); std::vector<u64> selected; u64 untouched = 0u;
    for ( usize i = 0; i < before.Faces(); ++i ) {
        f64 minX = CY_F64_MAX; for ( const auto id : before.Corners( i ) ) { minX = std::min( minX, before.Position( id ).x ); }
        if ( neighbour && minX > 64.0 ) { untouched = before.value.faces.pData[i].sourceId.value; }
        else { selected.push_back( before.value.faces.pData[i].sourceId.value ); }
    }
    REQUIRE( selected.size() == ( neighbour ? 2u : 4u ) );
    REQUIRE( MapTopologyEdit_InsetFaceRegionByEdgeWidth( &f.map, mesh, { selected.data(), selected.size() }, 8.0 ) == map_status_t::OK );
    description_t after( f.map, mesh ); CHECK( after.Vertices() == ( neighbour ? 14u : 17u ) ); CHECK( after.Faces() == ( neighbour ? 9u : 12u ) );
    CHECK( BoundaryEdges( f.map, mesh ) == 8u ); const auto moved = InsetCheckBorder( before, after, selected, 8.0 );
    if ( neighbour ) {
        const auto *old = RegionFindFace( before, untouched ), *now = RegionFindFace( after, untouched ); REQUIRE( old ); REQUIRE( now );
        CHECK( now->cCorners == old->cCorners );
        for ( u32 k = 0; k < old->cCorners; ++k ) {
            const auto &a = before.value.corners.pData[old->iFirstCorner + k], &b = after.value.corners.pData[now->iFirstCorner + k];
            CHECK( before.value.vertices.pData[a.iVertex].sourceId.value == after.value.vertices.pData[b.iVertex].sourceId.value );
            CHECK( a.attributes.uv0.x == b.attributes.uv0.x ); CHECK( a.attributes.uv0.y == b.attributes.uv0.y );
            CHECK( a.attributes.uv1.x == b.attributes.uv1.x ); CHECK( a.attributes.uv1.y == b.attributes.uv1.y ); CHECK( a.attributes.colorRgba == b.attributes.colorRgba );
        }
    } else {
        usize centers = 0u;
        for ( usize i = 0; i < before.Vertices(); ++i ) { const auto &v = before.value.vertices.pData[i];
            if ( v.position.x == 64 && v.position.y == 32 ) { ++centers; CHECK( moved.count( v.sourceId.value ) == 0u ); InsetCheckPoint( after.Position( v.sourceId ), v.position ); } }
        CHECK( centers == 1u );
    }
}

TEST_CASE( "Region measured inset retains face-local surfacing and object lineage through save and open", "[map][topology-edit][region-inset-width][metadata]" )
{
    fixture_t f; const u64 mesh = RegionGrid( f.map ); u64 owner = 0u, group = 0u;
    REQUIRE( MapDocument_AddLayer( &f.map, SV( "inset" ), SV( "Inset detail" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryLayer( &f.map, mesh, SV( "inset" ) ) == map_status_t::OK );
    REQUIRE( MapDocument_AddEntity( &f.map, SV( "inset" ), SV( "func_structure" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( &f.map, mesh, owner ) == map_status_t::OK );
    REQUIRE( MapDocument_AddGroup( &f.map, SV( "Retained region" ), { &mesh, 1u }, &group ) == map_status_t::OK );
    description_t authored( f.map, mesh ); const auto selected = RegionFaces( authored );
    auto uv0 = []( usize face, math::vec3d_t p ) { return math::vec2d_t{ 10.0 * face + p.x / 32.0 + p.y / 64.0, 0.25 + p.y / 16.0 }; };
    auto uv1 = []( usize face, math::vec3d_t p ) { return math::vec2d_t{ -3.0 * face + p.x / 64.0, 0.5 + p.x / 128.0 - p.y / 32.0 }; };
    for ( usize i = 0; i < authored.Faces(); ++i ) {
        auto &face = authored.value.faces.pData[i];
        REQUIRE( MapDocument_MaterialRef( &f.map, i == 0u ? SV( "materials/inset_a.cymat" ) : SV( "materials/inset_b.cymat" ), &face.attributes.material.value ) == map_status_t::OK );
        face.attributes.smoothingGroups = 1u << ( i + 3u );
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            auto &corner = authored.value.corners.pData[face.iFirstCorner + k]; const auto p = authored.value.vertices.pData[corner.iVertex].position;
            corner.attributes.uv0 = uv0( i, p ); corner.attributes.uv1 = uv1( i, p ); corner.attributes.colorRgba = i == 0u ? 0x3165AFFFu : 0xDCA035FFu;
        }
    }
    std::set<std::pair<u32, u32>> edgeIndices;
    for ( usize i = 0; i < authored.Faces(); ++i ) { const auto &face = authored.value.faces.pData[i];
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            const u32 a = authored.value.corners.pData[face.iFirstCorner + k].iVertex, b = authored.value.corners.pData[face.iFirstCorner + ( k + 1u ) % face.cCorners].iVertex;
            edgeIndices.emplace( std::min( a, b ), std::max( a, b ) );
        }
    }
    Vector_Clear( &authored.value.edges );
    for ( const auto &[a, b] : edgeIndices ) { REQUIRE( Vector_PushBack( &authored.value.edges, geo::mesh_source_edge_t{ a, b,
        { static_cast<u8>( geo::MESH_EDGE_FLAG_HARD | geo::MESH_EDGE_FLAG_SEAM ) }, 0.625 } ) ); }
    InsetReplaceSource( f.map, authored );
    saved_t materialized; REQUIRE( materialized.Save( &f.map ) == map_status_t::OK );
    map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( &f.map, mesh, &store ); REQUIRE( record ); REQUIRE( store );
    RegionSetU64( store->store.pDocument, record, "future_inset_root", 941u );
    auto *faceRecords = KeyValue_ObjectInsert( store->store.pDocument, record, SV( "faces" ), key_value_type_t::ARRAY ); REQUIRE( faceRecords );
    for ( usize i = 0; i < selected.size(); ++i ) { auto *face = KeyValue_ArrayAppend( store->store.pDocument, faceRecords, key_value_type_t::OBJECT ); REQUIRE( face );
        RegionSetU64( store->store.pDocument, face, "id", selected[i] ); RegionSetU64( store->store.pDocument, face, "future_inset_face", i + 1u ); }
    const auto oldEdges = InsetRegionEdges( authored, selected );
    REQUIRE( MapTopologyEdit_InsetFaceRegionByEdgeWidth( &f.map, mesh, { selected.data(), selected.size() }, 8.0 ) == map_status_t::OK );
    description_t after( f.map, mesh ); const auto moved = InsetCheckBorder( authored, after, selected, 8.0 );
    for ( usize i = 0; i < after.Faces(); ++i ) {
        const auto &face = after.value.faces.pData[i]; u64 parentId = face.sourceId.value;
        if ( !RegionFindFace( authored, parentId ) ) {
            std::vector<u64> old; for ( const auto id : after.Corners( i ) ) { if ( authored.HasVertex( id ) ) { old.push_back( id.value ); } }
            REQUIRE( old.size() == 2u ); parentId = oldEdges.at( { std::min( old[0], old[1] ), std::max( old[0], old[1] ) } ).parent;
        }
        const auto found = std::find( selected.begin(), selected.end(), parentId ); REQUIRE( found != selected.end() ); const usize parentIndex = static_cast<usize>( found - selected.begin() );
        const auto *parent = RegionFindFace( authored, parentId ); REQUIRE( parent );
        CHECK( face.attributes.material.value == parent->attributes.material.value ); CHECK( face.attributes.smoothingGroups == parent->attributes.smoothingGroups );
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            const auto &corner = after.value.corners.pData[face.iFirstCorner + k]; const auto p = after.value.vertices.pData[corner.iVertex].position;
            const auto first = uv0( parentIndex, p ), second = uv1( parentIndex, p );
            CHECK( corner.attributes.uv0.x == Catch::Approx( first.x ).margin( 1e-9 ) ); CHECK( corner.attributes.uv0.y == Catch::Approx( first.y ).margin( 1e-9 ) );
            CHECK( corner.attributes.uv1.x == Catch::Approx( second.x ).margin( 1e-9 ) ); CHECK( corner.attributes.uv1.y == Catch::Approx( second.y ).margin( 1e-9 ) );
            CHECK( corner.attributes.colorRgba == ( parentIndex == 0u ? 0x3165AFFFu : 0xDCA035FFu ) );
        }
        record = MapDocument_FindObject( &f.map, mesh, nullptr ); const auto *extra = RegionFaceRecord( record, face.sourceId.value ); REQUIRE( extra ); u64 tag = 0u;
        REQUIRE( KeyValue_GetU64( KeyValue_Find( extra, SV( "future_inset_face" ) ), &tag ) ); CHECK( tag == parentIndex + 1u );
    }
    for ( const auto &[edge, use] : oldEdges ) {
        const auto mapped = [&]( u64 id ) { const auto found = moved.find( id ); return found == moved.end() ? id : found->second; };
        for ( const auto tested : { inset_edge_t{ mapped( edge.first ), mapped( edge.second ) }, edge } ) {
            // An old interior seam is replaced by the cap seam; only original
            // perimeter edges are retained in the outer border.
            if ( tested == edge && use.count != 1u ) { continue; }
            geo::geometry_mesh_edge_handle_t handle{}; const auto *source = Source( f.map, mesh );
            REQUIRE( geo::MeshSourceEdit_TryFindEdge( source, { tested.first }, { tested.second }, &handle ) );
            const auto attributes = geo::MeshAttributeStore_GetEdge( &source->attributes, handle );
            CHECK( ( attributes.flags & ( geo::MESH_EDGE_FLAG_HARD | geo::MESH_EDGE_FLAG_SEAM ) ) == ( geo::MESH_EDGE_FLAG_HARD | geo::MESH_EDGE_FLAG_SEAM ) );
            const auto crease = std::find_if( after.value.edges.pData, after.value.edges.pData + after.value.edges.nCount, [&]( const auto &candidate ) {
                const u64 a = after.value.vertices.pData[candidate.iVertexA].sourceId.value, b = after.value.vertices.pData[candidate.iVertexB].sourceId.value;
                return std::min( a, b ) == std::min( tested.first, tested.second ) && std::max( a, b ) == std::max( tested.first, tested.second ); } );
            REQUIRE( crease != after.value.edges.pData + after.value.edges.nCount ); CHECK( crease->creaseWeight == 0.625 );
        }
    }
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK ); map_document_t loaded; saved.Load( loaded ); description_t persisted( loaded, mesh );
    REQUIRE( persisted.Faces() == after.Faces() );
    for ( usize i = 0; i < persisted.Faces(); ++i ) { auto &face = persisted.value.faces.pData[i]; const auto *expected = RegionFindFace( after, face.sourceId.value ); REQUIRE( expected );
        CHECK( StringView_Equals( MapMaterials_Path( &loaded.materials, face.attributes.material.value ), MapMaterials_Path( &f.map.materials, expected->attributes.material.value ) ) ); face.attributes.material = expected->attributes.material; }
    CHECK( geo::MeshSourceDescription_Equal( &after.value, &persisted.value ) );
    for ( auto *document : { &f.map, &loaded } ) {
        CHECK( MapGroups_Parent( document, mesh ) == group ); record = MapDocument_FindObject( document, mesh, nullptr ); REQUIRE( record ); u64 tag = 0u;
        REQUIRE( KeyValue_GetU64( KeyValue_Find( record, SV( "future_inset_root" ) ), &tag ) ); CHECK( tag == 941u );
        usize roots = 0u;
        for ( usize i = 0; i < document->geometryRecords.nCount; ++i ) { const auto &placement = document->geometryRecords.pData[i]; if ( placement.id != mesh ) { continue; } ++roots;
            CHECK( placement.owner == owner ); REQUIRE( placement.iLayer < document->layers.nCount );
            CHECK( StringView_Equals( StringView_FromCString( document->layers.pData[placement.iLayer].id ), SV( "inset" ) ) ); }
        CHECK( roots == 1u ); CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &document->geometry.sourceIds ) );
        for ( usize i = 0; i < after.Faces(); ++i ) { const auto *extra = RegionFaceRecord( record, after.value.faces.pData[i].sourceId.value ); REQUIRE( extra );
            REQUIRE( KeyValue_GetU64( KeyValue_Find( extra, SV( "future_inset_face" ) ), &tag ) ); CHECK( ( tag == 1u || tag == 2u ) ); }
    }
}

TEST_CASE( "Region measured inset refuses unsupported unions and folded caps without publishing the candidate", "[map][topology-edit][region-inset-width]" )
{
    fixture_t f; u64 mesh = 0u; f64 width = 8.0; std::vector<u64> selected;
    SECTION( "Concave L shaped union" ) {
        mesh = RegionGrid( f.map, 2u, 2u ); description_t d( f.map, mesh );
        for ( usize i = 0; i < d.Faces(); ++i ) { f64 x = 0, y = 0; for ( const auto id : d.Corners( i ) ) { const auto p = d.Position( id ); x += p.x / 4; y += p.y / 4; }
            if ( x < 64 || y < 32 ) { selected.push_back( d.value.faces.pData[i].sourceId.value ); } }
        REQUIRE( selected.size() == 3u );
    }
    SECTION( "A selected ring enclosing an unselected center" ) {
        mesh = RegionGrid( f.map, 3u, 3u ); description_t d( f.map, mesh );
        for ( usize i = 0; i < d.Faces(); ++i ) { f64 x = 0, y = 0; for ( const auto id : d.Corners( i ) ) { const auto p = d.Position( id ); x += p.x / 4; y += p.y / 4; }
            if ( x < 43 || x > 85 || y < 22 || y > 42 ) { selected.push_back( d.value.faces.pData[i].sourceId.value ); } }
        REQUIRE( selected.size() == 8u );
    }
    SECTION( "Disconnected cells in one mesh" ) {
        mesh = RegionGrid( f.map, 3u, 1u ); description_t d( f.map, mesh );
        for ( usize i = 0; i < d.Faces(); ++i ) { f64 x = 0; for ( const auto id : d.Corners( i ) ) { x += d.Position( id ).x / 4; }
            if ( x < 43 || x > 85 ) { selected.push_back( d.value.faces.pData[i].sourceId.value ); } }
        REQUIRE( selected.size() == 2u );
    }
    SECTION( "Adjacent noncoplanar box faces" ) { mesh = f.BoxMesh(); description_t d( f.map, mesh ); selected = RegionFaces( d ); }
    SECTION( "Opposite winding disconnected coplanar shells" ) {
        mesh = f.Quad(); const u64 other = f.Primitive( map_primitive_kind_t::QUAD, { { 192, 0, 0 }, { 320, 64, 0 } } );
        REQUIRE( MapMeshEdit_FlipNormals( &f.map, { &other, 1u } ) == map_status_t::OK );
        const u64 roots[]{ mesh, other }; REQUIRE( MapTopologyEdit_JoinMeshes( &f.map, { roots, 2u }, &mesh ) == map_status_t::OK );
        description_t d( f.map, mesh ); selected = RegionFaces( d );
    }
    SECTION( "A retained interior vertex would fold the cap despite a valid outer offset" ) {
        mesh = RegionGrid( f.map, 2u, 2u ); description_t d( f.map, mesh ); usize changed = 0u;
        for ( usize i = 0; i < d.Vertices(); ++i ) { auto &p = d.value.vertices.pData[i].position; if ( p.x == 64 && p.y == 32 ) { p.y = 4; ++changed; } }
        REQUIRE( changed == 1u ); InsetReplaceSource( f.map, d ); selected = RegionFaces( d ); width = 16.0;
    }
    SECTION( "Width collapses the inner region" ) { mesh = RegionGrid( f.map ); description_t d( f.map, mesh ); selected = RegionFaces( d ); width = 32.0; }
    SECTION( "Width exceeds the region" ) { mesh = RegionGrid( f.map ); description_t d( f.map, mesh ); selected = RegionFaces( d ); width = 40.0; }
    SECTION( "Repeated face identity" ) { mesh = RegionGrid( f.map ); description_t d( f.map, mesh ); selected = RegionFaces( d ); selected[1] = selected[0]; }
    SECTION( "Missing face identity" ) { mesh = RegionGrid( f.map ); description_t d( f.map, mesh ); selected = RegionFaces( d ); selected[1] = CY_U64_MAX; }
    SECTION( "Nonfinite width" ) { mesh = RegionGrid( f.map ); description_t d( f.map, mesh ); selected = RegionFaces( d ); width = std::numeric_limits<f64>::quiet_NaN(); }
    SECTION( "Nonpositive width" ) { mesh = RegionGrid( f.map ); description_t d( f.map, mesh ); selected = RegionFaces( d ); width = 0.0; }
    REQUIRE( mesh != 0u ); description_t before( f.map, mesh ); saved_t savedBefore; REQUIRE( savedBefore.Save( &f.map ) == map_status_t::OK );
    const auto revision = f.map.geometry.revision, next = f.map.nextId, sourceNext = f.map.geometry.sourceIds.allocator.next.value;
    // Map topology adapters operate on a private working copy. Failed copies
    // are discarded; no geometry or ID cursor from them reaches the live map.
    {
        map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &f.map, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> candidate( raw );
        CHECK( MapTopologyEdit_InsetFaceRegionByEdgeWidth( candidate.get(), mesh, { selected.data(), selected.size() }, width ) == map_status_t::INVALID_ARGUMENT );
    }
    CHECK( f.map.geometry.revision == revision ); CHECK( f.map.nextId == next ); CHECK( f.map.geometry.sourceIds.allocator.next.value == sourceNext );
    description_t after( f.map, mesh ); CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
    saved_t savedAfter; REQUIRE( savedAfter.Save( &f.map ) == map_status_t::OK ); CHECK( savedAfter.root == savedBefore.root );
}

TEST_CASE( "Region measured inset releases failed private candidates and permits a fresh retry", "[map][topology-edit][region-inset-width][oom]" )
{
    edge_pull_audit_t audit;
    {
        map_document_t original; REQUIRE( MapDocument_Create( &original, &audit.allocator, { SV( "Inset failures" ), SV( "reap" ), {} } ) == map_status_t::OK );
        const u64 mesh = RegionGrid( original ); description_t before( original, mesh ); const auto selected = RegionFaces( before );
        saved_t savedBefore; REQUIRE( savedBefore.Save( &original ) == map_status_t::OK );
        const auto next = original.nextId, sourceNext = original.geometry.sourceIds.allocator.next.value, revision = original.geometry.revision;
        const usize liveBytes = audit.bytes; usize allocations = 0u;
        {
            map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> candidate( raw );
            audit.calls = 0u;
            REQUIRE( MapTopologyEdit_InsetFaceRegionByEdgeWidth( candidate.get(), mesh, { selected.data(), selected.size() }, 8.0 ) == map_status_t::OK );
            allocations = audit.calls;
        }
        REQUIRE( allocations >= 8u ); CHECK( audit.bytes == liveBytes );
        const std::set<usize> failures{ 0u, 1u, allocations / 4u, allocations / 2u, allocations * 3u / 4u, allocations - 2u, allocations - 1u };
        for ( const auto failure : failures ) {
            CAPTURE( failure, allocations );
            {
                map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> candidate( raw );
                audit.calls = 0u; audit.failAt = failure;
                const auto status = MapTopologyEdit_InsetFaceRegionByEdgeWidth( candidate.get(), mesh, { selected.data(), selected.size() }, 8.0 );
                audit.failAt = CY_USIZE_MAX; CHECK( status == map_status_t::OUT_OF_MEMORY );
            }
            CHECK( audit.bytes == liveBytes ); CHECK( original.nextId == next ); CHECK( original.geometry.revision == revision );
            CHECK( original.geometry.sourceIds.allocator.next.value == sourceNext ); description_t after( original, mesh );
            CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) ); saved_t savedAfter; REQUIRE( savedAfter.Save( &original ) == map_status_t::OK ); CHECK( savedAfter.root == savedBefore.root );
        }
        {
            map_document_t *raw = nullptr; REQUIRE( MapEdit_Clone( &original, &raw ) == map_status_t::OK ); std::unique_ptr<map_document_t> retry( raw );
            REQUIRE( MapTopologyEdit_InsetFaceRegionByEdgeWidth( retry.get(), mesh, { selected.data(), selected.size() }, 8.0 ) == map_status_t::OK );
            description_t after( *retry, mesh ); CHECK( after.Vertices() == 12u ); CHECK( after.Faces() == 8u ); InsetCheckBorder( before, after, selected, 8.0 );
        }
        CHECK( audit.bytes == liveBytes );
    }
    CHECK( audit.bytes == 0u );
}

namespace {
enum class join_stack_t { MISSING, EMPTY, ENABLED, DISABLED, UNKNOWN, UNSUPPORTED, NULL_FIELD, MALFORMED_FIELD, MALFORMED_ENTRY };
void JoinStack( map_document_t &map, u64 mesh, join_stack_t kind, f64 offset = 256.0 )
{
    if ( kind == join_stack_t::MISSING ) { return; }
    if ( kind == join_stack_t::ENABLED || kind == join_stack_t::DISABLED ) {
        geo::modifier_t stage{}; stage.kind = geo::modifier_kind_t::MIRROR; stage.axis = 0u; stage.offset = offset;
        stage.bEnabled = kind == join_stack_t::ENABLED; stage.bWeld = false; bool changed = false;
        REQUIRE( MapModifiers_Write( &map, mesh, { stage }, &changed ) == map_status_t::OK ); REQUIRE( changed ); return;
    }
    map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( &map, mesh, &store ); REQUIRE( record ); REQUIRE( store );
    auto *document = store->store.pDocument;
    if ( kind == join_stack_t::NULL_FIELD ) { REQUIRE( KeyValue_ObjectInsert( document, record, SV( "modifiers" ), key_value_type_t::NULL_VALUE ) ); return; }
    auto *stages = KeyValue_ObjectInsert( document, record, SV( "modifiers" ), kind == join_stack_t::MALFORMED_FIELD ? key_value_type_t::STRING : key_value_type_t::ARRAY ); REQUIRE( stages );
    if ( kind == join_stack_t::MALFORMED_FIELD ) { REQUIRE( KeyValue_SetString( document, stages, SV( "retained legacy payload" ) ) ); return; }
    if ( kind == join_stack_t::EMPTY ) { return; }
    auto *stage = KeyValue_ArrayAppend( document, stages, kind == join_stack_t::MALFORMED_ENTRY ? key_value_type_t::U64 : key_value_type_t::OBJECT ); REQUIRE( stage );
    if ( kind == join_stack_t::MALFORMED_ENTRY ) { REQUIRE( KeyValue_SetU64( document, stage, 941u ) ); return; }
    auto *type = KeyValue_ObjectInsert( document, stage, SV( "type" ), key_value_type_t::STRING ); REQUIRE( type );
    REQUIRE( KeyValue_SetString( document, type, SV( kind == join_stack_t::UNKNOWN ? "future_lattice" : "subdivide" ) ) );
    auto *payload = KeyValue_ObjectInsert( document, stage, SV( kind == join_stack_t::UNKNOWN ? "future_payload" : "scheme" ), key_value_type_t::STRING ); REQUIRE( payload );
    REQUIRE( KeyValue_SetString( document, payload, SV( kind == join_stack_t::UNKNOWN ? "retain this record exactly" : "loop" ) ) );
}
}

TEST_CASE( "Join refuses every retained modifier stack on survivor or donor before changing either source", "[map][topology-edit][join-modifier-preservation]" )
{
    fixture_t f; const u64 a = f.Quad(), b = f.Primitive( map_primitive_kind_t::QUAD, { { 192, 0, 0 }, { 320, 64, 0 } } );
    saved_t materialized; REQUIRE( materialized.Save( &f.map ) == map_status_t::OK );
    SECTION( "Enabled survivor" ) { JoinStack( f.map, a, join_stack_t::ENABLED ); }
    SECTION( "Enabled donor" ) { JoinStack( f.map, b, join_stack_t::ENABLED ); }
    SECTION( "Identical stacks must not be applied to the combined cage" ) { JoinStack( f.map, a, join_stack_t::ENABLED ); JoinStack( f.map, b, join_stack_t::ENABLED ); }
    SECTION( "Different stacks cannot be merged implicitly" ) { JoinStack( f.map, a, join_stack_t::ENABLED ); JoinStack( f.map, b, join_stack_t::ENABLED, 512 ); }
    SECTION( "A disabled donor stage still has authored meaning" ) { JoinStack( f.map, b, join_stack_t::DISABLED ); }
    SECTION( "Unknown stage" ) { JoinStack( f.map, b, join_stack_t::UNKNOWN ); }
    SECTION( "Known type with unsupported subdivision scheme" ) { JoinStack( f.map, a, join_stack_t::UNSUPPORTED ); }
    SECTION( "Explicit null is retained data rather than an absent field" ) { JoinStack( f.map, b, join_stack_t::NULL_FIELD ); }
    SECTION( "Malformed modifiers field" ) { JoinStack( f.map, b, join_stack_t::MALFORMED_FIELD ); }
    SECTION( "Malformed retained stage" ) { JoinStack( f.map, a, join_stack_t::MALFORMED_ENTRY ); }
    description_t oldA( f.map, a ), oldB( f.map, b ); saved_t before; REQUIRE( before.Save( &f.map ) == map_status_t::OK );
    const auto revision = f.map.geometry.revision, next = f.map.nextId, sourceNext = f.map.geometry.sourceIds.allocator.next.value;
    const u64 roots[]{ b, a }; u64 retained = CY_U64_MAX;
    CHECK( MapTopologyEdit_JoinMeshes( &f.map, { roots, 2u }, &retained ) == map_status_t::INVALID_ARGUMENT );
    CHECK( retained == CY_U64_MAX ); CHECK( f.map.geometry.meshes.nCount == 2u ); CHECK( f.map.geometry.revision == revision );
    CHECK( f.map.nextId == next ); CHECK( f.map.geometry.sourceIds.allocator.next.value == sourceNext );
    description_t afterA( f.map, a ), afterB( f.map, b );
    CHECK( geo::MeshSourceDescription_Equal( &oldA.value, &afterA.value ) ); CHECK( geo::MeshSourceDescription_Equal( &oldB.value, &afterB.value ) );
    saved_t after; REQUIRE( after.Save( &f.map ) == map_status_t::OK ); CHECK( after.root == before.root );
    CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &f.map.geometry.sourceIds ) );
}

TEST_CASE( "Join allows missing and empty modifier records and preserves all component identities", "[map][topology-edit][join-modifier-preservation]" )
{
    fixture_t f; const u64 a = f.Quad(), b = f.Primitive( map_primitive_kind_t::QUAD, { { 192, 0, 0 }, { 320, 64, 0 } } );
    saved_t materialized; REQUIRE( materialized.Save( &f.map ) == map_status_t::OK );
    SECTION( "Both modifier members missing" ) {}
    SECTION( "Empty survivor and donor arrays" ) { JoinStack( f.map, a, join_stack_t::EMPTY ); JoinStack( f.map, b, join_stack_t::EMPTY ); }
    SECTION( "Only donor has an empty array" ) { JoinStack( f.map, b, join_stack_t::EMPTY ); }
    description_t oldA( f.map, a ), oldB( f.map, b ); const u64 next = f.map.nextId;
    const u64 roots[]{ b, a }; u64 retained = CY_U64_MAX;
    REQUIRE( MapTopologyEdit_JoinMeshes( &f.map, { roots, 2u }, &retained ) == map_status_t::OK );
    CHECK( retained == a ); CHECK( f.map.geometry.meshes.nCount == 1u ); CHECK( f.map.nextId == next );
    description_t joined( f.map, retained ); CHECK( joined.Vertices() == 8u ); CHECK( joined.Faces() == 2u );
    for ( const auto *old : { &oldA, &oldB } ) {
        for ( usize i = 0; i < old->Vertices(); ++i ) { const auto &v = old->value.vertices.pData[i]; InsetCheckPoint( joined.Position( v.sourceId ), v.position ); }
        for ( usize i = 0; i < old->Faces(); ++i ) { CHECK( RegionFindFace( joined, old->value.faces.pData[i].sourceId.value ) != nullptr ); }
    }
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK ); map_document_t loaded; saved.Load( loaded );
    description_t reopened( loaded, retained ); CHECK( geo::MeshSourceDescription_Equal( &joined.value, &reopened.value ) );
    CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &loaded.geometry.sourceIds ) );
}

TEST_CASE( "Join input preparation allocation refusal preserves both roots and permits retry", "[map][topology-edit][join-modifier-preservation][oom]" )
{
    edge_pull_audit_t audit;
    {
        map_document_t map; REQUIRE( MapDocument_Create( &map, &audit.allocator, { SV( "Join failures" ), SV( "reap" ), {} } ) == map_status_t::OK );
        const u64 a = RegionGrid( map ), b = RegionGrid( map ); saved_t before; REQUIRE( before.Save( &map ) == map_status_t::OK );
        description_t oldA( map, a ), oldB( map, b ); const usize bytes = audit.bytes;
        const auto next = map.nextId, sourceNext = map.geometry.sourceIds.allocator.next.value, revision = map.geometry.revision;
        const u64 roots[]{ b, a }; u64 retained = CY_U64_MAX; audit.calls = 0; audit.failAt = 0;
        const auto status = MapTopologyEdit_JoinMeshes( &map, { roots, 2u }, &retained ); audit.failAt = CY_USIZE_MAX;
        CHECK( status == map_status_t::OUT_OF_MEMORY ); CHECK( retained == CY_U64_MAX ); CHECK( audit.bytes == bytes );
        CHECK( map.nextId == next ); CHECK( map.geometry.sourceIds.allocator.next.value == sourceNext ); CHECK( map.geometry.revision == revision );
        { description_t afterA( map, a ), afterB( map, b ); CHECK( geo::MeshSourceDescription_Equal( &oldA.value, &afterA.value ) ); CHECK( geo::MeshSourceDescription_Equal( &oldB.value, &afterB.value ) ); }
        saved_t after; REQUIRE( after.Save( &map ) == map_status_t::OK ); CHECK( after.root == before.root );
        REQUIRE( MapTopologyEdit_JoinMeshes( &map, { roots, 2u }, &retained ) == map_status_t::OK ); CHECK( retained == a );
        description_t joined( map, retained ); CHECK( joined.Vertices() == 12u ); CHECK( joined.Faces() == 4u );
    }
    CHECK( audit.bytes == 0u );
}
