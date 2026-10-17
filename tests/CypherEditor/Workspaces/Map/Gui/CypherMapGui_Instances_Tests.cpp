//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Instances_Tests.cpp
//  Purpose: Tests prefab instances (CYMAP.md 6.11): creating one from the
//           selection, showing and picking it, moving, turning and copying
//           it, collapsing it, and editing its prefab in place so every
//           instance follows.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Hotspots.h"
#include "CypherMapGui_Instances.h"
#include "CypherMapGui_Notice.h"
#include "CypherMap_EntityEdit.h"
#include "CypherMap_Groups.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherCommon/Tier1/CypherCommon_KeyValue.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <string>
#include <vector>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{
struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t ws{};
    QTemporaryDir content;
    explicit session_t( const allocator_t *allocator = Allocator_GetSystem() )
    {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, allocator ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &ws, &gui ) );
        REQUIRE( MapWorkspace_RegisterCommands( &ws, &gui.commands ) == command_registry_status_t::OK );
        REQUIRE( content.isValid() );
        MapHotspots_SetResolver( &ws, [this]( const QString &path ) {
            const QString file = QDir( content.path() ).filePath( path );
            return QFileInfo::exists( file ) ? file : QString();
        } );
        MapAssets_SetRoots( &ws, { content.path() } );
    }
    ~session_t()
    {
        MapAssets_SetRoots( &ws, {} );
        MapHotspots_SetResolver( &ws, {} );
        MapWorkspace_Shutdown( &ws );
        gui::EditorGui_Shutdown( &gui );
    }
    u64 Box( math::vec3d_t lo, math::vec3d_t hi )
    {
        map_bounds_t bounds{};
        MapBounds_AddPoint( bounds, lo );
        MapBounds_AddPoint( bounds, hi );
        REQUIRE( MapWorkspace_CreateBox( &ws, bounds ) );
        return EditorSelection_At( &ws.selection, 0 );
    }
    command_result_t Run( const std::string &line ) { return EditorCommands_ExecuteLine( &gui.commands, StringView_FromCString( line.c_str() ) ); }
    void Select( std::vector<u64> ids ) { MapWorkspace_SetSelection( &ws, ids.data(), ids.size() ); }
    const map_wire_object_t *Object( u64 id ) { return MapWireframe_FindObject( ws.wire, id ); }
    usize Count( map_wire_kind_t kind )
    {
        usize n = 0;
        for ( usize i = 0; i < ws.wire.objects.nCount; ++i ) { n += ws.wire.objects.pData[i].kind == kind ? 1u : 0u; }
        return n;
    }
};
} // namespace

TEST_CASE( "An instance takes the selection's place, shows its prefab and is picked as one", "[map][gui][instances]" )
{
    session_t s;
    const u64 a = s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
    const u64 b = s.Box( { 64, 0, 0 }, { 128, 32, 32 } );
    s.Select( { a, b } );
    REQUIRE( s.Run( "map.prefab.create_instance prefabs/crates" ) == command_result_t::OK );
    CHECK( QFileInfo::exists( QDir( s.content.path() ).filePath( QStringLiteral( "prefabs/crates.cyprefab" ) ) ) );
    CHECK( s.Object( a ) == nullptr ); // The boxes left the map...
    CHECK( s.Count( map_wire_kind_t::BRUSH ) == 0u );
    REQUIRE( EditorSelection_Count( &s.ws.selection ) == 1u ); // ...for one selected instance.
    const u64 instance = EditorSelection_At( &s.ws.selection, 0 );
    REQUIRE( MapInstances_IsInstance( &s.ws, instance ) );
    map_instance_info_t info{};
    REQUIRE( MapInstances_Info( &s.ws, instance, &info ) );
    CHECK( info.source == QStringLiteral( "prefabs/crates.cyprefab" ) );
    CHECK( info.bLoaded );
    CHECK( info.origin.x == Catch::Approx( 64.0 ) ); // The bottom centre of the boxes.
    CHECK( info.origin.z == Catch::Approx( 0.0 ) );
    // It shows where the boxes were, as one PREFAB object.
    const map_wire_object_t *object = s.Object( instance );
    REQUIRE( object != nullptr );
    CHECK( object->kind == map_wire_kind_t::PREFAB );
    CHECK( object->bounds.box.minimum.x == Catch::Approx( 0.0 ) );
    CHECK( object->bounds.box.maximum.x == Catch::Approx( 128.0 ) );
    CHECK( object->bounds.box.maximum.z == Catch::Approx( 64.0 ) );
    // A ray into it hits its faces.
    CHECK( MapInstances_Raycast( &s.ws, instance, { 32, 32, 500 }, { 0, 0, -1 } ) == Catch::Approx( 500.0 - 64.0 ) );
    CHECK( MapInstances_Raycast( &s.ws, instance, { 500, 500, 500 }, { 0, 0, -1 } ) < 0.0 );
    // Undo brings the boxes back.
    REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK );
    CHECK( s.Object( a ) != nullptr );
    CHECK( s.Count( map_wire_kind_t::PREFAB ) == 0u );
}

TEST_CASE( "Instances move, turn, copy and survive saving", "[map][gui][instances]" )
{
    session_t s;
    const u64 a = s.Box( { -32, -16, 0 }, { 32, 16, 16 } );
    s.Select( { a } );
    REQUIRE( s.Run( "map.prefab.create_instance prefabs/plank" ) == command_result_t::OK );
    const u64 instance = EditorSelection_At( &s.ws.selection, 0 );
    REQUIRE( MapWorkspace_TranslateSelection( &s.ws, { 256, 0, 0 } ) );
    CHECK( s.Object( instance )->bounds.box.minimum.x == Catch::Approx( 224.0 ) );
    // A quarter turn about its origin: the plank lies along Y.
    REQUIRE( MapWorkspace_RotateSelection( &s.ws, { 0, 0, 90 }, { 256, 0, 0 } ) );
    const auto &turned = s.Object( instance )->bounds.box;
    CHECK( turned.maximum.x - turned.minimum.x == Catch::Approx( 32.0 ) );
    CHECK( turned.maximum.y - turned.minimum.y == Catch::Approx( 64.0 ) );
    map_instance_info_t info{};
    REQUIRE( MapInstances_Info( &s.ws, instance, &info ) );
    CHECK( info.angles.y == Catch::Approx( 90.0 ) );
    // Shift-drag copies it: a second instance of the same prefab.
    REQUIRE( MapWorkspace_TranslateSelection( &s.ws, { 0, 128, 0 }, true ) );
    CHECK( s.Count( map_wire_kind_t::PREFAB ) == 2u );
    // Saved and opened again, both are still there.
    const QString file = QDir( s.content.path() ).filePath( QStringLiteral( "level.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &s.ws, file ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_Open( &s.ws, file ).status == map_files_status_t::OK );
    CHECK( s.Count( map_wire_kind_t::PREFAB ) == 2u );
    REQUIRE( MapInstances_Info( &s.ws, instance, &info ) );
    CHECK( info.origin.x == Catch::Approx( 256.0 ) );
}

TEST_CASE( "Collapse copies the prefab in where the instance stands", "[map][gui][instances]" )
{
    session_t s;
    const u64 a = s.Box( { 0, 0, 0 }, { 64, 32, 32 } );
    s.Select( { a } );
    REQUIRE( s.Run( "map.prefab.create_instance prefabs/block" ) == command_result_t::OK );
    const u64 instance = EditorSelection_At( &s.ws.selection, 0 );
    REQUIRE( MapWorkspace_RotateSelection( &s.ws, { 0, 0, 90 }, { 32, 16, 0 } ) );
    REQUIRE( s.Run( "map.prefab.collapse" ) == command_result_t::OK );
    CHECK_FALSE( MapInstances_IsInstance( &s.ws, instance ) );
    CHECK( s.Count( map_wire_kind_t::BRUSH ) == 1u );
    REQUIRE( EditorSelection_Count( &s.ws.selection ) == 1u );
    const auto &box = s.Object( EditorSelection_At( &s.ws.selection, 0 ) )->bounds.box;
    CHECK( box.maximum.x - box.minimum.x == Catch::Approx( 32.0 ) ); // Turned as the instance was.
    CHECK( box.maximum.y - box.minimum.y == Catch::Approx( 64.0 ) );
}

TEST_CASE( "Editing a prefab in place changes every instance of it", "[map][gui][instances]" )
{
    session_t s;
    const u64 a = s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
    const u64 b = s.Box( { 64, 0, 0 }, { 128, 64, 32 } );
    s.Select( { a, b } );
    REQUIRE( s.Run( "map.prefab.create_instance prefabs/pair" ) == command_result_t::OK );
    const u64 first = EditorSelection_At( &s.ws.selection, 0 );
    REQUIRE( MapWorkspace_TranslateSelection( &s.ws, { 0, 512, 0 }, true ) ); // A second instance.
    const u64 second = EditorSelection_At( &s.ws.selection, 0 );
    REQUIRE( first != second );
    const f64 width = s.Object( second )->bounds.box.maximum.x - s.Object( second )->bounds.box.minimum.x;
    CHECK( width == Catch::Approx( 128.0 ) );

    // Edit the first: it becomes a group of two boxes.
    s.Select( { first } );
    REQUIRE( s.Run( "map.prefab.edit" ) == command_result_t::OK );
    CHECK_FALSE( MapInstances_IsInstance( &s.ws, first ) );
    REQUIRE( EditorSelection_Count( &s.ws.selection ) == 2u );
    const u64 group = MapDocument_GroupOf( s.ws.pDocument, EditorSelection_At( &s.ws.selection, 0 ) );
    CHECK( group != 0u );
    // Remove the smaller box from the prefab.
    u64 smaller = EditorSelection_At( &s.ws.selection, 0 );
    if ( s.Object( smaller )->bounds.box.maximum.z > 40.0 ) { smaller = EditorSelection_At( &s.ws.selection, 1 ); }
    s.Select( { smaller } );
    REQUIRE( MapWorkspace_DeleteSelection( &s.ws ) );
    REQUIRE( s.Run( "map.prefab.finish_edit" ) == command_result_t::OK );
    // Both instances are back and both show only the cube.
    REQUIRE( MapInstances_IsInstance( &s.ws, first ) );
    REQUIRE( MapInstances_IsInstance( &s.ws, second ) );
    for ( u64 instance : { first, second } ) {
        const auto &box = s.Object( instance )->bounds.box;
        CHECK( box.maximum.x - box.minimum.x == Catch::Approx( 64.0 ) );
        CHECK( box.maximum.z - box.minimum.z == Catch::Approx( 64.0 ) );
    }
    CHECK( s.Count( map_wire_kind_t::BRUSH ) == 0u ); // The editing group went.

    // Cancelling an edit leaves the prefab as it was.
    s.Select( { second } );
    REQUIRE( s.Run( "map.prefab.edit" ) == command_result_t::OK );
    REQUIRE( MapWorkspace_DeleteSelection( &s.ws ) ); // Everything in it...
    REQUIRE( s.Run( "map.prefab.cancel_edit" ) == command_result_t::OK );
    REQUIRE( MapInstances_IsInstance( &s.ws, second ) ); // ...and back unchanged.
    CHECK( s.Object( second )->bounds.box.maximum.z - s.Object( second )->bounds.box.minimum.z == Catch::Approx( 64.0 ) );
}

TEST_CASE( "An instance whose prefab file is missing shows a marker and says why", "[map][gui][instances]" )
{
    session_t s;
    const u64 a = s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
    s.Select( { a } );
    REQUIRE( s.Run( "map.prefab.create_instance prefabs/gone" ) == command_result_t::OK );
    const u64 instance = EditorSelection_At( &s.ws.selection, 0 );
    REQUIRE( QFile::remove( QDir( s.content.path() ).filePath( QStringLiteral( "prefabs/gone.cyprefab" ) ) ) );
    REQUIRE( s.Run( "map.prefab.reload" ) == command_result_t::OK );
    map_instance_info_t info{};
    REQUIRE( MapInstances_Info( &s.ws, instance, &info ) );
    CHECK_FALSE( info.bLoaded );
    CHECK( info.problem.contains( QStringLiteral( "not in the content folders" ) ) );
    const map_wire_object_t *object = s.Object( instance );
    REQUIRE( object != nullptr );
    CHECK( object->nLines == 12u ); // The marker box.
    CHECK( s.Run( "map.prefab.collapse" ) == command_result_t::FAILED );
}

TEST_CASE( "A map with an instance saves and opens with the same objects", "[map][gui][instances]" )
{
    session_t s;
    REQUIRE( MapWorkspace_Open( &s.ws, QStringLiteral( CYPHER_MAP_EXAMPLE_DIR "/facility.cymap" ) ).status == map_files_status_t::OK );
    const usize before = s.ws.wire.objects.nCount;
    CHECK( s.Count( map_wire_kind_t::PREFAB ) == 1u );
    const QString file = QDir( s.content.path() ).filePath( QStringLiteral( "copy.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &s.ws, file ).status == map_files_status_t::OK );
    CHECK( s.ws.wire.objects.nCount == before );
    REQUIRE( MapWorkspace_Open( &s.ws, file ).status == map_files_status_t::OK );
    CHECK( s.Count( map_wire_kind_t::PREFAB ) == 1u );
    CHECK( s.ws.wire.objects.nCount == before );
}

TEST_CASE( "Place Instance puts another instance of a prefab file at the map cursor", "[map][gui][instances]" )
{
    session_t s;
    const u64 a = s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
    s.Select( { a } );
    REQUIRE( s.Run( "map.prefab.create_instance prefabs/cube" ) == command_result_t::OK );
    MapWorkspace_SetCursor( &s.ws, { 500, 300, 0 }, 3u );
    REQUIRE( s.Run( "map.prefab.place_instance prefabs/cube.cyprefab" ) == command_result_t::OK );
    CHECK( s.Count( map_wire_kind_t::PREFAB ) == 2u );
    map_instance_info_t info{};
    REQUIRE( MapInstances_Info( &s.ws, EditorSelection_At( &s.ws.selection, 0 ), &info ) );
    CHECK( info.origin.x == 496.0 ); // On the 16-unit grid.
    CHECK( info.origin.y == 304.0 );
    CHECK( s.Run( "map.prefab.place_instance prefabs/nothing.cyprefab" ) == command_result_t::FAILED );
}

namespace
{
// Fail a map allocation only after QSaveFile has replaced the temporary test
// asset. This exercises the actual cross-resource ordering, not a mocked save.
struct prefab_allocation_t {
    usize calls{}, failOn{}, live{};
    bool afterSave{}, failedAfterSave{};
    QString file;
    QByteArray original;
};
void *PrefabAllocate( void *context, usize bytes, usize alignment ) noexcept
{
    auto &state = *static_cast<prefab_allocation_t *>( context );
    if ( ++state.calls == state.failOn ) { return nullptr; }
    if ( state.afterSave && !state.failedAfterSave ) {
        QFile file( state.file );
        if ( file.open( QIODevice::ReadOnly ) && file.readAll() != state.original ) {
            state.failedAfterSave = true;
            return nullptr;
        }
    }
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment );
    if ( memory != nullptr ) { ++state.live; }
    return memory;
}
void PrefabFree( void *context, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory != nullptr ) { --static_cast<prefab_allocation_t *>( context )->live; }
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}
QByteArray ReadPrefabTestFile( const QString &path )
{
    QFile file( path ); REQUIRE( file.open( QIODevice::ReadOnly ) ); return file.readAll();
}
QByteArray PrefabMapText( map_document_t *document )
{
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( MapDocument_Save( document, &text ) == map_status_t::OK );
    const auto view = TextBuffer_View( &text );
    const QByteArray result( view.pData, static_cast<qsizetype>( view.cchLength ) );
    TextBuffer_Shutdown( &text ); return result;
}
struct prefab_map_snapshot_t {
    map_document_t *document;
    QByteArray text;
    u64 geometryRevision, next, sourceNext, selectionRevision;
    usize steps, applied;
    undo_state_token_t token;
    const u64 *selection;
    usize count;
    explicit prefab_map_snapshot_t( map_workspace_t &ws ) : document( ws.pDocument ), text( PrefabMapText( document ) ),
        geometryRevision( document->geometry.revision ), next( document->nextId ), sourceNext( document->geometry.sourceIds.allocator.next.value ),
        selectionRevision( ws.selection.revision ), steps( EditorHistory_StepCount( &ws.history ) ), applied( EditorHistory_AppliedStepCount( &ws.history ) ),
        token( UndoRedo_StateToken( ws.history.pUndo ) ), selection( ws.selection.ids.pData ), count( ws.selection.ids.nCount ) {}
    void Check( map_workspace_t &ws ) const
    {
        CHECK( ws.pDocument == document ); CHECK( PrefabMapText( ws.pDocument ) == text );
        CHECK( ws.pDocument->geometry.revision == geometryRevision ); CHECK( ws.pDocument->nextId == next );
        CHECK( ws.pDocument->geometry.sourceIds.allocator.next.value == sourceNext );
        CHECK( ws.selection.ids.pData == selection ); CHECK( ws.selection.ids.nCount == count ); CHECK( ws.selection.revision == selectionRevision );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    }
};
} // namespace

TEST_CASE( "Prefab Finish retains and reports an external save when map publication fails", "[map][gui][instances][prefab-finish-failure]" )
{
    for ( const bool cancel : { false, true } ) {
        CAPTURE( cancel );
        prefab_allocation_t audit{}; allocator_t allocator{ PrefabAllocate, nullptr, PrefabFree, &audit };
        {
            session_t s( &allocator );
            s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
            REQUIRE( s.Run( "map.prefab.create_instance prefabs/partial" ) == command_result_t::OK );
            const u64 instance = EditorSelection_At( &s.ws.selection, 0u );
            audit.file = QDir( s.content.path() ).filePath( QStringLiteral( "prefabs/partial.cyprefab" ) );
            audit.original = ReadPrefabTestFile( audit.file );
            REQUIRE( s.Run( "map.prefab.edit" ) == command_result_t::OK );
            const u64 member = EditorSelection_At( &s.ws.selection, 0u );
            const u64 group = MapDocument_GroupOf( s.ws.pDocument, member ); REQUIRE( group != 0u );
            REQUIRE( MapWorkspace_TranslateSelection( &s.ws, { 0, 0, 32 } ) );
            prefab_map_snapshot_t before( s.ws );
            audit.afterSave = true;
            CHECK( s.Run( "map.prefab.finish_edit" ) == command_result_t::FAILED );
            audit.afterSave = false;
            REQUIRE( audit.failedAfterSave ); before.Check( s.ws );
            CHECK( MapDocument_GroupOf( s.ws.pDocument, member ) == group );
            CHECK_FALSE( MapInstances_IsInstance( &s.ws, instance ) );
            const QByteArray saved = ReadPrefabTestFile( audit.file ); CHECK( saved != audit.original );
            map_notice_t kind{}; const QString notice = MapNotice_Last( &s.ws, &kind );
            CHECK( kind == map_notice_t::WARNING ); CHECK( notice.contains( QStringLiteral( "saved on disk" ) ) );
            CHECK( notice.contains( QStringLiteral( "group" ) ) ); CHECK( notice.contains( QStringLiteral( "Retry" ) ) );
            REQUIRE( s.Run( cancel ? "map.prefab.cancel_edit" : "map.prefab.finish_edit" ) == command_result_t::OK );
            REQUIRE( MapInstances_IsInstance( &s.ws, instance ) );
            CHECK( EditorHistory_AppliedStepCount( &s.ws.history ) == before.applied + 1u );
            CHECK( s.Object( instance )->bounds.box.minimum.z == Catch::Approx( 32.0 ) );
            CHECK( s.Object( instance )->bounds.box.maximum.z == Catch::Approx( 96.0 ) );
            if ( cancel ) {
                CHECK( ReadPrefabTestFile( audit.file ) == saved );
                const QString stopped = MapNotice_Last( &s.ws, nullptr );
                CHECK( stopped.contains( QStringLiteral( "saved prefab changes remain on disk" ) ) );
                CHECK_FALSE( stopped.contains( QStringLiteral( "unchanged" ) ) );
            }
            REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK );
            CHECK( PrefabMapText( s.ws.pDocument ) == before.text );
            REQUIRE( MapWorkspace_Redo( &s.ws ) == editor_history_status_t::OK );
            REQUIRE( MapInstances_IsInstance( &s.ws, instance ) );
        }
        CHECK( audit.live == 0u );
    }
}

TEST_CASE( "Prefab edit capture failures preserve overrides and leave admission retryable", "[map][gui][instances][prefab-finish-failure]" )
{
    for ( const bool overrides : { false, true } ) {
        CAPTURE( overrides );
        prefab_allocation_t audit{}; allocator_t allocator{ PrefabAllocate, nullptr, PrefabFree, &audit };
        {
            session_t s( &allocator ); s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
            REQUIRE( s.Run( "map.prefab.create_instance prefabs/capture" ) == command_result_t::OK );
            const u64 instance = EditorSelection_At( &s.ws.selection, 0u );
            if ( overrides ) {
                map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( s.ws.pDocument, instance, &store );
                REQUIRE( record != nullptr ); REQUIRE( store != nullptr );
                auto *object = KeyValue_ObjectInsert( store->store.pDocument, record, StringView_FromCString( "overrides" ), key_value_type_t::OBJECT ); REQUIRE( object != nullptr );
                auto *value = KeyValue_ObjectInsert( store->store.pDocument, object, StringView_FromCString( "door.name" ), key_value_type_t::STRING ); REQUIRE( value != nullptr );
                REQUIRE( KeyValue_SetString( store->store.pDocument, value, StringView_FromCString( "owned override" ) ) );
            }
            prefab_map_snapshot_t before( s.ws );
            audit.failOn = audit.calls + 1u;
            CHECK( s.Run( "map.prefab.edit" ) == command_result_t::FAILED );
            audit.failOn = 0u; before.Check( s.ws );
            REQUIRE( s.Run( "map.prefab.edit" ) == command_result_t::OK );
            REQUIRE( s.Run( "map.prefab.cancel_edit" ) == command_result_t::OK );
            REQUIRE( MapInstances_IsInstance( &s.ws, instance ) );
            if ( overrides ) {
                const auto *record = MapDocument_FindObject( s.ws.pDocument, instance, nullptr );
                const auto *value = KeyValue_Find( KeyValue_Find( record, StringView_FromCString( "overrides" ) ), StringView_FromCString( "door.name" ) );
                string_view_t text{}; REQUIRE( KeyValue_GetString( value, &text ) ); CHECK( StringView_Equals( text, StringView_FromCString( "owned override" ) ) );
            }
        }
        CHECK( audit.live == 0u );
    }
}
