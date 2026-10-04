//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMason_Smoke_Tests.cpp
//  Purpose: Smoke tests that build Mason's real window headlessly and drive
//           it through commands the way menus, shortcuts, and the console
//           do.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMason_MainWindow.h"
#include "CypherMason_Branding.h"

#include "CypherEditorGui_CommandPalette.h"
#include "CypherEditorGui_AppearancePage.h"
#include "CypherMapGui_Check.h"
#include "CypherMapGui_ToolPanels.h"
#include "CypherMapGui_Panels.h"
#include "CypherMapGui_Views.h"
#include "CypherMapGui_Input.h"
#include "CypherMapGui_EntityProperties.h"
#include "CypherEditorGui_Section.h"
#include "CypherEditorGui_AssetBrowser.h"
#include "CypherEditorGui_AssetWindow.h"
#include "CypherEditorGui_DatabaseView.h"
#include "CypherEditorGui_CodeEditor.h"
#include "CypherMason_RecentFiles.h"
#include "CypherMason_Welcome.h"
#include "CypherEditorGui_CommandHistory.h"
#include "CypherEditorGui_SettingsDialog.h"
#include "CypherEditorGui_KeymapSettings.h"
#include "CypherEditorGui_ShortcutsDialog.h"
#include "CypherEditorGui_ThemeEditor.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include "DockAreaWidget.h"
#include "DockWidget.h"
#include "DockManager.h"
#include "FloatingDockContainer.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <QMainWindow>
#include <QLineEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenuBar>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QTabWidget>
#include <QPushButton>
#include <QComboBox>
#include <QMenu>
#include <QTimer>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSplashScreen>
#include <QStatusBar>

#include <filesystem>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::mason;
using namespace cypher::editor::map;
using namespace cypher::editor::gui;

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

// Owns a headless Mason for one test.
struct mason_session_t {
    mason_session_t()
    {
        pMason = Mason_Create( App(), Allocator_GetSystem(), MASON_FLAG_HEADLESS );
        REQUIRE( pMason != nullptr );
        Mason_Window( pMason )->resize( 1400, 900 );
        Mason_Window( pMason )->show();
        QCoreApplication::processEvents();
    }
    ~mason_session_t() { Mason_Destroy( pMason ); }

    command_result_t Run( const QString &line ) const
    {
        const QByteArray utf8 = line.toUtf8();
        return EditorCommands_ExecuteLine( &Mason_Gui( pMason )->commands, string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) } );
    }

    mason_t *pMason{ nullptr };
};

} // namespace

TEST_CASE( "Mason category actions show mode properties empty and constrain Meshes selection commands", "[mason][smoke][selection-mode]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    auto *panel = window->findChild<QWidget *>( QStringLiteral( "MapToolProperties" ) ); REQUIRE( panel != nullptr );
    for ( const auto &profile : { std::pair{ "vertices", "Vertex Editing" }, std::pair{ "edges", "Edge Editing" },
        std::pair{ "faces", "Face Editing" }, std::pair{ "meshes", "Mesh Editing" } } ) {
        CAPTURE( profile.first ); MapWorkspace_SetTool( ws, map_tool_t::NONE );
        auto *action = window->findChild<QAction *>( QStringLiteral( "map.select_mode.%1" ).arg( profile.first ) );
        REQUIRE( action != nullptr ); REQUIRE( action->isEnabled() ); action->trigger(); QCoreApplication::processEvents();
        CHECK( action->isChecked() ); CHECK( ws->tool == map_tool_t::SELECT );
        CHECK( MapToolProperties_Title( panel ) == QString::fromUtf8( profile.second ) );
        CHECK( EditorSelection_Count( &ws->selection ) == 0u ); CHECK_FALSE( MapWorkspace_IsModified( ws ) );
    }
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.edges" ) ) == command_result_t::OK );
    CHECK( MapToolProperties_Operations( panel ).contains( QStringLiteral( "map.mesh.extrude_edges" ) ) );
    CHECK_FALSE( MapToolProperties_Operations( panel ).contains( QStringLiteral( "map.mesh.extrude" ) ) );
    CHECK( session.Run( QStringLiteral( "edit.select_all" ) ) == command_result_t::DISABLED );
    CHECK( session.Run( QStringLiteral( "edit.invert_selection" ) ) == command_result_t::DISABLED );
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.meshes" ) ) == command_result_t::OK );
    REQUIRE( ws->wire.entities.nCount != 0u );
    MapWorkspace_Select( ws, ws->wire.entities.pData[0].id, MAP_SELECT_REPLACE );
    CHECK( session.Run( QStringLiteral( "map.select.same_class" ) ) == command_result_t::DISABLED );
    usize eligible = 0;
    for ( usize i = 0; i < ws->wire.objects.nCount; ++i ) {
        const auto &object = ws->wire.objects.pData[i];
        if ( object.kind == map_wire_kind_t::BRUSH || object.kind == map_wire_kind_t::MESH ) { ++eligible; }
    }
    REQUIRE( eligible > 0u ); REQUIRE( eligible < ws->wire.objects.nCount );
    REQUIRE( session.Run( QStringLiteral( "edit.select_all" ) ) == command_result_t::OK );
    CHECK( EditorSelection_Count( &ws->selection ) == eligible );
    for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) {
        const auto *object = MapWireframe_FindObject( ws->wire, ws->selection.ids.pData[i] ); REQUIRE( object != nullptr );
        CHECK( ( object->kind == map_wire_kind_t::BRUSH || object->kind == map_wire_kind_t::MESH ) );
    }
    REQUIRE( session.Run( QStringLiteral( "edit.invert_selection" ) ) == command_result_t::OK );
    CHECK( EditorSelection_Count( &ws->selection ) == 0u );
    REQUIRE( session.Run( QStringLiteral( "edit.invert_selection" ) ) == command_result_t::OK );
    CHECK( EditorSelection_Count( &ws->selection ) == eligible ); CHECK_FALSE( MapWorkspace_IsModified( ws ) );
}

TEST_CASE( "Capture Mason component mode properties", "[.selection-mode-capture]" )
{
    mason_session_t session; auto *window = Mason_Window( session.pMason ); auto *ws = Mason_MapWorkspace( session.pMason );
    window->resize( 1600, 1050 ); QDir().mkpath( QStringLiteral( "artifacts" ) );
    auto *panel = window->findChild<QWidget *>( QStringLiteral( "MapToolProperties" ) ); REQUIRE( panel != nullptr );
    for ( const char *mode : { "vertices", "edges", "faces", "meshes" } ) {
        REQUIRE( session.Run( QStringLiteral( "map.select_mode.%1" ).arg( mode ) ) == command_result_t::OK );
        QCoreApplication::processEvents();
        REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_mode_%1_workspace.png" ).arg( mode ) ) );
        REQUIRE( panel->grab().save( QStringLiteral( "artifacts/mason_mode_%1_properties.png" ).arg( mode ) ) );
    }
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.meshes" ) ) == command_result_t::OK );
    MapWorkspace_Select( ws, 1001, MAP_SELECT_REPLACE ); MapWorkspace_Frame( ws, CY_FALSE ); QCoreApplication::processEvents();
    REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_mode_meshes_selected.png" ) ) );
}

TEST_CASE( "Mason Escape leaves editing controls unchecked and can resume editing from Navigation", "[mason][smoke][input][neutral-navigation]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *window = Mason_Window( session.pMason );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    auto *camera = MapViews_PaneView( views, 0 ); REQUIRE( camera != nullptr );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_CreateBox( ws, box ) );
    const u64 selected = EditorSelection_At( &ws->selection, 0 );
    const auto *document = ws->pDocument; const auto steps = EditorHistory_StepCount( &ws->history );
    MapWorkspace_Frame( ws, CY_TRUE ); camera->setFocus(); QCoreApplication::processEvents();
    const auto key = [&]( int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        QKeyEvent preflight( QEvent::ShortcutOverride, code, modifiers ); QCoreApplication::sendEvent( camera, &preflight );
        QKeyEvent press( QEvent::KeyPress, code, modifiers ); QCoreApplication::sendEvent( camera, &press );
        QKeyEvent release( QEvent::KeyRelease, code, modifiers ); QCoreApplication::sendEvent( camera, &release );
        QCoreApplication::processEvents();
    };
    key( Qt::Key_Escape ); REQUIRE( ws->tool == map_tool_t::NONE );
    CHECK( MapWorkspace_IsSelected( ws, selected ) );
    const auto *palette = window->findChild<QWidget *>( QStringLiteral( "EditorToolPalette" ) ); REQUIRE( palette != nullptr );
    for ( const auto *button : palette->findChildren<QToolButton *>() ) {
        const auto *action = button->defaultAction(); REQUIRE( action != nullptr );
        if ( action->objectName().startsWith( QStringLiteral( "map.tool." ) ) && action->isCheckable() ) {
            CAPTURE( action->objectName().toStdString() ); CHECK_FALSE( action->isChecked() ); CHECK_FALSE( button->isChecked() );
        }
    }
    QKeyEvent up( QEvent::KeyPress, Qt::Key_E, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &up );
    CHECK( ws->tool == map_tool_t::NONE ); CHECK( MapCameraView_NavigationVelocity( camera ).z > 0 );
    QKeyEvent upRelease( QEvent::KeyRelease, Qt::Key_E, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &upRelease );
    key( Qt::Key_S, Qt::ShiftModifier ); REQUIRE( ws->tool == map_tool_t::SELECT );
    CHECK( window->findChild<QAction *>( QStringLiteral( "map.tool.select" ) )->isChecked() );
    CHECK( MapWorkspace_IsSelected( ws, selected ) );
    CHECK( MapCameraView_NavigationVelocity( camera ).z == 0 );
    key( Qt::Key_Escape ); REQUIRE( ws->tool == map_tool_t::NONE );
    std::filesystem::create_directories( "artifacts" );
    REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_navigation_workspace.png" ) ) );
    key( Qt::Key_Escape ); CHECK( EditorSelection_Count( &ws->selection ) == 0u );
    CHECK( ws->pDocument == document ); CHECK( EditorHistory_StepCount( &ws->history ) == steps );
    REQUIRE( session.Run( QStringLiteral( "map.tool.block" ) ) == command_result_t::OK );
    CHECK( ws->tool == map_tool_t::BLOCK );
    CHECK( window->findChild<QAction *>( QStringLiteral( "map.tool.block" ) )->isChecked() );
}

TEST_CASE( "Mason tool controls forward cancellation without stealing input from text fields or popups", "[mason][smoke][input][neutral-navigation][tool-cancel-bridge]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views != nullptr );
    auto *palette = window->findChild<QWidget *>( QStringLiteral( "EditorToolPalette" ) ); REQUIRE( palette != nullptr );
    QToolButton *blockButton = nullptr;
    for ( auto *button : palette->findChildren<QToolButton *>() ) {
        if ( button->defaultAction() != nullptr && button->defaultAction()->objectName() == QStringLiteral( "map.tool.block" ) ) { blockButton = button; break; }
    }
    REQUIRE( blockButton != nullptr );
    const auto key = []( QWidget *control, int code, bool exactFocus = true ) {
        INFO( "Control: " << control->metaObject()->className() << " / " << control->objectName().toStdString() << " key " << code );
        control->window()->activateWindow(); control->setFocus(); QCoreApplication::processEvents();
        auto *focused = QApplication::focusWidget(); REQUIRE( focused != nullptr );
        if ( exactFocus ) { REQUIRE( focused == control ); }
        else { REQUIRE( ( focused == control || focused == control->focusProxy() || control->isAncestorOf( focused ) || focused->isAncestorOf( control ) ) ); }
        QKeyEvent preflight( QEvent::ShortcutOverride, code, Qt::NoModifier ); QCoreApplication::sendEvent( focused, &preflight );
        QKeyEvent press( QEvent::KeyPress, code, Qt::NoModifier ); QCoreApplication::sendEvent( focused, &press );
        QCoreApplication::processEvents();
    };
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_CreateBox( ws, box ) ); const u64 selected = EditorSelection_At( &ws->selection, 0 );
    const auto *document = ws->pDocument; const auto steps = EditorHistory_StepCount( &ws->history );
    MapViews_SetActivePane( views, 0 ); REQUIRE( session.Run( QStringLiteral( "map.tool.block" ) ) == command_result_t::OK );
    MapWorkspace_SetEditPreview( ws, box ); REQUIRE( MapWorkspace_StageBlockPreview( ws ) );
    key( blockButton, Qt::Key_Escape ); CHECK_FALSE( ws->editPreview.bActive ); CHECK( ws->tool == map_tool_t::BLOCK );
    key( blockButton, Qt::Key_Escape ); REQUIRE( ws->tool == map_tool_t::NONE ); CHECK( MapWorkspace_IsSelected( ws, selected ) );
    REQUIRE( session.Run( QStringLiteral( "map.tool.translate" ) ) == command_result_t::OK );
    auto *panel = window->findChild<QWidget *>( QStringLiteral( "MapToolProperties" ) ); REQUIRE( panel != nullptr );
    auto *keys = panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ); REQUIRE( keys != nullptr );
    key( keys, Qt::Key_Escape ); REQUIRE( ws->tool == map_tool_t::NONE );
    REQUIRE( session.Run( QStringLiteral( "map.tool.translate" ) ) == command_result_t::OK );
    auto *number = panel->findChild<QDoubleSpinBox *>(); REQUIRE( number != nullptr );
    auto *field = number->findChild<QLineEdit *>(); REQUIRE( field != nullptr );
    key( field, Qt::Key_Escape, false ); CHECK( ws->tool == map_tool_t::TRANSLATE );
    QMenu popup( window ); popup.addAction( QStringLiteral( "Native menu item" ) ); popup.popup( window->mapToGlobal( QPoint( 100, 100 ) ) );
    QCoreApplication::processEvents(); REQUIRE( QApplication::activePopupWidget() != nullptr );
    QKeyEvent cancel( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    CHECK_FALSE( MapViews_HandleToolCancel( views, &cancel ) ); CHECK( ws->tool == map_tool_t::TRANSLATE ); popup.close();
    settings_document_t remap{}; REQUIRE( SettingsDocument_Init( &remap, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &remap, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "tool_control_cancel"
  bindings = {
    "map.viewport.2d" = { "map.tool.cancel" = [ "F10" ] }
    "map.viewport.3d" = { "map.tool.cancel" = [ "F9" ] }
  }
})cykv" ) ).status == settings_document_status_t::OK );
    auto *gui = Mason_Gui( session.pMason ); const auto base = gui->keymapChain[0]; const usize depth = gui->nKeymapChain;
    struct keymap_restore_t {
        editor_gui_t *gui; const key_value_t *root; usize depth;
        ~keymap_restore_t() { gui->keymapChain[0] = root; gui->nKeymapChain = depth; }
    } restore{ gui, base, depth };
    gui->keymapChain[0] = SettingsDocument_Root( &remap ); gui->nKeymapChain = 1;
    MapViews_SetActivePane( views, 1 );
    key( blockButton, Qt::Key_Escape ); CHECK( ws->tool == map_tool_t::TRANSLATE );
    key( blockButton, Qt::Key_F9 ); CHECK( ws->tool == map_tool_t::TRANSLATE );
    key( blockButton, Qt::Key_F10 ); CHECK( ws->tool == map_tool_t::NONE );
    gui->keymapChain[0] = base; gui->nKeymapChain = depth;
    CHECK( ws->pDocument == document ); CHECK( EditorHistory_StepCount( &ws->history ) == steps ); CHECK( MapWorkspace_IsSelected( ws, selected ) );
}

TEST_CASE( "Focused Mason viewport routes geometry keys without entering camera flight", "[mason][smoke][geometry-edit][input]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *views = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    QWidget *camera = MapViews_PaneView( views, 0 );
    REQUIRE( camera != nullptr );
    camera->setFocus();
    const auto key = [&]( int code ) {
        QKeyEvent preflight( QEvent::ShortcutOverride, code, Qt::NoModifier );
        QCoreApplication::sendEvent( camera, &preflight );
        QKeyEvent press( QEvent::KeyPress, code, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &press );
        QKeyEvent release( QEvent::KeyRelease, code, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &release );
    };
    key( Qt::Key_T ); CHECK( ws->tool == map_tool_t::TRANSLATE );
    key( Qt::Key_R ); CHECK( ws->tool == map_tool_t::ROTATE );
    key( Qt::Key_E ); CHECK( ws->tool == map_tool_t::SCALE );
    key( Qt::Key_3 ); CHECK( ws->elementMode == map_element_mode_t::FACES );
    key( Qt::Key_5 ); CHECK( ws->elementMode == map_element_mode_t::OBJECTS );
    key( Qt::Key_6 ); CHECK( ws->elementMode == map_element_mode_t::GROUPS );
    const auto velocity = MapCameraView_NavigationVelocity( camera, Qt::NoModifier );
    CHECK( velocity.x == 0 ); CHECK( velocity.y == 0 ); CHECK( velocity.z == 0 );
    CHECK( EditorHistory_StepCount( &ws->history ) == 0 );
}

TEST_CASE( "Capture Mason geometry authoring workspace", "[.geometry-workspace-screenshot]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    Mason_Window( session.pMason )->resize( 2500, 1540 );
    const auto box = [&]( math::vec3d_t minimum, math::vec3d_t maximum ) {
        map_bounds_t bounds{}; MapBounds_AddPoint( bounds, minimum ); MapBounds_AddPoint( bounds, maximum );
        REQUIRE( MapWorkspace_CreateBox( ws, bounds ) );
    };
    box( { -256, -256, -16 }, { 256, 256, 0 } );
    box( { 240, -256, 0 }, { 256, 256, 192 } );
    box( { -256, 240, 0 }, { 240, 256, 192 } );
    box( { -64, -64, 0 }, { 64, 64, 96 } );
    const u64 selected = EditorSelection_At( &ws->selection, 0 );
    auto *views = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::SHADED );
    MapWorkspace_Frame( ws, CY_FALSE ); MapWorkspace_SetTool( ws, map_tool_t::TRANSLATE );
    QCoreApplication::processEvents();
    REQUIRE( MapWorkspace_SaveAs( ws, QFileInfo( QStringLiteral( "artifacts/mason_geometry_demo.cymap" ) ).absoluteFilePath() ).status == map_files_status_t::OK );
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_geometry_workspace.png" ) ) );
    const auto *brush = geometry::GeometryDocument_FindBrush( &ws->pDocument->geometry, { selected } );
    REQUIRE( brush != nullptr );
    MapWorkspace_SelectBrushFace( ws, selected, brush->sides.pData[4].sourceId.value );
    MapWorkspace_SetTool( ws, map_tool_t::EXTRUDE ); QCoreApplication::processEvents();
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_geometry_workspace_face.png" ) ) );
    // Capture an actual construction gesture: all panes share the exact
    // generated cylinder, while only the active pane shows the live readout.
    const auto *shape = EditorSettings_Find( &ws->pGui->settings, StringView_FromCString( "editor.map.new_brush_shape" ) );
    REQUIRE( shape != nullptr );
    setting_value_t value{}; value.type = setting_type_t::ENUM; value.text = StringView_FromCString( "cylinder" );
    REQUIRE( EditorSettings_Write( &ws->pGui->settings, settings_scope_t::USER, *shape, value ) == settings_registry_status_t::OK );
    MapWorkspace_SetTool( ws, map_tool_t::BLOCK );
    QWidget *camera = MapViews_PaneView( views, 0 ); REQUIRE( camera != nullptr );
    QPointF start, end;
    REQUIRE( MapCameraView_WorldToView( camera, { -208, -208, 0 }, &start ) );
    REQUIRE( MapCameraView_WorldToView( camera, { -80, -80, 0 }, &end ) );
    QMouseEvent press( QEvent::MouseButtonPress, start, camera->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( camera, &press );
    QMouseEvent move( QEvent::MouseMove, end, camera->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( camera, &move );
    QCoreApplication::processEvents();
    REQUIRE( ws->editPreview.bActive ); REQUIRE( ws->editPreview.status == map_status_t::OK );
    CHECK( ws->editPreviewWire.faces.nCount == 18 );
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_geometry_workspace_live.png" ) ) );
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &escape );
    MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
    MapWorkspace_SetTool( ws, map_tool_t::CLIP );
    const auto *clipMode = EditorSettings_Find( &ws->pGui->settings, StringView_FromCString( "editor.map.clip_mode" ) );
    REQUIRE( clipMode != nullptr ); value.text = StringView_FromCString( "back" );
    REQUIRE( EditorSettings_Write( &ws->pGui->settings, settings_scope_t::USER, *clipMode, value ) == settings_registry_status_t::OK );
    QWidget *top = MapViews_PaneView( views, 1 ); REQUIRE( top != nullptr );
    start = MapOrthoView_WorldToView( top, { 0, -112 } ); end = MapOrthoView_WorldToView( top, { 0, 112 } );
    QMouseEvent clipPress( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &clipPress );
    QMouseEvent clipMove( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &clipMove );
    QMouseEvent clipRelease( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &clipRelease );
    REQUIRE( ws->editPreview.bClip ); REQUIRE( ws->editPreview.status == map_status_t::OK );
    REQUIRE( ws->editPreviewWire.bounds.box.maximum.x == 0 );
    QCoreApplication::processEvents();
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_geometry_workspace_clip.png" ) ) );
    REQUIRE( ws->editPreview.clipGuide.bHas );
    const auto fixed = ws->editPreview.clipGuide.points[1];
    REQUIRE( MapCameraView_WorldToView( camera, ws->editPreview.clipGuide.points[0], &start ) );
    REQUIRE( MapCameraView_WorldToView( camera, { 32, -80, 48 }, &end ) );
    const auto dragClip = [&]( QPointF a, QPointF b ) {
        QMouseEvent down( QEvent::MouseButtonPress, a, camera->mapToGlobal( a ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QCoreApplication::sendEvent( camera, &down );
        QMouseEvent motion( QEvent::MouseMove, b, camera->mapToGlobal( b ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
        QCoreApplication::sendEvent( camera, &motion );
        QMouseEvent up( QEvent::MouseButtonRelease, b, camera->mapToGlobal( b ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
        QCoreApplication::sendEvent( camera, &up );
        QCoreApplication::processEvents();
    };
    dragClip( start, end );
    REQUIRE( ws->editPreview.status == map_status_t::OK ); REQUIRE( ws->editPreview.clipGuide.bHas );
    CHECK( ws->editPreview.clipGuide.points[0].x == 32 ); CHECK( ws->editPreview.clipGuide.points[0].y == -80 );
    CHECK( math::Vec3d_EqualsExact( ws->editPreview.clipGuide.points[1], fixed ) );
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_geometry_workspace_clip_handles.png" ) ) );
    QCoreApplication::sendEvent( camera, &escape );
    REQUIRE( MapCameraView_WorldToView( camera, { 16, -112, 48 }, &start ) );
    REQUIRE( MapCameraView_WorldToView( camera, { -16, 112, 48 }, &end ) );
    dragClip( start, end );
    REQUIRE( ws->editPreview.status == map_status_t::OK ); REQUIRE( ws->editPreview.clipGuide.bHas );
    CHECK( ws->editPreview.clipGuide.extrusionAxis == 2 );
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_geometry_workspace_clip_3d.png" ) ) );
    CHECK( EditorHistory_StepCount( &ws->history ) == 4 );
    QCoreApplication::sendEvent( camera, &escape );
    // Exercise the registered Merge command in the real Mason workspace and
    // capture its selected result with the existing dimension/gizmo drawing.
    MapWorkspace_SetTool( ws, map_tool_t::SELECT );
    box( { 64, -64, 0 }, { 192, 64, 96 } );
    const u64 adjacent = EditorSelection_At( &ws->selection, 0 );
    const u64 operands[]{ selected, adjacent };
    MapWorkspace_SetSelection( ws, operands, 2 );
    REQUIRE( session.Run( QStringLiteral( "map.brush.merge" ) ) == command_result_t::OK );
    REQUIRE( ws->selection.ids.nCount == 1 );
    const u64 merged = EditorSelection_At( &ws->selection, 0 );
    CHECK( merged != selected ); CHECK( merged != adjacent );
    CHECK( ws->pDocument->geometry.brushes.nCount == 4 );
    CHECK( EditorHistory_StepCount( &ws->history ) == 6 );
    const auto *mergedObject = MapWireframe_FindObject( ws->wire, merged ); REQUIRE( mergedObject != nullptr );
    CHECK( mergedObject->bounds.box.minimum.x == -64 ); CHECK( mergedObject->bounds.box.maximum.x == 192 );
    MapWorkspace_SetTool( ws, map_tool_t::TRANSLATE ); QCoreApplication::processEvents();
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_geometry_workspace_merge.png" ) ) );
    REQUIRE( MapWorkspace_SaveAs( ws, QFileInfo( QStringLiteral( "artifacts/mason_geometry_merge_demo.cymap" ) ).absoluteFilePath() ).status == map_files_status_t::OK );
    MapWorkspace_SetTool( ws, map_tool_t::SELECT ); QCoreApplication::processEvents();
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_geometry_workspace_select.png" ) ) );
    // Select's visible X handle is operational in the real window. Capture
    // its private ghost and dimensions, then save the committed movement.
    const auto pivot = MapBounds_Center( MapViews_SelectionGeometryBounds( ws ) );
    start = MapOrthoView_WorldToView( top, { pivot.x + 48.0 / MapOrthoView_Zoom( top ), pivot.y } );
    end = start + QPointF( 32.0 * MapOrthoView_Zoom( top ), 0 );
    QMouseEvent directPress( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &directPress );
    QMouseEvent directMove( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &directMove ); QCoreApplication::processEvents();
    REQUIRE( ws->editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws->history ) == 6 );
    CHECK( MapViews_SelectionGeometryBounds( ws ).box.minimum.x == -64 );
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_geometry_workspace_select_live.png" ) ) );
    QMouseEvent directRelease( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &directRelease );
    CHECK( ws->tool == map_tool_t::SELECT ); CHECK( EditorHistory_StepCount( &ws->history ) == 7 );
    CHECK( MapViews_SelectionGeometryBounds( ws ).box.minimum.x == -32 );
    REQUIRE( MapWorkspace_SaveAs( ws, QFileInfo( QStringLiteral( "artifacts/mason_geometry_workflow_demo.cymap" ) ).absoluteFilePath() ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    CHECK( MapViews_SelectionGeometryBounds( ws ).box.minimum.x == -64 );
    REQUIRE( MapWorkspace_Redo( ws ) == editor_history_status_t::OK );
    CHECK( MapViews_SelectionGeometryBounds( ws ).box.minimum.x == -32 );
    // A non-box transform must carry its authored edges and truthful bounds
    // into the inactive camera and Front panes in the real themed workspace.
    map_primitive_desc_t cylinder{};
    cylinder.kind = map_primitive_kind_t::CYLINDER; cylinder.nSides = 5;
    cylinder.bounds = { { -224, -128, 0 }, { -64, 96, 160 } };
    REQUIRE( MapWorkspace_CreatePrimitive( ws, cylinder ) );
    MapWorkspace_SetTool( ws, map_tool_t::ROTATE ); QCoreApplication::processEvents();
    const auto rotationPivot = MapBounds_Center( MapViews_SelectionGeometryBounds( ws ) );
    const f64 radius = 64.0 / MapOrthoView_Zoom( top );
    start = MapOrthoView_WorldToView( top, { rotationPivot.x + radius, rotationPivot.y } );
    constexpr f64 diagonal = 0.7071067811865475244;
    end = MapOrthoView_WorldToView( top, { rotationPivot.x + radius * diagonal, rotationPivot.y + radius * diagonal } );
    const auto *unpublished = ws->pDocument;
    const auto historyCount = EditorHistory_StepCount( &ws->history );
    QMouseEvent rotatePress( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &rotatePress );
    QMouseEvent rotateMove( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &rotateMove ); QCoreApplication::processEvents();
    REQUIRE( ws->editPreview.bActive ); REQUIRE( ws->editPreview.transform.kind == map_transform_preview_kind_t::ROTATE );
    CHECK( ws->editPreview.transform.degrees.z == 45 ); CHECK( ws->pDocument == unpublished );
    CHECK( EditorHistory_StepCount( &ws->history ) == historyCount );
    const auto rotatedBounds = ws->editPreview.bounds;
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_shared_transform_workspace.png" ) ) );
    QMouseEvent rotateRelease( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &rotateRelease );
    REQUIRE_FALSE( ws->editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws->history ) == historyCount + 1 );
    const auto committedBounds = MapViews_SelectionGeometryBounds( ws );
    CHECK( std::abs( committedBounds.box.minimum.x - rotatedBounds.box.minimum.x ) < 1e-6 );
    CHECK( std::abs( committedBounds.box.maximum.y - rotatedBounds.box.maximum.y ) < 1e-6 );
    std::filesystem::create_directories( "artifacts/shared-transform" );
    REQUIRE( MapWorkspace_SaveAs( ws, QFileInfo( QStringLiteral( "artifacts/shared-transform/demo.cymap" ) ).absoluteFilePath() ).status == map_files_status_t::OK );
    // Resize the rotated, non-box solid directly in Select. Its opposite side
    // remains fixed while all panes share the exact transformed edges/readout.
    MapWorkspace_SetTool( ws, map_tool_t::SELECT ); QCoreApplication::processEvents();
    const auto resizeBounds = MapViews_SelectionGeometryBounds( ws ); const auto resizeCenter = MapBounds_Center( resizeBounds );
    start = MapOrthoView_WorldToView( top, { resizeBounds.box.maximum.x, resizeCenter.y } );
    end = start + QPointF( 64 * MapOrthoView_Zoom( top ), 0 );
    const usize resizeHistory = EditorHistory_StepCount( &ws->history ); const auto *resizeDocument = ws->pDocument;
    QMouseEvent resizePress( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &resizePress );
    QMouseEvent resizeMove( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &resizeMove ); QCoreApplication::processEvents();
    REQUIRE( ws->editPreview.bActive ); REQUIRE( ws->editPreview.transform.bResize );
    CHECK( ws->editPreview.transform.pivot.x == resizeBounds.box.minimum.x );
    CHECK( std::abs( ws->editPreview.bounds.box.minimum.x - resizeBounds.box.minimum.x ) < 1e-6 );
    CHECK( ws->editPreview.bounds.box.maximum.x > resizeBounds.box.maximum.x );
    CHECK( ws->pDocument == resizeDocument ); CHECK( EditorHistory_StepCount( &ws->history ) == resizeHistory );
    const auto liveResize = ws->editPreview.bounds;
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_select_resize_workspace.png" ) ) );
    QMouseEvent resizeRelease( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &resizeRelease );
    REQUIRE_FALSE( ws->editPreview.bActive ); CHECK( ws->tool == map_tool_t::SELECT );
    CHECK( EditorHistory_StepCount( &ws->history ) == resizeHistory + 1 );
    CHECK( std::abs( MapViews_SelectionGeometryBounds( ws ).box.maximum.x - liveResize.box.maximum.x ) < 1e-6 );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    CHECK( std::abs( MapViews_SelectionGeometryBounds( ws ).box.maximum.x - resizeBounds.box.maximum.x ) < 1e-6 );
    REQUIRE( MapWorkspace_Redo( ws ) == editor_history_status_t::OK );
    CHECK( std::abs( MapViews_SelectionGeometryBounds( ws ).box.maximum.x - liveResize.box.maximum.x ) < 1e-6 );
    std::filesystem::create_directories( "artifacts/select-resize" );
    const auto resizePath = QFileInfo( QStringLiteral( "artifacts/select-resize/demo.cymap" ) ).absoluteFilePath();
    const u64 resizedId = EditorSelection_At( &ws->selection, 0 );
    REQUIRE( MapWorkspace_SaveAs( ws, resizePath ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_Open( ws, resizePath ).status == map_files_status_t::OK );
    const auto *reloaded = MapWireframe_FindObject( ws->wire, resizedId ); REQUIRE( reloaded != nullptr );
    CHECK( std::abs( reloaded->bounds.box.minimum.x - liveResize.box.minimum.x ) < 1e-6 );
    CHECK( std::abs( reloaded->bounds.box.maximum.x - liveResize.box.maximum.x ) < 1e-6 );
    CHECK( std::abs( reloaded->bounds.box.maximum.y - liveResize.box.maximum.y ) < 1e-6 );
    // Construct a new primitive without publishing it on footprint release,
    // then adjust its height from the camera's visible RGB side control.
    MapWorkspace_Select( ws, 0, MAP_SELECT_REPLACE );
    MapWorkspace_SetTool( ws, map_tool_t::BLOCK );
    MapWorkspace_Frame( ws, CY_FALSE ); QCoreApplication::processEvents();
    const auto *constructionDocument = ws->pDocument;
    const usize constructionHistory = EditorHistory_StepCount( &ws->history );
    start = MapOrthoView_WorldToView( top, { 80, -128 } );
    end = MapOrthoView_WorldToView( top, { 208, 0 } );
    QMouseEvent footprintPress( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &footprintPress );
    QMouseEvent footprintMove( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &footprintMove );
    QMouseEvent footprintRelease( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &footprintRelease );
    REQUIRE( MapWorkspace_HasBlockPreview( ws ) ); REQUIRE( ws->editPreview.status == map_status_t::OK );
    CHECK( ws->pDocument == constructionDocument ); CHECK( EditorHistory_StepCount( &ws->history ) == constructionHistory );
    const auto constructionBounds = ws->editPreview.bounds; const auto constructionCenter = MapBounds_Center( constructionBounds );
    REQUIRE( MapCameraView_WorldToView( camera, { constructionCenter.x, constructionCenter.y, constructionBounds.box.maximum.z }, &start ) );
    REQUIRE( MapCameraView_WorldToView( camera, { constructionCenter.x, constructionCenter.y, constructionBounds.box.maximum.z + 64 }, &end ) );
    QMouseEvent heightPress( QEvent::MouseButtonPress, start, camera->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( camera, &heightPress );
    QMouseEvent heightMove( QEvent::MouseMove, end, camera->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( camera, &heightMove );
    QMouseEvent heightRelease( QEvent::MouseButtonRelease, end, camera->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( camera, &heightRelease ); QCoreApplication::processEvents();
    REQUIRE( MapWorkspace_HasBlockPreview( ws ) ); REQUIRE( ws->editPreview.status == map_status_t::OK );
    CHECK( ws->editPreview.bounds.box.minimum.z == constructionBounds.box.minimum.z );
    CHECK( std::abs( ws->editPreview.bounds.box.maximum.z - constructionBounds.box.maximum.z - 64 ) < 1e-6 );
    CHECK( ws->pDocument == constructionDocument ); CHECK( EditorHistory_StepCount( &ws->history ) == constructionHistory );
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_staged_block_workspace.png" ) ) );
    const auto constructed = ws->editPreview.primitive;
    QKeyEvent construct( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &construct );
    REQUIRE_FALSE( ws->editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws->history ) == constructionHistory + 1 );
    const u64 constructedId = EditorSelection_At( &ws->selection, 0 );
    REQUIRE( MapWireframe_FindObject( ws->wire, constructedId ) != nullptr );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    CHECK( MapWireframe_FindObject( ws->wire, constructedId ) == nullptr );
    REQUIRE( MapWorkspace_Redo( ws ) == editor_history_status_t::OK );
    std::filesystem::create_directories( "artifacts/staged-block" );
    const QString constructionPath = QFileInfo( QStringLiteral( "artifacts/staged-block/demo.cymap" ) ).absoluteFilePath();
    REQUIRE( MapWorkspace_SaveAs( ws, constructionPath ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_Open( ws, constructionPath ).status == map_files_status_t::OK );
    const auto *constructedObject = MapWireframe_FindObject( ws->wire, constructedId ); REQUIRE( constructedObject != nullptr );
    CHECK( std::abs( constructedObject->bounds.box.minimum.z - constructed.bounds.minimum.z ) < 1e-6 );
    CHECK( std::abs( constructedObject->bounds.box.maximum.z - constructed.bounds.maximum.z ) < 1e-6 );
    // Tiny selected geometry keeps visible controls in the actual themed
    // window. Pulling a control out on screen must not change its world anchor.
    MapWorkspace_SetTool( ws, map_tool_t::SELECT );
    box( { -144, -144, 0 }, { -128, -128, 16 } );
    const auto tinyBounds = MapViews_SelectionGeometryBounds( ws );
    const auto tinyCenter = MapBounds_Center( tinyBounds );
    const auto *tinyDocument = ws->pDocument;
    const auto tinyHistory = EditorHistory_StepCount( &ws->history );
    MapWorkspace_Frame( ws, CY_FALSE ); QCoreApplication::processEvents();
    const QPointF tinyPivot = MapOrthoView_WorldToView( top, { tinyCenter.x, tinyCenter.y } );
    const QPointF tinySide = MapOrthoView_WorldToView( top, { tinyBounds.box.maximum.x, tinyCenter.y } );
    REQUIRE( tinySide.x() - tinyPivot.x() < 24 );
    const f64 resizeClearance = 64.0 * EditorSettings_Real( &ws->pGui->settings, "editor.viewport.gizmo_scale", 1.0 ) + 24.0;
    start = tinyPivot + QPointF( resizeClearance, 0 ); end = start + QPointF( 32 * MapOrthoView_Zoom( top ), 0 );
    QMouseEvent tinyPress( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &tinyPress ); CHECK_FALSE( ws->editPreview.bActive );
    QMouseEvent tinyMove( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &tinyMove ); QCoreApplication::processEvents();
    REQUIRE( ws->editPreview.bActive ); REQUIRE( ws->editPreview.transform.bResize );
    CHECK( ws->editPreview.bounds.box.minimum.x == tinyBounds.box.minimum.x );
    CHECK( std::abs( ws->editPreview.bounds.box.maximum.x - tinyBounds.box.maximum.x - 32 ) < 1e-6 );
    CHECK( ws->pDocument == tinyDocument ); CHECK( EditorHistory_StepCount( &ws->history ) == tinyHistory );
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_gizmo_usability_workspace.png" ) ) );
    QMouseEvent tinyRelease( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &tinyRelease );
    CHECK( EditorHistory_StepCount( &ws->history ) == tinyHistory + 1 );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    CHECK( MapViews_SelectionGeometryBounds( ws ).box.maximum.x == tinyBounds.box.maximum.x );
    // Capture the user's source-location request through an actual move
    // gesture. The shared preview appears in Camera and both ortho panes.
    box( { -176, -64, 0 }, { -80, 32, 96 } );
    const u64 feedbackId = EditorSelection_At( &ws->selection, 0 );
    const auto feedbackBounds = MapViews_SelectionGeometryBounds( ws );
    const auto feedbackCenter = MapBounds_Center( feedbackBounds );
    const usize feedbackSteps = EditorHistory_StepCount( &ws->history );
    MapWorkspace_SetTool( ws, map_tool_t::TRANSLATE );
    start = MapOrthoView_WorldToView( top, { feedbackCenter.x, feedbackCenter.y } );
    end = start + QPointF( 192 * MapOrthoView_Zoom( top ), 0 );
    QMouseEvent feedbackPress( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &feedbackPress );
    QMouseEvent feedbackMove( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &feedbackMove ); QCoreApplication::processEvents();
    REQUIRE( ws->editPreview.bActive ); CHECK( ws->editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
    CHECK( std::abs( ws->editPreview.transform.delta.x - 192 ) < 1e-6 );
    CHECK( MapViews_SelectionGeometryBounds( ws ).box.minimum.x == feedbackBounds.box.minimum.x );
    CHECK( EditorHistory_StepCount( &ws->history ) == feedbackSteps );
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_transform_feedback_workspace.png" ) ) );
    QMouseEvent feedbackRelease( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &feedbackRelease );
    CHECK_FALSE( ws->editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws->history ) == feedbackSteps + 1 );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    REQUIRE( MapWireframe_FindObject( ws->wire, feedbackId ) != nullptr );
    CHECK( MapViews_SelectionGeometryBounds( ws ).box.minimum.x == feedbackBounds.box.minimum.x );
    // Widen both sides in Select, retaining the original center when Shift
    // is released after pickup. Capture one committed, persisted edit.
    MapWorkspace_SetTool( ws, map_tool_t::SELECT );
    const QPointF side = MapOrthoView_WorldToView( top, { feedbackBounds.box.maximum.x, feedbackCenter.y } );
    start = MapOrthoView_WorldToView( top, { feedbackCenter.x, feedbackCenter.y } );
    start.setX( start.x() + std::max( side.x() - start.x(), resizeClearance ) );
    end = start + QPointF( 32 * MapOrthoView_Zoom( top ), 0 );
    QMouseEvent centerPress( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier );
    QCoreApplication::sendEvent( top, &centerPress );
    QMouseEvent centerMove( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &centerMove ); QCoreApplication::processEvents();
    REQUIRE( ws->editPreview.bActive ); REQUIRE( ws->editPreview.transform.bResizeFromCenter );
    CHECK( std::abs( MapBounds_Center( ws->editPreview.bounds ).x - feedbackCenter.x ) < 1e-6 );
    CHECK( std::abs( ws->editPreview.bounds.box.minimum.x - feedbackBounds.box.minimum.x + 32 ) < 1e-6 );
    CHECK( std::abs( ws->editPreview.bounds.box.maximum.x - feedbackBounds.box.maximum.x - 32 ) < 1e-6 );
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_centered_resize_workspace.png" ) ) );
    QMouseEvent centerRelease( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &centerRelease );
    CHECK_FALSE( ws->editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws->history ) == feedbackSteps + 1 );
    const auto widened = MapViews_SelectionGeometryBounds( ws );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    CHECK( MapViews_SelectionGeometryBounds( ws ).box.minimum.x == feedbackBounds.box.minimum.x );
    REQUIRE( MapWorkspace_Redo( ws ) == editor_history_status_t::OK );
    const QString feedbackPath = QFileInfo( QStringLiteral( "artifacts/staged-block/transform-feedback.cymap" ) ).absoluteFilePath();
    REQUIRE( MapWorkspace_SaveAs( ws, feedbackPath ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_Open( ws, feedbackPath ).status == map_files_status_t::OK );
    const auto *widenedObject = MapWireframe_FindObject( ws->wire, feedbackId ); REQUIRE( widenedObject != nullptr );
    CHECK( std::abs( widenedObject->bounds.box.minimum.x - widened.box.minimum.x ) < 1e-6 );
    CHECK( std::abs( widenedObject->bounds.box.maximum.x - widened.box.maximum.x ) < 1e-6 );

    // Exercise the real menu commands and contextual viewport key over the
    // actual authored box, then persist and capture its mesh topology.
    MapWorkspace_SetTool( ws, map_tool_t::SELECT );
    MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
    MapWorkspace_Select( ws, feedbackId, MAP_SELECT_REPLACE );
    const auto meshFaces = [&]() {
        usize count = 0;
        for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) { count += ws->wire.faces.pData[i].id == feedbackId; }
        return count;
    };
    REQUIRE( EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( "map.brush.to_mesh" ), {} ) == command_result_t::OK );
    const auto *converted = MapWireframe_FindObject( ws->wire, feedbackId ); REQUIRE( converted != nullptr );
    CHECK( converted->kind == map_wire_kind_t::MESH ); CHECK( meshFaces() == 6 );
    CHECK( EditorSelection_At( &ws->selection, 0 ) == feedbackId );
    REQUIRE( EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( "map.mesh.triangulate" ), {} ) == command_result_t::OK );
    const auto *triangulated = MapWireframe_FindObject( ws->wire, feedbackId ); REQUIRE( triangulated != nullptr );
    CHECK( meshFaces() == 12 );
    const u64 triangulatedRevision = ws->pDocument->geometry.revision;
    top->setFocus();
    QKeyEvent reverseMesh( QEvent::KeyPress, Qt::Key_F, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &reverseMesh );
    CHECK( ws->pDocument->geometry.revision == triangulatedRevision + 1 );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    CHECK( meshFaces() == 12 );
    const QString meshPath = QFileInfo( QStringLiteral( "artifacts/mesh-authoring/demo.cymap" ) ).absoluteFilePath();
    REQUIRE( MapWorkspace_SaveAs( ws, meshPath ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_Open( ws, meshPath ).status == map_files_status_t::OK );
    const auto *persistedMesh = MapWireframe_FindObject( ws->wire, feedbackId ); REQUIRE( persistedMesh != nullptr );
    CHECK( persistedMesh->kind == map_wire_kind_t::MESH ); CHECK( meshFaces() == 12 );
    CHECK( std::abs( persistedMesh->bounds.box.minimum.x - widened.box.minimum.x ) < 1e-6 );
    CHECK( std::abs( persistedMesh->bounds.box.maximum.x - widened.box.maximum.x ) < 1e-6 );
    MapWorkspace_Select( ws, feedbackId, MAP_SELECT_REPLACE );
    QCoreApplication::processEvents();
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_mesh_authoring_workspace.png" ) ) );

    // Model one actual converted face through the same commands as the menu
    // and Tool Properties. Use a separate box so the cap is still a quad.
    box( { -64, -64, 0 }, { 64, 64, 96 } );
    const u64 faceMesh = EditorSelection_At( &ws->selection, 0 );
    REQUIRE( EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( "map.brush.to_mesh" ), {} ) == command_result_t::OK );
    u64 cap = 0;
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) {
        const auto &face = ws->wire.faces.pData[i];
        if ( face.id == faceMesh && face.faceId != 0 && face.normal.z > 0.9 ) { cap = face.faceId; break; }
    }
    REQUIRE( cap != 0 ); MapWorkspace_SelectMeshFace( ws, faceMesh, cap );
    MapWorkspace_SetTool( ws, map_tool_t::EXTRUDE );
    const usize faceSteps = EditorHistory_StepCount( &ws->history );
    REQUIRE( EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( "map.mesh.inset" ), {} ) == command_result_t::OK );
    CHECK( ws->selectedMeshFaceId == cap ); CHECK( EditorHistory_StepCount( &ws->history ) == faceSteps + 1 );
    REQUIRE( EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( "map.mesh.extrude" ), {} ) == command_result_t::OK );
    CHECK( ws->selectedMeshFaceId == cap ); CHECK( EditorHistory_StepCount( &ws->history ) == faceSteps + 2 );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK ); CHECK( ws->selectedMeshFaceId == cap );
    REQUIRE( MapWorkspace_Redo( ws ) == editor_history_status_t::OK ); CHECK( ws->selectedMeshFaceId == cap );
    const QString facePath = QFileInfo( QStringLiteral( "artifacts/mesh-face/modeling.cymap" ) ).absoluteFilePath();
    REQUIRE( MapWorkspace_SaveAs( ws, facePath ).status == map_files_status_t::OK );
    REQUIRE( MapWorkspace_Open( ws, facePath ).status == map_files_status_t::OK );
    MapWorkspace_SelectMeshFace( ws, faceMesh, cap ); REQUIRE( MapWorkspace_HasMeshFace( ws ) );
    usize persistedFaces = 0;
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) { persistedFaces += ws->wire.faces.pData[i].id == faceMesh; }
    CHECK( persistedFaces == 14 );
    MapWorkspace_Frame( ws, CY_TRUE );
    QCoreApplication::processEvents();
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_mesh_face_workspace.png" ) ) );
}

TEST_CASE( "Capture Mason entity key authoring in the real workspace", "[.entity-properties-screenshot]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *window = Mason_Window( session.pMason );
    window->resize( 2100, 1400 );
    MapWorkspace_Select( ws, 111u, MAP_SELECT_REPLACE ); // Entity 110's trigger brush.
    key_value_document_desc_t desc{}; desc.pAllocator = Allocator_GetSystem();
    auto *value = KeyValue_CreateDocument( desc );
    REQUIRE( value != nullptr );
    REQUIRE( KeyValue_SetString( value, KeyValue_Root( value ), StringView_FromCString( "scripts/arena/wave_spawn" ) ) );
    const usize steps = EditorHistory_StepCount( &ws->history );
    REQUIRE( MapWorkspace_SetEntityProperty( ws, StringView_FromCString( "script_path" ), KeyValue_Root( value ) ) );
    KeyValue_DestroyDocument( value );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1u );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Redo( ws ) == editor_history_status_t::OK );
    CHECK( EditorSelection_At( &ws->selection, 0u ) == 111u );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::FULLBRIGHT );
    MapWorkspace_Frame( ws, CY_FALSE );
    auto *panel = window->findChild<QWidget *>( QStringLiteral( "MapObjectProperties" ) );
    REQUIRE( panel != nullptr );
    auto *tree = panel->findChild<QTreeWidget *>( QStringLiteral( "MapPropertiesTree" ) );
    REQUIRE( tree != nullptr );
    for ( int i = 0; i < tree->topLevelItemCount(); ++i ) {
        auto *group = tree->topLevelItem( i );
        if ( group->text( 0 ) != QStringLiteral( "Entity keys" ) ) { group->setExpanded( false ); continue; }
        for ( int j = 0; j < group->childCount(); ++j ) {
            if ( group->child( j )->text( 0 ) == QStringLiteral( "script_path" ) ) { tree->setCurrentItem( group->child( j ) ); }
        }
    }
    QCoreApplication::processEvents();
    REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_entity_properties_workspace.png" ) ) );
    REQUIRE( panel->grab().save( QStringLiteral( "artifacts/mason_entity_properties_panel.png" ) ) );
    bool captured = false;
    QTimer::singleShot( 0, panel, [&]() {
        auto *dialog = qobject_cast<QDialog *>( QApplication::activeModalWidget() );
        CHECK( dialog != nullptr );
        if ( dialog != nullptr ) {
            captured = dialog->grab().save( QStringLiteral( "artifacts/mason_entity_value_editor.png" ) );
            dialog->reject();
        }
    } );
    CHECK_FALSE( MapEntityPropertyDialog_Show( panel, ws, map_entity_property_action_t::EDIT, StringView_FromCString( "script_path" ) ) );
    REQUIRE( captured );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1u );
}

TEST_CASE( "Mason builds its window with menus, tool strip, panels, and shortcuts", "[mason][smoke]" )
{
    mason_session_t session;
    QMainWindow *pWindow = Mason_Window( session.pMason );
    CHECK( pWindow->windowTitle().startsWith( QStringLiteral( "Untitled" ) ) );
    CHECK( pWindow->menuBar()->actions().size() == 10 ); // Window owns panel and layout commands.
    CHECK_FALSE( pWindow->menuBar()->isNativeMenuBar() );
    CHECK( pWindow->menuBar()->isVisibleTo( pWindow ) );

    const QToolBar *pTools = pWindow->findChild<QToolBar *>( QStringLiteral( "EditorToolStrip" ) );
    REQUIRE( pTools != nullptr );
    // Every original tool is retained, with extra operations in distinct groups.
    const QWidget *pPalette = pTools->findChild<QWidget *>( QStringLiteral( "EditorToolPalette" ) );
    REQUIRE( pPalette != nullptr );
    const auto buttons = pPalette->findChildren<QToolButton *>();
    REQUIRE( buttons.size() == 38 );
    CHECK( buttons[0]->defaultAction()->objectName() == QStringLiteral( "map.tool.select" ) );
    CHECK( buttons[1]->defaultAction()->objectName() == QStringLiteral( "map.tool.camera" ) );
    CHECK( buttons[0]->y() == buttons[1]->y() );
    CHECK( buttons[0]->x() < buttons[1]->x() );
    for ( const auto *pButton : buttons ) {
        CHECK( ( pButton->x() == buttons[0]->x() || pButton->x() == buttons[1]->x() ) );
        CHECK( pButton->menu() == nullptr );
        CHECK( !pButton->icon().isNull() );
    }
    CHECK( pTools->width() < 128 );
    CHECK( pWindow->toolBarArea( const_cast<QToolBar *>( pTools ) ) == Qt::LeftToolBarArea );

    CHECK( pWindow->findChild<QWidget *>( QStringLiteral( "mapTopView" ) ) != nullptr );
    auto *pManager = pWindow->findChild<ads::CDockManager *>();
    REQUIRE( pManager != nullptr );
    // ADS detaches inactive tab content from the widget tree. Query the
    // manager instead of counting only widgets parented to the window.
    auto *pConsole = pManager->findDockWidget( QStringLiteral( "console" ) );
    auto *pProblems = pManager->findDockWidget( QStringLiteral( "problems" ) );
    REQUIRE( pConsole != nullptr );
    REQUIRE( pProblems != nullptr );
    CHECK( pProblems->dockAreaWidget() == pConsole->dockAreaWidget() );
    auto *pProperties = pManager->findDockWidget( QStringLiteral( "map.properties" ) );
    REQUIRE( pProperties != nullptr );
    CHECK( pConsole->dockAreaWidget() == pProperties->dockAreaWidget() );
    CHECK( pConsole->dockAreaWidget()->dockWidgetsCount() == 4 );
    CHECK( pConsole->isClosed() );
    CHECK( pConsole->dockAreaWidget()->currentDockWidget() == pProperties );
    // Hammer keeps assets out of the docks: the panel exists (it owns the
    // asset scan) but the default layout does not dock it.
    auto *pAssets = pManager->findDockWidget( QStringLiteral( "assets" ) );
    CHECK( ( pAssets == nullptr || pAssets->isClosed() ) );
    CHECK( pWindow->findChild<QWidget *>( QStringLiteral( "EditorAssetBrowser" ) ) != nullptr );
    auto *pOutliner = pManager->findDockWidget( QStringLiteral( "map.outliner" ) );
    REQUIRE( pOutliner != nullptr );
    CHECK( pOutliner->dockAreaWidget()->dockWidgetsCount() == 2 );
    CHECK( pOutliner->dockAreaWidget()->currentDockWidget() == pOutliner );
    auto *pHistory = pManager->findDockWidget( QStringLiteral( "map.history" ) );
    REQUIRE( pHistory != nullptr );
    CHECK( !pHistory->isClosed() );
    CHECK( pHistory->dockAreaWidget() == pOutliner->dockAreaWidget() ); // Hammer: Undo History is the Outliner's neighbouring tab.
    CHECK( pHistory->widget()->findChild<QTreeWidget *>( QStringLiteral( "EditorHistorySteps" ) ) != nullptr );
    QWidget *pViews = pWindow->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( pViews != nullptr );
    CHECK( pViews->width() > pWindow->width() / 2 );
    // The enlarged asset browser has room for preview captions while the
    // editing views still occupy more than half the window's height.
    CHECK( pViews->height() > pWindow->height() / 2 );
    CHECK( MapViews_Arrangement( pViews ) == map_view_arrangement_t::HAMMER );
    QWidget *pShortcuts = pWindow->findChild<QWidget *>( QStringLiteral( "MapToolShortcuts" ) );
    REQUIRE( pShortcuts != nullptr );
    CHECK( EditorSection_IsExpanded( pShortcuts ) );

    const QAction *pSave = pWindow->findChild<QAction *>( QStringLiteral( "file.save" ) );
    REQUIRE( pSave != nullptr );
    CHECK( pSave->shortcuts().contains( QKeySequence( Qt::CTRL | Qt::Key_S ) ) );
    const QAction *pBlock = pWindow->findChild<QAction *>( QStringLiteral( "map.tool.block" ) );
    REQUIRE( pBlock != nullptr );
    CHECK( pBlock->isEnabled() ); // Box creation is connected to canonical brush geometry.
    const QAction *pSelect = pWindow->findChild<QAction *>( QStringLiteral( "map.tool.select" ) );
    REQUIRE( pSelect != nullptr );
    CHECK( pSelect->isChecked() );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "Grid 16" ) ) );
}

TEST_CASE( "Mason upper toolbars share command state and can be hidden independently", "[mason][smoke]" )
{
    mason_session_t session;
    auto *pWindow = Mason_Window( session.pMason );
    auto *pGeometry = pWindow->findChild<QToolBar *>( QStringLiteral( "masonGeometryTools" ) );
    auto *pMain = pWindow->findChild<QToolBar *>( QStringLiteral( "masonMainToolBar" ) );
    auto *pTools = pWindow->findChild<QToolBar *>( QStringLiteral( "EditorToolStrip" ) );
    auto *pMenu = pWindow->findChild<QMenu *>( QStringLiteral( "masonToolbarMenu" ) );
    REQUIRE( pGeometry != nullptr );
    REQUIRE( pMain != nullptr );
    REQUIRE( pTools != nullptr );
    REQUIRE( pMenu != nullptr );
    CHECK( pMenu->actions().contains( pGeometry->toggleViewAction() ) );
    for ( const char *id : { "map.mesh.extrude", "map.mesh.bevel", "map.mesh.boolean_subtract", "map.texture.fit", "map.texture.unwrap" } ) {
        QAction *pAction = pWindow->findChild<QAction *>( QString::fromLatin1( id ) );
        REQUIRE( pAction != nullptr );
        CHECK( pGeometry->actions().contains( pAction ) );
        CHECK( !pAction->icon().isNull() );
        CHECK_FALSE( pAction->isEnabled() ); // The GUI cannot claim an unwired edit is available.
    }
    REQUIRE( pGeometry->isVisible() );
    pGeometry->toggleViewAction()->trigger();
    CHECK_FALSE( pGeometry->isVisible() );
    CHECK( pMain->isVisible() );
    CHECK( pTools->isVisible() );
    pGeometry->toggleViewAction()->trigger();
    CHECK( pGeometry->isVisible() );

    auto *pSnap = pWindow->findChild<QAction *>( QStringLiteral( "map.grid.snap" ) );
    REQUIRE( pSnap != nullptr );
    REQUIRE( pMain->actions().contains( pSnap ) );
    auto *pStatusSnap = pWindow->findChild<QToolButton *>( QStringLiteral( "masonSnapToggle" ) );
    REQUIRE( pStatusSnap != nullptr );
    const bool wasChecked = pSnap->isChecked();
    pStatusSnap->click();
    CHECK( pSnap->isChecked() == !wasChecked );
    CHECK( pStatusSnap->isChecked() == !wasChecked );
    CHECK( bool( Mason_MapWorkspace( session.pMason )->bSnapToGrid ) == !wasChecked );
    pSnap->trigger();
    CHECK( pStatusSnap->isChecked() == wasChecked );

    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    REQUIRE( session.Run( QStringLiteral( "edit.select_all" ) ) == command_result_t::OK );
    auto *pHide = pWindow->findChild<QAction *>( QStringLiteral( "map.hide.selected" ) );
    auto *pShow = pWindow->findChild<QAction *>( QStringLiteral( "map.hide.show_all" ) );
    REQUIRE( pHide != nullptr );
    REQUIRE( pShow != nullptr );
    REQUIRE( pMain->actions().contains( pHide ) );
    REQUIRE( pMain->actions().contains( pShow ) );
    CHECK( pHide->isEnabled() );
    pHide->trigger();
    CHECK( pShow->isEnabled() );
    pShow->trigger();
    CHECK_FALSE( pShow->isEnabled() );
}

TEST_CASE( "Mason uses two toolbar rows and normalizes older saved placements", "[mason][smoke][workspace]" )
{
    mason_session_t session;
    QMainWindow *window = Mason_Window( session.pMason );
    // Placement assertions inspect the final layout, not Qt's toolbar
    // transition animation between the two presentations.
    window->setDockOptions( window->dockOptions() & ~QMainWindow::AnimatedDocks );
    window->resize( 2560, 1440 );
    QCoreApplication::processEvents();
    auto *main = window->findChild<QToolBar *>( "masonMainToolBar" );
    auto *view = window->findChild<QToolBar *>( "masonViewFilters" );
    auto *utilities = window->findChild<QToolBar *>( "masonUtilityTools" );
    auto *select = window->findChild<QToolBar *>( "masonSelectModes" );
    auto *edit = window->findChild<QToolBar *>( "masonEditingTools" );
    auto *geometry = window->findChild<QToolBar *>( "masonGeometryTools" );
    REQUIRE( ( main && view && utilities && select && edit && geometry ) );
    const auto checkRows = [&]() {
        CHECK( main->y() == view->y() );
        CHECK( main->y() == utilities->y() );
        CHECK( utilities->x() > view->x() );
        CHECK( select->y() == edit->y() );
        CHECK( select->y() == geometry->y() );
        CHECK( select->y() > main->y() );
        int previousRight = -1;
        for ( const char *name : { "MasonFastAssetSearch", "view.sound_preview.utility", "tools.settings.utility", "masonCommandSearch" } ) {
            auto *control = utilities->findChild<QWidget *>( QString::fromLatin1( name ) );
            REQUIRE( control != nullptr );
            CAPTURE( name );
            CHECK( control->isVisibleTo( window ) );
            CHECK( window->rect().contains( QRect( control->mapTo( window, QPoint() ), control->size() ) ) );
            const int left = control->mapTo( window, QPoint() ).x();
            if ( previousRight >= 0 ) { CHECK( left - previousRight <= 8 ); }
            previousRight = left + control->width();
        }
    };
    checkRows();
    for ( int width : { 1600, 1280 } ) {
        window->resize( width, 900 );
        QCoreApplication::processEvents();
        INFO( "Window width: " << width );
        CHECK( window->width() == width );
        checkRows();
    }
    window->resize( 2560, 1440 );
    QCoreApplication::processEvents();
    auto *gui = Mason_Gui( session.pMason );
    const auto *descriptor = EditorSettings_Find( &gui->settings, StringView_FromCString( "editor.ui.two_toolbar_rows" ) );
    REQUIRE( descriptor != nullptr );
    const auto compact = [&]( bool enabled ) {
        setting_value_t value{}; value.type = setting_type_t::BOOL; value.bValue = enabled ? CY_TRUE : CY_FALSE;
        REQUIRE( EditorSettings_Write( &gui->settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
        QCoreApplication::processEvents();
    };
    compact( false );
    REQUIRE_FALSE( EditorSettings_Bool( &gui->settings, "editor.ui.two_toolbar_rows", CY_TRUE ) );
    window->removeToolBar( geometry );
    window->addToolBar( geometry );
    window->insertToolBarBreak( geometry );
    geometry->show(); // removeToolBar hides it; the saved third row must be visible.
    edit->hide();
    QCoreApplication::processEvents();
    REQUIRE( window->toolBarBreak( geometry ) );
    REQUIRE( geometry->isVisible() );
    QCoreApplication::sendPostedEvents( nullptr, QEvent::LayoutRequest );
    QCoreApplication::processEvents();
    REQUIRE( geometry->y() > select->y() );
    REQUIRE( session.Run( "view.layout.save" ) == command_result_t::OK );
    compact( true );
    CHECK( edit->isHidden() );
    CHECK( geometry->y() == select->y() );
    REQUIRE( session.Run( "view.layout.restore" ) == command_result_t::OK );
    CHECK( edit->isHidden() );
    CHECK( geometry->y() == select->y() );
    compact( false );
    REQUIRE( session.Run( "view.layout.restore" ) == command_result_t::OK );
    CHECK( geometry->y() > select->y() );
}

TEST_CASE( "Mason branding loads and startup reports stages after saved settings", "[mason][smoke][startup]" )
{
    QStringList stages;
    const auto report = []( void *context, const editor_gui_t &gui, const char *stage ) {
        REQUIRE( EditorSettings_Find( &gui.settings, StringView_FromCString( "editor.ui.show_splash" ) ) != nullptr );
        static_cast<QStringList *>( context )->append( QString::fromUtf8( stage ) );
    };
    mason_t *mason = Mason_Create( App(), Allocator_GetSystem(), MASON_FLAG_HEADLESS, report, &stages );
    REQUIRE( mason != nullptr );
    CHECK( stages == ( QStringList{ "Preparing map workspace...", "Building tools and panels...", "Ready." } ) );
    CHECK_FALSE( Mason_Window( mason )->windowIcon().isNull() );
    CHECK_FALSE( Mason_ApplicationIcon().pixmap( 256, 256 ).isNull() );
    for ( const int size : { 64, 512, 1024 } ) {
        const QImage icon = Mason_ApplicationIcon().pixmap( size, size ).toImage();
        REQUIRE_FALSE( icon.isNull() );
        CHECK( icon.pixelColor( 0, 0 ).alpha() == 0 );
        CHECK( icon.pixelColor( icon.width() - 1, icon.height() - 1 ).alpha() == 0 );
        CHECK( icon.pixelColor( icon.width() / 2, icon.height() / 2 ).alpha() >= 250 );
    }
    CHECK_FALSE( QString::fromLatin1( Mason_Version() ).isEmpty() );
    const QPixmap startup = Mason_StartupImage( Mason_Gui( mason )->style );
    CHECK( startup.size() == QSize( 840, 460 ) );
    const QPixmap retina = Mason_StartupImage( Mason_Gui( mason )->style, 2.0 );
    CHECK( retina.size() == QSize( 1680, 920 ) );
    CHECK( retina.devicePixelRatio() == 2.0 );
    QSplashScreen *screen = Mason_StartupScreen( Mason_Gui( mason )->style );
    REQUIRE( screen != nullptr );
    CHECK( screen->objectName() == QStringLiteral( "MasonStartupScreen" ) );
    screen->showMessage( QStringLiteral( "Opening map..." ) );
    CHECK( screen->message() == QStringLiteral( "Opening map..." ) );
    delete screen;
    Mason_Destroy( mason );
}

TEST_CASE( "Mason keeps the full palette reachable on a laptop-sized window", "[mason][smoke]" )
{
    mason_session_t session;
    auto *pWindow = Mason_Window( session.pMason );
    pWindow->resize( 1280, 720 );
    QCoreApplication::processEvents();
    CHECK( pWindow->width() <= 1280 );
    CHECK( pWindow->height() <= 720 );
    auto *pTools = pWindow->findChild<QToolBar *>( QStringLiteral( "EditorToolStrip" ) );
    REQUIRE( pTools != nullptr );
    auto *pPalette = pTools->findChild<QWidget *>( QStringLiteral( "EditorToolPalette" ) );
    REQUIRE( pPalette != nullptr );
    auto *pScroll = pTools->findChild<QScrollArea *>( QStringLiteral( "EditorToolPaletteScroll" ) );
    REQUIRE( pScroll != nullptr );
    for ( auto *pButton : pPalette->findChildren<QToolButton *>() ) {
        INFO( pButton->defaultAction()->objectName().toStdString() );
        pScroll->ensureWidgetVisible( pButton );
        QCoreApplication::processEvents();
        CHECK( pButton->isVisibleTo( pWindow ) );
        const QRect inViewport( pButton->mapTo( pScroll->viewport(), QPoint() ), pButton->size() );
        CHECK( pScroll->viewport()->rect().contains( inViewport ) );
    }
}

TEST_CASE( "Mason left-click menus expose the same actions as their toolbar icons", "[mason][smoke][menus]" )
{
    mason_session_t session;
    auto *pWindow = Mason_Window( session.pMason );
    auto *pBar = pWindow->menuBar();
    const auto menu = [pBar]( const char *path ) {
        QMenu *pMenu = pBar->findChild<QMenu *>( QStringLiteral( "menu:" ) + QString::fromLatin1( path ) );
        REQUIRE( pMenu != nullptr );
        return pMenu;
    };
    for ( const char *root : { "&Selection", "&Map", "&Mesh", "&Texture" } ) {
        for ( auto *action : menu( root )->actions() ) { CHECK( action->menu() == nullptr ); }
    }
    CHECK( menu( "&Selection" )->actions().contains( pWindow->findChild<QAction *>( "map.select_mode.objects" ) ) );
    CHECK( menu( "&Mesh" )->actions().contains( pWindow->findChild<QAction *>( "map.mesh.boolean_union" ) ) );
    // Actual left-button press on File opens the in-window menu.
    QMenu *pFile = menu( "&File" );
    const QPointF point = pBar->actionGeometry( pFile->menuAction() ).center();
    QMouseEvent press( QEvent::MouseButtonPress, point, pBar->mapToGlobal( point.toPoint() ),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( pBar, &press );
    CHECK( pFile->isVisible() );
    pFile->hide();
    QMouseEvent release( QEvent::MouseButtonRelease, point, pBar->mapToGlobal( point.toPoint() ),
                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( pBar, &release );

    struct group_t { const char *button; const char *menu; const char *command; };
    const group_t groups[]{
        { "masonSelectionOptions", "menu:&Selection", "edit.invert_selection" },
        { "masonEditingOptions", "masonEditingMenu", "map.tool.translate" },
        { "masonMeshOptions", "menu:&Mesh", "map.mesh.extrude" },
        { "masonBooleanOptions", "masonBooleanMenu", "map.mesh.boolean_union" },
        { "masonSurfaceOptions", "menu:&Texture", "assets.browse_materials" },
    };
    for ( const auto &group : groups ) {
        CAPTURE( group.button );
        auto *pButton = pWindow->findChild<QToolButton *>( QString::fromLatin1( group.button ) );
        REQUIRE( pButton != nullptr );
        REQUIRE( pButton->menu() == pWindow->findChild<QMenu *>( QString::fromLatin1( group.menu ) ) );
        CHECK( pButton->popupMode() == QToolButton::InstantPopup );
        CHECK( pButton->menu()->actions().contains( pWindow->findChild<QAction *>( QString::fromLatin1( group.command ) ) ) );
        bool opened = false;
        QTimer::singleShot( 0, pButton, [&opened, pButton]() {
            opened = pButton->menu()->isVisible();
            pButton->menu()->hide();
        } );
        pButton->click();
        CHECK( opened );
    }
    CHECK_FALSE( MapWorkspace_IsModified( Mason_MapWorkspace( session.pMason ) ) );
    const auto *pTools = pWindow->findChild<QToolBar *>( QStringLiteral( "EditorToolStrip" ) );
    REQUIRE( pTools != nullptr );
    CHECK( pTools->findChild<QWidget *>( QStringLiteral( "EditorToolPalette" ) )->findChildren<QToolButton *>().size() == 38 );
}

TEST_CASE( "Top-right utilities search assets and synchronize sound preference", "[mason][smoke][assets]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *window = Mason_Window( session.pMason );
    auto *gui = Mason_Gui( session.pMason );
    auto *search = window->findChild<QLineEdit *>( "MasonFastAssetSearch" );
    auto *assets = window->findChild<QWidget *>( "EditorAssetBrowser" );
    auto *utilities = window->findChild<QToolBar *>( "masonUtilityTools" );
    auto *sound = window->findChild<QAction *>( "view.sound_preview" );
    REQUIRE( ( search && assets && utilities && sound ) );
    auto *soundButton = utilities->findChild<QToolButton *>( "view.sound_preview.utility" );
    auto *settingsButton = utilities->findChild<QToolButton *>( "tools.settings.utility" );
    REQUIRE( ( soundButton && settingsButton ) );
    CHECK( soundButton->defaultAction() == sound );
    CHECK( settingsButton->defaultAction() == window->findChild<QAction *>( "tools.settings" ) );
    REQUIRE( session.Run( "assets.search" ) == command_result_t::OK );
    REQUIRE( search->hasFocus() );
    // Typing shows matches in the Asset Browser window; the keys stay here.
    QKeyEvent typing( QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral( "metal_panel" ) );
    QCoreApplication::sendEvent( search, &typing );
    auto *assetWindow = window->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) );
    REQUIRE( assetWindow != nullptr );
    CHECK( assetWindow->isVisible() );
    CHECK( search->hasFocus() );
    CHECK( EditorAssetWindow_Tab( assetWindow ) == ASSET_WINDOW_TAB_ALL );
    CHECK( EditorAssetWindow_Visible( assetWindow ).contains( QStringLiteral( "materials/blockout/metal_panel.cymat" ) ) );
    CHECK_FALSE( EditorAssetWindow_Visible( assetWindow ).contains( QStringLiteral( "materials/blockout/floor_tile.cymat" ) ) );
    search->setCursorPosition( 5 );
    QKeyEvent middleEdit( QEvent::KeyPress, Qt::Key_Underscore, Qt::NoModifier, QStringLiteral( "_" ) );
    QCoreApplication::sendEvent( search, &middleEdit );
    CHECK( search->cursorPosition() == 6 ); // The window's query must not move the caret.
    QKeyEvent backspace( QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier );
    QCoreApplication::sendEvent( search, &backspace );
    CHECK( search->text() == QStringLiteral( "metal_panel" ) );
    // Enter moves into the results with the first match selected.
    QKeyEvent enter( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    QCoreApplication::sendEvent( search, &enter );
    CHECK( EditorAssetWindow_Selected( assetWindow ) == QStringLiteral( "materials/blockout/metal_panel.cymat" ) );
    // Browse Materials picks into Active Material: Materials tab, filter cleared.
    REQUIRE( session.Run( "assets.browse_materials" ) == command_result_t::OK );
    CHECK( EditorAssetWindow_IsPicking( assetWindow ) );
    CHECK( EditorAssetWindow_Tab( assetWindow ) == EditorAssetWindow_KindTab( editor_asset_kind_t::MATERIAL ) );
    CHECK( EditorAssetWindow_Visible( assetWindow ).contains( QStringLiteral( "materials/blockout/floor_tile.cymat" ) ) );
    REQUIRE( EditorAssetWindow_Select( assetWindow, QStringLiteral( "materials/blockout/metal_panel.cymat" ) ) );
    REQUIRE( EditorAssetWindow_Accept( assetWindow ) );
    const string_view_t active = EditorSettings_Text( &gui->settings, "editor.map.default_material", string_view_t{} );
    CHECK( std::string( active.pData, active.cchLength ) == "materials/blockout/metal_panel.cymat" );
    CHECK_FALSE( assetWindow->isVisible() );
    CHECK( assets != nullptr ); // Hidden shared catalogue source remains available.
    CHECK( sound->isChecked() );
    sound->trigger();
    CHECK_FALSE( sound->isChecked() );
    CHECK_FALSE( EditorSettings_Bool( &gui->settings, "editor.audio.preview_enabled", CY_TRUE ) );
    const auto *descriptor = EditorSettings_Find( &gui->settings, StringView_FromCString( "editor.audio.preview_enabled" ) );
    REQUIRE( descriptor != nullptr );
    setting_value_t value{}; value.type = setting_type_t::BOOL; value.bValue = CY_TRUE;
    REQUIRE( EditorSettings_Write( &gui->settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    CHECK( sound->isChecked() );
    CHECK_FALSE( MapWorkspace_IsModified( Mason_MapWorkspace( session.pMason ) ) );
}

TEST_CASE( "The Database View opens assets and entities, from the command and from Object Properties", "[mason][smoke][database]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *window = Mason_Window( session.pMason );
    REQUIRE( session.Run( QStringLiteral( "assets.database materials/blockout/floor_tile.cymat" ) ) == command_result_t::OK );
    auto *pView = window->findChild<QDialog *>( QStringLiteral( "EditorDatabaseView" ) );
    REQUIRE( pView != nullptr );
    CHECK( pView->isVisible() );
    CHECK( EditorDatabaseView_Tab( pView ) == DATABASE_TAB_MATERIALS );
    CHECK( EditorDatabaseView_Current( pView ) == QStringLiteral( "materials/blockout/floor_tile.cymat" ) );
    const QStringList rows = EditorDatabaseView_Properties( pView );
    CHECK( rows.contains( QStringLiteral( "Material Settings/Shader=shaders/tile_surface.cyshader" ) ) );
    CHECK( EditorDatabaseView_Library( pView ).size() >= 77 );
    // The shader's folder opens the shader with its stages.
    REQUIRE( EditorDatabaseView_OpenRow( pView, QStringLiteral( "Material Settings/Shader" ) ) );
    CHECK( EditorDatabaseView_Tab( pView ) == DATABASE_TAB_SHADERS );
    CHECK( EditorDatabaseView_CodeEditorCount( pView ) == 3 );
    CHECK( EditorDatabaseView_UsedBy( pView ).contains( QStringLiteral( "materials/blockout/floor_tile.cymat" ) ) );
    CHECK( session.Run( QStringLiteral( "assets.database materials/not/there.cymat" ) ) == command_result_t::INVALID_ARGUMENTS );

    // An entity's Class row carries the folder: it opens the entity's keys.
    REQUIRE( session.Run( QStringLiteral( "map.go_to spawn_a" ) ) == command_result_t::OK );
    QCoreApplication::processEvents();
    QToolButton *pOpen = nullptr;
    for ( QToolButton *pButton : window->findChildren<QToolButton *>( QStringLiteral( "MapPropertiesOpen" ) ) ) {
        if ( pButton->property( "databaseTarget" ).toString().startsWith( QLatin1Char( '#' ) ) ) { pOpen = pButton; }
    }
    REQUIRE( pOpen != nullptr );
    pOpen->click();
    CHECK( EditorDatabaseView_Tab( pView ) == DATABASE_TAB_ENTITIES );
    CHECK( EditorDatabaseView_Current( pView ) == pOpen->property( "databaseTarget" ).toString() );
    bool bOrigin = false;
    for ( const QString &row : EditorDatabaseView_Properties( pView ) ) { bOrigin = bOrigin || row.startsWith( QStringLiteral( "Entity/origin=" ) ); }
    CHECK( bOrigin );
    pView->hide();
}

TEST_CASE( "Fast asset typing keeps focus while the Asset Browser window shows matches", "[mason][smoke][assets]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason );
    auto *search = window->findChild<QLineEdit *>( "MasonFastAssetSearch" );
    REQUIRE( search != nullptr );
    for ( int pass = 0; pass < 2; ++pass ) {
        window->activateWindow();
        REQUIRE( session.Run( "assets.search" ) == command_result_t::OK );
        QCoreApplication::processEvents();
        REQUIRE( search->hasFocus() );
        QKeyEvent typing( QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral( "metal" ) );
        QCoreApplication::sendEvent( search, &typing );
        QCoreApplication::processEvents();
        CHECK( search->hasFocus() );
        CHECK( QApplication::activeWindow() == window );
        auto *assetWindow = window->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) );
        REQUIRE( assetWindow != nullptr );
        CHECK( assetWindow->isVisible() );
        if ( pass == 0 ) { assetWindow->hide(); } // Closed: typing brings it back.
        search->clear();
    }
    // The docked panel is not opened by searching.
    auto *manager = window->findChild<ads::CDockManager *>();
    REQUIRE( manager != nullptr );
    auto *dock = manager->findDockWidget( QStringLiteral( "assets" ) );
    CHECK( ( dock == nullptr || dock->isClosed() ) );
}

TEST_CASE( "Mason central panes host the real asset and database editors and retain shader drafts", "[mason][smoke][view-content]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *window = Mason_Window( session.pMason );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::FOUR );
    auto *camera = MapViews_PaneView( views, 0 );
    const auto cameraPosition = MapCameraView_Position( camera );
    // Drive the same direct choices offered by each pane's title menu.
    for ( int i = 0; i < 3; ++i ) {
        auto *choice = views->findChild<QAction *>( QStringLiteral( "EditorViewContent%1_%2" ).arg( i ).arg( i + 4 ) );
        REQUIRE( choice != nullptr );
        REQUIRE( choice->isEnabled() );
        choice->trigger();
    }
    QCoreApplication::processEvents();
    auto *assets = MapViews_PaneView( views, 0 );
    auto *database = MapViews_PaneView( views, 1 );
    auto *shaders = MapViews_PaneView( views, 2 );
    for ( QWidget *content : { assets, database, shaders } ) {
        CHECK_FALSE( content->isWindow() );
        CHECK( content->isVisibleTo( window ) );
        auto *header = content->parentWidget()->findChild<QWidget *>( QStringLiteral( "EditorViewHeader" ) );
        REQUIRE( header != nullptr );
        CHECK( content->geometry().top() >= header->geometry().bottom() );
    }
    EditorAssetWindow_SetFilter( assets, QStringLiteral( "floor_tile" ) );
    CHECK_FALSE( EditorAssetWindow_Visible( assets ).isEmpty() );
    REQUIRE( EditorAssetWindow_Select( assets, QStringLiteral( "materials/blockout/floor_tile.cymat" ) ) );
    REQUIRE( EditorAssetWindow_Accept( assets ) );
    CHECK( StringView_Equals( EditorSettings_Text( &Mason_Gui( session.pMason )->settings, "editor.map.default_material", {} ),
           StringView_FromCString( "materials/blockout/floor_tile.cymat" ) ) );
    REQUIRE( EditorDatabaseView_OpenEntity( database, 110u ) );
    CHECK( EditorDatabaseView_Tab( database ) == DATABASE_TAB_ENTITIES );
    CHECK( EditorDatabaseView_Current( database ) == QStringLiteral( "#110" ) );
    REQUIRE( EditorDatabaseView_Open( shaders, QStringLiteral( "shaders/tile_surface.cyshader" ) ) );
    REQUIRE( EditorDatabaseView_CodeEditorCount( shaders ) == 3 );
    auto *source = EditorDatabaseView_CodeEditor( shaders, 1 );
    REQUIRE( source != nullptr );
    auto *text = EditorCodeEditor_Text( source );
    REQUIRE( text != nullptr );
    MapViews_SetActivePane( views, 2 );
    text->setFocus();
    text->moveCursor( QTextCursor::End );
    text->insertPlainText( QStringLiteral( "\n// retained pane draft" ) );
    const QString draft = text->toPlainText();
    REQUIRE( EditorDatabaseView_IsModified( shaders ) );
    // A map transaction refreshes every library, but must not reload stage drafts.
    MapWorkspace_Select( Mason_MapWorkspace( session.pMason ), 111u, MAP_SELECT_REPLACE );
    REQUIRE( session.Run( QStringLiteral( "edit.duplicate" ) ) == command_result_t::OK );
    CHECK( text->toPlainText() == draft );
    CHECK( EditorDatabaseView_IsModified( shaders ) );
    // Root changes and Rescan (including the embedded browser's own button)
    // refresh all cached libraries against the same catalogue.
    QTemporaryDir contentRoot;
    REQUIRE( contentRoot.isValid() );
    REQUIRE( QDir().mkpath( contentRoot.filePath( QStringLiteral( "materials/workspace" ) ) ) );
    auto *catalogue = window->findChild<QWidget *>( QStringLiteral( "EditorAssetBrowser" ) );
    REQUIRE( catalogue != nullptr );
    QStringList roots = EditorAssetBrowser_Roots( catalogue );
    REQUIRE_FALSE( roots.isEmpty() );
    const QString materialSource = QDir( roots.front() ).filePath( QStringLiteral( "materials/blockout/floor_tile.cymat" ) );
    REQUIRE( QFile::copy( materialSource, contentRoot.filePath( QStringLiteral( "materials/workspace/added.cymat" ) ) ) );
    roots.prepend( contentRoot.path() );
    const auto *rootsSetting = EditorSettings_Find( &Mason_Gui( session.pMason )->settings, StringView_FromCString( "editor.assets.roots" ) );
    REQUIRE( rootsSetting != nullptr );
    const QByteArray rootsText = roots.join( QLatin1Char( ';' ) ).toUtf8();
    setting_value_t rootsValue{}; rootsValue.type = setting_type_t::STRING;
    rootsValue.text = { rootsText.constData(), static_cast<usize>( rootsText.size() ) };
    REQUIRE( EditorSettings_Write( &Mason_Gui( session.pMason )->settings, settings_scope_t::USER, *rootsSetting, rootsValue ) == settings_registry_status_t::OK );
    EditorAssetWindow_SetFilter( assets, QStringLiteral( "workspace/" ) );
    EditorDatabaseView_SetTab( database, DATABASE_TAB_MATERIALS );
    CHECK( EditorAssetWindow_Visible( assets ).contains( QStringLiteral( "materials/workspace/added.cymat" ) ) );
    CHECK( EditorDatabaseView_Library( database ).contains( QStringLiteral( "materials/workspace/added.cymat" ) ) );
    REQUIRE( QFile::copy( materialSource, contentRoot.filePath( QStringLiteral( "materials/workspace/rescanned.cymat" ) ) ) );
    auto *rescan = assets->findChild<QToolButton *>( QStringLiteral( "AssetWindowRescan" ) );
    REQUIRE( rescan != nullptr );
    rescan->click();
    CHECK( EditorAssetWindow_Visible( assets ).contains( QStringLiteral( "materials/workspace/rescanned.cymat" ) ) );
    CHECK( EditorDatabaseView_Library( database ).contains( QStringLiteral( "materials/workspace/rescanned.cymat" ) ) );
    REQUIRE( session.Run( QStringLiteral( "assets.refresh" ) ) == command_result_t::OK );
    CHECK( text->toPlainText() == draft );
    CHECK( EditorDatabaseView_IsModified( shaders ) );
    const QByteArray saved = MapViews_SavePresentation( views );
    for ( int i = 0; i < 3; ++i ) { MapViews_SetPaneType( views, i, map_view_type_t::CAMERA ); }
    CHECK( MapViews_PaneView( views, 0 ) == camera );
    CHECK( MapCameraView_Position( camera ).x == cameraPosition.x );
    CHECK( MapCameraView_Position( camera ).y == cameraPosition.y );
    CHECK( MapCameraView_Position( camera ).z == cameraPosition.z );
    REQUIRE( MapViews_RestorePresentation( views, saved ) );
    CHECK( MapViews_PaneView( views, 0 ) == assets );
    CHECK( MapViews_PaneView( views, 1 ) == database );
    CHECK( MapViews_PaneView( views, 2 ) == shaders );
    CHECK( text->toPlainText() == draft );
    CHECK( EditorDatabaseView_IsModified( shaders ) );
    CHECK_FALSE( EditorDatabaseView_Open( shaders, QStringLiteral( "materials/blockout/floor_tile.cymat" ) ) );
    // Reset changes presentation and closes Console without destroying cached work.
    REQUIRE( session.Run( QStringLiteral( "view.layout.reset" ) ) == command_result_t::OK );
    MapViews_SetPaneType( views, 0, map_view_type_t::ASSETS );
    MapViews_SetPaneType( views, 2, map_view_type_t::SHADERS );
    CHECK( MapViews_PaneView( views, 0 ) == assets );
    CHECK( MapViews_PaneView( views, 2 ) == shaders );
    CHECK( text->toPlainText() == draft );
    auto *manager = window->findChild<ads::CDockManager *>();
    REQUIRE( manager != nullptr );
    CHECK( manager->findDockWidget( QStringLiteral( "assets" ) ) == nullptr );
    CHECK( manager->findDockWidget( QStringLiteral( "console" ) )->isClosed() );
}

TEST_CASE( "Every cached Mason pane remains subscribed alongside tools and Settings", "[mason][smoke][view-content][settings]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason );
    auto *gui = Mason_Gui( session.pMason );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    const usize errors = EditorLog_Count( &gui->log, log_level_t::Error );
    MapViews_SetArrangement( views, map_view_arrangement_t::FOUR );
    for ( int pane = 0; pane < MAP_VIEW_PANE_COUNT; ++pane ) {
        for ( int type = 0; type < static_cast<int>( map_view_type_t::COUNT ); ++type ) {
            MapViews_SetPaneType( views, pane, static_cast<map_view_type_t>( type ) );
            REQUIRE( MapViews_PaneType( views, pane ) == static_cast<map_view_type_t>( type ) );
        }
        MapViews_SetPaneType( views, pane, map_view_type_t::CAMERA );
    }
    REQUIRE( session.Run( QStringLiteral( "tools.settings" ) ) == command_result_t::OK );
    auto *dialog = window->findChild<QDialog *>( QStringLiteral( "EditorSettingsDialog" ) );
    REQUIRE( dialog != nullptr );
    EditorSettingsDialog_ShowPage( dialog, QString::fromLatin1( EDITOR_APPEARANCE_PAGE ) );
    CHECK( gui->settings.nListeners > 32u ); // Previously overflowed and left controls unsubscribed.
    CHECK( gui->settings.nListeners < EDITOR_SETTINGS_MAX_LISTENERS );
    CHECK( EditorLog_Count( &gui->log, log_level_t::Error ) == errors );
    EditorSettingsDialog_ShowPage( dialog, QStringLiteral( "Viewports/Grid and Snapping" ) );
    auto *grid = qobject_cast<QSpinBox *>( EditorSettingsDialog_EditorFor( dialog, QStringLiteral( "editor.grid.size" ) ) );
    REQUIRE( grid != nullptr );
    grid->setValue( 32 );
    CHECK( Mason_MapWorkspace( session.pMason )->gridSize == 32.0 );
    dialog->hide();
    for ( const char *tool : { "map.tool.block", "map.tool.select", "map.tool.block", "map.tool.select" } ) {
        REQUIRE( session.Run( QString::fromLatin1( tool ) ) == command_result_t::OK );
        CHECK( gui->settings.nListeners < EDITOR_SETTINGS_MAX_LISTENERS );
        CHECK( EditorLog_Count( &gui->log, log_level_t::Error ) == errors );
    }
}

TEST_CASE( "Capture Mason inspector tabs and embedded central editor panels", "[.workspace-panels-screenshot]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *window = Mason_Window( session.pMason );
    window->resize( 2100, 1400 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapWorkspace_Select( Mason_MapWorkspace( session.pMason ), 111u, MAP_SELECT_REPLACE );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::FULLBRIGHT );
    MapWorkspace_Frame( Mason_MapWorkspace( session.pMason ), CY_FALSE );
    QCoreApplication::processEvents();
    REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_workspace_panels_default.png" ) ) );
    MapViews_SetPaneType( views, 2, map_view_type_t::ASSETS );
    auto *assets = MapViews_PaneView( views, 2 );
    EditorAssetWindow_SetTab( assets, 1 );
    EditorAssetWindow_LoadThumbnails( assets );
    QCoreApplication::processEvents();
    REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_workspace_panels_assets.png" ) ) );
    MapViews_SetArrangement( views, map_view_arrangement_t::TWO );
    MapViews_SetPaneType( views, 1, map_view_type_t::SHADERS );
    REQUIRE( EditorDatabaseView_Open( MapViews_PaneView( views, 1 ), QStringLiteral( "shaders/tile_surface.cyshader" ) ) );
    QCoreApplication::processEvents();
    REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_workspace_panels_shaders.png" ) ) );
    REQUIRE( session.Run( QStringLiteral( "view.console" ) ) == command_result_t::OK );
    QCoreApplication::processEvents();
    REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_workspace_panels_console.png" ) ) );
}

TEST_CASE( "Distinct snapping controls share preferences and preserve custom steps", "[mason][smoke][snapping]" )
{
    mason_session_t session;
    auto *pWindow = Mason_Window( session.pMason );
    auto *pMap = Mason_MapWorkspace( session.pMason );
    const auto testSnap = [&]( const char *command, const char *menuName, double step, bool angle ) {
        if ( angle ) { MapWorkspace_SetAngleSnap( pMap, step ); }
        else { MapWorkspace_SetScaleSnap( pMap, step ); }
        auto *pAction = pWindow->findChild<QAction *>( QString::fromLatin1( command ) );
        auto *pStatus = pWindow->findChild<QToolButton *>( QString::fromLatin1( command ) + QStringLiteral( ".status" ) );
        auto *pMenu = pWindow->findChild<QMenu *>( QString::fromLatin1( menuName ) );
        REQUIRE( pAction != nullptr );
        REQUIRE( pStatus != nullptr );
        REQUIRE( pMenu != nullptr );
        CHECK( pAction->isChecked() );
        CHECK( pStatus->defaultAction() == pAction );
        CHECK( pStatus->menu() == pMenu );
        CHECK( pStatus->popupMode() == QToolButton::MenuButtonPopup );
        pAction->trigger();
        CHECK_FALSE( pAction->isChecked() );
        CHECK( ( angle ? pMap->angleSnap : pMap->scaleSnap ) == 0.0 );
        pStatus->click();
        CHECK( pAction->isChecked() );
        CHECK( ( angle ? pMap->angleSnap : pMap->scaleSnap ) == step );
        // Choosing a menu preset updates the combo and toggle as well.
        for ( auto *pStep : pMenu->actions() ) {
            if ( pStep->isCheckable() && pStep->data().toDouble() == 0.0 ) { pStep->trigger(); break; }
        }
        CHECK_FALSE( pStatus->isChecked() );
        auto *pCombo = pWindow->findChild<QComboBox *>( angle ? QStringLiteral( "masonAngleSnap" ) : QStringLiteral( "masonScaleSnap" ) );
        REQUIRE( pCombo != nullptr );
        CHECK( pCombo->currentData().toDouble() == 0.0 );
    };
    testSnap( "map.grid.angle_snap", "masonAngleSnapMenu", 7.5, true );
    testSnap( "map.grid.scale_snap", "masonScaleSnapMenu", 0.125, false );
    CHECK( pMap->bSnapToGrid );
    CHECK_FALSE( MapWorkspace_IsModified( pMap ) );
}

TEST_CASE( "Mason opens, selects, and saves through its commands", "[mason][smoke]" )
{
    mason_session_t session;
    REQUIRE( session.Run( QStringLiteral( "file.open \"%1\"" ).arg( ExampleRoot() ) ) == command_result_t::OK );
    QMainWindow *pWindow = Mason_Window( session.pMason );
    CHECK( pWindow->windowTitle().startsWith( QStringLiteral( "facility.cymap" ) ) );

    CHECK( session.Run( QStringLiteral( "edit.select_all" ) ) == command_result_t::OK );
    const map::map_workspace_t *pMap = Mason_MapWorkspace( session.pMason );
    CHECK( EditorSelection_Count( &pMap->selection ) >= pMap->wire.objects.nCount );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "selected" ) ) );
    CHECK( pWindow->findChild<QAction *>( QStringLiteral( "map.view.center_selection_2d" ) )->isEnabled() );
    CHECK( session.Run( QStringLiteral( "edit.select_none" ) ) == command_result_t::OK );
    CHECK( EditorSelection_Count( &pMap->selection ) == 0u );

    CHECK( session.Run( QStringLiteral( "map.grid.larger" ) ) == command_result_t::OK );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "Grid 32" ) ) );

    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "cypher_mason_smoke";
    std::filesystem::remove_all( directory );
    std::filesystem::create_directories( directory );
    const QString target = QString::fromStdString( ( directory / "saved" ).string() ); // Suffix added by Mason.
    REQUIRE( session.Run( QStringLiteral( "file.save_as \"%1\"" ).arg( target ) ) == command_result_t::OK );
    CHECK( std::filesystem::exists( directory / "saved.cymap" ) );
    CHECK( pWindow->windowTitle().startsWith( QStringLiteral( "saved.cymap" ) ) );
    CHECK( session.Run( QStringLiteral( "file.save" ) ) == command_result_t::OK );
    std::filesystem::remove_all( directory );

    // A failed open keeps the current map and says why.
    CHECK( session.Run( QStringLiteral( "file.open /nonexistent/missing.cymap" ) ) == command_result_t::FAILED );
    CHECK( pWindow->windowTitle().startsWith( QStringLiteral( "saved.cymap" ) ) );
}

TEST_CASE( "The command palette opens from its shortcut command and runs commands", "[mason][smoke]" )
{
    mason_session_t session;
    QMainWindow *pWindow = Mason_Window( session.pMason );
    const QAction *pPaletteAction = pWindow->findChild<QAction *>( QStringLiteral( "view.command_palette" ) );
    REQUIRE( pPaletteAction != nullptr );
    CHECK( pPaletteAction->shortcut() == QKeySequence( Qt::CTRL | Qt::SHIFT | Qt::Key_P ) );
    REQUIRE( session.Run( QStringLiteral( "view.command_palette" ) ) == command_result_t::OK );
    QWidget *pPalette = pWindow->findChild<QWidget *>( QStringLiteral( "EditorCommandPalette" ) );
    REQUIRE( pPalette != nullptr );
    CHECK( pPalette->isVisible() );
    EditorCommandPalette_SetQuery( pPalette, QStringLiteral( "larger grid" ) );
    REQUIRE( EditorCommandPalette_Accept( pPalette ) );
    CHECK( Mason_MapWorkspace( session.pMason )->gridSize == 32.0 );
}

TEST_CASE( "The settings dialog edits the user scope and the map follows", "[mason][smoke]" )
{
    mason_session_t session;
    QMainWindow *pWindow = Mason_Window( session.pMason );
    const QAction *pSettings = pWindow->findChild<QAction *>( QStringLiteral( "tools.settings" ) );
    REQUIRE( pSettings != nullptr );
    CHECK( pSettings->shortcut() == QKeySequence( Qt::CTRL | Qt::Key_Comma ) );
    REQUIRE( session.Run( QStringLiteral( "tools.settings" ) ) == command_result_t::OK );
    QDialog *pDialog = pWindow->findChild<QDialog *>( QStringLiteral( "EditorSettingsDialog" ) );
    REQUIRE( pDialog != nullptr );
    CHECK( pDialog->isVisible() );
    EditorSettingsDialog_ShowPage( pDialog, QStringLiteral( "Viewports/Grid and Snapping" ) );
    auto *pSize = qobject_cast<QSpinBox *>( EditorSettingsDialog_EditorFor( pDialog, QStringLiteral( "editor.grid.size" ) ) );
    REQUIRE( pSize != nullptr );
    pSize->setValue( 256 );
    CHECK( Mason_MapWorkspace( session.pMason )->gridSize == 256.0 );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "Grid 256" ) ) );
    CHECK( EditorSettingsDialog_SourceFor( pDialog, QStringLiteral( "editor.grid.size" ) ) == QStringLiteral( "User" ) );
    pSize->setValue( 10 );
    auto *gridChoice = pWindow->findChild<QComboBox *>( QStringLiteral( "masonGridSize" ) );
    REQUIRE( gridChoice != nullptr );
    CHECK( gridChoice->currentData().toDouble() == 10.0 );
    CHECK( gridChoice->currentText() == QStringLiteral( "10 u" ) );
    // The map workspace's own page is there too.
    EditorSettingsDialog_ShowPage( pDialog, QStringLiteral( "Map Editing" ) );
    usize mapSettings{};
    ( void )MapWorkspace_SettingsCatalogue( &mapSettings );
    CHECK( EditorSettingsDialog_VisibleSettings( pDialog ).size() == static_cast<qsizetype>( mapSettings ) );
}

TEST_CASE( "The theme editor previews through Mason and the theme setting follows", "[mason][smoke]" )
{
    mason_session_t session;
    editor_gui_t *pGui = Mason_Gui( session.pMason );
    REQUIRE( session.Run( QStringLiteral( "tools.theme_editor" ) ) == command_result_t::OK );
    QDialog *pEditor = Mason_Window( session.pMason )->findChild<QDialog *>( QStringLiteral( "EditorThemeEditor" ) );
    REQUIRE( pEditor != nullptr );
    CHECK( pEditor->isVisible() );

    EditorThemeEditor_SetColor( pEditor, QStringLiteral( "ui.accent" ), QColor( 0x40, 0xC0, 0x80 ) );
    CHECK( pGui->style.colors[STYLE_COLOR_ACCENT] == 0x40C080FFu );
    auto *pKeys = Mason_Window( session.pMason )->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) );
    REQUIRE( pKeys != nullptr );
    REQUIRE( pKeys->topLevelItemCount() > 0 );
    CHECK( pKeys->topLevelItem( 0 )->foreground( 0 ).color() == QColor( 0x40, 0xC0, 0x80 ) );
    REQUIRE_FALSE( EditorThemeEditor_Save( pEditor, QStringLiteral( "mason_test" ), QStringLiteral( "Mason Test" ) ).isEmpty() );
    const auto activeTheme = [pGui]() {
        const string_view_t id = EditorGui_ActiveThemeId( pGui );
        return std::string( id.pData, id.cchLength );
    };
    CHECK( activeTheme() == "mason_test" );
    const string_view_t setting = EditorSettings_Text( &pGui->settings, "editor.ui.theme", string_view_t{} );
    CHECK( std::string( setting.pData, setting.cchLength ) == "mason_test" );

    // Choosing a theme in settings selects it; an unknown one keeps the current.
    const setting_descriptor_t *pTheme = EditorSettings_Find( &pGui->settings, StringView_FromCString( "editor.ui.theme" ) );
    REQUIRE( pTheme != nullptr );
    setting_value_t value{};
    value.type = setting_type_t::STRING;
    value.text = StringView_FromCString( "not_installed" );
    REQUIRE( EditorSettings_Write( &pGui->settings, settings_scope_t::USER, *pTheme, value ) == settings_registry_status_t::OK );
    CHECK( activeTheme() == "mason_test" );
    value.text = StringView_FromCString( "charcoal" );
    REQUIRE( EditorSettings_Write( &pGui->settings, settings_scope_t::USER, *pTheme, value ) == settings_registry_status_t::OK );
    CHECK( activeTheme() == "charcoal" );
    CHECK( pGui->style.colors[STYLE_COLOR_ACCENT] == 0xE59A2FFFu );
    pEditor->reject();
    CHECK( activeTheme() == "charcoal" );
}

TEST_CASE( "Panel toggles and layout reset keep docks and actions in step", "[mason][smoke]" )
{
    mason_session_t session;
    QMainWindow *pWindow = Mason_Window( session.pMason );
    const QAction *pConsole = pWindow->findChild<QAction *>( QStringLiteral( "view.console" ) );
    REQUIRE( pConsole != nullptr );
    CHECK_FALSE( pConsole->isChecked() );
    CHECK( session.Run( QStringLiteral( "view.console" ) ) == command_result_t::OK );
    QCoreApplication::processEvents();
    CHECK( pConsole->isChecked() );
    CHECK( session.Run( QStringLiteral( "view.layout.reset" ) ) == command_result_t::OK );
    QCoreApplication::processEvents();
    CHECK_FALSE( pConsole->isChecked() );
}

TEST_CASE( "Status switches reopen closed panels and inspection never hides properties", "[mason][smoke][panels]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason );
    auto *switches = window->findChild<QWidget *>( QStringLiteral( "MasonPanelSwitches" ) );
    REQUIRE( switches != nullptr );
    for ( const char *id : { "view.tool_properties", "view.active_material", "view.outliner", "view.properties", "view.history" } ) {
        const QString name = QString::fromLatin1( id );
        auto *action = window->findChild<QAction *>( name );
        auto *button = switches->findChild<QToolButton *>( name + QStringLiteral( ".status" ) );
        REQUIRE( action != nullptr );
        REQUIRE( button != nullptr );
        CHECK( button->defaultAction() == action );
        CHECK( button->isCheckable() );
        const bool before = action->isChecked();
        button->click();
        QCoreApplication::processEvents();
        CHECK( button->isChecked() == !before );
        button->click();
        QCoreApplication::processEvents();
        CHECK( button->isChecked() == before );
    }
    auto *assets = switches->findChild<QToolButton *>( QStringLiteral( "view.assets.status" ) );
    REQUIRE( assets != nullptr );
    CHECK_FALSE( assets->isCheckable() );
    assets->click();
    auto *assetWindow = window->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) );
    REQUIRE( assetWindow != nullptr );
    CHECK( assetWindow->isVisible() );
    assetWindow->hide();
    auto *properties = window->findChild<QAction *>( QStringLiteral( "view.properties" ) );
    REQUIRE( properties != nullptr );
    if ( properties->isChecked() ) { REQUIRE( session.Run( "view.properties" ) == command_result_t::OK ); }
    CHECK( session.Run( "view.properties.open" ) == command_result_t::OK );
    QCoreApplication::processEvents();
    CHECK( properties->isChecked() );
    CHECK( session.Run( "view.properties.open" ) == command_result_t::OK );
    CHECK( properties->isChecked() );

    auto *palette = window->findChild<QWidget *>( QStringLiteral( "EditorToolPalette" ) );
    REQUIRE( palette != nullptr );
    QStringList ids;
    for ( auto *button : palette->findChildren<QToolButton *>() ) { ids.append( button->defaultAction()->objectName() ); }
    CHECK_FALSE( ids.contains( QStringLiteral( "map.grid.snap" ) ) );
    CHECK( ids.contains( QStringLiteral( "view.properties.open" ) ) );
    CHECK( ids.contains( QStringLiteral( "map.hide.unselected" ) ) );
    window->resize( 1280, 720 );
    window->statusBar()->showMessage( QStringLiteral( "Opened facility.cymap" ), 8000 );
    QCoreApplication::processEvents();
    for ( auto *button : window->statusBar()->findChildren<QToolButton *>() ) {
        INFO( button->objectName().toStdString() );
        INFO( "position " << button->mapTo( window, QPoint() ).x() << ", " << button->mapTo( window, QPoint() ).y()
              << " size " << button->width() << " x " << button->height() << " window " << window->width() << " x " << window->height() );
        CHECK( button->isVisibleTo( window ) );
        CHECK( window->rect().contains( QRect( button->mapTo( window, QPoint() ), button->size() ) ) );
    }
}

TEST_CASE( "Center selection commands move only their requested view family", "[mason][smoke][views]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *workspace = Mason_MapWorkspace( session.pMason );
    auto *views = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    auto *camera = MapViews_PaneView( views, 0 );
    auto *top = MapViews_PaneView( views, 1 );
    REQUIRE( camera != nullptr );
    REQUIRE( top != nullptr );
    MapWorkspace_Select( workspace, 111u, MAP_SELECT_REPLACE );
    REQUIRE( EditorSelection_Count( &workspace->selection ) == 1u );
    const auto before = MapCameraView_Position( camera );
    const double zoom = MapOrthoView_Zoom( top );
    REQUIRE( session.Run( "map.view.center_selection_2d" ) == command_result_t::OK );
    const auto after2d = MapCameraView_Position( camera );
    CHECK( after2d.x == before.x );
    CHECK( after2d.y == before.y );
    CHECK( after2d.z == before.z );
    CHECK( MapOrthoView_Zoom( top ) != zoom );
    const QPointF origin = MapOrthoView_WorldToView( top, QPointF() );
    const double zoom2d = MapOrthoView_Zoom( top );
    REQUIRE( session.Run( "map.view.center_selection_3d" ) == command_result_t::OK );
    const auto after3d = MapCameraView_Position( camera );
    CHECK( ( after3d.x != before.x || after3d.y != before.y || after3d.z != before.z ) );
    CHECK( MapOrthoView_WorldToView( top, QPointF() ) == origin );
    CHECK( MapOrthoView_Zoom( top ) == zoom2d );
}

TEST_CASE( "The asset browser finds the project's assets and sets the active material", "[mason][smoke]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    QWidget *pAssets = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorAssetBrowser" ) );
    REQUIRE( pAssets != nullptr );
    // The example map sits inside the repository, whose assets folder is
    // the project's content.
    const QStringList roots = EditorAssetBrowser_Roots( pAssets );
    REQUIRE( roots.size() == 1 );
    CHECK( roots.front().endsWith( QStringLiteral( "/assets" ) ) );
    CHECK( EditorAssets_KindCount( EditorAssetBrowser_Catalog( pAssets ), editor_asset_kind_t::MATERIAL ) >= 77u );
    // The example map names materials the repository does not ship.
    CHECK( EditorAssetBrowser_Missing( pAssets ).contains( QStringLiteral( "materials/sky/dusk.cymat" ) ) );

    // Double-clicking a material makes it the Active Material, which then
    // shows the material's own texture instead of the dev-grid stand-in.
    QWidget *pActive = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "MapActiveMaterial" ) );
    REQUIRE( pActive != nullptr );
    EditorAssetBrowser_Activate( pAssets, QStringLiteral( "materials/blockout/floor_tile.cymat" ) );
    const string_view_t active = EditorSettings_Text( &Mason_Gui( session.pMason )->settings, "editor.map.default_material", string_view_t{} );
    CHECK( std::string( active.pData, active.cchLength ) == "materials/blockout/floor_tile.cymat" );
    CHECK( map::MapActiveMaterial_HasImage( pActive ) );
    CHECK( session.Run( QStringLiteral( "assets.browse_materials" ) ) == command_result_t::OK );
    CHECK( session.Run( QStringLiteral( "assets.refresh" ) ) == command_result_t::OK );
}

TEST_CASE( "Capture viewport interaction and real asset inspectors", "[.interaction-screenshot]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *window = Mason_Window( session.pMason );
    auto *gui = Mason_Gui( session.pMason );
    REQUIRE( EditorGui_SelectTheme( gui, App(), StringView_FromCString( "slate" ) ) == editor_gui_status_t::OK );
    window->resize( 1920, 1200 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::FOUR );
    MapWorkspace_Select( Mason_MapWorkspace( session.pMason ), 111u, MAP_SELECT_REPLACE );
    QCoreApplication::processEvents();
    MapWorkspace_Frame( Mason_MapWorkspace( session.pMason ), false );
    const auto capture = []( QWidget *widget, const char *name ) {
        QCoreApplication::processEvents();
        CHECK( widget->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / name ).string() ) ) );
    };
    capture( window, "mason_interaction_views.png" );
    auto *assets = window->findChild<QWidget *>( QStringLiteral( "EditorAssetBrowser" ) );
    REQUIRE( assets != nullptr );
    auto *preview = EditorAssetBrowser_Preview( assets, QStringLiteral( "shaders/tile_surface.cyshader" ) );
    REQUIRE( preview != nullptr );
    capture( preview, "mason_interaction_shader.png" );
    preview->close();
    preview = EditorAssetBrowser_Preview( assets, QStringLiteral( "materials/blockout/floor_tile.cymat" ) );
    REQUIRE( preview != nullptr );
    capture( preview, "mason_interaction_material.png" );
    preview->close();
}

TEST_CASE( "Shift+G repeats the last repeatable command through Command History", "[mason][smoke][command_history]" )
{
    mason_session_t session;
    auto *pMap = Mason_MapWorkspace( session.pMason );
    REQUIRE( session.Run( QStringLiteral( "map.grid 16" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "map.grid.larger" ) ) == command_result_t::OK );
    CHECK( pMap->gridSize == 32.0 );
    REQUIRE( session.Run( QStringLiteral( "view.console" ) ) == command_result_t::OK ); // Looking around is not repeated.
    REQUIRE( session.Run( QStringLiteral( "edit.repeat_command" ) ) == command_result_t::OK );
    CHECK( pMap->gridSize == 64.0 );
    QWidget *pHistory = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorCommandHistory" ) );
    if ( pHistory == nullptr ) {
        // A closed dock is not parented to the window; open it to inspect.
        REQUIRE( session.Run( QStringLiteral( "view.command_history" ) ) == command_result_t::OK );
        QCoreApplication::processEvents();
        pHistory = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorCommandHistory" ) );
    }
    REQUIRE( pHistory != nullptr );
    const QStringList rows = EditorCommandHistory_Rows( pHistory );
    CHECK( rows.contains( QStringLiteral( "map.grid.larger" ) ) );
    CHECK_FALSE( rows.contains( QStringLiteral( "view.console" ) ) );
}

TEST_CASE( "Help lists every command with its keys, and the views' held keys and mouse", "[mason][smoke][shortcuts]" )
{
    mason_session_t session;
    REQUIRE( session.Run( QStringLiteral( "help.shortcuts" ) ) == command_result_t::OK );
    QDialog *pDialog = Mason_Window( session.pMason )->findChild<QDialog *>( QStringLiteral( "EditorShortcutsDialog" ) );
    REQUIRE( pDialog != nullptr );
    const QStringList rows = EditorShortcutsDialog_Rows( pDialog );
    CHECK( rows.contains( QStringLiteral( "Edit\tUndo\tCtrl+Z\tWindow" ) ) );
    CHECK( rows.contains( QStringLiteral( "Edit\tRepeat Command\tShift+G\tWindow" ) ) );
    CHECK( rows.contains( QStringLiteral( "Map Tools\tSelection Tool\tShift+S\tWindow" ) ) );
    // Held keys: the 3D view's fly controls.
    EditorShortcutsDialog_SetSearch( pDialog, QStringLiteral( "camera forward" ) );
    CHECK( EditorShortcutsDialog_Rows( pDialog ).contains( QStringLiteral( "Camera and Mouse\tCamera Forward\tW, Up\tmap.viewport.3d" ) ) );
    // Only bound commands, and the plain-text list for printing.
    EditorShortcutsDialog_SetSearch( pDialog, QString() );
    EditorShortcutsDialog_SetBoundOnly( pDialog, true );
    for ( const QString &row : EditorShortcutsDialog_Rows( pDialog ) ) { CHECK_FALSE( row.split( QLatin1Char( '\t' ) ).value( 2 ).isEmpty() ); }
    CHECK( EditorShortcutsDialog_Text( pDialog ).contains( QStringLiteral( "Ctrl+Z" ) ) );
    CHECK( EditorShortcutsDialog_GroupOf( QStringLiteral( "map.texture.fit" ) ) == QStringLiteral( "Texture" ) );
    pDialog->hide();
}

TEST_CASE( "Mason status dimensions follow the live construction and restore on cancellation", "[mason][smoke][geometry-edit][preview]" )
{
    mason_session_t session;
    auto *map = Mason_MapWorkspace( session.pMason ); REQUIRE( map != nullptr );
    map_bounds_t bounds{};
    MapBounds_AddPoint( bounds, { -64, -32, 0 } ); MapBounds_AddPoint( bounds, { 64, 32, 96 } );
    REQUIRE( MapWorkspace_CreateBox( map, bounds ) );
    const QString before = Mason_StatusText( session.pMason );
    CHECK( before.contains( QStringLiteral( "128w 64l 96h" ) ) );
    MapWorkspace_SetTool( map, map_tool_t::BLOCK );
    MapWorkspace_Select( map, 0, MAP_SELECT_REPLACE );
    const auto steps = EditorHistory_StepCount( &map->history );
    bounds.box.maximum.z = 160;
    MapWorkspace_SetEditPreview( map, bounds );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "Preview 128w 64l 160h" ) ) );
    bounds.box.maximum.x = bounds.box.minimum.x;
    MapWorkspace_SetEditPreview( map, bounds );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "Invalid preview 0w 64l 160h" ) ) );
    MapWorkspace_ClearEditPreview( map );
    CHECK_FALSE( Mason_StatusText( session.pMason ).contains( QStringLiteral( "Preview" ) ) );
    CHECK_FALSE( Mason_StatusText( session.pMason ).contains( QStringLiteral( "160h" ) ) );
    CHECK( EditorHistory_StepCount( &map->history ) == steps );
    REQUIRE( map->wire.objects.nCount == 1 );
    MapWorkspace_Select( map, map->wire.objects.pData[0].id, MAP_SELECT_REPLACE );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "128w 64l 96h" ) ) );
}

TEST_CASE( "Go To and Map Info run from the Map menu and the console", "[mason][smoke][dialogs]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *pMap = Mason_MapWorkspace( session.pMason );
    CHECK( session.Run( QStringLiteral( "map.go_to spawn_a" ) ) == command_result_t::OK );
    CHECK( EditorSelection_Count( &pMap->selection ) == 1u );
    // Hammer's status readout: a point entity shows where it stands...
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "@(" ) ) );
    // ...geometry its size and centre.
    CHECK( session.Run( QStringLiteral( "map.go_to 1000" ) ) == command_result_t::OK );
    const QRegularExpression size( QStringLiteral( "\\d+(\\.\\d+)?w \\d+(\\.\\d+)?l \\d+(\\.\\d+)?h @\\(" ) );
    CHECK( Mason_StatusText( session.pMason ).contains( size ) );
    CHECK( session.Run( QStringLiteral( "map.go_to 256 -128 64" ) ) == command_result_t::OK );
    CHECK( session.Run( QStringLiteral( "map.go_to nobody_here" ) ) == command_result_t::FAILED );
    REQUIRE( session.Run( QStringLiteral( "map.go_to" ) ) == command_result_t::OK );
    CHECK( Mason_Window( session.pMason )->findChild<QDialog *>( QStringLiteral( "MapGoToDialog" ) ) != nullptr );
    REQUIRE( session.Run( QStringLiteral( "map.info" ) ) == command_result_t::OK );
    QDialog *pInfo = Mason_Window( session.pMason )->findChild<QDialog *>( QStringLiteral( "MapInfoDialog" ) );
    REQUIRE( pInfo != nullptr );
    CHECK( pInfo->isVisible() );
}

TEST_CASE( "Hammer's view keys change the active pane's projection", "[mason][smoke][views]" )
{
    mason_session_t session;
    QWidget *pViews = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( pViews != nullptr );
    MapViews_SetActivePane( pViews, 1 );
    QCoreApplication::processEvents();
    CHECK( MapViews_ActivePane( pViews ) == 1 );
    REQUIRE( session.Run( QStringLiteral( "map.view.front" ) ) == command_result_t::OK ); // F3
    CHECK( MapViews_PaneType( pViews, 1 ) == map_view_type_t::FRONT );
    CHECK( MapViews_PaneType( pViews, 0 ) == map_view_type_t::CAMERA ); // Other panes keep theirs.
    REQUIRE( session.Run( QStringLiteral( "map.view.cycle_2d" ) ) == command_result_t::OK ); // Ctrl+Space
    CHECK( MapViews_PaneType( pViews, 1 ) == map_view_type_t::SIDE );
    REQUIRE( session.Run( QStringLiteral( "map.view.cycle_2d" ) ) == command_result_t::OK );
    CHECK( MapViews_PaneType( pViews, 1 ) == map_view_type_t::TOP );
    REQUIRE( session.Run( QStringLiteral( "map.view.maximize" ) ) == command_result_t::OK );
    CHECK( MapViews_MaximizedPane( pViews ) == 1 );
    REQUIRE( session.Run( QStringLiteral( "map.view.maximize" ) ) == command_result_t::OK );
    CHECK( MapViews_MaximizedPane( pViews ) == -1 );
    // A pane the layout does not show is never the active one.
    MapViews_SetArrangement( pViews, map_view_arrangement_t::PERSPECTIVE );
    CHECK( MapViews_ActivePane( pViews ) == 0 );
}

TEST_CASE( "The recent maps list is newest first, unique, bounded, and kept on disk", "[mason][recent]" )
{
    QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const QString listPath = folder.filePath( QStringLiteral( "recent_maps.txt" ) );
    const auto map = [&folder]( const char *pName ) {
        const QString path = folder.filePath( QString::fromLatin1( pName ) );
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        return path;
    };
    mason_recent_t recent;
    MasonRecent_Load( &recent, listPath, 3 );
    CHECK( recent.files.isEmpty() );
    const QString a = map( "a.cymap" ), b = map( "b.cymap" ), c = map( "c.cymap" ), d = map( "d.cymap" );
    CHECK( MasonRecent_Add( &recent, a ) );
    CHECK( MasonRecent_Add( &recent, b ) );
    CHECK( MasonRecent_Add( &recent, a ) ); // Again: moves to the front, once.
    REQUIRE( recent.files.size() == 2 );
    CHECK( QFileInfo( recent.files.front() ).fileName() == QStringLiteral( "a.cymap" ) );
    CHECK( MasonRecent_Add( &recent, c ) );
    CHECK( MasonRecent_Add( &recent, d ) ); // The oldest (b) drops off at three.
    CHECK( recent.files.size() == 3 );
    CHECK_FALSE( recent.files.contains( QFileInfo( b ).canonicalFilePath() ) );

    mason_recent_t reloaded;
    MasonRecent_Load( &reloaded, listPath, 3 );
    CHECK( reloaded.files == recent.files );
    // A damaged list never yields relative or duplicate entries.
    {
        QFile file( listPath );
        REQUIRE( file.open( QIODevice::Append | QIODevice::Text ) );
        file.write( "relative/path.cymap\n" );
    }
    MasonRecent_Load( &reloaded, listPath, 8 );
    CHECK( reloaded.files.size() == 3 );
    CHECK( MasonRecent_Clear( &reloaded ) );
    MasonRecent_Load( &reloaded, listPath, 8 );
    CHECK( reloaded.files.isEmpty() );
}

TEST_CASE( "Opened maps reach Open Recent and the welcome window", "[mason][smoke][recent]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    const QStringList recent = Mason_RecentMaps( session.pMason );
    REQUIRE_FALSE( recent.isEmpty() );
    CHECK( QFileInfo( recent.front() ).fileName() == QStringLiteral( "facility.cymap" ) );
    auto *pMenu = Mason_Window( session.pMason )->findChild<QMenu *>( QStringLiteral( "masonRecentMenu" ) );
    REQUIRE( pMenu != nullptr );
    QMetaObject::invokeMethod( pMenu, "aboutToShow" );
    CHECK( pMenu->actions().front()->text().contains( QStringLiteral( "facility" ) ) );
    // The welcome window lists it and reports the pick; Mason opens it.
    std::unique_ptr<QDialog> pWelcome( MasonWelcome_Create( nullptr, Mason_Gui( session.pMason )->style, recent, true ) );
    CHECK( MasonWelcome_Rows( pWelcome.get() ).front().startsWith( QStringLiteral( "facility\t" ) ) );
    MasonWelcome_Choose( pWelcome.get(), mason_welcome_choice_t::OPEN, 0 );
    CHECK( MasonWelcome_Choice( pWelcome.get() ) == mason_welcome_choice_t::OPEN );
    CHECK( MasonWelcome_ChosenFile( pWelcome.get() ) == recent.front() );
    CHECK_FALSE( Mason_ShowWelcome( session.pMason ) ); // Headless shows nothing.
    pWelcome->resize( 760, 420 );
    pWelcome->show();
    QCoreApplication::processEvents();
    CHECK( pWelcome->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_welcome.png" ).string() ) ) );
}

TEST_CASE( "Check for Problems runs from Alt+P and reports to the console", "[mason][smoke][check]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    CHECK( session.Run( QStringLiteral( "map.check" ) ) == command_result_t::OK );
    // The example map names assets the repository does not ship; the
    // asset browser's catalogue reports them.
    auto *pMap = Mason_MapWorkspace( session.pMason );
    QWidget *pAssets = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorAssetBrowser" ) );
    REQUIRE( pAssets != nullptr );
    std::unique_ptr<QDialog> pCheck( MapCheckDialog_Create( nullptr, pMap,
        []( void *pContext, const QString &path ) {
            const QByteArray utf8 = path.toUtf8();
            return EditorAssets_Find( EditorAssetBrowser_Catalog( static_cast<QWidget *>( pContext ) ),
                                      string_view_t{ utf8.constData(), static_cast<usize>( utf8.size() ) } ) != nullptr;
        },
        pAssets ) );
    CHECK( MapCheckDialog_Refresh( pCheck.get() ) > 0 );
    pCheck->show();
    QCoreApplication::processEvents();
    CHECK( pCheck->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_check.png" ).string() ) ) );
}

// Writes a picture of the window for eyeballing the layout:
// cypher_mason_smoke_tests "[.screenshot]"
TEST_CASE( "Screenshot of Mason with the example map", "[.screenshot]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    Mason_Window( session.pMason )->resize( 1600, 1000 );
    QCoreApplication::processEvents();
    // Thumbnails load as they scroll into view; a picture wants them all now.
    if ( QWidget *pAssets = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorAssetBrowser" ) ) ) {
        EditorAssetBrowser_SetTab( pAssets, EditorAssetBrowser_KindTab( editor_asset_kind_t::MATERIAL ) );
        EditorAssetBrowser_Activate( pAssets, QStringLiteral( "materials/blockout/metal_panel.cymat" ) );
        EditorAssetBrowser_LoadThumbnails( pAssets );
        QCoreApplication::processEvents();
    }
    const QString path = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason.png" ).string() );
    CHECK( Mason_Window( session.pMason )->grab().save( path ) );
    // The asset browser on Used in Map, with the map's missing assets.
    if ( QWidget *pAssets = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorAssetBrowser" ) ) ) {
        EditorAssetBrowser_SetTab( pAssets, ASSET_TAB_USED );
        EditorAssetBrowser_LoadThumbnails( pAssets );
        pAssets->resize( 900, 300 );
        QCoreApplication::processEvents();
        const QString usedPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_assets_used.png" ).string() );
        CHECK( pAssets->grab().save( usedPath ) );
        EditorAssetBrowser_SetTab( pAssets, EditorAssetBrowser_KindTab( editor_asset_kind_t::MATERIAL ) );
    }
    // The same window with the command palette open.
    REQUIRE( session.Run( QStringLiteral( "view.command_palette" ) ) == command_result_t::OK );
    QWidget *pPalette = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorCommandPalette" ) );
    REQUIRE( pPalette != nullptr );
    EditorCommandPalette_SetQuery( pPalette, QStringLiteral( "grid" ) );
    QCoreApplication::processEvents();
    const QString palettePath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_palette.png" ).string() );
    CHECK( Mason_Window( session.pMason )->grab().save( palettePath ) );
    // The settings dialog on the camera page.
    REQUIRE( session.Run( QStringLiteral( "tools.settings" ) ) == command_result_t::OK );
    QDialog *pSettings = Mason_Window( session.pMason )->findChild<QDialog *>( QStringLiteral( "EditorSettingsDialog" ) );
    REQUIRE( pSettings != nullptr );
    EditorSettingsDialog_ShowPage( pSettings, QStringLiteral( "Viewports/Camera" ) );
    QCoreApplication::processEvents();
    const QString settingsPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_settings.png" ).string() );
    CHECK( pSettings->grab().save( settingsPath ) );
    // The Appearance page, then the whole editor in the TileEditor's preset.
    EditorSettingsDialog_ShowPage( pSettings, QStringLiteral( "Appearance" ) );
    pSettings->resize( 1120, 900 );
    QCoreApplication::processEvents();
    const QString appearancePath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_appearance.png" ).string() );
    CHECK( pSettings->grab().save( appearancePath ) );
    QWidget *pAppearance = pSettings->findChild<QWidget *>( QStringLiteral( "EditorAppearancePage" ) );
    REQUIRE( pAppearance != nullptr );
    EditorAppearancePage_SelectTheme( pAppearance, QStringLiteral( "radiant_dark" ) );
    QCoreApplication::processEvents();
    const QString presetPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_radiant_dark.png" ).string() );
    CHECK( Mason_Window( session.pMason )->grab().save( presetPath ) );
    EditorAppearancePage_SelectTheme( pAppearance, QStringLiteral( "charcoal" ) );
    pSettings->hide();
    // The theme editor on a derived colour.
    REQUIRE( session.Run( QStringLiteral( "tools.theme_editor" ) ) == command_result_t::OK );
    QDialog *pTheme = Mason_Window( session.pMason )->findChild<QDialog *>( QStringLiteral( "EditorThemeEditor" ) );
    REQUIRE( pTheme != nullptr );
    QTreeWidget *pTokens = pTheme->findChild<QTreeWidget *>();
    REQUIRE( pTokens != nullptr );
    const QList<QTreeWidgetItem *> border = pTokens->findItems( QStringLiteral( "Control borders, scrollbar handles" ), Qt::MatchExactly | Qt::MatchRecursive );
    REQUIRE_FALSE( border.isEmpty() );
    pTokens->setCurrentItem( border.front() );
    pTokens->scrollToItem( border.front() );
    QCoreApplication::processEvents();
    const QString themePath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_theme.png" ).string() );
    CHECK( pTheme->grab().save( themePath ) );
    pTheme->reject();
    // Hammer's pop-up Asset Browser, as Active Material's Browse opens it.
    if ( session.Run( QStringLiteral( "assets.browse_materials" ) ) == command_result_t::OK ) {
        auto *pAssetWindow = Mason_Window( session.pMason )->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) );
        REQUIRE( pAssetWindow != nullptr );
        pAssetWindow->resize( 1180, 760 );
        EditorAssetWindow_LoadThumbnails( pAssetWindow );
        QCoreApplication::processEvents();
        const QString assetPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_asset_window.png" ).string() );
        CHECK( pAssetWindow->grab().save( assetPath ) );
        EditorAssetWindow_SetView( pAssetWindow, editor_asset_window_view_t::LIST );
        QCoreApplication::processEvents();
        const QString listPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_asset_window_list.png" ).string() );
        CHECK( pAssetWindow->grab().save( listPath ) );
        EditorAssetWindow_SetView( pAssetWindow, editor_asset_window_view_t::GRID );
        pAssetWindow->reject();
    }
    // Sandbox's Database View: a material's grid, then its shader's code.
    if ( session.Run( QStringLiteral( "assets.database materials/blockout/metal_panel.cymat" ) ) == command_result_t::OK ) {
        auto *pView = Mason_Window( session.pMason )->findChild<QDialog *>( QStringLiteral( "EditorDatabaseView" ) );
        REQUIRE( pView != nullptr );
        pView->resize( 1280, 820 );
        QCoreApplication::processEvents();
        const QString materialPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_database_material.png" ).string() );
        CHECK( pView->grab().save( materialPath ) );
        if ( EditorDatabaseView_OpenRow( pView, QStringLiteral( "Material Settings/Shader" ) ) ) {
            QCoreApplication::processEvents();
            const QString shaderPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_database_shader.png" ).string() );
            CHECK( pView->grab().save( shaderPath ) );
        }
        if ( EditorDatabaseView_Open( pView, QStringLiteral( "textures/blockout/metal_panel.cytex" ) ) ) {
            QCoreApplication::processEvents();
            const QString texturePath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_database_texture.png" ).string() );
            CHECK( pView->grab().save( texturePath ) );
        }
        pView->hide();
    }
    // A cordon around one room piece: outside is dimmed and hidden.
    if ( session.Run( QStringLiteral( "map.go_to 1000" ) ) == command_result_t::OK &&
         session.Run( QStringLiteral( "map.cordon.edit" ) ) == command_result_t::OK ) {
        QCoreApplication::processEvents();
        const QString cordonPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_cordon.png" ).string() );
        CHECK( Mason_Window( session.pMason )->grab().save( cordonPath ) );
        CHECK( session.Run( QStringLiteral( "map.cordon.toggle" ) ) == command_result_t::OK );
    }
    // The layout picker's icon grid.
    if ( auto *pLayoutButton = Mason_Window( session.pMason )->findChild<QToolButton *>( QStringLiteral( "masonLayoutPreset" ) ) ) {
        QMenu *pLayoutMenu = pLayoutButton->menu();
        REQUIRE( pLayoutMenu != nullptr );
        pLayoutMenu->popup( pLayoutButton->mapToGlobal( QPoint( 0, pLayoutButton->height() ) ) );
        QCoreApplication::processEvents();
        const QString layoutPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_layouts.png" ).string() );
        CHECK( pLayoutMenu->grab().save( layoutPath ) );
        pLayoutMenu->hide();
    }
    // The keyboard shortcuts reference.
    REQUIRE( session.Run( QStringLiteral( "help.shortcuts" ) ) == command_result_t::OK );
    QDialog *pShortcuts = Mason_Window( session.pMason )->findChild<QDialog *>( QStringLiteral( "EditorShortcutsDialog" ) );
    REQUIRE( pShortcuts != nullptr );
    QCoreApplication::processEvents();
    const QString shortcutsPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_shortcuts.png" ).string() );
    CHECK( pShortcuts->grab().save( shortcutsPath ) );
    pShortcuts->hide();
    pPalette->hide();
    pSettings->hide();
    EditorAppearancePage_SelectTheme( pAppearance, QStringLiteral( "hammer_charcoal" ) );
    MapWorkspace_Select( Mason_MapWorkspace( session.pMason ), 1000u, MAP_SELECT_REPLACE );
    QCoreApplication::processEvents();
    const QString hammerPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_hammer_charcoal.png" ).string() );
    CHECK( Mason_Window( session.pMason )->grab().save( hammerPath ) );
    REQUIRE( session.Run( QStringLiteral( "map.tool.camera" ) ) == command_result_t::OK );
    QCoreApplication::processEvents();
    if ( auto *properties = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "MapToolProperties" ) ) ) {
        const QString cameraPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_camera_properties.png" ).string() );
        CHECK( properties->grab().save( cameraPath ) );
    }
    REQUIRE( session.Run( QStringLiteral( "map.tool.select" ) ) == command_result_t::OK );
    const QString startupPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_startup.png" ).string() );
    QSplashScreen *startup = Mason_StartupScreen( Mason_Gui( session.pMason )->style );
    startup->showMessage( QStringLiteral( "Building tools and panels..." ), Qt::AlignBottom | Qt::AlignLeft, Qt::white );
    startup->show();
    QCoreApplication::processEvents();
    CHECK( startup->grab().save( startupPath ) );
    delete startup;
    // Also inspect the user's four-view Graphite configuration, not only the
    // bundled presentation-led layout.
    EditorAppearancePage_SelectTheme( pAppearance, QStringLiteral( "graphite" ) );
    REQUIRE( session.Run( QStringLiteral( "view.layout.four" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "map.view.frame_all" ) ) == command_result_t::OK );
    MapWorkspace_Select( Mason_MapWorkspace( session.pMason ), 120u, MAP_SELECT_REPLACE );
    QCoreApplication::processEvents();
    const QString graphitePath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_graphite_four.png" ).string() );
    CHECK( Mason_Window( session.pMason )->grab().save( graphitePath ) );
    Mason_Window( session.pMason )->resize( 1280, 720 );
    QCoreApplication::processEvents();
    const QString laptopPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_laptop.png" ).string() );
    CHECK( Mason_Window( session.pMason )->grab().save( laptopPath ) );
    // Optional local review of an installed user theme, still inside an
    // isolated headless session with no writes to the user's preferences.
    const QString reviewThemes = qEnvironmentVariable( "CYPHER_MASON_CAPTURE_THEME_DIR" );
    const QByteArray reviewTheme = qgetenv( "CYPHER_MASON_CAPTURE_THEME_ID" );
    if ( !reviewThemes.isEmpty() && !reviewTheme.isEmpty() ) {
        CHECK( EditorGui_LoadThemeFolder( Mason_Gui( session.pMason ), reviewThemes ) > 0u );
        REQUIRE( EditorGui_SelectTheme( Mason_Gui( session.pMason ), App(),
                 { reviewTheme.constData(), static_cast<usize>( reviewTheme.size() ) } ) == editor_gui_status_t::OK );
        for ( const QSize size : { QSize( 1280, 720 ), QSize( 2560, 1600 ) } ) {
            Mason_Window( session.pMason )->resize( size );
            QCoreApplication::processEvents();
            const QString reviewPath = QString::fromStdString( ( std::filesystem::temp_directory_path() /
                ( "mason_user_theme_" + std::to_string( size.width() ) + ".png" ) ).string() );
            CHECK( Mason_Window( session.pMason )->grab().save( reviewPath ) );
        }
    }
    // Popup menus are separate Qt windows; capture their actual rendering too.
    const char *menuNames[]{ "menu:&Mesh", "EditorViewOptionsMenu0", "EditorViewDrawingAids", "masonAngleSnapMenu", "masonScaleSnapMenu" };
    const char *imageNames[]{ "mason_mesh_menu.png", "mason_view_menu.png", "mason_drawing_aids_menu.png", "mason_angle_snap_menu.png", "mason_scale_snap_menu.png" };
    for ( int i = 0; i < 5; ++i ) {
        auto *pMenu = Mason_Window( session.pMason )->findChild<QMenu *>( QString::fromLatin1( menuNames[i] ) );
        REQUIRE( pMenu != nullptr );
        pMenu->popup( QPoint( 100, 100 ) );
        QCoreApplication::processEvents();
        const QString menuPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / imageNames[i] ).string() );
        CHECK( pMenu->grab().save( menuPath ) );
        pMenu->hide();
    }
    auto *pFilter = Mason_Window( session.pMason )->findChild<QToolButton *>( QStringLiteral( "MapOutlinerFilters" ) );
    REQUIRE( pFilter != nullptr );
    REQUIRE( pFilter->menu() != nullptr );
    pFilter->menu()->popup( QPoint( 100, 100 ) );
    QCoreApplication::processEvents();
    const QString filterPath = QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_outliner_filters.png" ).string() );
    CHECK( pFilter->menu()->grab().save( filterPath ) );
    pFilter->menu()->hide();
}

TEST_CASE( "Mason reset restores closed and reassigned view panes", "[mason][smoke]" )
{
    mason_session_t session;
    auto *pViews = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( pViews != nullptr );
    MapViews_SetPaneType( pViews, 0, map_view_type_t::SIDE );
    MapViews_SetPaneVisible( pViews, 2, false );
    MapViews_SetMaximized( pViews, 1 );
    REQUIRE( session.Run( QStringLiteral( "view.layout.reset" ) ) == command_result_t::OK );
    QCoreApplication::processEvents();
    CHECK( MapViews_MaximizedPane( pViews ) == -1 );
    constexpr map_view_type_t types[]{ map_view_type_t::CAMERA, map_view_type_t::TOP, map_view_type_t::FRONT, map_view_type_t::SIDE };
    for ( int i = 0; i < MAP_VIEW_PANE_COUNT; ++i ) {
        CHECK( MapViews_IsPaneVisible( pViews, i ) == ( i < 3 ) );
        CHECK( MapViews_PaneType( pViews, i ) == types[i] );
    }
}

TEST_CASE( "Mason saves and restores independent panel and viewport presentation", "[mason][smoke][workspace]" )
{
    mason_session_t session;
    auto *pWindow = Mason_Window( session.pMason );
    auto *pViews = pWindow->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    auto *pPicker = pWindow->findChild<QToolButton *>( QStringLiteral( "masonLayoutPreset" ) );
    auto *pFour = pWindow->findChild<QToolButton *>( QStringLiteral( "masonLayout_layout-four" ) );
    auto *pOptions = pWindow->findChild<QToolButton *>( QStringLiteral( "masonSessionOptions" ) );
    REQUIRE( pViews != nullptr );
    REQUIRE( pPicker != nullptr );
    REQUIRE( pFour != nullptr );
    REQUIRE( pOptions != nullptr );
    REQUIRE( pOptions->menu() != nullptr );
    CHECK( pOptions->menu()->actions().contains( pWindow->findChild<QAction *>( QStringLiteral( "view.layout.save" ) ) ) );
    pFour->click();
    REQUIRE( MapViews_Arrangement( pViews ) == map_view_arrangement_t::FOUR );
    MapViews_SetPaneType( pViews, 2, map_view_type_t::TOP ); // Two simultaneous Top views.
    MapViews_SetPaneVisible( pViews, 3, false );
    REQUIRE( session.Run( QStringLiteral( "view.history" ) ) == command_result_t::OK );
    QCoreApplication::processEvents();
    REQUIRE( session.Run( QStringLiteral( "view.layout.save" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "view.layout.reset" ) ) == command_result_t::OK );
    REQUIRE( MapViews_Arrangement( pViews ) == map_view_arrangement_t::HAMMER );
    REQUIRE( session.Run( QStringLiteral( "view.layout.restore" ) ) == command_result_t::OK );
    QCoreApplication::processEvents();
    CHECK( MapViews_Arrangement( pViews ) == map_view_arrangement_t::FOUR );
    CHECK( pFour->isChecked() );
    CHECK( MapViews_PaneType( pViews, 2 ) == map_view_type_t::TOP );
    CHECK_FALSE( MapViews_IsPaneVisible( pViews, 3 ) );
    CHECK_FALSE( pWindow->findChild<QAction *>( QStringLiteral( "view.history" ) )->isChecked() );
    // Switching layouts and toggling panels is a UI operation, never a map edit.
    CHECK_FALSE( MapWorkspace_IsModified( Mason_MapWorkspace( session.pMason ) ) );
    CHECK( EditorHistory_StepCount( &Mason_MapWorkspace( session.pMason )->history ) == 0u );
}

TEST_CASE( "Mason command search and editing toolbar use shared command actions", "[mason][smoke]" )
{
    mason_session_t session;
    QMainWindow *pWindow = Mason_Window( session.pMason );
    auto *pSearch = pWindow->findChild<QToolButton *>( QStringLiteral( "masonCommandSearch" ) );
    REQUIRE( pSearch != nullptr );
    CHECK( pSearch->defaultAction() == pWindow->findChild<QAction *>( QStringLiteral( "view.command_palette" ) ) );
    pSearch->click();
    QCoreApplication::processEvents();
    auto *pPalette = pWindow->findChild<QWidget *>( QStringLiteral( "EditorCommandPalette" ) );
    REQUIRE( pPalette != nullptr );
    CHECK( pPalette->isVisible() );
    auto *pEditing = pWindow->findChild<QToolBar *>( QStringLiteral( "masonEditingTools" ) );
    REQUIRE( pEditing != nullptr );
    auto *pTranslate = pWindow->findChild<QAction *>( QStringLiteral( "map.tool.translate" ) );
    REQUIRE( pTranslate != nullptr );
    CHECK( pEditing->actions().contains( pTranslate ) );
    CHECK( pTranslate->isEnabled() ); // The transactional geometry interaction is now connected.
}

TEST_CASE( "Mason edit commands duplicate and delete authored geometry transactionally", "[mason][smoke][geometry-edit]" )
{
    mason_session_t session;
    auto *pMap = Mason_MapWorkspace( session.pMason );
    auto *pWindow = Mason_Window( session.pMason );
    CHECK( session.Run( QStringLiteral( "edit.duplicate" ) ) == command_result_t::DISABLED );
    CHECK( session.Run( QStringLiteral( "edit.delete" ) ) == command_result_t::DISABLED );
    CHECK( session.Run( QStringLiteral( "map.transform.dialog" ) ) == command_result_t::DISABLED );
    map_bounds_t bounds{};
    MapBounds_AddPoint( bounds, { -32.0, -32.0, 0.0 } );
    MapBounds_AddPoint( bounds, { 32.0, 32.0, 64.0 } );
    REQUIRE( MapWorkspace_CreateBox( pMap, bounds ) );
    REQUIRE( pMap->wire.objects.nCount == 1u );
    MapWorkspace_Select( pMap, pMap->wire.objects.pData[0].id, MAP_SELECT_REPLACE );
    REQUIRE( session.Run( QStringLiteral( "map.transform.dialog" ) ) == command_result_t::OK );
    auto *pTransform = pWindow->findChild<QDialog *>( QStringLiteral( "MapTransformDialog" ) );
    REQUIRE( pTransform != nullptr );
    CHECK( pTransform->isVisible() );
    CHECK_FALSE( pTransform->isModal() );
    pTransform->hide();
    REQUIRE( session.Run( QStringLiteral( "edit.duplicate" ) ) == command_result_t::OK );
    REQUIRE( pMap->wire.objects.nCount == 2u );
    REQUIRE( EditorSelection_Count( &pMap->selection ) == 1u );
    CHECK( pMap->wire.objects.pData[0].id != pMap->wire.objects.pData[1].id );
    REQUIRE( session.Run( QStringLiteral( "edit.delete" ) ) == command_result_t::OK );
    CHECK( pMap->wire.objects.nCount == 1u );
    REQUIRE( session.Run( QStringLiteral( "edit.undo" ) ) == command_result_t::OK );
    CHECK( pMap->wire.objects.nCount == 2u );
    REQUIRE( session.Run( QStringLiteral( "edit.undo" ) ) == command_result_t::OK );
    CHECK( pMap->wire.objects.nCount == 1u );
}

TEST_CASE( "Mason history refreshes rendered map data and tracks clean document state", "[mason][smoke][history]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *pMap = Mason_MapWorkspace( session.pMason );
    auto *pWindow = Mason_Window( session.pMason );
    // Undo History is a tab behind the Outliner; bring it forward first
    // (ADS parents only the current tab to the window).
    auto *pManager = pWindow->findChild<ads::CDockManager *>();
    REQUIRE( pManager != nullptr );
    ads::CDockWidget *pHistoryDock = pManager->findDockWidget( QStringLiteral( "map.history" ) );
    REQUIRE( pHistoryDock != nullptr );
    pHistoryDock->setAsCurrentTab();
    QCoreApplication::processEvents();
    auto *pHistory = pHistoryDock->widget();
    REQUIRE( pHistory != nullptr );
    auto *pSteps = pHistory->findChild<QTreeWidget *>( QStringLiteral( "EditorHistorySteps" ) );
    auto *pUndo = pHistory->findChild<QToolButton *>( QStringLiteral( "EditorHistoryUndo" ) );
    auto *pRedo = pHistory->findChild<QToolButton *>( QStringLiteral( "EditorHistoryRedo" ) );
    REQUIRE( pSteps != nullptr );
    REQUIRE( pUndo != nullptr );
    REQUIRE( pRedo != nullptr );
    struct move_t { u64 id; math::vec3d_t before; math::vec3d_t after; };
    const auto *pEntity = MapWireframe_FindEntity( pMap->wire, 110u );
    REQUIRE( pEntity != nullptr );
    const move_t move{ 110u, pEntity->origin, { 64.0, 16.0, 48.0 } };
    // Use a real document operation here: history navigation must rebuild
    // the map views, not merely move a row highlight in the history widget.
    undo_operation_desc_t operation{};
    operation.id = 1u;
    operation.label = StringView_FromCString( "Move spawn" );
    operation.payload = BinaryBlock_FromData( &move, sizeof( move ) );
    operation.pUserData = pMap;
    operation.pfnUndo = []( binary_block_t bytes, void *pContext ) noexcept {
        move_t value{};
        std::memcpy( &value, bytes.pData, sizeof( value ) );
        return MapDocument_SetEntityOrigin( static_cast<map_workspace_t *>( pContext )->pDocument, value.id, value.before ) == map_status_t::OK
                   ? CY_ERROR_OK : Cy_ErrorMake( common_error_t::ERR_INVALID_STATE );
    };
    operation.pfnRedo = []( binary_block_t bytes, void *pContext ) noexcept {
        move_t value{};
        std::memcpy( &value, bytes.pData, sizeof( value ) );
        return MapDocument_SetEntityOrigin( static_cast<map_workspace_t *>( pContext )->pDocument, value.id, value.after ) == map_status_t::OK
                   ? CY_ERROR_OK : Cy_ErrorMake( common_error_t::ERR_INVALID_STATE );
    };
    REQUIRE( MapDocument_SetEntityOrigin( pMap->pDocument, move.id, move.after ) == map_status_t::OK );
    REQUIRE( EditorHistory_Push( &pMap->history, operation ) == editor_history_status_t::OK );
    MapWorkspace_DocumentChanged( pMap );
    REQUIRE( pSteps->topLevelItemCount() == 2 );
    CHECK( pSteps->topLevelItem( 1 )->text( 0 ) == QStringLiteral( "Move spawn" ) );
    CHECK( pWindow->isWindowModified() );
    CHECK( MapWireframe_FindEntity( pMap->wire, move.id )->origin.x == move.after.x );
    pUndo->click();
    CHECK( MapWireframe_FindEntity( pMap->wire, move.id )->origin.x == move.before.x );
    CHECK_FALSE( pWindow->isWindowModified() );
    pRedo->click();
    CHECK( MapWireframe_FindEntity( pMap->wire, move.id )->origin.x == move.after.x );
    CHECK( pWindow->isWindowModified() );
    pSteps->itemClicked( pSteps->topLevelItem( 0 ), 0 );
    CHECK( MapWireframe_FindEntity( pMap->wire, move.id )->origin.z == move.before.z );
    CHECK_FALSE( pWindow->isWindowModified() );
    REQUIRE( session.Run( QStringLiteral( "file.new" ) ) == command_result_t::OK );
    CHECK( pSteps->topLevelItemCount() == 1 );
    CHECK_FALSE( pUndo->isEnabled() );
    CHECK_FALSE( pRedo->isEnabled() );
}

namespace
{

bool MasonWorkflowKey( QWidget *view, int code, Qt::KeyboardModifiers modifiers = Qt::NoModifier )
{
    QKeyEvent preflight( QEvent::ShortcutOverride, code, modifiers );
    preflight.ignore();
    QCoreApplication::sendEvent( view, &preflight );
    QKeyEvent press( QEvent::KeyPress, code, modifiers );
    QCoreApplication::sendEvent( view, &press );
    QKeyEvent release( QEvent::KeyRelease, code, modifiers );
    QCoreApplication::sendEvent( view, &release );
    return preflight.isAccepted();
}

void MasonWorkflowMouse( QWidget *view, QEvent::Type type, QPointF point, Qt::MouseButton button,
                         Qt::MouseButtons held = Qt::NoButton )
{
    QMouseEvent event( type, point, view->mapToGlobal( point ), button, held, Qt::NoModifier );
    QCoreApplication::sendEvent( view, &event );
}

void CheckMasonPosition( const math::vec3d_t &actual, const math::vec3d_t &expected )
{
    CHECK( std::abs( actual.x - expected.x ) < 1e-7 );
    CHECK( std::abs( actual.y - expected.y ) < 1e-7 );
    CHECK( std::abs( actual.z - expected.z ) < 1e-7 );
}

struct mason_keymap_chain_restore_t {
    editor_gui_t &gui;
    const key_value_t *previous[EDITOR_KEYMAP_MAX_DEPTH]{};
    usize count;
    explicit mason_keymap_chain_restore_t( editor_gui_t &value ) : gui( value ), count( value.nKeymapChain ) {
        for ( usize i = 0; i < count; ++i ) { previous[i] = gui.keymapChain[i]; }
    }
    ~mason_keymap_chain_restore_t() {
        for ( usize i = 0; i < EDITOR_KEYMAP_MAX_DEPTH; ++i ) { gui.keymapChain[i] = previous[i]; }
        gui.nKeymapChain = count;
    }
};

} // namespace

TEST_CASE( "Mason arrow nudges follow each orthographic projection and publish one reversible move", "[mason][smoke][viewport-workflow][nudge]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *views = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    map_bounds_t bounds{};
    MapBounds_AddPoint( bounds, { 3.5, 7.25, 9.5 } );
    MapBounds_AddPoint( bounds, { 67.5, 71.25, 73.5 } );
    REQUIRE( MapWorkspace_CreateBox( ws, bounds ) );
    const u64 id = EditorSelection_At( &ws->selection, 0 );
    const auto position = [&]() {
        const auto *object = MapWireframe_FindObject( ws->wire, id );
        REQUIRE( object != nullptr );
        return object->bounds.box.minimum;
    };
    // Nudge uses the authored grid size, not its screen-density adaptation or
    // the absolute coordinates of an off-grid object's existing vertices.
    MapWorkspace_SetGridSize( ws, 32 );
    MapWorkspace_SetSnapToGrid( ws, false );
    MapWorkspace_SetTool( ws, map_tool_t::SELECT );
    const struct { map_view_type_t type; int u; int v; } projections[]{
        { map_view_type_t::TOP, 0, 1 }, { map_view_type_t::FRONT, 1, 2 }, { map_view_type_t::SIDE, 0, 2 }
    };
    const struct { int key; int horizontal; int vertical; const char *name; } directions[]{
        { Qt::Key_Left, -1, 0, "left" }, { Qt::Key_Right, 1, 0, "right" },
        { Qt::Key_Up, 0, 1, "up" }, { Qt::Key_Down, 0, -1, "down" }
    };
    for ( const auto &projection : projections ) {
        MapViews_SetPaneType( views, 1, projection.type );
        MapViews_SetActivePane( views, 1 );
        QWidget *view = MapViews_PaneView( views, 1 );
        REQUIRE( view != nullptr );
        for ( const bool fine : { false, true } ) {
            for ( const auto &direction : directions ) {
                CAPTURE( static_cast<int>( projection.type ), fine, direction.name );
                const math::vec3d_t before = position();
                math::vec3d_t expected = before;
                f64 *components[]{ &expected.x, &expected.y, &expected.z };
                const f64 step = fine ? 1.0 : ws->gridSize;
                *components[projection.u] += direction.horizontal * step;
                *components[projection.v] += direction.vertical * step;
                const auto steps = EditorHistory_StepCount( &ws->history );
                const QString command = QStringLiteral( "map.%1.%2" )
                    .arg( fine ? QStringLiteral( "nudge_fine" ) : QStringLiteral( "nudge" ), QString::fromLatin1( direction.name ) );
                const QByteArray commandId = command.toUtf8();
                REQUIRE( ( EditorCommands_State( &ws->pGui->commands, StringView_FromCString( commandId.constData() ) ) & COMMAND_STATE_ENABLED ) != 0 );
                REQUIRE( MasonWorkflowKey( view, direction.key, fine ? Qt::ShiftModifier : Qt::NoModifier ) );
                CheckMasonPosition( position(), expected );
                CHECK( EditorSelection_Count( &ws->selection ) == 1u );
                CHECK( EditorSelection_At( &ws->selection, 0 ) == id );
                CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1u );
                REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
                CheckMasonPosition( position(), before );
                REQUIRE( MapWorkspace_Redo( ws ) == editor_history_status_t::OK );
                CheckMasonPosition( position(), expected );
            }
        }
    }
    // Changing the grid takes effect immediately, even after all view caches
    // have been populated. Fine movement remains one world unit.
    const auto before = position();
    MapWorkspace_SetGridSize( ws, 8 );
    REQUIRE( session.Run( QStringLiteral( "map.nudge.right" ) ) == command_result_t::OK );
    CheckMasonPosition( position(), { before.x + 8, before.y, before.z } );
    REQUIRE( session.Run( QStringLiteral( "map.nudge_fine.up" ) ) == command_result_t::OK );
    CheckMasonPosition( position(), { before.x + 8, before.y, before.z + 1 } );
    QTemporaryDir saved;
    REQUIRE( saved.isValid() );
    const auto final = position();
    const QString path = saved.filePath( QStringLiteral( "nudged.cymap" ) );
    REQUIRE( MapWorkspace_SaveAs( ws, path ).status == map_files_status_t::OK );
    REQUIRE( Mason_OpenMap( session.pMason, path ) );
    CheckMasonPosition( position(), final );
}

TEST_CASE( "Mason nudges reserve their keys without editing unavailable selections or active gestures", "[mason][smoke][viewport-workflow][nudge][gesture-context]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *views = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    map_bounds_t bounds{};
    MapBounds_AddPoint( bounds, { -32, -32, 0 } ); MapBounds_AddPoint( bounds, { 32, 32, 64 } );
    REQUIRE( MapWorkspace_CreateBox( ws, bounds ) );
    const u64 id = EditorSelection_At( &ws->selection, 0 );
    MapViews_SetPaneType( views, 1, map_view_type_t::TOP );
    MapViews_SetActivePane( views, 1 );
    QWidget *view = MapViews_PaneView( views, 1 );
    const auto steps = EditorHistory_StepCount( &ws->history );
    const auto *document = ws->pDocument;
    const auto blocked = [&]() {
        CHECK_FALSE( MapViews_CanNudgeSelection( views ) );
        CHECK_FALSE( MapViews_NudgeSelection( views, 1, 0 ) );
        CHECK( session.Run( QStringLiteral( "map.nudge.right" ) ) == command_result_t::DISABLED );
        REQUIRE( MasonWorkflowKey( view, Qt::Key_Right ) );
        CHECK( ws->pDocument == document );
        CHECK( EditorHistory_StepCount( &ws->history ) == steps );
    };
    SECTION( "Empty, read-only, hidden and component selections are unavailable" ) {
        MapWorkspace_Select( ws, 0, MAP_SELECT_REPLACE ); blocked();
        MapWorkspace_Select( ws, id, MAP_SELECT_REPLACE );
        ws->pDocument->bReadOnly = CY_TRUE; blocked(); ws->pDocument->bReadOnly = CY_FALSE;
        MapWorkspace_SetVisgroupHidden( ws, map_visgroup_t::BRUSHES, CY_TRUE ); blocked();
        MapWorkspace_SetVisgroupHidden( ws, map_visgroup_t::BRUSHES, CY_FALSE );
        for ( const auto mode : { map_element_mode_t::VERTICES, map_element_mode_t::EDGES, map_element_mode_t::FACES } ) {
            MapWorkspace_SetElementMode( ws, mode ); blocked();
        }
        MapWorkspace_SetElementMode( ws, map_element_mode_t::MESHES );
        CHECK( MapViews_CanNudgeSelection( views ) );
        MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
        CHECK( MapViews_CanNudgeSelection( views ) );
    }
    SECTION( "A staged preview and an unrelated history transaction are protected" ) {
        MapWorkspace_SetEditPreview( ws, bounds ); blocked(); MapWorkspace_ClearEditPreview( ws );
        REQUIRE( EditorHistory_Begin( &ws->history, StringView_FromCString( "Independent gesture" ) ) == editor_history_status_t::OK );
        blocked(); EditorHistory_Cancel( &ws->history );
        CHECK( MapViews_CanNudgeSelection( views ) );
    }
    SECTION( "Space panning protects geometry until its held key is released" ) {
        QKeyEvent space( QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier );
        QCoreApplication::sendEvent( view, &space ); blocked();
        QKeyEvent released( QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier );
        QCoreApplication::sendEvent( view, &released );
        CHECK( MapViews_CanNudgeSelection( views ) );
    }
    SECTION( "An empty-space marquee cannot publish a nudge or a late geometry edit" ) {
        MapWorkspace_Frame( ws, false );
        const QPointF empty = MapOrthoView_WorldToView( view, { 180, 180 } );
        MasonWorkflowMouse( view, QEvent::MouseButtonPress, empty, Qt::LeftButton, Qt::LeftButton );
        blocked();
        MasonWorkflowMouse( view, QEvent::MouseButtonRelease, empty, Qt::LeftButton );
        CHECK( ws->pDocument == document );
        CHECK( EditorHistory_StepCount( &ws->history ) == steps );
        CheckMasonPosition( MapWireframe_FindObject( ws->wire, id )->bounds.box.minimum, bounds.box.minimum );
    }
    SECTION( "Camera and embedded content do not turn arrow input into map nudges" ) {
        MapViews_SetActivePane( views, 0 );
        CHECK_FALSE( MapViews_CanNudgeSelection( views ) );
        CHECK_FALSE( MapViews_NudgeSelection( views, 1, 0 ) );
        MapViews_SetPaneType( views, 1, map_view_type_t::ASSETS );
        MapViews_SetActivePane( views, 1 );
        CHECK_FALSE( MapViews_CanNudgeSelection( views ) );
        CHECK_FALSE( MapViews_NudgeSelection( views, 1, 0 ) );
        CHECK( ws->pDocument == document );
        CHECK( EditorHistory_StepCount( &ws->history ) == steps );
    }
    CHECK_FALSE( MapViews_NudgeSelection( views, 0, 0 ) );
    CHECK_FALSE( MapViews_NudgeSelection( views, 2, 0 ) );
    CHECK_FALSE( MapViews_NudgeSelection( views, 1, 1 ) );
    CHECK_FALSE( MapViews_CanNudgeSelection( nullptr ) );
    CHECK_FALSE( MapViews_NudgeSelection( nullptr, 1, 0 ) );
}

TEST_CASE( "Mason nudge moves selected point entities through the existing authored transaction", "[mason][smoke][viewport-workflow][nudge][entity]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *views = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapWorkspace_Select( ws, 110u, MAP_SELECT_REPLACE );
    const auto *entity = MapWireframe_FindEntity( ws->wire, 110u );
    REQUIRE( entity != nullptr );
    const auto before = entity->origin;
    const auto steps = EditorHistory_StepCount( &ws->history );
    MapWorkspace_SetGridSize( ws, 16 );
    MapWorkspace_SetElementMode( ws, map_element_mode_t::GROUPS );
    MapViews_SetPaneType( views, 1, map_view_type_t::FRONT ); MapViews_SetActivePane( views, 1 );
    REQUIRE( session.Run( QStringLiteral( "map.nudge.right" ) ) == command_result_t::OK );
    entity = MapWireframe_FindEntity( ws->wire, 110u ); REQUIRE( entity != nullptr );
    CheckMasonPosition( entity->origin, { before.x, before.y + 16, before.z } );
    CHECK( EditorSelection_At( &ws->selection, 0 ) == 110u );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1 );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    CheckMasonPosition( MapWireframe_FindEntity( ws->wire, 110u )->origin, before );
    REQUIRE( MapWorkspace_Redo( ws ) == editor_history_status_t::OK );
    CheckMasonPosition( MapWireframe_FindEntity( ws->wire, 110u )->origin, { before.x, before.y + 16, before.z } );
}

TEST_CASE( "Mason viewport nudge remaps and reservations protect competing window shortcuts", "[mason][smoke][viewport-workflow][nudge][input]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *views = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    map_bounds_t bounds{};
    MapBounds_AddPoint( bounds, { 0, 0, 0 } ); MapBounds_AddPoint( bounds, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( ws, bounds ) );
    const u64 id = EditorSelection_At( &ws->selection, 0 );
    MapWorkspace_SetGridSize( ws, 16 );
    MapViews_SetPaneType( views, 1, map_view_type_t::TOP ); MapViews_SetActivePane( views, 1 );
    QWidget *view = MapViews_PaneView( views, 1 );
    settings_document_t keys{};
    REQUIRE( SettingsDocument_Init( &keys, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &keys, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "mason_nudge_override" bindings = { "map.viewport.2d" = {
    "map.nudge.right" = [ "J" ]
    "map.nudge.left" = []
} } })cykv" ) ).status == settings_document_status_t::OK );
    mason_keymap_chain_restore_t restore( *ws->pGui );
    REQUIRE( restore.count < EDITOR_KEYMAP_MAX_DEPTH );
    for ( usize i = restore.count; i > 0; --i ) { ws->pGui->keymapChain[i] = ws->pGui->keymapChain[i - 1]; }
    ws->pGui->keymapChain[0] = SettingsDocument_Root( &keys ); ws->pGui->nKeymapChain = restore.count + 1;
    CHECK( MapInput_CommandBindings( ws, "map.nudge.right", false ) == QStringList{ QStringLiteral( "J" ) } );
    CHECK( MapInput_CommandBindings( ws, "map.nudge.left", false ).isEmpty() );
    const auto steps = EditorHistory_StepCount( &ws->history );
    QKeyEvent oldRight( QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier );
    CHECK_FALSE( MapInput_DispatchKey( ws, &oldRight, false, false, true ) );
    REQUIRE( MasonWorkflowKey( view, Qt::Key_Left ) ); // Explicit unbinding reserves the inherited key.
    CHECK( EditorHistory_StepCount( &ws->history ) == steps );
    int fallbackCalls = 0;
    QAction fallback( QStringLiteral( "Fallback" ), Mason_Window( session.pMason ) );
    fallback.setShortcut( QKeySequence( Qt::Key_J ) );
    fallback.setShortcutContext( Qt::WindowShortcut );
    Mason_Window( session.pMason )->addAction( &fallback );
    QObject::connect( &fallback, &QAction::triggered, &fallback, [&]() { ++fallbackCalls; } );
    REQUIRE( MasonWorkflowKey( view, Qt::Key_J ) );
    CHECK( fallbackCalls == 0 );
    CheckMasonPosition( MapWireframe_FindObject( ws->wire, id )->bounds.box.minimum, { 16, 0, 0 } );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1 );
    MapWorkspace_Select( ws, 0, MAP_SELECT_REPLACE );
    REQUIRE( MasonWorkflowKey( view, Qt::Key_J ) ); // Disabled command still reserves its key.
    CHECK( fallbackCalls == 0 );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1 );
    // Nudges deliberately have no window action binding: an inspector field
    // keeps native cursor movement while the last active pane remains Top.
    auto *action = Mason_Window( session.pMason )->findChild<QAction *>( QStringLiteral( "map.nudge.right" ) );
    CHECK( ( action == nullptr || action->shortcuts().isEmpty() ) );
    QLineEdit inspector( Mason_Window( session.pMason ) );
    inspector.setText( QStringLiteral( "map name" ) ); inspector.show(); inspector.setFocus(); inspector.setCursorPosition( 4 );
    ( void )MasonWorkflowKey( &inspector, Qt::Key_Left );
    CHECK( inspector.cursorPosition() == 3 );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1 );
}

TEST_CASE( "Mason Tab cycles only visible view panes and embedded asset controls keep native Tab", "[mason][smoke][viewport-workflow][view-cycle]" )
{
    mason_session_t session;
    auto *views = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::FOUR );
    MapViews_SetActivePane( views, 0 );
    REQUIRE( MasonWorkflowKey( MapViews_PaneView( views, 0 ), Qt::Key_Tab ) );
    CHECK( MapViews_ActivePane( views ) == 1 );
    MapViews_SetPaneVisible( views, 2, false );
    REQUIRE( MasonWorkflowKey( MapViews_PaneView( views, 1 ), Qt::Key_Tab ) );
    CHECK( MapViews_ActivePane( views ) == 3 );
    REQUIRE( MapViews_CycleActivePane( views ) );
    CHECK( MapViews_ActivePane( views ) == 0 );
    MapViews_SetMaximized( views, 3 );
    REQUIRE( MasonWorkflowKey( MapViews_PaneView( views, 3 ), Qt::Key_Tab ) );
    CHECK( MapViews_ActivePane( views ) == 3 );
    CHECK( MapViews_MaximizedPane( views ) == 3 );
    MapViews_SetMaximized( views, -1 );
    MapViews_SetPaneType( views, 1, map_view_type_t::ASSETS );
    MapViews_SetActivePane( views, 0 );
    REQUIRE( MapViews_CycleActivePane( views ) );
    CHECK( MapViews_ActivePane( views ) == 1 );
    auto *assets = MapViews_PaneView( views, 1 );
    auto *filter = assets->findChild<QLineEdit *>( QStringLiteral( "AssetWindowFilter" ) );
    REQUIRE( filter != nullptr ); filter->setFocus();
    ( void )MasonWorkflowKey( filter, Qt::Key_Tab );
    CHECK( MapViews_ActivePane( views ) == 1 );
    CHECK( QApplication::focusWidget() != filter );
    CHECK( assets->isAncestorOf( QApplication::focusWidget() ) );
    auto *action = Mason_Window( session.pMason )->findChild<QAction *>( QStringLiteral( "map.view.cycle" ) );
    CHECK( ( action == nullptr || action->shortcuts().isEmpty() ) );
}

TEST_CASE( "Mason contextual view keys change only the active pane and reserve F11 for mesh edges", "[mason][smoke][viewport-workflow][view-modes]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::WIREFRAME );
    MapViews_SetPaneType( views, 1, map_view_type_t::TOP ); MapViews_SetActivePane( views, 1 );
    const auto initialState = window->windowState();
    const struct { int key; map_render_mode_t mode; } modes[]{
        { Qt::Key_F5, map_render_mode_t::FULLBRIGHT }, { Qt::Key_F6, map_render_mode_t::SHADED }, { Qt::Key_F7, map_render_mode_t::NORMALS }
    };
    for ( const auto &mode : modes ) {
        CAPTURE( mode.key );
        REQUIRE( MasonWorkflowKey( MapViews_PaneView( views, 1 ), mode.key ) );
        CHECK( MapViews_PaneType( views, 1 ) == map_view_type_t::CAMERA );
        CHECK( MapViews_PaneView( views, 1 )->hasFocus() ); // The next physical view key must reach the replacement.
        CHECK( MapViews_PaneRenderMode( views, 1 ) == mode.mode );
        CHECK( MapViews_PaneRenderMode( views, 0 ) == map_render_mode_t::WIREFRAME );
    }
    QWidget *camera = MapViews_PaneView( views, 1 );
    const bool overlay = MapViews_PaneWireOverlay( views, 1 );
    const bool otherOverlay = MapViews_PaneWireOverlay( views, 0 );
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_F8 ) );
    CHECK( MapViews_PaneWireOverlay( views, 1 ) != overlay );
    CHECK( MapViews_PaneWireOverlay( views, 0 ) == otherOverlay );
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_F8 ) );
    CHECK( MapViews_PaneWireOverlay( views, 1 ) == overlay );
    const bool edges = MapViews_PaneMeshEdges( views, 1 );
    const bool otherEdges = MapViews_PaneMeshEdges( views, 0 );
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_F11 ) );
    CHECK( MapViews_PaneMeshEdges( views, 1 ) != edges );
    CHECK( MapViews_PaneMeshEdges( views, 0 ) == otherEdges );
    CHECK( window->windowState() == initialState ); // More specific than global fullscreen.
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_F11 ) );
    CHECK( MapViews_PaneMeshEdges( views, 1 ) == edges );
    CHECK( window->windowState() == initialState );
    REQUIRE( session.Run( QStringLiteral( "view.fullscreen" ) ) == command_result_t::OK );
    CHECK( window->windowState().testFlag( Qt::WindowFullScreen ) );
    CHECK( ( EditorCommands_State( &Mason_Gui( session.pMason )->commands, StringView_FromCString( "view.fullscreen" ) ) & COMMAND_STATE_CHECKED ) != 0 );
    REQUIRE( session.Run( QStringLiteral( "view.fullscreen" ) ) == command_result_t::OK );
    CHECK_FALSE( window->windowState().testFlag( Qt::WindowFullScreen ) );
    CHECK( ( window->windowState() & ~Qt::WindowFullScreen ) == ( initialState & ~Qt::WindowFullScreen ) );
    window->setWindowState( initialState | Qt::WindowMaximized );
    REQUIRE( session.Run( QStringLiteral( "view.fullscreen" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "view.fullscreen" ) ) == command_result_t::OK );
    CHECK( window->windowState().testFlag( Qt::WindowMaximized ) );
    CHECK_FALSE( window->windowState().testFlag( Qt::WindowFullScreen ) );
}

TEST_CASE( "Mason geometric Tab routing honors remaps unbinding and physical Backtab", "[mason][smoke][viewport-workflow][view-cycle][input]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *views = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::FOUR );
    const char *text = nullptr;
    bool remap = false, backtab = false;
    SECTION( "A remapped Tab executes its command once without native focus traversal" ) {
        remap = true;
        text = R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "tab_remap" bindings = { "map.viewport" = {
    "map.view.cycle" = [] "map.grid.larger" = [ "Tab" ]
} } })cykv";
    }
    SECTION( "An explicitly unbound Tab remains reserved" ) {
        text = R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "tab_unbind" bindings = { "map.viewport" = { "map.view.cycle" = [] } } })cykv";
    }
    SECTION( "A physical Shift Backtab executes a declared Shift Tab cycle" ) {
        backtab = true;
        text = R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "tab_shift" bindings = { "map.viewport" = { "map.view.cycle" = [ "Shift+Tab" ] } } })cykv";
    }
    REQUIRE( text != nullptr );
    settings_document_t keys{};
    REQUIRE( SettingsDocument_Init( &keys, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &keys, StringView_FromCString( text ) ).status == settings_document_status_t::OK );
    for ( const auto type : { map_view_type_t::CAMERA, map_view_type_t::TOP } ) {
        CAPTURE( static_cast<int>( type ) );
        MapViews_SetPaneType( views, 0, type ); MapViews_SetActivePane( views, 0 );
        QWidget *view = MapViews_PaneView( views, 0 );
        REQUIRE( view->hasFocus() );
        mason_keymap_chain_restore_t restore( *ws->pGui );
        REQUIRE( restore.count < EDITOR_KEYMAP_MAX_DEPTH );
        for ( usize i = restore.count; i > 0; --i ) { ws->pGui->keymapChain[i] = ws->pGui->keymapChain[i - 1]; }
        ws->pGui->keymapChain[0] = SettingsDocument_Root( &keys ); ws->pGui->nKeymapChain = restore.count + 1;
        MapWorkspace_SetGridSize( ws, 16 );
        const auto history = EditorHistory_StepCount( &ws->history );
        if ( backtab ) {
            REQUIRE( MasonWorkflowKey( view, Qt::Key_Backtab, Qt::ShiftModifier ) );
            CHECK( MapViews_ActivePane( views ) == 1 );
            CHECK( MapViews_PaneView( views, 1 )->hasFocus() );
            CHECK( ws->gridSize == 16 );
        } else {
            REQUIRE( MasonWorkflowKey( view, Qt::Key_Tab ) );
            CHECK( MapViews_ActivePane( views ) == 0 );
            CHECK( view->hasFocus() );
            CHECK( ws->gridSize == ( remap ? 32 : 16 ) );
        }
        CHECK( EditorHistory_StepCount( &ws->history ) == history );
    }
}

TEST_CASE( "Mason pane changes preserve external text focus and never focus hidden cached panes", "[mason][smoke][viewport-workflow][view-focus]" )
{
    mason_session_t session;
    auto *views = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::FOUR );
    MapViews_SetActivePane( views, 0 );
    QLineEdit inspector( Mason_Window( session.pMason ) );
    inspector.setText( QStringLiteral( "Unfinished name" ) ); inspector.show(); inspector.setFocus();
    REQUIRE( inspector.hasFocus() );
    MapViews_SetPaneType( views, 0, map_view_type_t::TOP );
    CHECK( inspector.hasFocus() );
    MapViews_SetPaneType( views, 1, map_view_type_t::SHADERS );
    CHECK( inspector.hasFocus() );
    CHECK( inspector.text() == QStringLiteral( "Unfinished name" ) );
    MapViews_SetActivePane( views, 0 );
    REQUIRE( MapViews_PaneView( views, 0 )->hasFocus() );
    MapViews_SetPaneVisible( views, 2, false );
    MapViews_SetActivePane( views, 2 );
    CHECK( MapViews_ActivePane( views ) == 0 );
    CHECK( MapViews_PaneView( views, 0 )->hasFocus() );
    CHECK_FALSE( MapViews_PaneView( views, 2 )->hasFocus() );
    MapViews_SetMaximized( views, 3 );
    REQUIRE( MapViews_PaneView( views, 3 )->hasFocus() );
    MapViews_SetActivePane( views, 0 );
    CHECK( MapViews_ActivePane( views ) == 3 );
    CHECK( MapViews_PaneView( views, 3 )->hasFocus() );
    CHECK_FALSE( MapViews_PaneView( views, 0 )->hasFocus() );
}

TEST_CASE( "Capture Mason viewport shortcut contexts and orthographic nudge reference", "[.keyboard-workflow-screenshot]" )
{
    mason_session_t session;
    REQUIRE( session.Run( QStringLiteral( "help.shortcuts" ) ) == command_result_t::OK );
    auto *dialog = Mason_Window( session.pMason )->findChild<QDialog *>( QStringLiteral( "EditorShortcutsDialog" ) );
    REQUIRE( dialog != nullptr );
    auto *tree = dialog->findChild<QTreeWidget *>( QStringLiteral( "EditorShortcutsList" ) );
    REQUIRE( tree != nullptr );
    REQUIRE( tree->columnCount() == 4 );
    REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    EditorShortcutsDialog_SetBoundOnly( dialog, true );
    EditorShortcutsDialog_SetSearch( dialog, QStringLiteral( "map.viewport" ) );
    dialog->resize( 1280, 1140 );
    QCoreApplication::processEvents();
    const auto viewportRows = EditorShortcutsDialog_Rows( dialog );
    REQUIRE_FALSE( viewportRows.isEmpty() );
    bool foundMode = false, foundNudge = false, foundNavigation = false;
    for ( const auto &row : viewportRows ) {
        CHECK( row.split( QLatin1Char( '\t' ) ).value( 3 ).startsWith( QStringLiteral( "map.viewport" ) ) );
        foundMode |= row.contains( QStringLiteral( "3d Fullbright\tF5\tmap.viewport" ) );
        foundNudge |= row.contains( QStringLiteral( "Nudge Right\tRight\tmap.viewport.2d" ) );
        foundNavigation |= row.contains( QStringLiteral( "Camera Forward\tW, Up\tmap.viewport.3d" ) );
    }
    CHECK( foundMode ); CHECK( foundNudge ); CHECK( foundNavigation );
    CHECK( dialog->grab().save( QStringLiteral( "artifacts/mason_keyboard_shortcuts.png" ) ) );
    EditorShortcutsDialog_SetSearch( dialog, QStringLiteral( "map.nudge" ) );
    dialog->resize( 1280, 480 );
    QCoreApplication::processEvents();
    const auto nudgeRows = EditorShortcutsDialog_Rows( dialog );
    REQUIRE_FALSE( nudgeRows.isEmpty() );
    int nudges = 0;
    for ( int group = 0; group < tree->topLevelItemCount(); ++group ) {
        const auto *category = tree->topLevelItem( group );
        for ( int child = 0; child < category->childCount(); ++child ) {
            const auto *row = category->child( child );
            const QString id = row->text( 3 );
            if ( id.startsWith( QStringLiteral( "map.nudge." ) ) || id.startsWith( QStringLiteral( "map.nudge_fine." ) ) ) {
                ++nudges;
                CHECK( row->text( 2 ) == QStringLiteral( "map.viewport.2d" ) );
                CHECK_FALSE( row->text( 1 ).isEmpty() );
            }
        }
    }
    REQUIRE( nudges == 8 ); // Search is fuzzy and may legitimately retain unrelated rows.
    CHECK( dialog->grab().save( QStringLiteral( "artifacts/mason_keyboard_nudges.png" ) ) );
    dialog->hide();
}

TEST_CASE( "Mason Keybindings command reuses Settings and its page releases borrowed keymap listeners", "[mason][smoke][keybindings]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason );
    auto *gui = Mason_Gui( session.pMason );
    const usize listeners = gui->nKeymapListeners;
    REQUIRE( session.Run( QStringLiteral( "tools.settings" ) ) == command_result_t::OK );
    auto *dialog = window->findChild<QDialog *>( QStringLiteral( "EditorSettingsDialog" ) );
    REQUIRE( dialog != nullptr );
    auto *page = dialog->findChild<QWidget *>( QStringLiteral( "EditorKeymapSettings" ) );
    REQUIRE( page != nullptr );
    CHECK( gui->nKeymapListeners == listeners + 1u );
    CHECK( EditorKeymapSettings_CurrentKeymap( page ) == QStringLiteral( "cypher_default" ) );
    EditorSettingsDialog_ShowPage( dialog, QStringLiteral( "Viewports/Grid and Snapping" ) );
    REQUIRE( session.Run( QStringLiteral( "tools.keymap_editor" ) ) == command_result_t::OK );
    CHECK( EditorSettingsDialog_CurrentPage( dialog ) == QStringLiteral( "Keybindings" ) );
    CHECK( dialog->isVisible() );
    CHECK( page->isVisible() );
    REQUIRE( dialog->findChild<QPushButton *>( QStringLiteral( "SettingsImport" ) ) != nullptr );
    REQUIRE( dialog->findChild<QPushButton *>( QStringLiteral( "SettingsExport" ) ) != nullptr );
    for ( int reopen = 0; reopen < 3; ++reopen ) {
        dialog->hide();
        REQUIRE( session.Run( QStringLiteral( "tools.keymap_editor" ) ) == command_result_t::OK );
        CHECK( window->findChild<QDialog *>( QStringLiteral( "EditorSettingsDialog" ) ) == dialog );
        CHECK( dialog->findChild<QWidget *>( QStringLiteral( "EditorKeymapSettings" ) ) == page );
        CHECK( gui->nKeymapListeners == listeners + 1u );
    }
    QTemporaryDir scratch;
    REQUIRE( scratch.isValid() );
    QWidget *transient = EditorKeymapSettings_Create( nullptr, gui, scratch.path() );
    REQUIRE( transient != nullptr );
    CHECK( gui->nKeymapListeners == listeners + 2u );
    delete transient;
    CHECK( gui->nKeymapListeners == listeners + 1u );
    REQUIRE( EditorGui_SelectKeymap( gui, StringView_FromCString( "cypher_default" ) ) == editor_gui_status_t::OK );
    CHECK( EditorKeymapSettings_CurrentKeymap( page ) == QStringLiteral( "cypher_default" ) );
    CHECK( gui->nKeymapListeners == listeners + 1u );
    auto *reference = page->findChild<QPushButton *>( QStringLiteral( "KeymapShortcutsReference" ) );
    REQUIRE( reference != nullptr ); REQUIRE( reference->isEnabled() ); reference->click();
    auto *referenceDialog = window->findChild<QDialog *>( QStringLiteral( "EditorShortcutsDialog" ) );
    REQUIRE( referenceDialog != nullptr ); CHECK( referenceDialog->isVisible() ); referenceDialog->hide();
    dialog->hide();
}

TEST_CASE( "Mason Keybindings Apply publishes staged viewport input and window shortcuts with a safe preference fallback", "[mason][smoke][keybindings][viewport-workflow]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason );
    auto *gui = Mason_Gui( session.pMason );
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    map_bounds_t bounds{};
    MapBounds_AddPoint( bounds, { 3.5, 7.25, 9.5 } );
    MapBounds_AddPoint( bounds, { 67.5, 71.25, 73.5 } );
    REQUIRE( MapWorkspace_CreateBox( ws, bounds ) );
    const u64 id = EditorSelection_At( &ws->selection, 0 );
    const auto position = [&]() {
        const auto *object = MapWireframe_FindObject( ws->wire, id );
        REQUIRE( object != nullptr );
        return object->bounds.box.minimum;
    };
    const auto activeKeymap = [gui]() {
        const string_view_t value = EditorGui_ActiveKeymapId( gui );
        return std::string( value.pData, value.cchLength );
    };
    const auto preference = [gui]() {
        const string_view_t value = EditorSettings_Text( &gui->settings, "editor.ui.keymap", {} );
        return std::string( value.pData, value.cchLength );
    };
    MapWorkspace_SetGridSize( ws, 32 );
    MapWorkspace_SetTool( ws, map_tool_t::SELECT );
    MapViews_SetPaneType( views, 1, map_view_type_t::TOP );
    const QAction *settingsAction = window->findChild<QAction *>( QStringLiteral( "tools.settings" ) );
    REQUIRE( settingsAction != nullptr );
    REQUIRE( EditorCommands_Find( &gui->commands, StringView_FromCString( "map.nudge.right" ) ) != nullptr );
    const QKeySequence originalShortcut( Qt::CTRL | Qt::Key_Comma );
    const QKeySequence remappedShortcut( Qt::CTRL | Qt::ALT | Qt::Key_F12 );
    REQUIRE( settingsAction->shortcut() == originalShortcut );
    REQUIRE( session.Run( QStringLiteral( "tools.keymap_editor" ) ) == command_result_t::OK );
    auto *dialog = window->findChild<QDialog *>( QStringLiteral( "EditorSettingsDialog" ) );
    REQUIRE( dialog != nullptr );
    auto *page = dialog->findChild<QWidget *>( QStringLiteral( "EditorKeymapSettings" ) );
    REQUIRE( page != nullptr );
    REQUIRE( EditorKeymapSettings_SetTriggers( page, keymap_section_t::BINDINGS, keymap_platform_t::NONE,
                                             QStringLiteral( "map.viewport.2d" ), QStringLiteral( "map.nudge.right" ), { QStringLiteral( "J" ) } ) );
    REQUIRE( EditorKeymapSettings_SetTriggers( page, keymap_section_t::BINDINGS, keymap_platform_t::NONE,
                                             QStringLiteral( "global" ), QStringLiteral( "tools.settings" ), { QStringLiteral( "Ctrl+Alt+F12" ) } ) );
    CHECK( EditorKeymapSettings_HasChanges( page ) );
    INFO( EditorKeymapSettings_Conflicts( page ).join( QLatin1Char( '\n' ) ).toStdString() );
    REQUIRE( EditorKeymapSettings_Conflicts( page ).isEmpty() );
    CHECK( activeKeymap() == "cypher_default" );
    CHECK( settingsAction->shortcut() == originalShortcut );
    dialog->hide();
    MapViews_SetActivePane( views, 1 );
    QWidget *view = MapViews_PaneView( views, 1 );
    REQUIRE( view != nullptr );
    const math::vec3d_t before = position();
    const usize steps = EditorHistory_StepCount( &ws->history );
    ( void )MasonWorkflowKey( view, Qt::Key_J );
    CheckMasonPosition( position(), before );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps );
    REQUIRE( MasonWorkflowKey( view, Qt::Key_Right ) );
    const math::vec3d_t moved{ before.x + 32, before.y, before.z };
    CheckMasonPosition( position(), moved );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    CheckMasonPosition( position(), before );

    REQUIRE( session.Run( QStringLiteral( "tools.keymap_editor" ) ) == command_result_t::OK );
    REQUIRE( EditorKeymapSettings_Apply( page, QStringLiteral( "mason_workflow_keys" ), QStringLiteral( "Mason workflow keys" ) ) );
    CHECK_FALSE( EditorKeymapSettings_HasChanges( page ) );
    CHECK( activeKeymap() == "mason_workflow_keys" );
    CHECK( preference() == "mason_workflow_keys" );
    INFO( "Settings shortcut: " << settingsAction->shortcut().toString( QKeySequence::PortableText ).toStdString() );
    INFO( "Expected shortcut: " << remappedShortcut.toString( QKeySequence::PortableText ).toStdString() );
    CHECK( settingsAction->shortcut() == remappedShortcut );
    const QAction *nudgeAction = window->findChild<QAction *>( QStringLiteral( "map.nudge.right" ) );
    CHECK( ( nudgeAction == nullptr || nudgeAction->shortcuts().isEmpty() ) ); // Scoped viewport input must never become a window shortcut.
    dialog->hide();
    MapViews_SetActivePane( views, 1 );
    REQUIRE( MasonWorkflowKey( view, Qt::Key_J ) );
    CheckMasonPosition( position(), moved );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1u );
    ( void )MasonWorkflowKey( view, Qt::Key_Right );
    CheckMasonPosition( position(), moved );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    CheckMasonPosition( position(), before );
    REQUIRE( MapWorkspace_Redo( ws ) == editor_history_status_t::OK );
    CheckMasonPosition( position(), moved );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );

    const setting_descriptor_t *keymapSetting = EditorSettings_Find( &gui->settings, StringView_FromCString( "editor.ui.keymap" ) );
    REQUIRE( keymapSetting != nullptr );
    setting_value_t value{};
    value.type = setting_type_t::STRING;
    value.text = StringView_FromCString( "not_installed" );
    REQUIRE( EditorSettings_Write( &gui->settings, settings_scope_t::USER, *keymapSetting, value ) == settings_registry_status_t::OK );
    CHECK( preference() == "not_installed" );
    CHECK( activeKeymap() == "mason_workflow_keys" );
    CHECK( settingsAction->shortcut() == remappedShortcut );
    REQUIRE( MasonWorkflowKey( view, Qt::Key_J ) );
    CheckMasonPosition( position(), moved );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    value.text = StringView_FromCString( "cypher_default" );
    REQUIRE( EditorSettings_Write( &gui->settings, settings_scope_t::USER, *keymapSetting, value ) == settings_registry_status_t::OK );
    CHECK( activeKeymap() == "cypher_default" );
    CHECK( preference() == "cypher_default" );
    CHECK( settingsAction->shortcut() == originalShortcut );
    ( void )MasonWorkflowKey( view, Qt::Key_J );
    CheckMasonPosition( position(), before );
    REQUIRE( MasonWorkflowKey( view, Qt::Key_Right ) );
    CheckMasonPosition( position(), moved );
    CHECK( EditorSelection_Count( &ws->selection ) == 1u );
    CHECK( EditorSelection_At( &ws->selection, 0 ) == id );
}

TEST_CASE( "Capture Mason Settings Keybindings viewport input and orthographic controls", "[.keybindings-settings-screenshot]" )
{
    mason_session_t session;
    REQUIRE( session.Run( QStringLiteral( "tools.keymap_editor" ) ) == command_result_t::OK );
    auto *dialog = Mason_Window( session.pMason )->findChild<QDialog *>( QStringLiteral( "EditorSettingsDialog" ) );
    REQUIRE( dialog != nullptr );
    auto *page = dialog->findChild<QWidget *>( QStringLiteral( "EditorKeymapSettings" ) );
    REQUIRE( page != nullptr );
    REQUIRE( EditorSettingsDialog_CurrentPage( dialog ) == QStringLiteral( "Keybindings" ) );
    REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    auto *rows = page->findChild<QTreeWidget *>( QStringLiteral( "KeymapBindings" ) ); REQUIRE( rows != nullptr );
    auto *advanced = page->findChild<QToolButton *>( QStringLiteral( "KeymapAdvancedToggle" ) ); REQUIRE( advanced != nullptr );
    REQUIRE_FALSE( advanced->isChecked() );
    for ( const QSize size : { QSize( 1045, 753 ), QSize( 1260, 900 ) } ) {
        dialog->resize( size ); QCoreApplication::processEvents();
        REQUIRE( rows->topLevelItemCount() > 20 );
        const int rowHeight = rows->visualItemRect( rows->topLevelItem( 0 ) ).height(); REQUIRE( rowHeight > 0 );
        CHECK( rows->viewport()->height() >= 8 * rowHeight );
        CHECK( dialog->grab().save( QStringLiteral( "artifacts/mason_keybindings_simple_%1x%2.png" ).arg( size.width() ).arg( size.height() ) ) );
        advanced->click(); QCoreApplication::processEvents();
        CHECK( rows->viewport()->height() >= 5 * rowHeight );
        CHECK( dialog->grab().save( QStringLiteral( "artifacts/mason_keybindings_advanced_%1x%2.png" ).arg( size.width() ).arg( size.height() ) ) );
        advanced->click(); QCoreApplication::processEvents();
    }
    dialog->resize( 1560, 1050 );
    EditorKeymapSettings_SetFilter( page, QStringLiteral( "map.viewport" ) );
    QCoreApplication::processEvents();
    REQUIRE_FALSE( EditorKeymapSettings_Rows( page ).isEmpty() );
    CHECK( dialog->grab().save( QStringLiteral( "artifacts/mason_keybindings_settings.png" ) ) );
    EditorKeymapSettings_SetFilter( page, QStringLiteral( "map.nudge" ) );
    QCoreApplication::processEvents();
    int nudges = 0;
    for ( const QString &row : EditorKeymapSettings_Rows( page ) ) {
        const QStringList fields = row.split( QLatin1Char( '\t' ) );
        REQUIRE( fields.size() == 7 );
        if ( fields[1].startsWith( QStringLiteral( "map.nudge." ) ) || fields[1].startsWith( QStringLiteral( "map.nudge_fine." ) ) ) {
            ++nudges;
            CHECK( fields[3] == QStringLiteral( "map.viewport.2d" ) );
            CHECK_FALSE( fields[4].isEmpty() );
        }
    }
    REQUIRE( nudges == 8 );
    CHECK( dialog->grab().save( QStringLiteral( "artifacts/mason_keybindings_nudges.png" ) ) );
    CHECK_FALSE( EditorKeymapSettings_HasChanges( page ) );
    dialog->hide();
}

TEST_CASE( "Capture compact borderless Mason welcome and startup branding", "[.borderless-branding-screenshot]" )
{
    mason_session_t session;
    REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    CHECK( Mason_StartupImage( Mason_Gui( session.pMason )->style ).save( QStringLiteral( "artifacts/mason_borderless_startup.png" ) ) );
    std::unique_ptr<QDialog> welcome( MasonWelcome_Create( Mason_Window( session.pMason ), Mason_Gui( session.pMason )->style, {}, true ) );
    welcome->show(); QCoreApplication::processEvents();
    const auto *logo = welcome->findChild<QLabel *>( QStringLiteral( "MasonWelcomeLogo" ) ); REQUIRE( logo != nullptr );
    CHECK( logo->pixmap().deviceIndependentSize() == QSizeF( 96, 96 ) );
    CHECK( welcome->grab().save( QStringLiteral( "artifacts/mason_borderless_welcome.png" ) ) );
    welcome->hide();
}

TEST_CASE( "Capture Mason compact primitive icons and Quad dimensions", "[.primitive-icons-screenshot]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    REQUIRE( MapWorkspace_New( ws ) == map_status_t::OK );
    REQUIRE( session.Run( QStringLiteral( "map.tool.block" ) ) == command_result_t::OK );
    auto *panel = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "MapToolProperties" ) );
    REQUIRE( panel != nullptr );
    for ( const char *shape : { "box", "quad", "wedge", "cylinder", "spike", "sphere" } ) {
        auto *button = panel->findChild<QToolButton *>( QStringLiteral( "MapPrimitive.%1" ).arg( QString::fromLatin1( shape ) ) );
        REQUIRE( button != nullptr ); REQUIRE_FALSE( button->icon().isNull() );
    }
    auto *quad = panel->findChild<QToolButton *>( QStringLiteral( "MapPrimitive.quad" ) ); quad->click();
    QCoreApplication::processEvents();
    REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    CHECK( panel->grab().save( QStringLiteral( "artifacts/mason_primitive_icons_quad.png" ) ) );
    panel->findChild<QToolButton *>( QStringLiteral( "MapPrimitive.cylinder" ) )->click();
    QCoreApplication::processEvents();
    CHECK( panel->grab().save( QStringLiteral( "artifacts/mason_primitive_icons_cylinder.png" ) ) );
}

TEST_CASE( "Mason clipboard shortcuts copy across maps and keep text editing native", "[mason][smoke][clipboard][input][workflow]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    auto *grid = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( grid != nullptr );
    QWidget *view = MapViews_PaneView( grid, 0 ); REQUIRE( view != nullptr );
    QApplication::clipboard()->clear();
    map_bounds_t bounds{}; MapBounds_AddPoint( bounds, { 0, 0, 0 } ); MapBounds_AddPoint( bounds, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( ws, bounds ) );
    window->activateWindow(); view->setFocus(); QCoreApplication::processEvents();
    CHECK( EditorCommands_Find( &ws->pGui->commands, StringView_FromCString( "edit.paste_special" ) ) != nullptr );
    const usize steps = EditorHistory_StepCount( &ws->history );
    ( void )MasonWorkflowKey( view, Qt::Key_C, Qt::ControlModifier );
    REQUIRE( QApplication::clipboard()->mimeData()->hasFormat( QString::fromLatin1( MAP_CLIPBOARD_MIME ) ) );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps );
    REQUIRE( MapWorkspace_New( ws ) == map_status_t::OK );
    ( void )MasonWorkflowKey( view, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier );
    REQUIRE( ws->wire.objects.nCount == 1 ); CHECK( ws->wire.objects.pData[0].bounds.box.minimum.x == 0 );
    CHECK( ws->elementMode == map_element_mode_t::OBJECTS ); CHECK( ws->tool == map_tool_t::SELECT );
    ( void )MasonWorkflowKey( view, Qt::Key_X, Qt::ControlModifier ); CHECK( ws->wire.objects.nCount == 0 );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK ); CHECK( ws->wire.objects.nCount == 1 );
    REQUIRE( MapWorkspace_Redo( ws ) == editor_history_status_t::OK ); CHECK( ws->wire.objects.nCount == 0 );
    ( void )MasonWorkflowKey( view, Qt::Key_V, Qt::ControlModifier ); CHECK( ws->wire.objects.nCount == 1 );
    const usize objectCount = ws->wire.objects.nCount, mapSteps = EditorHistory_StepCount( &ws->history );
    auto *edit = new QLineEdit( window ); edit->setGeometry( 400, 250, 200, 28 ); edit->show(); edit->setText( QStringLiteral( "native map name" ) ); edit->setFocus(); edit->selectAll();
    QCoreApplication::processEvents(); REQUIRE( QApplication::focusWidget() == edit );
    REQUIRE( session.Run( QStringLiteral( "edit.copy" ) ) == command_result_t::OK );
    CHECK( QApplication::clipboard()->text() == QStringLiteral( "native map name" ) );
    CHECK_FALSE( QApplication::clipboard()->mimeData()->hasFormat( QString::fromLatin1( MAP_CLIPBOARD_MIME ) ) );
    REQUIRE( session.Run( QStringLiteral( "edit.cut" ) ) == command_result_t::OK ); CHECK( edit->text().isEmpty() );
    REQUIRE( session.Run( QStringLiteral( "edit.paste" ) ) == command_result_t::OK ); CHECK( edit->text() == QStringLiteral( "native map name" ) );
    edit->selectAll();
    const auto nativeCopy = QKeySequence::keyBindings( QKeySequence::Copy ); REQUIRE_FALSE( nativeCopy.isEmpty() );
    const auto combination = nativeCopy.front()[0]; ( void )MasonWorkflowKey( edit, static_cast<int>( combination.key() ), combination.keyboardModifiers() );
    CHECK( QApplication::clipboard()->text() == QStringLiteral( "native map name" ) );
    CHECK( ws->wire.objects.nCount == objectCount ); CHECK( EditorHistory_StepCount( &ws->history ) == mapSteps );
    edit->setReadOnly( true ); CHECK( session.Run( QStringLiteral( "edit.cut" ) ) == command_result_t::DISABLED );
    delete edit; view->setFocus(); QCoreApplication::processEvents();
    CHECK( session.Run( QStringLiteral( "edit.paste" ) ) == command_result_t::DISABLED );
    QApplication::clipboard()->clear();
}

TEST_CASE( "Mason Quad Slice is a real contextual toolbar menu and viewport shortcut", "[mason][smoke][quad-slice][input][workflow]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_CreateBox( ws, box ) ); const u64 id = ws->selection.ids.pData[0]; REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
    u64 face = 0;
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) { const auto &f = ws->wire.faces.pData[i]; if ( f.id == id && f.normal.z > .99 ) { face = f.faceId; } }
    REQUIRE( face != 0 ); MapWorkspace_SelectMeshFace( ws, id, face );
    auto *toolbar = window->findChild<QToolBar *>( QStringLiteral( "masonGeometryTools" ) ); REQUIRE( toolbar != nullptr );
    QAction *action = nullptr;
    for ( auto *candidate : toolbar->actions() ) { if ( candidate->objectName() == QStringLiteral( "map.mesh.quad_slice" ) ) { action = candidate; } }
    REQUIRE( action != nullptr ); CHECK_FALSE( action->icon().isNull() ); CHECK( action->isEnabled() );
    auto *grid = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( grid != nullptr );
    auto *camera = MapViews_PaneView( grid, 0 ); REQUIRE( camera != nullptr ); camera->setFocus(); QCoreApplication::processEvents();
    const usize steps = EditorHistory_StepCount( &ws->history );
    CHECK( MasonWorkflowKey( camera, Qt::Key_U, Qt::ControlModifier | Qt::ShiftModifier ) );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1 ); CHECK( ws->wire.faces.nCount == 9 ); CHECK( ws->wire.points.nCount == 13 ); CHECK( ws->selectedMeshFaceId == face );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK ); CHECK( ws->wire.faces.nCount == 6 );
    MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS ); CHECK_FALSE( action->isEnabled() );
    CHECK_FALSE( MasonWorkflowKey( camera, Qt::Key_U, Qt::ControlModifier | Qt::ShiftModifier ) ); CHECK( ws->wire.faces.nCount == 6 );
}

TEST_CASE( "Capture Mason authored Quad Slice grid and tool controls", "[.clipboard-quad-capture]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    window->resize( 2200, 1400 );
    map_bounds_t bounds{}; MapBounds_AddPoint( bounds, { -128, -96, 0 } ); MapBounds_AddPoint( bounds, { 128, 96, 96 } );
    REQUIRE( MapWorkspace_CreateBox( ws, bounds ) ); const u64 id = ws->selection.ids.pData[0]; REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::FULLBRIGHT );
    REQUIRE( session.Run( QStringLiteral( "map.view.center_selection_3d" ) ) == command_result_t::OK );
    u64 face = 0; for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) { const auto &candidate = ws->wire.faces.pData[i]; if ( candidate.id == id && candidate.normal.z > .99 ) { face = candidate.faceId; } }
    REQUIRE( face != 0 ); MapWorkspace_SelectMeshFace( ws, id, face );
    auto *panel = window->findChild<QWidget *>( QStringLiteral( "MapToolProperties" ) ); REQUIRE( panel != nullptr );
    auto *u = panel->findChild<QWidget *>( QStringLiteral( "editor.map.mesh_slice_u" ) );
    auto *v = panel->findChild<QWidget *>( QStringLiteral( "editor.map.mesh_slice_v" ) ); REQUIRE( u ); REQUIRE( v );
    u->findChild<QSpinBox *>()->setValue( 4 ); v->findChild<QSpinBox *>()->setValue( 3 );
    REQUIRE( session.Run( QStringLiteral( "map.mesh.quad_slice" ) ) == command_result_t::OK );
    REQUIRE( ws->wire.faces.nCount == 17 );
    QCoreApplication::processEvents(); REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    CHECK( window->grab().save( QStringLiteral( "artifacts/mason_clipboard_quad_workspace.png" ) ) );
    CHECK( panel->grab().save( QStringLiteral( "artifacts/mason_clipboard_quad_tool_properties.png" ) ) );
}


TEST_CASE( "Content Library map categories share the real inspector and refresh through undo", "[mason][smoke][content-library]" )
{
    mason_session_t session; REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *window = Mason_Window( session.pMason ); auto *ws = Mason_MapWorkspace( session.pMason );
    REQUIRE( session.Run( QStringLiteral( "assets.database" ) ) == command_result_t::OK );
    auto *library = window->findChild<QDialog *>( QStringLiteral( "EditorDatabaseView" ) ); REQUIRE( library != nullptr );
    CHECK( library->windowTitle() == QStringLiteral( "Content Library" ) );
    CHECK( EditorDatabaseView_Tab( library ) == DATABASE_TAB_ENTITIES );
    auto *tabs = library->findChild<QTabWidget *>( QStringLiteral( "DatabaseTabs" ) ); REQUIRE( tabs != nullptr );
    REQUIRE( tabs->count() == DATABASE_TAB_COUNT );
    for ( int tab = 0; tab < tabs->count(); ++tab ) { CAPTURE( tab ); CHECK_FALSE( tabs->tabIcon( tab ).isNull() ); }
    EditorDatabaseView_SetTab( library, DATABASE_TAB_TRIGGERS );
    REQUIRE( EditorDatabaseView_OpenEntity( library, 110u ) );
    CHECK( EditorDatabaseView_Tab( library ) == DATABASE_TAB_TRIGGERS );
    auto *manager = window->findChild<ads::CDockManager *>(); REQUIRE( manager != nullptr );
    auto *properties = manager->findDockWidget( QString::fromLatin1( MAP_PANEL_PROPERTIES ) ); REQUIRE( properties != nullptr );
    properties->toggleView( false ); REQUIRE( properties->isClosed() );
    REQUIRE( session.Run( QStringLiteral( "map.go_to spawn_a" ) ) == command_result_t::OK );
    auto *page = library->findChild<QWidget *>( QStringLiteral( "DatabasePage%1" ).arg( DATABASE_TAB_TRIGGERS ) ); REQUIRE( page != nullptr );
    auto *select = page->findChild<QPushButton *>( QStringLiteral( "DatabaseSelectEntity" ) ); REQUIRE( select != nullptr );
    REQUIRE( select->isEnabled() ); select->click(); QCoreApplication::processEvents();
    CHECK( EditorSelection_Contains( &ws->selection, 110u ) );
    CHECK_FALSE( properties->isClosed() ); CHECK( properties->dockAreaWidget()->currentDockWidget() == properties );
    const usize history = EditorHistory_StepCount( &ws->history );
    key_value_document_desc_t desc{}; desc.pAllocator = Allocator_GetSystem();
    auto *value = KeyValue_CreateDocument( desc ); REQUIRE( value != nullptr );
    REQUIRE( KeyValue_SetString( value, KeyValue_Root( value ), StringView_FromCString( "scripts/arena/wave.cfg" ) ) );
    const bool edited = MapWorkspace_SetEntityProperty( ws, StringView_FromCString( "script_path" ), KeyValue_Root( value ) );
    KeyValue_DestroyDocument( value ); REQUIRE( edited ); CHECK( EditorHistory_StepCount( &ws->history ) == history + 1 );
    EditorDatabaseView_SetTab( library, DATABASE_TAB_SCRIPTS ); CHECK( EditorDatabaseView_Library( library ).contains( QStringLiteral( "#110" ) ) );
    REQUIRE( EditorDatabaseView_OpenEntity( library, 110u ) );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views != nullptr );
    MapViews_SetPaneType( views, 2, map_view_type_t::DATABASE ); auto *embedded = MapViews_PaneView( views, 2 ); REQUIRE( embedded != nullptr );
    CHECK( MapViews_TypeTitle( map_view_type_t::DATABASE ) == QStringLiteral( "Content Library" ) );
    CHECK( EditorDatabaseView_Tab( embedded ) == DATABASE_TAB_ENTITIES );
    EditorDatabaseView_SetTab( embedded, DATABASE_TAB_SCRIPTS ); REQUIRE( EditorDatabaseView_OpenEntity( embedded, 110u ) );
    REQUIRE( MapWorkspace_Undo( ws ) == editor_history_status_t::OK );
    CHECK_FALSE( EditorDatabaseView_Library( library ).contains( QStringLiteral( "#110" ) ) );
    CHECK_FALSE( EditorDatabaseView_Library( embedded ).contains( QStringLiteral( "#110" ) ) );
    CHECK( EditorDatabaseView_Current( library ).isEmpty() ); CHECK( EditorDatabaseView_Current( embedded ).isEmpty() );
    REQUIRE( MapWorkspace_Redo( ws ) == editor_history_status_t::OK );
    CHECK( EditorDatabaseView_Library( library ).contains( QStringLiteral( "#110" ) ) );
    CHECK( EditorDatabaseView_Library( embedded ).contains( QStringLiteral( "#110" ) ) );
    REQUIRE( MapWorkspace_New( ws ) == map_status_t::OK );
    CHECK( EditorDatabaseView_Library( library ).isEmpty() ); CHECK( EditorDatabaseView_Library( embedded ).isEmpty() );
    library->hide();
}

TEST_CASE( "Capture Mason Content Library and clean entity labels", "[.content-library-capture]" )
{
    mason_session_t session; REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *window = Mason_Window( session.pMason ); window->resize( 2000, 1250 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::FULLBRIGHT );
    auto *clean = views->findChild<QAction *>( QStringLiteral( "EditorViewClean2D" ) ); REQUIRE( clean != nullptr ); clean->trigger();
    REQUIRE( session.Run( QStringLiteral( "map.go_to wave1_trigger" ) ) == command_result_t::OK );
    QCoreApplication::processEvents(); REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    CHECK( window->grab().save( QStringLiteral( "artifacts/mason_content_library_clean_workspace.png" ) ) );
    REQUIRE( session.Run( QStringLiteral( "assets.database #110" ) ) == command_result_t::OK );
    auto *library = window->findChild<QDialog *>( QStringLiteral( "EditorDatabaseView" ) ); REQUIRE( library != nullptr );
    library->resize( 1360, 880 ); EditorDatabaseView_SetTab( library, DATABASE_TAB_TRIGGERS ); REQUIRE( EditorDatabaseView_OpenEntity( library, 110u ) );
    QCoreApplication::processEvents(); CHECK( library->grab().save( QStringLiteral( "artifacts/mason_content_library_triggers.png" ) ) );
    EditorDatabaseView_SetTab( library, DATABASE_TAB_ENTITIES ); REQUIRE( EditorDatabaseView_OpenEntity( library, 110u ) );
    QCoreApplication::processEvents(); CHECK( library->grab().save( QStringLiteral( "artifacts/mason_content_library_entities.png" ) ) );
    library->hide();
}

TEST_CASE( "Capture Mason clean RGB dimensions and direct face controls", "[.selection-clarity-capture]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    window->resize( 2200, 1400 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::PERSPECTIVE );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::FULLBRIGHT );
    MapWorkspace_SetGridSize( ws, 64 );
    map_bounds_t cube{}; MapBounds_AddPoint( cube, { 0, 0, 0 } ); MapBounds_AddPoint( cube, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_CreateBox( ws, cube ) );
    const u64 cubeId = ws->selection.ids.pData[0];
    MapWorkspace_SetTool( ws, map_tool_t::SELECT ); MapWorkspace_Frame( ws, CY_FALSE ); QCoreApplication::processEvents();
    auto *camera = MapViews_PaneView( views, 0 ); REQUIRE( camera != nullptr );
    std::filesystem::create_directories( "artifacts" );
    REQUIRE( camera->grab().save( QStringLiteral( "artifacts/mason_clean_block_view.png" ) ) );
    REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_clean_block_workspace.png" ) ) );
    u64 side = 0;
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) {
        const auto &face = ws->wire.faces.pData[i]; if ( face.id == cubeId && face.normal.z > .99 ) { side = face.sideId; break; }
    }
    REQUIRE( side != 0 ); MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES ); MapWorkspace_SelectBrushFace( ws, cubeId, side );
    QCoreApplication::processEvents(); REQUIRE( ws->tool == map_tool_t::SELECT );
    const auto faceBounds = MapViews_SelectionGeometryBounds( ws );
    CHECK( faceBounds.box.minimum.z == faceBounds.box.maximum.z ); CHECK( faceBounds.box.maximum.x - faceBounds.box.minimum.x == 128 );
    REQUIRE( camera->grab().save( QStringLiteral( "artifacts/mason_clean_face_view.png" ) ) );
    REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_clean_face_workspace.png" ) ) );
    MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
    map_bounds_t wall{}; MapBounds_AddPoint( wall, { 320, 0, 0 } ); MapBounds_AddPoint( wall, { 384, 512, 960 } );
    REQUIRE( MapWorkspace_CreateBox( ws, wall ) ); MapWorkspace_Frame( ws, CY_TRUE ); QCoreApplication::processEvents();
    REQUIRE( camera->grab().save( QStringLiteral( "artifacts/mason_clean_wall_view.png" ) ) );
}

TEST_CASE( "Capture complete brush volume during Mason face Push Pull", "[.face-volume-preview-capture]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    window->resize( 1600, 1050 );
    map_bounds_t cube{}; MapBounds_AddPoint( cube, { -128, -128, 0 } ); MapBounds_AddPoint( cube, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_CreateBox( ws, cube ) ); const u64 id = EditorSelection_At( &ws->selection, 0 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneType( views, 1, map_view_type_t::TOP ); MapViews_SetPaneType( views, 2, map_view_type_t::FRONT );
    MapCameraView_SetRenderMode( MapViews_PaneView( views, 0 ), map_render_mode_t::FULLBRIGHT );
    MapWorkspace_SetTool( ws, map_tool_t::SELECT ); MapWorkspace_Frame( ws, CY_TRUE );
    u64 side = 0;
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) {
        const auto &face = ws->wire.faces.pData[i]; if ( face.id == id && face.normal.z > .99 ) { side = face.sideId; break; }
    }
    REQUIRE( side != 0 ); MapWorkspace_SelectBrushFace( ws, id, side ); MapWorkspace_SetGridSize( ws, 64 );
    QCoreApplication::processEvents(); REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    auto *camera = MapViews_PaneView( views, 0 ); REQUIRE( camera != nullptr );
    CHECK( window->grab().save( QStringLiteral( "artifacts/mason_face_volume_before.png" ) ) );
    const auto *document = ws->pDocument; const usize steps = EditorHistory_StepCount( &ws->history );
    MapWorkspace_SetFacePreview( ws, 64 ); REQUIRE( MapWorkspace_HasFacePreview( ws ) );
    REQUIRE( ws->editPreview.status == map_status_t::OK ); CHECK( ws->editPreview.bounds.box.maximum.z == 192 );
    CHECK( ws->pDocument == document ); CHECK( EditorHistory_StepCount( &ws->history ) == steps );
    QCoreApplication::processEvents();
    CHECK( window->grab().save( QStringLiteral( "artifacts/mason_face_volume_preview_workspace.png" ) ) );
    CHECK( camera->grab().save( QStringLiteral( "artifacts/mason_face_volume_preview_camera.png" ) ) );
    REQUIRE( MapWorkspace_CommitFacePreview( ws ) ); CHECK_FALSE( MapWorkspace_HasFacePreview( ws ) );
    QCoreApplication::processEvents();
    CHECK( window->grab().save( QStringLiteral( "artifacts/mason_face_volume_committed.png" ) ) );
    REQUIRE( session.Run( QStringLiteral( "edit.undo" ) ) == command_result_t::OK );
    CHECK( MapWireframe_FindObject( ws->wire, id )->bounds.box.maximum.z == 128 );
}

TEST_CASE( "Mason reports mesh edge selection and dispatches topology queries without parent edits", "[mason][smoke][mesh-edge]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    window->resize( 1600, 1050 );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, 0 } ); MapBounds_AddPoint( box, { 128, 128, 256 } );
    REQUIRE( MapWorkspace_CreateBox( ws, box ) ); const u64 id = EditorSelection_At( &ws->selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::FOUR ); MapViews_SetPaneType( views, 1, map_view_type_t::TOP );
    MapWorkspace_Frame( ws, CY_TRUE ); QCoreApplication::processEvents();
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.edges" ) ) == command_result_t::OK );
    auto *top = MapViews_PaneView( views, 1 ); REQUIRE( top != nullptr );
    const QPointF point = MapOrthoView_WorldToView( top, { 128, 0 } ); map_mesh_edge_hit_t hit{};
    REQUIRE( MapOrthoView_PickMeshEdge( top, point, &hit ) ); REQUIRE( hit.object == id );
    REQUIRE( MapWorkspace_SelectMeshEdge( ws, id, hit.edge ) );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "1 edge selected" ) ) );
    auto *size = window->findChild<QLabel *>( QStringLiteral( "masonStatusSize" ) ); REQUIRE( size );
    CHECK( size->text().isEmpty() );
    const auto *document = ws->pDocument; const usize steps = EditorHistory_StepCount( &ws->history );
    REQUIRE( session.Run( QStringLiteral( "map.select.ring" ) ) == command_result_t::OK );
    CHECK( ws->meshSelection.edges.nCount == 4u );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "4 edges selected" ) ) );
    CHECK( session.Run( QStringLiteral( "edit.delete" ) ) == command_result_t::DISABLED );
    CHECK( ws->pDocument == document ); CHECK( EditorHistory_StepCount( &ws->history ) == steps );
    MapWorkspace_SetTool( ws, map_tool_t::NONE );
    CHECK( ws->meshSelection.edges.nCount == 4u ); CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "4 edges selected" ) ) );
    MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
    CHECK( ws->meshSelection.edges.nCount == 0u ); CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "1 selected" ) ) );
}

TEST_CASE( "Capture Mason authored mesh edge editing", "[.mesh-edge-capture]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    window->resize( 1600, 1050 );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, 0 } ); MapBounds_AddPoint( box, { 128, 128, 256 } );
    REQUIRE( MapWorkspace_CreateBox( ws, box ) ); const u64 id = EditorSelection_At( &ws->selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER ); MapViews_SetPaneType( views, 1, map_view_type_t::TOP );
    MapCameraView_SetRenderMode( MapViews_PaneView( views, 0 ), map_render_mode_t::FULLBRIGHT );
    MapWorkspace_Frame( ws, CY_TRUE ); REQUIRE( session.Run( QStringLiteral( "map.select_mode.edges" ) ) == command_result_t::OK );
    QCoreApplication::processEvents(); auto *top = MapViews_PaneView( views, 1 ); REQUIRE( top != nullptr );
    const QPointF point = MapOrthoView_WorldToView( top, { 128, 0 } ); map_mesh_edge_hit_t hit{};
    REQUIRE( MapOrthoView_PickMeshEdge( top, point, &hit ) ); REQUIRE( hit.object == id );
    REQUIRE( MapWorkspace_SelectMeshEdge( ws, id, hit.edge ) ); REQUIRE( MapWorkspace_SelectMeshEdgeRing( ws ) );
    QCoreApplication::processEvents(); QDir().mkpath( QStringLiteral( "artifacts" ) );
    REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_mesh_edge_workspace.png" ) ) );
    auto *panel = window->findChild<QWidget *>( QStringLiteral( "MapToolProperties" ) ); REQUIRE( panel != nullptr );
    REQUIRE( panel->grab().save( QStringLiteral( "artifacts/mason_mesh_edge_properties.png" ) ) );
}

TEST_CASE( "Mason reports mesh vertex selection without exposing parent dimensions or edits", "[mason][smoke][mesh-vertex]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    window->resize( 1600, 1050 );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, 0 } ); MapBounds_AddPoint( box, { 128, 128, 256 } );
    REQUIRE( MapWorkspace_CreateBox( ws, box ) ); const u64 id = EditorSelection_At( &ws->selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    MapViews_SetArrangement( views, map_view_arrangement_t::FOUR ); MapViews_SetPaneType( views, 1, map_view_type_t::TOP );
    MapWorkspace_Frame( ws, CY_TRUE ); QCoreApplication::processEvents();
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.vertices" ) ) == command_result_t::OK );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "No vertices selected" ) ) );
    auto *size = window->findChild<QLabel *>( QStringLiteral( "masonStatusSize" ) ); REQUIRE( size ); CHECK( size->text().isEmpty() );
    auto *top = MapViews_PaneView( views, 1 ); REQUIRE( top );
    map_mesh_vertex_hit_t first{}, second{};
    REQUIRE( MapOrthoView_PickMeshVertex( top, MapOrthoView_WorldToView( top, { -128, -128 } ), &first ) );
    REQUIRE( MapOrthoView_PickMeshVertex( top, MapOrthoView_WorldToView( top, { 128, 128 } ), &second ) );
    REQUIRE( first.object == id ); REQUIRE( second.object == id ); REQUIRE( first.vertex.value != second.vertex.value );
    REQUIRE( MapWorkspace_SelectMeshVertex( ws, id, first.vertex ) );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "1 vertex selected" ) ) );
    REQUIRE( MapWorkspace_SelectMeshVertex( ws, id, second.vertex, MAP_SELECT_ADD ) );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "2 vertices selected" ) ) ); CHECK( size->text().isEmpty() );
    const auto *document = ws->pDocument; const usize steps = EditorHistory_StepCount( &ws->history );
    for ( const char *command : { "edit.delete", "edit.duplicate", "map.mesh.merge", "map.mesh.collapse", "map.mesh.bevel" } ) {
        CAPTURE( command ); CHECK( session.Run( QString::fromLatin1( command ) ) == command_result_t::DISABLED );
    }
    CHECK( ws->pDocument == document ); CHECK( EditorHistory_StepCount( &ws->history ) == steps );
    MapWorkspace_SetTool( ws, map_tool_t::NONE );
    CHECK( MapWorkspace_HasMeshVertices( ws ) ); CHECK( ws->meshSelection.vertices.nCount == 2 );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "2 vertices selected" ) ) ); CHECK( size->text().isEmpty() );
    MapWorkspace_SetTool( ws, map_tool_t::SELECT );
    MapWorkspace_ClearMeshVertices( ws );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "No vertices selected" ) ) ); CHECK( size->text().isEmpty() );
    REQUIRE( MapWorkspace_SelectMeshVertex( ws, id, first.vertex ) );
    MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
    CHECK( ws->meshSelection.vertices.nCount == 0 );
    CHECK( Mason_StatusText( session.pMason ).contains( QStringLiteral( "1 selected" ) ) ); CHECK_FALSE( size->text().isEmpty() );
}

TEST_CASE( "Capture Mason authored mesh vertex editing", "[.mesh-vertex-capture]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    window->resize( 1600, 1050 );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, 0 } ); MapBounds_AddPoint( box, { 128, 128, 256 } );
    REQUIRE( MapWorkspace_CreateBox( ws, box ) ); const u64 id = EditorSelection_At( &ws->selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER ); MapViews_SetPaneType( views, 1, map_view_type_t::TOP );
    MapCameraView_SetRenderMode( MapViews_PaneView( views, 0 ), map_render_mode_t::FULLBRIGHT );
    MapWorkspace_Frame( ws, CY_TRUE ); REQUIRE( session.Run( QStringLiteral( "map.select_mode.vertices" ) ) == command_result_t::OK );
    QCoreApplication::processEvents(); auto *top = MapViews_PaneView( views, 1 ); REQUIRE( top );
    for ( const math::vec2d_t corner : { math::vec2d_t{ -128, -128 }, math::vec2d_t{ 128, 128 } } ) {
        map_mesh_vertex_hit_t hit{};
        REQUIRE( MapOrthoView_PickMeshVertex( top, MapOrthoView_WorldToView( top, QPointF( corner.x, corner.y ) ), &hit ) ); REQUIRE( hit.object == id );
        REQUIRE( MapWorkspace_SelectMeshVertex( ws, id, hit.vertex, MAP_SELECT_ADD ) );
    }
    REQUIRE( ws->meshSelection.vertices.nCount == 2 );
    QCoreApplication::processEvents(); QDir().mkpath( QStringLiteral( "artifacts" ) );
    REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_mesh_vertex_workspace.png" ) ) );
    auto *panel = window->findChild<QWidget *>( QStringLiteral( "MapToolProperties" ) ); REQUIRE( panel );
    REQUIRE( panel->grab().save( QStringLiteral( "artifacts/mason_mesh_vertex_properties.png" ) ) );
}

TEST_CASE( "Mason flight speed defaults execute only in the camera and appear in editable keybindings", "[mason][smoke][camera-speed]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *window = Mason_Window( session.pMason );
    auto *gui = Mason_Gui( session.pMason );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    auto *camera = MapViews_PaneView( views, 0 ); REQUIRE( camera );
    auto *top = MapViews_PaneView( views, 1 ); REQUIRE( top );
    const auto *descriptor = EditorSettings_Find( &gui->settings, StringView_FromCString( "editor.camera.move_speed" ) ); REQUIRE( descriptor );
    setting_value_t value{}; value.type = setting_type_t::REAL; value.flValue = 1000.0;
    REQUIRE( EditorSettings_Write( &gui->settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    const auto speed = [&]() { return EditorSettings_Real( &gui->settings, "editor.camera.move_speed", 0.0 ); };
    const auto *document = ws->pDocument;
    const auto selectionRevision = ws->selection.revision;
    const auto steps = EditorHistory_StepCount( &ws->history );
    MapViews_SetActivePane( views, 0 );
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_Equal ) ); CHECK( speed() == 2000.0 );
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_0 ) ); CHECK( speed() == 1000.0 );
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_Minus ) ); CHECK( speed() == 500.0 );
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_0 ) ); CHECK( speed() == 1000.0 );
    MapViews_SetActivePane( views, 1 );
    ( void )MasonWorkflowKey( top, Qt::Key_Equal ); CHECK( speed() == 1000.0 );
    QLineEdit text( window ); text.setText( QStringLiteral( "camera" ) ); text.show(); text.setFocus();
    ( void )MasonWorkflowKey( &text, Qt::Key_Minus ); CHECK( speed() == 1000.0 );
    CHECK( ws->pDocument == document ); CHECK( ws->selection.revision == selectionRevision );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps );

    REQUIRE( session.Run( QStringLiteral( "tools.keymap_editor" ) ) == command_result_t::OK );
    auto *page = window->findChild<QWidget *>( QStringLiteral( "EditorKeymapSettings" ) ); REQUIRE( page );
    const auto rows = EditorKeymapSettings_Rows( page );
    for ( const QString id : { QStringLiteral( "map.camera.speed_increase" ), QStringLiteral( "map.camera.speed_decrease" ), QStringLiteral( "map.camera.speed_reset" ) } ) {
        CAPTURE( id.toStdString() );
        bool found = false;
        for ( const QString &row : rows ) {
            const auto columns = row.split( QLatin1Char( '\t' ) );
            if ( columns.size() >= 5 && columns[1] == id && columns[3] == QStringLiteral( "map.viewport.3d" ) ) {
                found = true; CHECK_FALSE( columns[4].isEmpty() );
            }
        }
        CHECK( found );
    }
    REQUIRE( EditorKeymapSettings_SetTriggers( page, keymap_section_t::BINDINGS, keymap_platform_t::NONE,
        QStringLiteral( "map.viewport.3d" ), QStringLiteral( "map.camera.speed_increase" ), { QStringLiteral( "J" ) } ) );
    CHECK( EditorKeymapSettings_HasChanges( page ) );
    CHECK( EditorKeymapSettings_Conflicts( page ).isEmpty() );
    EditorKeymapSettings_Revert( page );
}

TEST_CASE( "Mason navigation shortcuts open Go To and return the view to the full map", "[mason][smoke][camera-speed][navigation]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *window = Mason_Window( session.pMason );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    auto *camera = MapViews_PaneView( views, 0 ); REQUIRE( camera );
    const auto *document = ws->pDocument;
    const auto steps = EditorHistory_StepCount( &ws->history );
    MapViews_SetActivePane( views, 0 );
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier ) );
    auto *dialog = window->findChild<QDialog *>( QStringLiteral( "MapGoToDialog" ) ); REQUIRE( dialog );
    auto *input = dialog->findChild<QLineEdit *>( QStringLiteral( "MapGoToInput" ) ); REQUIRE( input );
    CHECK( dialog->isVisible() ); input->setText( QStringLiteral( "spawn_a" ) );
    ( void )MasonWorkflowKey( input, Qt::Key_Return );
    REQUIRE( EditorSelection_Count( &ws->selection ) == 1u );
    const auto *entity = MapWireframe_FindEntity( ws->wire, EditorSelection_At( &ws->selection, 0 ) ); REQUIRE( entity );
    REQUIRE( ws->frameBounds.bHas );
    dialog->hide(); MapViews_SetActivePane( views, 0 );
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_Home ) );
    CHECK( ws->frameBounds.box.minimum.x == ws->wire.bounds.box.minimum.x );
    CHECK( ws->frameBounds.box.maximum.z == ws->wire.bounds.box.maximum.z );
    CHECK( ws->frameTarget == map_frame_target_t::ALL );
    CHECK( ws->pDocument == document ); CHECK( EditorHistory_StepCount( &ws->history ) == steps );
}

TEST_CASE( "Mason shifted camera flight keeps editing tool actions idle until navigation releases", "[mason][smoke][camera-shift][navigation]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    auto *camera = MapViews_PaneView( views, 0 ); REQUIRE( camera );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_CreateBox( ws, box ) );
    const auto *document = ws->pDocument; const auto steps = EditorHistory_StepCount( &ws->history );
    const auto selectionRevision = ws->selection.revision;
    MapViews_SetActivePane( views, 0 );
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_Escape ) ); REQUIRE( ws->tool == map_tool_t::NONE );
    const QPointF point = camera->rect().center();
    const auto press = [&]( int code ) {
        // Qt toggles a modifier key's own bit to expose its post-event state.
        const auto modifiers = code == Qt::Key_Shift ? Qt::NoModifier : Qt::ShiftModifier;
        QKeyEvent preflight( QEvent::ShortcutOverride, code, modifiers ); preflight.ignore();
        QCoreApplication::sendEvent( camera, &preflight ); REQUIRE( preflight.isAccepted() );
        QKeyEvent down( QEvent::KeyPress, code, modifiers ); REQUIRE( down.modifiers() == Qt::ShiftModifier );
        QCoreApplication::sendEvent( camera, &down );
        REQUIRE( down.isAccepted() );
    };
    const auto release = [&]( int code ) {
        QKeyEvent up( QEvent::KeyRelease, code, Qt::ShiftModifier );
        REQUIRE( up.modifiers() == ( code == Qt::Key_Shift ? Qt::NoModifier : Qt::ShiftModifier ) );
        QCoreApplication::sendEvent( camera, &up );
    };
    // Start with Shift already held, then capture look before the first direction.
    press( Qt::Key_Shift );
    QMouseEvent look( QEvent::MouseButtonPress, point, camera->mapToGlobal( point ),
        Qt::RightButton, Qt::RightButton, Qt::ShiftModifier );
    QCoreApplication::sendEvent( camera, &look );
    int lookSteps = 0;
    for ( const int code : { Qt::Key_S, Qt::Key_D, Qt::Key_E } ) {
        CAPTURE( code ); press( code );
        CHECK( ws->tool == map_tool_t::NONE );
        const auto velocity = MapCameraView_NavigationVelocity( camera, Qt::ShiftModifier );
        CHECK( velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z > 0.0 );
        release( code );
        const auto before = MapCameraView_Forward( camera );
        const QPointF next = point + QPointF( 8 * ++lookSteps, 0 );
        QMouseEvent turn( QEvent::MouseMove, next, camera->mapToGlobal( next ),
            Qt::NoButton, Qt::RightButton, Qt::ShiftModifier ); QCoreApplication::sendEvent( camera, &turn );
        const auto after = MapCameraView_Forward( camera );
        CHECK( before.x != after.x );
    }
    auto *palette = window->findChild<QWidget *>( QStringLiteral( "EditorToolPalette" ) ); REQUIRE( palette );
    for ( const auto *button : palette->findChildren<QToolButton *>() ) {
        const auto *action = button->defaultAction(); REQUIRE( action );
        if ( action->objectName().startsWith( QStringLiteral( "map.tool." ) ) && action->isCheckable() ) {
            CAPTURE( action->objectName().toStdString() ); CHECK_FALSE( action->isChecked() ); CHECK_FALSE( button->isChecked() );
        }
    }
    QMouseEvent stop( QEvent::MouseButtonRelease, point, camera->mapToGlobal( point ),
        Qt::RightButton, Qt::NoButton, Qt::ShiftModifier ); QCoreApplication::sendEvent( camera, &stop );
    release( Qt::Key_Shift );
    const auto stopped = MapCameraView_Forward( camera );
    MasonWorkflowMouse( camera, QEvent::MouseMove, point + QPointF( 40, 0 ), Qt::NoButton );
    CHECK( MapCameraView_Forward( camera ).x == stopped.x );
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_S, Qt::ShiftModifier ) );
    CHECK( ws->tool == map_tool_t::SELECT );
    const auto *select = window->findChild<QAction *>( QStringLiteral( "map.tool.select" ) ); REQUIRE( select );
    CHECK( select->isChecked() );
    CHECK( ws->pDocument == document ); CHECK( ws->selection.revision == selectionRevision );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps );
}

TEST_CASE( "Capture Mason camera navigation and speed feedback", "[mason][smoke][.camera-speed-capture]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    window->resize( 1600, 1050 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    auto *camera = MapViews_PaneView( views, 0 ); REQUIRE( camera );
    MapCameraView_SetRenderMode( camera, map_render_mode_t::FULLBRIGHT );
    MapWorkspace_SetTool( ws, map_tool_t::CAMERA ); MapViews_SetActivePane( views, 0 );
    QCoreApplication::processEvents();
    REQUIRE( MasonWorkflowKey( camera, Qt::Key_Equal ) );
    QCoreApplication::processEvents(); REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_camera_navigation_workspace.png" ) ) );
    auto *panel = window->findChild<QWidget *>( QStringLiteral( "MapToolProperties" ) ); REQUIRE( panel );
    REQUIRE( panel->grab().save( QStringLiteral( "artifacts/mason_camera_navigation_properties.png" ) ) );
}

namespace
{
QWidget *MasonSeparatedOrthoScene( mason_session_t &session )
{
    auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    window->resize( 1600, 1050 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER ); MapViews_SetPaneType( views, 1, map_view_type_t::TOP );
    map_bounds_t context{}, selected{};
    MapBounds_AddPoint( context, { -1024, -1024, -16 } ); MapBounds_AddPoint( context, { 1024, 1024, 0 } );
    MapBounds_AddPoint( selected, { 0, 0, 0 } ); MapBounds_AddPoint( selected, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( ws, context ) ); REQUIRE( MapWorkspace_CreateBox( ws, selected ) );
    MapWorkspace_SetGridSize( ws, 64 ); MapWorkspace_SetSnapToGrid( ws, true );
    MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS ); MapWorkspace_SetTool( ws, map_tool_t::SELECT );
    MapWorkspace_Frame( ws, CY_FALSE ); MapViews_SetActivePane( views, 1 ); QCoreApplication::processEvents();
    auto *top = MapViews_PaneView( views, 1 ); REQUIRE( top != nullptr );
    REQUIRE( top->isVisible() ); REQUIRE( 64.0 * MapOrthoView_Zoom( top ) < 24.0 );
    REQUIRE( 64.0 * MapOrthoView_Zoom( top ) > 3.0 );
    return top;
}

QPointF MasonOrthoMovePickup( QWidget *top, const map_workspace_t &ws )
{
    const auto center = MapBounds_Center( MapViews_SelectionGeometryBounds( &ws ) );
    const f64 pixels = 64.0 * EditorSettings_Real( &ws.pGui->settings, "editor.viewport.gizmo_scale", 1.0 );
    return MapOrthoView_WorldToView( top, { center.x, center.y } ) + QPointF( pixels, 0 );
}
}

TEST_CASE( "Mason zoomed out 2D move arrows and resize squares are separate reversible grid controls", "[mason][smoke][ortho-gizmo-separation][gizmos]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason );
    QWidget *top = MasonSeparatedOrthoScene( session );
    const u64 selected = EditorSelection_At( &ws->selection, 0u ); const usize steps = EditorHistory_StepCount( &ws->history );
    const auto token = UndoRedo_StateToken( ws->history.pUndo );
    const auto original = MapViews_SelectionGeometryBounds( ws );
    const auto bounds = [&]() {
        const auto *object = MapWireframe_FindObject( ws->wire, selected ); REQUIRE( object != nullptr ); return object->bounds;
    };
    const QPointF move = MasonOrthoMovePickup( top, *ws ), resize = move + QPointF( 24, 0 );
    REQUIRE( top->rect().contains( move.toPoint() ) ); REQUIRE( top->rect().contains( resize.toPoint() ) );
    for ( const auto &target : { std::pair{ move, Qt::SizeAllCursor }, std::pair{ resize, Qt::SizeHorCursor } } ) {
        MasonWorkflowMouse( top, QEvent::MouseMove, target.first, Qt::NoButton ); CHECK( top->cursor().shape() == target.second );
        MasonWorkflowMouse( top, QEvent::MouseButtonPress, target.first, Qt::LeftButton, Qt::LeftButton );
        CHECK_FALSE( ws->editPreview.bActive );
        MasonWorkflowMouse( top, QEvent::MouseButtonRelease, target.first, Qt::LeftButton );
        CHECK_FALSE( ws->editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws->history ) == steps );
        CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws->history.pUndo ) ) );
    }
    const QPointF travel( ws->gridSize * MapOrthoView_Zoom( top ), 0 );
    const auto *beforeMove = ws->pDocument;
    MasonWorkflowMouse( top, QEvent::MouseButtonPress, move, Qt::LeftButton, Qt::LeftButton );
    MasonWorkflowMouse( top, QEvent::MouseMove, move + travel, Qt::NoButton, Qt::LeftButton );
    REQUIRE( ws->editPreview.bActive ); REQUIRE( ws->editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
    CHECK_FALSE( ws->editPreview.transform.bResize ); CheckMasonPosition( ws->editPreview.transform.delta, { 64, 0, 0 } );
    CHECK( ws->pDocument == beforeMove ); CHECK( EditorHistory_StepCount( &ws->history ) == steps );
    CheckMasonPosition( bounds().box.minimum, original.box.minimum ); CheckMasonPosition( bounds().box.maximum, original.box.maximum );
    MasonWorkflowMouse( top, QEvent::MouseButtonRelease, move + travel, Qt::LeftButton );
    CHECK_FALSE( ws->editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1u );
    CheckMasonPosition( bounds().box.minimum, { 64, 0, 0 } ); CheckMasonPosition( bounds().box.maximum, { 128, 64, 64 } );
    REQUIRE( session.Run( QStringLiteral( "edit.undo" ) ) == command_result_t::OK );
    CheckMasonPosition( bounds().box.minimum, original.box.minimum ); CheckMasonPosition( bounds().box.maximum, original.box.maximum );
    REQUIRE( session.Run( QStringLiteral( "edit.redo" ) ) == command_result_t::OK );
    const auto moved = bounds(); const auto *beforeResize = ws->pDocument;
    const QPointF resizeMoved = MasonOrthoMovePickup( top, *ws ) + QPointF( 24, 0 );
    MasonWorkflowMouse( top, QEvent::MouseMove, resizeMoved, Qt::NoButton ); CHECK( top->cursor().shape() == Qt::SizeHorCursor );
    MasonWorkflowMouse( top, QEvent::MouseButtonPress, resizeMoved, Qt::LeftButton, Qt::LeftButton );
    MasonWorkflowMouse( top, QEvent::MouseMove, resizeMoved + travel, Qt::NoButton, Qt::LeftButton );
    REQUIRE( ws->editPreview.bActive ); REQUIRE( ws->editPreview.transform.kind == map_transform_preview_kind_t::SCALE );
    REQUIRE( ws->editPreview.transform.bResize ); CHECK_FALSE( ws->editPreview.transform.bResizeFromCenter );
    CHECK( ws->editPreview.transform.pivot.x == moved.box.minimum.x );
    CheckMasonPosition( ws->editPreview.bounds.box.minimum, moved.box.minimum );
    CheckMasonPosition( ws->editPreview.bounds.box.maximum, { 192, 64, 64 } );
    CHECK( ws->pDocument == beforeResize ); CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1u );
    MasonWorkflowMouse( top, QEvent::MouseButtonRelease, resizeMoved + travel, Qt::LeftButton );
    CHECK_FALSE( ws->editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws->history ) == steps + 2u );
    CheckMasonPosition( bounds().box.minimum, moved.box.minimum ); CheckMasonPosition( bounds().box.maximum, { 192, 64, 64 } );
    REQUIRE( session.Run( QStringLiteral( "edit.undo" ) ) == command_result_t::OK );
    CheckMasonPosition( bounds().box.minimum, moved.box.minimum ); CheckMasonPosition( bounds().box.maximum, moved.box.maximum );
    REQUIRE( session.Run( QStringLiteral( "edit.undo" ) ) == command_result_t::OK );
    CheckMasonPosition( bounds().box.minimum, original.box.minimum ); CheckMasonPosition( bounds().box.maximum, original.box.maximum );
    REQUIRE( session.Run( QStringLiteral( "edit.redo" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "edit.redo" ) ) == command_result_t::OK );
    CheckMasonPosition( bounds().box.minimum, moved.box.minimum ); CheckMasonPosition( bounds().box.maximum, { 192, 64, 64 } );
    CHECK( MapWorkspace_IsSelected( ws, selected ) ); CHECK( EditorSelection_Count( &ws->selection ) == 1u );
    CHECK( ws->tool == map_tool_t::SELECT ); CHECK( ws->wire.objects.nCount == 2u );
}

TEST_CASE( "Capture Mason separated 2D move and resize controls", "[mason][smoke][.ortho-gizmo-separation-capture]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    QWidget *top = MasonSeparatedOrthoScene( session );
    // Offscreen events drive real hover positions, while this presence flag
    // represents the physical pointer being inside the actual viewport.
    top->setAttribute( Qt::WA_UnderMouse, true );
    const QPointF move = MasonOrthoMovePickup( top, *ws ), resize = move + QPointF( 24, 0 );
    const auto center = MapBounds_Center( MapViews_SelectionGeometryBounds( ws ) );
    const QPoint origin = top->mapTo( window, MapOrthoView_WorldToView( top, { center.x, center.y } ).toPoint() );
    const QRect crop = QRect( origin - QPoint( 115, 115 ), QSize( 360, 260 ) ).intersected( window->rect() );
    REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    const auto capture = [&]( const QString &prefix ) {
        for ( const auto &target : { std::pair{ move, "move" }, std::pair{ resize, "resize" } } ) {
            MasonWorkflowMouse( top, QEvent::MouseMove, target.first, Qt::NoButton ); QCoreApplication::processEvents();
            CHECK( top->cursor().shape() == ( target.first == move ? Qt::SizeAllCursor : Qt::SizeHorCursor ) );
            REQUIRE( window->grab().save( QStringLiteral( "artifacts/%1_%2_hover_workspace.png" ).arg( prefix, target.second ) ) );
            REQUIRE( window->grab( crop ).save( QStringLiteral( "artifacts/%1_%2_hover_controls.png" ).arg( prefix, target.second ) ) );
        }
    };
    capture( QStringLiteral( "mason_ortho" ) );
    // Retain the current view so the selected boundary meets the move-arrow tip.
    const f64 zoom = MapOrthoView_Zoom( top ), factor = 128.0 / ( 64.0 * zoom );
    REQUIRE( MapWorkspace_ScaleSelection( ws, { factor, factor, factor }, center ) );
    QCoreApplication::processEvents(); CHECK( MapOrthoView_Zoom( top ) == zoom );
    const auto expanded = MapViews_SelectionGeometryBounds( ws );
    CHECK( std::abs( ( expanded.box.maximum.x - expanded.box.minimum.x ) * zoom - 128.0 ) < 1e-6 );
    CHECK( std::abs( MasonOrthoMovePickup( top, *ws ).x() - move.x() ) < 1e-6 );
    capture( QStringLiteral( "mason_ortho_boundary" ) );
}
