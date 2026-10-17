//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Hierarchy_Tests.cpp
//  Purpose: Tests Hammer 5's hierarchy commands: Parent to Last Selected
//           (the pick order, not the ID order, decides the parent), Unparent,
//           Select Parent and Select Children, and children following their
//           parent's move.
//
//  History:
//  - Created by Karlo Siric on 2026-10-14
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Hierarchy.h"
#include "CypherMapGui_Check.h"
#include "CypherMapGui_Notice.h"
#include "CypherMap_Hierarchy.h"
#include "CypherMap_Edit.h"
#include "CypherMap_Groups.h"
#include "CypherGeometry_Document.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QTemporaryDir>

#include <algorithm>
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
    session_t()
    {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &ws, &gui ) );
        REQUIRE( MapWorkspace_RegisterCommands( &ws, &gui.commands ) == command_registry_status_t::OK );
    }
    ~session_t() { MapWorkspace_Shutdown( &ws ); gui::EditorGui_Shutdown( &gui ); }
    u64 Entity( math::vec3d_t at )
    {
        u64 id = 0;
        REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( ws.pDocument->layers.pData[0].id ), StringView_FromCString( "info_target" ), at, &id ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &ws );
        return id;
    }
    // Picks one after another, as Shift+clicks would.
    void Pick( std::initializer_list<u64> order )
    {
        std::vector<u64> ids;
        for ( u64 id : order ) { ids.push_back( id ); MapWorkspace_SetSelection( &ws, ids.data(), ids.size() ); }
    }
    command_result_t Run( const char *command ) { return EditorCommands_Execute( &gui.commands, StringView_FromCString( command ), {} ); }
    std::vector<u64> Selection() { return std::vector<u64>( ws.selection.ids.pData, ws.selection.ids.pData + ws.selection.ids.nCount ); }
    u64 Parent( u64 id ) { return MapHierarchy_Parent( ws.pDocument, id ); }
    usize Steps() { return EditorHistory_AppliedStepCount( &ws.history ); }
    f64 X( u64 id ) { const auto *entity = MapWireframe_FindEntity( ws.wire, id ); REQUIRE( entity != nullptr ); return entity->origin.x; }
};

string_view_t HollowSv( const char *text ) { return StringView_FromCString( text ); }
std::string HollowText( map_document_t *map )
{
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    const auto status = MapDocument_Save( map, &text );
    const std::string saved( TextBuffer_Data( &text ), TextBuffer_Length( &text ) ); TextBuffer_Shutdown( &text );
    REQUIRE( status == map_status_t::OK ); return saved;
}

struct hollow_group_fixture_t {
    session_t s;
    u64 brush{}, owner{}, inner{}, outer{};
    hollow_group_fixture_t()
    {
        auto *map = s.ws.pDocument;
        REQUIRE( MapEdit_CreateBox( map, { { -64, -64, -64 }, { 64, 64, 64 } }, HollowSv( "materials/hollow.cymat" ), {}, &brush ) == map_status_t::OK );
        REQUIRE( MapDocument_AddLayer( map, HollowSv( "detail" ), HollowSv( "Detail" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryLayer( map, brush, HollowSv( "detail" ) ) == map_status_t::OK );
        REQUIRE( MapDocument_AddEntity( map, HollowSv( "detail" ), HollowSv( "func_shell" ), {}, &owner ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( map, brush, owner ) == map_status_t::OK );
        REQUIRE( MapDocument_AddGroup( map, HollowSv( "Shell" ), { &brush, 1 }, &inner ) == map_status_t::OK );
        REQUIRE( MapDocument_AddGroup( map, HollowSv( "Building" ), { &inner, 1 }, &outer ) == map_status_t::OK );
        (void)HollowText( map ); // Materialize authored geometry records before adding future fields.
        for ( const u64 id : { brush, inner, outer } ) {
            map_layer_store_t *store = nullptr; auto *record = MapDocument_FindObject( map, id, &store ); REQUIRE( record ); REQUIRE( store );
            auto *marker = KeyValue_ObjectInsert( store->store.pDocument, record, HollowSv( "future_lineage" ), key_value_type_t::U64 ); REQUIRE( marker );
            REQUIRE( KeyValue_SetU64( store->store.pDocument, marker, id + 10000 ) );
        }
        MapWorkspace_DocumentChanged( &s.ws ); MapWorkspace_SetTool( &s.ws, map_tool_t::SELECT ); MapWorkspace_SetElementMode( &s.ws, map_element_mode_t::OBJECTS ); s.Pick( { brush } );
    }
    void Check( map_document_t *map, const std::vector<u64> &fragments )
    {
        auto groups = MapGroups_All( map ); std::sort( groups.begin(), groups.end() ); CHECK( groups == std::vector<u64>{ inner, outer } );
        CHECK( MapGroups_Name( map, inner ) == "Shell" ); CHECK( MapGroups_Name( map, outer ) == "Building" );
        std::vector<u64> members; REQUIRE( MapGroups_Members( map, inner, members ) == map_status_t::OK ); CHECK( members == fragments );
        REQUIRE( MapGroups_Members( map, outer, members ) == map_status_t::OK ); CHECK( members == std::vector<u64>{ inner } );
        CHECK( MapGroups_Parent( map, inner ) == outer );
        for ( const u64 group : { inner, outer } ) {
            const auto *record = MapDocument_FindObject( map, group, nullptr ); REQUIRE( record ); u64 marker = 0;
            REQUIRE( KeyValue_GetU64( KeyValue_Find( record, HollowSv( "future_lineage" ) ), &marker ) ); CHECK( marker == group + 10000 );
        }
        for ( const u64 root : fragments ) {
            CHECK( MapGroups_Parent( map, root ) == inner );
            const auto *record = MapDocument_FindObject( map, root, nullptr ); REQUIRE( record ); u64 marker = 0;
            REQUIRE( KeyValue_GetU64( KeyValue_Find( record, HollowSv( "future_lineage" ) ), &marker ) ); CHECK( marker == brush + 10000 );
            const map_geometry_record_t *placement = nullptr;
            for ( usize i = 0; i < map->geometryRecords.nCount; ++i ) { if ( map->geometryRecords.pData[i].id == root ) { placement = &map->geometryRecords.pData[i]; break; } }
            REQUIRE( placement ); CHECK( placement->owner == owner ); REQUIRE( placement->iLayer < map->layers.nCount ); CHECK( StringView_Equals( HollowSv( map->layers.pData[placement->iLayer].id ), HollowSv( "detail" ) ) );
        }
    }
};

struct hollow_allocation_t { usize calls{}, live{}; };
void *HollowAllocate( void *context, usize bytes, usize alignment ) noexcept
{
    auto &audit = *static_cast<hollow_allocation_t *>( context );
    if ( audit.calls++ == 0 ) { return nullptr; }
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment ); if ( memory ) { ++audit.live; } return memory;
}
void HollowFree( void *context, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory ) { --static_cast<hollow_allocation_t *>( context )->live; } Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}
struct hollow_allocator_scope_t {
    gui::editor_gui_t &gui; const allocator_t *previous;
    hollow_allocator_scope_t( gui::editor_gui_t &target, const allocator_t *allocator ) : gui( target ), previous( target.pAllocator ) { gui.pAllocator = allocator; }
    ~hollow_allocator_scope_t() { gui.pAllocator = previous; }
};
} // namespace

TEST_CASE( "Registered Hollow keeps its nested group available through selection history and reopening", "[map][gui][hierarchy][hollow-group-lineage]" )
{
    hollow_group_fixture_t f; auto &s = f.s; const usize steps = s.Steps();
    REQUIRE( s.Run( "map.group.select" ) == command_result_t::OK ); REQUIRE( s.Selection() == std::vector<u64>{ f.brush } );
    const auto original = HollowText( s.ws.pDocument );
    REQUIRE( s.Run( "map.brush.hollow" ) == command_result_t::OK ); REQUIRE( s.Steps() == steps + 1 );
    const auto fragments = s.Selection(); REQUIRE( fragments.size() == 6 ); f.Check( s.ws.pDocument, fragments );
    CHECK( geometry::GeometryDocument_FindBrush( &s.ws.pDocument->geometry, { f.brush } ) == nullptr );
    const auto hollowed = HollowText( s.ws.pDocument );
    s.Pick( { fragments.front() } ); REQUIRE( s.Run( "map.group.select" ) == command_result_t::OK ); CHECK( s.Selection() == fragments ); CHECK( s.Steps() == steps + 1 );
    REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK ); CHECK( HollowText( s.ws.pDocument ) == original ); CHECK( s.Selection() == std::vector<u64>{ f.brush } ); f.Check( s.ws.pDocument, { f.brush } );
    REQUIRE( MapWorkspace_Redo( &s.ws ) == editor_history_status_t::OK ); CHECK( HollowText( s.ws.pDocument ) == hollowed ); CHECK( s.Selection() == fragments ); f.Check( s.ws.pDocument, fragments );
    QTemporaryDir directory; REQUIRE( directory.isValid() ); const auto path = directory.path() + QStringLiteral( "/grouped-shell.cymap" );
    REQUIRE( MapWorkspace_SaveAs( &s.ws, path ).status == map_files_status_t::OK );
    session_t loaded; REQUIRE( MapWorkspace_Open( &loaded.ws, path ).status == map_files_status_t::OK ); f.Check( loaded.ws.pDocument, fragments );
    loaded.Pick( { fragments.back() } ); REQUIRE( loaded.Run( "map.group.select" ) == command_result_t::OK ); CHECK( loaded.Selection() == fragments );
}

TEST_CASE( "Hollow publication allocation refusal preserves the original group and retries in one step", "[map][gui][hierarchy][hollow-group-lineage][allocation]" )
{
    hollow_allocation_t audit{}; const allocator_t allocator{ &HollowAllocate, nullptr, &HollowFree, &audit };
    {
        hollow_group_fixture_t f; auto &s = f.s; const auto before = HollowText( s.ws.pDocument );
        const auto *document = s.ws.pDocument; const auto *wire = s.ws.wire.points.pData; const auto *roots = s.ws.selection.ids.pData;
        const auto next = document->nextId, revision = document->geometry.revision, selectionRevision = s.ws.selection.revision;
        const auto token = UndoRedo_StateToken( s.ws.history.pUndo ); const auto steps = s.Steps();
        {
            hollow_allocator_scope_t scope( s.gui, &allocator ); CHECK( s.Run( "map.brush.hollow" ) == command_result_t::FAILED );
        }
        CHECK( audit.calls > 0 ); CHECK( audit.live == 0 ); CHECK( s.ws.lastEdit.status == map_status_t::OUT_OF_MEMORY );
        CHECK( s.ws.pDocument == document ); CHECK( document->nextId == next ); CHECK( document->geometry.revision == revision ); CHECK( s.ws.wire.points.pData == wire );
        CHECK( s.ws.selection.ids.pData == roots ); CHECK( s.ws.selection.revision == selectionRevision ); CHECK( s.Selection() == std::vector<u64>{ f.brush } );
        CHECK( s.Steps() == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( s.ws.history.pUndo ) ) ); CHECK( HollowText( s.ws.pDocument ) == before ); f.Check( s.ws.pDocument, { f.brush } );
        REQUIRE( s.Run( "map.brush.hollow" ) == command_result_t::OK ); CHECK( s.Steps() == steps + 1 ); const auto fragments = s.Selection(); REQUIRE( fragments.size() == 6 ); f.Check( s.ws.pDocument, fragments );
        REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK ); CHECK( HollowText( s.ws.pDocument ) == before );
        REQUIRE( MapWorkspace_Redo( &s.ws ) == editor_history_status_t::OK ); f.Check( s.ws.pDocument, fragments );
    }
    CHECK( audit.live == 0 );
}

TEST_CASE( "Parent to Last Selected attaches to the entity picked last", "[map][gui][hierarchy]" )
{
    session_t s;
    // The parent has the smallest ID: the selection's sorted order would put it first.
    const u64 train = s.Entity( { 0, 0, 0 } ), light = s.Entity( { 32, 0, 64 } ), sound = s.Entity( { -32, 0, 16 } );
    REQUIRE( train < light );
    s.Pick( { light } );
    CHECK( s.Run( "map.entity.reparent" ) == command_result_t::DISABLED ); // One entity is not enough.
    s.Pick( { light, sound, train } );
    CHECK( MapHierarchy_SelectedEntities( &s.ws ) == std::vector<u64>{ light, sound, train } );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.entity.reparent" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1u );
    CHECK( s.Parent( light ) == train );
    CHECK( s.Parent( sound ) == train );
    CHECK( s.Parent( train ) == 0u );
    CHECK( s.Selection() == std::vector<u64>{ train, light, sound } ); // Kept.
    // Again: nothing to change, no step.
    REQUIRE( s.Run( "map.entity.reparent" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1u );
    // The other way round would be a loop.
    s.Pick( { train, light } );
    CHECK( s.Run( "map.entity.reparent" ) == command_result_t::FAILED );
    CHECK( MapNotice_Last( &s.ws ).contains( QStringLiteral( "loop" ) ) );
    CHECK( s.Parent( train ) == 0u );

    // Moving the parent moves the children; a child moves alone.
    s.Pick( { train } );
    REQUIRE( MapWorkspace_TranslateSelection( &s.ws, { 64, 0, 0 } ) );
    CHECK( s.X( train ) == Catch::Approx( 64.0 ) );
    CHECK( s.X( light ) == Catch::Approx( 96.0 ) );
    CHECK( s.X( sound ) == Catch::Approx( 32.0 ) );
    s.Pick( { light } );
    REQUIRE( MapWorkspace_TranslateSelection( &s.ws, { 16, 0, 0 } ) );
    CHECK( s.X( train ) == Catch::Approx( 64.0 ) );
    CHECK( s.X( light ) == Catch::Approx( 112.0 ) );
}

TEST_CASE( "Select Parent, Select Children and Unparent walk the hierarchy", "[map][gui][hierarchy]" )
{
    session_t s;
    const u64 train = s.Entity( { 0, 0, 0 } ), light = s.Entity( { 32, 0, 64 } ), spark = s.Entity( { 32, 0, 80 } ), loose = s.Entity( { 400, 0, 0 } );
    s.Pick( { light, train } );
    REQUIRE( s.Run( "map.entity.reparent" ) == command_result_t::OK );
    s.Pick( { spark, light } );
    REQUIRE( s.Run( "map.entity.reparent" ) == command_result_t::OK );
    // Children at every depth; the parent of each.
    s.Pick( { train } );
    REQUIRE( s.Run( "map.select.children" ) == command_result_t::OK );
    CHECK( s.Selection() == std::vector<u64>{ light, spark } );
    s.Pick( { spark } );
    REQUIRE( s.Run( "map.select.parent" ) == command_result_t::OK );
    CHECK( s.Selection() == std::vector<u64>{ light } );
    s.Pick( { loose } );
    CHECK( s.Run( "map.select.parent" ) == command_result_t::DISABLED );
    CHECK( s.Run( "map.select.children" ) == command_result_t::DISABLED );
    // Unparent detaches what is selected and hangs from something; Undo puts it back.
    s.Pick( { light, loose } );
    const usize steps = s.Steps();
    REQUIRE( s.Run( "map.entity.unparent" ) == command_result_t::OK );
    CHECK( s.Steps() == steps + 1u );
    CHECK( s.Parent( light ) == 0u );
    CHECK( s.Parent( spark ) == light ); // Its own children stay.
    REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK );
    CHECK( s.Parent( light ) == train );
}

TEST_CASE( "Check for Problems finds an entity whose parent is gone and detaches it", "[map][gui][hierarchy]" )
{
    session_t s;
    const u64 train = s.Entity( { 0, 0, 0 } ), light = s.Entity( { 32, 0, 64 } );
    REQUIRE( MapHierarchy_SetParent( s.ws.pDocument, { &light, 1 }, train ) == map_status_t::OK );
    // Removed directly (not through Delete, which would take the child too).
    REQUIRE( MapDocument_RemoveObject( s.ws.pDocument, train ) == map_status_t::OK );
    MapWorkspace_DocumentChanged( &s.ws );
    const auto issues = MapCheck_Run( &s.ws );
    const auto found = std::find_if( issues.begin(), issues.end(), []( const map_issue_t &issue ) { return issue.kind == QLatin1String( "missing_parent" ); } );
    REQUIRE( found != issues.end() );
    CHECK( found->id == light );
    CHECK_FALSE( MapCheck_FixLabel( *found ).isEmpty() );
    CHECK( MapCheck_Fix( &s.ws, { *found } ) == 1 );
    CHECK( s.Parent( light ) == 0u );
}
