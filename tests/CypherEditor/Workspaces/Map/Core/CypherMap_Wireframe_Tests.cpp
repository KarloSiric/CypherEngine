//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Wireframe_Tests.cpp
//  Purpose: Contract tests for the map wireframe and 2D picking.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Files.h"
#include "CypherMap_Wireframe.h"

#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_MeshSelection.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>
#include <set>
#include <string>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;

namespace
{

void CreateMap( map_document_t *pMap )
{
    map_create_desc_t desc{};
    desc.name = StringView_FromCString( "Wire Test" );
    desc.game = StringView_FromCString( "reap" );
    REQUIRE( MapDocument_Create( pMap, Allocator_GetSystem(), desc ) == map_status_t::OK );
}

u64 AddBox( map_document_t *pMap, math::vec3d_t center, f64 half )
{
    geo::brush_solid_t box{};
    REQUIRE( geo::BrushGenerator_TryMakeBox( &box, pMap->pAllocator, pMap->geometryPolicy, &pMap->geometry.sourceIds.allocator, center,
                                             math::vec3d_t{ half, half, half } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryAddBrush( &pMap->geometry, &box ) == geo::geometry_status_t::OK );
    const u64 id = box.sourceId.value;
    geo::BrushSolid_Shutdown( &box );
    return id;
}

u64 AddEntity( map_document_t *pMap, const char *pClass, math::vec3d_t origin )
{
    u64 id = 0u;
    REQUIRE( MapDocument_AddEntity( pMap, StringView_FromCString( "default" ), StringView_FromCString( pClass ), origin, &id ) ==
             map_status_t::OK );
    return id;
}

void LoadEntityMap( map_document_t &map, const std::string &entities )
{
    constexpr const char *root = R"(@cykv 1
@schema "cypher.map" 10
{ map_id = "4c1f0a52-7a8e-4f3b-9d61-0b2d3e4f5a6b" name = "Connections" game = "reap"
  next_id = 100u layers = [ { id = "default" } ] })";
    const std::string chunk = std::string( R"(@cykv 1
@schema "cypher.map_chunk" 10
{ map_id = "4c1f0a52-7a8e-4f3b-9d61-0b2d3e4f5a6b" layer = "default" cell = "global"
  entities = [ )" ) + entities + "] }";
    const map_chunk_input_t input{ StringView_FromCString( "default/global.cymapchunk" ), { chunk.data(), chunk.size() } };
    REQUIRE( MapDocument_Load( &map, Allocator_GetSystem(), StringView_FromCString( root ), { &input, 1u } ) == map_status_t::OK );
}

struct allocation_audit_t {
    usize calls{ 0u };
    usize bytes{ 0u };
    usize failAt{ CY_USIZE_MAX };
    allocator_t allocator{};
    allocation_audit_t()
    {
        allocator.pUserData = this;
        allocator.pfnAllocate = []( void *pContext, usize size, usize alignment ) noexcept -> void * {
            auto &audit = *static_cast<allocation_audit_t *>( pContext );
            if ( audit.calls++ == audit.failAt ) { return nullptr; }
            void *pMemory = Allocator_Allocate( Allocator_GetSystem(), size, alignment );
            if ( pMemory ) { audit.bytes += size; } return pMemory;
        };
        allocator.pfnFree = []( void *pContext, void *pMemory, usize size, usize alignment ) noexcept {
            if ( pMemory ) { static_cast<allocation_audit_t *>( pContext )->bytes -= size; }
            Allocator_Free( Allocator_GetSystem(), pMemory, size, alignment );
        };
    }
};

void CheckEmpty( const map_wireframe_t &wire )
{
    CHECK( wire.points.nCount == 0u );
    CHECK( wire.pointSourceIds.nCount == 0u );
    CHECK( wire.lines.nCount == 0u );
    CHECK( wire.objects.nCount == 0u );
    CHECK( wire.entities.nCount == 0u );
    CHECK( wire.connections.nCount == 0u );
    CHECK( wire.faces.nCount == 0u );
    CHECK( wire.faceIndices.nCount == 0u );
    CHECK( wire.nBrokenBrushes == 0u );
    CHECK_FALSE( wire.bounds.bHas );
}

// Fail every allocation observed in an otherwise successful build. Fresh
// wires make the same growth path repeat; successful retries also verify that
// no temporary descriptor or half-filled index range survives the failure.
void SweepBuildFailures( const map_document_t &map, u64 id )
{
    allocation_audit_t audit;
    usize allocations = 0u, expectedPoints = 0u, expectedLines = 0u, expectedFaces = 0u;
    {
        map_wireframe_t wire{}; REQUIRE( MapWireframe_Init( &wire, &audit.allocator ) );
        const auto callsBefore = audit.calls;
        REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
        allocations = audit.calls - callsBefore;
        REQUIRE( MapWireframe_FindObject( wire, id ) != nullptr );
        expectedPoints = wire.points.nCount; expectedLines = wire.lines.nCount; expectedFaces = wire.faces.nCount;
        REQUIRE( wire.pointSourceIds.nCount == expectedPoints );
        REQUIRE( expectedPoints != 0u ); REQUIRE( expectedFaces != 0u );
    }
    REQUIRE( allocations > 0u ); REQUIRE( audit.bytes == 0u );
    for ( usize failure = 0u; failure < allocations; ++failure ) {
        CAPTURE( failure, allocations );
        {
            map_wireframe_t wire{}; REQUIRE( MapWireframe_Init( &wire, &audit.allocator ) );
            audit.failAt = audit.calls + failure;
            const auto status = MapWireframe_Build( &wire, map );
            audit.failAt = CY_USIZE_MAX;
            REQUIRE( status == map_status_t::OUT_OF_MEMORY ); CheckEmpty( wire );
            REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
            CHECK( wire.points.nCount == expectedPoints ); CHECK( wire.lines.nCount == expectedLines ); CHECK( wire.faces.nCount == expectedFaces );
            CHECK( wire.pointSourceIds.nCount == expectedPoints );
            CHECK( MapWireframe_FindObject( wire, id ) != nullptr );
        }
        CHECK( audit.bytes == 0u );
    }
}

geo::geometry_source_id_t NextId( map_document_t &map )
{
    const auto result = geo::GeometrySourceIdAllocator_Allocate( &map.geometry.sourceIds.allocator );
    REQUIRE( result.status == geo::geometry_status_t::OK );
    return result.id;
}

// Two adjacent open ramp quads. ID assignment deliberately differs from
// position/corner order so identity must come from the source descriptor.
u64 AddOpenRampMesh( map_document_t &map )
{
    geo::mesh_source_description_t desc{};
    const auto meshId = NextId( map );
    REQUIRE( geo::MeshSourceDescription_Init( &desc, map.pAllocator, meshId ) == geo::geometry_status_t::OK );
    geo::geometry_source_id_t vertexIds[6]{};
    for ( auto &id : vertexIds ) { id = NextId( map ); }
    const u32 permutation[6]{ 5u, 1u, 4u, 0u, 3u, 2u };
    for ( u32 i = 0u; i < 6u; ++i ) {
        const f64 x = static_cast<f64>( i % 3u ) * 64.0, y = static_cast<f64>( i / 3u ) * 64.0;
        REQUIRE( geo::MeshSourceDescription_TryAddVertex( &desc, { x, y, x * 0.125 + y * 0.25 }, vertexIds[permutation[i]], nullptr ) ==
                 geo::geometry_status_t::OK );
    }
    const auto faceId0 = NextId( map ), faceId1 = NextId( map );
    const u32 first[4]{ 0u, 1u, 4u, 3u }, second[4]{ 1u, 2u, 5u, 4u };
    REQUIRE( geo::MeshSourceDescription_TryAddFace( &desc, { first, 4u }, faceId1, {}, nullptr ) == geo::geometry_status_t::OK );
    REQUIRE( geo::MeshSourceDescription_TryAddFace( &desc, { second, 4u }, faceId0, {}, nullptr ) == geo::geometry_status_t::OK );
    geo::mesh_source_t mesh{};
    REQUIRE( geo::MeshSource_TryBuild( &desc, map.pAllocator, &mesh ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryAddMesh( &map.geometry, &mesh ) == geo::geometry_status_t::OK );
    geo::MeshSource_Shutdown( &mesh );
    geo::MeshSourceDescription_Shutdown( &desc );
    return meshId.value;
}

void CheckMeshIdentities( const map_document_t &map, const map_wireframe_t &wire, u64 meshId )
{
    const auto *pMesh = geo::GeometryDocument_FindMesh( &map.geometry, { meshId } );
    const auto *pObject = MapWireframe_FindObject( wire, meshId );
    REQUIRE( pMesh != nullptr ); REQUIRE( pObject != nullptr );
    REQUIRE( pObject->kind == map_wire_kind_t::MESH );
    REQUIRE( wire.pointSourceIds.nCount == wire.points.nCount );
    geo::mesh_source_description_t desc{};
    REQUIRE( geo::MeshSourceDescription_Init( &desc, Allocator_GetSystem(), { meshId } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::MeshSource_TryDescribe( pMesh, &desc ) == geo::geometry_status_t::OK );
    REQUIRE( pObject->nPoints == desc.vertices.nCount );
    std::set<u64> expectedFaces, actualFaces;
    std::set<std::pair<u64, u64>> expectedEdges, actualEdges;
    geo::mesh_selection_t selection{};
    REQUIRE( geo::MeshSelection_Init( &selection, Allocator_GetSystem(), { meshId } ) == geo::geometry_status_t::OK );
    for ( u32 i = 0u; i < pObject->nPoints; ++i ) {
        const u32 point = pObject->iFirstPoint + i;
        CHECK( wire.pointSourceIds.pData[point] == desc.vertices.pData[i].sourceId.value );
        CHECK( math::Vec3d_EqualsExact( wire.points.pData[point], desc.vertices.pData[i].position ) );
        geo::geometry_mesh_vertex_handle_t hVertex{};
        REQUIRE( geo::MeshSource_TryFindVertex( pMesh, { wire.pointSourceIds.pData[point] }, &hVertex ) );
        CHECK( geo::MeshSource_VertexId( pMesh, hVertex ).value == wire.pointSourceIds.pData[point] );
        REQUIRE( geo::MeshSelection_TryAddVertex( &selection, { wire.pointSourceIds.pData[point] } ) == geo::geometry_status_t::OK );
    }
    for ( usize i = 0u; i < desc.faces.nCount; ++i ) {
        const auto &face = desc.faces.pData[i];
        expectedFaces.insert( face.sourceId.value );
        for ( u32 c = 0u; c < face.cCorners; ++c ) {
            const u64 a = desc.vertices.pData[desc.corners.pData[face.iFirstCorner + c].iVertex].sourceId.value;
            const u64 b = desc.vertices.pData[desc.corners.pData[face.iFirstCorner + ( c + 1u ) % face.cCorners].iVertex].sourceId.value;
            expectedEdges.insert( std::minmax( a, b ) );
        }
    }
    for ( usize i = 0u; i < wire.faces.nCount; ++i ) {
        const auto &face = wire.faces.pData[i];
        if ( face.id != meshId ) { CHECK( face.faceId == 0u ); continue; }
        CHECK( face.sideId == 0u ); CHECK( face.bTwoSided );
        CHECK( actualFaces.insert( face.faceId ).second );
        geo::geometry_mesh_face_handle_t hFace{};
        REQUIRE( geo::MeshSource_TryFindFace( pMesh, { face.faceId }, &hFace ) );
        CHECK( geo::MeshSource_FaceId( pMesh, hFace ).value == face.faceId );
        REQUIRE( geo::MeshSelection_TryAddFace( &selection, { face.faceId } ) == geo::geometry_status_t::OK );
    }
    for ( u32 i = 0u; i < pObject->nLines; ++i ) {
        const auto &line = wire.lines.pData[pObject->iFirstLine + i];
        const u64 a = wire.pointSourceIds.pData[line.iA], b = wire.pointSourceIds.pData[line.iB];
        CHECK( a < b ); CHECK( actualEdges.insert( { a, b } ).second );
        REQUIRE( geo::MeshSelection_TryAddEdge( &selection, geo::MeshEdgeRef_Make( { a }, { b } ) ) == geo::geometry_status_t::OK );
    }
    CHECK( actualFaces == expectedFaces ); CHECK( actualEdges == expectedEdges );
    u32 removed = 0u;
    REQUIRE( geo::MeshSelection_TryPrune( &selection, pMesh, &removed ) == geo::geometry_status_t::OK );
    CHECK( removed == 0u );
    CHECK( selection.vertices.nCount == pObject->nPoints );
    CHECK( selection.edges.nCount == pObject->nLines );
    CHECK( selection.faces.nCount == actualFaces.size() );
    geo::MeshSelection_Shutdown( &selection );
    geo::MeshSourceDescription_Shutdown( &desc );
}

} // namespace

TEST_CASE( "A box brush becomes eight points and twelve edges", "[map][wireframe]" )
{
    map_document_t map{};
    CreateMap( &map );
    const u64 id = AddBox( &map, { 100.0, 0.0, 0.0 }, 32.0 );
    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );

    const map_wire_object_t *pBox = MapWireframe_FindObject( wire, id );
    REQUIRE( pBox != nullptr );
    CHECK( pBox->kind == map_wire_kind_t::BRUSH );
    CHECK( pBox->nLines == 12u );
    CHECK( pBox->iFirstPoint == 0u ); CHECK( pBox->nPoints == 8u );
    CHECK( wire.points.nCount == 8u );
    REQUIRE( wire.pointSourceIds.nCount == wire.points.nCount );
    for ( usize i = 0u; i < wire.pointSourceIds.nCount; ++i ) { CHECK( wire.pointSourceIds.pData[i] == 0u ); }
    CHECK( pBox->bounds.box.minimum.x == 68.0 );
    CHECK( pBox->bounds.box.maximum.x == 132.0 );
    CHECK( pBox->bounds.box.maximum.z == 32.0 );
    CHECK( wire.nBrokenBrushes == 0u );
}

TEST_CASE( "Entities get a box, their class, and a count of tied geometry", "[map][wireframe]" )
{
    map_document_t map{};
    CreateMap( &map );
    const u64 light = AddEntity( &map, "light", { 0.0, 0.0, 64.0 } );
    const u64 trigger = AddEntity( &map, "trigger_once", { 512.0, 0.0, 0.0 } );
    const u64 brush = AddBox( &map, { 512.0, 0.0, 0.0 }, 32.0 );
    REQUIRE( MapDocument_SetGeometryOwner( &map, brush, trigger ) == map_status_t::OK );

    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );

    const map_wire_entity_t *pLight = MapWireframe_FindEntity( wire, light );
    REQUIRE( pLight != nullptr );
    CHECK( std::string( pLight->className ) == "light" );
    CHECK( pLight->origin.z == 64.0 );
    CHECK( pLight->nOwned == 0u );
    const map_wire_object_t *pLightBox = MapWireframe_FindObject( wire, light );
    REQUIRE( pLightBox != nullptr );
    CHECK( pLightBox->kind == map_wire_kind_t::ENTITY );
    CHECK( pLightBox->bounds.box.minimum.z == 64.0 - MAP_WIRE_POINT_ENTITY_HALF );

    const map_wire_entity_t *pTrigger = MapWireframe_FindEntity( wire, trigger );
    REQUIRE( pTrigger != nullptr );
    CHECK( pTrigger->nOwned == 1u );
    const map_wire_object_t *pBrush = MapWireframe_FindObject( wire, brush );
    REQUIRE( pBrush != nullptr );
    CHECK( pBrush->owner == trigger );
}

TEST_CASE( "Picking prefers the smallest object whose edge is under the cursor", "[map][wireframe]" )
{
    map_document_t map{};
    CreateMap( &map );
    const u64 room = AddBox( &map, { 0.0, 0.0, 0.0 }, 256.0 );
    const u64 crate = AddBox( &map, { 0.0, 0.0, 0.0 }, 16.0 );
    const u64 light = AddEntity( &map, "light", { 128.0, 128.0, 0.0 } );
    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );

    // Top view: x across, y up.
    CHECK( MapWireframe_Pick2D( wire, 0u, 1u, 256.0, 10.0, 2.0 ) == room );
    CHECK( MapWireframe_Pick2D( wire, 0u, 1u, 16.5, 0.0, 2.0 ) == crate );
    CHECK( MapWireframe_Pick2D( wire, 0u, 1u, 131.0, 126.0, 2.0 ) == light );
    CHECK( MapWireframe_Pick2D( wire, 0u, 1u, 100.0, -100.0, 2.0 ) == 0u );
    CHECK( MapWireframe_Pick2D( wire, 0u, 1u, 16.5, 0.0, 0.1 ) == 0u );
    // Front view (y, z) sees the same crate from the side.
    CHECK( MapWireframe_Pick2D( wire, 1u, 2u, 0.0, 16.0, 1.0 ) == crate );
    // Invalid axis pairs pick nothing (and assert in debug builds, so only
    // the tolerance guard is exercised here).
    CHECK( MapWireframe_Pick2D( wire, 0u, 1u, 0.0, 0.0, -1.0 ) == 0u );
}

TEST_CASE( "The example map yields a wireframe for every object kind", "[map][wireframe]" )
{
    map_document_t map{};
    const std::string root = ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string();
    REQUIRE( MapFiles_Load( &map, Allocator_GetSystem(), root.c_str() ).status == map_files_status_t::OK );
    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );

    usize counts[5]{};
    std::vector<u32> pointOwners( wire.points.nCount, 0u );
    for ( usize i = 0u; i < wire.objects.nCount; ++i ) {
        const map_wire_object_t &object = wire.objects.pData[i];
        ++counts[static_cast<usize>( object.kind )];
        CHECK( object.bounds.bHas );
        REQUIRE( static_cast<usize>( object.iFirstPoint ) + object.nPoints <= wire.points.nCount );
        for ( u32 vertex = 0; vertex < object.nPoints; ++vertex ) { ++pointOwners[object.iFirstPoint + vertex]; }
        for ( u32 edge = 0; edge < object.nLines; ++edge ) {
            const auto &line = wire.lines.pData[object.iFirstLine + edge];
            CHECK( line.iA >= object.iFirstPoint ); CHECK( line.iB >= object.iFirstPoint );
            CHECK( static_cast<usize>( line.iA ) < static_cast<usize>( object.iFirstPoint ) + object.nPoints );
            CHECK( static_cast<usize>( line.iB ) < static_cast<usize>( object.iFirstPoint ) + object.nPoints );
        }
        if ( object.kind == map_wire_kind_t::ENTITY ) { CHECK( object.nPoints == 0 ); }
        if ( object.kind != map_wire_kind_t::ENTITY ) { CHECK( object.nLines != 0u ); }
        if ( i != 0u ) { CHECK( wire.objects.pData[i - 1u].id < object.id ); }
    }
    for ( u32 owners : pointOwners ) { CHECK( owners == 1 ); }
    CHECK( counts[static_cast<usize>( map_wire_kind_t::BRUSH )] == map.geometry.brushes.nCount );
    CHECK( counts[static_cast<usize>( map_wire_kind_t::MESH )] == map.geometry.meshes.nCount );
    CHECK( counts[static_cast<usize>( map_wire_kind_t::PATCH )] == map.geometry.patches.nCount );
    CHECK( counts[static_cast<usize>( map_wire_kind_t::TERRAIN )] == map.geometry.heightFields.nCount );
    CHECK( map.geometry.meshes.nCount != 0u );
    CHECK( map.geometry.patches.nCount != 0u );
    CHECK( map.geometry.heightFields.nCount != 0u );
    CHECK( counts[static_cast<usize>( map_wire_kind_t::ENTITY )] != 0u );
    CHECK( wire.nBrokenBrushes == 0u );
    CHECK( wire.bounds.bHas );

    bool bAnyTied = false;
    for ( usize i = 0u; i < wire.entities.nCount; ++i ) { bAnyTied = bAnyTied || wire.entities.pData[i].nOwned != 0u; }
    CHECK( bAnyTied );
}

TEST_CASE( "Facility output wires resolve real recipients across chunks without positioning logic entities", "[map][wireframe][connections]" )
{
    map_document_t map{};
    const std::string root = ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string();
    REQUIRE( MapFiles_Load( &map, Allocator_GetSystem(), root.c_str() ).status == map_files_status_t::OK );
    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    REQUIRE( wire.connections.nCount == 4u );
    const auto &spawn = wire.connections.pData[0];
    CHECK( spawn.sourceId == 101u );
    CHECK( spawn.targetId == 170u );
    CHECK( spawn.status == map_wire_connection_status_t::RESOLVED );
    CHECK( spawn.bHasSourceOrigin );
    CHECK_FALSE( spawn.bHasTargetOrigin );
    REQUIRE( MapWireframe_FindEntity( wire, 170u ) != nullptr );
    CHECK_FALSE( MapWireframe_FindEntity( wire, 170u )->bHasOrigin );
    const auto &trigger = wire.connections.pData[1];
    CHECK( trigger.sourceId == 110u );
    CHECK( trigger.targetId == 120u );
    CHECK( trigger.sourceOrigin.x == 0.0 );
    CHECK( trigger.targetOrigin.y == 400.0 );
    CHECK( trigger.targetOrigin.z == 32.0 );
    CHECK( trigger.bHasSourceOrigin );
    CHECK( trigger.bHasTargetOrigin );
    CHECK( std::string( trigger.output ) == "on_trigger" );
    CHECK( std::string( trigger.input ) == "start" );
    CHECK( std::string( trigger.target ) == "wave1" );
    const auto &missing = wire.connections.pData[2];
    CHECK( missing.sourceId == 120u );
    CHECK( missing.iOutput == 0u );
    CHECK( missing.status == map_wire_connection_status_t::MISSING_TARGET );
    CHECK( missing.targetId == 0u );
    CHECK_FALSE( missing.bHasTargetOrigin );
    const auto &door = wire.connections.pData[3];
    CHECK( door.sourceId == 120u );
    CHECK( door.targetId == 150u );
    CHECK( door.iOutput == 1u );
    CHECK( door.targetOrigin.y == 504.0 );
    CHECK( door.targetOrigin.z == 64.0 );
    CHECK( door.iSourceLayer == MapWireframe_FindEntity( wire, 120u )->iLayer );
    CHECK( door.iTargetLayer == MapWireframe_FindEntity( wire, 150u )->iLayer );
}

TEST_CASE( "Output names fan out to all duplicate and prefix recipients in deterministic ID order", "[map][wireframe][connections]" )
{
    map_document_t map{};
    LoadEntityMap( map, R"(
        { id = 4u class = "func_door" name = "doorway" origin = [ 40, 0, 0 ] },
        { id = 3u class = "func_door" name = "door" origin = [ 30, 0, 0 ] },
        { id = 1u class = "trigger_once" origin = [ 1, 2, 3 ] outputs = [
            { output = "on_trigger" target = "door" input = "open" },
            { output = "on_end" target = "door*" input = "close" },
            { output = "on_reset" target = "DOOR" input = "open" } ] },
        { id = 2u class = "func_door" name = "door" origin = [ 20, 0, 0 ] }
    )" );
    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    REQUIRE( wire.connections.nCount == 6u );
    const u64 targets[]{ 2u, 3u, 2u, 3u, 4u, 0u };
    const usize outputs[]{ 0u, 0u, 1u, 1u, 1u, 2u };
    for ( usize i = 0u; i < 6u; ++i ) {
        CHECK( wire.connections.pData[i].sourceId == 1u );
        CHECK( wire.connections.pData[i].targetId == targets[i] );
        CHECK( wire.connections.pData[i].iOutput == outputs[i] );
    }
    CHECK( wire.connections.pData[5].status == map_wire_connection_status_t::MISSING_TARGET );
}

TEST_CASE( "Special and malformed output targets do not manufacture world endpoints", "[map][wireframe][connections]" )
{
    map_document_t map{};
    LoadEntityMap( map, R"(
        { id = 1u class = "logic_relay" outputs = [
            { output = "fire" target = "!self" input = "trigger" },
            { output = "fire" target = "!activator" input = "trigger" },
            { output = "fire" target = "!caller" input = "trigger" },
            { output = "fire" target = "!player" input = "trigger" },
            { output = "fire" target = "!unknown" input = "trigger" },
            { output = "fire" target = 42 input = "trigger" },
            { output = "fire" target = "relay" },
            { output = "fire" target = "re*lay" input = "trigger" },
            7 ] },
        { id = 2u class = "logic_relay" name = "relay" origin = [ 5, 6, 7 ] }
    )" );
    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    REQUIRE( wire.connections.nCount == 9u );
    CHECK( wire.connections.pData[0].status == map_wire_connection_status_t::RESOLVED );
    CHECK( wire.connections.pData[0].targetId == 1u );
    for ( usize i = 0u; i < 9u; ++i ) {
        CHECK_FALSE( wire.connections.pData[i].bHasSourceOrigin );
        CHECK_FALSE( wire.connections.pData[i].bHasTargetOrigin );
        if ( i == 0u ) { continue; }
        CHECK( wire.connections.pData[i].targetId == 0u );
        CHECK( wire.connections.pData[i].status == ( i < 4u ? map_wire_connection_status_t::RUNTIME_TARGET
                                                                         : map_wire_connection_status_t::INVALID_OUTPUT ) );
    }
}

TEST_CASE( "Connection matching uses complete names rather than truncated viewport labels", "[map][wireframe][connections]" )
{
    const std::string prefix( 90u, 'a' );
    map_document_t map{};
    LoadEntityMap( map,
        "{ id = 1u class = \"trigger_once\" origin = [ 0, 0, 0 ] outputs = [ { output = \"fire\" input = \"open\" target = \"" + prefix + "B\" } ] },"
        "{ id = 2u class = \"func_door\" name = \"" + prefix + "A\" origin = [ 2, 0, 0 ] },"
        "{ id = 3u class = \"func_door\" name = \"" + prefix + "B\" origin = [ 3, 0, 0 ] }" );
    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    REQUIRE( wire.connections.nCount == 1u );
    CHECK( wire.connections.pData[0].targetId == 3u );
    CHECK( wire.connections.pData[0].targetOrigin.x == 3.0 );
    CHECK( std::string( wire.connections.pData[0].target ).size() == MAP_WIRE_TEXT_CAPACITY - 1u );
}

TEST_CASE( "Wire rebuilds remove stale connections and do not retain document pointers", "[map][wireframe][connections]" )
{
    map_document_t map{};
    LoadEntityMap( map, R"(
        { id = 1u class = "trigger_once" origin = [ 0, 0, 0 ] outputs = [ { output = "fire" target = "door" input = "open" } ] },
        { id = 2u class = "func_door" name = "door" origin = [ 32, 0, 0 ] }
    )" );
    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    REQUIRE( wire.connections.nCount == 1u );
    REQUIRE( MapDocument_SetEntityOrigin( &map, 2u, { 64.0, 0.0, 0.0 } ) == map_status_t::OK );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    CHECK( wire.connections.pData[0].targetOrigin.x == 64.0 );
    REQUIRE( MapDocument_RemoveObject( &map, 2u ) == map_status_t::OK );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    REQUIRE( wire.connections.nCount == 1u );
    CHECK( wire.connections.pData[0].status == map_wire_connection_status_t::MISSING_TARGET );
    CHECK_FALSE( wire.connections.pData[0].bHasTargetOrigin );
    MapDocument_Shutdown( &map );
    CHECK( std::string( wire.connections.pData[0].target ) == "door" );
    CreateMap( &map );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    CHECK( wire.connections.nCount == 0u );
}

TEST_CASE( "The all-names wildcard excludes unnamed entities and skips damaged chunks", "[map][wireframe][connections]" )
{
    map_document_t map{};
    LoadEntityMap( map, R"(
        { id = 1u class = "trigger_once" origin = [ 0, 0, 0 ] outputs = [ { output = "fire" target = "*" input = "open" } ] },
        { id = 2u class = "func_door" name = "door" origin = [ 32, 0, 0 ] },
        { id = 3u class = "func_door" origin = [ 48, 0, 0 ] }
    )" );
    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    REQUIRE( wire.connections.nCount == 1u );
    CHECK( wire.connections.pData[0].targetId == 2u );
    REQUIRE( map.chunks.nCount == 1u );
    map.chunks.pData[0]->bDamaged = CY_TRUE;
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    CHECK( wire.connections.nCount == 0u );
}

TEST_CASE( "A connection rebuild allocation failure clears stale viewport data", "[map][wireframe][connections]" )
{
    map_document_t map{};
    LoadEntityMap( map, R"(
        { id = 1u class = "trigger_once" origin = [ 0, 0, 0 ] outputs = [ { output = "fire" target = "door" input = "open" } ] },
        { id = 2u class = "func_door" name = "door" origin = [ 32, 0, 0 ] }
    )" );
    bool bFail = false;
    const allocator_t allocator{
        []( void *pContext, usize size, usize alignment ) noexcept -> void * {
            return *static_cast<bool *>( pContext ) ? nullptr : Allocator_Allocate( Allocator_GetSystem(), size, alignment );
        },
        nullptr,
        []( void *, void *pMemory, usize size, usize alignment ) noexcept {
            Allocator_Free( Allocator_GetSystem(), pMemory, size, alignment );
        },
        &bFail
    };
    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, &allocator ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    REQUIRE( wire.connections.nCount == 1u );
    bFail = true;
    CHECK( MapWireframe_Build( &wire, map ) == map_status_t::OUT_OF_MEMORY );
    CHECK( wire.connections.nCount == 0u );
    CHECK( wire.entities.nCount == 0u );
    CHECK( wire.objects.nCount == 0u );
    CHECK_FALSE( wire.bounds.bHas );
}

TEST_CASE( "Every brush wire allocation failure aborts and clears the partial view", "[map][wireframe][allocation]" )
{
    map_document_t map{}; CreateMap( &map ); const u64 id = AddBox( &map, {}, 32.0 );
    SweepBuildFailures( map, id );
    CHECK( map.geometry.brushes.nCount == 1u );
}

TEST_CASE( "Brush cap faces above 64 corners retain their complete rings and checked scratch allocation", "[map][wireframe][allocation]" )
{
    constexpr u32 sides = 65u;
    map_document_t map{}; CreateMap( &map ); geo::brush_solid_t cylinder{};
    REQUIRE( geo::BrushGenerator_TryMakeCylinder( &cylinder, map.pAllocator, map.geometryPolicy, &map.geometry.sourceIds.allocator,
        {}, 128.0, 32.0, sides, 2u ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryAddBrush( &map.geometry, &cylinder ) == geo::geometry_status_t::OK );
    const u64 id = cylinder.sourceId.value;
    const u64 top = cylinder.sides.pData[sides].sourceId.value, bottom = cylinder.sides.pData[sides + 1u].sourceId.value;
    geo::BrushSolid_Shutdown( &cylinder );
    {
        map_wireframe_t wire{}; REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
        REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
        REQUIRE( wire.faces.nCount == sides + 2u ); usize caps = 0u;
        for ( usize i = 0u; i < wire.faces.nCount; ++i ) {
            const auto &face = wire.faces.pData[i];
            if ( face.sideId != top && face.sideId != bottom ) { CHECK( face.nIndices == 4u ); continue; }
            ++caps; REQUIRE( face.nIndices == sides ); std::set<u32> indices;
            for ( u32 c = 0u; c < face.nIndices; ++c ) {
                const u32 index = wire.faceIndices.pData[face.iFirstIndex + c];
                REQUIRE( index < wire.points.nCount ); CHECK( indices.insert( index ).second );
                CHECK( std::abs( wire.points.pData[index].z - ( face.sideId == top ? 32.0 : -32.0 ) ) <=
                       map.geometryPolicy.numerical.fAbsoluteDistanceTolerance );
            }
            CHECK( indices.size() == sides );
        }
        CHECK( caps == 2u ); CHECK( wire.nBrokenBrushes == 0u );
    }
    SweepBuildFailures( map, id );
}

TEST_CASE( "Every mesh descriptor and wire allocation failure aborts without dropping an object", "[map][wireframe][allocation]" )
{
    map_document_t facility{};
    const std::string root = ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string();
    REQUIRE( MapFiles_Load( &facility, Allocator_GetSystem(), root.c_str() ).status == map_files_status_t::OK );
    REQUIRE( facility.geometry.meshes.nCount != 0u );
    map_document_t map{}; CreateMap( &map );
    REQUIRE( geo::GeometryDocument_TryAddMesh( &map.geometry, facility.geometry.meshes.pData[0] ) == geo::geometry_status_t::OK );
    SweepBuildFailures( map, map.geometry.meshes.pData[0]->sourceId.value );
    CHECK( map.geometry.meshes.nCount == 1u );
}

TEST_CASE( "Open mesh wire components retain source identities and unique real polygon edges", "[map][wireframe][mesh-components]" )
{
    map_document_t map{}; CreateMap( &map );
    const u64 meshId = AddOpenRampMesh( map );
    const u64 ownerId = AddEntity( &map, "prop_static", { 256.0, 0.0, 0.0 } );
    REQUIRE( MapDocument_SetGeometryOwner( &map, meshId, ownerId ) == map_status_t::OK );
    map_wireframe_t wire{}; REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    const auto *pObject = MapWireframe_FindObject( wire, meshId );
    REQUIRE( pObject != nullptr );
    CHECK( pObject->owner == ownerId ); CHECK( pObject->nPoints == 6u );
    // Eight directed quad boundaries contain one shared edge. The two fan
    // diagonals used to fill the quads are absent from editable edge picking.
    CHECK( pObject->nLines == 7u ); CHECK( wire.faces.nCount == 2u );
    CheckMeshIdentities( map, wire, meshId );
    CHECK( MapWireframe_Pick2D( wire, 0u, 1u, 32.0, 32.0, 0.1 ) == 0u );
    CHECK( MapWireframe_Pick2D( wire, 0u, 1u, 64.0, 32.0, 0.1 ) == meshId );
    SweepBuildFailures( map, meshId );
}

TEST_CASE( "Authored mesh cache identities survive shifted point ranges and save reload", "[map][wireframe][mesh-components][files]" )
{
    map_document_t map{}; CreateMap( &map ); const u64 meshId = AddOpenRampMesh( map );
    map_wireframe_t wire{}; REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    CheckMeshIdentities( map, wire, meshId );
    const auto *pObject = MapWireframe_FindObject( wire, meshId ); REQUIRE( pObject != nullptr );
    const u32 previousFirstPoint = pObject->iFirstPoint;
    const std::vector<u64> vertexIds( wire.pointSourceIds.pData + pObject->iFirstPoint,
                                     wire.pointSourceIds.pData + pObject->iFirstPoint + pObject->nPoints );
    std::vector<u64> faceIds;
    for ( usize i = 0u; i < wire.faces.nCount; ++i ) { if ( wire.faces.pData[i].id == meshId ) { faceIds.push_back( wire.faces.pData[i].faceId ); } }
    const u64 brushId = AddBox( &map, { -256.0, 0.0, 0.0 }, 32.0 );
    REQUIRE( MapWireframe_Build( &wire, map ) == map_status_t::OK );
    pObject = MapWireframe_FindObject( wire, meshId ); REQUIRE( pObject != nullptr );
    CHECK( pObject->iFirstPoint != previousFirstPoint );
    REQUIRE( pObject->nPoints == vertexIds.size() );
    for ( u32 i = 0u; i < pObject->nPoints; ++i ) { CHECK( wire.pointSourceIds.pData[pObject->iFirstPoint + i] == vertexIds[i] ); }
    CheckMeshIdentities( map, wire, meshId );

    struct scratch_t {
        std::filesystem::path path{ std::filesystem::temp_directory_path() / "cypher_wire_mesh_identity_roundtrip" };
        scratch_t() { std::filesystem::remove_all( path ); std::filesystem::create_directories( path ); }
        ~scratch_t() { std::filesystem::remove_all( path ); }
    } scratch;
    const auto root = ( scratch.path / "identities.cymap" ).string();
    REQUIRE( MapFiles_Save( &map, root.c_str() ).status == map_files_status_t::OK );
    map_document_t loaded{};
    REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), root.c_str() ).status == map_files_status_t::OK );
    REQUIRE( MapWireframe_Build( &wire, loaded ) == map_status_t::OK );
    CheckMeshIdentities( loaded, wire, meshId );
    pObject = MapWireframe_FindObject( wire, meshId ); REQUIRE( pObject != nullptr );
    REQUIRE( pObject->nPoints == vertexIds.size() );
    for ( u32 i = 0u; i < pObject->nPoints; ++i ) { CHECK( wire.pointSourceIds.pData[pObject->iFirstPoint + i] == vertexIds[i] ); }
    std::vector<u64> loadedFaceIds;
    for ( usize i = 0u; i < wire.faces.nCount; ++i ) { if ( wire.faces.pData[i].id == meshId ) { loadedFaceIds.push_back( wire.faces.pData[i].faceId ); } }
    CHECK( loadedFaceIds == faceIds );
    const auto *pBrush = MapWireframe_FindObject( wire, brushId ); REQUIRE( pBrush != nullptr );
    for ( u32 i = 0u; i < pBrush->nPoints; ++i ) { CHECK( wire.pointSourceIds.pData[pBrush->iFirstPoint + i] == 0u ); }
    // Reusing capacity for an unrelated document must not leave stale source
    // IDs after the corresponding presentation points have disappeared.
    MapDocument_Shutdown( &loaded ); CreateMap( &loaded );
    REQUIRE( MapWireframe_Build( &wire, loaded ) == map_status_t::OK ); CheckEmpty( wire );
}

TEST_CASE( "Terrain lattice and patch wire allocation failures leave no partial indices", "[map][wireframe][allocation]" )
{
    map_document_t facility{};
    const std::string root = ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string();
    REQUIRE( MapFiles_Load( &facility, Allocator_GetSystem(), root.c_str() ).status == map_files_status_t::OK );
    map_document_t map{}; CreateMap( &map );
    SECTION( "Terrain scratch lattice uses the caller's allocator" )
    {
        REQUIRE( facility.geometry.heightFields.nCount != 0u );
        REQUIRE( geo::GeometryDocument_TryAddHeightField( &map.geometry, facility.geometry.heightFields.pData[0] ) == geo::geometry_status_t::OK );
        SweepBuildFailures( map, map.geometry.heightFields.pData[0]->sourceId.value );
    }
    SECTION( "Patch face generation stops after a failed control-point append" )
    {
        REQUIRE( facility.geometry.patches.nCount != 0u );
        REQUIRE( geo::GeometryDocument_TryAddPatch( &map.geometry, facility.geometry.patches.pData[0] ) == geo::geometry_status_t::OK );
        SweepBuildFailures( map, map.geometry.patches.pData[0]->sourceId.value );
    }
}
