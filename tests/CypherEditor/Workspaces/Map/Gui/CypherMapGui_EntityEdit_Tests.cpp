//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Entity property edits publish immutable snapshots while retaining the
// selected geometry and its authored face identity.
//////////////////////////////////////////////////////////////////////////
#include "CypherMapGui_Workspace.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QTemporaryDir>
#include <filesystem>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{
string_view_t Text( const char *value ) { return StringView_FromCString( value ); }

struct allocation_failure_t { usize calls{ 0 }, failOn{ 0 }, live{ 0 }; };
void *Allocate( void *context, usize bytes, usize alignment ) noexcept
{
    auto &state = *static_cast<allocation_failure_t *>( context );
    if ( ++state.calls == state.failOn ) { return nullptr; }
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment );
    if ( memory != nullptr ) { ++state.live; }
    return memory;
}
void Free( void *context, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory != nullptr ) { --static_cast<allocation_failure_t *>( context )->live; }
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}

struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
    explicit session_t( const allocator_t *allocator = Allocator_GetSystem() )
    {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, allocator ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        const QString path = QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
        REQUIRE( MapWorkspace_Open( &workspace, path ).status == map_files_status_t::OK );
    }
    ~session_t() { MapWorkspace_Shutdown( &workspace ); gui::EditorGui_Shutdown( &gui ); }
};

struct value_t {
    key_value_document_t *document{ nullptr };
    explicit value_t( key_value_type_t type )
    {
        key_value_document_desc_t desc{}; desc.pAllocator = Allocator_GetSystem();
        document = KeyValue_CreateDocument( desc );
        REQUIRE( document != nullptr );
        REQUIRE( KeyValue_SetRootType( document, type ) );
    }
    ~value_t() { KeyValue_DestroyDocument( document ); }
    key_value_t *Node() const { return KeyValue_Root( document ); }
};

const key_value_t *Property( map_document_t *document, u64 id, const char *key )
{
    const auto *record = MapDocument_FindObject( document, id, nullptr );
    return KeyValue_Find( KeyValue_Find( record, Text( "properties" ) ), Text( key ) );
}

void CheckBoolean( map_document_t *document, u64 id, const char *key, bool expected )
{
    bool_t value = CY_FALSE;
    REQUIRE( KeyValue_GetBool( Property( document, id, key ), &value ) );
    CHECK( ( value != CY_FALSE ) == expected );
}

void CheckString( map_document_t *document, u64 id, const char *key, const char *expected )
{
    string_view_t value{};
    REQUIRE( KeyValue_GetString( Property( document, id, key ), &value ) );
    CHECK( StringView_Equals( value, Text( expected ) ) );
}

void CheckSelection( const map_workspace_t &ws, span_t<const u64> expected )
{
    REQUIRE( ws.selection.ids.nCount == expected.nCount );
    for ( usize i = 0; i < expected.nCount; ++i ) { CHECK( ws.selection.ids.pData[i] == expected.pData[i] ); }
}
}

TEST_CASE( "Entity property edits resolve multiple owners once and retain selected children", "[map][gui][entity-edit][owners][snapshot]" )
{
    session_t session; auto &ws = session.workspace;
    const u64 selection[]{ 110, 111, 150, 151 };
    MapWorkspace_SetSelection( &ws, selection, 4 );
    REQUIRE( MapWorkspace_CanEditEntityProperties( &ws ) );
    const auto geometryRevision = ws.pDocument->geometry.revision;
    const auto nextId = ws.pDocument->nextId;
    value_t disabled( key_value_type_t::BOOL );
    REQUIRE( KeyValue_SetBool( disabled.document, disabled.Node(), CY_TRUE ) );
    REQUIRE( MapWorkspace_SetEntityProperty( &ws, Text( "start_disabled" ), disabled.Node() ) );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    CheckBoolean( ws.pDocument, 110, "start_disabled", true );
    CheckBoolean( ws.pDocument, 150, "start_disabled", true );
    CHECK( Property( ws.pDocument, 111, "start_disabled" ) == nullptr );
    CHECK( Property( ws.pDocument, 151, "start_disabled" ) == nullptr );
    CHECK( ws.pDocument->geometry.revision == geometryRevision );
    CHECK( ws.pDocument->nextId == nextId );
    CheckSelection( ws, { selection, 4 } );
    const auto *document = ws.pDocument;
    const auto token = UndoRedo_StateToken( ws.history.pUndo );
    CHECK_FALSE( MapWorkspace_SetEntityProperty( &ws, Text( "start_disabled" ), disabled.Node() ) );
    CHECK( ws.pDocument == document );
    CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );

    REQUIRE( MapWorkspace_RenameEntityProperty( &ws, Text( "start_disabled" ), Text( "disabled" ) ) );
    CHECK( EditorHistory_StepCount( &ws.history ) == 2 );
    CheckBoolean( ws.pDocument, 110, "disabled", true );
    CheckBoolean( ws.pDocument, 150, "disabled", true );
    CHECK( Property( ws.pDocument, 110, "start_disabled" ) == nullptr );
    REQUIRE( MapWorkspace_RemoveEntityProperty( &ws, Text( "disabled" ) ) );
    CHECK( EditorHistory_StepCount( &ws.history ) == 3 );
    CHECK( Property( ws.pDocument, 110, "disabled" ) == nullptr );
    CHECK( Property( ws.pDocument, 150, "disabled" ) == nullptr );
    CHECK_FALSE( MapWorkspace_RemoveEntityProperty( &ws, Text( "disabled" ) ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CheckBoolean( ws.pDocument, 110, "disabled", true );
    CheckBoolean( ws.pDocument, 150, "disabled", true );
    CheckSelection( ws, { selection, 4 } );
}

TEST_CASE( "Entity keys survive save undo redo without losing a selected trigger face", "[map][gui][entity-edit][persistence][face]" )
{
    session_t session; auto &ws = session.workspace;
    MapWorkspace_SelectBrushFace( &ws, 111, 1310 );
    REQUIRE( MapWorkspace_HasBrushFace( &ws ) );
    const auto geometryRevision = ws.pDocument->geometry.revision;
    value_t script( key_value_type_t::STRING );
    REQUIRE( KeyValue_SetString( script.document, script.Node(), Text( "scripts/waves/start_wave.cy" ) ) );
    REQUIRE( MapWorkspace_SetEntityProperty( &ws, Text( "script" ), script.Node() ) );
    CheckString( ws.pDocument, 110, "script", "scripts/waves/start_wave.cy" );
    CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1310 );
    CHECK( ws.pDocument->geometry.revision == geometryRevision );
    CHECK( MapWireframe_FindObject( ws.wire, 111 )->owner == 110 );
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const QString path = folder.filePath( QStringLiteral( "entity_snapshot.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK );
    CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    map_document_t loaded{};
    REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
    CheckString( &loaded, 110, "script", "scripts/waves/start_wave.cy" );
    CHECK( Property( &loaded, 111, "script" ) == nullptr );
    MapWorkspace_Select( &ws, 140, MAP_SELECT_REPLACE );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( Property( ws.pDocument, 110, "script" ) == nullptr );
    const u64 expected[]{ 111 }; CheckSelection( ws, { expected, 1 } );
    CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1310 );
    CHECK( MapWorkspace_IsModified( &ws ) );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    CheckString( ws.pDocument, 110, "script", "scripts/waves/start_wave.cy" );
    CheckSelection( ws, { expected, 1 } );
    CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1310 );
    CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
}

TEST_CASE( "Light and logic identity edits refresh wire metadata while keeping typed keys", "[map][gui][entity-edit][identity][typed]" )
{
    session_t session; auto &ws = session.workspace;
    MapWorkspace_Select( &ws, 140, MAP_SELECT_REPLACE );
    value_t intensity( key_value_type_t::F64 );
    REQUIRE( KeyValue_SetF64( intensity.document, intensity.Node(), 1024.5 ) );
    REQUIRE( MapWorkspace_SetEntityProperty( &ws, Text( "intensity" ), intensity.Node() ) );
    f64 actual = 0;
    REQUIRE( KeyValue_GetF64( Property( ws.pDocument, 140, "intensity" ), &actual ) );
    CHECK( actual == 1024.5 );
    REQUIRE( MapWorkspace_SetEntityIdentityField( &ws, Text( "name" ), Text( "preview_light" ) ) );
    REQUIRE( MapWireframe_FindEntity( ws.wire, 140 ) != nullptr );
    CHECK( QString::fromUtf8( MapWireframe_FindEntity( ws.wire, 140 )->name ) == QStringLiteral( "preview_light" ) );
    CHECK_FALSE( MapWorkspace_SetEntityIdentityField( &ws, Text( "name" ), Text( "preview_light" ) ) );
    REQUIRE( MapWorkspace_SetEntityIdentityField( &ws, Text( "class" ), Text( "light_spot" ) ) );
    CHECK( QString::fromUtf8( MapWireframe_FindEntity( ws.wire, 140 )->className ) == QStringLiteral( "light_spot" ) );
    REQUIRE( KeyValue_GetF64( Property( ws.pDocument, 140, "intensity" ), &actual ) );
    CHECK( actual == 1024.5 );

    // Logic entities without an origin have no pickable helper object.
    MapWorkspace_Select( &ws, 170, MAP_SELECT_REPLACE );
    REQUIRE( MapWorkspace_CanEditEntityProperties( &ws ) );
    REQUIRE( MapWorkspace_SetEntityIdentityField( &ws, Text( "name" ), Text( "wave_logic" ) ) );
    CHECK( QString::fromUtf8( MapWireframe_FindEntity( ws.wire, 170 )->name ) == QStringLiteral( "wave_logic" ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    CHECK( QString::fromUtf8( MapWireframe_FindEntity( ws.wire, 170 )->name ) == QStringLiteral( "spawn_music" ) );
}

TEST_CASE( "Rejected entity metadata edits keep live state and history unchanged", "[map][gui][entity-edit][atomic]" )
{
    session_t session; auto &ws = session.workspace;
    MapWorkspace_SelectBrushFace( &ws, 111, 1310 );
    const auto *document = ws.pDocument;
    const auto *points = ws.wire.points.pData;
    const auto *selection = ws.selection.ids.pData;
    const auto selectionRevision = ws.selection.revision;
    const auto token = UndoRedo_StateToken( ws.history.pUndo );
    CHECK_FALSE( MapWorkspace_RenameEntityProperty( &ws, Text( "filter" ), Text( "start_disabled" ) ) );
    CHECK_FALSE( MapWorkspace_SetEntityIdentityField( &ws, Text( "id" ), Text( "20" ) ) );
    CHECK_FALSE( MapWorkspace_SetEntityIdentityField( &ws, Text( "class" ), Text( "" ) ) );
    CHECK_FALSE( MapWorkspace_SetEntityProperty( &ws, Text( "filter" ), nullptr ) );
    CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == points );
    CHECK( ws.selection.ids.pData == selection ); CHECK( ws.selection.revision == selectionRevision );
    CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1310 );
    CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );

    ws.pDocument->bReadOnly = CY_TRUE;
    CHECK_FALSE( MapWorkspace_CanEditEntityProperties( &ws ) );
    CHECK_FALSE( MapWorkspace_RemoveEntityProperty( &ws, Text( "filter" ) ) );
    ws.pDocument->bReadOnly = CY_FALSE;
    ws.editPreview.bActive = CY_TRUE;
    CHECK_FALSE( MapWorkspace_CanEditEntityProperties( &ws ) );
    CHECK_FALSE( MapWorkspace_RemoveEntityProperty( &ws, Text( "filter" ) ) );
    ws.editPreview.bActive = CY_FALSE;
    REQUIRE( EditorHistory_Begin( &ws.history, Text( "Other edit" ) ) == editor_history_status_t::OK );
    CHECK_FALSE( MapWorkspace_CanEditEntityProperties( &ws ) );
    CHECK_FALSE( MapWorkspace_RemoveEntityProperty( &ws, Text( "filter" ) ) );
    EditorHistory_Cancel( &ws.history );
    CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == points );
    CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
    const u64 mixed[]{ 111, 1000 }; MapWorkspace_SetSelection( &ws, mixed, 2 );
    CHECK_FALSE( MapWorkspace_CanEditEntityProperties( &ws ) );
    CHECK_FALSE( MapWorkspace_RemoveEntityProperty( &ws, Text( "filter" ) ) );
    CHECK( ws.pDocument == document ); CheckSelection( ws, { mixed, 2 } );
    CheckString( ws.pDocument, 110, "filter", "players" );
    CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
}

TEST_CASE( "Entity edit allocation failures cannot publish partial keys selection or history", "[map][gui][entity-edit][allocation][atomic]" )
{
    allocation_failure_t audit{}; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
    value_t script( key_value_type_t::STRING );
    REQUIRE( KeyValue_SetString( script.document, script.Node(), Text( "scripts/trigger.cy" ) ) );
    usize successfulCalls = 0;
    {
        session_t session( &allocator ); auto &ws = session.workspace;
        MapWorkspace_SelectBrushFace( &ws, 111, 1310 );
        audit.calls = 0;
        REQUIRE( MapWorkspace_SetEntityProperty( &ws, Text( "script" ), script.Node() ) );
        successfulCalls = audit.calls;
        REQUIRE( successfulCalls > 4 );
    }
    REQUIRE( audit.live == 0 );
    for ( const usize failOn : { usize{ 1 }, usize{ 2 }, successfulCalls / 2, successfulCalls } ) {
        CAPTURE( failOn ); audit.failOn = 0;
        {
            session_t session( &allocator ); auto &ws = session.workspace;
            MapWorkspace_SelectBrushFace( &ws, 111, 1310 );
            const auto *document = ws.pDocument;
            const auto *points = ws.wire.points.pData;
            const auto *selection = ws.selection.ids.pData;
            const auto selectionRevision = ws.selection.revision;
            const auto geometryRevision = document->geometry.revision;
            const auto token = UndoRedo_StateToken( ws.history.pUndo );
            const auto live = audit.live;
            audit.calls = 0; audit.failOn = failOn;
            CHECK_FALSE( MapWorkspace_SetEntityProperty( &ws, Text( "script" ), script.Node() ) );
            CHECK( audit.calls >= failOn ); CHECK( audit.live == live );
            CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == points );
            CHECK( ws.selection.ids.pData == selection ); CHECK( ws.selection.revision == selectionRevision );
            CHECK( ws.pDocument->geometry.revision == geometryRevision );
            CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1310 );
            CHECK( Property( ws.pDocument, 110, "script" ) == nullptr );
            CHECK( EditorHistory_StepCount( &ws.history ) == 0 );
            CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
            audit.calls = 0; audit.failOn = 0;
            REQUIRE( MapWorkspace_SetEntityProperty( &ws, Text( "script" ), script.Node() ) );
            CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
        }
        CHECK( audit.live == 0 );
    }
}
