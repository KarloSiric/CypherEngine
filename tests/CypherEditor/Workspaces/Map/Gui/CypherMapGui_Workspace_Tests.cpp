//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Workspace_Tests.cpp
//  Purpose: Contract tests for the Map workspace session and its commands.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Workspace.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherGeometry_DocumentMeshes.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTemporaryDir>

#include <filesystem>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <algorithm>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{

QApplication *App()
{
    return qobject_cast<QApplication *>( QCoreApplication::instance() );
}

QString ExampleRoot()
{
    return QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
}

struct change_log_t {
    u32 all{ 0u };
    int nCalls{ 0 };
};

void Record( void *pContext, u32 changes ) noexcept
{
    auto *pLog = static_cast<change_log_t *>( pContext );
    pLog->all |= changes;
    ++pLog->nCalls;
}

command_result_t Run( const gui::editor_gui_t &gui, const char *pLine )
{
    return EditorCommands_ExecuteLine( &gui.commands, StringView_FromCString( pLine ) );
}

} // namespace

namespace
{
struct edge_session_t {
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
    explicit edge_session_t( const allocator_t *allocator = Allocator_GetSystem() )
    {
        REQUIRE( gui::EditorGui_Init( &gui, App(), allocator ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        REQUIRE( MapWorkspace_RegisterCommands( &workspace, &gui.commands ) == command_registry_status_t::OK );
    }
    ~edge_session_t() { MapWorkspace_Shutdown( &workspace ); gui::EditorGui_Shutdown( &gui ); }
};

u64 AddEdgeTestMesh( map_workspace_t &workspace, f64 x = 0 )
{
    map_bounds_t bounds{}; bounds.bHas = CY_TRUE;
    bounds.box = { { x, 0, 0 }, { x + 64, 64, 64 } };
    REQUIRE( MapWorkspace_CreateBox( &workspace, bounds ) );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &workspace ) );
    REQUIRE( workspace.selection.ids.nCount == 1 );
    return workspace.selection.ids.pData[0];
}

std::vector<geometry::mesh_edge_ref_t> SourceEdges( const map_workspace_t &workspace, u64 object )
{
    const auto *mesh = geometry::GeometryDocument_FindMesh( &workspace.pDocument->geometry, { object } ); REQUIRE( mesh != nullptr );
    geometry::mesh_source_description_t description{};
    REQUIRE( geometry::MeshSourceDescription_Init( &description, Allocator_GetSystem(), { object } ) == geometry::geometry_status_t::OK );
    REQUIRE( geometry::MeshSource_TryDescribe( mesh, &description ) == geometry::geometry_status_t::OK );
    std::vector<geometry::mesh_edge_ref_t> edges;
    for ( usize f = 0; f < description.faces.nCount; ++f ) {
        const auto face = description.faces.pData[f];
        for ( u32 i = 0; i < face.cCorners; ++i ) {
            const auto a = description.corners.pData[face.iFirstCorner + i].iVertex;
            const auto b = description.corners.pData[face.iFirstCorner + ( i + 1 ) % face.cCorners].iVertex;
            const auto edge = geometry::MeshEdgeRef_Make( description.vertices.pData[a].sourceId, description.vertices.pData[b].sourceId );
            if ( std::none_of( edges.begin(), edges.end(), [&]( auto candidate ) { return candidate.a.value == edge.a.value && candidate.b.value == edge.b.value; } ) ) {
                edges.push_back( edge );
            }
        }
    }
    REQUIRE( edges.size() == 12 );
    return edges;
}

std::vector<geometry::geometry_source_id_t> SourceVertices( const map_workspace_t &workspace, u64 object )
{
    const auto *mesh = geometry::GeometryDocument_FindMesh( &workspace.pDocument->geometry, { object } ); REQUIRE( mesh != nullptr );
    geometry::mesh_source_description_t description{};
    REQUIRE( geometry::MeshSourceDescription_Init( &description, Allocator_GetSystem(), { object } ) == geometry::geometry_status_t::OK );
    REQUIRE( geometry::MeshSource_TryDescribe( mesh, &description ) == geometry::geometry_status_t::OK );
    std::vector<geometry::geometry_source_id_t> vertices;
    for ( usize i = 0; i < description.vertices.nCount; ++i ) { vertices.push_back( description.vertices.pData[i].sourceId ); }
    std::sort( vertices.begin(), vertices.end(), []( auto a, auto b ) { return a.value < b.value; } );
    REQUIRE( vertices.size() == 8 );
    return vertices;
}

struct edge_allocation_t { usize calls{}, failOn{}, live{}; };
void *AllocateEdgeTest( void *context, usize bytes, usize alignment ) noexcept
{
    auto &audit = *static_cast<edge_allocation_t *>( context );
    if ( ++audit.calls == audit.failOn ) { return nullptr; }
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment );
    if ( memory != nullptr ) { ++audit.live; }
    return memory;
}
void FreeEdgeTest( void *context, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory != nullptr ) { --static_cast<edge_allocation_t *>( context )->live; }
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}

struct camera_scope_t {
    settings_registry_t *registry{};
    settings_scope_t scope{};
    settings_document_t store{};
    camera_scope_t( settings_registry_t &settings, settings_scope_t target, const char *body = "{}", const allocator_t *allocator = Allocator_GetSystem() )
        : registry( &settings ), scope( target )
    {
        REQUIRE( SettingsDocument_Init( &store, allocator, EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        const std::string text = std::string( "@cykv 1\n@schema \"cypher.settings\" 2\n" ) + body + "\n";
        REQUIRE( SettingsDocument_Load( &store, { text.data(), text.size() } ).status == settings_document_status_t::OK );
        EditorSettings_SetScope( registry, scope, &store );
    }
    ~camera_scope_t() { EditorSettings_SetScope( registry, scope, nullptr ); }
};

std::string SettingsText( const settings_document_t &store )
{
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( SettingsDocument_Write( &store, &text ) == settings_document_status_t::OK );
    return { text.pData, text.cchLength };
}

f64 CameraSpeed( const settings_registry_t &settings ) { return EditorSettings_Real( &settings, "editor.camera.move_speed", -1.0 ); }

void WriteCameraSpeed( settings_registry_t &settings, settings_scope_t scope, f64 speed )
{
    const auto *descriptor = EditorSettings_Find( &settings, StringView_FromCString( "editor.camera.move_speed" ) ); REQUIRE( descriptor != nullptr );
    setting_value_t value{}; value.type = setting_type_t::REAL; value.flValue = speed;
    REQUIRE( EditorSettings_Write( &settings, scope, *descriptor, value ) == settings_registry_status_t::OK );
}

struct camera_change_log_t { const settings_registry_t *registry{}; usize calls{}; bool exact{ true }; f64 speed{}; };
void RecordCameraSpeed( void *context, string_view_t path ) noexcept
{
    auto &log = *static_cast<camera_change_log_t *>( context ); ++log.calls;
    log.exact = log.exact && StringView_Equals( path, StringView_FromCString( "editor.camera.move_speed" ) );
    log.speed = CameraSpeed( *log.registry );
}
}

TEST_CASE( "Camera speed commands scale clamp and reset through the narrowest settings scope", "[map][gui][workspace][camera-speed]" )
{
    edge_session_t session; auto &settings = session.gui.settings; auto &ws = session.workspace;
    camera_scope_t user( settings, settings_scope_t::USER, "{ editor = { camera = { move_speed = 1500.0 } } }" );
    camera_scope_t project( settings, settings_scope_t::PROJECT );
    camera_scope_t local( settings, settings_scope_t::WORKSPACE, "{ future = { name = \"preserve me\" } }" );
    const auto userText = SettingsText( user.store ), projectText = SettingsText( project.store );
    const auto *storeAddress = settings.scopes[0];
    camera_change_log_t log{ &settings }; REQUIRE( EditorSettings_AddListener( &settings, &RecordCameraSpeed, &log ) );
    REQUIRE( Run( session.gui, "map.camera.speed_increase" ) == command_result_t::OK );
    CHECK( CameraSpeed( settings ) == 3000 ); CHECK( log.calls == 1 ); CHECK( log.exact ); CHECK( log.speed == 3000 );
    CHECK( settings.scopes[0] == storeAddress ); CHECK( SettingsText( user.store ) == userText ); CHECK( SettingsText( project.store ) == projectText );
    CHECK( SettingsText( local.store ).find( "preserve me" ) != std::string::npos );
    REQUIRE( Run( session.gui, "map.camera.speed_decrease" ) == command_result_t::OK ); CHECK( CameraSpeed( settings ) == 1500 );
    const auto *descriptor = EditorSettings_Find( &settings, StringView_FromCString( "editor.camera.move_speed" ) ); REQUIRE( descriptor != nullptr );
    setting_value_t raw{}; CHECK( Setting_Read( SettingsDocument_Root( &local.store ), *descriptor, &raw, nullptr ) == setting_read_status_t::ABSENT );
    // Reset writes the descriptor default even when the wider user scope differs.
    REQUIRE( Run( session.gui, "map.camera.speed_reset" ) == command_result_t::OK ); CHECK( CameraSpeed( settings ) == descriptor->flDefault );
    CHECK( Run( session.gui, "map.camera.speed_reset" ) == command_result_t::DISABLED );
    WriteCameraSpeed( settings, settings_scope_t::WORKSPACE, 75000 );
    REQUIRE( Run( session.gui, "map.camera.speed_increase" ) == command_result_t::OK ); CHECK( CameraSpeed( settings ) == descriptor->flMax );
    CHECK_FALSE( MapWorkspace_CanChangeCameraSpeed( &ws, map_camera_speed_action_t::INCREASE ) );
    CHECK( Run( session.gui, "map.camera.speed_increase" ) == command_result_t::DISABLED );
    WriteCameraSpeed( settings, settings_scope_t::WORKSPACE, 15 );
    REQUIRE( Run( session.gui, "map.camera.speed_decrease" ) == command_result_t::OK ); CHECK( CameraSpeed( settings ) == descriptor->flMin );
    CHECK( Run( session.gui, "map.camera.speed_decrease" ) == command_result_t::DISABLED );
    REQUIRE( MapWorkspace_ChangeCameraSpeed( &ws, map_camera_speed_action_t::RESET ) ); CHECK( CameraSpeed( settings ) == 1000 );
    CHECK( log.exact ); EditorSettings_RemoveListener( &settings, &RecordCameraSpeed, &log );
    settings_document_t reopened{}; REQUIRE( SettingsDocument_Init( &reopened, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    const auto saved = SettingsText( local.store ); REQUIRE( SettingsDocument_Load( &reopened, { saved.data(), saved.size() } ).status == settings_document_status_t::OK );
    REQUIRE( Setting_Read( SettingsDocument_Root( &reopened ), *descriptor, &raw, nullptr ) == setting_read_status_t::VALUE ); CHECK( raw.flValue == 1000 );
}

TEST_CASE( "Camera speed remains independent of map metadata selection history and editability", "[map][gui][workspace][camera-speed]" )
{
    edge_session_t session; auto &ws = session.workspace; auto &settings = session.gui.settings;
    const auto object = AddEdgeTestMesh( ws ); const auto vertices = SourceVertices( ws, object );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[0] ) );
    ws.pDocument->bReadOnly = CY_TRUE;
    camera_scope_t user( settings, settings_scope_t::USER );
    const auto *document = ws.pDocument; const auto *metadata = document->root.pDocument;
    const auto metadataText = SettingsText( document->root ); const auto geometryRevision = document->geometry.revision;
    const auto nextId = document->nextId, selectionRevision = ws.selection.revision; const auto *roots = ws.selection.ids.pData;
    const auto *components = ws.meshSelection.vertices.pData; const auto *points = ws.wire.points.pData; const auto *undo = ws.history.pUndo;
    const auto steps = EditorHistory_StepCount( &ws.history ); const auto modified = MapWorkspace_IsModified( &ws );
    change_log_t log{}; REQUIRE( MapWorkspace_AddListener( &ws, &Record, &log ) );
    REQUIRE( Run( session.gui, "map.camera.speed_increase" ) == command_result_t::OK );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->root.pDocument == metadata ); CHECK( SettingsText( document->root ) == metadataText );
    CHECK( document->geometry.revision == geometryRevision ); CHECK( document->nextId == nextId ); CHECK( document->bReadOnly );
    CHECK( ws.selection.ids.pData == roots ); CHECK( ws.selection.revision == selectionRevision ); CHECK( ws.meshSelection.vertices.pData == components );
    CHECK( MapWorkspace_HasMeshVertices( &ws ) ); CHECK( ws.wire.points.pData == points ); CHECK( ws.history.pUndo == undo );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( MapWorkspace_IsModified( &ws ) == modified ); CHECK( log.nCalls == 0 );
    // Navigation preferences also remain available when a host has no active map.
    auto *ownedDocument = ws.pDocument; ws.pDocument = nullptr;
    CHECK( MapWorkspace_CanChangeCameraSpeed( &ws, map_camera_speed_action_t::DECREASE ) );
    CHECK( MapWorkspace_ChangeCameraSpeed( &ws, map_camera_speed_action_t::DECREASE ) ); ws.pDocument = ownedDocument;
    CHECK( CameraSpeed( settings ) == 1000 ); CHECK( log.nCalls == 0 ); MapWorkspace_RemoveListener( &ws, &Record, &log );
}

TEST_CASE( "Camera speed notification retains staged clipping without a map change", "[map][gui][workspace][camera-speed][clip]" )
{
    edge_session_t session; auto &ws = session.workspace; auto &settings = session.gui.settings;
    camera_scope_t user( settings, settings_scope_t::USER );
    map_bounds_t bounds{}; bounds.bHas = CY_TRUE; bounds.box = { { 0, 0, 0 }, { 64, 64, 64 } };
    REQUIRE( MapWorkspace_CreateBox( &ws, bounds ) ); MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    MapWorkspace_SetClipPreview( &ws, { { 1, 0, 0 }, -32 } ); REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.bClip );
    REQUIRE( ws.editPreview.status == map_status_t::OK );
    const auto *previewPoints = ws.editPreviewWire.points.pData; const auto steps = EditorHistory_StepCount( &ws.history );
    change_log_t mapLog{}; REQUIRE( MapWorkspace_AddListener( &ws, &Record, &mapLog ) );
    camera_change_log_t settingLog{ &settings }; REQUIRE( EditorSettings_AddListener( &settings, &RecordCameraSpeed, &settingLog ) );
    REQUIRE( Run( session.gui, "map.camera.speed_increase" ) == command_result_t::OK );
    CHECK( settingLog.calls == 1 ); CHECK( settingLog.exact ); CHECK( settingLog.speed == 2000 ); CHECK( mapLog.nCalls == 0 );
    CHECK( ws.editPreview.bActive ); CHECK( ws.editPreview.bClip ); CHECK( ws.editPreview.clipPlane.d == -32 );
    CHECK( ws.editPreviewWire.points.pData == previewPoints ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    EditorSettings_RemoveListener( &settings, &RecordCameraSpeed, &settingLog ); MapWorkspace_RemoveListener( &ws, &Record, &mapLog );
}

TEST_CASE( "Camera speed rejects unavailable scopes and rechecks stale command state", "[map][gui][workspace][camera-speed]" )
{
    edge_session_t session; auto &ws = session.workspace; auto &settings = session.gui.settings;
    for ( const auto action : { map_camera_speed_action_t::INCREASE, map_camera_speed_action_t::DECREASE, map_camera_speed_action_t::RESET } ) {
        CHECK_FALSE( MapWorkspace_CanChangeCameraSpeed( &ws, action ) ); CHECK_FALSE( MapWorkspace_ChangeCameraSpeed( &ws, action ) );
    }
    CHECK( Run( session.gui, "map.camera.speed_increase" ) == command_result_t::DISABLED ); CHECK( CameraSpeed( settings ) == 1000 );
    CHECK_FALSE( MapWorkspace_CanChangeCameraSpeed( nullptr, map_camera_speed_action_t::INCREASE ) );
    camera_scope_t user( settings, settings_scope_t::USER, "{ editor = { camera = { move_speed = 2000.0 } } }" );
    const auto *command = EditorCommands_Find( &session.gui.commands, StringView_FromCString( "map.camera.speed_increase" ) ); REQUIRE( command != nullptr );
    CHECK( ( command->flags & COMMAND_FLAG_EDITS_DOCUMENT ) == 0 ); CHECK( ( command->pfnState( command->pContext ) & COMMAND_STATE_ENABLED ) != 0 );
    WriteCameraSpeed( settings, settings_scope_t::USER, 100000 );
    CHECK( command->pfnExecute( command->pContext, {} ) == command_result_t::DISABLED ); CHECK( CameraSpeed( settings ) == 100000 );
    EditorSettings_SetScope( &settings, settings_scope_t::USER, nullptr );
    CHECK( command->pfnExecute( command->pContext, {} ) == command_result_t::DISABLED );
    EditorSettings_SetScope( &settings, settings_scope_t::USER, &user.store ); const auto userText = SettingsText( user.store );
    {
        camera_scope_t blocked( settings, settings_scope_t::WORKSPACE, "{ editor = 4 }" ); const auto blockedText = SettingsText( blocked.store );
        CHECK_FALSE( MapWorkspace_CanChangeCameraSpeed( &ws, map_camera_speed_action_t::DECREASE ) );
        CHECK_FALSE( MapWorkspace_ChangeCameraSpeed( &ws, map_camera_speed_action_t::DECREASE ) );
        CHECK( SettingsText( blocked.store ) == blockedText ); CHECK( SettingsText( user.store ) == userText );
    }
    settings_document_t uninitialized{}; EditorSettings_SetScope( &settings, settings_scope_t::PROJECT, &uninitialized );
    CHECK_FALSE( MapWorkspace_CanChangeCameraSpeed( &ws, map_camera_speed_action_t::DECREASE ) );
    CHECK_FALSE( MapWorkspace_ChangeCameraSpeed( &ws, map_camera_speed_action_t::DECREASE ) );
    EditorSettings_SetScope( &settings, settings_scope_t::PROJECT, nullptr );
    CHECK_FALSE( MapWorkspace_ChangeCameraSpeed( &ws, static_cast<map_camera_speed_action_t>( 255 ) ) );
}

TEST_CASE( "Camera speed rejects nonfinite descriptor boundaries and recovers invalid stored values", "[map][gui][workspace][camera-speed]" )
{
    edge_session_t session; auto &settings = session.gui.settings; auto &ws = session.workspace;
    camera_scope_t user( settings, settings_scope_t::USER, "{ editor = { camera = { move_speed = \"nan\" } } }" );
    CHECK( CameraSpeed( settings ) == 1000 ); REQUIRE( MapWorkspace_ChangeCameraSpeed( &ws, map_camera_speed_action_t::INCREASE ) );
    CHECK( CameraSpeed( settings ) == 2000 );
    const auto *descriptor = EditorSettings_Find( &settings, StringView_FromCString( "editor.camera.move_speed" ) ); REQUIRE( descriptor != nullptr );
    usize index{}; while ( settings.descriptors.pData[index] != descriptor ) { ++index; }
    setting_descriptor_t invalid = *descriptor; settings.descriptors.pData[index] = &invalid;
    for ( const auto nonfinite : { std::numeric_limits<f64>::infinity(), std::numeric_limits<f64>::quiet_NaN() } ) {
        invalid = *descriptor; invalid.flMax = nonfinite;
        CHECK_FALSE( MapWorkspace_CanChangeCameraSpeed( &ws, map_camera_speed_action_t::INCREASE ) );
        CHECK_FALSE( MapWorkspace_ChangeCameraSpeed( &ws, map_camera_speed_action_t::DECREASE ) );
        invalid = *descriptor; invalid.flDefault = nonfinite;
        CHECK_FALSE( MapWorkspace_ChangeCameraSpeed( &ws, map_camera_speed_action_t::RESET ) );
    }
    settings.descriptors.pData[index] = descriptor; CHECK( CameraSpeed( settings ) == 2000 );
}

TEST_CASE( "Every camera speed staging allocation failure preserves settings and map state and permits retry", "[map][gui][workspace][camera-speed][allocation][atomic]" )
{
    edge_allocation_t audit{}; const allocator_t allocator{ &AllocateEdgeTest, nullptr, &FreeEdgeTest, &audit }; usize successfulCalls{};
    {
        edge_session_t session; camera_scope_t local( session.gui.settings, settings_scope_t::WORKSPACE, "{ future = { text = \"keep\" } }", &allocator );
        audit.calls = 0; REQUIRE( MapWorkspace_ChangeCameraSpeed( &session.workspace, map_camera_speed_action_t::INCREASE ) ); successfulCalls = audit.calls;
    }
    CHECK( audit.live == 0 ); REQUIRE( successfulCalls > 0 );
    for ( usize failure = 1; failure <= successfulCalls; ++failure ) {
        CAPTURE( failure );
        {
            edge_session_t session; auto &ws = session.workspace; auto &settings = session.gui.settings;
            camera_scope_t local( settings, settings_scope_t::WORKSPACE, "{ future = { text = \"keep\" } }", &allocator );
            const auto original = SettingsText( local.store ); auto *tree = local.store.pDocument; const auto loadedVersion = local.store.nLoadedVersion;
            const auto *document = ws.pDocument; const auto *metadata = document->root.pDocument; const auto revision = document->geometry.revision;
            const auto *roots = ws.selection.ids.pData; const auto selectionRevision = ws.selection.revision; const auto steps = EditorHistory_StepCount( &ws.history );
            change_log_t mapLog{}; REQUIRE( MapWorkspace_AddListener( &ws, &Record, &mapLog ) );
            camera_change_log_t settingLog{ &settings }; REQUIRE( EditorSettings_AddListener( &settings, &RecordCameraSpeed, &settingLog ) );
            const auto live = audit.live; audit.calls = 0; audit.failOn = failure;
            CHECK( Run( session.gui, "map.camera.speed_increase" ) == command_result_t::FAILED ); audit.failOn = 0;
            CHECK( audit.live == live ); CHECK( local.store.pDocument == tree ); CHECK( local.store.nLoadedVersion == loadedVersion );
            CHECK( SettingsText( local.store ) == original ); CHECK( CameraSpeed( settings ) == 1000 ); CHECK( settingLog.calls == 0 );
            CHECK( ws.pDocument == document ); CHECK( document->root.pDocument == metadata ); CHECK( document->geometry.revision == revision );
            CHECK( ws.selection.ids.pData == roots ); CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
            CHECK( mapLog.nCalls == 0 );
            REQUIRE( Run( session.gui, "map.camera.speed_increase" ) == command_result_t::OK ); CHECK( CameraSpeed( settings ) == 2000 );
            CHECK( settingLog.calls == 1 ); CHECK( settingLog.exact ); CHECK( settingLog.speed == 2000 ); CHECK( mapLog.nCalls == 0 );
            EditorSettings_RemoveListener( &settings, &RecordCameraSpeed, &settingLog ); MapWorkspace_RemoveListener( &ws, &Record, &mapLog );
        }
        CHECK( audit.live == 0 );
    }
}

TEST_CASE( "Authored mesh edge selection is persistent canonical and confined to one source", "[map][gui][workspace][mesh-edge]" )
{
    edge_session_t session; auto &ws = session.workspace;
    const u64 first = AddEdgeTestMesh( ws ), second = AddEdgeTestMesh( ws, 128 );
    const auto edges = SourceEdges( ws, first ), other = SourceEdges( ws, second );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
    const usize steps = EditorHistory_StepCount( &ws.history );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, first, { edges[0].b, edges[0].a } ) );
    REQUIRE( MapWorkspace_HasMeshEdges( &ws ) ); CHECK( ws.meshSelection.meshId.value == first );
    CHECK( ws.meshSelection.edges.nCount == 1 ); CHECK( ws.meshSelection.edges.pData[0].a.value < ws.meshSelection.edges.pData[0].b.value );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, first, edges[1], MAP_SELECT_ADD ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, first, edges[1], MAP_SELECT_ADD ) ); CHECK( ws.meshSelection.edges.nCount == 2 );
    CHECK_FALSE( MapWorkspace_SelectMeshEdge( &ws, second, other[0], MAP_SELECT_ADD ) );
    CHECK_FALSE( MapWorkspace_SelectMeshEdge( &ws, second, other[0], MAP_SELECT_TOGGLE ) );
    CHECK_FALSE( MapWorkspace_SelectMeshEdge( &ws, first, { { 999999 }, { 999998 } } ) );
    CHECK( ws.meshSelection.meshId.value == first ); CHECK( ws.meshSelection.edges.nCount == 2 );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, first, edges[1], MAP_SELECT_TOGGLE ) ); CHECK( ws.meshSelection.edges.nCount == 1 );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, first, edges[0], MAP_SELECT_TOGGLE ) );
    CHECK_FALSE( MapWorkspace_HasMeshEdges( &ws ) ); CHECK( ws.selectedMeshEdgeSeed.a.value == 0 );
    REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == first );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, second, other[0], MAP_SELECT_ADD ) );
    CHECK( ws.meshSelection.meshId.value == second ); CHECK( ws.selection.ids.pData[0] == second );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps );
}

TEST_CASE( "Edge loop and ring commands select read-only topology without enabling parent edits", "[map][gui][workspace][mesh-edge]" )
{
    edge_session_t session; auto &ws = session.workspace; const u64 object = AddEdgeTestMesh( ws ); const auto edges = SourceEdges( ws, object );
    const auto enabled = [&]( const char *command ) { return ( EditorCommands_State( &session.gui.commands, StringView_FromCString( command ) ) & COMMAND_STATE_ENABLED ) != 0; };
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    CHECK_FALSE( enabled( "map.select.loop" ) ); CHECK_FALSE( enabled( "map.select.ring" ) );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, object, edges[0] ) ); ws.pDocument->bReadOnly = CY_TRUE;
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
    const usize steps = EditorHistory_StepCount( &ws.history );
    CHECK( enabled( "map.select.loop" ) ); CHECK( enabled( "map.select.ring" ) );
    REQUIRE( Run( session.gui, "map.select.loop" ) == command_result_t::OK );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, object, edges[0] ) );
    REQUIRE( Run( session.gui, "map.select.ring" ) == command_result_t::OK );
    CHECK( ws.meshSelection.edges.nCount == 4 ); CHECK( geometry::MeshSelection_HasEdge( &ws.meshSelection, edges[0] ) );
    CHECK( ws.selectedMeshEdgeSeed.a.value == edges[0].a.value ); CHECK( ws.selectedMeshEdgeSeed.b.value == edges[0].b.value );
    ws.pDocument->bReadOnly = CY_FALSE;
    CHECK_FALSE( MapWorkspace_CanEditSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanMoveSelection( &ws ) );
    CHECK_FALSE( MapWorkspace_CanEditEntityProperties( &ws ) ); CHECK_FALSE( MapWorkspace_DeleteSelection( &ws ) );
    CHECK_FALSE( MapWorkspace_DuplicateSelection( &ws ) ); CHECK_FALSE( MapWorkspace_TranslateSelection( &ws, { 1, 0, 0 } ) );
    CHECK_FALSE( MapWorkspace_ScaleSelection( &ws, { 2, 2, 2 }, {} ) ); CHECK_FALSE( MapWorkspace_RotateSelection( &ws, { 0, 0, 90 }, {} ) );
    CHECK_FALSE( MapWorkspace_FlipMeshNormals( &ws ) ); CHECK_FALSE( MapWorkspace_TriangulateMeshSelection( &ws ) );
    for ( const char *command : { "map.mesh.extrude_edges", "map.mesh.bevel", "map.mesh.split_edges", "map.mesh.dissolve", "map.mesh.collapse",
             "map.mesh.connect_edges", "map.mesh.bridge", "map.mesh.extend_edges", "map.mesh.snap_edge_to_edge", "map.mesh.normals_hard",
             "map.mesh.normals_soft", "map.mesh.normals_default", "map.texture.weld_uvs", "map.select.ribs", "map.pivot.clear",
             "map.tool.edge_cut", "map.tool.edge_arc", "map.mesh.radial_align" } ) { CAPTURE( command ); CHECK_FALSE( enabled( command ) ); }
    MapWorkspace_SetTool( &ws, map_tool_t::NONE ); CHECK( MapWorkspace_HasMeshEdges( &ws ) );
    CHECK_FALSE( enabled( "map.select.loop" ) ); CHECK_FALSE( enabled( "map.select.ring" ) );
    CHECK( Run( session.gui, "map.select.ring" ) == command_result_t::DISABLED );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    CHECK( ws.meshSelection.meshId.value == 0 ); CHECK( ws.meshSelection.edges.nCount == 0 ); CHECK( ws.selection.ids.nCount == 0 );
}

TEST_CASE( "Authored edge context clears on root mode document and visibility replacement", "[map][gui][workspace][mesh-edge]" )
{
    edge_session_t session; auto &ws = session.workspace; const u64 object = AddEdgeTestMesh( ws ); const auto edges = SourceEdges( ws, object );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES ); REQUIRE( MapWorkspace_SelectMeshEdge( &ws, object, edges[0] ) );
    SECTION( "same root selection explicitly replaces component context" ) { MapWorkspace_Select( &ws, object, MAP_SELECT_REPLACE ); }
    SECTION( "root set explicitly replaces component context" ) { MapWorkspace_SetSelection( &ws, &object, 1 ); }
    SECTION( "mode change" ) { MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES ); }
    SECTION( "new map" ) { REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); }
    SECTION( "open" ) {
        CHECK( MapWorkspace_Open( &ws, QStringLiteral( "/nonexistent/nothing.cymap" ) ).status == map_files_status_t::ROOT_MISSING );
        CHECK( MapWorkspace_HasMeshEdges( &ws ) ); REQUIRE( MapWorkspace_Open( &ws, ExampleRoot() ).status == map_files_status_t::OK );
    }
    SECTION( "history replaces geometry" ) {
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK_FALSE( MapWorkspace_HasMeshEdges( &ws ) );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    }
    SECTION( "visgroup hides source" ) { MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::MESHES, CY_TRUE ); }
    SECTION( "hide selected" ) { REQUIRE( Run( session.gui, "map.hide.selected" ) == command_result_t::OK ); }
    SECTION( "source is deleted" ) { REQUIRE( MapEdit_Delete( ws.pDocument, { &object, 1 } ) == map_status_t::OK ); MapWorkspace_DocumentChanged( &ws ); }
    CHECK( ws.meshSelection.meshId.value == 0 ); CHECK( ws.meshSelection.edges.nCount == 0 ); CHECK( ws.selectedMeshEdgeSeed.a.value == 0 );
}

TEST_CASE( "Document refresh prunes dead authored endpoints while retaining live edges", "[map][gui][workspace][mesh-edge]" )
{
    edge_session_t session; auto &ws = session.workspace; const u64 object = AddEdgeTestMesh( ws ); const auto edges = SourceEdges( ws, object );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES ); REQUIRE( MapWorkspace_SelectMeshEdge( &ws, object, edges[0] ) );
    REQUIRE( geometry::MeshSelection_TryAddEdge( &ws.meshSelection, { { 999998 }, { 999999 } } ) == geometry::geometry_status_t::OK );
    ws.selectedMeshEdgeSeed = { { 999998 }, { 999999 } };
    change_log_t log{}; REQUIRE( MapWorkspace_AddListener( &ws, &Record, &log ) );
    MapWorkspace_Notify( &ws, MAP_CHANGE_DOCUMENT );
    CHECK( ws.meshSelection.edges.nCount == 1 ); CHECK( MapWorkspace_HasMeshEdges( &ws ) );
    CHECK( ws.selectedMeshEdgeSeed.a.value == edges[0].a.value ); CHECK( ( log.all & MAP_CHANGE_SELECTION ) != 0 );
    MapWorkspace_RemoveListener( &ws, &Record, &log );
}

TEST_CASE( "Edge selection and topology query allocation failures preserve the entire session", "[map][gui][workspace][mesh-edge][allocation][atomic]" )
{
    for ( const bool ring : { false, true } ) {
        CAPTURE( ring ); edge_allocation_t audit{}; const allocator_t allocator{ &AllocateEdgeTest, nullptr, &FreeEdgeTest, &audit };
        usize successfulCalls{};
        const auto prepare = [&]( edge_session_t &session ) {
            auto &ws = session.workspace; const u64 first = AddEdgeTestMesh( ws ), second = AddEdgeTestMesh( ws, 128 );
            const auto edges = SourceEdges( ws, first ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
            REQUIRE( MapWorkspace_SelectMeshEdge( &ws, first, edges[0] ) ); return second;
        };
        const auto apply = [&]( map_workspace_t &ws, u64 target ) {
            return ring ? MapWorkspace_SelectMeshEdgeRing( &ws ) : MapWorkspace_SelectMeshEdge( &ws, target, SourceEdges( ws, target )[0] );
        };
        {
            edge_session_t session; const u64 target = prepare( session ); const auto *original = session.gui.pAllocator;
            session.gui.pAllocator = &allocator; const bool selected = apply( session.workspace, target ); session.gui.pAllocator = original;
            REQUIRE( selected ); successfulCalls = audit.calls; REQUIRE( successfulCalls != 0 );
        }
        REQUIRE( audit.live == 0 );
        for ( usize failure = 1; failure <= successfulCalls; ++failure ) {
            CAPTURE( failure ); edge_session_t session; auto &ws = session.workspace; const u64 target = prepare( session );
            const auto *document = ws.pDocument; const auto documentRevision = document->geometry.revision;
            const auto *roots = ws.selection.ids.pData; const auto root = roots[0]; const auto selectionRevision = ws.selection.revision;
            const auto *components = ws.meshSelection.edges.pData; const auto seed = ws.selectedMeshEdgeSeed; const auto edge = components[0];
            const auto *points = ws.wire.points.pData; const auto steps = EditorHistory_StepCount( &ws.history );
            audit.calls = 0; audit.failOn = failure; const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
            const bool selected = apply( ws, target ); session.gui.pAllocator = original;
            CHECK_FALSE( selected ); CHECK( audit.calls >= failure ); CHECK( audit.live == 0 );
            CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == documentRevision ); CHECK( ws.wire.points.pData == points );
            CHECK( ws.selection.ids.pData == roots ); CHECK( ws.selection.ids.pData[0] == root ); CHECK( ws.selection.revision == selectionRevision );
            CHECK( ws.meshSelection.edges.pData == components ); CHECK( ws.meshSelection.edges.nCount == 1 ); CHECK( ws.meshSelection.meshId.value == root );
            CHECK( ws.meshSelection.edges.pData[0].a.value == edge.a.value ); CHECK( ws.meshSelection.edges.pData[0].b.value == edge.b.value );
            CHECK( ws.selectedMeshEdgeSeed.a.value == seed.a.value ); CHECK( ws.selectedMeshEdgeSeed.b.value == seed.b.value );
            CHECK( EditorHistory_StepCount( &ws.history ) == steps );
            audit.calls = 0; audit.failOn = 0; REQUIRE( apply( ws, target ) );
        }
        CHECK( audit.live == 0 );
    }
}

TEST_CASE( "Failed root replacement preserves edge context and successful replacement notifies once", "[map][gui][workspace][mesh-edge][allocation][atomic]" )
{
    edge_allocation_t audit{}; const allocator_t allocator{ &AllocateEdgeTest, nullptr, &FreeEdgeTest, &audit };
    {
        edge_session_t session( &allocator ); auto &ws = session.workspace;
        const u64 first = AddEdgeTestMesh( ws ), second = AddEdgeTestMesh( ws, 128 ); const auto edges = SourceEdges( ws, first );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES ); REQUIRE( MapWorkspace_SelectMeshEdge( &ws, first, edges[0] ) );
        const auto *roots = ws.selection.ids.pData; const auto *components = ws.meshSelection.edges.pData;
        const auto revision = ws.selection.revision; const auto live = audit.live;
        change_log_t log{}; REQUIRE( MapWorkspace_AddListener( &ws, &Record, &log ) );
        audit.calls = 0; audit.failOn = 1; MapWorkspace_Select( &ws, second, MAP_SELECT_REPLACE );
        CHECK( audit.calls == 1 ); CHECK( audit.live == live ); CHECK( ws.selection.ids.pData == roots );
        CHECK( ws.meshSelection.edges.pData == components ); CHECK( ws.selection.revision == revision ); CHECK( log.nCalls == 0 );
        audit.calls = 0; MapWorkspace_SetSelection( &ws, &second, 1 );
        CHECK( audit.calls == 1 ); CHECK( audit.live == live ); CHECK( ws.selection.ids.pData == roots );
        CHECK( ws.meshSelection.edges.pData == components ); CHECK( ws.selection.revision == revision ); CHECK( log.nCalls == 0 );
        audit.failOn = 0; MapWorkspace_SetSelection( &ws, &second, 1 );
        CHECK( ws.selection.ids.pData[0] == second ); CHECK( ws.meshSelection.meshId.value == 0 ); CHECK( log.nCalls == 1 );
        MapWorkspace_RemoveListener( &ws, &Record, &log );
    }
    CHECK( audit.live == 0 );
}

TEST_CASE( "Saving retains valid persistent authored edge context", "[map][gui][workspace][mesh-edge][persistence]" )
{
    edge_session_t session; auto &ws = session.workspace; const u64 object = AddEdgeTestMesh( ws ); const auto edges = SourceEdges( ws, object );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES ); REQUIRE( MapWorkspace_SelectMeshEdge( &ws, object, edges[0] ) );
    QTemporaryDir temporary; REQUIRE( temporary.isValid() );
    REQUIRE( MapWorkspace_SaveAs( &ws, temporary.filePath( QStringLiteral( "edge.cymap" ) ) ).status == map_files_status_t::OK );
    CHECK( MapWorkspace_HasMeshEdges( &ws ) ); CHECK( ws.meshSelection.meshId.value == object );
    CHECK( ws.meshSelection.edges.nCount == 1 ); CHECK( ws.selectedMeshEdgeSeed.a.value == edges[0].a.value );
    CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
}

TEST_CASE( "Authored vertex selection stores sorted persistent IDs within one source", "[map][gui][workspace][mesh-vertex]" )
{
    edge_session_t session; auto &ws = session.workspace;
    const u64 first = AddEdgeTestMesh( ws ), second = AddEdgeTestMesh( ws, 128 );
    const auto vertices = SourceVertices( ws, first ), other = SourceVertices( ws, second );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision;
    const auto steps = EditorHistory_StepCount( &ws.history );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, first, vertices[3] ) );
    REQUIRE( MapWorkspace_HasMeshVertices( &ws ) ); CHECK_FALSE( MapWorkspace_HasMeshEdges( &ws ) );
    change_log_t log{}; REQUIRE( MapWorkspace_AddListener( &ws, &Record, &log ) );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, first, vertices[0], MAP_SELECT_ADD ) );
    CHECK( log.nCalls == 1 ); CHECK( log.all == MAP_CHANGE_SELECTION );
    REQUIRE( ws.meshSelection.vertices.nCount == 2 );
    CHECK( ws.meshSelection.vertices.pData[0].value == vertices[0].value );
    CHECK( ws.meshSelection.vertices.pData[1].value == vertices[3].value );
    const auto selectionRevision = ws.selection.revision;
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, first, vertices[0], MAP_SELECT_ADD ) );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, first, vertices[7], MAP_SELECT_REMOVE ) );
    CHECK( log.nCalls == 1 ); CHECK( ws.selection.revision == selectionRevision );
    CHECK_FALSE( MapWorkspace_SelectMeshVertex( &ws, second, other[0], MAP_SELECT_ADD ) );
    CHECK_FALSE( MapWorkspace_SelectMeshVertex( &ws, second, other[0], MAP_SELECT_TOGGLE ) );
    CHECK_FALSE( MapWorkspace_SelectMeshVertex( &ws, second, other[0], MAP_SELECT_REMOVE ) );
    CHECK_FALSE( MapWorkspace_SelectMeshVertex( &ws, first, other[0] ) );
    CHECK_FALSE( MapWorkspace_SelectMeshVertex( &ws, first, { first } ) );
    CHECK_FALSE( MapWorkspace_SelectMeshVertex( &ws, first, {} ) );
    CHECK_FALSE( MapWorkspace_SelectMeshVertex( &ws, first, vertices[0], static_cast<map_select_mode_t>( 255 ) ) );
    CHECK( ws.meshSelection.meshId.value == first ); CHECK( ws.meshSelection.vertices.nCount == 2 ); CHECK( log.nCalls == 1 );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, first, vertices[3], MAP_SELECT_TOGGLE ) );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, first, vertices[0], MAP_SELECT_REMOVE ) );
    CHECK_FALSE( MapWorkspace_HasMeshVertices( &ws ) ); CHECK( ws.selection.ids.pData[0] == first );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, second, other[0], MAP_SELECT_ADD ) );
    CHECK( ws.meshSelection.meshId.value == second ); CHECK( ws.selection.ids.pData[0] == second );
    CHECK( ws.meshSelection.edges.nCount == 0 ); CHECK( ws.meshSelection.faces.nCount == 0 ); CHECK( ws.selectedMeshEdgeSeed.a.value == 0 );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    MapWorkspace_RemoveListener( &ws, &Record, &log );
}

TEST_CASE( "Vertices support read-only inspection without parent edits or tool fallback", "[map][gui][workspace][mesh-vertex]" )
{
    edge_session_t session; auto &ws = session.workspace; const u64 object = AddEdgeTestMesh( ws ); const auto vertices = SourceVertices( ws, object );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); ws.pDocument->bReadOnly = CY_TRUE;
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[0] ) );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[1], MAP_SELECT_ADD ) );
    ws.pDocument->bReadOnly = CY_FALSE;
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const auto steps = EditorHistory_StepCount( &ws.history );
    CHECK_FALSE( MapWorkspace_CanEditSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanMoveSelection( &ws ) );
    CHECK_FALSE( MapWorkspace_CanEditEntityProperties( &ws ) ); CHECK_FALSE( MapWorkspace_DeleteSelection( &ws ) );
    CHECK_FALSE( MapWorkspace_DuplicateSelection( &ws ) ); CHECK_FALSE( MapWorkspace_TranslateSelection( &ws, { 1, 0, 0 } ) );
    CHECK_FALSE( MapWorkspace_ScaleSelection( &ws, { 2, 2, 2 }, {} ) ); CHECK_FALSE( MapWorkspace_RotateSelection( &ws, { 0, 0, 90 }, {} ) );
    CHECK_FALSE( MapWorkspace_ResizeSelection( &ws, { 1, 0, 0 }, { 16, 0, 0 }, false ) );
    CHECK_FALSE( MapWorkspace_CanCopySelection( &ws ) ); CHECK_FALSE( MapWorkspace_CopySelection( &ws ) );
    CHECK_FALSE( MapWorkspace_CanCutSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CutSelection( &ws ) );
    CHECK_FALSE( MapWorkspace_CanSelectMeshEdgeTopology( &ws ) ); CHECK_FALSE( MapWorkspace_SelectMeshEdgeRing( &ws ) );
    ws.editPreview.bActive = CY_TRUE;
    CHECK_FALSE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[2] ) ); ws.editPreview = {};
    REQUIRE( EditorHistory_Begin( &ws.history, StringView_FromCString( "inspection guard" ) ) == editor_history_status_t::OK );
    CHECK_FALSE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[2] ) ); EditorHistory_Cancel( &ws.history );
    MapWorkspace_SetTool( &ws, map_tool_t::NONE ); CHECK( MapWorkspace_HasMeshVertices( &ws ) );
    CHECK_FALSE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[2] ) );
    CHECK( ws.meshSelection.vertices.nCount == 2 ); CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    MapWorkspace_Select( &ws, 0, MAP_SELECT_REPLACE );
    CHECK( ws.meshSelection.meshId.value == 0 ); CHECK( ws.meshSelection.vertices.nCount == 0 ); CHECK( ws.selection.ids.nCount == 0 );
}

TEST_CASE( "Vertex and edge modes replace component context before a single refresh", "[map][gui][workspace][mesh-vertex][mesh-edge]" )
{
    edge_session_t session; auto &ws = session.workspace; const u64 object = AddEdgeTestMesh( ws );
    const auto vertices = SourceVertices( ws, object ); const auto edges = SourceEdges( ws, object );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES ); REQUIRE( MapWorkspace_SelectMeshEdge( &ws, object, edges[0] ) );
    MapWorkspace_ClearMeshVertices( &ws ); CHECK( MapWorkspace_HasMeshEdges( &ws ) ); CHECK_FALSE( MapWorkspace_HasMeshVertices( &ws ) );
    CHECK_FALSE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[0] ) ); CHECK( MapWorkspace_HasMeshEdges( &ws ) );
    change_log_t log{}; REQUIRE( MapWorkspace_AddListener( &ws, &Record, &log ) );
    const auto edgeRevision = ws.selection.revision;
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    CHECK( log.nCalls == 1 ); CHECK( ws.selection.revision == edgeRevision + 1 );
    CHECK( ws.meshSelection.meshId.value == 0 ); CHECK( ws.meshSelection.edges.nCount == 0 ); CHECK( ws.selectedMeshEdgeSeed.a.value == 0 );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[0] ) );
    MapWorkspace_ClearMeshEdges( &ws ); CHECK( MapWorkspace_HasMeshVertices( &ws ) ); CHECK_FALSE( MapWorkspace_HasMeshEdges( &ws ) );
    CHECK_FALSE( MapWorkspace_SelectMeshEdge( &ws, object, edges[0] ) ); CHECK( MapWorkspace_HasMeshVertices( &ws ) );
    const auto vertexRevision = ws.selection.revision; log = {};
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    CHECK( log.nCalls == 1 ); CHECK( ws.selection.revision == vertexRevision + 1 ); CHECK( ws.meshSelection.vertices.nCount == 0 );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, object, edges[0] ) ); CHECK( ws.meshSelection.vertices.nCount == 0 );
    MapWorkspace_RemoveListener( &ws, &Record, &log );
}

TEST_CASE( "Selected authored vertices cannot edit their owning entity metadata", "[map][gui][workspace][mesh-vertex][entity-edit]" )
{
    edge_session_t session; auto &ws = session.workspace; const u64 object = AddEdgeTestMesh( ws );
    u64 owner{};
    REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ), StringView_FromCString( "func_detail" ), {}, &owner ) == map_status_t::OK );
    REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, object, owner ) == map_status_t::OK ); MapWorkspace_DocumentChanged( &ws );
    REQUIRE( MapWorkspace_CanEditEntityProperties( &ws ) );
    key_value_document_desc_t desc{}; desc.pAllocator = Allocator_GetSystem();
    struct owned_value_t { key_value_document_t *document{}; ~owned_value_t() { KeyValue_DestroyDocument( document ); } };
    owned_value_t owned{ KeyValue_CreateDocument( desc ) }; auto *value = owned.document; REQUIRE( value != nullptr );
    REQUIRE( KeyValue_SetRootType( value, key_value_type_t::BOOL ) );
    REQUIRE( KeyValue_SetBool( value, KeyValue_Root( value ), CY_TRUE ) );
    REQUIRE( MapWorkspace_SetEntityProperty( &ws, StringView_FromCString( "active" ), KeyValue_Root( value ) ) );
    const auto vertices = SourceVertices( ws, object ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[0] ) );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision; const auto steps = EditorHistory_StepCount( &ws.history );
    CHECK_FALSE( MapWorkspace_CanEditEntityProperties( &ws ) );
    REQUIRE( KeyValue_SetBool( value, KeyValue_Root( value ), CY_FALSE ) );
    CHECK_FALSE( MapWorkspace_SetEntityProperty( &ws, StringView_FromCString( "active" ), KeyValue_Root( value ) ) );
    CHECK_FALSE( MapWorkspace_RenameEntityProperty( &ws, StringView_FromCString( "active" ), StringView_FromCString( "enabled" ) ) );
    CHECK_FALSE( MapWorkspace_RemoveEntityProperty( &ws, StringView_FromCString( "active" ) ) );
    CHECK_FALSE( MapWorkspace_SetEntityIdentityField( &ws, StringView_FromCString( "name" ), StringView_FromCString( "changed_owner" ) ) );
    CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CHECK( MapWorkspace_HasMeshVertices( &ws ) );
}

TEST_CASE( "Vertex context clears on root mode document and visibility replacement", "[map][gui][workspace][mesh-vertex]" )
{
    edge_session_t session; auto &ws = session.workspace; const u64 object = AddEdgeTestMesh( ws ); const auto vertices = SourceVertices( ws, object );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[0] ) );
    SECTION( "explicit component clear retains the inspection root" ) {
        MapWorkspace_ClearMeshVertices( &ws ); CHECK( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == object );
    }
    SECTION( "same root selection" ) { MapWorkspace_Select( &ws, object, MAP_SELECT_REPLACE ); }
    SECTION( "root set" ) { MapWorkspace_SetSelection( &ws, &object, 1 ); }
    SECTION( "whole object mode" ) { MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); }
    SECTION( "new map" ) { REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); }
    SECTION( "open" ) {
        CHECK( MapWorkspace_Open( &ws, QStringLiteral( "/nonexistent/nothing.cymap" ) ).status == map_files_status_t::ROOT_MISSING );
        CHECK( MapWorkspace_HasMeshVertices( &ws ) ); REQUIRE( MapWorkspace_Open( &ws, ExampleRoot() ).status == map_files_status_t::OK );
    }
    SECTION( "history replacement" ) {
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK_FALSE( MapWorkspace_HasMeshVertices( &ws ) );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    }
    SECTION( "visgroup hides source" ) {
        MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::MESHES, CY_TRUE );
        CHECK_FALSE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[0] ) );
    }
    SECTION( "hide selected" ) { REQUIRE( Run( session.gui, "map.hide.selected" ) == command_result_t::OK ); }
    SECTION( "deleted source" ) { REQUIRE( MapEdit_Delete( ws.pDocument, { &object, 1 } ) == map_status_t::OK ); MapWorkspace_DocumentChanged( &ws ); }
    CHECK( ws.meshSelection.meshId.value == 0 ); CHECK( ws.meshSelection.vertices.nCount == 0 ); CHECK( ws.meshSelection.edges.nCount == 0 );
}

TEST_CASE( "Vertex refresh prunes dead persistent IDs without allocating or losing live IDs", "[map][gui][workspace][mesh-vertex][allocation]" )
{
    edge_session_t session; auto &ws = session.workspace; const u64 object = AddEdgeTestMesh( ws ); const auto vertices = SourceVertices( ws, object );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[0] ) );
    REQUIRE( geometry::MeshSelection_TryAddVertex( &ws.meshSelection, { 999999 } ) == geometry::geometry_status_t::OK );
    CHECK_FALSE( MapWorkspace_HasMeshVertices( &ws ) );
    const auto *components = ws.meshSelection.vertices.pData; const auto revision = ws.selection.revision;
    change_log_t log{}; REQUIRE( MapWorkspace_AddListener( &ws, &Record, &log ) );
    edge_allocation_t audit{}; audit.failOn = 1; const allocator_t allocator{ &AllocateEdgeTest, nullptr, &FreeEdgeTest, &audit };
    const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
    MapWorkspace_Notify( &ws, MAP_CHANGE_DOCUMENT ); session.gui.pAllocator = original;
    CHECK( audit.calls == 0 ); CHECK( audit.live == 0 ); CHECK( ws.meshSelection.vertices.pData == components );
    CHECK( ws.meshSelection.vertices.nCount == 1 ); CHECK( ws.meshSelection.vertices.pData[0].value == vertices[0].value );
    CHECK( MapWorkspace_HasMeshVertices( &ws ) ); CHECK( ws.selection.revision == revision + 1 ); CHECK( log.nCalls == 1 );
    CHECK( ( log.all & MAP_CHANGE_SELECTION ) != 0 ); MapWorkspace_RemoveListener( &ws, &Record, &log );
}

TEST_CASE( "Every vertex selection allocation failure preserves components roots and history", "[map][gui][workspace][mesh-vertex][allocation][atomic]" )
{
    for ( const auto mode : { MAP_SELECT_REPLACE, MAP_SELECT_ADD, MAP_SELECT_TOGGLE } ) {
        CAPTURE( mode ); edge_allocation_t audit{}; const allocator_t allocator{ &AllocateEdgeTest, nullptr, &FreeEdgeTest, &audit };
        const auto prepare = [&]( edge_session_t &session ) {
            auto &ws = session.workspace; const u64 first = AddEdgeTestMesh( ws ), second = AddEdgeTestMesh( ws, 128 );
            MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
            REQUIRE( MapWorkspace_SelectMeshVertex( &ws, first, SourceVertices( ws, first )[0] ) );
            return mode == MAP_SELECT_REPLACE ? second : first;
        };
        const auto apply = [&]( map_workspace_t &ws, u64 target ) {
            return MapWorkspace_SelectMeshVertex( &ws, target, SourceVertices( ws, target )[mode == MAP_SELECT_TOGGLE ? 0 : 1], mode );
        };
        usize successfulCalls{};
        {
            edge_session_t session; const u64 target = prepare( session ); const auto *original = session.gui.pAllocator;
            session.gui.pAllocator = &allocator; const bool selected = apply( session.workspace, target ); session.gui.pAllocator = original;
            REQUIRE( selected ); successfulCalls = audit.calls; REQUIRE( successfulCalls != 0 );
        }
        REQUIRE( audit.live == 0 );
        for ( usize failure = 1; failure <= successfulCalls; ++failure ) {
            CAPTURE( failure ); edge_session_t session; auto &ws = session.workspace; const u64 target = prepare( session );
            const auto *document = ws.pDocument; const auto documentRevision = document->geometry.revision;
            const auto *roots = ws.selection.ids.pData; const auto root = roots[0]; const auto selectionRevision = ws.selection.revision;
            const auto *components = ws.meshSelection.vertices.pData; const auto vertex = components[0];
            const auto *points = ws.wire.points.pData; const auto steps = EditorHistory_StepCount( &ws.history );
            change_log_t log{}; REQUIRE( MapWorkspace_AddListener( &ws, &Record, &log ) );
            audit.calls = 0; audit.failOn = failure; const auto *original = session.gui.pAllocator; session.gui.pAllocator = &allocator;
            const bool selected = apply( ws, target ); session.gui.pAllocator = original;
            CHECK_FALSE( selected ); CHECK( audit.calls >= failure ); CHECK( audit.live == 0 ); CHECK( log.nCalls == 0 );
            CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == documentRevision ); CHECK( ws.wire.points.pData == points );
            CHECK( ws.selection.ids.pData == roots ); CHECK( roots[0] == root ); CHECK( ws.selection.revision == selectionRevision );
            CHECK( ws.meshSelection.vertices.pData == components ); CHECK( ws.meshSelection.vertices.nCount == 1 );
            CHECK( ws.meshSelection.meshId.value == root ); CHECK( components[0].value == vertex.value ); CHECK( ws.meshSelection.edges.nCount == 0 );
            CHECK( EditorHistory_StepCount( &ws.history ) == steps );
            audit.calls = 0; audit.failOn = 0; REQUIRE( apply( ws, target ) ); CHECK( log.nCalls == 1 );
            MapWorkspace_RemoveListener( &ws, &Record, &log );
        }
        CHECK( audit.live == 0 );
    }
}

TEST_CASE( "Failed root replacement retains vertices and successful replacement notifies once", "[map][gui][workspace][mesh-vertex][allocation][atomic]" )
{
    edge_allocation_t audit{}; const allocator_t allocator{ &AllocateEdgeTest, nullptr, &FreeEdgeTest, &audit };
    {
        edge_session_t session( &allocator ); auto &ws = session.workspace;
        const u64 first = AddEdgeTestMesh( ws ), second = AddEdgeTestMesh( ws, 128 );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); REQUIRE( MapWorkspace_SelectMeshVertex( &ws, first, SourceVertices( ws, first )[0] ) );
        const auto *roots = ws.selection.ids.pData; const auto *components = ws.meshSelection.vertices.pData;
        const auto revision = ws.selection.revision; const auto live = audit.live;
        change_log_t log{}; REQUIRE( MapWorkspace_AddListener( &ws, &Record, &log ) );
        audit.calls = 0; audit.failOn = 1; MapWorkspace_Select( &ws, second, MAP_SELECT_REPLACE );
        CHECK( audit.live == live ); CHECK( ws.selection.ids.pData == roots ); CHECK( ws.meshSelection.vertices.pData == components );
        CHECK( ws.selection.revision == revision ); CHECK( log.nCalls == 0 );
        audit.calls = 0; MapWorkspace_SetSelection( &ws, &second, 1 );
        CHECK( audit.live == live ); CHECK( ws.selection.ids.pData == roots ); CHECK( ws.meshSelection.vertices.pData == components );
        CHECK( ws.selection.revision == revision ); CHECK( log.nCalls == 0 );
        audit.failOn = 0; MapWorkspace_SetSelection( &ws, &second, 1 );
        CHECK( ws.selection.ids.pData[0] == second ); CHECK( ws.meshSelection.meshId.value == 0 ); CHECK( log.nCalls == 1 );
        MapWorkspace_RemoveListener( &ws, &Record, &log );
    }
    CHECK( audit.live == 0 );
}

TEST_CASE( "Saved rebuilt authored vertices retain persistent selection identity", "[map][gui][workspace][mesh-vertex][persistence]" )
{
    edge_session_t session; auto &ws = session.workspace; const u64 object = AddEdgeTestMesh( ws ); const auto vertices = SourceVertices( ws, object );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES ); REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[0] ) );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[7], MAP_SELECT_ADD ) );
    MapWorkspace_DocumentChanged( &ws ); CHECK( MapWorkspace_HasMeshVertices( &ws ) );
    QTemporaryDir temporary; REQUIRE( temporary.isValid() ); const auto path = temporary.filePath( QStringLiteral( "vertex.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    CHECK( MapWorkspace_HasMeshVertices( &ws ) ); CHECK( ws.meshSelection.meshId.value == object ); REQUIRE( ws.meshSelection.vertices.nCount == 2 );
    CHECK( ws.meshSelection.vertices.pData[0].value == vertices[0].value ); CHECK( ws.meshSelection.vertices.pData[1].value == vertices[7].value );
    CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    REQUIRE( MapWorkspace_Open( &ws, path ).status == map_files_status_t::OK ); CHECK_FALSE( MapWorkspace_HasMeshVertices( &ws ) );
    const auto reopened = SourceVertices( ws, object );
    CHECK( std::equal( vertices.begin(), vertices.end(), reopened.begin(), []( auto a, auto b ) { return a.value == b.value; } ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::VERTICES );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, vertices[0] ) ); CHECK( MapWorkspace_HasMeshVertices( &ws ) );
}

TEST_CASE( "Workspace notification skips listeners removed by an earlier callback", "[map][gui][workspace][lifetime]" )
{
    struct removal_t { map_workspace_t *workspace; change_log_t *removed; };
    map_workspace_t workspace{};
    change_log_t removed{};
    removal_t context{ &workspace, &removed };
    const auto remove = []( void *p, u32 ) noexcept {
        auto *c = static_cast<removal_t *>( p );
        MapWorkspace_RemoveListener( c->workspace, &Record, c->removed );
    };
    REQUIRE( MapWorkspace_AddListener( &workspace, remove, &context ) );
    REQUIRE( MapWorkspace_AddListener( &workspace, &Record, &removed ) );
    MapWorkspace_Notify( &workspace, MAP_CHANGE_SELECTION );
    CHECK( removed.nCalls == 0 );
    CHECK( workspace.nListeners == 1 );
}

TEST_CASE( "A workspace starts with an untitled map and the default grid", "[map][gui][workspace]" )
{
    gui::editor_gui_t gui{};
    REQUIRE( gui::EditorGui_Init( &gui, App(), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
    map_workspace_t workspace{};
    REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
    CHECK( workspace.pDocument != nullptr );
    CHECK( MapWorkspace_DisplayName( &workspace ) == QStringLiteral( "Untitled" ) );
    CHECK( workspace.gridSize == MAP_GRID_DEFAULT );
    CHECK( workspace.tool == map_tool_t::SELECT );
    CHECK( workspace.frameBounds.bHas ); // An empty map frames the origin.
    CHECK( MapWorkspace_Save( &workspace ).status == map_files_status_t::INVALID_ARGUMENT );
}

TEST_CASE( "Selection categories can be opened empty and their root eligibility stays explicit", "[map][gui][workspace][selection-mode]" )
{
    gui::editor_gui_t gui{};
    REQUIRE( gui::EditorGui_Init( &gui, App(), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
    map_workspace_t ws{}; REQUIRE( MapWorkspace_Init( &ws, &gui ) );
    REQUIRE( MapWorkspace_RegisterCommands( &ws, &gui.commands ) == command_registry_status_t::OK );
    const struct { const char *id; map_element_mode_t mode; } profiles[]{
        { "vertices", map_element_mode_t::VERTICES }, { "edges", map_element_mode_t::EDGES },
        { "faces", map_element_mode_t::FACES }, { "meshes", map_element_mode_t::MESHES },
        { "objects", map_element_mode_t::OBJECTS }, { "groups", map_element_mode_t::GROUPS }
    };
    for ( const auto &profile : profiles ) {
        CAPTURE( profile.id ); REQUIRE( MapWorkspace_IsElementModeAvailable( profile.mode ) );
        MapWorkspace_SetTool( &ws, map_tool_t::NONE );
        const std::string command = std::string( "map.select_mode." ) + profile.id;
        REQUIRE( Run( gui, command.c_str() ) == command_result_t::OK );
        CHECK( ws.elementMode == profile.mode ); CHECK( ws.tool == map_tool_t::SELECT );
        CHECK( EditorSelection_Count( &ws.selection ) == 0u ); CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
        for ( const auto kind : { map_wire_kind_t::BRUSH, map_wire_kind_t::MESH, map_wire_kind_t::ENTITY, map_wire_kind_t::PATCH, map_wire_kind_t::TERRAIN } ) {
            map_wire_object_t object{}; object.id = 17; object.kind = kind;
            const bool rootMode = profile.mode == map_element_mode_t::OBJECTS || profile.mode == map_element_mode_t::GROUPS;
            const bool surfaceMode = profile.mode == map_element_mode_t::MESHES || profile.mode == map_element_mode_t::FACES;
            const bool geometry = kind == map_wire_kind_t::BRUSH || kind == map_wire_kind_t::MESH;
            CHECK( MapWorkspace_IsSelectableObject( &ws, object ) == ( rootMode || ( surfaceMode && geometry ) ) );
            REQUIRE( EditorSelection_Apply( &ws.hidden, object.id, EDITOR_SELECT_ADD ) );
            CHECK_FALSE( MapWorkspace_IsSelectableObject( &ws, object ) ); ( void )EditorSelection_Clear( &ws.hidden );
        }
    }
    CHECK_FALSE( MapWorkspace_IsElementModeAvailable( map_element_mode_t::NAVIGATION ) );
}

TEST_CASE( "Opening replaces the map only when it loads", "[map][gui][workspace]" )
{
    gui::editor_gui_t gui{};
    REQUIRE( gui::EditorGui_Init( &gui, App(), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
    map_workspace_t workspace{};
    REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
    change_log_t log{};
    REQUIRE( MapWorkspace_AddListener( &workspace, &Record, &log ) );

    const map_document_t *pUntitled = workspace.pDocument;
    CHECK( MapWorkspace_Open( &workspace, QStringLiteral( "/nonexistent/nothing.cymap" ) ).status == map_files_status_t::ROOT_MISSING );
    CHECK( workspace.pDocument == pUntitled );
    CHECK( log.nCalls == 0 );

    MapWorkspace_Select( &workspace, 5u, MAP_SELECT_REPLACE );
    const map_files_result_t opened = MapWorkspace_Open( &workspace, ExampleRoot() );
    REQUIRE( opened.status == map_files_status_t::OK );
    CHECK( MapWorkspace_DisplayName( &workspace ) == QStringLiteral( "facility.cymap" ) );
    CHECK( workspace.wire.objects.nCount != 0u );
    CHECK( EditorSelection_Count( &workspace.selection ) == 0u );
    CHECK( ( log.all & MAP_CHANGE_DOCUMENT ) != 0u );
    CHECK( ( log.all & MAP_CHANGE_FRAME ) != 0u );
    CHECK( ( log.all & MAP_CHANGE_TITLE ) != 0u );
    MapWorkspace_RemoveListener( &workspace, &Record, &log );
}

TEST_CASE( "Selection modes keep a sorted set and notify once per change", "[map][gui][workspace]" )
{
    gui::editor_gui_t gui{};
    REQUIRE( gui::EditorGui_Init( &gui, App(), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
    map_workspace_t workspace{};
    REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
    change_log_t log{};
    REQUIRE( MapWorkspace_AddListener( &workspace, &Record, &log ) );

    MapWorkspace_Select( &workspace, 30u, MAP_SELECT_REPLACE );
    MapWorkspace_Select( &workspace, 10u, MAP_SELECT_ADD );
    MapWorkspace_Select( &workspace, 20u, MAP_SELECT_TOGGLE );
    REQUIRE( EditorSelection_Count( &workspace.selection ) == 3u );
    CHECK( EditorSelection_At( &workspace.selection, 0 ) == 10u );
    CHECK( EditorSelection_At( &workspace.selection, 2 ) == 30u );
    CHECK( log.nCalls == 3 );

    MapWorkspace_Select( &workspace, 10u, MAP_SELECT_ADD ); // Already selected: no change, no notification.
    CHECK( log.nCalls == 3 );
    MapWorkspace_Select( &workspace, 20u, MAP_SELECT_TOGGLE );
    CHECK_FALSE( MapWorkspace_IsSelected( &workspace, 20u ) );

    const u64 ids[]{ 7u, 3u, 7u, 0u, 5u };
    MapWorkspace_SetSelection( &workspace, ids, std::size( ids ) );
    REQUIRE( EditorSelection_Count( &workspace.selection ) == 3u );
    CHECK( EditorSelection_At( &workspace.selection, 0 ) == 3u );
    CHECK( EditorSelection_At( &workspace.selection, 1 ) == 5u );
    CHECK( EditorSelection_At( &workspace.selection, 2 ) == 7u );

    MapWorkspace_Select( &workspace, 0u, MAP_SELECT_REPLACE );
    CHECK( EditorSelection_Count( &workspace.selection ) == 0u );
    const int nBefore = log.nCalls;
    MapWorkspace_Select( &workspace, 0u, MAP_SELECT_REPLACE );
    CHECK( log.nCalls == nBefore );
    MapWorkspace_RemoveListener( &workspace, &Record, &log );
}

TEST_CASE( "Map commands drive the grid and tools and report unfinished tools disabled", "[map][gui][workspace]" )
{
    gui::editor_gui_t gui{};
    REQUIRE( gui::EditorGui_Init( &gui, App(), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
    map_workspace_t workspace{};
    REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
    REQUIRE( MapWorkspace_RegisterCommands( &workspace, &gui.commands ) == command_registry_status_t::OK );

    CHECK( Run( gui, "map.grid.smaller" ) == command_result_t::OK );
    CHECK( workspace.gridSize == 8.0 );
    CHECK( Run( gui, "map.grid.larger" ) == command_result_t::OK );
    CHECK( Run( gui, "map.grid.larger" ) == command_result_t::OK );
    CHECK( workspace.gridSize == 32.0 );
    CHECK( Run( gui, "map.grid 16" ) == command_result_t::OK );
    CHECK( workspace.gridSize == 16.0 );
    CHECK( Run( gui, "map.grid 12" ) == command_result_t::INVALID_ARGUMENTS );
    CHECK( Run( gui, "map.grid 8192" ) == command_result_t::INVALID_ARGUMENTS );
    CHECK( Run( gui, "map.grid 1" ) == command_result_t::OK );
    CHECK( ( EditorCommands_State( &gui.commands, StringView_FromCString( "map.grid.smaller" ) ) & COMMAND_STATE_ENABLED ) == 0u );

    CHECK( Run( gui, "map.grid.show" ) == command_result_t::OK );
    CHECK_FALSE( workspace.bGridVisible );

    CHECK( Run( gui, "map.tool.camera" ) == command_result_t::OK );
    CHECK( workspace.tool == map_tool_t::CAMERA );
    CHECK( ( EditorCommands_State( &gui.commands, StringView_FromCString( "map.tool.camera" ) ) & COMMAND_STATE_CHECKED ) != 0u );
    CHECK( Run( gui, "map.tool.navigation" ) == command_result_t::OK );
    CHECK( workspace.tool == map_tool_t::NONE );
    CHECK( MapWorkspace_IsToolAvailable( map_tool_t::NONE ) );
    CHECK( std::string_view( MapWorkspace_ToolName( map_tool_t::NONE ) ) == "Navigation" );
    CHECK( ( EditorCommands_State( &gui.commands, StringView_FromCString( "map.tool.navigation" ) ) & COMMAND_STATE_CHECKED ) != 0u );
    for ( const char *id : { "map.tool.select", "map.tool.camera", "map.tool.block", "map.tool.translate", "map.tool.rotate", "map.tool.scale" } ) {
        CHECK( ( EditorCommands_State( &gui.commands, StringView_FromCString( id ) ) & COMMAND_STATE_CHECKED ) == 0u );
    }
    CHECK( Run( gui, "map.tool.block" ) == command_result_t::OK );
    CHECK( workspace.tool == map_tool_t::BLOCK );
    CHECK( Run( gui, "map.run" ) == command_result_t::DISABLED );
    CHECK( ( EditorCommands_State( &gui.commands, StringView_FromCString( "map.view.center_selection_2d" ) ) & COMMAND_STATE_ENABLED ) == 0u );
}

TEST_CASE( "Merge reuses its command and requires exactly two compatible visible authored brushes", "[map][gui][workspace][merge]" )
{
    gui::editor_gui_t gui{};
    REQUIRE( gui::EditorGui_Init( &gui, App(), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
    map_workspace_t ws{};
    REQUIRE( MapWorkspace_Init( &ws, &gui ) );
    command_registry_t commands{};
    REQUIRE( EditorCommands_Init( &commands, Allocator_GetSystem() ) == command_registry_status_t::OK );
    REQUIRE( MapWorkspace_RegisterCommands( &ws, &commands ) == command_registry_status_t::OK );
    CHECK( EditorCommands_Count( &commands ) == 142u ); // Includes the Edge Editing catalog and camera speed controls.
    const auto *merge = EditorCommands_Find( &commands, StringView_FromCString( "map.brush.merge" ) );
    REQUIRE( merge != nullptr );
    CHECK( std::string( merge->pIcon ) == "csg-merge" );
    CHECK( std::string( merge->pDescription ).find( "lowest-ID" ) != std::string::npos );
    const auto enabled = [&]() { return ( EditorCommands_State( &commands, StringView_FromCString( "map.brush.merge" ) ) & COMMAND_STATE_ENABLED ) != 0u; };
    CHECK_FALSE( enabled() );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { 0, 0, 0 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    const u64 first = EditorSelection_At( &ws.selection, 0 );
    CHECK_FALSE( enabled() );
    box.box.minimum.x = 64; box.box.maximum.x = 128;
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    const u64 second = EditorSelection_At( &ws.selection, 0 );
    const u64 pair[]{ second, first };
    MapWorkspace_SetSelection( &ws, pair, 2 );
    REQUIRE( enabled() );

    SECTION( "Hidden sources disable the command without changing selection" ) {
        REQUIRE( EditorSelection_Apply( &ws.hidden, second, EDITOR_SELECT_ADD ) );
        CHECK_FALSE( enabled() );
        ( void )EditorSelection_Clear( &ws.hidden );
        CHECK( enabled() );
        MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::BRUSHES, CY_TRUE );
        CHECK_FALSE( enabled() );
    }
    SECTION( "Read-only maps cannot merge" ) {
        ws.pDocument->bReadOnly = CY_TRUE;
        CHECK_FALSE( enabled() );
        CHECK( EditorCommands_Execute( &commands, StringView_FromCString( "map.brush.merge" ), {} ) == command_result_t::DISABLED );
        CHECK( ws.wire.objects.nCount == 2u );
    }
    SECTION( "Different layers cannot merge" ) {
        REQUIRE( MapDocument_AddLayer( ws.pDocument, StringView_FromCString( "detail" ), StringView_FromCString( "Detail" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryLayer( ws.pDocument, second, StringView_FromCString( "detail" ) ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &ws );
        CHECK_FALSE( enabled() );
    }
    SECTION( "Different entity ownership and mixed selections cannot merge" ) {
        u64 entity{};
        REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "default" ), StringView_FromCString( "func_detail" ), { 0, 0, 0 }, &entity ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, second, entity ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &ws );
        CHECK_FALSE( enabled() );
        const u64 mixed[]{ first, entity };
        MapWorkspace_SetSelection( &ws, mixed, 2 );
        CHECK_FALSE( enabled() );
        const u64 three[]{ first, second, entity };
        MapWorkspace_SetSelection( &ws, three, 3 );
        CHECK_FALSE( enabled() );
    }
    SECTION( "Execution creates one undoable union without changing the active tool" ) {
        MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
        const usize steps = EditorHistory_StepCount( &ws.history );
        REQUIRE( EditorCommands_Execute( &commands, StringView_FromCString( "map.brush.merge" ), {} ) == command_result_t::OK );
        CHECK( ws.tool == map_tool_t::BLOCK );
        REQUIRE( ws.wire.objects.nCount == 1u );
        CHECK( ws.wire.objects.pData[0].bounds.box.minimum.x == 0.0 );
        CHECK( ws.wire.objects.pData[0].bounds.box.maximum.x == 128.0 );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
        CHECK_FALSE( enabled() );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        CHECK( ws.wire.objects.nCount == 2u );
        CHECK( enabled() );
    }
    EditorCommands_Shutdown( &commands );
}

TEST_CASE( "Save As names the map and later saves go to the same place", "[map][gui][workspace]" )
{
    gui::editor_gui_t gui{};
    REQUIRE( gui::EditorGui_Init( &gui, App(), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
    map_workspace_t workspace{};
    REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
    REQUIRE( MapWorkspace_Open( &workspace, ExampleRoot() ).status == map_files_status_t::OK );

    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "cypher_map_gui_saveas";
    std::filesystem::remove_all( directory );
    std::filesystem::create_directories( directory );
    const QString target = QString::fromStdString( ( directory / "copy.cymap" ).string() );
    REQUIRE( MapWorkspace_SaveAs( &workspace, target ).status == map_files_status_t::OK );
    CHECK( workspace.path == target );
    CHECK( MapWorkspace_DisplayName( &workspace ) == QStringLiteral( "copy.cymap" ) );
    CHECK( std::filesystem::exists( directory / "copy" / "structure" ) );
    CHECK( MapWorkspace_Save( &workspace ).status == map_files_status_t::OK );
    std::filesystem::remove_all( directory );
}

TEST_CASE( "Authored geometry transforms clips hollows saves and reopens through the workspace", "[map][gui][workspace][geometry-edit][persistence]" )
{
    gui::editor_gui_t gui{};
    REQUIRE( gui::EditorGui_Init( &gui, App(), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
    map_workspace_t ws{};
    REQUIRE( MapWorkspace_Init( &ws, &gui ) );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    const u64 original = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( MapWorkspace_TranslateSelection( &ws, { 32, 16, 0 } ) );
    REQUIRE( MapWorkspace_ScaleSelection( &ws, { 2, 1, 1 }, { 32, 16, 64 } ) );
    REQUIRE( MapWorkspace_RotateSelection( &ws, { 0, 0, 90 }, { 32, 16, 64 } ) );
    REQUIRE( MapWorkspace_ClipSelection( &ws, { { 1, 0, 0 }, -32 } ) );
    CHECK( EditorSelection_At( &ws.selection, 0 ) == original );
    REQUIRE( MapWorkspace_HollowSelection( &ws, 8 ) );
    CHECK( ws.pDocument->geometry.brushes.nCount == 6 );
    CHECK( EditorSelection_Count( &ws.selection ) == 6 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 6 );
    const auto expected = ws.wire.bounds;
    QTemporaryDir temporary;
    REQUIRE( temporary.isValid() );
    const QString target = temporary.filePath( QStringLiteral( "authored.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, target ).status == map_files_status_t::OK );
    CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( ws.pDocument->geometry.brushes.nCount == 1 );
    CHECK( MapWorkspace_IsModified( &ws ) );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    REQUIRE( MapWorkspace_Open( &ws, target ).status == map_files_status_t::OK );
    CHECK( ws.pDocument->geometry.brushes.nCount == 6 );
    CHECK( ws.wire.faces.nCount == 36 );
    CHECK( std::abs( ws.wire.bounds.box.minimum.x - expected.box.minimum.x ) < 1e-6 );
    CHECK( std::abs( ws.wire.bounds.box.maximum.y - expected.box.maximum.y ) < 1e-6 );
    CHECK( ws.wire.bounds.box.maximum.z == expected.box.maximum.z );
}

TEST_CASE( "Rejected undo ownership budget leaves the live map and view untouched", "[map][gui][workspace][geometry-edit][history]" )
{
    gui::editor_gui_t gui{};
    REQUIRE( gui::EditorGui_Init( &gui, App(), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
    map_workspace_t ws{};
    REQUIRE( MapWorkspace_Init( &ws, &gui ) );
    EditorHistory_Shutdown( &ws.history );
    REQUIRE( EditorHistory_Init( &ws.history, Allocator_GetSystem(), 8, 1 ) == editor_history_status_t::OK );
    const auto *live = ws.pDocument;
    map_bounds_t box{}; MapBounds_AddPoint( box, { 0, 0, 0 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    CHECK_FALSE( MapWorkspace_CreateBox( &ws, box ) );
    CHECK( ws.pDocument == live );
    CHECK( ws.pDocument->geometry.brushes.nCount == 0 );
    CHECK( ws.wire.objects.nCount == 0 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
    CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
}

namespace
{

// A minimal edit for the undo plumbing: place a light at x = payload, or
// remove it again. Real map edits arrive with the tools.
struct light_edit_t {
    map::map_workspace_t *pWorkspace{ nullptr };
    u64 id{ 0u };
};

error_code_t UndoPlace( binary_block_t, void *pUserData ) noexcept
{
    auto *pEdit = static_cast<light_edit_t *>( pUserData );
    if ( MapDocument_RemoveObject( pEdit->pWorkspace->pDocument, pEdit->id ) != map_status_t::OK ) {
        return Cy_ErrorMake( common_error_t::ERR_NOT_FOUND );
    }
    MapWorkspace_DocumentChanged( pEdit->pWorkspace );
    return CY_ERROR_OK;
}

error_code_t RedoPlace( binary_block_t, void *pUserData ) noexcept
{
    auto *pEdit = static_cast<light_edit_t *>( pUserData );
    const map_status_t status = MapDocument_AddEntity( pEdit->pWorkspace->pDocument, StringView_FromCString( "default" ),
                                                       StringView_FromCString( "light" ), { 64.0, 0.0, 0.0 }, &pEdit->id );
    if ( status == map_status_t::OK ) { MapWorkspace_DocumentChanged( pEdit->pWorkspace ); }
    return status == map_status_t::OK ? CY_ERROR_OK : Cy_ErrorMake( common_error_t::ERR_INVALID_STATE );
}

} // namespace

TEST_CASE( "Undo and redo refresh the map and follow the saved point", "[map][gui][workspace]" )
{
    gui::editor_gui_t gui{};
    REQUIRE( gui::EditorGui_Init( &gui, App(), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
    map_workspace_t workspace{};
    REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
    CHECK_FALSE( MapWorkspace_IsModified( &workspace ) );
    change_log_t log{};
    REQUIRE( MapWorkspace_AddListener( &workspace, &Record, &log ) );

    light_edit_t edit{ &workspace, 0u };
    REQUIRE( RedoPlace( {}, &edit ) == CY_ERROR_OK );
    undo_operation_desc_t op{};
    op.id = 1u;
    op.label = StringView_FromCString( "Place Light" );
    op.pfnUndo = UndoPlace;
    op.pfnRedo = RedoPlace;
    op.pUserData = &edit;
    REQUIRE( EditorHistory_Push( &workspace.history, op ) == editor_history_status_t::OK );
    MapWorkspace_DocumentChanged( &workspace );
    CHECK( MapWorkspace_IsModified( &workspace ) );
    CHECK( ( log.all & MAP_CHANGE_TITLE ) != 0u );
    CHECK( MapWireframe_FindEntity( workspace.wire, edit.id ) != nullptr );

    MapWorkspace_Select( &workspace, edit.id, MAP_SELECT_REPLACE );
    REQUIRE( MapWorkspace_Undo( &workspace ) == editor_history_status_t::OK );
    CHECK_FALSE( MapWorkspace_IsModified( &workspace ) );
    CHECK( MapWireframe_FindEntity( workspace.wire, edit.id ) == nullptr );
    CHECK( EditorSelection_Count( &workspace.selection ) == 0u ); // The removed light left the selection.
    REQUIRE( MapWorkspace_Redo( &workspace ) == editor_history_status_t::OK );
    CHECK( MapWorkspace_IsModified( &workspace ) );
    CHECK( MapWireframe_FindEntity( workspace.wire, edit.id ) != nullptr );
    CHECK( MapWorkspace_Redo( &workspace ) == editor_history_status_t::NOTHING_TO_DO );

    // Opening another map starts a fresh, clean history.
    REQUIRE( MapWorkspace_Open( &workspace, ExampleRoot() ).status == map_files_status_t::OK );
    CHECK_FALSE( MapWorkspace_IsModified( &workspace ) );
    CHECK_FALSE( EditorHistory_CanUndo( &workspace.history ) );
    MapWorkspace_RemoveListener( &workspace, &Record, &log );
}

TEST_CASE( "The grid follows its settings and writes them back", "[map][gui][workspace]" )
{
    gui::editor_gui_t gui{};
    REQUIRE( gui::EditorGui_Init( &gui, App(), Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
    settings_document_t user{};
    REQUIRE( SettingsDocument_Init( &user, Allocator_GetSystem(), { StringView_FromCString( "cypher.settings" ), 1u, 2u } ) ==
             settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &user, StringView_FromCString( "@cykv 1\n@schema \"cypher.settings\" 2\n{ editor = { grid = { size = 64 } } }\n" ) )
                 .status == settings_document_status_t::OK );
    EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, &user );
    map_workspace_t workspace{};
    REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
    CHECK( workspace.gridSize == 64.0 ); // Read from the user scope.
    CHECK( EditorSettings_Find( &gui.settings, StringView_FromCString( "editor.map.texture_lock" ) ) != nullptr );

    // [ and ] write the setting, so the choice persists.
    MapWorkspace_SetGridSize( &workspace, 128.0 );
    CHECK( EditorSettings_Integer( &gui.settings, "editor.grid.size", 0 ) == 128 );
    CHECK( workspace.gridSize == 128.0 );
    // A change made elsewhere (the settings dialog) reaches the session.
    const setting_descriptor_t *pShow = EditorSettings_Find( &gui.settings, StringView_FromCString( "editor.grid.show" ) );
    REQUIRE( pShow != nullptr );
    setting_value_t hidden{};
    hidden.type = setting_type_t::BOOL;
    hidden.bValue = CY_FALSE;
    REQUIRE( EditorSettings_Write( &gui.settings, settings_scope_t::USER, *pShow, hidden ) == settings_registry_status_t::OK );
    CHECK_FALSE( workspace.bGridVisible );

    REQUIRE( MapWorkspace_RegisterCommands( &workspace, &gui.commands ) == command_registry_status_t::OK );
    MapWorkspace_SetAngleSnap( &workspace, 7.5 );
    MapWorkspace_SetScaleSnap( &workspace, 0.125 );
    REQUIRE( Run( gui, "map.grid.angle_snap" ) == command_result_t::OK );
    REQUIRE( Run( gui, "map.grid.scale_snap" ) == command_result_t::OK );
    CHECK( EditorSettings_Real( &gui.settings, "editor.grid.angle_snap", -1.0 ) == 0.0 );
    CHECK( EditorSettings_Real( &gui.settings, "editor.grid.scale_snap", -1.0 ) == 0.0 );
    REQUIRE( Run( gui, "map.grid.angle_snap" ) == command_result_t::OK );
    REQUIRE( Run( gui, "map.grid.scale_snap" ) == command_result_t::OK );
    CHECK( workspace.angleSnap == 7.5 );
    CHECK( workspace.scaleSnap == 0.125 );
    CHECK( EditorSettings_Real( &gui.settings, "editor.grid.scale_snap", -1.0 ) == 0.125 );
    MapWorkspace_SetAngleSnap( &workspace, std::numeric_limits<double>::quiet_NaN() );
    MapWorkspace_SetScaleSnap( &workspace, std::numeric_limits<double>::infinity() );
    MapWorkspace_SetGridSize( &workspace, std::numeric_limits<double>::quiet_NaN() );
    CHECK( workspace.angleSnap == 7.5 );
    CHECK( workspace.scaleSnap == 0.125 );
    CHECK( workspace.gridSize == 128.0 );

    // An attached project override must not silently defeat a quick toggle.
    settings_document_t project{};
    REQUIRE( SettingsDocument_Init( &project, Allocator_GetSystem(), { StringView_FromCString( "cypher.settings" ), 1u, 2u } ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &project, StringView_FromCString( "@cykv 1\n@schema \"cypher.settings\" 2\n{ editor = { grid = { angle_snap = 45.0 } } }\n" ) ).status == settings_document_status_t::OK );
    EditorSettings_SetScope( &gui.settings, settings_scope_t::PROJECT, &project );
    CHECK( workspace.angleSnap == 45.0 );
    MapWorkspace_SetAngleSnap( &workspace, 5.0 );
    CHECK( EditorSettings_Real( &gui.settings, "editor.grid.angle_snap", -1.0 ) == 5.0 );
    CHECK( workspace.angleSnap == 5.0 );
    EditorSettings_SetScope( &gui.settings, settings_scope_t::PROJECT, nullptr );
    CHECK( workspace.angleSnap == 7.5 ); // User preference was left intact.
    SettingsDocument_Shutdown( &project );
    MapWorkspace_Shutdown( &workspace );
    EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr );
}
