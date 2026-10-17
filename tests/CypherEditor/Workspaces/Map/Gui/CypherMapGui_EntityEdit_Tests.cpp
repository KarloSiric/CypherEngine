//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Entity property edits publish immutable snapshots while retaining the
// selected geometry and its authored face identity.
//////////////////////////////////////////////////////////////////////////
#include "CypherMapGui_Workspace.h"
#include "CypherMapGui_EntityIo.h"
#include "CypherMapGui_Layers.h"
#include "CypherMapGui_Notice.h"
#include "CypherMap_EntityOutputs.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QDialog>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
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
    MapWorkspace_SelectBrushFace( &ws, 111, 1350 );
    REQUIRE( MapWorkspace_HasBrushFace( &ws ) );
    const auto geometryRevision = ws.pDocument->geometry.revision;
    value_t script( key_value_type_t::STRING );
    REQUIRE( KeyValue_SetString( script.document, script.Node(), Text( "scripts/waves/start_wave.cy" ) ) );
    REQUIRE( MapWorkspace_SetEntityProperty( &ws, Text( "script" ), script.Node() ) );
    CheckString( ws.pDocument, 110, "script", "scripts/waves/start_wave.cy" );
    CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1350 );
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
    CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1350 );
    CHECK( MapWorkspace_IsModified( &ws ) );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK );
    CheckString( ws.pDocument, 110, "script", "scripts/waves/start_wave.cy" );
    CheckSelection( ws, { expected, 1 } );
    CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1350 );
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
    MapWorkspace_SelectBrushFace( &ws, 111, 1350 );
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
    CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1350 );
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
        MapWorkspace_SelectBrushFace( &ws, 111, 1350 );
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
            MapWorkspace_SelectBrushFace( &ws, 111, 1350 );
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
            CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1350 );
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

namespace
{
QString IoFacilityPath()
{
    return QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
}
void IoSelectChild( map_workspace_t &ws )
{
    MapWorkspace_SetTool( &ws, map_tool_t::SELECT );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    MapWorkspace_SelectBrushFace( &ws, 111, 1350 );
    REQUIRE( MapWorkspace_HasBrushFace( &ws ) ); REQUIRE( MapEntityIo_Entity( &ws ) == 110 );
}
std::vector<map_output_t> IoRead( map_document_t *document )
{
    std::vector<map_output_t> result;
    REQUIRE( MapEntityOutputs_Read( document, 110, result ) == map_status_t::OK );
    return result;
}
void IoEqual( const std::vector<map_output_t> &actual, const std::vector<map_output_t> &expected )
{
    REQUIRE( actual.size() == expected.size() );
    for ( usize i = 0; i < actual.size(); ++i ) {
        CHECK( actual[i].output == expected[i].output ); CHECK( actual[i].target == expected[i].target );
        CHECK( actual[i].input == expected[i].input ); CHECK( actual[i].parameter == expected[i].parameter );
        CHECK( actual[i].delay == expected[i].delay ); CHECK( actual[i].times == expected[i].times );
    }
}
QTableWidget *IoTable( QWidget *parent )
{
    auto *table = parent->findChild<QTableWidget *>( QStringLiteral( "MapEntityOutputs" ) ); REQUIRE( table ); return table;
}
QPushButton *IoAdd( QWidget *parent )
{
    auto *button = parent->findChild<QPushButton *>( QStringLiteral( "MapEntityOutputAdd" ) ); REQUIRE( button ); return button;
}
QDialog *IoShow( map_workspace_t &ws )
{
    auto *dialog = MapEntityIo_Show( &ws, nullptr ); REQUIRE( dialog );
    QCoreApplication::processEvents(); REQUIRE( IoTable( dialog )->rowCount() == 1 ); return dialog;
}
void IoTableMatches( QTableWidget *table, const std::vector<map_output_t> &outputs )
{
    REQUIRE( table->rowCount() == static_cast<int>( outputs.size() ) );
    for ( usize i = 0; i < outputs.size(); ++i ) {
        const int row = static_cast<int>( i ); const auto &o = outputs[i];
        CHECK( table->item( row, 0 )->text() == QString::fromStdString( o.output ) );
        CHECK( table->item( row, 1 )->text() == QString::fromStdString( o.target ) );
        CHECK( table->item( row, 2 )->text() == QString::fromStdString( o.input ) );
        CHECK( table->item( row, 3 )->text() == QString::fromStdString( o.parameter ) );
        CHECK( table->item( row, 4 )->data( Qt::EditRole ).toDouble() == o.delay );
        CHECK( table->item( row, 5 )->data( Qt::EditRole ).toLongLong() == o.times );
    }
}
struct io_published_t {
    const map_document_t *document;
    const void *points, *selection;
    u64 geometry, revision, next, sourceNext, faceObject, faceSide;
    usize applied, steps;
    undo_state_token_t token;
    std::vector<u64> selected;
    explicit io_published_t( const map_workspace_t &ws ) : document( ws.pDocument ), points( ws.wire.points.pData ), selection( ws.selection.ids.pData ),
        geometry( ws.pDocument->geometry.revision ), revision( ws.selection.revision ), next( ws.pDocument->nextId ),
        sourceNext( ws.pDocument->geometry.sourceIds.allocator.next.value ), faceObject( ws.selectedBrushFaceObject ), faceSide( ws.selectedBrushFaceSide ),
        applied( EditorHistory_AppliedStepCount( &ws.history ) ), steps( EditorHistory_StepCount( &ws.history ) ), token( UndoRedo_StateToken( ws.history.pUndo ) )
    { if ( ws.selection.ids.nCount != 0 ) { selected.assign( ws.selection.ids.pData, ws.selection.ids.pData + ws.selection.ids.nCount ); } }
    void Check( const map_workspace_t &ws ) const
    {
        CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == points ); CHECK( ws.selection.ids.pData == selection );
        CHECK( ws.pDocument->geometry.revision == geometry ); CHECK( ws.selection.revision == revision );
        CHECK( ws.pDocument->nextId == next ); CHECK( ws.pDocument->geometry.sourceIds.allocator.next.value == sourceNext );
        CHECK( ws.selectedBrushFaceObject == faceObject ); CHECK( ws.selectedBrushFaceSide == faceSide );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == applied ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
        CheckSelection( ws, { selected.data(), selected.size() } );
    }
};
void IoPrepareRedo( map_workspace_t &ws )
{
    value_t value( key_value_type_t::STRING ); REQUIRE( KeyValue_SetString( value.document, value.Node(), Text( "redo survives" ) ) );
    REQUIRE( MapWorkspace_SetEntityProperty( &ws, Text( "io_redo_probe" ), value.Node() ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); REQUIRE( EditorHistory_CanRedo( &ws.history ) );
    REQUIRE( MapWorkspace_HasBrushFace( &ws ) );
}
}

TEST_CASE( "Entity I/O coalesces table cells while retaining the child face through history and persistence", "[map][gui][entity-io][entity-io-capture][persistence]" )
{
    session_t session; auto &ws = session.workspace; IoSelectChild( ws );
    auto *dialog = IoShow( ws ); auto *table = IoTable( dialog ); const auto original = IoRead( ws.pDocument );
    const usize steps = EditorHistory_AppliedStepCount( &ws.history ); const u64 geometry = ws.pDocument->geometry.revision, next = ws.pDocument->nextId;
    table->item( 0, 1 )->setText( QStringLiteral( "!self" ) );
    table->item( 0, 3 )->setText( QStringLiteral( "coalesced parameter" ) );
    table->item( 0, 4 )->setData( Qt::EditRole, 2.25 );
    CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps ); QCoreApplication::processEvents();
    auto expected = original; expected[0].target = "!self"; expected[0].parameter = "coalesced parameter"; expected[0].delay = 2.25;
    IoEqual( IoRead( ws.pDocument ), expected ); IoTableMatches( table, expected );
    CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + 1 ); CHECK( ws.pDocument->geometry.revision == geometry ); CHECK( ws.pDocument->nextId == next );
    CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1350 );
    MapWorkspace_Select( &ws, 140, MAP_SELECT_REPLACE );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); IoEqual( IoRead( ws.pDocument ), original );
    CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1350 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); IoEqual( IoRead( ws.pDocument ), expected );
    CHECK( ws.selectedBrushFaceObject == 111 ); CHECK( ws.selectedBrushFaceSide == 1350 );
    QTemporaryDir directory; REQUIRE( directory.isValid() ); const auto path = directory.filePath( QStringLiteral( "entity_io.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &ws, path ).status == map_files_status_t::OK ); session_t reopened;
    REQUIRE( MapWorkspace_Open( &reopened.workspace, path ).status == map_files_status_t::OK ); IoEqual( IoRead( reopened.workspace.pDocument ), expected );
    CHECK( MapWireframe_FindObject( reopened.workspace.wire, 111 )->owner == 110 );
}

TEST_CASE( "Entity I/O invalidates queued cells on selection or document replacement even with the same entity ID", "[map][gui][entity-io][entity-io-capture][stale]" )
{
    enum class change_t { SELECTION, NEW, OPEN, UNDO }; change_t change = change_t::SELECTION;
    SECTION( "Selection goes away and returns before the timer" ) {}
    SECTION( "New discards the pending old document write" ) { change = change_t::NEW; }
    SECTION( "Open restores the same source entity ID" ) { change = change_t::OPEN; }
    SECTION( "Undo and Redo restore the same entity before the timer" ) { change = change_t::UNDO; }
    session_t session; auto &ws = session.workspace; IoSelectChild( ws );
    if ( change == change_t::UNDO ) {
        value_t value( key_value_type_t::BOOL ); REQUIRE( KeyValue_SetBool( value.document, value.Node(), CY_TRUE ) );
        REQUIRE( MapWorkspace_SetEntityProperty( &ws, Text( "io_history_probe" ), value.Node() ) );
    }
    auto *table = IoTable( IoShow( ws ) ); const auto original = IoRead( ws.pDocument );
    table->item( 0, 3 )->setText( QStringLiteral( "must never publish" ) );
    if ( change == change_t::SELECTION ) { MapWorkspace_Select( &ws, 140, MAP_SELECT_REPLACE ); IoSelectChild( ws ); }
    else if ( change == change_t::NEW ) { REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK ); }
    else if ( change == change_t::OPEN ) { REQUIRE( MapWorkspace_Open( &ws, IoFacilityPath() ).status == map_files_status_t::OK ); IoSelectChild( ws ); }
    else { REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); }
    const io_published_t afterChange( ws ); QCoreApplication::processEvents(); afterChange.Check( ws );
    if ( change == change_t::NEW ) { CHECK( table->rowCount() == 0 ); CHECK( MapEntityIo_Entity( &ws ) == 0 ); }
    else {
        IoEqual( IoRead( ws.pDocument ), original ); IoTableMatches( table, original );
        // A cancelled generation must not prevent a fresh edit of the same row.
        table->item( 0, 3 )->setText( QStringLiteral( "fresh generation" ) ); QCoreApplication::processEvents();
        auto expected = original; expected[0].parameter = "fresh generation"; IoEqual( IoRead( ws.pDocument ), expected );
        CHECK( EditorHistory_AppliedStepCount( &ws.history ) == afterChange.applied + 1 );
    }
}

TEST_CASE( "A second Entity I/O page publication cancels the first page's queued table", "[map][gui][entity-io][entity-io-capture][multiple-pages]" )
{
    session_t session; auto &ws = session.workspace; IoSelectChild( ws );
    auto *dialog = IoShow( ws ); auto *first = IoTable( dialog ); QTabWidget tabs; MapEntityIo_AddTabs( &ws, &tabs );
    tabs.resize( 760, 360 ); tabs.show(); QCoreApplication::processEvents(); auto *second = IoTable( &tabs );
    const auto original = IoRead( ws.pDocument ); const usize steps = EditorHistory_AppliedStepCount( &ws.history );
    first->item( 0, 3 )->setText( QStringLiteral( "stale first page" ) );
    REQUIRE( IoAdd( &tabs )->isEnabled() ); IoAdd( &tabs )->click();
    REQUIRE( second->rowCount() == 2 ); const auto added = IoRead( ws.pDocument ); REQUIRE( added.size() == 2 );
    CHECK( added[0].parameter == original[0].parameter ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + 1 );
    const io_published_t published( ws ); QCoreApplication::processEvents(); published.Check( ws );
    IoEqual( IoRead( ws.pDocument ), added ); IoTableMatches( first, added ); IoTableMatches( second, added );
    second->item( 1, 3 )->setText( QStringLiteral( "new second page" ) ); QCoreApplication::processEvents();
    auto expected = added; expected[1].parameter = "new second page"; IoEqual( IoRead( ws.pDocument ), expected );
    IoTableMatches( first, expected ); IoTableMatches( second, expected ); CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps + 2 );
}

TEST_CASE( "Entity I/O enforces resolved owner locks and readiness even for programmatic table writes", "[map][gui][entity-io][entity-io-capture][locks]" )
{
    enum class guard_t { READ_ONLY, PREVIEW, TRANSACTION, LOCKED_OWNER, LOCK_ROUNDTRIP }; guard_t guard = guard_t::READ_ONLY;
    SECTION( "Read-only document" ) {}
    SECTION( "Active geometry preview" ) { guard = guard_t::PREVIEW; }
    SECTION( "Open history transaction" ) { guard = guard_t::TRANSACTION; }
    SECTION( "Selected child resolves to a locked owner" ) { guard = guard_t::LOCKED_OWNER; }
    SECTION( "Lock then unlock before the queued callback" ) { guard = guard_t::LOCK_ROUNDTRIP; }
    session_t session; auto &ws = session.workspace; IoSelectChild( ws ); auto *dialog = IoShow( ws ); auto *table = IoTable( dialog );
    const auto original = IoRead( ws.pDocument ); const auto *owner = MapWireframe_FindEntity( ws.wire, 110 ); REQUIRE( owner ); REQUIRE( owner->iLayer < ws.pDocument->layers.nCount );
    const QString layer = QString::fromUtf8( ws.pDocument->layers.pData[owner->iLayer].id );
    if ( guard == guard_t::LOCK_ROUNDTRIP ) {
        table->item( 0, 3 )->setText( QStringLiteral( "stale unlocked callback" ) );
        REQUIRE( MapLayers_SetLocked( &ws, layer, true ) ); REQUIRE( MapLayers_SetLocked( &ws, layer, false ) ); IoSelectChild( ws );
    } else {
        if ( guard == guard_t::READ_ONLY ) { ws.pDocument->bReadOnly = CY_TRUE; }
        else if ( guard == guard_t::PREVIEW ) { ws.editPreview.bActive = CY_TRUE; }
        else if ( guard == guard_t::TRANSACTION ) { REQUIRE( EditorHistory_Begin( &ws.history, Text( "Unrelated transaction" ) ) == editor_history_status_t::OK ); }
        else {
            REQUIRE( MapLayers_SetLocked( &ws, layer, true ) ); REQUIRE( MapLayers_IsObjectLocked( &ws, 110 ) );
            // Force inspection through the low-level selection API; admission
            // must check the resolved entity, not trust a disabled table.
            MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); ( void )EditorSelection_Clear( &ws.selection );
            const u64 child = 111; REQUIRE( EditorSelection_Set( &ws.selection, &child, 1 ) ); MapWorkspace_Notify( &ws, MAP_CHANGE_SELECTION );
            REQUIRE( MapEntityIo_Entity( &ws ) == 110 ); REQUIRE( table->rowCount() == 1 ); CHECK_FALSE( IoAdd( dialog )->isEnabled() );
        }
        table->item( 0, 3 )->setText( QStringLiteral( "write bypasses disabled controls" ) );
    }
    const io_published_t before( ws ); QCoreApplication::processEvents(); before.Check( ws );
    IoEqual( IoRead( ws.pDocument ), original ); IoTableMatches( table, original );
    ws.pDocument->bReadOnly = CY_FALSE; ws.editPreview.bActive = CY_FALSE;
    if ( guard == guard_t::TRANSACTION ) { EditorHistory_Cancel( &ws.history ); }
    if ( guard == guard_t::LOCKED_OWNER ) { REQUIRE( MapLayers_SetLocked( &ws, layer, false ) ); }
    IoSelectChild( ws ); table->item( 0, 3 )->setText( QStringLiteral( "retry after guard clears" ) ); QCoreApplication::processEvents();
    auto expected = original; expected[0].parameter = "retry after guard clears"; IoEqual( IoRead( ws.pDocument ), expected );
    CHECK( EditorHistory_AppliedStepCount( &ws.history ) == before.applied + 1 );
}

TEST_CASE( "Entity I/O Add allocation failures preserve Redo and restore the table before retry", "[map][gui][entity-io][entity-io-capture][allocation]" )
{
    allocation_failure_t audit{}; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit }; usize calls = 0;
    {
        session_t session( &allocator ); auto &ws = session.workspace; IoSelectChild( ws ); IoPrepareRedo( ws );
        auto *dialog = IoShow( ws ); audit.calls = 0; IoAdd( dialog )->click(); calls = audit.calls;
        REQUIRE( IoRead( ws.pDocument ).size() == 2 ); REQUIRE_FALSE( EditorHistory_CanRedo( &ws.history ) );
    }
    REQUIRE( audit.live == 0 ); REQUIRE( calls > 10 ); usize refused = 0;
    for ( const usize failOn : { usize{ 1 }, calls / 2, calls - 1 } ) {
        CAPTURE( failOn, calls ); audit.failOn = 0;
        {
            session_t session( &allocator ); auto &ws = session.workspace; IoSelectChild( ws ); IoPrepareRedo( ws );
            auto *dialog = IoShow( ws ); auto *table = IoTable( dialog ); const auto original = IoRead( ws.pDocument ); const io_published_t before( ws );
            audit.calls = 0; audit.failOn = failOn; IoAdd( dialog )->click(); audit.failOn = 0; CHECK( audit.calls >= failOn );
            if ( EditorHistory_AppliedStepCount( &ws.history ) == before.applied ) {
                ++refused; before.Check( ws ); IoEqual( IoRead( ws.pDocument ), original ); IoTableMatches( table, original ); REQUIRE( EditorHistory_CanRedo( &ws.history ) );
                map_notice_t kind{}; CHECK_FALSE( MapNotice_Last( &ws, &kind ).isEmpty() ); CHECK( kind == map_notice_t::ERROR );
                REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CheckString( ws.pDocument, 110, "io_redo_probe", "redo survives" );
                REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); IoAdd( dialog )->click();
            }
            auto added = IoRead( ws.pDocument ); REQUIRE( added.size() == 2 ); IoEqual( { added[0] }, original );
            CHECK( EditorHistory_AppliedStepCount( &ws.history ) == before.applied + 1 ); CHECK_FALSE( EditorHistory_CanRedo( &ws.history ) );
            // A late successful edit can still exhaust a presentation lookup.
            // Reopening refreshes from source even when selection is unchanged.
            REQUIRE( MapEntityIo_Show( &ws, nullptr ) == dialog ); IoTableMatches( table, added );
        }
        CHECK( audit.live == 0 );
    }
    CHECK( refused >= 2 );
}
