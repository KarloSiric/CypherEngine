//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_ObjectCreate_Tests.cpp
//  Purpose: Contract tests for the objects the interactive tools create:
//           entities, polygon and path meshes, patch primitives, and the
//           mesh paint stroke; and that they survive save and reload.
//
//  History:
//  - Created by Karlo Siric on 2026-10-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_ObjectCreate.h"
#include "CypherMap_PatchEdit.h"
#include "CypherMap_Wireframe.h"
#include "CypherMap_Edit.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMap_MeshEdit.h"
#include "CypherMap_Prefab.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_DocumentSurfaces.h"
#include "CypherGeometry_MeshBoundaryOps.h"
#include "CypherGeometry_CurveSampling.h"
#include "CypherGeometry_RaycastQueries.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <utility>
#include <string>
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
    fixture_t() { REQUIRE( MapDocument_Create( &map, Allocator_GetSystem(), { SV( "Create fixture" ), SV( "reap" ), {} } ) == map_status_t::OK ); }
};
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
usize Faces( const map_document_t &map, u64 id )
{
    geo::mesh_source_description_t d{};
    REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { id } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &map.geometry, { id } ), &d ) == geo::geometry_status_t::OK );
    const usize n = d.faces.nCount;
    geo::MeshSourceDescription_Shutdown( &d );
    return n;
}
string_view_t Property( map_document_t &map, u64 id, const char *key )
{
    const auto *record = MapEntityEdit_FindEntity( &map, id );
    string_view_t out{};
    ( void )KeyValue_GetString( KeyValue_Find( KeyValue_Find( record, SV( "properties" ) ), SV( key ) ), &out );
    if ( out.cchLength == 0 ) { ( void )KeyValue_GetString( KeyValue_Find( record, SV( key ) ), &out ); }
    return out;
}
} // namespace

TEST_CASE( "Entities are created with their class, origin and string properties", "[map][object-create][entity]" )
{
    fixture_t f;
    const map_property_t properties[]{ { SV( "targetname" ), SV( "corner_a" ) }, { SV( "texture" ), SV( "materials/dev/dev_grid.cymat" ) } };
    u64 id = 0;
    REQUIRE( MapCreate_Entity( &f.map, SV( "path_corner" ), { 64, 32, 16 }, { properties, 2 }, &id ) == map_status_t::OK );
    REQUIRE( id != 0 );
    REQUIRE( MapEntityEdit_FindEntity( &f.map, id ) != nullptr );
    CHECK( StringView_Equals( Property( f.map, id, "targetname" ), SV( "corner_a" ) ) );
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded; saved.Load( loaded );
    CHECK( MapEntityEdit_FindEntity( &loaded, id ) != nullptr );
    CHECK( StringView_Equals( Property( loaded, id, "texture" ), SV( "materials/dev/dev_grid.cymat" ) ) );
    u64 none = 0;
    CHECK( MapCreate_Entity( &f.map, SV( "" ), {}, {}, &none ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Polygon meshes extrude any flat polygon, including concave ones, into a closed solid", "[map][object-create][polygon]" )
{
    fixture_t f;
    // An L shape, clockwise seen from above: the winding is fixed up.
    const math::vec3d_t l[]{ { 0, 0, 0 }, { 0, 128, 0 }, { 64, 128, 0 }, { 64, 64, 0 }, { 128, 64, 0 }, { 128, 0, 0 } };
    u64 id = 0;
    REQUIRE( MapCreate_PolygonMesh( &f.map, { l, 6 }, { 0, 0, 32 }, SV( "materials/dev/dev_grid.cymat" ), 128.0, &id ) == map_status_t::OK );
    const auto *mesh = geo::GeometryDocument_FindMesh( &f.map.geometry, { id } ); REQUIRE( mesh );
    CHECK( Faces( f.map, id ) == 8 ); CHECK( geo::MeshBoundary_CountBoundaryEdges( &mesh->mesh ) == 0 );
    const auto bounds = MapGeometry_MeshBounds( *mesh, f.map.pAllocator );
    CHECK( bounds.box.maximum.z == Catch::Approx( 32 ) ); CHECK( bounds.box.maximum.x == Catch::Approx( 128 ) );
    CHECK( f.map.nextId > id );
    const math::vec3d_t line[]{ { 0, 0, 0 }, { 64, 0, 0 }, { 128, 0, 0 } };
    CHECK( MapCreate_PolygonMesh( &f.map, { line, 3 }, { 0, 0, 32 }, SV( "materials/dev/dev_grid.cymat" ), 128.0, &id ) == map_status_t::INVALID_ARGUMENT );
    const math::vec3d_t bent[]{ { 0, 0, 0 }, { 64, 0, 0 }, { 64, 64, 40 }, { 0, 64, 0 } };
    CHECK( MapCreate_PolygonMesh( &f.map, { bent, 4 }, { 0, 0, 32 }, SV( "materials/dev/dev_grid.cymat" ), 128.0, &id ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Path meshes sweep a standing rectangle along a polyline", "[map][object-create][curve]" )
{
    fixture_t f;
    const math::vec3d_t path[]{ { 0, 0, 0 }, { 256, 0, 0 }, { 256, 256, 0 } };
    u64 id = 0;
    REQUIRE( MapCreate_PathMesh( &f.map, { path, 3 }, 32.0, 16.0, SV( "materials/dev/dev_grid.cymat" ), 128.0, &id ) == map_status_t::OK );
    const auto *mesh = geo::GeometryDocument_FindMesh( &f.map.geometry, { id } ); REQUIRE( mesh );
    CHECK( Faces( f.map, id ) == 2 + 4 * 2 ); CHECK( geo::MeshBoundary_CountBoundaryEdges( &mesh->mesh ) == 0 );
    CHECK( MapGeometry_MeshBounds( *mesh, f.map.pAllocator ).box.maximum.z == Catch::Approx( 16 ) );
    const math::vec3d_t up[]{ { 0, 0, 0 }, { 0, 0, 64 } };
    CHECK( MapCreate_PathMesh( &f.map, { up, 2 }, 32.0, 16.0, SV( "materials/dev/dev_grid.cymat" ), 128.0, &id ) == map_status_t::INVALID_ARGUMENT );
}

namespace
{
struct hollow_description_t {
    geo::mesh_source_description_t value{};
    explicit hollow_description_t( const geo::mesh_source_t &mesh )
    {
        REQUIRE( geo::MeshSourceDescription_Init( &value, Allocator_GetSystem(), mesh.sourceId ) == geo::geometry_status_t::OK );
        REQUIRE( geo::MeshSource_TryDescribe( &mesh, &value ) == geo::geometry_status_t::OK );
    }
    ~hollow_description_t() { geo::MeshSourceDescription_Shutdown( &value ); }
};
geo::mesh_raycast_hit_t HollowRay( const map_document_t &map, const geo::mesh_source_t &mesh, math::vec3d_t origin, math::vec3d_t direction )
{
    usize bytes = 0;
    REQUIRE( geo::MeshQueries_TryGetRaycastScratchSize( &mesh.mesh, map.geometryPolicy, &bytes ) == geo::geometry_status_t::OK );
    geo::geometry_scratch_t scratch{};
    geo::geometry_scratch_desc_t desc{}; desc.pFallbackAllocator = Allocator_GetSystem(); desc.cbCapacity = desc.cbBudget = bytes;
    REQUIRE( geo::GeometryScratch_Acquire( &scratch, desc ) == geo::geometry_status_t::OK );
    geo::geometry_raycast_options_t options{}; options.fMaximumDistance = 64;
    options.meshSurfaceMode = geo::geometry_mesh_raycast_surface_t::PROJECTED_TRIANGLES;
    geo::mesh_raycast_hit_t hit{};
    const auto status = geo::MeshQueries_TryRaycast( &mesh.mesh, mesh.sourceId, { origin, direction }, options, map.geometryPolicy, &scratch, &hit );
    REQUIRE( geo::GeometryScratch_Release( &scratch ) == geo::geometry_status_t::OK );
    REQUIRE( status == geo::geometry_status_t::OK );
    return hit;
}
struct hollow_audit_t {
    usize calls{}, bytes{}, failAt{ CY_USIZE_MAX };
    allocator_t allocator{};
    hollow_audit_t()
    {
        allocator.pUserData = this;
        allocator.pfnAllocate = []( void *ctx, usize size, usize alignment ) noexcept -> void * {
            auto &audit = *static_cast<hollow_audit_t *>( ctx );
            if ( audit.calls++ == audit.failAt ) { return nullptr; }
            void *memory = Allocator_Allocate( Allocator_GetSystem(), size, alignment );
            if ( memory != nullptr ) { audit.bytes += size; }
            return memory;
        };
        allocator.pfnFree = []( void *ctx, void *memory, usize size, usize alignment ) noexcept {
            if ( memory != nullptr ) { static_cast<hollow_audit_t *>( ctx )->bytes -= size; }
            Allocator_Free( Allocator_GetSystem(), memory, size, alignment );
        };
    }
};
void HollowFailureMap( map_document_t &map, const allocator_t *allocator )
{
    REQUIRE( MapDocument_Create( &map, allocator, { SV( "Hollow failure fixture" ), SV( "reap" ), {} } ) == map_status_t::OK );
    const math::vec3d_t base[]{ { 64, 64, 0 }, { 72, 64, 0 }, { 72, 72, 0 }, { 64, 72, 0 } };
    u64 existing = 0;
    REQUIRE( MapCreate_PolygonMesh( &map, { base, 4 }, { 0, 0, 8 }, SV( "existing.cymat" ), 8, &existing ) == map_status_t::OK );
    REQUIRE( geo::GeometryDocument_MeshCount( &map.geometry ) == 1 );
}
void HollowCheckUnchanged( map_document_t &map, const saved_t &before, u64 next, u64 geometryNext, u64 revision,
    usize live, usize claimed, usize materials, usize materialBytes, usize records, usize meshes )
{
    CHECK( map.nextId == next ); CHECK( map.geometry.sourceIds.allocator.next.value == geometryNext );
    CHECK( map.geometry.revision == revision );
    CHECK( geo::GeometrySourceIdRegistry_Count( &map.geometry.sourceIds ) == live );
    CHECK( geo::GeometrySourceIdRegistry_ClaimedCount( &map.geometry.sourceIds ) == claimed );
    CHECK( map.materials.entries.nCount == materials ); CHECK( map.materials.text.nCount == materialBytes );
    CHECK( map.geometryRecords.nCount == records ); CHECK( geo::GeometryDocument_MeshCount( &map.geometry ) == meshes );
    saved_t after; REQUIRE( after.Save( &map ) == map_status_t::OK ); CHECK( after.root == before.root );
}
}

TEST_CASE( "Hollow Pipe creates annular walls with an open passage and distance UV charts", "[map][object-create][hollow-pipe]" )
{
    fixture_t f;
    REQUIRE( MapDocument_AddLayer( &f.map, SV( "pipes" ), SV( "Pipes" ) ) == map_status_t::OK );
    std::strcpy( f.map.creationLayer, "pipes" );
    const math::vec3d_t path[]{ { 0, 0, 0 }, { 0, 0, 32 } };
    const auto revision = f.map.geometry.revision;
    u64 id = 0;
    REQUIRE( MapCreate_HollowPipe( &f.map, { path, 2 }, 4, 1, 8, SV( "materials/pipes/steel.cymat" ), 8, &id ) == map_status_t::OK );
    CHECK( f.map.geometry.revision == revision + 1 ); CHECK( f.map.nextId > id );
    REQUIRE( f.map.geometryRecords.nCount == 1 ); CHECK( f.map.geometryRecords.pData[0].id == id );
    CHECK( f.map.geometryRecords.pData[0].iLayer == 1 ); CHECK( f.map.geometryRecords.pData[0].owner == 0 );
    const auto *mesh = geo::GeometryDocument_FindMesh( &f.map.geometry, { id } ); REQUIRE( mesh != nullptr );
    REQUIRE( geo::MeshSource_Validate( mesh, f.map.pAllocator ).fault == geo::mesh_source_fault_t::NONE );
    CHECK( geo::EditableMesh_VertexCount( &mesh->mesh ) == 80 ); CHECK( geo::EditableMesh_FaceCount( &mesh->mesh ) == 80 );
    CHECK( geo::EditableMesh_ShellCount( &mesh->mesh ) == 1 ); CHECK( geo::EditableMesh_EulerCharacteristic( &mesh->mesh ) == 0 );
    CHECK( geo::MeshBoundary_CountBoundaryEdges( &mesh->mesh ) == 0 ); CHECK( geo::EditableMesh_SignedVolume( &mesh->mesh ) > 0 );
    CHECK_FALSE( HollowRay( f.map, *mesh, { 0, 0, -8 }, { 0, 0, 1 } ).bHit );
    const auto bore = HollowRay( f.map, *mesh, { 0, 0, 16 }, { 1, 0, 0 } ); REQUIRE( bore.bHit );
    CHECK( bore.position.x == Catch::Approx( 3 ) ); CHECK( bore.normal.x < -0.9 );
    const auto outside = HollowRay( f.map, *mesh, { 8, 0, 16 }, { -1, 0, 0 } ); REQUIRE( outside.bHit );
    CHECK( outside.position.x == Catch::Approx( 4 ) ); CHECK( outside.normal.x > 0.9 );
    const auto rim = HollowRay( f.map, *mesh, { 3.5, 0, -8 }, { 0, 0, 1 } ); REQUIRE( rim.bHit );
    CHECK( rim.position.z == Catch::Approx( 0 ).margin( 1e-9 ) ); CHECK( rim.normal.z < -0.99 );
    hollow_description_t described( *mesh );
    // This clamped two-point Catmull-Rom span is z(t)=16t+48t²-32t³.
    // Arc-length inversion linearly interpolates 64 chords, so its position
    // error is bounded by max|z''| h²/8 = 96/(8*64²), not floating-point epsilon.
    // UV distance remains the exact requested 0,8,16,24,32 sample distance.
    constexpr double ringPositionError = 96.0 / ( 8.0 * 64.0 * 64.0 );
    usize ringVertices[5]{};
    for ( usize i = 0; i < described.value.vertices.nCount; ++i ) {
        const auto z = described.value.vertices.pData[i].position.z;
        const auto ring = std::lround( z / 8.0 );
        REQUIRE( ring >= 0 ); REQUIRE( ring <= 4 );
        CHECK( std::fabs( z - 8.0 * static_cast<double>( ring ) ) <= ringPositionError + 1e-12 );
        ++ringVertices[ring];
    }
    for ( const auto count : ringVertices ) { CHECK( count == 16 ); }
    usize outer = 0, inner = 0, rims = 0;
    std::map<u64, std::pair<double, double>> seam;
    for ( usize i = 0; i < described.value.faces.nCount; ++i ) {
        const auto &face = described.value.faces.pData[i]; REQUIRE( face.cCorners == 4 );
        CHECK( StringView_Equals( MapMaterials_Path( &f.map.materials, face.attributes.material.value ), SV( "materials/pipes/steel.cymat" ) ) );
        const auto group = face.attributes.smoothingGroups;
        if ( group == 1 ) { ++outer; } else if ( group == 2 ) { ++inner; } else { CHECK( group == 0 ); ++rims; }
        for ( u32 k = 0; k < face.cCorners; ++k ) {
            const auto &corner = described.value.corners.pData[face.iFirstCorner + k];
            const auto &vertex = described.value.vertices.pData[corner.iVertex];
            const auto &a = corner.attributes;
            CHECK( a.uv0.x == a.uv1.x ); CHECK( a.uv0.y == a.uv1.y ); CHECK( a.colorRgba == 0xFFFFFFFFu );
            if ( group != 0 ) {
                CHECK( a.uv0.y == static_cast<double>( std::lround( vertex.position.z / 8.0 ) ) );
                CHECK( std::hypot( vertex.position.x, vertex.position.y ) == Catch::Approx( group == 1 ? 4.0 : 3.0 ) );
                CHECK( ( group == 1 ? a.uv0.x >= 0.0 : a.uv0.x <= 0.0 ) );
                if ( group == 1 ) {
                    auto [at, inserted] = seam.emplace( vertex.sourceId.value, std::pair{ a.uv0.x, a.uv0.x } );
                    if ( !inserted ) { at->second.first = std::min( at->second.first, a.uv0.x ); at->second.second = std::max( at->second.second, a.uv0.x ); }
                }
            }
        }
    }
    CHECK( outer == 32 ); CHECK( inner == 32 ); CHECK( rims == 16 );
    usize seamVertices = 0;
    for ( const auto &[vertex, range] : seam ) {
        (void)vertex;
        if ( range.second - range.first > 1 ) { ++seamVertices; CHECK( range.second - range.first == Catch::Approx( 3.141592653589793 ) ); }
    }
    CHECK( seamVertices == 5 );
}

TEST_CASE( "Curved Hollow Pipe preserves source identities and all corner charts through save and reload", "[map][object-create][hollow-pipe][persistence]" )
{
    fixture_t f;
    const math::vec3d_t path[]{ { 0, 0, 0 }, { 32, 0, 0 }, { 64, 16, 0 } };
    u64 id = 0;
    REQUIRE( MapCreate_HollowPipe( &f.map, { path, 3 }, 2, 0.5, 8, SV( "materials/pipes/copper.cymat" ), 16, &id ) == map_status_t::OK );
    const auto *mesh = geo::GeometryDocument_FindMesh( &f.map.geometry, { id } ); REQUIRE( mesh != nullptr );
    geo::curve_network_t network{}; geo::geometry_source_id_allocator_t scratchIds{ { 1 } }; u32 index = 0;
    REQUIRE( geo::CurveNetwork_Init( &network, Allocator_GetSystem(), { 1 } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::CurveNetwork_TryAddCatmullRomPath( &network, { path, 3 }, false, &scratchIds, &index ) == geo::geometry_status_t::OK );
    vector_t<geo::curve_sample_t> samples{}; REQUIRE( Vector_Init( &samples, Allocator_GetSystem() ) );
    geo::curve_sampling_options_t sampling{}; sampling.fMaxSpacing = 8;
    REQUIRE( geo::CurveSampling_TrySamplePath( &network, index, sampling, &samples ) == geo::geometry_status_t::OK );
    const usize expected = 16 * samples.nCount; geo::CurveNetwork_Shutdown( &network );
    CHECK( geo::EditableMesh_FaceCount( &mesh->mesh ) == expected ); CHECK( geo::EditableMesh_VertexCount( &mesh->mesh ) == expected );
    CHECK( geo::MeshBoundary_CountBoundaryEdges( &mesh->mesh ) == 0 ); CHECK( geo::EditableMesh_ShellCount( &mesh->mesh ) == 1 );
    CHECK( geo::EditableMesh_SignedVolume( &mesh->mesh ) > 0 );
    hollow_description_t before( *mesh ); saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded; saved.Load( loaded );
    const auto *restored = geo::GeometryDocument_FindMesh( &loaded.geometry, { id } ); REQUIRE( restored != nullptr );
    hollow_description_t after( *restored ); REQUIRE( before.value.faces.nCount == after.value.faces.nCount );
    for ( usize i = 0; i < before.value.faces.nCount; ++i ) {
        const auto &a = before.value.faces.pData[i]; auto &b = after.value.faces.pData[i];
        CHECK( a.sourceId.value == b.sourceId.value );
        CHECK( StringView_Equals( MapMaterials_Path( &f.map.materials, a.attributes.material.value ), MapMaterials_Path( &loaded.materials, b.attributes.material.value ) ) );
        b.attributes.material = a.attributes.material; // Material handles are document-local; paths are the persistent identity.
    }
    CHECK( geo::MeshSourceDescription_Equal( &before.value, &after.value ) );
    CHECK( loaded.nextId == f.map.nextId ); CHECK( loaded.geometryRecords.nCount == f.map.geometryRecords.nCount );
}

TEST_CASE( "Hollow Pipe rejects invalid input and folded paths without consuming map state", "[map][object-create][hollow-pipe][atomic]" )
{
    fixture_t f;
    const math::vec3d_t straight[]{ { 0, 0, 0 }, { 0, 0, 32 } };
    saved_t before; REQUIRE( before.Save( &f.map ) == map_status_t::OK );
    const auto next = f.map.nextId, geometryNext = f.map.geometry.sourceIds.allocator.next.value, revision = f.map.geometry.revision;
    const auto live = geo::GeometrySourceIdRegistry_Count( &f.map.geometry.sourceIds ), claimed = geo::GeometrySourceIdRegistry_ClaimedCount( &f.map.geometry.sourceIds );
    const auto materials = f.map.materials.entries.nCount, materialBytes = f.map.materials.text.nCount, records = f.map.geometryRecords.nCount;
    u64 out = 987654321;
    SECTION( "Wall closes the passage" ) { CHECK( MapCreate_HollowPipe( &f.map, { straight, 2 }, 4, 4, 8, SV( "new.cymat" ), 8, &out ) == map_status_t::INVALID_ARGUMENT ); }
    SECTION( "Invalid side count" ) { CHECK( MapCreate_HollowPipe( &f.map, { straight, 2 }, 4, 1, 2, SV( "new.cymat" ), 8, &out ) == map_status_t::INVALID_ARGUMENT ); }
    SECTION( "Non-finite radius" ) { CHECK( MapCreate_HollowPipe( &f.map, { straight, 2 }, std::numeric_limits<double>::infinity(), 1, 8, SV( "new.cymat" ), 8, &out ) == map_status_t::INVALID_ARGUMENT ); }
    SECTION( "Folded tight bend" ) {
        const math::vec3d_t tight[]{ { -16, 0, 0 }, { 0, 0, 0 }, { 0, 16, 0 } };
        CHECK( MapCreate_HollowPipe( &f.map, { tight, 3 }, 64, 4, 8, SV( "new.cymat" ), 8, &out ) == map_status_t::GEOMETRY_FAILED );
    }
    SECTION( "Crossing centreline" ) {
        const math::vec3d_t crossing[]{ { -32, -32, 0 }, { 32, 32, 0 }, { -32, 32, 0 }, { 32, -32, 0 } };
        CHECK( MapCreate_HollowPipe( &f.map, { crossing, 4 }, 2, 0.5, 8, SV( "new.cymat" ), 8, &out ) == map_status_t::GEOMETRY_FAILED );
    }
    SECTION( "Read-only map" ) { f.map.bReadOnly = true; CHECK( MapCreate_HollowPipe( &f.map, { straight, 2 }, 4, 1, 8, SV( "new.cymat" ), 8, &out ) == map_status_t::READ_ONLY ); f.map.bReadOnly = false; }
    SECTION( "Identity budget exhausted during preparation" ) {
        f.map.nextId = CY_U64_MAX - 4;
        CHECK( MapCreate_HollowPipe( &f.map, { straight, 2 }, 4, 1, 8, SV( "new.cymat" ), 8, &out ) == map_status_t::LIMIT_EXCEEDED );
        CHECK( f.map.nextId == CY_U64_MAX - 4 ); f.map.nextId = next;
    }
    CHECK( out == 987654321 );
    HollowCheckUnchanged( f.map, before, next, geometryNext, revision, live, claimed, materials, materialBytes, records, 0 );
}

TEST_CASE( "Hollow Pipe allocation failures leave material identity placement and output unpublished", "[map][object-create][hollow-pipe][allocation]" )
{
    const math::vec3d_t path[]{ { 0, 0, 0 }, { 0, 0, 16 } };
    usize calls = 0;
    hollow_audit_t measure;
    {
        map_document_t map; HollowFailureMap( map, &measure.allocator );
        saved_t before; REQUIRE( before.Save( &map ) == map_status_t::OK );
        const auto begin = measure.calls; u64 out = 0;
        REQUIRE( MapCreate_HollowPipe( &map, { path, 2 }, 4, 1, 4, SV( "new.cymat" ), 8, &out ) == map_status_t::OK );
        calls = measure.calls - begin;
    }
    REQUIRE( measure.bytes == 0 ); REQUIRE( calls > 16 );
    std::set<usize> failures{ 0, 1, 2, 3, calls / 4, calls / 2, 3 * calls / 4 };
    for ( usize i = calls - 16; i < calls; ++i ) { failures.insert( i ); }
    for ( usize failure : failures ) {
        CAPTURE( failure, calls ); hollow_audit_t audit;
        {
            map_document_t map; HollowFailureMap( map, &audit.allocator );
            saved_t before; REQUIRE( before.Save( &map ) == map_status_t::OK );
            const auto next = map.nextId, geometryNext = map.geometry.sourceIds.allocator.next.value, revision = map.geometry.revision;
            const auto live = geo::GeometrySourceIdRegistry_Count( &map.geometry.sourceIds ), claimed = geo::GeometrySourceIdRegistry_ClaimedCount( &map.geometry.sourceIds );
            const auto materials = map.materials.entries.nCount, materialBytes = map.materials.text.nCount, records = map.geometryRecords.nCount;
            audit.failAt = audit.calls + failure; u64 out = 987654321;
            CHECK( MapCreate_HollowPipe( &map, { path, 2 }, 4, 1, 4, SV( "new.cymat" ), 8, &out ) == map_status_t::OUT_OF_MEMORY );
            audit.failAt = CY_USIZE_MAX;
            CHECK( out == 987654321 );
            HollowCheckUnchanged( map, before, next, geometryNext, revision, live, claimed, materials, materialBytes, records, 1 );
            REQUIRE( MapCreate_HollowPipe( &map, { path, 2 }, 4, 1, 4, SV( "new.cymat" ), 8, &out ) == map_status_t::OK );
            CHECK( out != 987654321 );
        }
        CHECK( audit.bytes == 0 );
    }
}

TEST_CASE( "Patch primitives fill their box and survive save and reload", "[map][object-create][patch]" )
{
    fixture_t f;
    std::vector<u64> ids;
    for ( auto shape : { map_patch_shape_t::PLANE, map_patch_shape_t::CYLINDER, map_patch_shape_t::CONE, map_patch_shape_t::SPHERE,
                         map_patch_shape_t::DISC, map_patch_shape_t::BEVEL } ) {
        CAPTURE( static_cast<int>( shape ) );
        u64 id = 0;
        REQUIRE( MapCreate_Patch( &f.map, shape, { { 0, 0, 0 }, { 128, 128, 128 } }, 2, 8, SV( "materials/dev/dev_grid.cymat" ), &id ) == map_status_t::OK );
        const auto *patch = geo::GeometryDocument_FindPatch( &f.map.geometry, { id } ); REQUIRE( patch );
        const auto bounds = MapGeometry_PatchBounds( *patch );
        CHECK( bounds.box.maximum.x <= 128.0 + 1e-6 ); CHECK( bounds.box.minimum.x >= -1e-6 );
        ids.push_back( id );
    }
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded; saved.Load( loaded );
    for ( u64 id : ids ) { CHECK( geo::GeometryDocument_FindPatch( &loaded.geometry, { id } ) != nullptr ); }
    u64 id = 0;
    CHECK( MapCreate_Patch( &f.map, map_patch_shape_t::CYLINDER, { { 0, 0, 0 }, { 0, 128, 128 } }, 2, 8, SV( "materials/dev/dev_grid.cymat" ), &id ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "A paint stroke changes corner colours near its dabs only", "[map][object-create][paint]" )
{
    fixture_t f;
    const math::vec3d_t square[]{ { 0, 0, 0 }, { 256, 0, 0 }, { 256, 256, 0 }, { 0, 256, 0 } };
    u64 id = 0;
    REQUIRE( MapCreate_PolygonMesh( &f.map, { square, 4 }, { 0, 0, 64 }, SV( "materials/dev/dev_grid.cymat" ), 128.0, &id ) == map_status_t::OK );
    const auto revision = f.map.geometry.revision;
    const math::vec3d_t dab[]{ { 0, 0, 64 } };
    REQUIRE( MapMesh_PaintStroke( &f.map, id, { dab, 1 }, 32.0, 1.0, 1u, 0u ) == map_status_t::OK );
    CHECK( f.map.geometry.revision != revision );
    geo::mesh_source_description_t d{};
    REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { id } ) == geo::geometry_status_t::OK );
    REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &f.map.geometry, { id } ), &d ) == geo::geometry_status_t::OK );
    usize painted = 0, untouched = 0;
    for ( usize i = 0; i < d.corners.nCount; ++i ) {
        const auto &p = d.vertices.pData[d.corners.pData[i].iVertex].position;
        const u32 color = d.corners.pData[i].attributes.colorRgba;
        if ( p.x == 0 && p.y == 0 && p.z == 64 ) { painted += color != 0xFFFFFFFFu; } else { untouched += color == 0xFFFFFFFFu; }
    }
    geo::MeshSourceDescription_Shutdown( &d );
    CHECK( painted > 0 ); CHECK( untouched > 0 );
    CHECK( MapMesh_PaintStroke( &f.map, id, { dab, 1 }, 0.0, 1.0, 1u, 0u ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Groups keep their members, nest, and survive save and reload", "[map][object-create][group]" )
{
    fixture_t f;
    u64 a = 0, b = 0, c = 0;
    REQUIRE( MapEdit_CreateBox( &f.map, { { 0, 0, 0 }, { 64, 64, 64 } }, SV( "materials/dev/dev_grid.cymat" ), {}, &a ) == map_status_t::OK );
    REQUIRE( MapEdit_CreateBox( &f.map, { { 128, 0, 0 }, { 192, 64, 64 } }, SV( "materials/dev/dev_grid.cymat" ), {}, &b ) == map_status_t::OK );
    REQUIRE( MapCreate_Entity( &f.map, SV( "info_target" ), { 0, 256, 0 }, {}, &c ) == map_status_t::OK );
    const u64 pair[]{ b, a };
    u64 inner = 0, outer = 0;
    REQUIRE( MapDocument_AddGroup( &f.map, SV( "pair" ), { pair, 2 }, &inner ) == map_status_t::OK );
    const u64 nested[]{ inner, c };
    REQUIRE( MapDocument_AddGroup( &f.map, SV( "all" ), { nested, 2 }, &outer ) == map_status_t::OK );
    CHECK( MapDocument_GroupOf( &f.map, a ) == outer ); CHECK( MapDocument_GroupOf( &f.map, c ) == outer );
    vector_t<u64> members{}; REQUIRE( Vector_Init( &members, Allocator_GetSystem() ) );
    REQUIRE( MapDocument_GroupMembers( &f.map, outer, &members ) == map_status_t::OK );
    CHECK( members.nCount == 3 );
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded; saved.Load( loaded );
    CHECK( MapDocument_GroupOf( &loaded, b ) == outer );
    REQUIRE( MapDocument_GroupMembers( &loaded, outer, &members ) == map_status_t::OK );
    CHECK( members.nCount == 3 );
    Vector_Shutdown( &members );
}

TEST_CASE( "Visgroups list their members, also unsaved geometry, and survive save and reload", "[map][object-create][visgroup]" )
{
    fixture_t f;
    u64 brush = 0, entity = 0, visgroup = 0;
    REQUIRE( MapEdit_CreateBox( &f.map, { { 0, 0, 0 }, { 64, 64, 64 } }, SV( "materials/dev/dev_grid.cymat" ), {}, &brush ) == map_status_t::OK );
    REQUIRE( MapCreate_Entity( &f.map, SV( "info_target" ), { 0, 256, 0 }, {}, &entity ) == map_status_t::OK );
    REQUIRE( MapDocument_AddVisgroup( &f.map, SV( "Arena" ), &visgroup ) == map_status_t::OK );
    REQUIRE( MapDocument_AddToVisgroup( &f.map, visgroup, brush ) == map_status_t::OK );
    REQUIRE( MapDocument_AddToVisgroup( &f.map, visgroup, entity ) == map_status_t::OK );
    REQUIRE( MapDocument_AddToVisgroup( &f.map, visgroup, entity ) == map_status_t::OK ); // Once only.
    vector_t<u64> members{}; REQUIRE( Vector_Init( &members, Allocator_GetSystem() ) );
    REQUIRE( MapDocument_VisgroupMembers( &f.map, visgroup, &members ) == map_status_t::OK );
    CHECK( members.nCount == 2 );
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded; saved.Load( loaded );
    REQUIRE( MapDocument_VisgroupMembers( &loaded, visgroup, &members ) == map_status_t::OK );
    CHECK( members.nCount == 2 );
    CHECK( KeyValue_ChildCount( KeyValue_Find( SettingsDocument_Root( &loaded.root ), SV( "visgroups" ) ) ) == 1 );
    Vector_Shutdown( &members );
}

TEST_CASE( "Prefabs store a selection relative to its base and insert copies with fresh IDs", "[map][object-create][prefab]" )
{
    fixture_t f;
    u64 brush = 0, entity = 0;
    REQUIRE( MapEdit_CreateBox( &f.map, { { 100, 100, 16 }, { 164, 164, 80 } }, SV( "materials/dev/dev_grid.cymat" ), {}, &brush ) == map_status_t::OK );
    REQUIRE( MapCreate_Entity( &f.map, SV( "light" ), { 132, 132, 64 }, {}, &entity ) == map_status_t::OK );
    const u64 ids[]{ brush, entity };
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem(), 0 ) );
    REQUIRE( MapPrefab_Write( &f.map, { ids, 2 }, SV( "Lamp post" ), &text ) == map_status_t::OK );
    const std::string written( TextBuffer_Data( &text ), TextBuffer_View( &text ).cchLength );
    CHECK( written.find( "cypher.prefab" ) != std::string::npos ); CHECK( written.find( "prefab_id" ) != std::string::npos );
    CHECK( written.find( "brushes" ) != std::string::npos ); CHECK( written.find( "entities" ) != std::string::npos );
    vector_t<u64> placed{}; REQUIRE( Vector_Init( &placed, Allocator_GetSystem() ) );
    const usize brushes = f.map.geometry.brushes.nCount;
    REQUIRE( MapPrefab_Insert( &f.map, TextBuffer_View( &text ), { 512, 0, 0 }, &placed ) == map_status_t::OK );
    REQUIRE( placed.nCount == 2 );
    CHECK( f.map.geometry.brushes.nCount == brushes + 1 );
    u64 copy = 0;
    for ( usize i = 0; i < placed.nCount; ++i ) { if ( geo::GeometryDocument_FindBrush( &f.map.geometry, { placed.pData[i] } ) ) { copy = placed.pData[i]; } }
    REQUIRE( copy != 0 ); CHECK( copy != brush );
    geo::brush_source_t source{};
    REQUIRE( geo::GeometryDocument_TryCopyBrushSource( &f.map.geometry, { copy }, f.map.pAllocator, &source ) == geo::geometry_status_t::OK );
    const auto bounds = MapGeometry_BrushBounds( source, f.map.geometryPolicy, f.map.pAllocator );
    CHECK( bounds.box.minimum.z == Catch::Approx( 0 ) ); // The prefab's base stands on the placed point...
    CHECK( ( bounds.box.minimum.x + bounds.box.maximum.x ) * 0.5 == Catch::Approx( 512 ) ); // ...centred on it.
    geo::BrushSource_Shutdown( &source );
    CHECK( MapPrefab_Insert( &f.map, SV( "not a prefab" ), {}, &placed ) == map_status_t::ROOT_UNREADABLE );
    Vector_Shutdown( &placed ); TextBuffer_Shutdown( &text );
}

TEST_CASE( "Patch matrix operations insert, remove, transpose and invert controls", "[map][object-create][patch-edit]" )
{
    fixture_t f;
    u64 id = 0;
    REQUIRE( MapCreate_Patch( &f.map, map_patch_shape_t::PLANE, { { 0, 0, 0 }, { 128, 64, 0 } }, 2, 8, SV( "materials/dev/dev_grid.cymat" ), &id ) == map_status_t::OK );
    const auto *patch = geo::GeometryDocument_FindPatch( &f.map.geometry, { id } ); REQUIRE( patch );
    REQUIRE( patch->cColumns == 3 ); REQUIRE( patch->cRows == 3 );
    const auto boundsBefore = MapGeometry_PatchBounds( *patch ).box;
    CHECK_FALSE( MapPatchEdit_Can( &f.map, id, map_patch_op_t::REMOVE_ROW ) ); // One span: nothing to merge.
    CHECK( MapPatchEdit_Apply( &f.map, { &id, 1 }, map_patch_op_t::REMOVE_ROW ) == map_status_t::INVALID_ARGUMENT );
    REQUIRE( MapPatchEdit_Apply( &f.map, { &id, 1 }, map_patch_op_t::INSERT_ROW ) == map_status_t::OK );
    patch = geo::GeometryDocument_FindPatch( &f.map.geometry, { id } );
    CHECK( patch->cRows == 5 ); CHECK( patch->cColumns == 3 );
    const auto boundsAfter = MapGeometry_PatchBounds( *patch ).box; // The surface keeps its shape.
    CHECK( boundsAfter.maximum.x == Catch::Approx( boundsBefore.maximum.x ) ); CHECK( boundsAfter.maximum.y == Catch::Approx( boundsBefore.maximum.y ) );
    CHECK( f.map.nextId > patch->controls.pData[patch->controls.nCount - 1].sourceId.value );
    REQUIRE( MapPatchEdit_Apply( &f.map, { &id, 1 }, map_patch_op_t::TRANSPOSE ) == map_status_t::OK );
    patch = geo::GeometryDocument_FindPatch( &f.map.geometry, { id } );
    CHECK( patch->cRows == 3 ); CHECK( patch->cColumns == 5 );
    REQUIRE( MapPatchEdit_Apply( &f.map, { &id, 1 }, map_patch_op_t::REMOVE_COLUMN ) == map_status_t::OK );
    patch = geo::GeometryDocument_FindPatch( &f.map.geometry, { id } );
    CHECK( patch->cColumns == 3 );
    const auto first = patch->controls.pData[0].sourceId.value;
    REQUIRE( MapPatchEdit_Apply( &f.map, { &id, 1 }, map_patch_op_t::INVERT ) == map_status_t::OK );
    patch = geo::GeometryDocument_FindPatch( &f.map.geometry, { id } );
    CHECK( patch->controls.pData[0].sourceId.value != first ); // Columns reversed.
    const u64 unknown = 999999;
    CHECK( MapPatchEdit_Apply( &f.map, { &unknown, 1 }, map_patch_op_t::INVERT ) == map_status_t::UNKNOWN_OBJECT );

    u64 torus = 0;
    REQUIRE( MapCreate_Patch( &f.map, map_patch_shape_t::TORUS, { { 0, 0, 0 }, { 128, 128, 32 } }, 2, 8, SV( "materials/dev/dev_grid.cymat" ), &torus ) == map_status_t::OK );
    const auto ring = MapGeometry_PatchBounds( *geo::GeometryDocument_FindPatch( &f.map.geometry, { torus } ) ).box;
    CHECK( ring.maximum.x - ring.minimum.x == Catch::Approx( 128 ).margin( 1.0 ) );
    CHECK( ring.maximum.z - ring.minimum.z == Catch::Approx( 32 ).margin( 1.0 ) );
}

TEST_CASE( "Thicken adds a back patch behind each patch and seams along its open borders", "[map][object-create][patch-edit][thicken]" )
{
    fixture_t f;
    u64 plane = 0;
    REQUIRE( MapCreate_Patch( &f.map, map_patch_shape_t::PLANE, { { 0, 0, 0 }, { 128, 64, 0 } }, 2, 8, SV( "materials/dev/dev_grid.cymat" ), &plane ) == map_status_t::OK );
    const auto *front = geo::GeometryDocument_FindPatch( &f.map.geometry, { plane } ); REQUIRE( front );
    const auto normal = geo::Patch_Evaluate( front, 0.5, 0.5 ).normal;
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
    REQUIRE( MapPatchEdit_Thicken( &f.map, { &plane, 1 }, 16.0, true, &roots ) == map_status_t::OK );
    CHECK( roots.nCount == 1 + 1 + 4 ); // The source, its back, four seams.
    const auto *back = geo::GeometryDocument_FindPatch( &f.map.geometry, { roots.pData[1] } ); REQUIRE( back );
    const auto at = geo::Patch_Evaluate( back, 0.5, 0.5 );
    CHECK( at.position.z == Catch::Approx( -16.0 * normal.z ) ); // Behind the front face...
    CHECK( at.normal.z * normal.z < 0.0 );                       // ...facing the other way.
    for ( usize i = 1; i < roots.nCount; ++i ) { CHECK( f.map.nextId > roots.pData[i] ); }
    Vector_Clear( &roots );

    // A cylinder is closed around: only its two rims get seams.
    u64 tube = 0;
    REQUIRE( MapCreate_Patch( &f.map, map_patch_shape_t::CYLINDER, { { 300, 0, 0 }, { 428, 128, 128 } }, 2, 8, SV( "materials/dev/dev_grid.cymat" ), &tube ) == map_status_t::OK );
    REQUIRE( MapPatchEdit_Thicken( &f.map, { &tube, 1 }, 8.0, true, &roots ) == map_status_t::OK );
    CHECK( roots.nCount == 1 + 1 + 2 );
    Vector_Clear( &roots );
    REQUIRE( MapPatchEdit_Thicken( &f.map, { &tube, 1 }, 8.0, false, &roots ) == map_status_t::OK );
    CHECK( roots.nCount == 2 ); // Without seams: just the back.
    CHECK( MapPatchEdit_Thicken( &f.map, { &tube, 1 }, 0.0, true, &roots ) == map_status_t::INVALID_ARGUMENT );
    Vector_Shutdown( &roots );
}

TEST_CASE( "Sweep and lathe build closed solids; displacements build grids on a face", "[map][object-create][sweep]" )
{
    fixture_t f;
    const auto closedMesh = [&]( u64 id ) {
        const auto *mesh = geo::GeometryDocument_FindMesh( &f.map.geometry, { id } );
        REQUIRE( mesh != nullptr );
        return geo::MeshBoundary_CountBoundaryEdges( &mesh->mesh ) == 0;
    };
    SECTION( "A round tube swept along a smooth curve is closed at its ends" )
    {
        std::vector<math::vec3d_t> points{ { 0, 0, 0 }, { 128, 0, 0 }, { 256, 128, 0 }, { 256, 256, 64 } };
        std::vector<math::vec2d_t> profile;
        for ( int i = 0; i < 8; ++i ) { const f64 a = 2.0 * 3.14159265358979323846 * i / 8; profile.push_back( { 16 * std::cos( a ), 16 * std::sin( a ) } ); }
        u64 id = 0;
        REQUIRE( MapCreate_Sweep( &f.map, { points.data(), points.size() }, false, { profile.data(), profile.size() }, true, SV( "materials/dev/dev_grid.cymat" ), 128.0, &id ) == map_status_t::OK );
        CHECK( closedMesh( id ) );
    }
    SECTION( "A lathed profile touching the axis is a closed solid with poles" )
    {
        std::vector<math::vec2d_t> profile{ { 0, 0 }, { 48, 16 }, { 32, 64 }, { 40, 96 }, { 0, 128 } };
        u64 id = 0;
        REQUIRE( MapCreate_Lathe( &f.map, { profile.data(), profile.size() }, { 0, 0, 0 }, { 0, 0, 1 }, 2.0 * 3.14159265358979323846, 16,
                                  SV( "materials/dev/dev_grid.cymat" ), 128.0, &id ) == map_status_t::OK );
        CHECK( closedMesh( id ) );
        std::vector<math::vec2d_t> crossing{ { 16, 0 }, { -16, 64 } };
        CHECK( MapCreate_Lathe( &f.map, { crossing.data(), crossing.size() }, {}, { 0, 0, 1 }, 6.0, 16, SV( "x" ), 128.0, &id ) == map_status_t::INVALID_ARGUMENT );
    }
    SECTION( "Walls along an L-shaped path: one closed solid with a mitred corner; a closed room outline too" )
    {
        std::vector<math::vec3d_t> path{ { 0, 0, 0 }, { 256, 0, 0 }, { 256, 256, 0 } };
        u64 id = 0;
        REQUIRE( MapCreate_Walls( &f.map, { path.data(), path.size() }, { 0, 0, 1 }, false, 16.0, 128.0, map_wall_join_t::MITER, SV( "materials/dev/dev_grid.cymat" ), 128.0, &id ) == map_status_t::OK );
        CHECK( closedMesh( id ) );
        std::vector<math::vec3d_t> room{ { 0, 0, 0 }, { 512, 0, 0 }, { 512, 384, 0 }, { 0, 384, 0 } };
        REQUIRE( MapCreate_Walls( &f.map, { room.data(), room.size() }, { 0, 0, 1 }, true, 16.0, 128.0, map_wall_join_t::BEVEL, SV( "materials/dev/dev_grid.cymat" ), 128.0, &id ) == map_status_t::OK );
        CHECK( closedMesh( id ) );
    }
    SECTION( "A noisy power 2 displacement stands off its face" )
    {
        map_displacement_desc_t desc{};
        desc.corners[0] = { 0, 0, 0 }; desc.corners[1] = { 256, 0, 0 }; desc.corners[2] = { 256, 256, 0 }; desc.corners[3] = { 0, 256, 0 };
        desc.power = 2; desc.elevation = 8.0; desc.noiseAmplitude = 16.0; desc.seed = 7;
        u64 id = 0;
        REQUIRE( MapCreate_Displacement( &f.map, desc, SV( "materials/dev/dev_grid.cymat" ), 128.0, &id ) == map_status_t::OK );
        const auto *mesh = geo::GeometryDocument_FindMesh( &f.map.geometry, { id } ); REQUIRE( mesh != nullptr );
        CHECK( GenerationPool_Count( &mesh->mesh.vertices ) == 25u ); CHECK( GenerationPool_Count( &mesh->mesh.faces ) == 16u );
        desc.power = 5;
        CHECK( MapCreate_Displacement( &f.map, desc, SV( "x" ), 128.0, &id ) == map_status_t::INVALID_ARGUMENT );
    }
}

TEST_CASE( "Curve patches: a strip through clicked points, an arch pulled toward them, a lathe", "[map][object-create][patch][curve]" )
{
    fixture_t f;
    const std::vector<math::vec3d_t> path{ { 0, 0, 0 }, { 128, 0, 0 }, { 128, 128, 0 }, { 256, 128, 0 } };
    const auto patchOf = [&]( u64 id ) { const auto *patch = geo::GeometryDocument_FindPatch( &f.map.geometry, { id } ); REQUIRE( patch != nullptr ); return patch; };
    const auto control = []( const geo::patch_surface_t *patch, u32 column, u32 row ) { return patch->controls.pData[row * patch->cColumns + column].position; };
    u64 id = 0;
    SECTION( "Straight: one span per segment, a sharp corner on every point, extruded up" )
    {
        REQUIRE( MapCreate_PatchStrip( &f.map, { path.data(), path.size() }, false, false, { 0, 0, 64 }, SV( "materials/dev/dev_grid.cymat" ), &id ) == map_status_t::OK );
        const auto *patch = patchOf( id );
        CHECK( patch->cColumns == 7u ); CHECK( patch->cRows == 3u );
        for ( u32 i = 0; i < 4; ++i ) { CHECK( control( patch, 2 * i, 0 ).x == Catch::Approx( path[i].x ) ); CHECK( control( patch, 2 * i, 0 ).y == Catch::Approx( path[i].y ) ); }
        CHECK( control( patch, 0, 2 ).z == Catch::Approx( 64.0 ) );
    }
    SECTION( "Smooth: ends on the first and last point; closed loops wrap" )
    {
        REQUIRE( MapCreate_PatchStrip( &f.map, { path.data(), path.size() }, true, false, { 0, 0, 64 }, SV( "materials/dev/dev_grid.cymat" ), &id ) == map_status_t::OK );
        const auto *patch = patchOf( id );
        CHECK( patch->cColumns == 5u );
        CHECK( control( patch, 0, 0 ).x == Catch::Approx( 0.0 ) ); CHECK( control( patch, 4, 0 ).x == Catch::Approx( 256.0 ) );
        REQUIRE( MapCreate_PatchStrip( &f.map, { path.data(), path.size() }, true, true, { 0, 0, 64 }, SV( "materials/dev/dev_grid.cymat" ), &id ) == map_status_t::OK );
        patch = patchOf( id );
        CHECK( patch->cColumns == 9u );
        CHECK( control( patch, 0, 0 ).x == Catch::Approx( control( patch, 8, 0 ).x ) ); CHECK( control( patch, 0, 0 ).y == Catch::Approx( control( patch, 8, 0 ).y ) );
        CHECK( MapCreate_PatchStrip( &f.map, { path.data(), 2 }, true, true, { 0, 0, 64 }, SV( "x" ), &id ) == map_status_t::INVALID_ARGUMENT );
        CHECK( MapCreate_PatchStrip( &f.map, { path.data(), path.size() }, true, false, { 0, 0, 0 }, SV( "x" ), &id ) == map_status_t::INVALID_ARGUMENT );
    }
    SECTION( "Lathe: a full turn about the axis; profiles crossing the axis are refused" )
    {
        const std::vector<math::vec2d_t> profile{ { 32, 0 }, { 64, 64 }, { 16, 128 } };
        REQUIRE( MapCreate_PatchLathe( &f.map, { profile.data(), profile.size() }, true, { 0, 0, 0 }, { 0, 0, 1 }, 8, SV( "materials/dev/dev_grid.cymat" ), &id ) == map_status_t::OK );
        const auto bounds = MapGeometry_PatchBounds( *patchOf( id ) ).box;
        CHECK( bounds.maximum.z == Catch::Approx( 128.0 ) ); CHECK( bounds.minimum.z == Catch::Approx( 0.0 ) );
        CHECK( bounds.maximum.x > 32.0 ); CHECK( bounds.minimum.x < -32.0 );
        const std::vector<math::vec2d_t> crossing{ { 16, 0 }, { -16, 64 } };
        CHECK( MapCreate_PatchLathe( &f.map, { crossing.data(), crossing.size() }, true, {}, { 0, 0, 1 }, 8, SV( "x" ), &id ) == map_status_t::INVALID_ARGUMENT );
    }
    saved_t saved; REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded; saved.Load( loaded );
    CHECK( geo::GeometryDocument_FindPatch( &loaded.geometry, { id } ) != nullptr );
}

TEST_CASE( "Patch Cap closes a cylinder's open ends facing outward and leaves open patches alone", "[map][object-create][patch][patch-cap]" )
{
    fixture_t f;
    u64 tube = 0, plane = 0, disc = 0;
    REQUIRE( MapCreate_Patch( &f.map, map_patch_shape_t::CYLINDER, { { -64, -64, 0 }, { 64, 64, 128 } }, 2, 8, SV( "materials/dev/dev_grid.cymat" ), &tube ) == map_status_t::OK );
    REQUIRE( MapCreate_Patch( &f.map, map_patch_shape_t::PLANE, { { 200, 0, 0 }, { 328, 128, 0 } }, 2, 8, SV( "materials/dev/dev_grid.cymat" ), &plane ) == map_status_t::OK );
    REQUIRE( MapCreate_Patch( &f.map, map_patch_shape_t::DISC, { { 400, 0, 0 }, { 528, 128, 0 } }, 2, 8, SV( "materials/dev/dev_grid.cymat" ), &disc ) == map_status_t::OK );
    vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
    usize caps = 0;
    const auto revision = f.map.geometry.revision;
    const u64 open[]{ plane, disc };
    REQUIRE( MapPatchEdit_Cap( &f.map, { open, 2 }, &roots, &caps ) == map_status_t::OK );
    CHECK( caps == 0 ); CHECK( f.map.geometry.revision == revision );
    Vector_Clear( &roots );
    REQUIRE( MapPatchEdit_Cap( &f.map, { &tube, 1 }, &roots, &caps ) == map_status_t::OK );
    REQUIRE( caps == 2 ); REQUIRE( roots.nCount == 3 );
    const auto *source = geo::GeometryDocument_FindPatch( &f.map.geometry, { tube } ); REQUIRE( source != nullptr );
    for ( usize i = 1; i < roots.nCount; ++i ) {
        const auto *cap = geo::GeometryDocument_FindPatch( &f.map.geometry, { roots.pData[i] } ); REQUIRE( cap != nullptr );
        CHECK( cap->materialId == source->materialId );
        const auto bounds = MapGeometry_PatchBounds( *cap ).box;
        CHECK( bounds.maximum.z - bounds.minimum.z == Catch::Approx( 0 ).margin( 1e-6 ) ); // Flat, at one end.
        const auto sample = geo::Patch_Evaluate( cap, 0.25, 0.5 );
        REQUIRE( sample.bNormalValid );
        CHECK( sample.normal.z * ( bounds.minimum.z > 64 ? 1.0 : -1.0 ) > 0.9 ); // Facing out of the tube.
    }
    Vector_Shutdown( &roots );
}

TEST_CASE( "Hammer 5 block shapes: arch, stairs, spiral stairs, pipe and torus are closed meshes inside their box", "[map][object-create][block-shapes]" )
{
    fixture_t f;
    const auto check = [&]( map_primitive_desc_t desc, usize expectedFaces ) {
        u64 id = 0;
        REQUIRE( MapEdit_CreatePrimitive( &f.map, desc, SV( "materials/dev/dev_grid.cymat" ), {}, &id ) == map_status_t::OK );
        const auto *mesh = geo::GeometryDocument_FindMesh( &f.map.geometry, { id } ); REQUIRE( mesh != nullptr );
        CHECK( geo::MeshBoundary_CountBoundaryEdges( &mesh->mesh ) == 0 ); // Watertight.
        if ( expectedFaces != 0 ) { CHECK( GenerationPool_Count( &mesh->mesh.faces ) == expectedFaces ); }
        map_wireframe_t wire{}; REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
        REQUIRE( MapWireframe_Build( &wire, f.map ) == map_status_t::OK );
        const auto *object = MapWireframe_FindObject( wire, id ); REQUIRE( object != nullptr );
        const auto &b = object->bounds.box;
        CHECK( b.minimum.x >= desc.bounds.minimum.x - 1e-6 ); CHECK( b.maximum.x <= desc.bounds.maximum.x + 1e-6 );
        CHECK( b.minimum.z >= desc.bounds.minimum.z - 1e-6 ); CHECK( b.maximum.z <= desc.bounds.maximum.z + 1e-6 );
        // Every face points out: the mesh encloses positive volume.
        f64 volume = 0.0;
        for ( usize i = 0; i < wire.faces.nCount; ++i ) {
            const auto &face = wire.faces.pData[i];
            if ( face.id != id ) { continue; }
            const auto p0 = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex]];
            for ( u32 k = 1; k + 1 < face.nIndices; ++k ) {
                const auto p1 = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k]], p2 = wire.points.pData[wire.faceIndices.pData[face.iFirstIndex + k + 1]];
                volume += ( p0.x * ( p1.y * p2.z - p1.z * p2.y ) - p0.y * ( p1.x * p2.z - p1.z * p2.x ) + p0.z * ( p1.x * p2.y - p1.y * p2.x ) ) / 6.0;
            }
        }
        CHECK( volume > 0.0 );
        MapWireframe_Shutdown( &wire );
        return id;
    };
    map_primitive_desc_t desc{}; desc.bounds = { { 0, 0, 0 }, { 256, 256, 128 } }; desc.nSides = 12;
    desc.kind = map_primitive_kind_t::ARCH; desc.arcDegrees = 180.0; ( void )check( desc, 6 * 4 + 2 );
    desc.kind = map_primitive_kind_t::PIPE; ( void )check( desc, 12 * 4 );
    desc.kind = map_primitive_kind_t::TORUS; ( void )check( desc, 12 * 6 );
    desc.kind = map_primitive_kind_t::SPIRAL_STAIRS; desc.nSteps = 8; desc.turnDegrees = 270.0; ( void )check( desc, 8 * 6 );
    desc.kind = map_primitive_kind_t::STAIRS; desc.nSteps = 4; desc.stairsDirection = -2; // Rising toward -Y.
    const u64 stairs = check( desc, 2 * 4 + 2 + 4 * 2 ); // A side face per step column, bottom and back, treads and risers.
    ( void )stairs;
    desc.kind = map_primitive_kind_t::ARCH; desc.wallThickness = 200.0;
    u64 id = 0;
    CHECK( MapEdit_CreatePrimitive( &f.map, desc, SV( "materials/dev/dev_grid.cymat" ), {}, &id ) == map_status_t::INVALID_ARGUMENT ); // Thicker than the radius.
}
