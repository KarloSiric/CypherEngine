//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code - Copyright (c) 2026 Karlo Siric.
// Map clipboard snapshots, placement, contextual guards and atomic history.
//////////////////////////////////////////////////////////////////////////
#include "CypherMapGui_Workspace.h"
#include "CypherMap_Clipboard.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentMeshes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QMimeData>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;
namespace math = cypher::math;
namespace geo = cypher::editor::geometry;

namespace
{
string_view_t Text( const char *p ) { return StringView_FromCString( p ); }
struct clipboard_scope_t {
    clipboard_scope_t() { REQUIRE( QGuiApplication::platformName() == QStringLiteral( "offscreen" ) ); QApplication::clipboard()->clear(); }
    ~clipboard_scope_t() { QApplication::clipboard()->clear(); }
};
struct session_t {
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
    explicit session_t( const allocator_t *allocator = Allocator_GetSystem() )
    {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, allocator ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        REQUIRE( MapWorkspace_RegisterCommands( &workspace, &gui.commands ) == command_registry_status_t::OK );
    }
    ~session_t() { MapWorkspace_Shutdown( &workspace ); gui::EditorGui_Shutdown( &gui ); }
};
struct settings_t {
    settings_registry_t *registry;
    settings_document_t store{};
    explicit settings_t( settings_registry_t *r ) : registry( r )
    {
        REQUIRE( SettingsDocument_Init( &store, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( registry, settings_scope_t::USER, &store );
    }
    ~settings_t() { EditorSettings_SetScope( registry, settings_scope_t::USER, nullptr ); }
    void Placement( const char *text )
    {
        const auto *descriptor = EditorSettings_Find( registry, Text( "editor.map.paste_offset" ) ); REQUIRE( descriptor );
        setting_value_t value{}; value.type = descriptor->type; value.text = Text( text );
        REQUIRE( EditorSettings_Write( registry, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    }
};
struct allocation_failure_t { usize calls{}, failOn{}, live{}; };
void *Allocate( void *context, usize bytes, usize alignment ) noexcept
{
    auto &state = *static_cast<allocation_failure_t *>( context );
    if ( ++state.calls == state.failOn ) { return nullptr; }
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment ); if ( memory ) { ++state.live; } return memory;
}
void Free( void *context, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory ) { --static_cast<allocation_failure_t *>( context )->live; }
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}
map_bounds_t Box()
{
    return { { { 3, 7, 11 }, { 67, 103, 139 } }, CY_TRUE };
}
u64 AddBox( map_workspace_t &ws )
{
    REQUIRE( MapWorkspace_CreateBox( &ws, Box() ) ); REQUIRE( ws.selection.ids.nCount == 1 ); return ws.selection.ids.pData[0];
}
std::vector<u64> Selection( const map_workspace_t &ws )
{
    std::vector<u64> ids; for ( usize i = 0; i < ws.selection.ids.nCount; ++i ) { ids.push_back( ws.selection.ids.pData[i] ); } return ids;
}
std::map<QString, QByteArray> MimeSnapshot()
{
    std::map<QString, QByteArray> result;
    if ( const auto *mime = QApplication::clipboard()->mimeData() ) { for ( const auto &format : mime->formats() ) { result[format] = mime->data( format ); } }
    return result;
}
QByteArray Bundle()
{
    const auto *mime = QApplication::clipboard()->mimeData(); REQUIRE( mime );
    const auto bytes = mime->data( QString::fromLatin1( MAP_CLIPBOARD_MIME ) ); REQUIRE_FALSE( bytes.isEmpty() ); return bytes;
}
void Seed( const QByteArray &bundle = {} )
{
    auto *mime = new QMimeData(); mime->setText( QStringLiteral( "Clipboard contents before edit" ) );
    mime->setData( QStringLiteral( "application/x-mason-test-sentinel" ), QByteArray( "keep\0this", 9 ) );
    if ( !bundle.isNull() ) { mime->setData( QString::fromLatin1( MAP_CLIPBOARD_MIME ), bundle ); }
    QApplication::clipboard()->setMimeData( mime );
}
void CheckPoint( math::vec3d_t point, math::vec3d_t expected )
{
    CHECK( point.x == Catch::Approx( expected.x ).margin( 1e-8 ) ); CHECK( point.y == Catch::Approx( expected.y ).margin( 1e-8 ) ); CHECK( point.z == Catch::Approx( expected.z ).margin( 1e-8 ) );
}
void CheckBounds( const map_wire_object_t *object, map_bounds_t original, math::vec3d_t offset )
{
    REQUIRE( object ); REQUIRE( object->bounds.bHas );
    CheckPoint( object->bounds.box.minimum, { original.box.minimum.x + offset.x, original.box.minimum.y + offset.y, original.box.minimum.z + offset.z } );
    CheckPoint( object->bounds.box.maximum, { original.box.maximum.x + offset.x, original.box.maximum.y + offset.y, original.box.maximum.z + offset.z } );
}
u64 MatchingObject( const map_workspace_t &ws, const std::vector<u64> &ids, const map_wire_object_t &original, math::vec3d_t offset = {} )
{
    u64 found = 0;
    const math::vec3d_t minimum{ original.bounds.box.minimum.x + offset.x, original.bounds.box.minimum.y + offset.y, original.bounds.box.minimum.z + offset.z };
    const math::vec3d_t maximum{ original.bounds.box.maximum.x + offset.x, original.bounds.box.maximum.y + offset.y, original.bounds.box.maximum.z + offset.z };
    const auto equal = []( math::vec3d_t a, math::vec3d_t b ) { return std::fabs( a.x - b.x ) < 1e-8 && std::fabs( a.y - b.y ) < 1e-8 && std::fabs( a.z - b.z ) < 1e-8; };
    for ( const auto id : ids ) {
        const auto *object = MapWireframe_FindObject( ws.wire, id ); REQUIRE( object );
        if ( object->kind == original.kind && equal( object->bounds.box.minimum, minimum ) && equal( object->bounds.box.maximum, maximum ) ) {
            REQUIRE( found == 0 ); found = id;
        }
    }
    REQUIRE( found != 0 ); return found;
}
void Facility( map_workspace_t &ws )
{
    const auto path = QDir( QString::fromUtf8( CYPHER_MAP_EXAMPLE_DIR ) ).filePath( QStringLiteral( "facility.cymap" ) );
    REQUIRE( MapWorkspace_Open( &ws, path ).status == map_files_status_t::OK );
}
const key_value_t *EntityProperty( map_document_t &document, u64 id, const char *name )
{
    return KeyValue_Find( KeyValue_Find( MapDocument_FindObject( &document, id, nullptr ), Text( "properties" ) ), Text( name ) );
}
void CheckTrigger( map_document_t &document, u64 id )
{
    const auto *record = MapDocument_FindObject( &document, id, nullptr ); REQUIRE( record );
    string_view_t value{}; REQUIRE( KeyValue_GetString( KeyValue_Find( record, Text( "class" ) ), &value ) ); CHECK( StringView_Equals( value, Text( "trigger_once" ) ) );
    REQUIRE( KeyValue_GetString( KeyValue_Find( record, Text( "name" ) ), &value ) ); CHECK( StringView_Equals( value, Text( "wave1_trigger" ) ) );
    bool_t disabled = CY_TRUE; REQUIRE( KeyValue_GetBool( EntityProperty( document, id, "start_disabled" ), &disabled ) ); CHECK_FALSE( disabled );
    REQUIRE( KeyValue_GetString( EntityProperty( document, id, "filter" ), &value ) ); CHECK( StringView_Equals( value, Text( "players" ) ) );
    const auto *outputs = KeyValue_Find( record, Text( "outputs" ) ); REQUIRE( KeyValue_ChildCount( outputs ) == 1 );
    const auto *output = KeyValue_ChildAt( outputs, 0 ); REQUIRE( KeyValue_GetString( KeyValue_Find( output, Text( "target" ) ), &value ) );
    CHECK( StringView_Equals( value, Text( "wave1" ) ) );
    REQUIRE( KeyValue_GetString( KeyValue_Find( output, Text( "input" ) ), &value ) ); CHECK( StringView_Equals( value, Text( "start" ) ) );
    f64 delay = 0; REQUIRE( KeyValue_GetF64( KeyValue_Find( output, Text( "delay" ) ), &delay ) ); CHECK( delay == Catch::Approx( 0.5 ) );
}
}

TEST_CASE( "Clipboard snapshot transfers mixed authored roots to another map with new identities and persistent undo", "[map][gui][clipboard][persistence]" )
{
    clipboard_scope_t clipboard; session_t destination; auto &dst = destination.workspace;
    settings_t settings( &destination.gui.settings ); settings.Placement( "none" );
    const u64 sourceIds[]{ 1000, 1200, 1300, 1100, 101, 110, 111 };
    std::vector<map_wire_object_t> originals; std::vector<u64> copiedIds; QByteArray snapshot;
    {
        session_t source; auto &src = source.workspace; Facility( src ); MapWorkspace_SetSelection( &src, sourceIds, 7 );
        REQUIRE( src.selection.ids.nCount == 7 ); const auto selected = Selection( src );
        const auto *document = src.pDocument; const auto *points = src.wire.points.pData; const auto token = UndoRedo_StateToken( src.history.pUndo );
        copiedIds = selected;
        for ( const auto id : selected ) { const auto *object = MapWireframe_FindObject( src.wire, id ); REQUIRE( object ); originals.push_back( *object ); }
        REQUIRE( MapWorkspace_CopySelection( &src ) ); snapshot = Bundle();
        CHECK( src.pDocument == document ); CHECK( src.wire.points.pData == points ); CHECK( Selection( src ) == selected );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( src.history.pUndo ) ) ); CHECK( EditorHistory_StepCount( &src.history ) == 0 );
        REQUIRE( MapWorkspace_New( &src ) == map_status_t::OK ); CHECK( Bundle() == snapshot );
    }
    const auto beforeNext = dst.pDocument->nextId; MapWorkspace_SetElementMode( &dst, map_element_mode_t::FACES ); MapWorkspace_SetTool( &dst, map_tool_t::TEXTURE );
    REQUIRE( MapWorkspace_CanPaste( &dst ) ); REQUIRE( MapWorkspace_Paste( &dst ) );
    const auto pasted = Selection( dst ); REQUIRE( pasted.size() == 7 ); CHECK( dst.elementMode == map_element_mode_t::OBJECTS ); CHECK( dst.tool == map_tool_t::SELECT );
    CHECK( EditorHistory_StepCount( &dst.history ) == 1 ); CHECK( Bundle() == snapshot );
    std::set<u64> unique; std::vector<u64> mappedIds;
    CHECK( std::is_sorted( pasted.begin(), pasted.end() ) );
    for ( const auto id : pasted ) { CHECK( MapWorkspace_IsSelected( &dst, id ) ); }
    for ( usize i = 0; i < pasted.size(); ++i ) {
        const auto id = MatchingObject( dst, pasted, originals[i] ); mappedIds.push_back( id );
        CHECK( id >= beforeNext ); CHECK( id != copiedIds[i] ); CHECK( unique.insert( id ).second );
        const auto *object = MapWireframe_FindObject( dst.wire, id ); REQUIRE( object ); CHECK( object->kind == originals[i].kind );
        CheckBounds( object, originals[i].bounds, {} ); CHECK( object->iLayer == 0 );
    }
    const auto ownerIndex = static_cast<usize>( std::find( copiedIds.begin(), copiedIds.end(), u64{ 110 } ) - copiedIds.begin() );
    const auto childIndex = static_cast<usize>( std::find( copiedIds.begin(), copiedIds.end(), u64{ 111 } ) - copiedIds.begin() );
    REQUIRE( ownerIndex < pasted.size() ); REQUIRE( childIndex < pasted.size() );
    const auto owner = mappedIds[ownerIndex], child = mappedIds[childIndex];
    CHECK( MapWireframe_FindObject( dst.wire, child )->owner == owner ); CheckTrigger( *dst.pDocument, owner );
    CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &dst.pDocument->geometry.sourceIds ) );
    REQUIRE( MapWorkspace_Undo( &dst ) == editor_history_status_t::OK ); CHECK( dst.wire.objects.nCount == 0 ); CHECK( dst.selection.ids.nCount == 0 ); CHECK( Bundle() == snapshot );
    REQUIRE( MapWorkspace_Redo( &dst ) == editor_history_status_t::OK ); CHECK( Selection( dst ) == pasted ); CheckTrigger( *dst.pDocument, owner );
    QTemporaryDir folder; REQUIRE( folder.isValid() ); const auto path = folder.filePath( QStringLiteral( "clipboard.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( &dst, path ).status == map_files_status_t::OK ); map_document_t loaded;
    REQUIRE( MapFiles_Load( &loaded, Allocator_GetSystem(), path.toUtf8().constData() ).status == map_files_status_t::OK );
    map_wireframe_t wire; REQUIRE( MapWireframe_Init( &wire, Allocator_GetSystem() ) ); REQUIRE( MapWireframe_Build( &wire, loaded ) == map_status_t::OK );
    for ( usize i = 0; i < mappedIds.size(); ++i ) { CheckBounds( MapWireframe_FindObject( wire, mappedIds[i] ), originals[i].bounds, {} ); }
    CHECK( MapWireframe_FindObject( wire, child )->owner == owner ); CheckTrigger( loaded, owner );
    CHECK( loaded.nextId == dst.pDocument->nextId ); CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &loaded.geometry.sourceIds ) );
}

TEST_CASE( "Paste placement uses authored grid plane cursor center and in place coordinates independently of displayed grid", "[map][gui][clipboard][placement]" )
{
    clipboard_scope_t clipboard;
    struct placement_t { const char *name; u32 axes; bool snap, inPlace; math::vec3d_t expected; };
    const placement_t cases[]{
        { "none", 5, true, false, {} }, { "grid", 0, true, false, { 64, 64, 0 } },
        { "grid", 5, true, false, { 64, 0, 64 } }, { "grid", 6, false, false, { 0, 64, 64 } },
        { "cursor", 3, true, false, { 157, -183, 0 } }, { "cursor", 5, true, false, { 157, 0, 181 } },
        { "cursor", 6, false, false, { 0, -185, 191 } }, { "cursor", 0, true, false, {} },
        { "grid", 7, true, true, {} }, { "cursor", 7, true, true, {} }
    };
    for ( const auto &entry : cases ) {
        CAPTURE( entry.name, entry.axes, entry.snap, entry.inPlace ); session_t s; auto &ws = s.workspace; settings_t settings( &s.gui.settings ); settings.Placement( entry.name );
        const auto original = AddBox( ws ); REQUIRE( MapWorkspace_CopySelection( &ws ) ); const auto bundle = Bundle();
        MapWorkspace_SetGridSize( &ws, 64 ); MapWorkspace_SetSnapToGrid( &ws, entry.snap ? CY_TRUE : CY_FALSE );
        MapWorkspace_SetCursor( &ws, { 193, -130, 266 }, entry.axes );
        MapWorkspace_SetGridVisible( &ws, CY_FALSE ); REQUIRE( MapWorkspace_Paste( &ws, entry.inPlace ) );
        REQUIRE( ws.selection.ids.nCount == 1 ); const auto pasted = ws.selection.ids.pData[0]; CHECK( pasted != original );
        CheckBounds( MapWireframe_FindObject( ws.wire, pasted ), Box(), entry.expected ); CheckBounds( MapWireframe_FindObject( ws.wire, original ), Box(), {} );
        CHECK( EditorHistory_StepCount( &ws.history ) == 2 ); CHECK( Bundle() == bundle );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( Selection( ws ) == std::vector<u64>{ original } );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( Selection( ws ) == std::vector<u64>{ pasted } );
        CheckBounds( MapWireframe_FindObject( ws.wire, pasted ), Box(), entry.expected );
    }
}

TEST_CASE( "Paste moves a selected owner and its selected child only once and retains entity key values verbatim", "[map][gui][clipboard][ownership]" )
{
    clipboard_scope_t clipboard; session_t source; auto &src = source.workspace; Facility( src );
    const auto *child = MapWireframe_FindObject( src.wire, 111 ); REQUIRE( child ); const auto originalObject = *child; const auto original = child->bounds;
    const u64 selected[]{ 111, 110 }; MapWorkspace_SetSelection( &src, selected, 2 ); REQUIRE( MapWorkspace_CopySelection( &src ) );
    session_t destination; auto &dst = destination.workspace; settings_t settings( &destination.gui.settings ); settings.Placement( "grid" );
    MapWorkspace_SetGridSize( &dst, 64 ); MapWorkspace_SetCursor( &dst, {}, 5 ); REQUIRE( MapWorkspace_Paste( &dst ) );
    REQUIRE( dst.selection.ids.nCount == 2 );
    const auto newChild = MatchingObject( dst, Selection( dst ), originalObject, { 64, 0, 64 } );
    const auto owner = MapWireframe_FindObject( dst.wire, newChild )->owner; REQUIRE( owner != 0 ); CHECK( MapWorkspace_IsSelected( &dst, owner ) );
    CheckBounds( MapWireframe_FindObject( dst.wire, newChild ), original, { 64, 0, 64 } );
    CHECK( MapWireframe_FindObject( dst.wire, newChild )->owner == owner );
    const auto *entity = MapWireframe_FindEntity( dst.wire, owner ); REQUIRE( entity ); CHECK( entity->nOwned == 1 ); CheckPoint( entity->origin, { 64, 0, 64 } );
    CheckTrigger( *dst.pDocument, owner ); CheckTrigger( *src.pDocument, 110 ); CheckBounds( MapWireframe_FindObject( src.wire, 111 ), original, {} );
}

TEST_CASE( "Paste normalizes new entity first allocation order before selection toggling copying and undo", "[map][gui][clipboard][selection-order]" )
{
    clipboard_scope_t clipboard; session_t s; auto &ws = s.workspace; const auto brush = AddBox( ws ); u64 entity = 0;
    REQUIRE( MapDocument_AddEntity( ws.pDocument, Text( "default" ), Text( "info_target" ), { 512, 256, 32 }, &entity ) == map_status_t::OK ); REQUIRE( brush < entity );
    REQUIRE( MapWireframe_Build( &ws.wire, *ws.pDocument ) == map_status_t::OK );
    const u64 original[]{ entity, brush }; MapWorkspace_SetSelection( &ws, original, 2 ); REQUIRE( MapWorkspace_CopySelection( &ws ) );
    const auto next = ws.pDocument->nextId; REQUIRE( MapWorkspace_Paste( &ws, true ) ); const auto pasted = Selection( ws ); REQUIRE( pasted.size() == 2 );
    CHECK( std::is_sorted( pasted.begin(), pasted.end() ) ); CHECK( MapWireframe_FindObject( ws.wire, pasted[0] )->kind == map_wire_kind_t::ENTITY );
    CHECK( MapWireframe_FindObject( ws.wire, pasted[1] )->kind == map_wire_kind_t::BRUSH );
    for ( const auto id : pasted ) { CHECK( id >= next ); CHECK( MapWorkspace_IsSelected( &ws, id ) ); }
    CheckBounds( MapWireframe_FindObject( ws.wire, pasted[1] ), Box(), {} );
    MapWorkspace_Select( &ws, pasted[1], MAP_SELECT_TOGGLE ); CHECK_FALSE( MapWorkspace_IsSelected( &ws, pasted[1] ) ); CHECK( Selection( ws ) == std::vector<u64>{ pasted[0] } );
    MapWorkspace_Select( &ws, pasted[1], MAP_SELECT_TOGGLE ); CHECK( Selection( ws ) == pasted ); REQUIRE( MapWorkspace_CopySelection( &ws ) );
    REQUIRE( MapWorkspace_CutSelection( &ws ) ); CHECK( ws.selection.ids.nCount == 0 ); CHECK( ws.wire.objects.nCount == 2 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( Selection( ws ) == pasted );
    for ( const auto id : pasted ) { CHECK( MapWorkspace_IsSelected( &ws, id ) ); }
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ws.wire.objects.nCount == 2 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( Selection( ws ) == pasted );
    for ( const auto id : pasted ) { CHECK( MapWorkspace_IsSelected( &ws, id ) ); }
}

TEST_CASE( "Originless logic records copy cut and paste without manufacturing positions or geometry", "[map][gui][clipboard][logic]" )
{
    clipboard_scope_t clipboard; session_t s; auto &ws = s.workspace; u64 id = 0;
    REQUIRE( MapDocument_AddEntity( ws.pDocument, Text( "default" ), Text( "logic_script" ), {}, &id ) == map_status_t::OK );
    map_chunk_t *chunk = nullptr; auto *record = MapDocument_FindObject( ws.pDocument, id, &chunk ); REQUIRE( record ); REQUIRE( chunk );
    auto *document = chunk->store.pDocument; auto *origin = KeyValue_Find( record, Text( "origin" ) ); REQUIRE( origin ); REQUIRE( KeyValue_Remove( document, record, origin ) );
    auto *properties = KeyValue_ObjectInsert( document, record, Text( "properties" ), key_value_type_t::OBJECT ); REQUIRE( properties );
    auto *script = KeyValue_ObjectInsert( document, properties, Text( "script" ), key_value_type_t::STRING ); REQUIRE( script );
    REQUIRE( KeyValue_SetString( document, script, Text( "scripts/waves/start_wave.cy" ) ) );
    REQUIRE( MapWireframe_Build( &ws.wire, *ws.pDocument ) == map_status_t::OK ); MapWorkspace_SetSelection( &ws, &id, 1 );
    CHECK( MapWireframe_FindObject( ws.wire, id ) == nullptr ); REQUIRE( MapWorkspace_CanCopySelection( &ws ) ); REQUIRE( MapWorkspace_CopySelection( &ws ) );
    const auto bundle = Bundle(); REQUIRE( MapWorkspace_CutSelection( &ws ) ); CHECK( MapDocument_FindObject( ws.pDocument, id, nullptr ) == nullptr ); CHECK( Bundle() == bundle );
    MapWorkspace_SetGridSize( &ws, 64 ); MapWorkspace_SetCursor( &ws, { 256, 512, 128 }, 3 ); REQUIRE( MapWorkspace_Paste( &ws ) );
    REQUIRE( ws.selection.ids.nCount == 1 ); const auto pasted = ws.selection.ids.pData[0]; CHECK( pasted != id );
    const auto *logic = MapWireframe_FindEntity( ws.wire, pasted ); REQUIRE( logic ); CHECK_FALSE( logic->bHasOrigin ); CHECK( MapWireframe_FindObject( ws.wire, pasted ) == nullptr );
    record = MapDocument_FindObject( ws.pDocument, pasted, nullptr ); REQUIRE( record ); CHECK( KeyValue_Find( record, Text( "origin" ) ) == nullptr );
    string_view_t path{}; REQUIRE( KeyValue_GetString( EntityProperty( *ws.pDocument, pasted, "script" ), &path ) ); CHECK( StringView_Equals( path, Text( "scripts/waves/start_wave.cy" ) ) );
    CHECK( ws.pDocument->geometryRecords.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == 2 ); CHECK( MapWorkspace_CanCopySelection( &ws ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( Selection( ws ) == std::vector<u64>{ id } );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( Selection( ws ) == std::vector<u64>{ pasted } );
    CHECK_FALSE( MapWireframe_FindEntity( ws.wire, pasted )->bHasOrigin ); CHECK( Bundle() == bundle );
}

TEST_CASE( "Clipboard guards permit readonly copying and component paste while blocking unsafe source edits and gestures", "[map][gui][clipboard][guards]" )
{
    clipboard_scope_t clipboard; session_t s; auto &ws = s.workspace; const auto id = AddBox( ws );
    CHECK( MapWorkspace_CanCopySelection( &ws ) ); CHECK( MapWorkspace_CanCutSelection( &ws ) ); REQUIRE( MapWorkspace_CopySelection( &ws ) ); const auto mime = MimeSnapshot();
    const auto *document = ws.pDocument; const auto token = UndoRedo_StateToken( ws.history.pUndo );
    ws.pDocument->bReadOnly = CY_TRUE; CHECK( MapWorkspace_CanCopySelection( &ws ) ); REQUIRE( MapWorkspace_CopySelection( &ws ) );
    CHECK_FALSE( MapWorkspace_CanCutSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CutSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanPaste( &ws ) ); CHECK_FALSE( MapWorkspace_Paste( &ws ) );
    ws.pDocument->bReadOnly = CY_FALSE;
    for ( const auto mode : { map_element_mode_t::FACES, map_element_mode_t::EDGES, map_element_mode_t::VERTICES, map_element_mode_t::NAVIGATION } ) {
        CAPTURE( static_cast<int>( mode ) ); ws.elementMode = mode;
        CHECK_FALSE( MapWorkspace_CanCopySelection( &ws ) ); CHECK_FALSE( MapWorkspace_CopySelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanCutSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CutSelection( &ws ) );
        CHECK( ws.pDocument == document ); CHECK( MimeSnapshot() == mime ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    }
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    const auto rejectPreview = [&]() {
        CHECK_FALSE( MapWorkspace_CanCopySelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanCutSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanPaste( &ws ) );
        CHECK_FALSE( MapWorkspace_CopySelection( &ws ) ); CHECK_FALSE( MapWorkspace_CutSelection( &ws ) ); CHECK_FALSE( MapWorkspace_Paste( &ws ) );
        CHECK( ws.pDocument == document ); CHECK( MimeSnapshot() == mime ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) );
    };
    REQUIRE( EditorHistory_Begin( &ws.history, Text( "In progress" ) ) == editor_history_status_t::OK ); rejectPreview(); EditorHistory_Cancel( &ws.history );
    map_transform_preview_t transform{}; transform.kind = map_transform_preview_kind_t::TRANSLATE; transform.delta = { 32, 0, 0 };
    MapWorkspace_SetTransformPreview( &ws, transform ); REQUIRE( ws.editPreview.bActive ); rejectPreview(); MapWorkspace_ClearEditPreview( &ws );
    MapWorkspace_SetTool( &ws, map_tool_t::BLOCK ); MapWorkspace_SetEditPreview( &ws, Box() ); REQUIRE( MapWorkspace_StageBlockPreview( &ws ) ); rejectPreview(); MapWorkspace_ClearEditPreview( &ws );
    REQUIRE( EditorSelection_Apply( &ws.hidden, id, EDITOR_SELECT_ADD ) ); CHECK_FALSE( MapWorkspace_CopySelection( &ws ) ); CHECK_FALSE( MapWorkspace_CutSelection( &ws ) ); (void)EditorSelection_Clear( &ws.hidden );
    for ( const auto mode : { map_element_mode_t::FACES, map_element_mode_t::EDGES, map_element_mode_t::VERTICES } ) {
        ws.elementMode = mode; REQUIRE( MapWorkspace_Paste( &ws, true ) ); CHECK( ws.elementMode == map_element_mode_t::OBJECTS ); CHECK( ws.tool == map_tool_t::SELECT );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
    }
}

TEST_CASE( "Native text malformed MIME and oversized clipboard input never publish map geometry or history", "[map][gui][clipboard][malformed]" )
{
    clipboard_scope_t clipboard; session_t s; auto &ws = s.workspace; AddBox( ws ); REQUIRE( MapWorkspace_CopySelection( &ws ) ); const auto valid = Bundle();
    const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData; const auto selection = Selection( ws );
    const auto token = UndoRedo_StateToken( ws.history.pUndo ); const auto next = ws.pDocument->nextId; const auto steps = EditorHistory_StepCount( &ws.history );
    auto checkUnchanged = [&]() {
        const auto mime = MimeSnapshot(); CHECK_FALSE( MapWorkspace_Paste( &ws ) ); CHECK_FALSE( MapWorkspace_Paste( &ws, true ) );
        CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == points ); CHECK( Selection( ws ) == selection ); CHECK( ws.pDocument->nextId == next );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) ); CHECK( MimeSnapshot() == mime );
    };
    QApplication::clipboard()->setText( QString::fromUtf8( valid ) ); CHECK_FALSE( MapWorkspace_CanPaste( &ws ) ); checkUnchanged();
    Seed( QByteArray( "", 0 ) ); CHECK_FALSE( MapWorkspace_CanPaste( &ws ) ); checkUnchanged();
    Seed( QByteArray( static_cast<qsizetype>( MAP_CLIPBOARD_TEXT_MAX + 1 ), 'x' ) ); CHECK_FALSE( MapWorkspace_CanPaste( &ws ) ); checkUnchanged();
    Seed( QByteArray( "not a map" ) ); CHECK( MapWorkspace_CanPaste( &ws ) ); checkUnchanged();
    auto unknownSchema = valid; unknownSchema.replace( "cypher.map.clipboard", "cypher.future.clipboard" ); Seed( unknownSchema ); checkUnchanged();
    Seed( valid ); REQUIRE( MapWorkspace_Paste( &ws, true ) );
}

TEST_CASE( "Cut publishes a single reversible delete and replaces clipboard only after success", "[map][gui][clipboard][cut]" )
{
    clipboard_scope_t clipboard; session_t s; auto &ws = s.workspace; const auto id = AddBox( ws ); Seed(); const auto previous = MimeSnapshot();
    const auto steps = EditorHistory_StepCount( &ws.history ); REQUIRE( MapWorkspace_CutSelection( &ws ) );
    CHECK( ws.wire.objects.nCount == 0 ); CHECK( ws.selection.ids.nCount == 0 ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 ); CHECK( MimeSnapshot() != previous );
    const auto cut = Bundle(); REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( Selection( ws ) == std::vector<u64>{ id } ); CheckBounds( MapWireframe_FindObject( ws.wire, id ), Box(), {} ); CHECK( Bundle() == cut );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ws.wire.objects.nCount == 0 ); CHECK( Bundle() == cut );
    REQUIRE( MapWorkspace_Paste( &ws, true ) ); REQUIRE( ws.selection.ids.nCount == 1 ); const auto pasted = ws.selection.ids.pData[0]; CHECK( pasted != id ); CheckBounds( MapWireframe_FindObject( ws.wire, pasted ), Box(), {} );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps + 2 );
}

TEST_CASE( "Clipboard allocation failures preserve map selection history source identities and every previous MIME byte", "[map][gui][clipboard][allocation]" )
{
    clipboard_scope_t clipboard;
    for ( int action = 0; action < 3; ++action ) {
        CAPTURE( action ); allocation_failure_t audit{}; const allocator_t allocator{ &Allocate, nullptr, &Free, &audit };
        auto prepare = [&]( session_t &s ) {
            AddBox( s.workspace );
            if ( action == 2 ) { REQUIRE( MapWorkspace_CopySelection( &s.workspace ) ); Seed( Bundle() ); }
            else { Seed(); }
        };
        const auto apply = [&]( map_workspace_t &ws ) { return action == 0 ? MapWorkspace_CopySelection( &ws ) : action == 1 ? MapWorkspace_CutSelection( &ws ) : MapWorkspace_Paste( &ws ); };
        usize successfulCalls = 0;
        {
            session_t probe( &allocator ); prepare( probe ); audit.calls = 0; REQUIRE( apply( probe.workspace ) ); successfulCalls = audit.calls; REQUIRE( successfulCalls > 2 );
        }
        REQUIRE( audit.live == 0 );
        for ( usize failure : { usize{ 1 }, usize{ 2 }, successfulCalls } ) {
            CAPTURE( failure, successfulCalls );
            {
                audit.calls = 0; audit.failOn = 0; session_t s( &allocator ); prepare( s ); auto &ws = s.workspace;
                const auto *document = ws.pDocument; const auto *points = ws.wire.points.pData; const auto selected = Selection( ws ); const auto *selection = ws.selection.ids.pData;
                const auto next = document->nextId, revision = document->geometry.revision, selectionRevision = ws.selection.revision;
                const auto steps = EditorHistory_StepCount( &ws.history ), baselineLive = audit.live; const auto token = UndoRedo_StateToken( ws.history.pUndo ); const auto mime = MimeSnapshot();
                audit.calls = 0; audit.failOn = failure; CHECK_FALSE( apply( ws ) ); audit.failOn = 0;
                CHECK( audit.calls >= failure ); CHECK( audit.live == baselineLive ); CHECK( ws.pDocument == document ); CHECK( ws.wire.points.pData == points );
                CHECK( ws.selection.ids.pData == selection ); CHECK( ws.selection.revision == selectionRevision ); CHECK( Selection( ws ) == selected );
                CHECK( ws.pDocument->nextId == next ); CHECK( ws.pDocument->geometry.revision == revision ); CHECK( geo::GeometrySourceIdRegistry_ValidateDeep( &ws.pDocument->geometry.sourceIds ) );
                CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) ); CHECK( MimeSnapshot() == mime );
                audit.calls = 0; REQUIRE( apply( ws ) ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + ( action == 0 ? 0 : 1 ) );
                if ( action != 0 ) { REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( Selection( ws ) == selected ); CheckBounds( MapWireframe_FindObject( ws.wire, selected[0] ), Box(), {} ); }
            }
            CHECK( audit.live == 0 );
        }
    }
}
