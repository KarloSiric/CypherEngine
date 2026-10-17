//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Layers_Tests.cpp
//  Purpose: Tests TrenchBroom-style layer controls: the current layer new
//           objects go to, hidden and locked layers, isolating a layer,
//           selecting a layer's objects, and the Outliner's layer rows.
//
//  History:
//  - Created by Karlo Siric on 2026-10-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Layers.h"
#include "CypherMapGui_ObjectTools.h"
#include "CypherMapGui_Panels.h"
#include "CypherMap_ObjectCreate.h"
#include "CypherMapGui_Views.h"
#include "CypherMapGui_Notice.h"
#include "CypherMap_Groups.h"
#include "CypherMap_SelectionSets.h"
#include "CypherMap_Hierarchy.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QMouseEvent>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <memory>
#include <algorithm>
#include <string>
#include <vector>
#include <utility>

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
    u64 Box( math::vec3d_t lo, math::vec3d_t hi )
    {
        map_bounds_t bounds{};
        MapBounds_AddPoint( bounds, lo );
        MapBounds_AddPoint( bounds, hi );
        REQUIRE( MapWorkspace_CreateBox( &ws, bounds ) );
        return EditorSelection_At( &ws.selection, 0 );
    }
    command_result_t Run( const char *line ) { return EditorCommands_ExecuteLine( &gui.commands, StringView_FromCString( line ) ); }
    std::string LayerOf( u64 id )
    {
        const map_wire_object_t *object = MapWireframe_FindObject( ws.wire, id );
        if ( object == nullptr ) {
            const map_wire_entity_t *entity = MapWireframe_FindEntity( ws.wire, id );
            REQUIRE( entity != nullptr );
            return ws.pDocument->layers.pData[entity->iLayer].id;
        }
        return ws.pDocument->layers.pData[object->iLayer].id;
    }
    const map_wire_object_t &Object( u64 id )
    {
        const map_wire_object_t *object = MapWireframe_FindObject( ws.wire, id );
        REQUIRE( object != nullptr );
        return *object;
    }
};
} // namespace

TEST_CASE( "New objects go to the current layer, and undo keeps the choice", "[map][gui][layers]" )
{
    session_t s;
    const std::string first = s.ws.pDocument->layers.pData[0].id;
    const u64 seed = s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
    REQUIRE( s.Run( "map.layer.create" ) == command_result_t::OK ); // The selection moves into the new layer.
    const std::string second = s.ws.pDocument->layers.pData[1].id;
    CHECK( s.LayerOf( seed ) == second );
    CHECK( MapLayers_Current( &s.ws ).toStdString() == first );       // Not chosen yet: the first layer.
    REQUIRE( s.Run( ( "map.layer.set_current " + second ).c_str() ) == command_result_t::OK );
    CHECK( MapLayers_Current( &s.ws ).toStdString() == second );
    // A box, an entity and a paste land there.
    const u64 box = s.Box( { 128, 0, 0 }, { 192, 64, 64 } );
    CHECK( s.LayerOf( box ) == second );
    struct job_t { u64 id; } job{ 0 };
    REQUIRE( MapWorkspace_ApplyMetadataEdit( &s.ws, "Entity", []( void *context, map_document_t *copy, vector_t<u64> *out, bool *changed ) noexcept {
        auto &j = *static_cast<job_t *>( context );
        const auto status = MapCreate_Entity( copy, StringView_FromCString( "light" ), { 0, 0, 128 }, {}, &j.id );
        *changed = status == map_status_t::OK;
        return status == map_status_t::OK && !Vector_PushBack( out, j.id ) ? map_status_t::OUT_OF_MEMORY : status;
    }, &job ) );
    CHECK( s.LayerOf( job.id ) == second );
    REQUIRE( s.Run( ( "map.layer.set_current " + first ).c_str() ) == command_result_t::OK );
    MapWorkspace_SetSelection( &s.ws, &box, 1 );
    REQUIRE( MapWorkspace_CopySelection( &s.ws ) );
    REQUIRE( MapWorkspace_Paste( &s.ws ) );
    CHECK( s.LayerOf( EditorSelection_At( &s.ws.selection, 0 ) ) == first ); // Pasted into the current layer, not the source's.
    // Undoing the paste does not undo the layer choice.
    REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK );
    CHECK( MapLayers_Current( &s.ws ).toStdString() == first );
    CHECK( s.Run( "map.layer.set_current no_such_layer" ) == command_result_t::INVALID_ARGUMENTS );
}

TEST_CASE( "A hidden layer is not drawn or picked; a locked one is drawn but not picked", "[map][gui][layers]" )
{
    session_t s;
    const std::string first = s.ws.pDocument->layers.pData[0].id;
    const u64 floor = s.Box( { 0, 0, 0 }, { 256, 256, 16 } );
    const u64 crate = s.Box( { 64, 64, 16 }, { 128, 128, 80 } );
    MapWorkspace_SetSelection( &s.ws, &crate, 1 );
    REQUIRE( s.Run( "map.layer.create" ) == command_result_t::OK );
    const std::string props = s.ws.pDocument->layers.pData[1].id;
    MapWorkspace_SetSelection( &s.ws, &crate, 1 );

    // Hidden: gone from the views and the selection, and from under the cursor.
    REQUIRE( s.Run( ( "map.layer.toggle_hidden " + props ).c_str() ) == command_result_t::OK );
    CHECK_FALSE( MapWorkspace_IsVisible( &s.ws, s.Object( crate ) ) );
    CHECK( MapWorkspace_IsVisible( &s.ws, s.Object( floor ) ) );
    CHECK( EditorSelection_Count( &s.ws.selection ) == 0u );
    CHECK( MapObjectTools_ObjectsAlongRay( &s.ws, { 96, 96, 500 }, { 0, 0, -1 } ) == std::vector<u64>{ floor } );
    REQUIRE( s.Run( ( "map.layer.toggle_hidden " + props ).c_str() ) == command_result_t::OK );
    CHECK( MapWorkspace_IsVisible( &s.ws, s.Object( crate ) ) );

    // Locked: drawn, not selectable, and Select Objects in Layer finds nothing.
    REQUIRE( s.Run( ( "map.layer.toggle_locked " + props ).c_str() ) == command_result_t::OK );
    CHECK( MapWorkspace_IsVisible( &s.ws, s.Object( crate ) ) );
    CHECK_FALSE( MapWorkspace_IsSelectableObject( &s.ws, s.Object( crate ) ) );
    REQUIRE( s.Run( ( "map.layer.select_objects " + props ).c_str() ) == command_result_t::OK );
    CHECK( EditorSelection_Count( &s.ws.selection ) == 0u );
    REQUIRE( s.Run( ( "map.layer.select_objects " + first ).c_str() ) == command_result_t::OK );
    CHECK( EditorSelection_Count( &s.ws.selection ) == 1u );

    // Isolate shows one layer; Show All brings everything back, unlocked.
    REQUIRE( s.Run( ( "map.layer.isolate " + props ).c_str() ) == command_result_t::OK );
    CHECK_FALSE( MapWorkspace_IsVisible( &s.ws, s.Object( floor ) ) );
    CHECK( MapWorkspace_IsVisible( &s.ws, s.Object( crate ) ) );
    REQUIRE( s.Run( "map.layer.show_all" ) == command_result_t::OK );
    CHECK( MapWorkspace_IsVisible( &s.ws, s.Object( floor ) ) );
    CHECK( MapWorkspace_IsSelectableObject( &s.ws, s.Object( crate ) ) );
}

TEST_CASE( "The Outliner marks the current, hidden and locked layers and double-click makes one current", "[map][gui][layers]" )
{
    session_t s;
    const u64 box = s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
    MapWorkspace_SetSelection( &s.ws, &box, 1 );
    REQUIRE( s.Run( "map.layer.create" ) == command_result_t::OK );
    const QString second = QString::fromUtf8( s.ws.pDocument->layers.pData[1].id );
    std::unique_ptr<QWidget> outliner( MapOutliner_Create( nullptr, &s.ws ) );
    outliner->show();
    QCoreApplication::processEvents();
    auto *tree = outliner->findChild<QTreeWidget *>( QStringLiteral( "MapOutlinerTree" ) );
    REQUIRE( tree != nullptr );
    const auto row = [&]( const QString &layer ) -> QTreeWidgetItem * {
        for ( QTreeWidgetItemIterator it( tree ); *it != nullptr; ++it ) {
            if ( ( *it )->data( 0, Qt::UserRole + 2 ).toString() == QStringLiteral( "layer:" ) + layer ) { return *it; }
        }
        return nullptr;
    };
    const QString first = QString::fromUtf8( s.ws.pDocument->layers.pData[0].id );
    REQUIRE( row( first ) != nullptr );
    CHECK( row( first )->text( 0 ).contains( QStringLiteral( "current" ) ) );
    CHECK( row( first )->font( 0 ).bold() );
    Q_EMIT tree->itemDoubleClicked( row( second ), 0 );
    QCoreApplication::processEvents();
    CHECK( MapLayers_Current( &s.ws ) == second );
    CHECK( row( second )->text( 0 ).contains( QStringLiteral( "current" ) ) );
    REQUIRE( MapLayers_SetHidden( &s.ws, first, true ) );
    REQUIRE( MapLayers_SetLocked( &s.ws, second, true ) );
    QCoreApplication::processEvents();
    CHECK( row( first )->text( 0 ).contains( QStringLiteral( "hidden" ) ) );
    CHECK( row( first )->font( 0 ).italic() );
    CHECK( row( second )->text( 0 ).contains( QStringLiteral( "locked" ) ) );
}


namespace
{
void LayerMouse( QWidget *view, QEvent::Type type, QPointF position )
{
    QMouseEvent event( type, position, view->mapToGlobal( position ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( view, &event );
}
std::unique_ptr<QWidget> LayerTop( map_workspace_t &ws )
{
    std::unique_ptr<QWidget> view( MapOrthoView_Create( nullptr, &ws, map_ortho_axes_t::TOP ) );
    view->resize( 800, 600 ); view->show(); QCoreApplication::processEvents();
    MapWorkspace_Frame( &ws, CY_FALSE ); QCoreApplication::processEvents(); return view;
}
QTreeWidgetItem *LayerObjectRow( QTreeWidget *tree, u64 id )
{
    for ( QTreeWidgetItemIterator it( tree ); *it != nullptr; ++it ) {
        if ( ( *it )->data( 0, Qt::UserRole ).toULongLong() == id ) { return *it; }
    }
    return nullptr;
}
struct layer_clipboard_guard_t {
    std::unique_ptr<QMimeData> original{ std::make_unique<QMimeData>() };
    layer_clipboard_guard_t()
    {
        if ( const auto *mime = QApplication::clipboard()->mimeData() ) {
            for ( const auto &format : mime->formats() ) { original->setData( format, mime->data( format ) ); }
        }
    }
    ~layer_clipboard_guard_t() { QApplication::clipboard()->setMimeData( original.release() ); }
};
}

TEST_CASE( "Every indirect selection source refuses a mixed locked group without losing the old selection", "[map][gui][layers][layer-lock-policy][selection]" )
{
    for ( const int route : { 0, 1, 2, 3, 4 } ) {
        DYNAMIC_SECTION( "route=" << route ) {
            session_t s; const u64 a = s.Box( { 0, 0, 0 }, { 64, 64, 64 } ), b = s.Box( { 128, 0, 0 }, { 192, 64, 64 } );
            const u64 sentinel = s.Box( { 320, 0, 0 }, { 384, 64, 64 } );
            REQUIRE( MapWorkspace_ConvertBrushSelection( &s.ws ) );
            const u64 pair[]{ a, b }; MapWorkspace_SetSelection( &s.ws, pair, 2 ); REQUIRE( s.Run( "map.group.create" ) == command_result_t::OK );
            const u64 group = MapDocument_GroupOf( s.ws.pDocument, a ); REQUIRE( group != 0 );
            REQUIRE( s.Run( "map.selection_set.create ProtectedAssembly" ) == command_result_t::OK );
            const auto sets = MapSelectionSets_All( s.ws.pDocument ); REQUIRE( sets.size() == 1 );
            MapWorkspace_Select( &s.ws, b, MAP_SELECT_REPLACE ); REQUIRE( s.Run( "map.layer.create" ) == command_result_t::OK );
            const QString lockedLayer = QString::fromStdString( s.LayerOf( b ) ); REQUIRE( MapLayers_SetLocked( &s.ws, lockedLayer, true ) );
            const u64 oldRoot = route == 2 ? a : sentinel;
            MapWorkspace_Select( &s.ws, oldRoot, MAP_SELECT_REPLACE );
            MapWorkspace_SetTool( &s.ws, map_tool_t::SELECT );
            std::unique_ptr<QWidget> view;
            QTreeWidget *tree{};
            if ( route == 0 ) {
                u64 face{}; for ( usize i = 0; i < s.ws.wire.faces.nCount; ++i ) {
                    const auto &candidate = s.ws.wire.faces.pData[i]; if ( candidate.id == sentinel && candidate.normal.z > 0.99 ) { face = candidate.faceId; }
                }
                REQUIRE( face != 0 ); MapWorkspace_SelectMeshFace( &s.ws, sentinel, face );
            } else if ( route == 1 ) { MapWorkspace_SetElementMode( &s.ws, map_element_mode_t::GROUPS ); view = LayerTop( s.ws ); }
            else if ( route == 4 ) {
                view.reset( MapOutliner_Create( nullptr, &s.ws ) ); view->resize( 600, 700 ); view->show(); QCoreApplication::processEvents();
                tree = view->findChild<QTreeWidget *>( QStringLiteral( "MapOutlinerTree" ) ); REQUIRE( tree ); tree->expandAll();
            }
            const auto *document = s.ws.pDocument;
            const auto revision = s.ws.selection.revision, geometryRevision = document->geometry.revision;
            const auto *roots = s.ws.selection.ids.pData, *faceSet = s.ws.meshFaceSet.pData;
            const auto face = s.ws.selectedMeshFaceId, faceObject = s.ws.selectedMeshFaceObject;
            const usize faceCount = s.ws.meshFaceSet.nCount, steps = EditorHistory_StepCount( &s.ws.history );
            const auto token = UndoRedo_StateToken( s.ws.history.pUndo );
            if ( route == 0 ) {
                MapWorkspace_SetSelection( &s.ws, pair, 2 );
                CHECK( s.ws.selectedMeshFaceObject == sentinel );
                u64 lockedSide{}; for ( usize i = 0; i < s.ws.wire.faces.nCount; ++i ) {
                    const auto &candidate = s.ws.wire.faces.pData[i]; if ( candidate.id == b && candidate.normal.z > 0.99 ) { lockedSide = candidate.sideId; }
                }
                REQUIRE( lockedSide != 0 ); MapWorkspace_SelectBrushFace( &s.ws, b, lockedSide );
                CHECK_FALSE( MapWorkspace_HasBrushFace( &s.ws ) );
            }
            else if ( route == 1 ) {
                const QPointF at = MapOrthoView_WorldToView( view.get(), { 0, 32 } );
                REQUIRE( MapOrthoView_Pick( view.get(), at ) == a ); LayerMouse( view.get(), QEvent::MouseButtonPress, at ); LayerMouse( view.get(), QEvent::MouseButtonRelease, at );
            } else if ( route == 2 ) { ( void )s.Run( "map.group.select" ); }
            else if ( route == 3 ) { ( void )s.Run( ( "map.selection_set.select " + std::to_string( sets[0].id ) ).c_str() ); }
            else {
                auto *row = LayerObjectRow( tree, group ); REQUIRE( row ); tree->scrollToItem( row ); QCoreApplication::processEvents();
                const QPointF at = tree->visualItemRect( row ).center(); LayerMouse( tree->viewport(), QEvent::MouseButtonPress, at ); LayerMouse( tree->viewport(), QEvent::MouseButtonRelease, at );
                CHECK_FALSE( row->isSelected() ); REQUIRE( LayerObjectRow( tree, oldRoot ) ); CHECK( LayerObjectRow( tree, oldRoot )->isSelected() );
            }
            CHECK( s.ws.pDocument == document ); CHECK( document->geometry.revision == geometryRevision );
            CHECK( s.ws.selection.ids.pData == roots ); REQUIRE( s.ws.selection.ids.nCount == 1 ); CHECK( s.ws.selection.ids.pData[0] == oldRoot );
            CHECK( s.ws.selection.revision == revision ); CHECK( s.ws.selectedMeshFaceId == face ); CHECK( s.ws.selectedMeshFaceObject == faceObject );
            CHECK( s.ws.meshFaceSet.pData == faceSet ); CHECK( s.ws.meshFaceSet.nCount == faceCount );
            CHECK( EditorHistory_StepCount( &s.ws.history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( s.ws.history.pUndo ) ) );
            CHECK( MapNotice_Last( &s.ws ).contains( QStringLiteral( "lock" ), Qt::CaseInsensitive ) );
            REQUIRE( MapLayers_SetLocked( &s.ws, lockedLayer, false ) ); MapWorkspace_SetElementMode( &s.ws, map_element_mode_t::OBJECTS );
            MapWorkspace_SetSelection( &s.ws, pair, 2 ); REQUIRE( s.ws.selection.ids.nCount == 2 );
            REQUIRE( MapWorkspace_TranslateSelection( &s.ws, { 32, 0, 0 } ) ); CHECK( s.Object( a ).bounds.box.minimum.x == Catch::Approx( 32 ) ); CHECK( s.Object( b ).bounds.box.minimum.x == Catch::Approx( 160 ) );
            REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK ); CHECK( s.Object( a ).bounds.box.minimum.x == Catch::Approx( 0 ) ); CHECK( s.Object( b ).bounds.box.minimum.x == Catch::Approx( 128 ) );
            REQUIRE( MapWorkspace_Redo( &s.ws ) == editor_history_status_t::OK ); CHECK( s.Object( b ).bounds.box.minimum.x == Catch::Approx( 160 ) );
        }
    }
}


namespace
{
struct layer_hierarchy_fixture_t {
    session_t s;
    u64 parent{}, child{}, grandchild{}, parentBrush{}, grandchildBrush{};
    QString protectedLayer{ QStringLiteral( "protected" ) };
    layer_hierarchy_fixture_t()
    {
        auto &ws = s.ws;
        parentBrush = s.Box( { -64, -64, 0 }, { 64, 64, 128 } );
        grandchildBrush = s.Box( { 288, -32, 0 }, { 352, 32, 64 } );
        REQUIRE( MapDocument_AddLayer( ws.pDocument, StringView_FromCString( "protected" ), StringView_FromCString( "Protected" ) ) == map_status_t::OK );
        const auto normal = StringView_FromCString( ws.pDocument->layers.pData[0].id );
        REQUIRE( MapDocument_AddEntity( ws.pDocument, normal, StringView_FromCString( "info_target" ), { 0, 0, 0 }, &parent ) == map_status_t::OK );
        REQUIRE( MapDocument_AddEntity( ws.pDocument, normal, StringView_FromCString( "info_target" ), { 160, 0, 0 }, &child ) == map_status_t::OK );
        REQUIRE( MapDocument_AddEntity( ws.pDocument, StringView_FromCString( "protected" ), StringView_FromCString( "info_target" ), { 320, 0, 0 }, &grandchild ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, parentBrush, parent ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( ws.pDocument, grandchildBrush, grandchild ) == map_status_t::OK );
        REQUIRE( MapHierarchy_SetParent( ws.pDocument, { &child, 1 }, parent ) == map_status_t::OK );
        REQUIRE( MapHierarchy_SetParent( ws.pDocument, { &grandchild, 1 }, child ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &ws ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
        MapWorkspace_SetTool( &ws, map_tool_t::TRANSLATE ); MapWorkspace_Select( &ws, parent, MAP_SELECT_REPLACE );
        REQUIRE( s.Object( parentBrush ).owner == parent ); REQUIRE( s.Object( grandchildBrush ).owner == grandchild );
    }
    void CheckAt( f64 offset )
    {
        CHECK( s.Object( parentBrush ).bounds.box.minimum.x == Catch::Approx( -64 + offset ) );
        CHECK( s.Object( grandchildBrush ).bounds.box.minimum.x == Catch::Approx( 288 + offset ) );
        for ( const auto &[id, x] : { std::pair{ parent, 0.0 }, std::pair{ child, 160.0 }, std::pair{ grandchild, 320.0 } } ) {
            const auto *entity = MapWireframe_FindEntity( s.ws.wire, id ); REQUIRE( entity ); CHECK( entity->origin.x == Catch::Approx( x + offset ) );
        }
        CHECK( MapHierarchy_Parent( s.ws.pDocument, child ) == parent ); CHECK( MapHierarchy_Parent( s.ws.pDocument, grandchild ) == child );
        CHECK( s.Object( parentBrush ).owner == parent ); CHECK( s.Object( grandchildBrush ).owner == grandchild );
    }
};
}

TEST_CASE( "Undo and Redo restore geometry without reacquiring a root batch containing a locked object", "[map][gui][layers][layer-lock-policy][history]" )
{
    session_t s; const u64 a = s.Box( { 0, 0, 0 }, { 64, 64, 64 } ), b = s.Box( { 128, 0, 0 }, { 192, 64, 64 } );
    REQUIRE( s.Run( "map.layer.create" ) == command_result_t::OK ); const QString layer = QString::fromStdString( s.LayerOf( b ) );
    const u64 pair[]{ a, b }; MapWorkspace_SetSelection( &s.ws, pair, 2 );
    const usize steps = EditorHistory_AppliedStepCount( &s.ws.history );
    REQUIRE( MapWorkspace_TranslateSelection( &s.ws, { 32, 0, 0 } ) ); REQUIRE( MapLayers_SetLocked( &s.ws, layer, true ) );
    REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK );
    CHECK( s.Object( a ).bounds.box.minimum.x == Catch::Approx( 0 ) ); CHECK( s.Object( b ).bounds.box.minimum.x == Catch::Approx( 128 ) );
    CHECK( s.ws.selection.ids.nCount == 0 ); CHECK_FALSE( MapWorkspace_CanMoveSelection( &s.ws ) );
    CHECK( MapLayers_IsLockedId( &s.ws, layer ) ); CHECK( EditorHistory_AppliedStepCount( &s.ws.history ) == steps );
    REQUIRE( MapWorkspace_Redo( &s.ws ) == editor_history_status_t::OK );
    CHECK( s.Object( a ).bounds.box.minimum.x == Catch::Approx( 32 ) ); CHECK( s.Object( b ).bounds.box.minimum.x == Catch::Approx( 160 ) );
    CHECK( s.ws.selection.ids.nCount == 0 ); CHECK_FALSE( MapWorkspace_DeleteSelection( &s.ws ) );
    CHECK( EditorHistory_AppliedStepCount( &s.ws.history ) == steps + 1 );
    REQUIRE( MapLayers_SetLocked( &s.ws, layer, false ) ); REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK );
    REQUIRE( s.ws.selection.ids.nCount == 2 ); CHECK( MapWorkspace_IsSelected( &s.ws, a ) ); CHECK( MapWorkspace_IsSelected( &s.ws, b ) );
    CHECK( MapWorkspace_CanMoveSelection( &s.ws ) );
}

TEST_CASE( "Root edits refuse an entire hierarchy containing a locked descendant while Copy remains read only", "[map][gui][layers][layer-lock-policy][hierarchy]" )
{
    layer_clipboard_guard_t clipboard;
    for ( const int action : { 0, 1, 2, 3, 4, 5 } ) {
        DYNAMIC_SECTION( "action=" << action ) {
            layer_hierarchy_fixture_t f; auto &ws = f.s.ws;
            REQUIRE( MapLayers_SetLocked( &ws, f.protectedLayer, true ) );
            REQUIRE( ws.selection.ids.nCount == 1 ); REQUIRE( ws.selection.ids.pData[0] == f.parent ); // Inspection remains possible.
            CHECK_FALSE( MapWorkspace_CanMoveSelection( &ws ) ); CHECK_FALSE( MapWorkspace_CanTurnSelection( &ws ) );
            REQUIRE( MapWorkspace_CanCopySelection( &ws ) ); REQUIRE( MapWorkspace_CopySelection( &ws ) );
            auto *mime = new QMimeData(); mime->setText( QStringLiteral( "Do not replace on refused Cut" ) );
            mime->setData( QStringLiteral( "application/x-mason-lock-test" ), QByteArray( "keep\0bytes", 10 ) ); QApplication::clipboard()->setMimeData( mime );
            const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
            const auto *roots = ws.selection.ids.pData; const usize steps = EditorHistory_StepCount( &ws.history ); const auto token = UndoRedo_StateToken( ws.history.pUndo );
            bool changed{};
            if ( action == 0 ) { changed = MapWorkspace_TranslateSelection( &ws, { 64, 0, 0 } ); }
            else if ( action == 1 ) { changed = MapWorkspace_RotateSelection( &ws, { 0, 0, 90 }, {} ); }
            else if ( action == 2 ) { changed = MapWorkspace_ScaleSelection( &ws, { 2, 2, 2 }, {} ); }
            else if ( action == 3 ) { changed = MapWorkspace_DeleteSelection( &ws ); }
            else if ( action == 4 ) { changed = MapWorkspace_DuplicateSelection( &ws, { 64, 0, 0 } ); }
            else { changed = MapWorkspace_CutSelection( &ws ); }
            REQUIRE_FALSE( changed );
            CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
            CHECK( ws.selection.ids.pData == roots ); CHECK( ws.selection.revision == selectionRevision ); REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == f.parent );
            CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) ); f.CheckAt( 0 );
            CHECK( QApplication::clipboard()->text() == QStringLiteral( "Do not replace on refused Cut" ) );
            CHECK( QApplication::clipboard()->mimeData()->data( QStringLiteral( "application/x-mason-lock-test" ) ) == QByteArray( "keep\0bytes", 10 ) );
            REQUIRE( MapLayers_SetLocked( &ws, f.protectedLayer, false ) ); REQUIRE( MapWorkspace_TranslateSelection( &ws, { 64, 0, 0 } ) ); f.CheckAt( 64 );
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); f.CheckAt( 0 );
            REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); f.CheckAt( 64 );
        }
    }
}

TEST_CASE( "Locking then unlocking a descendant cancels a captured parent drag before late input", "[map][gui][layers][layer-lock-policy][gesture]" )
{
    layer_hierarchy_fixture_t f; auto &ws = f.s.ws; auto view = LayerTop( ws );
    const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
    const usize steps = EditorHistory_AppliedStepCount( &ws.history ); const auto token = UndoRedo_StateToken( ws.history.pUndo );
    const f64 grab = 40.0 / MapOrthoView_Zoom( view.get() );
    const QPointF start = MapOrthoView_WorldToView( view.get(), { grab, 0 } ), end = MapOrthoView_WorldToView( view.get(), { grab + 64, 0 } );
    LayerMouse( view.get(), QEvent::MouseButtonPress, start ); LayerMouse( view.get(), QEvent::MouseMove, end );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
    REQUIRE( MapLayers_SetLocked( &ws, f.protectedLayer, true ) );
    CHECK_FALSE( ws.editPreview.bActive ); REQUIRE( MapLayers_SetLocked( &ws, f.protectedLayer, false ) );
    LayerMouse( view.get(), QEvent::MouseMove, end + QPointF( 16, 0 ) ); LayerMouse( view.get(), QEvent::MouseButtonRelease, end + QPointF( 16, 0 ) );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
    CHECK( ws.selection.revision == selectionRevision + 2 ); REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == f.parent );
    REQUIRE( EditorHistory_AppliedStepCount( &ws.history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) ); f.CheckAt( 0 );
    LayerMouse( view.get(), QEvent::MouseButtonPress, start ); LayerMouse( view.get(), QEvent::MouseMove, end ); LayerMouse( view.get(), QEvent::MouseButtonRelease, end );
    REQUIRE( EditorHistory_AppliedStepCount( &ws.history ) == steps + 1 ); f.CheckAt( 64 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); f.CheckAt( 0 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); f.CheckAt( 64 );
}

TEST_CASE( "A locked brush cannot be converted through the component-pick adapter", "[map][gui][layers][layer-lock-policy][components]" )
{
    session_t s;
    const u64 retained = s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
    const u64 locked = s.Box( { 128, 0, 0 }, { 192, 64, 64 } );
    REQUIRE( s.Run( "map.layer.create" ) == command_result_t::OK );
    const QString layer = QString::fromStdString( s.LayerOf( locked ) );
    REQUIRE( MapLayers_SetLocked( &s.ws, layer, true ) );
    MapWorkspace_Select( &s.ws, retained, MAP_SELECT_REPLACE );
    MapWorkspace_SetTool( &s.ws, map_tool_t::SELECT ); MapWorkspace_SetElementMode( &s.ws, map_element_mode_t::EDGES );
    const auto *document = s.ws.pDocument;
    const auto revision = document->geometry.revision, selectionRevision = s.ws.selection.revision;
    const auto *roots = s.ws.selection.ids.pData;
    const usize steps = EditorHistory_StepCount( &s.ws.history ); const auto token = UndoRedo_StateToken( s.ws.history.pUndo );
    REQUIRE_FALSE( MapWorkspace_ConvertBrushForComponents( &s.ws, locked ) );
    CHECK( s.ws.pDocument == document ); CHECK( document->geometry.revision == revision );
    CHECK( s.ws.selection.ids.pData == roots ); CHECK( s.ws.selection.revision == selectionRevision );
    REQUIRE( s.ws.selection.ids.nCount == 1 ); CHECK( s.ws.selection.ids.pData[0] == retained );
    CHECK( s.Object( locked ).kind == map_wire_kind_t::BRUSH );
    CHECK( EditorHistory_StepCount( &s.ws.history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( s.ws.history.pUndo ) ) );
    REQUIRE( MapLayers_SetLocked( &s.ws, layer, false ) ); REQUIRE( MapWorkspace_ConvertBrushForComponents( &s.ws, locked ) );
    CHECK( s.Object( locked ).kind == map_wire_kind_t::MESH );
    REQUIRE( MapWorkspace_Undo( &s.ws ) == editor_history_status_t::OK ); CHECK( s.Object( locked ).kind == map_wire_kind_t::BRUSH );
    REQUIRE( MapWorkspace_Redo( &s.ws ) == editor_history_status_t::OK ); CHECK( s.Object( locked ).kind == map_wire_kind_t::MESH );
}

namespace
{
struct layer_allocation_t { usize calls{}, live{}; bool fail{}; };
void *LayerAllocate( void *context, usize bytes, usize alignment ) noexcept
{
    auto &audit = *static_cast<layer_allocation_t *>( context ); ++audit.calls;
    if ( audit.fail ) { return nullptr; }
    void *memory = Allocator_Allocate( Allocator_GetSystem(), bytes, alignment );
    if ( memory != nullptr ) { ++audit.live; }
    return memory;
}
void LayerFree( void *context, void *memory, usize bytes, usize alignment ) noexcept
{
    if ( memory != nullptr ) { --static_cast<layer_allocation_t *>( context )->live; }
    Allocator_Free( Allocator_GetSystem(), memory, bytes, alignment );
}
struct layer_allocator_scope_t {
    gui::editor_gui_t &gui;
    const allocator_t *previous;
    layer_allocator_scope_t( gui::editor_gui_t &target, const allocator_t *allocator ) : gui( target ), previous( target.pAllocator ) { gui.pAllocator = allocator; }
    ~layer_allocator_scope_t() { gui.pAllocator = previous; }
};
}

TEST_CASE( "A failed hierarchy lock query refuses the edit without publishing partial state", "[map][gui][layers][layer-lock-policy][allocation]" )
{
    // Keep the allocator alive through fixture teardown even if a regression
    // unexpectedly publishes a buffer during the failed operation.
    layer_allocation_t audit{}; const allocator_t allocator{ &LayerAllocate, nullptr, &LayerFree, &audit };
    {
        layer_hierarchy_fixture_t f; auto &ws = f.s.ws;
        REQUIRE( MapDocument_AddLayer( ws.pDocument, StringView_FromCString( "unrelated" ), StringView_FromCString( "Unrelated" ) ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &ws );
        REQUIRE( MapLayers_SetLocked( &ws, QStringLiteral( "unrelated" ), true ) );
        const auto *document = ws.pDocument; const auto revision = document->geometry.revision, selectionRevision = ws.selection.revision;
        const auto *roots = ws.selection.ids.pData; const usize steps = EditorHistory_StepCount( &ws.history ); const auto token = UndoRedo_StateToken( ws.history.pUndo );
        {
            layer_allocator_scope_t scope( f.s.gui, &allocator );
            REQUIRE( MapWorkspace_CanMoveSelection( &ws ) ); REQUIRE( audit.calls > 0 ); CHECK( audit.live == 0 );
            audit.fail = true; audit.calls = 0;
            CHECK_FALSE( MapWorkspace_CanMoveSelection( &ws ) ); CHECK( audit.calls > 0 ); CHECK( audit.live == 0 );
            audit.calls = 0;
            REQUIRE_FALSE( MapWorkspace_TranslateSelection( &ws, { 64, 0, 0 } ) ); CHECK( audit.calls > 0 ); CHECK( audit.live == 0 );
        }
        CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
        CHECK( ws.selection.ids.pData == roots ); CHECK( ws.selection.revision == selectionRevision );
        REQUIRE( ws.selection.ids.nCount == 1 ); CHECK( ws.selection.ids.pData[0] == f.parent );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws.history.pUndo ) ) ); f.CheckAt( 0 );
        REQUIRE( MapWorkspace_TranslateSelection( &ws, { 64, 0, 0 } ) ); f.CheckAt( 64 );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); f.CheckAt( 0 );
        REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); f.CheckAt( 64 );
    }
    CHECK( audit.live == 0 );
}

TEST_CASE( "Ordinary brush lock checks need no hierarchy allocation", "[map][gui][layers][layer-lock-policy][allocation]" )
{
    layer_allocation_t audit{}; const allocator_t allocator{ &LayerAllocate, nullptr, &LayerFree, &audit };
    session_t s;
    const u64 movable = s.Box( { 0, 0, 0 }, { 64, 64, 64 } );
    const u64 locked = s.Box( { 128, 0, 0 }, { 192, 64, 64 } );
    REQUIRE( s.Run( "map.layer.create" ) == command_result_t::OK );
    REQUIRE( MapLayers_SetLocked( &s.ws, QString::fromStdString( s.LayerOf( locked ) ), true ) );
    MapWorkspace_SetElementMode( &s.ws, map_element_mode_t::OBJECTS );
    MapWorkspace_Select( &s.ws, movable, MAP_SELECT_REPLACE );
    REQUIRE( s.ws.selection.ids.nCount == 1 ); REQUIRE( s.ws.selection.ids.pData[0] == movable );
    const auto revision = s.ws.selection.revision;
    {
        layer_allocator_scope_t scope( s.gui, &allocator ); audit.fail = true;
        CHECK( MapWorkspace_CanMoveSelection( &s.ws ) ); CHECK( MapWorkspace_CanTurnSelection( &s.ws ) );
        CHECK( audit.calls == 0 ); CHECK( audit.live == 0 );
        // Skipping descendant collection must still check the requested root.
        CHECK_FALSE( MapWorkspace_AreEditTargetsUnlocked( &s.ws, { &locked, 1 }, true ) );
        CHECK( audit.calls == 0 ); CHECK( audit.live == 0 );
    }
    CHECK( s.ws.selection.revision == revision );
    REQUIRE( s.ws.selection.ids.nCount == 1 ); CHECK( s.ws.selection.ids.pData[0] == movable );
}
