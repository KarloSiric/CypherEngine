//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMap_Modifiers_Tests.cpp
//  Purpose: Tests mesh modifier stacks in the map record: read and write,
//           save and reload, evaluation, subdivision levels and Apply.
//
//  History:
//  - Created by Karlo Siric on 2026-10-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMap_Modifiers.h"
#include "CypherMap_Edit.h"
#include "CypherMap_MeshEdit.h"
#include "CypherMap_Wireframe.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherGeometry_DocumentMeshes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <cmath>
#include <string>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor::map;
namespace geo = cypher::editor::geometry;

namespace
{
string_view_t SV( const char *p ) { return StringView_FromCString( p ); }
struct fixture_t {
    map_document_t map{};
    fixture_t() { REQUIRE( MapDocument_Create( &map, Allocator_GetSystem(), { SV( "Modifiers fixture" ), SV( "reap" ), {} } ) == map_status_t::OK ); }
    u64 BoxMesh( math::aabbd_t bounds = { { -64, -64, 0 }, { 64, 64, 128 } } )
    {
        map_primitive_desc_t desc{}; desc.kind = map_primitive_kind_t::BOX; desc.bounds = bounds; desc.axis = 2;
        u64 id = 0;
        REQUIRE( MapEdit_CreatePrimitive( &map, desc, SV( "materials/dev/dev_grid.cymat" ), {}, &id ) == map_status_t::OK );
        vector_t<u64> roots{}; REQUIRE( Vector_Init( &roots, Allocator_GetSystem() ) );
        REQUIRE( MapMeshEdit_ConvertBrushes( &map, { &id, 1 }, &roots ) == map_status_t::OK );
        Vector_Shutdown( &roots );
        return id;
    }
    usize Faces( u64 mesh )
    {
        geo::mesh_source_description_t d{};
        REQUIRE( geo::MeshSourceDescription_Init( &d, Allocator_GetSystem(), { mesh } ) == geo::geometry_status_t::OK );
        REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &map.geometry, { mesh } ), &d ) == geo::geometry_status_t::OK );
        const usize n = d.faces.nCount;
        geo::MeshSourceDescription_Shutdown( &d );
        return n;
    }
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
};
} // namespace

TEST_CASE( "Subdivision levels live in the mesh's record, survive a save, and show in the wireframe", "[map][modifiers]" )
{
    fixture_t f;
    const u64 mesh = f.BoxMesh();
    CHECK( MapModifiers_SubdivisionLevel( &f.map, mesh ) == 0 );
    bool changed = false;
    const auto revision = f.map.geometry.revision;
    REQUIRE( MapModifiers_SetSubdivisionLevel( &f.map, mesh, 2, &changed ) == map_status_t::OK ); // An unsaved mesh: a record is made.
    CHECK( changed ); CHECK( f.map.geometry.revision != revision );
    CHECK( MapModifiers_SubdivisionLevel( &f.map, mesh ) == 2 );
    REQUIRE( MapModifiers_SetSubdivisionLevel( &f.map, mesh, 2, &changed ) == map_status_t::OK ); CHECK_FALSE( changed );
    CHECK( MapModifiers_SetSubdivisionLevel( &f.map, mesh, 6, &changed ) == map_status_t::INVALID_ARGUMENT );
    CHECK( f.Faces( mesh ) == 6 ); // The cage is untouched.

    // The wireframe: the cage's 8 points and 12 lines stay, the faces are the
    // smooth surface (6 * 4^2), each carrying its cage face.
    map_wireframe_t wire{};
    REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) );
    REQUIRE( MapWireframe_Build( &wire, f.map ) == map_status_t::OK );
    const auto *object = MapWireframe_FindObject( wire, mesh ); REQUIRE( object != nullptr );
    CHECK( object->nLines == 12 );
    usize faces = 0, pointsWithIds = 0;
    for ( usize i = 0; i < wire.faces.nCount; ++i ) { if ( wire.faces.pData[i].id == mesh ) { ++faces; CHECK( wire.faces.pData[i].faceId != 0 ); } }
    for ( u32 k = 0; k < object->nPoints; ++k ) { pointsWithIds += wire.pointSourceIds.pData[object->iFirstPoint + k] != 0; }
    CHECK( faces == 96 ); CHECK( pointsWithIds == 8 );
    MapWireframe_Shutdown( &wire );

    saved_t saved;
    REQUIRE( saved.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded{};
    REQUIRE( MapDocument_Load( &loaded, Allocator_GetSystem(), { saved.root.data(), saved.root.size() } ) == map_status_t::OK );
    CHECK( MapModifiers_SubdivisionLevel( &loaded, mesh ) == 2 );

    REQUIRE( MapModifiers_SetSubdivisionLevel( &f.map, mesh, 0, &changed ) == map_status_t::OK ); CHECK( changed );
    std::vector<geo::modifier_t> stages;
    REQUIRE( MapModifiers_Read( &f.map, mesh, stages ) == map_status_t::OK ); CHECK( stages.empty() );
}

TEST_CASE( "Apply bakes the stack into the mesh: mirror then subdivide", "[map][modifiers]" )
{
    fixture_t f;
    const u64 mesh = f.BoxMesh( { { 32, -64, 0 }, { 96, 64, 128 } } );
    geo::modifier_t mirror{}; mirror.kind = geo::modifier_kind_t::MIRROR; mirror.axis = 0; mirror.offset = 0.0;
    geo::modifier_t array{}; array.kind = geo::modifier_kind_t::LINEAR_ARRAY; array.cCopies = 2; array.step = { 0, 256, 0 };
    bool changed = false;
    REQUIRE( MapModifiers_Write( &f.map, mesh, { mirror, array }, &changed ) == map_status_t::OK );
    geo::modifier_result_t result{};
    REQUIRE( geo::ModifierResult_Init( &result, Allocator_GetSystem() ) == geo::geometry_status_t::OK );
    REQUIRE( MapModifiers_Evaluate( &f.map, mesh, &result ) == map_status_t::OK );
    CHECK( result.mesh.faces.nCount == 24 ); // Two mirrored boxes, arrayed twice.
    geo::ModifierResult_Shutdown( &result );
    REQUIRE( MapModifiers_Apply( &f.map, mesh ) == map_status_t::OK );
    CHECK( f.Faces( mesh ) == 24 );
    std::vector<geo::modifier_t> stages;
    REQUIRE( MapModifiers_Read( &f.map, mesh, stages ) == map_status_t::OK ); CHECK( stages.empty() );
    geo::modifier_t bad{}; bad.kind = geo::modifier_kind_t::TAPER; bad.alongAxis = 7;
    CHECK( MapModifiers_Write( &f.map, mesh, { bad }, &changed ) == map_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Modifiers are stored as CYMAP.md 6.4 writes them, and unknown stage types survive edits", "[map][modifiers][format]" )
{
    fixture_t f;
    const u64 mesh = f.BoxMesh();
    geo::modifier_t mirror{}; mirror.kind = geo::modifier_kind_t::MIRROR; mirror.axis = 1; mirror.offset = 256.0;
    geo::modifier_t radial{}; radial.kind = geo::modifier_kind_t::RADIAL_ARRAY; radial.cCopies = 4; radial.angle = 3.14159265358979323846;
    bool changed = false;
    REQUIRE( MapModifiers_Write( &f.map, mesh, { mirror, radial }, &changed ) == map_status_t::OK );
    map_layer_store_t *chunk = nullptr;
    key_value_t *record = MapDocument_FindObject( &f.map, mesh, &chunk ); REQUIRE( record != nullptr );
    key_value_t *modifiers = KeyValue_Find( record, SV( "modifiers" ) );
    REQUIRE( KeyValue_Type( modifiers ) == key_value_type_t::ARRAY ); REQUIRE( KeyValue_ChildCount( modifiers ) == 2 );
    string_view_t text{};
    REQUIRE( KeyValue_GetString( KeyValue_Find( KeyValue_ChildAt( modifiers, 0 ), SV( "axis" ) ), &text ) ); CHECK( std::string( text.pData, text.cchLength ) == "y" );
    f64 angle = 0.0;
    REQUIRE( KeyValue_GetF64( KeyValue_Find( KeyValue_ChildAt( modifiers, 1 ), SV( "angle" ) ), &angle ) ); CHECK( angle == Catch::Approx( 180.0 ) ); // Degrees.
    // A stage type from a newer editor, written by hand.
    auto *document = chunk->store.pDocument;
    key_value_t *future = KeyValue_ArrayAppend( document, modifiers, key_value_type_t::OBJECT ); REQUIRE( future != nullptr );
    key_value_t *type = KeyValue_ObjectInsert( document, future, SV( "type" ), key_value_type_t::STRING ); REQUIRE( type != nullptr );
    REQUIRE( KeyValue_SetString( document, type, SV( "lattice" ) ) );
    std::vector<geo::modifier_t> stages;
    REQUIRE( MapModifiers_Read( &f.map, mesh, stages ) == map_status_t::OK ); CHECK( stages.size() == 2 ); // Skipped when evaluating...
    stages.pop_back();
    REQUIRE( MapModifiers_Write( &f.map, mesh, stages, &changed ) == map_status_t::OK );
    modifiers = KeyValue_Find( MapDocument_FindObject( &f.map, mesh, nullptr ), SV( "modifiers" ) );
    REQUIRE( KeyValue_ChildCount( modifiers ) == 2 );                                                 // ...but kept in the file.
    REQUIRE( KeyValue_GetString( KeyValue_Find( KeyValue_ChildAt( modifiers, 1 ), SV( "type" ) ), &text ) ); CHECK( std::string( text.pData, text.cchLength ) == "lattice" );
}

namespace
{
struct modifier_surface_t {
    geo::modifier_result_t value{};
    modifier_surface_t() { REQUIRE( geo::ModifierResult_Init( &value, Allocator_GetSystem() ) == geo::geometry_status_t::OK ); }
    ~modifier_surface_t() { geo::ModifierResult_Shutdown( &value ); }
    void Read( const map_document_t &map, u64 mesh ) { REQUIRE( MapModifiers_Evaluate( &map, mesh, &value ) == map_status_t::OK ); }
};
struct modifier_transform_case_t {
    enum kind_t { TRANSLATE, TURN, SCALE, SHEAR } kind;
    math::vec3d_t amount{};
    math::vec3d_t pivot{ 11, -7, 3 };
    u32 axis{ 2 };
};
math::vec3d_t ModifierExpectedPoint( math::vec3d_t point, const modifier_transform_case_t &edit )
{
    if ( edit.kind == modifier_transform_case_t::TRANSLATE ) { return math::Vec3d_Add( point, edit.amount ); }
    auto p = math::Vec3d_Subtract( point, edit.pivot );
    if ( edit.kind == modifier_transform_case_t::SCALE ) { p = { p.x * edit.amount.x, p.y * edit.amount.y, p.z * edit.amount.z }; }
    else if ( edit.kind == modifier_transform_case_t::SHEAR ) {
        const f64 along = edit.axis == 0 ? p.x : edit.axis == 1 ? p.y : p.z;
        p = math::Vec3d_Add( p, math::Vec3d_Scale( edit.amount, along ) );
    } else {
        // Independent XYZ Euler oracle, rather than using the production affine.
        constexpr double radians = 3.14159265358979323846 / 180;
        auto x = edit.amount.x * radians; p = { p.x, p.y * std::cos( x ) - p.z * std::sin( x ), p.y * std::sin( x ) + p.z * std::cos( x ) };
        auto y = edit.amount.y * radians; p = { p.x * std::cos( y ) + p.z * std::sin( y ), p.y, -p.x * std::sin( y ) + p.z * std::cos( y ) };
        auto z = edit.amount.z * radians; p = { p.x * std::cos( z ) - p.y * std::sin( z ), p.x * std::sin( z ) + p.y * std::cos( z ), p.z };
    }
    return math::Vec3d_Add( p, edit.pivot );
}
map_status_t ModifierEdit( map_document_t &map, span_t<const u64> ids, const modifier_transform_case_t &edit )
{
    switch ( edit.kind ) {
        case modifier_transform_case_t::TRANSLATE: return MapEdit_Translate( &map, ids, edit.amount, true );
        case modifier_transform_case_t::TURN: return MapEdit_Rotate( &map, ids, edit.amount, edit.pivot, true );
        case modifier_transform_case_t::SCALE: return MapEdit_Scale( &map, ids, edit.amount, edit.pivot, true );
        case modifier_transform_case_t::SHEAR: return MapEdit_Shear( &map, ids, edit.axis, edit.amount, edit.pivot, true );
    }
    return map_status_t::INVALID_ARGUMENT;
}
geo::modifier_t ModifierStage( geo::modifier_kind_t kind )
{
    geo::modifier_t stage{}; stage.kind = kind; stage.axis = 2; stage.alongAxis = 0;
    stage.origin = { 2, -3, 5 }; stage.factor = .5;
    stage.angle = kind == geo::modifier_kind_t::RADIAL_ARRAY ? 3.14159265358979323846 : .6;
    stage.cCopies = 3; stage.step = { 96, 7, 3 }; stage.direction = { 0, 0, 1 };
    if ( kind == geo::modifier_kind_t::MIRROR ) { stage.axis = 0; stage.offset = -16; }
    if ( kind == geo::modifier_kind_t::TAPER ) { stage.alongAxis = 2; }
    stage.subdivision.cLevels = 1; stage.subdivision.scheme = geo::subdivision_scheme_t::LINEAR;
    return stage;
}
void CheckModifierSurface( const geo::modifier_result_t &before, const geo::modifier_result_t &after,
    const modifier_transform_case_t &edit )
{
    REQUIRE( after.mesh.vertices.nCount == before.mesh.vertices.nCount );
    REQUIRE( after.mesh.faces.nCount == before.mesh.faces.nCount );
    REQUIRE( after.mesh.corners.nCount == before.mesh.corners.nCount );
    for ( usize i = 0; i < before.mesh.vertices.nCount; ++i ) {
        CAPTURE( i );
        CHECK( after.vertexCopies.pData[i] == before.vertexCopies.pData[i] );
        CHECK( after.vertexSources.pData[i] == before.vertexSources.pData[i] );
        const auto expected = ModifierExpectedPoint( before.mesh.vertices.pData[i].position, edit );
        const auto actual = after.mesh.vertices.pData[i].position;
        CHECK( actual.x == Catch::Approx( expected.x ).margin( 1e-8 ) );
        CHECK( actual.y == Catch::Approx( expected.y ).margin( 1e-8 ) );
        CHECK( actual.z == Catch::Approx( expected.z ).margin( 1e-8 ) );
    }
    for ( usize i = 0; i < before.mesh.faces.nCount; ++i ) {
        const auto &a = before.mesh.faces.pData[i]; const auto &b = after.mesh.faces.pData[i];
        CHECK( after.faceSources.pData[i] == before.faceSources.pData[i] ); CHECK( after.faceCopies.pData[i] == before.faceCopies.pData[i] );
        CHECK( a.cCorners == b.cCorners ); CHECK( a.iFirstCorner == b.iFirstCorner );
        CHECK( a.attributes.material.value == b.attributes.material.value ); CHECK( a.attributes.smoothingGroups == b.attributes.smoothingGroups );
    }
    for ( usize i = 0; i < before.mesh.corners.nCount; ++i ) {
        const auto &a = before.mesh.corners.pData[i]; const auto &b = after.mesh.corners.pData[i];
        CHECK( a.iVertex == b.iVertex );
        CHECK( a.attributes.uv0.x == b.attributes.uv0.x ); CHECK( a.attributes.uv0.y == b.attributes.uv0.y );
        CHECK( a.attributes.uv1.x == b.attributes.uv1.x ); CHECK( a.attributes.uv1.y == b.attributes.uv1.y );
        CHECK( a.attributes.colorRgba == b.attributes.colorRgba );
    }
}
}

TEST_CASE( "Representable root transforms preserve evaluated modifier shapes and corner attributes", "[map][modifiers][modifier-transform-safety]" )
{
    using edit_t = modifier_transform_case_t;
    for ( const auto kind : { geo::modifier_kind_t::MIRROR, geo::modifier_kind_t::BEND, geo::modifier_kind_t::TAPER,
                             geo::modifier_kind_t::LINEAR_ARRAY, geo::modifier_kind_t::RADIAL_ARRAY, geo::modifier_kind_t::SUBDIVIDE } ) {
        std::vector<edit_t> edits{ { edit_t::TRANSLATE, { 100, -35, 12 } }, { edit_t::TURN, { 0, 0, 90 } },
            { edit_t::TURN, { 0, 0, -90 } }, { edit_t::TURN, { 0, 0, 180 } }, { edit_t::SCALE, { 2, 2, 2 } } };
        if ( kind == geo::modifier_kind_t::TAPER || kind == geo::modifier_kind_t::RADIAL_ARRAY || kind == geo::modifier_kind_t::SUBDIVIDE ) {
            edits.push_back( { edit_t::TURN, { 0, 0, 15 } } ); edits.push_back( { edit_t::TURN, { 0, 0, 45 } } );
        }
        if ( kind == geo::modifier_kind_t::BEND ) {
            edits.push_back( { edit_t::TURN, { 90, 0, 0 } } ); edits.push_back( { edit_t::TURN, { -90, 0, 0 } } );
            edits.push_back( { edit_t::TURN, { 0, 90, 0 } } ); edits.push_back( { edit_t::SCALE, { 2, 2, .5 } } );
        }
        if ( kind == geo::modifier_kind_t::TAPER ) {
            edits.push_back( { edit_t::TURN, { 0, 90, 0 } } ); edits.push_back( { edit_t::SCALE, { 2, .5, 3 } } );
            edits.push_back( { edit_t::SHEAR, { .25, 0, 0 }, { 11, -7, 3 }, 1 } );
        }
        for ( const auto &edit : edits ) {
            CAPTURE( static_cast<int>( kind ), static_cast<int>( edit.kind ), edit.amount.x, edit.amount.y, edit.amount.z );
            fixture_t f; const u64 mesh = f.BoxMesh( { { 8, 4, 0 }, { 24, 20, 64 } } );
            bool changed = false; REQUIRE( MapModifiers_Write( &f.map, mesh, { ModifierStage( kind ) }, &changed ) == map_status_t::OK );
            modifier_surface_t before; before.Read( f.map, mesh );
            REQUIRE( ModifierEdit( f.map, { &mesh, 1 }, edit ) == map_status_t::OK );
            modifier_surface_t after; after.Read( f.map, mesh ); CheckModifierSurface( before.value, after.value, edit );
            // Composition catches a remapped stage that only works at the first pivot.
            const edit_t move{ edit_t::TRANSLATE, { -13, 29, 7 } };
            REQUIRE( ModifierEdit( f.map, { &mesh, 1 }, move ) == map_status_t::OK );
            modifier_surface_t moved; moved.Read( f.map, mesh ); CheckModifierSurface( after.value, moved.value, move );
        }
    }
}

TEST_CASE( "Unrepresentable modifier transforms refuse before any selected object is moved", "[map][modifiers][modifier-transform-safety]" )
{
    using edit_t = modifier_transform_case_t;
    struct refusal_t { geo::modifier_kind_t kind; edit_t edit; };
    const refusal_t refusals[]{
        { geo::modifier_kind_t::MIRROR, { edit_t::TURN, { 0, 0, 45 } } },
        { geo::modifier_kind_t::BEND, { edit_t::TURN, { 0, 0, 15 } } },
        { geo::modifier_kind_t::BEND, { edit_t::SCALE, { 2, 1, 1 } } },
        { geo::modifier_kind_t::BEND, { edit_t::SHEAR, { .25, 0, 0 } } },
        { geo::modifier_kind_t::TAPER, { edit_t::TURN, { 0, 180, 0 } } },
        { geo::modifier_kind_t::TAPER, { edit_t::SHEAR, { .25, 0, 0 } } },
        { geo::modifier_kind_t::RADIAL_ARRAY, { edit_t::SCALE, { 2, 1, 1 } } },
        { geo::modifier_kind_t::RADIAL_ARRAY, { edit_t::SHEAR, { .25, 0, 0 } } },
        { geo::modifier_kind_t::LINEAR_ARRAY, { edit_t::SCALE, { 2, 1, 1 } } },
        { geo::modifier_kind_t::MIRROR, { edit_t::SCALE, { 2, 1, 1 } } }
    };
    for ( const auto &test : refusals ) { for ( const bool enabled : { true, false } ) {
        CAPTURE( static_cast<int>( test.kind ), static_cast<int>( test.edit.kind ), enabled );
        fixture_t f; const u64 first = f.BoxMesh( { { -128, 0, 0 }, { -96, 32, 32 } } );
        const u64 target = f.BoxMesh( { { 8, 4, 0 }, { 24, 20, 64 } } );
        auto stage = ModifierStage( test.kind ); stage.bEnabled = enabled;
        bool changed = false; REQUIRE( MapModifiers_Write( &f.map, target, { stage }, &changed ) == map_status_t::OK );
        saved_t before; REQUIRE( before.Save( &f.map ) == map_status_t::OK );
        const auto revision = f.map.geometry.revision, nextId = f.map.nextId, partNext = f.map.geometry.sourceIds.allocator.next.value;
        const u64 ids[]{ first, target };
        REQUIRE( ModifierEdit( f.map, { ids, 2 }, test.edit ) == map_status_t::MODIFIER_TRANSFORM_UNSUPPORTED );
        saved_t after; REQUIRE( after.Save( &f.map ) == map_status_t::OK ); CHECK( after.root == before.root );
        CHECK( f.map.geometry.revision == revision ); CHECK( f.map.nextId == nextId ); CHECK( f.map.geometry.sourceIds.allocator.next.value == partNext );
    } }
}

TEST_CASE( "Disabled taper follows transforms and retains its origin through save and reenable", "[map][modifiers][modifier-transform-safety][persistence]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh( { { -16, -8, 0 }, { 16, 8, 64 } } );
    auto taper = ModifierStage( geo::modifier_kind_t::TAPER ); bool changed = false;
    REQUIRE( MapModifiers_Write( &f.map, mesh, { taper }, &changed ) == map_status_t::OK );
    modifier_surface_t before; before.Read( f.map, mesh );
    taper.bEnabled = false; REQUIRE( MapModifiers_Write( &f.map, mesh, { taper }, &changed ) == map_status_t::OK );
    const modifier_transform_case_t move{ modifier_transform_case_t::TRANSLATE, { 100, -35, 12 } };
    REQUIRE( ModifierEdit( f.map, { &mesh, 1 }, move ) == map_status_t::OK );
    saved_t text; REQUIRE( text.Save( &f.map ) == map_status_t::OK );
    map_document_t loaded{}; REQUIRE( MapDocument_Load( &loaded, Allocator_GetSystem(), { text.root.data(), text.root.size() } ) == map_status_t::OK );
    std::vector<geo::modifier_t> stages; REQUIRE( MapModifiers_Read( &loaded, mesh, stages ) == map_status_t::OK ); REQUIRE( stages.size() == 1 );
    CHECK_FALSE( stages[0].bEnabled ); CHECK( stages[0].origin.x == 102 ); CHECK( stages[0].origin.y == -38 ); CHECK( stages[0].origin.z == 17 );
    stages[0].bEnabled = true; REQUIRE( MapModifiers_Write( &loaded, mesh, stages, &changed ) == map_status_t::OK );
    modifier_surface_t after; after.Read( loaded, mesh ); CheckModifierSurface( before.value, after.value, move );
}

TEST_CASE( "Unknown and unsupported serialized stages refuse root transforms without changing retained records", "[map][modifiers][modifier-transform-safety][format]" )
{
    for ( const bool unknown : { true, false } ) { for ( const bool enabled : { true, false } ) {
        CAPTURE( unknown, enabled ); fixture_t f; const u64 mesh = f.BoxMesh(); bool changed = false;
        REQUIRE( MapModifiers_Write( &f.map, mesh, { ModifierStage( geo::modifier_kind_t::MIRROR ) }, &changed ) == map_status_t::OK );
        map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( &f.map, mesh, &store ); REQUIRE( store != nullptr );
        auto *list = KeyValue_Find( record, SV( "modifiers" ) ); auto *stage = KeyValue_ArrayAppend( store->store.pDocument, list, key_value_type_t::OBJECT ); REQUIRE( stage != nullptr );
        auto *type = KeyValue_ObjectInsert( store->store.pDocument, stage, SV( "type" ), key_value_type_t::STRING ); REQUIRE( type != nullptr );
        REQUIRE( KeyValue_SetString( store->store.pDocument, type, SV( unknown ? "lattice" : "subdivide" ) ) );
        auto *flag = KeyValue_ObjectInsert( store->store.pDocument, stage, SV( "enabled" ), key_value_type_t::BOOL ); REQUIRE( flag != nullptr );
        REQUIRE( KeyValue_SetBool( store->store.pDocument, flag, enabled ? CY_TRUE : CY_FALSE ) );
        auto *payload = KeyValue_ObjectInsert( store->store.pDocument, stage, SV( unknown ? "future_payload" : "scheme" ), key_value_type_t::STRING ); REQUIRE( payload != nullptr );
        REQUIRE( KeyValue_SetString( store->store.pDocument, payload, SV( unknown ? "preserve exactly" : "loop" ) ) );
        saved_t before; REQUIRE( before.Save( &f.map ) == map_status_t::OK ); const auto revision = f.map.geometry.revision;
        REQUIRE( MapEdit_Translate( &f.map, { &mesh, 1 }, { 100, 0, 0 }, true ) == map_status_t::MODIFIER_TRANSFORM_UNSUPPORTED );
        saved_t after; REQUIRE( after.Save( &f.map ) == map_status_t::OK ); CHECK( after.root == before.root ); CHECK( f.map.geometry.revision == revision );
        REQUIRE( MapModifiers_Transform( &f.map, mesh, math::CY_AFFINE3D_IDENTITY ) == map_status_t::OK );
        REQUIRE( after.Save( &f.map ) == map_status_t::OK ); CHECK( after.root == before.root );
    } }
}

TEST_CASE( "Root transforms retain custom members on known modifier stages", "[map][modifiers][modifier-transform-safety][format]" )
{
    fixture_t f; const u64 mesh = f.BoxMesh(); bool changed = false;
    REQUIRE( MapModifiers_Write( &f.map, mesh, { ModifierStage( geo::modifier_kind_t::TAPER ) }, &changed ) == map_status_t::OK );
    map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( &f.map, mesh, &store ); REQUIRE( store != nullptr );
    auto *stage = KeyValue_ChildAt( KeyValue_Find( record, SV( "modifiers" ) ), 0 ); REQUIRE( stage != nullptr );
    auto *custom = KeyValue_ObjectInsert( store->store.pDocument, stage, SV( "future_settings" ), key_value_type_t::OBJECT ); REQUIRE( custom != nullptr );
    auto *note = KeyValue_ObjectInsert( store->store.pDocument, custom, SV( "note" ), key_value_type_t::STRING ); REQUIRE( note != nullptr );
    REQUIRE( KeyValue_SetString( store->store.pDocument, note, SV( "keep this nested metadata" ) ) );
    REQUIRE( KeyValue_SetComments( store->store.pDocument, stage, { SV( "// Authored taper" ), {}, {} } ) );
    REQUIRE( MapEdit_Translate( &f.map, { &mesh, 1 }, { 100, -35, 12 }, true ) == map_status_t::OK );
    stage = KeyValue_ChildAt( KeyValue_Find( MapDocument_FindObject( &f.map, mesh, nullptr ), SV( "modifiers" ) ), 0 );
    string_view_t text{};
    REQUIRE( KeyValue_GetString( KeyValue_Find( KeyValue_Find( stage, SV( "future_settings" ) ), SV( "note" ) ), &text ) );
    CHECK( StringView_Equals( text, SV( "keep this nested metadata" ) ) );
    CHECK( StringView_Equals( KeyValue_Comments( stage ).leading, SV( "// Authored taper" ) ) );
    std::vector<geo::modifier_t> stages; REQUIRE( MapModifiers_Read( &f.map, mesh, stages ) == map_status_t::OK ); REQUIRE( stages.size() == 1 );
    CHECK( stages[0].origin.x == 102 ); CHECK( stages[0].origin.y == -38 ); CHECK( stages[0].origin.z == 17 );
}

TEST_CASE( "An over-limit retained modifier stack is refused before transforming any batch root", "[map][modifiers][modifier-transform-safety][format]" )
{
    fixture_t f; const u64 ordinary = f.BoxMesh( { { -128, 0, 0 }, { -96, 32, 32 } } );
    const u64 mesh = f.BoxMesh(); bool changed = false;
    REQUIRE( MapModifiers_Write( &f.map, mesh, { ModifierStage( geo::modifier_kind_t::MIRROR ) }, &changed ) == map_status_t::OK );
    map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( &f.map, mesh, &store ); REQUIRE( store != nullptr );
    auto *list = KeyValue_Find( record, SV( "modifiers" ) ); auto *stage = KeyValue_ChildAt( list, 0 ); REQUIRE( stage != nullptr );
    for ( u32 i = 1; i <= geo::kModifierStagesMax; ++i ) { REQUIRE( KeyValue_CloneInto( store->store.pDocument, list, stage ) != nullptr ); }
    REQUIRE( KeyValue_ChildCount( list ) == geo::kModifierStagesMax + 1 );
    const auto revision = f.map.geometry.revision, nextId = f.map.nextId;
    saved_t before; REQUIRE( before.Save( &f.map ) == map_status_t::OK );
    const u64 roots[]{ ordinary, mesh };
    REQUIRE( MapEdit_Translate( &f.map, { roots, 2 }, { 100, 0, 0 }, true ) == map_status_t::LIMIT_EXCEEDED );
    saved_t after; REQUIRE( after.Save( &f.map ) == map_status_t::OK );
    CHECK( after.root == before.root ); CHECK( f.map.geometry.revision == revision ); CHECK( f.map.nextId == nextId );
}
