//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Document_Tests.cpp
//  Purpose: Contract tests for the map document (CYMAP.md, ADR 0011).
//  Details: The map is the one file format every level lives in, so these
//           pin what must never regress: saving an unchanged map reproduces
//           the same bytes; every object kind is written readably and read
//           back exactly; objects land in the chunk their layer and position
//           select; nothing unknown or unreadable is ever dropped; hand
//           edits (missing IDs, odd layout, comments) are accepted; damage
//           never spreads to files the editor did not understand; and IDs
//           are never reused.
//
//  History:
//  - Created by Karlo Siric on 2026-09-27
//  - Rewritten on 2026-09-27 for map-owned readable geometry
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Document.h"

#include "CypherGeometry_BrushGenerator.h"
#include "CypherGeometry_BrushSolid.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValueParser.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;

namespace
{

string_view_t SV( const std::string &text )
{
    return { text.data(), text.size() };
}

// Collects save output the way a directory would look afterwards.
struct saved_files_t {
    std::map<std::string, std::string> chunks;
    std::string root;
    std::vector<std::string> written;
    std::vector<std::string> removed;
    usize nCalls{ 0u };
    usize iFailAt{ ~usize{ 0u } }; // Fail the n-th callback.
    const usize *pAllocationCalls{ nullptr };
    usize allocationsAtFirstSink{ CY_USIZE_MAX };

    bool_t Call() noexcept
    {
        if ( nCalls == 0u && pAllocationCalls != nullptr ) { allocationsAtFirstSink = *pAllocationCalls; }
        return nCalls++ != iFailAt;
    }

    map_save_sink_t Sink()
    {
        map_save_sink_t sink{};
        sink.pContext = this;
        sink.pfnWriteChunk = []( void *pContext, string_view_t path, string_view_t text ) noexcept -> bool_t {
            auto *pFiles = static_cast<saved_files_t *>( pContext );
            if ( !pFiles->Call() ) { return CY_FALSE; }
            pFiles->chunks[std::string( path.pData, path.cchLength )] = std::string( text.pData, text.cchLength );
            pFiles->written.emplace_back( path.pData, path.cchLength );
            return CY_TRUE;
        };
        sink.pfnRemoveChunk = []( void *pContext, string_view_t path ) noexcept -> bool_t {
            auto *pFiles = static_cast<saved_files_t *>( pContext );
            if ( !pFiles->Call() ) { return CY_FALSE; }
            pFiles->chunks.erase( std::string( path.pData, path.cchLength ) );
            pFiles->removed.emplace_back( path.pData, path.cchLength );
            return CY_TRUE;
        };
        sink.pfnWriteRoot = []( void *pContext, string_view_t text ) noexcept -> bool_t {
            auto *pFiles = static_cast<saved_files_t *>( pContext );
            if ( !pFiles->Call() ) { return CY_FALSE; }
            pFiles->root.assign( text.pData, text.cchLength );
            return CY_TRUE;
        };
        return sink;
    }

    void ClearLog()
    {
        written.clear();
        removed.clear();
        nCalls = 0u;
        allocationsAtFirstSink = CY_USIZE_MAX;
    }
};

map_status_t Save( map_document_t *pMap, saved_files_t &files )
{
    files.ClearLog();
    return MapDocument_Save( pMap, files.Sink() );
}

map_status_t Load( map_document_t *pMap, const std::string &root, const std::map<std::string, std::string> &chunks, u32 flags = MAP_LOAD_FLAG_NONE )
{
    std::vector<map_chunk_input_t> inputs;
    for ( const auto &[path, text] : chunks ) { inputs.push_back( { SV( path ), SV( text ) } ); }
    return MapDocument_Load( pMap, Allocator_GetSystem(), SV( root ), span_t<const map_chunk_input_t>{ inputs.data(), inputs.size() }, flags );
}

usize CountProblems( const map_document_t &map, map_problem_code_t code )
{
    usize n = 0u;
    for ( usize i = 0u; i < map.problems.nCount; ++i ) { n += map.problems.pData[i].code == code ? 1u : 0u; }
    return n;
}

bool HasProblem( const map_document_t &map, map_problem_code_t code )
{
    return CountProblems( map, code ) != 0u;
}

bool Contains( const std::string &text, const char *pNeedle )
{
    return text.find( pNeedle ) != std::string::npos;
}

usize Occurrences( const std::string &text, const char *pNeedle )
{
    usize n = 0u;
    for ( usize i = text.find( pNeedle ); i != std::string::npos; i = text.find( pNeedle, i + 1u ) ) { ++n; }
    return n;
}

void CreateMap( map_document_t *pMap )
{
    map_create_desc_t desc{};
    desc.name = StringView_FromCString( "Test Map" );
    desc.game = StringView_FromCString( "reap" );
    REQUIRE( MapDocument_Create( pMap, Allocator_GetSystem(), desc ) == map_status_t::OK );
}

struct save_allocation_audit_t {
    usize calls{}, bytes{}, failAt{ CY_USIZE_MAX };
    allocator_t allocator{};
    save_allocation_audit_t()
    {
        allocator.pUserData = this;
        allocator.pfnAllocate = []( void *pContext, usize size, usize alignment ) noexcept -> void * {
            auto &audit = *static_cast<save_allocation_audit_t *>( pContext );
            if ( audit.calls++ == audit.failAt ) { return nullptr; }
            void *p = Allocator_Allocate( Allocator_GetSystem(), size, alignment );
            if ( p != nullptr ) { audit.bytes += size; }
            return p;
        };
        allocator.pfnFree = []( void *pContext, void *p, usize size, usize alignment ) noexcept {
            if ( p != nullptr ) { static_cast<save_allocation_audit_t *>( pContext )->bytes -= size; }
            Allocator_Free( Allocator_GetSystem(), p, size, alignment );
        };
    }
};

u64 AddBox( map_document_t *pMap, math::vec3d_t center, f64 half = 32.0 )
{
    geo::brush_solid_t box{};
    REQUIRE( geo::BrushGenerator_TryMakeBox( &box, pMap->pAllocator, pMap->geometryPolicy, &pMap->geometry.sourceIds.allocator, center,
                                             math::vec3d_t{ half, half, half } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryAddBrush( &pMap->geometry, &box ) == geo::geometry_status_t::OK );
    const u64 id = box.sourceId.value;
    geo::BrushSolid_Shutdown( &box );
    return id;
}

void SetFaceMaterial( map_document_t *pMap, u64 brushId, usize iSide, const char *pPath )
{
    u64 ref = 0u;
    REQUIRE( MapDocument_MaterialRef( pMap, StringView_FromCString( pPath ), &ref ) == map_status_t::OK );
    const geo::brush_solid_t *pBrush = geo::GeometryDocument_FindBrush( &pMap->geometry, geo::geometry_source_id_t{ brushId } );
    REQUIRE( pBrush != nullptr );
    geo::geometry_brush_side_attribute_store_t *pStore =
        geo::GeometryDocument_FindBrushAttributesMutable( &pMap->geometry, geo::geometry_source_id_t{ brushId } );
    REQUIRE( pStore != nullptr );
    pStore->records.pData[pBrush->sides.pData[iSide].iAttributeIndex].material.value = ref;
}

geo::geometry_source_id_t NextId( map_document_t *pMap )
{
    const geo::geometry_source_id_result_t result = geo::GeometrySourceIdAllocator_Allocate( &pMap->geometry.sourceIds.allocator );
    REQUIRE( result.status == geo::geometry_status_t::OK );
    return result.id;
}

u64 AddMesh( map_document_t *pMap )
{
    geo::mesh_source_description_t desc{};
    const geo::geometry_source_id_t meshId = NextId( pMap );
    REQUIRE( geo::MeshSourceDescription_Init( &desc, pMap->pAllocator, meshId ) == geo::geometry_status_t::OK );
    const math::vec3d_t corners[4]{ { 0.0, 0.0, 0.0 }, { 64.0, 0.0, 0.0 }, { 64.0, 64.0, 8.0 }, { 0.0, 64.0, 8.0 } };
    for ( const math::vec3d_t &corner : corners ) {
        REQUIRE( geo::MeshSourceDescription_TryAddVertex( &desc, corner, NextId( pMap ), nullptr ) == geo::geometry_status_t::OK );
    }
    u64 ref = 0u;
    REQUIRE( MapDocument_MaterialRef( pMap, StringView_FromCString( "materials/metal/pipe.cymat" ), &ref ) == map_status_t::OK );
    geo::mesh_face_attributes_t attributes{};
    attributes.material.value = ref;
    attributes.smoothingGroups = 3u;
    const u32 indices[4]{ 0u, 1u, 2u, 3u };
    u32 iFace = 0u;
    REQUIRE( geo::MeshSourceDescription_TryAddFace( &desc, { indices, 4u }, NextId( pMap ), attributes, &iFace ) == geo::geometry_status_t::OK );
    const math::vec2d_t uvs[4]{ { 0.0, 0.0 }, { 1.0, 0.0 }, { 1.0, 1.0 }, { 0.0, 1.0 } };
    for ( u32 k = 0u; k < 4u; ++k ) {
        desc.corners.pData[desc.faces.pData[iFace].iFirstCorner + k].attributes.uv0 = uvs[k];
    }
    desc.corners.pData[desc.faces.pData[iFace].iFirstCorner].attributes.colorRgba = 0xFF000080u;
    geo::mesh_edge_attributes_t edge{};
    edge.flags = geo::MESH_EDGE_FLAG_HARD;
    REQUIRE( geo::MeshSourceDescription_TrySetEdge( &desc, 0u, 1u, edge, 0.5 ) == geo::geometry_status_t::OK );
    geo::mesh_source_t mesh{};
    REQUIRE( geo::MeshSource_TryBuild( &desc, pMap->pAllocator, &mesh ) == geo::geometry_status_t::OK );
    REQUIRE( geo::GeometryDocument_TryAddMesh( &pMap->geometry, &mesh ) == geo::geometry_status_t::OK );
    geo::MeshSource_Shutdown( &mesh );
    geo::MeshSourceDescription_Shutdown( &desc );
    return meshId.value;
}

u64 AddPatch( map_document_t *pMap )
{
    geo::patch_surface_t patch{};
    const geo::geometry_source_id_t patchId = NextId( pMap );
    REQUIRE( geo::Patch_TryInitFlat( &patch, pMap->pAllocator, geo::patch_basis_t::BIQUADRATIC_BEZIER, 3u, 3u, { 0.0, 0.0, 0.0 },
                                     { 128.0, 0.0, 0.0 }, { 0.0, 0.0, 64.0 }, patchId, &pMap->geometry.sourceIds.allocator ) ==
             geo::geometry_status_t::OK );
    u64 ref = 0u;
    REQUIRE( MapDocument_MaterialRef( pMap, StringView_FromCString( "materials/concrete/arch.cymat" ), &ref ) == map_status_t::OK );
    patch.materialId = static_cast<u32>( ref );
    REQUIRE( geo::GeometryDocument_TryAddPatch( &pMap->geometry, &patch ) == geo::geometry_status_t::OK );
    geo::Patch_Shutdown( &patch );
    return patchId.value;
}

u64 AddTerrain( map_document_t *pMap )
{
    geo::heightfield_t field{};
    const geo::geometry_source_id_t fieldId = NextId( pMap );
    REQUIRE( geo::HeightField_TryInit( &field, pMap->pAllocator, { -256.0, -256.0, 0.0 }, 64.0, 8u, 8u, 4u, fieldId,
                                       &pMap->geometry.sourceIds.allocator ) == geo::geometry_status_t::OK );
    for ( usize i = 0u; i < field.heights.nCount; ++i ) { field.heights.pData[i] = static_cast<f64>( i % 5u ) * 4.0; }
    field.holes.pData[3u * 8u + 2u] = 1u;
    REQUIRE( geo::GeometryDocument_TryAddHeightField( &pMap->geometry, &field ) == geo::geometry_status_t::OK );
    geo::HeightField_Shutdown( &field );
    return fieldId.value;
}

// A hand-written root for tests that exercise reading.
std::string Root( const char *pNextId = "100u", const char *pExtra = "" )
{
    return std::string( "@cykv 1\n@schema \"cypher.map\" 10\n{\n"
                        "    map_id = \"4c1f0a52-7a8e-4f3b-9d61-0b2d3e4f5a6b\"\n"
                        "    name = \"Hand\"\n"
                        "    game = \"reap\"\n"
                        "    next_id = " ) +
           pNextId + "\n    layers = [ { id = \"default\" } ]\n" + pExtra + "}\n";
}

std::string Chunk( const char *pLayer, const char *pCell, const std::string &body, const char *pMapId = "4c1f0a52-7a8e-4f3b-9d61-0b2d3e4f5a6b" )
{
    return std::string( "@cykv 1\n@schema \"cypher.map_chunk\" 10\n{\n    map_id = \"" ) + pMapId + "\"\n    layer = \"" + pLayer +
           "\"\n    cell = " + pCell + "\n" + body + "}\n";
}

// Six faces of an axis-aligned box in the readable brush format; IDs left
// out so the reader assigns them, as for any hand-added brush.
std::string BoxFaces( f64 minX, f64 minY, f64 minZ, f64 maxX, f64 maxY, f64 maxZ, const char *pMaterial )
{
    const auto real = []( f64 v ) { return std::to_string( v ); };
    const std::string material = std::string( " material = \"" ) + pMaterial + "\"";
    return "faces = [\n"
           "  { plane = [ 1, 0, 0, " + real( maxX ) + " ]" + material + " uv_u = [ 0, 1, 0 ] uv_v = [ 0, 0, -1 ] uv_size = [ 128, 128 ] },\n"
           "  { plane = [ -1, 0, 0, " + real( -minX ) + " ]" + material + " uv_u = [ 0, -1, 0 ] uv_v = [ 0, 0, -1 ] uv_size = [ 128, 128 ] },\n"
           "  { plane = [ 0, 1, 0, " + real( maxY ) + " ]" + material + " uv_u = [ -1, 0, 0 ] uv_v = [ 0, 0, -1 ] uv_size = [ 128, 128 ] },\n"
           "  { plane = [ 0, -1, 0, " + real( -minY ) + " ]" + material + " uv_u = [ 1, 0, 0 ] uv_v = [ 0, 0, -1 ] uv_size = [ 128, 128 ] },\n"
           "  { plane = [ 0, 0, 1, " + real( maxZ ) + " ]" + material + " uv_u = [ 1, 0, 0 ] uv_v = [ 0, -1, 0 ] uv_size = [ 128, 128 ] },\n"
           "  { plane = [ 0, 0, -1, " + real( -minZ ) + " ]" + material + " uv_u = [ 1, 0, 0 ] uv_v = [ 0, 1, 0 ] uv_size = [ 128, 128 ] }\n"
           "]";
}

// Saves, reloads, and saves again: the second save must reproduce the
// first. Maps that deliberately keep unreadable objects pass bProblemsExpected.
void RequireStable( const saved_files_t &first, bool bProblemsExpected = false )
{
    map_document_t loaded{};
    REQUIRE( Load( &loaded, first.root, first.chunks ) == map_status_t::OK );
    for ( usize i = 0u; !bProblemsExpected && i < loaded.problems.nCount; ++i ) {
        INFO( MapDocument_ProblemName( loaded.problems.pData[i].code ) << " " << loaded.problems.pData[i].path << " "
                                                                       << loaded.problems.pData[i].member );
        CHECK( false );
    }
    saved_files_t second;
    REQUIRE( Save( &loaded, second ) == map_status_t::OK );
    CHECK( second.root == first.root );
    CHECK( second.chunks == first.chunks );
    CHECK( second.removed.empty() );
}

} // namespace

// ---------------------------------------------------------------------------
// Writing and reading every object kind
// ---------------------------------------------------------------------------

TEST_CASE( "A new map saves a readable root and one chunk per occupied cell", "[map][document]" )
{
    map_document_t map{};
    CreateMap( &map );
    u64 id = 0u;
    REQUIRE( MapDocument_AddEntity( &map, StringView_FromCString( "default" ), StringView_FromCString( "info_player_start" ),
                                    { 128.0, -64.0, 16.0 }, &id ) == map_status_t::OK );
    CHECK( id == 1u );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    CHECK( Contains( files.root, "@schema \"cypher.map\" 10" ) );
    CHECK( Contains( files.root, "\n    next_id = 2u\n" ) );
    CHECK( Contains( files.root, "layers = [ { id = \"default\" } ]" ) );
    REQUIRE( files.chunks.size() == 1u );
    const std::string &chunk = files.chunks["default/x0_y-1.cymapchunk"];
    CHECK( Contains( chunk, "@schema \"cypher.map_chunk\" 10" ) );
    CHECK( Contains( chunk, "\n    layer = \"default\"\n" ) );
    CHECK( Contains( chunk, "\n    cell = [ 0, -1 ]\n" ) );
    CHECK( Contains( chunk, "info = { entities = 1 bounds = [ 128.0, -64.0, 16.0, 128.0, -64.0, 16.0 ] }" ) );
    CHECK( Contains( chunk, "entities = [ { id = 1u class = \"info_player_start\" origin = [ 128.0, -64.0, 16.0 ] } ]" ) );
}

TEST_CASE( "Brushes are written as planes, materials, and texture mappings", "[map][document][geometry]" )
{
    map_document_t map{};
    CreateMap( &map );
    const u64 brushId = AddBox( &map, { 0.0, 0.0, 64.0 } );
    SetFaceMaterial( &map, brushId, 0u, "materials/concrete/floor01.cymat" );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    const std::string &chunk = files.chunks["default/x0_y0.cymapchunk"];
    INFO( chunk );
    // +X face of a 64-unit box centred at (0, 0, 64): n . p = 32.
    CHECK( Contains( chunk, "plane = [ 1.0, 0.0, 0.0, 32.0 ] material = \"materials/concrete/floor01.cymat\"" ) );
    CHECK( Contains( chunk, "plane = [ 0.0, 0.0, -1.0, -32.0 ]" ) );
    CHECK( Contains( chunk, "info = { bounds = [ -32.0, -32.0, 32.0, 32.0, 32.0, 96.0 ] }" ) );
    CHECK( Occurrences( chunk, "plane = [" ) == 6u );
    CHECK( Occurrences( chunk, "material = " ) == 1u );
    CHECK_FALSE( Contains( chunk, "normal_x" ) );
    CHECK_FALSE( Contains( chunk, "claimed_source_ids" ) );
    RequireStable( files );

    map_document_t loaded{};
    REQUIRE( Load( &loaded, files.root, files.chunks ) == map_status_t::OK );
    const geo::brush_solid_t *pBrush = geo::GeometryDocument_FindBrush( &loaded.geometry, geo::geometry_source_id_t{ brushId } );
    REQUIRE( pBrush != nullptr );
    CHECK( pBrush->sides.nCount == 6u );
    CHECK( pBrush->sides.pData[0].plane.d == -32.0 );
}

TEST_CASE( "Meshes, patches, and terrains are written readably and read back exactly", "[map][document][geometry]" )
{
    map_document_t map{};
    CreateMap( &map );
    const u64 meshId = AddMesh( &map );
    const u64 patchId = AddPatch( &map );
    const u64 terrainId = AddTerrain( &map );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    REQUIRE( files.chunks.size() == 1u ); // The terrain's footprint is centred on (0, 0), like the mesh and patch.
    std::string all;
    for ( const auto &entry : files.chunks ) { all += entry.second; }
    INFO( all );
    CHECK( Contains( all, "[ 64.0, 64.0, 8.0 ]" ) );
    CHECK( Contains( all, "material = \"materials/metal/pipe.cymat\"\n" ) );
    CHECK( Contains( all, "smoothing = 3u\n" ) );
    CHECK( Contains( all, "uv = [ [ 0.0, 0.0 ], [ 1.0, 0.0 ], [ 1.0, 1.0 ], [ 0.0, 1.0 ] ]" ) );
    CHECK( Contains( all, "colors = [ \"#ff000080\", \"#ffffff\", \"#ffffff\", \"#ffffff\" ]" ) );
    CHECK( Contains( all, "edges = [ { vertices = [ 0, 1 ] hard = true crease = 0.5 } ]" ) );
    CHECK( Contains( all, "info = { bounds = [ 0.0, 0.0, 0.0, 64.0, 64.0, 8.0 ] vertices = 4 faces = 1 }" ) );
    CHECK( Contains( all, "basis = \"quadratic\"" ) );
    CHECK( Contains( all, "material = \"materials/concrete/arch.cymat\"" ) );
    CHECK( Contains( all, "cells = [ 8, 8 ]" ) );
    CHECK( Contains( all, "holes = [ [ 2, 3 ] ]" ) );
    CHECK( Contains( all, "heights = [\n" ) );
    RequireStable( files );

    map_document_t loaded{};
    REQUIRE( Load( &loaded, files.root, files.chunks ) == map_status_t::OK );
    CHECK( geo::GeometryDocument_FindMesh( &loaded.geometry, geo::geometry_source_id_t{ meshId } ) != nullptr );
    CHECK( geo::GeometryDocument_FindPatch( &loaded.geometry, geo::geometry_source_id_t{ patchId } ) != nullptr );
    const geo::heightfield_t *pTerrain = geo::GeometryDocument_FindHeightField( &loaded.geometry, geo::geometry_source_id_t{ terrainId } );
    REQUIRE( pTerrain != nullptr );
    CHECK( pTerrain->holes.pData[3u * 8u + 2u] == 1u );
    CHECK( pTerrain->heights.pData[4] == 16.0 );
}

TEST_CASE( "Brush entities hold their brushes, which move and go with them", "[map][document][geometry]" )
{
    map_document_t map{};
    CreateMap( &map );
    const u64 brushId = AddBox( &map, { 20000.0, 0.0, 0.0 } );
    u64 triggerId = 0u;
    REQUIRE( MapDocument_AddEntity( &map, StringView_FromCString( "default" ), StringView_FromCString( "trigger_once" ), { 0.0, 0.0, 0.0 },
                                    &triggerId ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( &map, brushId, triggerId ) == map_status_t::OK );
    CHECK( MapDocument_SetGeometryOwner( &map, brushId, 99999u ) == map_status_t::UNKNOWN_OBJECT );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    REQUIRE( files.chunks.size() == 1u );
    const std::string &chunk = files.chunks["default/x0_y0.cymapchunk"];
    INFO( chunk );
    // The brush sits inside the entity, not in the chunk's world list.
    CHECK( chunk.find( "class = \"trigger_once\"" ) < chunk.find( "brushes = [" ) );
    CHECK_FALSE( Contains( chunk, "\n    brushes = [" ) );
    CHECK( Contains( chunk, "info = { bounds = [ 19968.0, -32.0, -32.0, 20032.0, 32.0, 32.0 ] }" ) );
    RequireStable( files );

    // The entity moves chunk and its brush follows.
    REQUIRE( MapDocument_SetEntityOrigin( &map, triggerId, { 9000.0, 0.0, 0.0 } ) == map_status_t::OK );
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    CHECK( files.chunks.count( "default/x1_y0.cymapchunk" ) == 1u );
    CHECK( files.removed == std::vector<std::string>{ "default/x0_y0.cymapchunk" } );

    // Removing the entity removes its brush; the IDs are not issued again.
    REQUIRE( MapDocument_RemoveObject( &map, triggerId ) == map_status_t::OK );
    CHECK( geo::GeometryDocument_FindBrush( &map.geometry, geo::geometry_source_id_t{ brushId } ) == nullptr );
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    CHECK( files.chunks.empty() );
    CHECK( MapDocument_AllocateId( &map ) > triggerId );
}

TEST_CASE( "Owned geometry with lower and interleaved IDs follows each owner through save and reload", "[map][document][geometry][ownership][save]" )
{
    map_document_t map{};
    CreateMap( &map );
    REQUIRE( MapDocument_AddLayer( &map, StringView_FromCString( "gameplay" ), StringView_FromCString( "Gameplay" ) ) == map_status_t::OK );
    const u64 earlyA = AddBox( &map, { 20000, 0, 0 } );
    const u64 earlyB = AddBox( &map, { -20000, 0, 0 } );
    u64 firstOwner{};
    REQUIRE( MapDocument_AddEntity( &map, StringView_FromCString( "default" ), StringView_FromCString( "func_detail" ), { 9000, 10000, 64 }, &firstOwner ) == map_status_t::OK );
    const u64 middle = AddBox( &map, { 20000, 20000, 0 } );
    const u64 mesh = AddMesh( &map );
    u64 secondOwner{};
    REQUIRE( MapDocument_AddEntity( &map, StringView_FromCString( "gameplay" ), StringView_FromCString( "func_detail" ), { -9000, -100, 32 }, &secondOwner ) == map_status_t::OK );
    const u64 patch = AddPatch( &map );
    REQUIRE( earlyA < earlyB ); REQUIRE( earlyB < firstOwner );
    REQUIRE( firstOwner < middle ); REQUIRE( middle < mesh ); REQUIRE( mesh < secondOwner ); REQUIRE( secondOwner < patch );
    for ( const u64 id : { earlyA, earlyB, patch } ) {
        // A stale geometry layer and distant geometry coordinates must not
        // override the entity's authoritative layer and cell placement.
        REQUIRE( MapDocument_SetGeometryLayer( &map, id, StringView_FromCString( "gameplay" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( &map, id, firstOwner ) == map_status_t::OK );
    }
    for ( const u64 id : { middle, mesh } ) {
        REQUIRE( MapDocument_SetGeometryLayer( &map, id, StringView_FromCString( "default" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( &map, id, secondOwner ) == map_status_t::OK );
    }
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    REQUIRE( files.chunks.size() == 2u );
    CHECK( files.chunks.count( "default/x1_y1.cymapchunk" ) == 1u );
    CHECK( files.chunks.count( "gameplay/x-2_y-1.cymapchunk" ) == 1u );
    RequireStable( files );
    map_document_t loaded{};
    REQUIRE( Load( &loaded, files.root, files.chunks ) == map_status_t::OK );
    REQUIRE( loaded.geometry.brushes.nCount == 3u );
    REQUIRE( loaded.geometry.meshes.nCount == 1u );
    REQUIRE( loaded.geometry.patches.nCount == 1u );
    const auto requirePlacement = [&]( u64 id, u64 owner, const char *pLayer, const char *pPath ) {
        map_chunk_t *chunk{};
        REQUIRE( MapDocument_FindObject( &loaded, id, &chunk ) != nullptr );
        REQUIRE( chunk != nullptr );
        CHECK( std::string( chunk->layer ) == pLayer );
        CHECK( std::string( chunk->sourcePath ) == pPath );
        bool found = false;
        for ( usize i = 0u; i < loaded.geometryRecords.nCount; ++i ) {
            const auto &record = loaded.geometryRecords.pData[i];
            if ( record.id != id ) { continue; }
            found = true; CHECK( record.owner == owner );
            REQUIRE( record.iLayer < loaded.layers.nCount );
            CHECK( std::string( loaded.layers.pData[record.iLayer].id ) == pLayer );
        }
        CHECK( found );
    };
    for ( const u64 id : { earlyA, earlyB, patch } ) { requirePlacement( id, firstOwner, "default", "default/x1_y1.cymapchunk" ); }
    for ( const u64 id : { middle, mesh } ) { requirePlacement( id, secondOwner, "gameplay", "gameplay/x-2_y-1.cymapchunk" ); }
    // Moving one owner must move all of its geometry without affecting the
    // other owner's lower-ID brush and mesh or introducing world records.
    REQUIRE( MapDocument_SetEntityOrigin( &loaded, firstOwner, { -9000, 9000, 64 } ) == map_status_t::OK );
    REQUIRE( Save( &loaded, files ) == map_status_t::OK );
    REQUIRE( files.chunks.size() == 2u );
    CHECK( files.removed == std::vector<std::string>{ "default/x1_y1.cymapchunk" } );
    for ( const u64 id : { earlyA, earlyB, patch } ) { requirePlacement( id, firstOwner, "default", "default/x-2_y1.cymapchunk" ); }
    for ( const u64 id : { middle, mesh } ) { requirePlacement( id, secondOwner, "gameplay", "gameplay/x-2_y-1.cymapchunk" ); }
    RequireStable( files );
}

TEST_CASE( "Owned geometry save allocation failures before the sink preserve the map and release the plan", "[map][document][geometry][ownership][save][oom]" )
{
    save_allocation_audit_t audit;
    const auto createOwned = [&]( map_document_t &map ) {
        REQUIRE( MapDocument_Create( &map, &audit.allocator, { StringView_FromCString( "Save allocation audit" ), StringView_FromCString( "reap" ), {} } ) == map_status_t::OK );
        const u64 first = AddBox( &map, { 20000, 0, 0 } );
        const u64 second = AddBox( &map, { -20000, 0, 0 } );
        const u64 mesh = AddMesh( &map );
        u64 owner{};
        REQUIRE( MapDocument_AddEntity( &map, StringView_FromCString( "default" ), StringView_FromCString( "func_detail" ), { 0, 0, 0 }, &owner ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( &map, first, owner ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( &map, second, owner ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( &map, mesh, owner ) == map_status_t::OK );
    };
    usize preparationAllocations{};
    {
        map_document_t map{}; createOwned( map );
        saved_files_t files; files.pAllocationCalls = &audit.calls;
        audit.calls = 0u;
        REQUIRE( Save( &map, files ) == map_status_t::OK );
        preparationAllocations = files.allocationsAtFirstSink;
    }
    REQUIRE( audit.bytes == 0u ); REQUIRE( preparationAllocations > 16u );
    // The file writer refreshes its in-memory chunks after publishing. This
    // sweep deliberately stops at the first sink call, where failure must
    // still guarantee that no external file has been written.
    for ( usize failure = 0u; failure < preparationAllocations; ++failure ) {
        CAPTURE( failure, preparationAllocations );
        {
            map_document_t map{}; createOwned( map );
            const auto revision = map.geometry.revision, nextId = map.nextId;
            const usize baseline = audit.bytes;
            saved_files_t files;
            audit.calls = 0u; audit.failAt = failure;
            CHECK( Save( &map, files ) == map_status_t::OUT_OF_MEMORY );
            audit.failAt = CY_USIZE_MAX;
            CHECK( files.nCalls == 0u ); CHECK( files.root.empty() ); CHECK( files.chunks.empty() );
            CHECK( audit.bytes == baseline );
            CHECK( map.geometry.revision == revision ); CHECK( map.nextId == nextId );
            CHECK( map.geometry.brushes.nCount == 2u ); CHECK( map.geometry.meshes.nCount == 1u ); REQUIRE( map.geometryRecords.nCount == 3u );
            CHECK( map.geometryRecords.pData[0].owner == map.geometryRecords.pData[1].owner );
            CHECK( map.geometryRecords.pData[0].owner == map.geometryRecords.pData[2].owner );
            CHECK( map.geometryRecords.pData[0].owner != 0u );
        }
        CHECK( audit.bytes == 0u );
    }
}

TEST_CASE( "Saving a loaded map without changes reproduces the same bytes", "[map][document]" )
{
    saved_files_t first;
    {
        map_document_t map{};
        CreateMap( &map );
        REQUIRE( MapDocument_AddLayer( &map, StringView_FromCString( "gameplay" ), StringView_FromCString( "Gameplay" ) ) == map_status_t::OK );
        u64 id = 0u;
        for ( f64 x : { -9000.0, 0.0, 100.0, 20000.0 } ) {
            REQUIRE( MapDocument_AddEntity( &map, StringView_FromCString( "gameplay" ), StringView_FromCString( "light" ), { x, 5.0, 0.0 }, &id ) ==
                     map_status_t::OK );
        }
        const u64 brushId = AddBox( &map, { 64.0, 64.0, 64.0 } );
        SetFaceMaterial( &map, brushId, 2u, "materials/metal/wall_panel02.cymat" );
        ( void )AddMesh( &map );
        ( void )AddPatch( &map );
        ( void )AddTerrain( &map );
        REQUIRE( Save( &map, first ) == map_status_t::OK );
    }
    RequireStable( first );
}

TEST_CASE( "Moving an entity across a cell border moves it to another chunk", "[map][document]" )
{
    map_document_t map{};
    CreateMap( &map );
    u64 id = 0u;
    REQUIRE( MapDocument_AddEntity( &map, StringView_FromCString( "default" ), StringView_FromCString( "light" ), { 10.0, 10.0, 0.0 }, &id ) ==
             map_status_t::OK );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    REQUIRE( files.chunks.count( "default/x0_y0.cymapchunk" ) == 1u );
    REQUIRE( MapDocument_SetEntityOrigin( &map, id, { 9000.0, 10.0, 0.0 } ) == map_status_t::OK );
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    CHECK( files.chunks.count( "default/x0_y0.cymapchunk" ) == 0u );
    CHECK( files.chunks.count( "default/x1_y0.cymapchunk" ) == 1u );
    CHECK( files.removed == std::vector<std::string>{ "default/x0_y0.cymapchunk" } );
}

TEST_CASE( "Removing the last object in a chunk deletes the chunk file and never frees its ID", "[map][document]" )
{
    map_document_t map{};
    CreateMap( &map );
    u64 id = 0u;
    REQUIRE( MapDocument_AddEntity( &map, StringView_FromCString( "default" ), StringView_FromCString( "light" ), { 1.0, 1.0, 1.0 }, &id ) ==
             map_status_t::OK );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    REQUIRE( MapDocument_RemoveObject( &map, id ) == map_status_t::OK );
    CHECK( MapDocument_RemoveObject( &map, id ) == map_status_t::UNKNOWN_OBJECT );
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    CHECK( files.chunks.empty() );
    CHECK( files.removed == std::vector<std::string>{ "default/x0_y0.cymapchunk" } );
    CHECK( Contains( files.root, "next_id = 2u" ) );
    u64 next = 0u;
    REQUIRE( MapDocument_AddEntity( &map, StringView_FromCString( "default" ), StringView_FromCString( "light" ), { 1.0, 1.0, 1.0 }, &next ) ==
             map_status_t::OK );
    CHECK( next == 2u );
}

TEST_CASE( "Removing a world brush removes it from the file", "[map][document][geometry]" )
{
    map_document_t map{};
    CreateMap( &map );
    const u64 keep = AddBox( &map, { 0.0, 0.0, 0.0 } );
    const u64 drop = AddBox( &map, { 100.0, 0.0, 0.0 } );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    map_document_t loaded{};
    REQUIRE( Load( &loaded, files.root, files.chunks ) == map_status_t::OK );
    REQUIRE( MapDocument_RemoveObject( &loaded, drop ) == map_status_t::OK );
    REQUIRE( Save( &loaded, files ) == map_status_t::OK );
    const std::string &chunk = files.chunks["default/x0_y0.cymapchunk"];
    CHECK( Contains( chunk, ( std::string( "id = " ) + std::to_string( keep ) + "u" ).c_str() ) );
    CHECK_FALSE( Contains( chunk, ( std::string( "id = " ) + std::to_string( drop ) + "u\n" ).c_str() ) );
    CHECK( Occurrences( chunk, "plane = [" ) == 6u );
}

// ---------------------------------------------------------------------------
// Hand editing
// ---------------------------------------------------------------------------

TEST_CASE( "Hand-added objects and parts without IDs get fresh ones", "[map][document][hand]" )
{
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] = Chunk(
        "default", "[ 0, 0 ]",
        "    entities = [ { class = \"light\" origin = [ 8.0, 8.0, 8.0 ] }, { id = 0u class = \"light\" origin = [ 9.0, 9.0, 9.0 ] } ]\n"
        "    brushes = [ { " + BoxFaces( 0, 0, 0, 64, 64, 64, "materials/dev/dev_grid.cymat" ) + " } ]\n"
        "    meshes = [ { id = 90u vertices = [ [ 0, 0, 0 ], [ 16, 0, 0 ], [ 0, 16, 0 ] ] vertex_ids = [ 91u ] faces = [ { vertices = [ 0, 1, 2 ] } ] } ]\n" );
    map_document_t map{};
    REQUIRE( Load( &map, Root( "100u" ), chunks ) == map_status_t::OK );
    CHECK_FALSE( map.bReadOnly );
    CHECK( map.bIdsAssigned );
    CHECK( CountProblems( map, map_problem_code_t::ID_ASSIGNED ) == 4u ); // Two entities, the brush, the mesh.
    CHECK_FALSE( HasProblem( map, map_problem_code_t::OBJECT_UNREADABLE ) );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    const std::string &chunk = files.chunks["default/x0_y0.cymapchunk"];
    INFO( chunk );
    // Entities 100 and 101, the brush 102 with faces 103-108, then mesh parts.
    CHECK( Contains( chunk, "{ id = 100u class = \"light\"" ) );
    CHECK( Contains( chunk, "{ id = 101u class = \"light\"" ) );
    CHECK( Contains( chunk, "id = 102u" ) );
    CHECK( Contains( chunk, "{ id = 103u plane = [ 1.0, 0.0, 0.0, 64.0 ] material = \"materials/dev/dev_grid.cymat\"" ) );
    CHECK( Contains( chunk, "vertex_ids = [ 91u, 109u, 110u ]" ) );
    CHECK( Contains( files.root, "next_id = 112u" ) );
    RequireStable( files );
}

TEST_CASE( "Hand-written files with comments, quoted keys, and odd order are canonicalised", "[map][document][hand]" )
{
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] =
        "@cykv 1\n@schema \"cypher.map_chunk\" 10\n"
        "// A designer's hand edit.\n"
        "{\n"
        "    \"entities\" = [ { \"origin\" = [ 1, 2, 3 ] \"class\" = \"light\" \"id\" = 7 } ]  /* light */\n"
        "    \"cell\" = [ 0, 0 ]\n"
        "    \"layer\" = \"default\"\n"
        "    \"map_id\" = \"4c1f0a52-7a8e-4f3b-9d61-0b2d3e4f5a6b\"\n"
        "    info = { entities = 99 bounds = [ 0, 0, 0, 1, 1, 1 ] }\n"
        "}\n";
    map_document_t map{};
    REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
    CHECK( map.problems.nCount == 0u );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    const std::string &chunk = files.chunks["default/x0_y0.cymapchunk"];
    INFO( chunk );
    CHECK( chunk.find( "map_id" ) < chunk.find( "layer" ) );
    CHECK( chunk.find( "cell" ) < chunk.find( "entities" ) );
    // `info` is recomputed, never taken from the file.
    CHECK( Contains( chunk, "info = { entities = 1 bounds = [ 1.0, 2.0, 3.0, 1.0, 2.0, 3.0 ] }" ) );
    CHECK( Contains( chunk, "entities = [ { id = 7u class = \"light\" origin = [ 1.0, 2.0, 3.0 ] } ]" ) );
    CHECK_FALSE( Contains( chunk, "//" ) );
}

TEST_CASE( "A hand-written plane with a non-unit normal is normalised", "[map][document][hand]" )
{
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] = Chunk(
        "default", "[ 0, 0 ]",
        "    brushes = [ { id = 10u faces = [\n"
        "      { id = 11u plane = [ 2, 0, 0, 128 ] uv_u = [ 0, 1, 0 ] uv_v = [ 0, 0, -1 ] },\n"
        "      { id = 12u plane = [ -1, 0, 0, 0 ] uv_u = [ 0, 1, 0 ] uv_v = [ 0, 0, -1 ] },\n"
        "      { id = 13u plane = [ 0, 1, 0, 64 ] uv_u = [ 1, 0, 0 ] uv_v = [ 0, 0, -1 ] },\n"
        "      { id = 14u plane = [ 0, -1, 0, 0 ] uv_u = [ 1, 0, 0 ] uv_v = [ 0, 0, -1 ] },\n"
        "      { id = 15u plane = [ 0, 0, 1, 64 ] uv_u = [ 1, 0, 0 ] uv_v = [ 0, -1, 0 ] },\n"
        "      { id = 16u plane = [ 0, 0, -1, 0 ] uv_u = [ 1, 0, 0 ] uv_v = [ 0, 1, 0 ] }\n"
        "    ] } ]\n" );
    map_document_t map{};
    REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    CHECK( Contains( files.chunks["default/x0_y0.cymapchunk"], "{ id = 11u plane = [ 1.0, 0.0, 0.0, 64.0 ]" ) );
}

TEST_CASE( "Unknown members survive in the root, chunks, entities, records, and faces", "[map][document]" )
{
    const std::string root = Root( "100u", "    tool_state = { opened_by = \"mason 2\" }\n    visgroups = [ { id = 50u name = \"Lights\" future = 1 } ]\n" );
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] = Chunk(
        "default", "[ 0, 0 ]",
        "    plugin_data = { weather = \"rain\" }\n"
        "    entities = [ { future_field = [ 1, 2 ] class = \"light\" id = 7u origin = [ 1.0, 2.0, 3.0 ]\n"
        "        outputs = [ { times = 1 input = \"turn_on\" target = \"lamp\" output = \"on_trigger\" note = \"keep\" } ] } ]\n"
        "    brushes = [ { id = 20u name = \"pillar\" legacy_flags = 3 "
        + std::string( "faces = [\n"
                       "      { id = 21u plane = [ 1, 0, 0, 64 ] uv_u = [ 0, 1, 0 ] uv_v = [ 0, 0, -1 ] lightmap_scale = 8 },\n"
                       "      { id = 22u plane = [ -1, 0, 0, 0 ] uv_u = [ 0, 1, 0 ] uv_v = [ 0, 0, -1 ] },\n"
                       "      { id = 23u plane = [ 0, 1, 0, 64 ] uv_u = [ 1, 0, 0 ] uv_v = [ 0, 0, -1 ] },\n"
                       "      { id = 24u plane = [ 0, -1, 0, 0 ] uv_u = [ 1, 0, 0 ] uv_v = [ 0, 0, -1 ] },\n"
                       "      { id = 25u plane = [ 0, 0, 1, 64 ] uv_u = [ 1, 0, 0 ] uv_v = [ 0, -1, 0 ] },\n"
                       "      { id = 26u plane = [ 0, 0, -1, 0 ] uv_u = [ 1, 0, 0 ] uv_v = [ 0, 1, 0 ] }\n"
                       "    ] } ]\n" ) );
    map_document_t map{};
    REQUIRE( Load( &map, root, chunks ) == map_status_t::OK );
    CHECK( map.problems.nCount == 0u );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    const std::string &chunk = files.chunks["default/x0_y0.cymapchunk"];
    INFO( chunk );
    CHECK( Contains( files.root, "tool_state" ) );
    CHECK( Contains( files.root, "future = 1" ) );
    CHECK( Contains( chunk, "plugin_data = { weather = \"rain\" }" ) );
    CHECK( Contains( chunk, "future_field = [ 1, 2 ]" ) );
    CHECK( Contains( chunk, "note = \"keep\"" ) );
    CHECK( Contains( chunk, "legacy_flags = 3" ) );
    CHECK( Contains( chunk, "lightmap_scale = 8.0 }" ) );
    CHECK( Contains( chunk, "name = \"pillar\"" ) );
    // Canonical order: known members first, in the specified order.
    CHECK( chunk.find( "id = 7u" ) < chunk.find( "class = \"light\"" ) );
    CHECK( chunk.find( "class = \"light\"" ) < chunk.find( "future_field" ) );
    CHECK( chunk.find( "output = \"on_trigger\"" ) < chunk.find( "target = \"lamp\"" ) );
    CHECK( chunk.find( "entities" ) < chunk.find( "plugin_data" ) );
    CHECK( chunk.find( "name = \"pillar\"" ) < chunk.find( "faces = [" ) );
    CHECK( chunk.find( "faces = [" ) < chunk.find( "legacy_flags" ) );
    CHECK( files.root.find( "visgroups" ) < files.root.find( "tool_state" ) );
    RequireStable( files );
}

TEST_CASE( "Face lightmap scale and smoothing are kept per face in a fixed order", "[map][document][geometry]" )
{
    std::string faces = BoxFaces( 0, 0, 0, 64, 64, 64, "materials/dev/dev_grid.cymat" );
    // Hand-typed in the wrong order and spelling on the first face.
    faces.replace( faces.find( "uv_size" ), 7u, "note = \"seam here\" smoothing = 3 lightmap_scale = 8 uv_size" );
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] = Chunk( "default", "[ 0, 0 ]", "    brushes = [ { id = 10u " + faces + " } ]\n" );
    map_document_t map{};
    REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    const std::string &chunk = files.chunks["default/x0_y0.cymapchunk"];
    INFO( chunk );
    // That face no longer fits on a line, so the brush's faces all spread.
    CHECK( chunk.find( "uv_size = [ 128.0, 128.0 ]\n" ) < chunk.find( "lightmap_scale = 8.0\n" ) );
    CHECK( chunk.find( "lightmap_scale = 8.0\n" ) < chunk.find( "smoothing = 3u\n" ) );
    CHECK( chunk.find( "smoothing = 3u\n" ) < chunk.find( "note = \"seam here\"\n" ) );
    RequireStable( files );
}

TEST_CASE( "Outputs are written whole and in their firing order", "[map][document]" )
{
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] = Chunk( "default", "[ 0, 0 ]",
                                                "    entities = [ { id = 7u class = \"trigger_once\" origin = [ 0, 0, 0 ] angles = [ 0, 0, 0 ] scale = 1\n"
                                                "        outputs = [ { output = \"on_trigger\" target = \"zeta\" input = \"start\" },\n"
                                                "                    { output = \"on_trigger\" target = \"alpha\" input = \"stop\" delay = 2.5 times = 1 } ] } ]\n" );
    map_document_t map{};
    REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    const std::string &chunk = files.chunks["default/x0_y0.cymapchunk"];
    INFO( chunk );
    CHECK( Contains( chunk, "{ output = \"on_trigger\" target = \"zeta\" input = \"start\" parameter = \"\" delay = 0.0 times = -1 }" ) );
    CHECK( chunk.find( "zeta" ) < chunk.find( "alpha" ) );
    CHECK_FALSE( Contains( chunk, "angles" ) );
    CHECK_FALSE( Contains( chunk, "scale" ) );
}

// ---------------------------------------------------------------------------
// Damage
// ---------------------------------------------------------------------------

TEST_CASE( "Unreadable objects are reported and written back unchanged", "[map][document][damage]" )
{
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] = Chunk( "default", "[ 0, 0 ]",
                                                "    entities = [ 42, { id = 3u class = \"light\" origin = [ 1.0, 1.0, 1.0 ] } ]\n"
                                                "    brushes = [ { id = 30u faces = [ { id = 31u plane = [ 0, 0, 0, 5 ] uv_u = [ 1, 0, 0 ] uv_v = [ 0, 1, 0 ] } ] } ]\n" );
    map_document_t map{};
    REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
    CHECK( CountProblems( map, map_problem_code_t::OBJECT_UNREADABLE ) == 2u );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    const std::string &chunk = files.chunks["default/x0_y0.cymapchunk"];
    INFO( chunk );
    CHECK( Contains( chunk, "42" ) );
    CHECK( chunk.find( "light" ) < chunk.find( "42" ) ); // Readable first, unreadable after.
    CHECK( Contains( chunk, "{ id = 30u faces = [ { id = 31u plane = [ 0, 0, 0, 5 ]" ) );
    RequireStable( files, true );
}

TEST_CASE( "Chunks from another map or that do not parse are never touched", "[map][document][damage]" )
{
    std::map<std::string, std::string> chunks;
    chunks["default/x5_y5.cymapchunk"] = Chunk( "default", "[ 5, 5 ]", "    entities = [ { id = 9u class = \"x\" } ]\n",
                                                "00000000-0000-4000-8000-000000000001" );
    chunks["default/x6_y6.cymapchunk"] = "not cykv at all {";
    chunks["default/x7_y7.cymapchunk"] = Chunk( "default", "[ 7, 7 ]", "    brushes = 5\n" );
    chunks["default/x0_y0.cymapchunk"] = Chunk( "default", "[ 0, 0 ]", "    entities = [ { id = 3u class = \"light\" origin = [ 1.0, 1.0, 1.0 ] } ]\n" );
    map_document_t map{};
    REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
    CHECK( HasProblem( map, map_problem_code_t::CHUNK_FOREIGN_MAP ) );
    CHECK( CountProblems( map, map_problem_code_t::CHUNK_UNREADABLE ) == 2u );
    CHECK_FALSE( map.bReadOnly );
    CHECK( MapDocument_FindObject( &map, 9u, nullptr ) == nullptr );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    CHECK( files.written == std::vector<std::string>{ "default/x0_y0.cymapchunk" } );
    CHECK( files.removed.empty() );
    // Moving an object into a damaged chunk's path refuses the whole save
    // before anything is written.
    REQUIRE( MapDocument_SetEntityOrigin( &map, 3u, { 6.0 * 8192.0 + 1.0, 6.0 * 8192.0 + 1.0, 0.0 } ) == map_status_t::OK );
    CHECK( Save( &map, files ) == map_status_t::SAVE_BLOCKED );
    CHECK( files.nCalls == 0u );
}

TEST_CASE( "Duplicate IDs open the map read-only, and reassigning fixes them", "[map][document][damage]" )
{
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] = Chunk( "default", "[ 0, 0 ]", "    entities = [ { id = 5u class = \"a\" origin = [ 1.0, 1.0, 1.0 ] } ]\n" );
    chunks["default/x1_y0.cymapchunk"] = Chunk( "default", "[ 1, 0 ]", "    entities = [ { id = 5u class = \"b\" origin = [ 8200.0, 1.0, 1.0 ] } ]\n" );
    {
        map_document_t map{};
        REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
        CHECK( map.bReadOnly );
        CHECK( CountProblems( map, map_problem_code_t::DUPLICATE_ID ) == 2u ); // Both locations.
        saved_files_t files;
        CHECK( Save( &map, files ) == map_status_t::READ_ONLY );
        CHECK( files.nCalls == 0u );
    }
    map_document_t map{};
    REQUIRE( Load( &map, Root(), chunks, MAP_LOAD_FLAG_REASSIGN_DUPLICATES ) == map_status_t::OK );
    CHECK_FALSE( map.bReadOnly );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    CHECK( Contains( files.chunks["default/x0_y0.cymapchunk"], "id = 5u class = \"a\"" ) ); // First in path order keeps its ID.
    CHECK( Contains( files.chunks["default/x1_y0.cymapchunk"], "id = 100u class = \"b\"" ) );
}

TEST_CASE( "An entity ID equal to a brush face ID is a duplicate", "[map][document][damage]" )
{
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] = Chunk( "default", "[ 0, 0 ]",
                                                "    entities = [ { id = 21u class = \"a\" origin = [ 1.0, 1.0, 1.0 ] } ]\n"
                                                "    brushes = [ { id = 20u " + BoxFaces( 0, 0, 0, 8, 8, 8, "m.cymat" ) + " } ]\n" );
    // Give the first face ID 21 by hand.
    std::string &text = chunks["default/x0_y0.cymapchunk"];
    text.replace( text.find( "{ plane" ), 7u, "{ id = 21u plane" );
    map_document_t map{};
    REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
    CHECK( map.bReadOnly );
    CHECK( HasProblem( map, map_problem_code_t::DUPLICATE_ID ) );
}

TEST_CASE( "An ID at or above next_id raises next_id", "[map][document][damage]" )
{
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] = Chunk( "default", "[ 0, 0 ]", "    entities = [ { id = 10u class = \"a\" origin = [ 1.0, 1.0, 1.0 ] } ]\n" );
    map_document_t map{};
    REQUIRE( Load( &map, Root( "2u" ), chunks ) == map_status_t::OK );
    CHECK( HasProblem( map, map_problem_code_t::ID_ABOVE_NEXT_ID ) );
    CHECK( map.nextId == 11u );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    CHECK( Contains( files.root, "next_id = 11u" ) );
}

TEST_CASE( "Unknown members from two files for one chunk merge only when equal", "[map][document][damage]" )
{
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] = Chunk( "default", "[ 0, 0 ]", "    plugin = { a = 1 }\n    entities = [ { id = 4u class = \"a\" origin = [ 1.0, 1.0, 1.0 ] } ]\n" );
    chunks["default/copy.cymapchunk"] = Chunk( "default", "[ 0, 0 ]", "    plugin = { a = 1 }\n    entities = [ { id = 5u class = \"b\" origin = [ 2.0, 2.0, 2.0 ] } ]\n" );
    {
        map_document_t map{};
        REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
        CHECK( HasProblem( map, map_problem_code_t::CHUNK_PATH_MISMATCH ) );
        saved_files_t files;
        REQUIRE( Save( &map, files ) == map_status_t::OK );
        CHECK( files.written == std::vector<std::string>{ "default/x0_y0.cymapchunk" } );
        CHECK( files.removed == std::vector<std::string>{ "default/copy.cymapchunk" } );
        CHECK( Occurrences( files.chunks["default/x0_y0.cymapchunk"], "plugin" ) == 1u );
    }
    chunks["default/copy.cymapchunk"] = Chunk( "default", "[ 0, 0 ]", "    plugin = { a = 2 }\n" );
    map_document_t map{};
    REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
    saved_files_t files;
    CHECK( Save( &map, files ) == map_status_t::SAVE_BLOCKED );
    CHECK( files.nCalls == 0u );
}

TEST_CASE( "An invalid root does not open and names the member", "[map][document][damage]" )
{
    const std::string root = "@cykv 1\n@schema \"cypher.map\" 10\n{ map_id = \"4c1f0a52-7a8e-4f3b-9d61-0b2d3e4f5a6b\" name = \"x\" game = \"reap\" next_id = 1u }\n";
    map_document_t map{};
    REQUIRE( Load( &map, root, {} ) == map_status_t::ROOT_INVALID );
    REQUIRE( map.problems.nCount == 1u );
    CHECK( std::string( map.problems.pData[0].member ) == "layers" );
    map_document_t wrong{};
    CHECK( Load( &wrong, "@cykv 1\n@schema \"cypher.map\" 3\n{ }\n", {} ) == map_status_t::ROOT_UNREADABLE );
    map_document_t garbage{};
    CHECK( Load( &garbage, "{", {} ) == map_status_t::ROOT_UNREADABLE );
    std::string visgroups = "    visgroups = [ ";
    for ( usize i = 0u; i <= MAP_VISGROUPS_MAX; ++i ) { visgroups += ( i == 0u ? "" : ", " ) + std::string( "{ id = " ) + std::to_string( 1000u + i ) + "u }"; }
    visgroups += " ]\n";
    map_document_t many{};
    CHECK( Load( &many, Root( "100000u", visgroups.c_str() ), {} ) == map_status_t::ROOT_INVALID );
}

TEST_CASE( "Optional root members fall back to defaults with a report", "[map][document][damage]" )
{
    map_document_t map{};
    REQUIRE( Load( &map, Root( "100u", "    cell_size = [ 100.0, 0.0, 0.0 ]\n    units_per_meter = -1.0\n" ), {} ) == map_status_t::OK );
    CHECK( CountProblems( map, map_problem_code_t::ROOT_MEMBER_IGNORED ) == 2u );
    CHECK( map.grid.size[0] == 8192.0 );
}

// ---------------------------------------------------------------------------
// Placement of the other object kinds
// ---------------------------------------------------------------------------

TEST_CASE( "Groups, shapes, notes, and prefabs are placed by their rules", "[map][document]" )
{
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] = Chunk( "default", "[ 0, 0 ]",
                                                "    groups = [ { id = 20u name = \"Wave\" members = [ 5u, 4u ] } ]\n"
                                                "    prefabs = [ { id = 30u source = \"prefabs/door.cyprefab\" origin = [ 9000.0, 0.0, 0.0 ] } ]\n"
                                                "    shapes = [ { id = 40u type = \"polyline\" points = [ [ 16400, 0, 0 ], [ 16500, 10, 0 ] ] } ]\n"
                                                "    notes = [ { id = 50u position = [ -10, -10, 0 ] text = \"Needs cover\" } ]\n" );
    chunks["gameplay/x0_y0.cymapchunk"] = Chunk( "gameplay", "[ 0, 0 ]",
                                                 "    entities = [ { id = 4u class = \"a\" origin = [ 1.0, 1.0, 1.0 ] }, { id = 5u class = \"logic_relay\" } ]\n" );
    map_document_t map{};
    REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    for ( const auto &entry : files.chunks ) { INFO( entry.first << "\n" << entry.second ); }
    const std::string &global = files.chunks["gameplay/global.cymapchunk"];
    CHECK( Contains( global, "logic_relay" ) );
    CHECK( Contains( global, "members = [ 4u, 5u ]" ) ); // Sorted; first member's layer.
    CHECK( Contains( files.chunks["default/x1_y0.cymapchunk"], "prefabs" ) );
    CHECK( Contains( files.chunks["default/x2_y0.cymapchunk"], "info = { bounds = [ 16400.0, 0.0, 0.0, 16500.0, 10.0, 0.0 ] }" ) );
    CHECK( Contains( files.chunks["default/x-1_y-1.cymapchunk"], "text = \"Needs cover\"" ) );
    CHECK( files.chunks.count( "default/x0_y0.cymapchunk" ) == 0u );
    RequireStable( files );
}

TEST_CASE( "Foliage instances go to the chunk of their own position, sorted", "[map][document]" )
{
    std::map<std::string, std::string> chunks;
    chunks["default/x0_y0.cymapchunk"] = Chunk( "default", "[ 0, 0 ]",
                                                "    foliage = [ { model = \"models/foliage/pine.cymodel\" instances = [\n"
                                                "        [ 9000, 5, 0, 0, 10, 0, 1 ], [ 20, 5, 0, 0, 20, 0, 1.1 ], [ 10, 5, 0, 0, 30, 0, 0.9 ] ]\n"
                                                "        properties = { cast_shadows = true } } ]\n" );
    chunks["default/x1_y0.cymapchunk"] = Chunk( "default", "[ 1, 0 ]",
                                                "    foliage = [ { model = \"models/foliage/pine.cymodel\" instances = [ [ 8200, 1, 0, 0, 0, 0, 1 ] ]\n"
                                                "        properties = { cast_shadows = false } } ]\n" );
    map_document_t map{};
    REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
    CHECK( HasProblem( map, map_problem_code_t::FOLIAGE_PROPERTIES_DIFFER ) );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    const std::string &near = files.chunks["default/x0_y0.cymapchunk"];
    const std::string &far = files.chunks["default/x1_y0.cymapchunk"];
    INFO( near << "\n" << far );
    CHECK( near.find( "[ 10.0, 5.0, 0.0, 0.0, 30.0, 0.0, 0.9 ]" ) < near.find( "[ 20.0, 5.0, 0.0, 0.0, 20.0, 0.0, 1.1 ]" ) );
    CHECK( Contains( near, "info = { foliage = 2" ) );
    CHECK( Contains( far, "[ 8200.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 ]" ) );
    CHECK( Contains( far, "[ 9000.0, 5.0, 0.0, 0.0, 10.0, 0.0, 1.0 ]" ) );
    CHECK( Contains( far, "cast_shadows = true" ) ); // The first record's properties win.
}

TEST_CASE( "A chunk on an undeclared layer adopts it, and new layers keep root order", "[map][document]" )
{
    std::map<std::string, std::string> chunks;
    chunks["extra/global.cymapchunk"] = Chunk( "extra", "\"global\"", "    entities = [ { id = 4u class = \"a\" } ]\n" );
    map_document_t map{};
    REQUIRE( Load( &map, Root(), chunks ) == map_status_t::OK );
    CHECK( HasProblem( map, map_problem_code_t::CHUNK_LAYER_ADOPTED ) );
    REQUIRE( MapDocument_AddLayer( &map, StringView_FromCString( "detail" ), StringView_FromCString( "Detail" ) ) == map_status_t::OK );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    CHECK( files.root.find( "id = \"default\"" ) < files.root.find( "id = \"extra\"" ) );
    CHECK( files.root.find( "id = \"extra\"" ) < files.root.find( "id = \"detail\"" ) );
    CHECK( Contains( files.root, "{ id = \"detail\" name = \"Detail\" }" ) );
}

TEST_CASE( "A failing sink stops the save, and invalid arguments are refused", "[map][document]" )
{
    map_document_t map{};
    CreateMap( &map );
    u64 id = 0u;
    REQUIRE( MapDocument_AddEntity( &map, StringView_FromCString( "default" ), StringView_FromCString( "light" ), {}, &id ) == map_status_t::OK );
    saved_files_t files;
    files.iFailAt = 0u;
    CHECK( MapDocument_Save( &map, files.Sink() ) == map_status_t::SINK_FAILED );
    CHECK( files.root.empty() );

    map_document_t other{};
    map_create_desc_t desc{};
    desc.name = StringView_FromCString( "x" );
    desc.game = StringView_FromCString( "Not Stable" );
    CHECK( MapDocument_Create( &other, Allocator_GetSystem(), desc ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapDocument_AddEntity( &map, StringView_FromCString( "nope" ), StringView_FromCString( "light" ), {}, &id ) == map_status_t::UNKNOWN_LAYER );
    CHECK( MapDocument_AddLayer( &map, StringView_FromCString( "Bad Id" ), {} ) == map_status_t::INVALID_ARGUMENT );
    CHECK( MapDocument_SetEntityOrigin( &map, 999u, {} ) == map_status_t::UNKNOWN_OBJECT );
    const u64 terrain = AddTerrain( &map );
    CHECK( MapDocument_SetGeometryOwner( &map, terrain, id ) == map_status_t::INVALID_ARGUMENT );
    CHECK( std::string( MapDocument_StatusName( map_status_t::SAVE_BLOCKED ) ) == "SAVE_BLOCKED" );
    CHECK( std::string( MapDocument_ProblemName( map_problem_code_t::ID_ASSIGNED ) ) == "ID_ASSIGNED" );
}

TEST_CASE( "A chunk the reader would reject is refused on save", "[map][document][limits]" )
{
    // 9,000 box brushes (about 125 values each) in one cell pass the text
    // limit but not the value budget; saving refuses rather than writing a
    // chunk the reader would reject.
    map_document_t map{};
    CreateMap( &map );
    for ( usize i = 0u; i < 9000u; ++i ) { ( void )AddBox( &map, { static_cast<f64>( i % 95u ) * 80.0, static_cast<f64>( i / 95u ) * 80.0, 64.0 } ); }
    saved_files_t files;
    CHECK( Save( &map, files ) == map_status_t::LIMIT_EXCEEDED );
    CHECK( files.nCalls == 0u );
}

// ---------------------------------------------------------------------------
// The documented example map (CYMAP.md section 18)
// ---------------------------------------------------------------------------

namespace
{

// The example is written in the readable hand-edited form, IDs of faces and
// terrain tiles left out, and then saved by the real writer: the files in
// docs/formats/examples/cymap are exactly what Mason writes.
std::map<std::string, std::string> ExampleSources( std::string &rootOut )
{
    rootOut =
        "@cykv 1\n@schema \"cypher.map\" 10\n{\n"
        "    map_id = \"4c1f0a52-7a8e-4f3b-9d61-0b2d3e4f5a6b\"\n"
        "    name = \"Research Facility (example)\"\n"
        "    game = \"reap\"\n"
        "    game_version = 3u\n"
        "    next_id = 1u\n"
        "    description = \"The complete example map of CYMAP.md: one room, one wave, every object kind.\"\n"
        "    authors = [ \"Karlo\" ]\n"
        "    tags = [ \"waves\", \"indoor\", \"example\" ]\n"
        "    units_per_meter = 39.37\n"
        "    cell_size = [ 1024.0, 1024.0, 0.0 ]\n"
        "    layers = [\n"
        "        { id = \"structure\" name = \"Structure\" color = \"#6fa8dc\" },\n"
        "        { id = \"detail\" name = \"Detail\" },\n"
        "        { id = \"gameplay\" name = \"Gameplay\" },\n"
        "        { id = \"notes\" name = \"Designer notes\" editor_only = true }\n"
        "    ]\n"
        "    visgroups = [ { id = 10u name = \"Lights\" color = \"#ffd84a\" } ]\n"
        "    selection_sets = [ { id = 11u name = \"Room shell\" members = [ 1005u, 1004u, 1003u, 1002u, 1001u, 1000u ] } ]\n"
        "    cordons = [ { name = \"Room\" bounds = [ -512, -512, -32, 512, 512, 320 ] } ]\n"
        "    settings = {\n"
        "        map = { sky = \"materials/sky/dusk.cymat\" gravity = 800.0 ambient_color = \"#202028\" fog_color = \"#101820\" fog_start = 2048.0 fog_end = 12000.0 }\n"
        "        game = { wave_count = 12 wave_intermission = 8.0 difficulty = \"normal\" }\n"
        "    }\n"
        "}\n";
    std::map<std::string, std::string> chunks;
    chunks["structure/x0_y0.cymapchunk"] = Chunk(
        "structure", "[ 0, 0 ]",
        "    brushes = [\n"
        "        { id = 1000u name = \"floor\" " + BoxFaces( -512, -512, -32, 512, 512, 0, "materials/concrete/floor01.cymat" ) + " },\n"
        "        { id = 1001u name = \"ceiling\" " + BoxFaces( -512, -512, 288, 512, 512, 320, "materials/concrete/ceiling01.cymat" ) + " },\n"
        "        { id = 1002u name = \"north wall\" " + BoxFaces( -512, 496, 0, 512, 512, 288, "materials/concrete/wall03.cymat" ) + " },\n"
        "        { id = 1003u name = \"south wall\" " + BoxFaces( -512, -512, 0, 512, -496, 288, "materials/concrete/wall03.cymat" ) + " },\n"
        "        { id = 1004u name = \"east wall\" " + BoxFaces( 496, -496, 0, 512, 496, 288, "materials/concrete/wall03.cymat" ) + " },\n"
        "        { id = 1005u name = \"west wall\" " + BoxFaces( -512, -496, 0, -496, 496, 288, "materials/concrete/wall03.cymat" ) + " }\n"
        "    ]\n"
        "    terrains = [ { id = 1100u name = \"yard\" origin = [ 600, -128, 0 ] cell_size = 32 cells = [ 8, 8 ] tile_cells = 4\n"
        "        heights = [ [ 0, 0, 0, 0, 0, 0, 0, 0, 0 ], [ 0, 4, 4, 4, 4, 4, 4, 4, 0 ], [ 0, 4, 12, 12, 12, 12, 12, 4, 0 ],\n"
        "                    [ 0, 4, 12, 24, 24, 24, 12, 4, 0 ], [ 0, 4, 12, 24, 40, 24, 12, 4, 0 ], [ 0, 4, 12, 24, 24, 24, 12, 4, 0 ],\n"
        "                    [ 0, 4, 12, 12, 12, 12, 12, 4, 0 ], [ 0, 4, 4, 4, 4, 4, 4, 4, 0 ], [ 0, 0, 0, 0, 0, 0, 0, 0, 0 ] ]\n"
        "        holes = [ [ 7, 0 ] ]\n"
        "        paint = { base = \"materials/terrain/dirt.cymat\" layers = [ { material = \"materials/terrain/grass.cymat\" weights = [\n"
        "            [ 255, 255, 255, 255, 255, 255, 255, 255, 255 ], [ 255, 200, 200, 200, 200, 200, 200, 200, 255 ], [ 255, 200, 90, 90, 90, 90, 90, 200, 255 ],\n"
        "            [ 255, 200, 90, 0, 0, 0, 90, 200, 255 ], [ 255, 200, 90, 0, 0, 0, 90, 200, 255 ], [ 255, 200, 90, 0, 0, 0, 90, 200, 255 ],\n"
        "            [ 255, 200, 90, 90, 90, 90, 90, 200, 255 ], [ 255, 200, 200, 200, 200, 200, 200, 200, 255 ], [ 255, 255, 255, 255, 255, 255, 255, 255, 255 ] ] } ] }\n"
        "    } ]\n" );
    chunks["detail/x0_y0.cymapchunk"] = Chunk(
        "detail", "[ 0, 0 ]",
        "    meshes = [ { id = 1200u name = \"loading ramp\" vertices = [ [ 256, 256, 0 ], [ 384, 256, 0 ], [ 384, 384, 48 ], [ 256, 384, 48 ] ]\n"
        "        vertex_ids = [ 1201u, 1202u, 1203u, 1204u ]\n"
        "        faces = [ { id = 1205u vertices = [ 0, 1, 2, 3 ] material = \"materials/metal/grate01.cymat\" uv = [ [ 0, 0 ], [ 1, 0 ], [ 1, 1 ], [ 0, 1 ] ] } ]\n"
        "        edges = [ { vertices = [ 0, 1 ] hard = true } ]\n"
        "        modifiers = [ { type = \"mirror\" axis = \"x\" offset = 256.0 weld = true } ] } ]\n"
        "    patches = [ { id = 1300u name = \"doorway arch\" basis = \"quadratic\" columns = 3 rows = 3 material = \"materials/concrete/arch.cymat\"\n"
        "        controls = [ [ [ -64, 496, 128, 0, 0 ], [ 0, 496, 128, 0.5, 0 ], [ 64, 496, 128, 1, 0 ] ],\n"
        "                     [ [ -64, 496, 160, 0, 0.5 ], [ 0, 496, 192, 0.5, 0.5 ], [ 64, 496, 160, 1, 0.5 ] ],\n"
        "                     [ [ -64, 496, 192, 0, 1 ], [ 0, 496, 192, 0.5, 1 ], [ 64, 496, 192, 1, 1 ] ] ]\n"
        "        control_ids = [ 1301u, 1302u, 1303u, 1304u, 1305u, 1306u, 1307u, 1308u, 1309u ] } ]\n"
        "    foliage = [ { model = \"models/foliage/pine01.cymodel\" instances = [\n"
        "        [ 1100, 40, 0, 0, 132.5, 0, 1.1 ], [ 700, -60, 0, 0, 17, 0, 0.95 ], [ 760, 90, 0, 0, 260, 0, 1.0 ], [ 1180, -90, 0, 0, 75, 0, 1.2 ] ]\n"
        "        properties = { cast_shadows = true fade_distance = 4000.0 } } ]\n" );
    chunks["gameplay/x0_y0.cymapchunk"] = Chunk(
        "gameplay", "[ 0, 0 ]",
        "    entities = [\n"
        "        { id = 101u class = \"info_player_start\" name = \"spawn_a\" origin = [ -256, -256, 16 ] angles = [ 0, 45, 0 ]\n"
        "          properties = { team = \"red\" priority = 10 }\n"
        "          outputs = [ { output = \"on_spawn\" target = \"spawn_music\" input = \"trigger\" } ] },\n"
        "        { id = 102u class = \"info_player_start\" name = \"spawn_b\" origin = [ 256, -256, 16 ] angles = [ 0, 135, 0 ] properties = { team = \"blue\" priority = 10 } },\n"
        "        { id = 110u class = \"trigger_once\" name = \"wave1_trigger\" origin = [ 0, 0, 0 ]\n"
        "          properties = { start_disabled = false filter = \"players\" }\n"
        "          outputs = [ { output = \"on_trigger\" target = \"wave1\" input = \"start\" parameter = \"\" delay = 0.5 times = 1 } ]\n"
        "          brushes = [ { id = 111u " + BoxFaces( -64, -64, 0, 64, 64, 128, "materials/editor/trigger.cymat" ) + " } ] },\n"
        "        { id = 120u class = \"info_wave_start\" name = \"wave1\" origin = [ 0, 400, 32 ]\n"
        "          properties = { start_disabled = false wave = 1 spawn_group = \"wave1\" delay = 3.0 announce = \"sounds/announcer/wave1.cysnd\" }\n"
        "          outputs = [ { output = \"on_cleared\" target = \"wave2\" input = \"start\" delay = 8.0 times = 1 },\n"
        "                      { output = \"on_started\" target = \"arena_door\" input = \"close\" } ] },\n"
        "        { id = 130u class = \"info_enemy_spawn\" origin = [ -400, 400, 0 ] angles = [ 0, -90, 0 ]\n"
        "          properties = { start_disabled = true enemy = \"grunt\" spawn_group = \"wave1\" count = 4 flags = [ \"face_player\" ] } },\n"
        "        { id = 131u class = \"info_enemy_spawn\" origin = [ 400, 400, 0 ] angles = [ 0, -90, 0 ]\n"
        "          properties = { start_disabled = true enemy = \"stalker\" spawn_group = \"wave1\" count = 2 flags = [ \"face_player\", \"silent\" ] } },\n"
        "        { id = 140u class = \"light\" name = \"hall_light\" origin = [ 0, 0, 280 ] visgroups = [ 10u ]\n"
        "          properties = { start_disabled = false color = \"#ffd8a0\" intensity = 450.0 range = 768.0 cast_shadows = true } },\n"
        "        { id = 150u class = \"func_door\" name = \"arena_door\" origin = [ 0, 504, 64 ]\n"
        "          properties = { start_disabled = false move_dir = [ 0.0, 90.0, 0.0 ] speed = 200.0 lip = 8.0 wait = -1.0 open_sound = \"\" locked = false }\n"
        "          brushes = [ { id = 151u " + BoxFaces( -64, 488, 0, 64, 496, 128, "materials/metal/door02.cymat" ) + " } ] },\n"
        "        { id = 160u class = \"prop_static\" origin = [ -300, -200, 0 ] angles = [ 0, 35, 0 ]\n"
        "          properties = { model = \"models/props/crate_large.cymodel\" skin = 1 solid = \"vphysics\" fade_distance = 0.0 } },\n"
        "        { id = 170u class = \"logic_relay\" name = \"spawn_music\" properties = { start_disabled = false } comment = \"Plays the spawn sting.\" }\n"
        "    ]\n"
        "    shapes = [ { id = 180u name = \"patrol_route_a\" type = \"catmull_rom\" closed = true points = [ [ -400, 300, 16 ], [ 400, 300, 16 ], [ 400, -300, 16 ], [ -400, -300, 16 ] ] } ]\n"
        "    groups = [ { id = 190u name = \"Wave 1\" members = [ 131u, 130u, 120u, 110u ] } ]\n"
        "    prefabs = [ { id = 200u name = \"yard_gate\" source = \"prefabs/security_door.cyprefab\" origin = [ 600, 0, 0 ] angles = [ 0, 90, 0 ] overrides = { \"door.name\" = \"yard_door\" } } ]\n" );
    chunks["notes/x0_y0.cymapchunk"] = Chunk( "notes", "[ 0, 0 ]",
                                              "    notes = [ { id = 300u position = [ 0, 0, 200 ] text = \"Needs cover for wave 4.\" color = \"#ffd84a\" } ]\n" );
    return chunks;
}

std::string ReadFile( const std::filesystem::path &path )
{
    std::ifstream stream( path, std::ios::binary );
    return std::string( std::istreambuf_iterator<char>( stream ), std::istreambuf_iterator<char>() );
}

} // namespace

TEST_CASE( "Write the documented example map", "[.write-example]" )
{
    std::string root;
    const std::map<std::string, std::string> sources = ExampleSources( root );
    map_document_t map{};
    REQUIRE( Load( &map, root, sources ) == map_status_t::OK );
    CHECK_FALSE( map.bReadOnly );
    saved_files_t files;
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    const std::filesystem::path directory = CYPHER_MAP_EXAMPLE_DIR;
    std::filesystem::remove_all( directory / "facility" );
    std::filesystem::create_directories( directory / "facility" );
    std::ofstream( directory / "facility.cymap", std::ios::binary ) << files.root;
    for ( const auto &[path, text] : files.chunks ) {
        const std::filesystem::path file = directory / "facility" / path;
        std::filesystem::create_directories( file.parent_path() );
        std::ofstream( file, std::ios::binary ) << text;
    }
}

TEST_CASE( "The documented example map reproduces byte for byte", "[map][document][example]" )
{
    const std::filesystem::path directory = CYPHER_MAP_EXAMPLE_DIR;
    REQUIRE( std::filesystem::exists( directory / "facility.cymap" ) );
    const std::string root = ReadFile( directory / "facility.cymap" );
    std::map<std::string, std::string> chunks;
    for ( const auto &entry : std::filesystem::recursive_directory_iterator( directory / "facility" ) ) {
        if ( entry.is_regular_file() && entry.path().extension() == ".cymapchunk" ) {
            chunks[std::filesystem::relative( entry.path(), directory / "facility" ).generic_string()] = ReadFile( entry.path() );
        }
    }
    REQUIRE( chunks.size() >= 5u );
    saved_files_t files;
    files.root = root;
    files.chunks = chunks;
    RequireStable( files );

    // The example exercises every section of the format.
    std::string all = root;
    for ( const auto &entry : chunks ) { all += entry.second; }
    for ( const char *pSection : { "entities = [", "brushes = [", "meshes = [", "patches = [", "terrains = [", "shapes = [", "foliage = [",
                                   "notes = [", "groups = [", "prefabs = [", "selection_sets = [", "cordons = [", "outputs = [", "paint = {",
                                   "modifiers = [" } ) {
        INFO( pSection );
        CHECK( Contains( all, pSection ) );
    }
}

TEST_CASE( "Measure typical object sizes for CYMAP.md section 15", "[.scale]" )
{
    map_document_t map{};
    CreateMap( &map );
    constexpr usize kCount = 200u;
    saved_files_t files;
    for ( usize i = 0u; i < kCount; ++i ) {
        const u64 id = AddBox( &map, { static_cast<f64>( i ) * 128.0, 0.0, 64.0 } );
        for ( usize side = 0u; side < 6u; ++side ) { SetFaceMaterial( &map, id, side, "materials/concrete/wall03.cymat" ); }
    }
    REQUIRE( Save( &map, files ) == map_status_t::OK );
    usize cbBrushes = 0u;
    for ( const auto &entry : files.chunks ) { cbBrushes += entry.second.size(); }
    std::printf( "box brush: %.0f bytes\n", static_cast<double>( cbBrushes ) / kCount );
    usize nValues = 0u;
    for ( const auto &entry : files.chunks ) {
        key_value_document_t *pParsed = KeyValue_CreateDocument( {} );
        nValues += KeyValue_ParseText( SV( entry.second ), {}, pParsed ).nNodesParsed;
        KeyValue_DestroyDocument( pParsed );
    }
    std::printf( "box brush: %.0f values\n", static_cast<double>( nValues ) / kCount );

    // Entities with five properties and one output, foliage, and a terrain,
    // written by hand and saved by the writer.
    std::string entities = "    entities = [\n";
    std::string instances = "    foliage = [ { model = \"models/foliage/pine01.cymodel\" instances = [ ";
    for ( usize i = 0u; i < kCount; ++i ) {
        const std::string n = std::to_string( i + 1u );
        entities += ( i == 0u ? "" : ",\n" ) + std::string( "        { id = " ) + n + "u class = \"info_enemy_spawn\" name = \"spawn_" + n +
                    "\" origin = [ " + std::to_string( i * 16u ) + ".5, 128.25, 16.0 ] angles = [ 0, 90, 0 ]" +
                    " properties = { start_disabled = true enemy = \"grunt\" spawn_group = \"wave1\" count = 4 flags = [ \"face_player\" ] }" +
                    " outputs = [ { output = \"on_spawned\" target = \"wave1_counter\" input = \"add\" parameter = \"1\" delay = 0.0 times = -1 } ] }";
        instances += ( i == 0u ? "" : ", " ) + std::string( "[ " ) + std::to_string( i * 7u ) + ".25, 311.5, 44.125, 0, " + std::to_string( i % 360u ) + ".5, 0, 1.05 ]";
    }
    entities += "\n    ]\n";
    instances += " ] } ]\n";
    const auto measure = [&]( const std::string &body ) {
        std::map<std::string, std::string> chunks;
        chunks["default/global.cymapchunk"] = Chunk( "default", "\"global\"", body );
        map_document_t hand{};
        REQUIRE( Load( &hand, Root( "100000u" ), chunks ) == map_status_t::OK );
        saved_files_t handFiles;
        REQUIRE( Save( &hand, handFiles ) == map_status_t::OK );
        usize cb = 0u;
        for ( const auto &entry : handFiles.chunks ) { cb += entry.second.size(); }
        return cb;
    };
    const usize cbEntities = measure( entities );
    const usize cbFoliage = measure( instances );
    std::printf( "entity (5 properties, 1 output): %.0f bytes\n", static_cast<double>( cbEntities ) / kCount );
    std::printf( "foliage instance: %.0f bytes\n", static_cast<double>( cbFoliage ) / kCount );

    map_document_t terrain{};
    CreateMap( &terrain );
    geo::heightfield_t field{};
    const geo::geometry_source_id_t fieldId = NextId( &terrain );
    REQUIRE( geo::HeightField_TryInit( &field, terrain.pAllocator, { 0.0, 0.0, 0.0 }, 64.0, 64u, 64u, 16u, fieldId,
                                       &terrain.geometry.sourceIds.allocator ) == geo::geometry_status_t::OK );
    for ( usize i = 0u; i < field.heights.nCount; ++i ) { field.heights.pData[i] = static_cast<f64>( ( i * 37u ) % 512u ) * 0.25; }
    REQUIRE( geo::GeometryDocument_TryAddHeightField( &terrain.geometry, &field ) == geo::geometry_status_t::OK );
    geo::HeightField_Shutdown( &field );
    saved_files_t terrainFiles;
    REQUIRE( Save( &terrain, terrainFiles ) == map_status_t::OK );
    std::printf( "terrain: %.1f bytes per height sample\n", static_cast<double>( terrainFiles.chunks.begin()->second.size() ) / ( 65.0 * 65.0 ) );
}
