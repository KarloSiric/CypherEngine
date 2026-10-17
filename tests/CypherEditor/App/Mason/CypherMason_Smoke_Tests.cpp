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

#include "CypherMapGui_EntityIo.h"
#include "CypherMason_MainWindow.h"
#include "CypherMap_ObjectCreate.h"
#include "CypherMason_Branding.h"

#include "CypherEditorGui_CommandPalette.h"
#include "CypherEditorGui_AppearancePage.h"
#include "CypherMapGui_Check.h"
#include "CypherMapGui_ToolPanels.h"
#include "CypherMapGui_Panels.h"
#include "CypherMapGui_Views.h"
#include "CypherMapGui_ToolGestures.h"
#include "CypherMapGui_FastTexture.h"
#include "CypherMapGui_Game.h"
#include "CypherMapGui_Hotspots.h"
#include "CypherMapGui_Instances.h"
#include "CypherMapGui_UvEditor.h"
#include "CypherMap_Groups.h"
#include "CypherMap_Hierarchy.h"
#include "CypherMap_Edit.h"
#include "CypherMapGui_Notice.h"
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
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherGeometry_MeshSource.h"
#include <catch2/catch_approx.hpp>

#include "DockAreaWidget.h"
#include <algorithm>
#include <array>
#include "DockWidget.h"
#include "DockManager.h"
#include "FloatingDockContainer.h"

#include <catch2/catch_test_macros.hpp>

#include <QTableWidget>

#include <cstdio>

#include <QAction>
#include <QSet>
#include <QApplication>
#include <QCheckBox>
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
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
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
#include <functional>
#include <map>
#include <set>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSplashScreen>
#include <QStatusBar>
#include <QVBoxLayout>

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

#if defined( Q_OS_MACOS )
constexpr Qt::CursorShape kViewportMovementCursor = Qt::DragMoveCursor;
#else
constexpr Qt::CursorShape kViewportMovementCursor = Qt::SizeAllCursor;
#endif

void ActivateStartupTestWindow( QWidget *window )
{
    window->activateWindow();
    QCoreApplication::processEvents();
    if ( QApplication::platformName() != QStringLiteral( "cocoa" ) || window->isActiveWindow() ) { return; }
    // A background Cocoa test process may never receive OS activation. Give
    // native delivery a bounded opportunity, then model only that platform
    // event. Qt restores the window's remembered child; this helper must not
    // choose or focus a pane/search field. These are focus-routing tests, not
    // a benchmark of whether an unattended process can take OS foreground.
    QElapsedTimer wait;
    wait.start();
    while ( !window->isActiveWindow() && wait.elapsed() < 250 ) {
        QCoreApplication::processEvents( QEventLoop::AllEvents, 5 );
    }
    if ( !window->isActiveWindow() ) {
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        QApplication::setActiveWindow( window ); // Deliberate test-only platform-event simulation.
        QT_WARNING_POP
    }
    QCoreApplication::processEvents();
}

QString ExampleRoot()
{
    return QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
}

// Owns a headless Mason for one test.
struct mason_session_t {
    explicit mason_session_t( bool show = true )
    {
        pMason = Mason_Create( App(), Allocator_GetSystem(), MASON_FLAG_HEADLESS );
        REQUIRE( pMason != nullptr );
        Mason_Window( pMason )->resize( 1400, 900 );
        if ( show ) {
            Mason_Window( pMason )->show();
            QCoreApplication::processEvents();
        }
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

TEST_CASE( "Mason startup directs unprimed keyboard and toolbar input to its active view", "[mason][smoke][input][startup-focus]" )
{
    for ( int gesture = 0; gesture < 3; ++gesture ) {
        CAPTURE( gesture ); mason_session_t session;
        auto *window = Mason_Window( session.pMason ); auto *ws = Mason_MapWorkspace( session.pMason );
        // Model activating the launched window without selecting a control.
        // Native Cocoa may not activate a background test application itself.
        ActivateStartupTestWindow( window );
        auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views != nullptr );
        auto *search = window->findChild<QLineEdit *>( QStringLiteral( "MasonFastAssetSearch" ) ); REQUIRE( search != nullptr );
        auto *pane = MapViews_PaneView( views, MapViews_ActivePane( views ) ); REQUIRE( pane != nullptr );
        auto *focused = QApplication::focusWidget(); REQUIRE( focused != nullptr );
        INFO( "Natural startup focus: " << focused->metaObject()->className() << " / " << focused->objectName().toStdString() );
        CHECK( pane->isVisible() ); CHECK( focused == pane ); CHECK_FALSE( search->hasFocus() );
        CHECK( window->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) ) == nullptr );
        const auto *document = ws->pDocument; const usize steps = EditorHistory_StepCount( &ws->history );
        MapWorkspace_SetTool( ws, map_tool_t::NONE );
        if ( gesture < 2 ) {
            // Route actual text-bearing key events to Qt's naturally focused
            // widget. A helper that first focuses a viewport hides this bug.
            const int key = gesture == 0 ? Qt::Key_T : Qt::Key_S;
            const Qt::KeyboardModifiers modifiers = gesture == 0 ? Qt::NoModifier : Qt::ShiftModifier;
            const QString text = gesture == 0 ? QStringLiteral( "t" ) : QStringLiteral( "S" );
            focused = QApplication::focusWidget(); REQUIRE( focused != nullptr );
            QKeyEvent preflight( QEvent::ShortcutOverride, key, modifiers, text ); QCoreApplication::sendEvent( focused, &preflight );
            QKeyEvent press( QEvent::KeyPress, key, modifiers, text ); QCoreApplication::sendEvent( focused, &press );
            QKeyEvent release( QEvent::KeyRelease, key, modifiers, text ); QCoreApplication::sendEvent( focused, &release );
            QCoreApplication::processEvents();
            CHECK( ws->tool == ( gesture == 0 ? map_tool_t::TRANSLATE : map_tool_t::SELECT ) );
        } else {
            QToolButton *button = nullptr;
            for ( auto *candidate : window->findChildren<QToolButton *>() ) {
                if ( candidate->defaultAction() != nullptr && candidate->defaultAction()->objectName() == QStringLiteral( "map.tool.block" ) ) {
                    button = candidate; break;
                }
            }
            REQUIRE( button != nullptr ); REQUIRE( button->isVisible() );
            const QPointF at = button->rect().center();
            QMouseEvent press( QEvent::MouseButtonPress, at, button->mapToGlobal( at ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
            QCoreApplication::sendEvent( button, &press );
            QMouseEvent release( QEvent::MouseButtonRelease, at, button->mapToGlobal( at ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
            QCoreApplication::sendEvent( button, &release ); QCoreApplication::processEvents();
            CHECK( ws->tool == map_tool_t::BLOCK ); CHECK( button->isChecked() );
        }
        auto *assetWindow = window->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) );
        CHECK( ( assetWindow == nullptr || !assetWindow->isVisible() ) ); CHECK( search->text().isEmpty() );
        CHECK( ws->pDocument == document ); CHECK( EditorHistory_StepCount( &ws->history ) == steps );
        CHECK_FALSE( MapWorkspace_IsModified( ws ) );
    }
}

TEST_CASE( "Mason startup focuses its restored maximized pane and later shows retain explicit asset search focus", "[mason][smoke][input][startup-focus][assets]" )
{
    // Save through the real command into this headless session's private
    // workspace file before the first show queues startup layout restoration.
    mason_session_t session( false ); auto *window = Mason_Window( session.pMason );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::FOUR );
    MapViews_SetPaneType( views, 3, map_view_type_t::FRONT ); MapViews_SetActivePane( views, 3 ); MapViews_SetMaximized( views, 3 );
    REQUIRE( session.Run( QStringLiteral( "view.layout.save" ) ) == command_result_t::OK );
    MapViews_SetMaximized( views, -1 ); MapViews_SetActivePane( views, 0 ); MapViews_SetPaneType( views, 3, map_view_type_t::SIDE );
    window->show(); QCoreApplication::processEvents();
    ActivateStartupTestWindow( window );
    CHECK( MapViews_ActivePane( views ) == 3 ); CHECK( MapViews_MaximizedPane( views ) == 3 );
    CHECK( MapViews_PaneType( views, 3 ) == map_view_type_t::FRONT );
    auto *restored = MapViews_PaneView( views, 3 ); REQUIRE( restored != nullptr );
    CHECK( restored->isVisible() ); CHECK( QApplication::focusWidget() == restored );
    auto *search = window->findChild<QLineEdit *>( QStringLiteral( "MasonFastAssetSearch" ) ); REQUIRE( search != nullptr );
    REQUIRE( session.Run( QStringLiteral( "assets.search" ) ) == command_result_t::OK );
    QCoreApplication::processEvents(); REQUIRE( search->hasFocus() );
    // Deliberate search focus retains the established typing-to-results path.
    REQUIRE( QApplication::focusWidget() != nullptr );
    QKeyEvent typing( QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral( "metal" ) );
    QCoreApplication::sendEvent( QApplication::focusWidget(), &typing ); QCoreApplication::processEvents();
    auto *assetWindow = window->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) ); REQUIRE( assetWindow != nullptr );
    CHECK( assetWindow->isVisible() ); CHECK( search->hasFocus() ); CHECK( search->text() == QStringLiteral( "metal" ) );
    CHECK( EditorAssetWindow_Visible( assetWindow ).contains( QStringLiteral( "materials/blockout/metal_panel.cymat" ) ) );
    window->hide(); QCoreApplication::processEvents();
    window->show(); ActivateStartupTestWindow( window );
    // A headless platform need not reactivate a re-shown native window. Its
    // remembered focus must still be the explicitly chosen field, not a pane.
    CHECK( window->focusWidget() == search ); CHECK( search->text() == QStringLiteral( "metal" ) );
    CHECK( MapViews_ActivePane( views ) == 3 ); CHECK( MapViews_MaximizedPane( views ) == 3 );
    CHECK_FALSE( MapWorkspace_IsModified( Mason_MapWorkspace( session.pMason ) ) );
}

TEST_CASE( "Mason pending startup layout retains modal welcome input and remembers viewport focus", "[mason][smoke][input][startup-focus][startup-modal]" )
{
    mason_session_t session( false ); auto *window = Mason_Window( session.pMason );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views != nullptr );
    auto *ws = Mason_MapWorkspace( session.pMason ); const auto *document = ws->pDocument;
    const usize steps = EditorHistory_StepCount( &ws->history );
    QDialog welcome( window ); welcome.setWindowTitle( QStringLiteral( "Startup welcome focus probe" ) );
    welcome.setModal( true );
    QVBoxLayout layout( &welcome ); QLineEdit input( &welcome ); layout.addWidget( &input );
    bool inspected = false;
    // Main.cpp opens Welcome before the outer application loop. First-show
    // layout restoration therefore runs inside this modal loop in production.
    window->show();
    welcome.show();
    // The offscreen platform does not activate modal native windows. Model
    // OS activation before startup's queued callback, without choosing a child.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow( &welcome );
    QT_WARNING_POP
    CHECK( QApplication::activeModalWidget() == &welcome );
    CHECK( QApplication::focusWidget() == &input );
    QTimer::singleShot( 20, &welcome, [&]() {
        inspected = true;
        CHECK( QApplication::activeModalWidget() == &welcome );
        CHECK( QApplication::activeWindow() == &welcome );
        auto *focused = QApplication::focusWidget(); CHECK( focused == &input ); CHECK( input.hasFocus() );
        auto *pane = MapViews_PaneView( views, MapViews_ActivePane( views ) );
        CHECK( pane != nullptr );
        if ( pane != nullptr ) { CHECK( pane->isVisible() ); CHECK( window->focusWidget() == pane ); }
        // Native focus selection belongs to the dialog. Editing its field
        // must work while the main window remembers its later viewport target.
        if ( focused == &input ) {
            QKeyEvent press( QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, QStringLiteral( "a" ) );
            QCoreApplication::sendEvent( focused, &press );
            QKeyEvent release( QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, QStringLiteral( "a" ) );
            QCoreApplication::sendEvent( focused, &release );
        }
        CHECK( input.text() == QStringLiteral( "a" ) );
        CHECK( window->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) ) == nullptr );
        welcome.accept();
    } );
    CHECK( welcome.exec() == QDialog::Accepted ); CHECK( inspected );
    auto *pane = MapViews_PaneView( views, MapViews_ActivePane( views ) ); REQUIRE( pane != nullptr );
    CHECK( pane->isVisible() ); CHECK( window->focusWidget() == pane );
    CHECK( ws->pDocument == document ); CHECK( EditorHistory_StepCount( &ws->history ) == steps );
    CHECK_FALSE( MapWorkspace_IsModified( ws ) );
}

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
        control->window()->activateWindow(); QCoreApplication::processEvents();
        // This case tests key routing from an already focused control, not
        // macOS activation policy for a background test executable.
        QT_WARNING_PUSH
        QT_WARNING_DISABLE_DEPRECATED
        QApplication::setActiveWindow( control->window() );
        QT_WARNING_POP
        control->setFocus();
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
    // Framing a tiny selection reveals its real boundary controls instead of
    // adding a detached control rectangle to the map overview.
    MapWorkspace_SetTool( ws, map_tool_t::SELECT );
    box( { -144, -144, 0 }, { -128, -128, 16 } );
    const auto tinyBounds = MapViews_SelectionGeometryBounds( ws );
    const auto tinyCenter = MapBounds_Center( tinyBounds );
    const auto *tinyDocument = ws->pDocument;
    const auto tinyHistory = EditorHistory_StepCount( &ws->history );
    MapWorkspace_Frame( ws, CY_TRUE ); QCoreApplication::processEvents();
    const QPointF tinyPivot = MapOrthoView_WorldToView( top, { tinyCenter.x, tinyCenter.y } );
    const QPointF tinySide = MapOrthoView_WorldToView( top, { tinyBounds.box.maximum.x, tinyCenter.y } );
    REQUIRE( tinySide.x() - tinyPivot.x() >= 48 );
    start = tinySide; end = start + QPointF( 8 * MapOrthoView_Zoom( top ), 0 );
    QMouseEvent tinyPress( QEvent::MouseButtonPress, start, top->mapToGlobal( start ), Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier );
    QCoreApplication::sendEvent( top, &tinyPress ); CHECK_FALSE( ws->editPreview.bActive );
    QMouseEvent tinyMove( QEvent::MouseMove, end, top->mapToGlobal( end ), Qt::NoButton, Qt::LeftButton, Qt::ControlModifier );
    QCoreApplication::sendEvent( top, &tinyMove ); QCoreApplication::processEvents();
    REQUIRE( ws->editPreview.bActive ); REQUIRE( ws->editPreview.transform.bResize );
    CHECK( ws->editPreview.bounds.box.minimum.x == tinyBounds.box.minimum.x );
    CHECK( std::abs( ws->editPreview.bounds.box.maximum.x - tinyBounds.box.maximum.x - 8 ) < 1e-6 );
    CHECK( ws->pDocument == tinyDocument ); CHECK( EditorHistory_StepCount( &ws->history ) == tinyHistory );
    REQUIRE( Mason_Window( session.pMason )->grab().save( QStringLiteral( "artifacts/mason_gizmo_usability_workspace.png" ) ) );
    QMouseEvent tinyRelease( QEvent::MouseButtonRelease, end, top->mapToGlobal( end ), Qt::LeftButton, Qt::NoButton, Qt::ControlModifier );
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
    MapWorkspace_Frame( ws, CY_TRUE ); QCoreApplication::processEvents();
    const QPointF side = MapOrthoView_WorldToView( top, { feedbackBounds.box.maximum.x, feedbackCenter.y } );
    start = side;
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
    REQUIRE( buttons.size() == 42 ); // Edge Cut, Edge Arc and Displacement joined Knife and Loop Cut; Shear joined Mirror.
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
    CHECK( pTools->findChild<QWidget *>( QStringLiteral( "EditorToolPalette" ) )->findChildren<QToolButton *>().size() == 42 );
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
    // Revert to Saved: offered only once something changed, and back to the file.
    auto *pWorkspace = Mason_MapWorkspace( session.pMason );
    CHECK_FALSE( pWindow->findChild<QAction *>( QStringLiteral( "file.revert" ) )->isEnabled() );
    const usize objects = pWorkspace->wire.objects.nCount;
    map::map_bounds_t extra{}; map::MapBounds_AddPoint( extra, { 4000, 4000, 0 } ); map::MapBounds_AddPoint( extra, { 4064, 4064, 64 } );
    REQUIRE( map::MapWorkspace_CreateBox( pWorkspace, extra ) );
    QCoreApplication::processEvents();
    REQUIRE( pWindow->findChild<QAction *>( QStringLiteral( "file.revert" ) )->isEnabled() );
    REQUIRE( session.Run( QStringLiteral( "file.revert yes" ) ) == command_result_t::OK );
    CHECK( pWorkspace->wire.objects.nCount == objects );
    CHECK_FALSE( map::MapWorkspace_IsModified( pWorkspace ) );
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

TEST_CASE( "Mason's panel and asset windows inspect unsupported assets and still assign materials", "[mason][smoke][asset-activation-contract]" )
{
    QTemporaryDir content;
    REQUIRE( content.isValid() );
    const QString model = QStringLiteral( "models/activation/chair.cymesh" );
    const QString sound = QStringLiteral( "sounds/activation/door.cysnd" );
    const QString panelMaterial = QStringLiteral( "materials/activation/panel.cymat" );
    const QString windowMaterial = QStringLiteral( "materials/activation/window.cymat" );
    const QString paneMaterial = QStringLiteral( "materials/activation/pane.cymat" );
    for ( const QString &path : { model, sound, panelMaterial, windowMaterial, paneMaterial } ) {
        REQUIRE( QDir().mkpath( QFileInfo( content.filePath( path ) ).absolutePath() ) );
        QFile file( content.filePath( path ) );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        const QByteArray text = path.endsWith( QStringLiteral( ".cymat" ) )
            ? "@cykv 1\n@schema \"cypher.material\" 1\n{ shader = \"shaders/activation/surface.cyshader\" }"
            : path == sound ? "@cykv 1\n@schema \"cypher.sound\" 1\n{ source = \"sounds/activation/door.wav\" }"
                            : "@cykv 1\n@schema \"cypher.fixture\" 1\n{ label = \"model inspection fixture\" }";
        REQUIRE( file.write( text ) == text.size() );
    }
    mason_session_t session;
    auto *window = Mason_Window( session.pMason );
    auto *gui = Mason_Gui( session.pMason );
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *browser = window->findChild<QWidget *>( QStringLiteral( "EditorAssetBrowser" ) );
    REQUIRE( browser != nullptr );
    const auto *document = ws->pDocument;
    const usize steps = EditorHistory_StepCount( &ws->history );
    QStringList roots = EditorAssetBrowser_Roots( browser );
    roots.prepend( content.path() );
    const auto *rootsSetting = EditorSettings_Find( &gui->settings, StringView_FromCString( "editor.assets.roots" ) );
    REQUIRE( rootsSetting != nullptr );
    const QByteArray rootsText = roots.join( QLatin1Char( ';' ) ).toUtf8();
    setting_value_t rootsValue{};
    rootsValue.type = setting_type_t::STRING;
    rootsValue.text = { rootsText.constData(), static_cast<usize>( rootsText.size() ) };
    REQUIRE( EditorSettings_Write( &gui->settings, settings_scope_t::USER, *rootsSetting, rootsValue ) == settings_registry_status_t::OK );
    const auto activeMaterial = [&]() {
        const auto value = EditorSettings_Text( &gui->settings, "editor.map.default_material", {} );
        return QString::fromUtf8( value.pData != nullptr ? value.pData : "", static_cast<qsizetype>( value.cchLength ) );
    };
    const auto inspect = [&]( const QString &path ) {
        auto *preview = browser->findChild<QDialog *>( QStringLiteral( "AssetPreviewDialog" ) );
        REQUIRE( preview != nullptr );
        CHECK( preview->isVisible() );
        auto *source = preview->findChild<QPlainTextEdit *>( QStringLiteral( "AssetPreviewSource" ) );
        auto *files = preview->findChild<QComboBox *>( QStringLiteral( "AssetPreviewFiles" ) );
        REQUIRE( source != nullptr );
        REQUIRE( files != nullptr );
        CHECK( source->isReadOnly() );
        CHECK( files->findData( path ) >= 0 );
        preview->hide();
    };
    const auto accept = [&]( QWidget *assets, const QString &path, const QString &label ) {
        REQUIRE( EditorAssetWindow_Select( assets, path ) );
        auto *button = assets->findChild<QPushButton *>( QStringLiteral( "AssetWindowAccept" ) );
        REQUIRE( button != nullptr );
        CHECK( button->text() == label );
        REQUIRE( button->isVisible() );
        REQUIRE( button->isEnabled() );
        const QPointF point = button->rect().center();
        QMouseEvent press( QEvent::MouseButtonPress, point, button->mapToGlobal( point ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QMouseEvent release( QEvent::MouseButtonRelease, point, button->mapToGlobal( point ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
        QApplication::sendEvent( button, &press );
        QApplication::sendEvent( button, &release );
        QCoreApplication::processEvents();
    };

    // Exercise the real registered panel callback without replacing it with a test callback.
    const QString initialMaterial = activeMaterial();
    EditorAssetBrowser_Activate( browser, model );
    inspect( model );
    CHECK( activeMaterial() == initialMaterial );
    EditorAssetBrowser_Activate( browser, panelMaterial );
    CHECK( activeMaterial() == panelMaterial );

    REQUIRE( session.Run( QStringLiteral( "assets.browser" ) ) == command_result_t::OK );
    auto *assetWindow = window->findChild<QDialog *>( QStringLiteral( "EditorAssetWindow" ) );
    REQUIRE( assetWindow != nullptr );
    CHECK_FALSE( EditorAssetWindow_IsPicking( assetWindow ) );
    accept( assetWindow, model, QStringLiteral( "Inspect" ) );
    inspect( model );
    CHECK( activeMaterial() == panelMaterial );
    accept( assetWindow, windowMaterial, QStringLiteral( "Accept" ) );
    CHECK( activeMaterial() == windowMaterial );
    assetWindow->hide();

    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::FOUR );
    auto *choice = views->findChild<QAction *>( QStringLiteral( "EditorViewContent0_4" ) );
    REQUIRE( choice != nullptr );
    REQUIRE( choice->isEnabled() );
    choice->trigger();
    QCoreApplication::processEvents();
    auto *pane = MapViews_PaneView( views, 0 );
    REQUIRE( pane != nullptr );
    CHECK_FALSE( pane->isWindow() );
    accept( pane, sound, QStringLiteral( "Inspect" ) );
    inspect( sound );
    CHECK( activeMaterial() == windowMaterial );
    accept( pane, paneMaterial, QStringLiteral( "Accept" ) );
    CHECK( activeMaterial() == paneMaterial );
    CHECK( ws->pDocument == document );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps );
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

TEST_CASE( "Mason clipboard shortcuts preserve attached descendants when only their parent is selected", "[mason][smoke][hierarchy-clipboard][input][workflow]" )
{
    REQUIRE( QGuiApplication::platformName() == QStringLiteral( "offscreen" ) );
    struct saved_clipboard_t {
        std::map<QString, QByteArray> formats;
        saved_clipboard_t()
        {
            if ( const auto *mime = QApplication::clipboard()->mimeData() ) {
                for ( const auto &format : mime->formats() ) { formats[format] = mime->data( format ); }
            }
        }
        ~saved_clipboard_t()
        {
            auto *mime = new QMimeData();
            for ( const auto &[format, bytes] : formats ) { mime->setData( format, bytes ); }
            QApplication::clipboard()->setMimeData( mime );
        }
    } savedClipboard;
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    REQUIRE( MapWorkspace_New( ws ) == map_status_t::OK );
    std::array<u64, 3> original{}, geometry{};
    const auto origin = []( usize i ) { return math::vec3d_t{ 256.0 * i, 64.0 * i, 32.0 * i }; };
    for ( usize i = 0; i < original.size(); ++i ) {
        const auto at = origin( i );
        REQUIRE( MapDocument_AddEntity( ws->pDocument, StringView_FromCString( "default" ), StringView_FromCString( "func_detail" ), at, &original[i] ) == map_status_t::OK );
        REQUIRE( MapEdit_CreateBox( ws->pDocument, { at, { at.x + 32, at.y + 48, at.z + 64 } }, StringView_FromCString( "materials/dev/grid" ),
                                   StringView_FromCString( "default" ), &geometry[i] ) == map_status_t::OK );
        REQUIRE( MapDocument_SetGeometryOwner( ws->pDocument, geometry[i], original[i] ) == map_status_t::OK );
        if ( i != 0 ) { REQUIRE( MapHierarchy_SetParent( ws->pDocument, { &original[i], 1 }, original[i - 1] ) == map_status_t::OK ); }
    }
    MapWorkspace_DocumentChanged( ws ); MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
    MapWorkspace_SetSelection( ws, &original[0], 1 ); REQUIRE( ws->selection.ids.nCount == 1 );
    const auto checkHierarchy = [&]() {
        REQUIRE( ws->selection.ids.nCount == 1 ); CHECK( ws->wire.entities.nCount == 3 ); CHECK( ws->pDocument->geometryRecords.nCount == 3 );
        u64 parent = 0, current = ws->selection.ids.pData[0];
        for ( usize i = 0; i < original.size(); ++i ) {
            CAPTURE( i, current ); CHECK( MapHierarchy_Parent( ws->pDocument, current ) == parent );
            const auto *entity = MapWireframe_FindEntity( ws->wire, current ); REQUIRE( entity ); CHECK( entity->nOwned == 1 );
            const auto at = origin( i ); CheckMasonPosition( entity->origin, at );
            usize owned = 0;
            for ( usize o = 0; o < ws->wire.objects.nCount; ++o ) {
                const auto &object = ws->wire.objects.pData[o]; if ( object.owner != current ) { continue; }
                ++owned; CheckMasonPosition( object.bounds.box.minimum, at ); CheckMasonPosition( object.bounds.box.maximum, { at.x + 32, at.y + 48, at.z + 64 } );
            }
            CHECK( owned == 1 ); const auto children = MapHierarchy_Children( ws->pDocument, current, false );
            if ( i + 1 < original.size() ) { REQUIRE( children.size() == 1 ); parent = current; current = children[0]; }
            else { CHECK( children.empty() ); }
        }
        CHECK( MapHierarchy_Orphans( ws->pDocument ).empty() );
    };
    auto *grid = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( grid );
    QWidget *view = MapViews_PaneView( grid, 0 ); REQUIRE( view );
    window->activateWindow(); view->setFocus(); QCoreApplication::processEvents(); REQUIRE( QApplication::focusWidget() == view );
    const usize steps = EditorHistory_StepCount( &ws->history );
    ( void )MasonWorkflowKey( view, Qt::Key_C, Qt::ControlModifier );
    REQUIRE( QApplication::clipboard()->mimeData() );
    const auto copied = QApplication::clipboard()->mimeData()->data( QString::fromLatin1( MAP_CLIPBOARD_MIME ) ); REQUIRE_FALSE( copied.isEmpty() );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps ); checkHierarchy();
    ( void )MasonWorkflowKey( view, Qt::Key_X, Qt::ControlModifier );
    CHECK( ws->wire.objects.nCount == 0 ); CHECK( ws->wire.entities.nCount == 0 ); CHECK( ws->pDocument->geometryRecords.nCount == 0 );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1 );
    ( void )MasonWorkflowKey( view, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier );
    checkHierarchy(); const auto pasted = ws->selection.ids.pData[0]; CHECK( pasted != original[0] );
    for ( usize i = 0; i < original.size(); ++i ) {
        CHECK( MapWireframe_FindEntity( ws->wire, original[i] ) == nullptr ); CHECK( MapWireframe_FindObject( ws->wire, geometry[i] ) == nullptr );
    }
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 2 );
    REQUIRE( session.Run( QStringLiteral( "edit.undo" ) ) == command_result_t::OK ); CHECK( ws->wire.objects.nCount == 0 );
    REQUIRE( session.Run( QStringLiteral( "edit.undo" ) ) == command_result_t::OK ); checkHierarchy(); CHECK( ws->selection.ids.pData[0] == original[0] );
    REQUIRE( session.Run( QStringLiteral( "edit.redo" ) ) == command_result_t::OK ); CHECK( ws->wire.objects.nCount == 0 );
    REQUIRE( session.Run( QStringLiteral( "edit.redo" ) ) == command_result_t::OK ); checkHierarchy(); CHECK( ws->selection.ids.pData[0] == pasted );
    CHECK( QApplication::clipboard()->mimeData()->data( QString::fromLatin1( MAP_CLIPBOARD_MIME ) ) == copied );
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
    // Whole-object edits stay out of Vertices mode. Merge, Collapse and the
    // vertex Bevel do act on these vertices (CypherMapGui_GeometryCommands).
    for ( const char *command : { "edit.delete", "edit.duplicate" } ) {
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

TEST_CASE( "Mason flight speed defaults execute only in the camera and appear in editable keybindings", "[mason][smoke][camera-speed][camera-look-wheel]" )
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
    const QPointF lookPoint = camera->rect().center();
    QMouseEvent look( QEvent::MouseButtonPress, lookPoint, camera->mapToGlobal( lookPoint ), Qt::RightButton, Qt::RightButton, Qt::NoModifier );
    QCoreApplication::sendEvent( camera, &look );
    QKeyEvent fly( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &fly );
    const auto lookPosition = MapCameraView_Position( camera ), lookDirection = MapCameraView_Forward( camera );
    QWheelEvent speedWheel( lookPoint, camera->mapToGlobal( lookPoint ), {}, QPoint( 0, 120 ), Qt::RightButton, Qt::ShiftModifier, Qt::NoScrollPhase, false );
    QCoreApplication::sendEvent( camera, &speedWheel ); REQUIRE( speedWheel.isAccepted() ); CHECK( speed() == 2000.0 );
    CHECK( MapCameraView_Position( camera ).x == lookPosition.x ); CHECK( MapCameraView_Position( camera ).y == lookPosition.y ); CHECK( MapCameraView_Position( camera ).z == lookPosition.z );
    CHECK( MapCameraView_Forward( camera ).x == lookDirection.x );
    const auto velocity = MapCameraView_NavigationVelocity( camera );
    CHECK( std::abs( std::sqrt( velocity.x * velocity.x + velocity.y * velocity.y + velocity.z * velocity.z ) - 2000.0 ) < 1e-8 );
    const QPointF lookEnd = lookPoint + QPointF( 12, 0 );
    QMouseEvent turn( QEvent::MouseMove, lookEnd, camera->mapToGlobal( lookEnd ), Qt::NoButton, Qt::RightButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &turn );
    CHECK( MapCameraView_Forward( camera ).x != lookDirection.x );
    QKeyEvent flyStop( QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &flyStop );
    QMouseEvent lookStop( QEvent::MouseButtonRelease, lookEnd, camera->mapToGlobal( lookEnd ), Qt::RightButton, Qt::NoButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &lookStop );
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
    for ( const QString id : { QStringLiteral( "map.camera.speed_increase" ), QStringLiteral( "map.camera.speed_decrease" ) } ) {
        bool found = false;
        for ( const QString &row : rows ) {
            const auto columns = row.split( QLatin1Char( '\t' ) );
            if ( columns.size() == 7 && columns[1] == id && columns[3] == QStringLiteral( "map.camera.look" ) ) {
                found = true; CHECK( columns[2] == QStringLiteral( "Mouse" ) ); CHECK_FALSE( columns[4].isEmpty() ); CHECK( columns[6] == QStringLiteral( "3D look wheel declaration" ) );
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
    const auto bounds = MapViews_SelectionGeometryBounds( &ws ); const auto center = MapBounds_Center( bounds );
    f64 pixels = 64.0 * EditorSettings_Real( &ws.pGui->settings, "editor.viewport.gizmo_scale", 1.0 );
    for ( const f64 extent : { bounds.box.maximum.x - bounds.box.minimum.x, bounds.box.maximum.y - bounds.box.minimum.y } ) {
        const f64 radius = extent * MapOrthoView_Zoom( top ) * .5;
        if ( radius + 1e-6 >= 48 ) { pixels = std::min( pixels, radius - 24 ); }
    }
    pixels = std::max( pixels, 24.0 );
    return MapOrthoView_WorldToView( top, { center.x, center.y } ) + QPointF( pixels, 0 );
}
}

TEST_CASE( "Mason overview omits displaced resize controls and framing reveals real grid pickups", "[mason][smoke][ortho-gizmo-separation][gizmos]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason );
    QWidget *top = MasonSeparatedOrthoScene( session );
    const u64 selected = EditorSelection_At( &ws->selection, 0u ); const usize steps = EditorHistory_StepCount( &ws->history );
    const auto token = UndoRedo_StateToken( ws->history.pUndo );
    const auto original = MapViews_SelectionGeometryBounds( ws );
    const auto bounds = [&]() {
        const auto *object = MapWireframe_FindObject( ws->wire, selected ); REQUIRE( object != nullptr ); return object->bounds;
    };
    const QPointF move = MasonOrthoMovePickup( top, *ws ), oldResize = move + QPointF( 24, 0 );
    REQUIRE( top->rect().contains( move.toPoint() ) ); REQUIRE( top->rect().contains( oldResize.toPoint() ) );
    MasonWorkflowMouse( top, QEvent::MouseMove, oldResize, Qt::NoButton ); CHECK( top->cursor().shape() == Qt::ArrowCursor );
    for ( const auto &target : { std::pair{ move, kViewportMovementCursor } } ) {
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
    MapWorkspace_Frame( ws, CY_TRUE ); QCoreApplication::processEvents();
    const auto center = MapBounds_Center( moved );
    const QPointF resizeMoved = MapOrthoView_WorldToView( top, { moved.box.maximum.x, center.y } );
    REQUIRE( moved.box.maximum.x - center.x > 0 ); REQUIRE( ( moved.box.maximum.x - center.x ) * MapOrthoView_Zoom( top ) >= 48 );
    const QPointF resizeTravel( ws->gridSize * MapOrthoView_Zoom( top ), 0 );
    MasonWorkflowMouse( top, QEvent::MouseMove, resizeMoved, Qt::NoButton ); CHECK( top->cursor().shape() == Qt::SizeHorCursor );
    MasonWorkflowMouse( top, QEvent::MouseButtonPress, resizeMoved, Qt::LeftButton, Qt::LeftButton );
    MasonWorkflowMouse( top, QEvent::MouseMove, resizeMoved + resizeTravel, Qt::NoButton, Qt::LeftButton );
    REQUIRE( ws->editPreview.bActive ); REQUIRE( ws->editPreview.transform.kind == map_transform_preview_kind_t::SCALE );
    REQUIRE( ws->editPreview.transform.bResize ); CHECK_FALSE( ws->editPreview.transform.bResizeFromCenter );
    CHECK( ws->editPreview.transform.pivot.x == moved.box.minimum.x );
    CheckMasonPosition( ws->editPreview.bounds.box.minimum, moved.box.minimum );
    CheckMasonPosition( ws->editPreview.bounds.box.maximum, { 192, 64, 64 } );
    CHECK( ws->pDocument == beforeResize ); CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1u );
    MasonWorkflowMouse( top, QEvent::MouseButtonRelease, resizeMoved + resizeTravel, Qt::LeftButton );
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
    QPointF move = MasonOrthoMovePickup( top, *ws ), resize = move + QPointF( 24, 0 );
    const auto center = MapBounds_Center( MapViews_SelectionGeometryBounds( ws ) );
    const QPoint origin = top->mapTo( window, MapOrthoView_WorldToView( top, { center.x, center.y } ).toPoint() );
    const QRect crop = QRect( origin - QPoint( 115, 115 ), QSize( 360, 260 ) ).intersected( window->rect() );
    REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
    const auto capture = [&]( const QString &prefix, bool realResize ) {
        for ( const auto &target : { std::pair{ move, "move" }, std::pair{ resize, "resize" } } ) {
            MasonWorkflowMouse( top, QEvent::MouseMove, target.first, Qt::NoButton ); QCoreApplication::processEvents();
            CHECK( top->cursor().shape() == ( target.first == move ? kViewportMovementCursor : realResize ? Qt::SizeHorCursor : Qt::ArrowCursor ) );
            REQUIRE( window->grab().save( QStringLiteral( "artifacts/%1_%2_hover_workspace.png" ).arg( prefix, target.second ) ) );
            REQUIRE( window->grab( crop ).save( QStringLiteral( "artifacts/%1_%2_hover_controls.png" ).arg( prefix, target.second ) ) );
        }
    };
    capture( QStringLiteral( "mason_ortho" ), false );
    // Retain the view and enlarge the object. Real boundary controls appear
    // while the move arrow contracts to keep their pickups separate.
    const f64 zoom = MapOrthoView_Zoom( top ), factor = 128.0 / ( 64.0 * zoom );
    REQUIRE( MapWorkspace_ScaleSelection( ws, { factor, factor, factor }, center ) );
    QCoreApplication::processEvents(); CHECK( MapOrthoView_Zoom( top ) == zoom );
    const auto expanded = MapViews_SelectionGeometryBounds( ws );
    CHECK( std::abs( ( expanded.box.maximum.x - expanded.box.minimum.x ) * zoom - 128.0 ) < 1e-6 );
    const QPointF pivot = MapOrthoView_WorldToView( top, { center.x, center.y } );
    move = MasonOrthoMovePickup( top, *ws ); resize = MapOrthoView_WorldToView( top, { expanded.box.maximum.x, center.y } );
    CHECK( std::abs( move.x() - pivot.x() - 40 ) < 1e-6 ); CHECK( std::abs( resize.x() - move.x() - 24 ) < 1e-6 );
    capture( QStringLiteral( "mason_ortho_boundary" ), true );
}

TEST_CASE( "Mason geometry snap checkbox aligns different authoring grids through a Top move gizmo", "[mason][smoke][geometry-snap]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    window->resize( 1600, 1050 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER ); MapViews_SetPaneType( views, 1, map_view_type_t::TOP );
    map_bounds_t source{}, target{};
    MapBounds_AddPoint( source, { 0, 0, 0 } ); MapBounds_AddPoint( source, { 64, 64, 64 } );
    MapBounds_AddPoint( target, { 144, 0, 0 } ); MapBounds_AddPoint( target, { 176, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( ws, source ) ); const u64 sourceId = EditorSelection_At( &ws->selection, 0u );
    REQUIRE( MapWorkspace_CreateBox( ws, target ) ); const u64 targetId = EditorSelection_At( &ws->selection, 0u );
    MapWorkspace_Select( ws, sourceId, MAP_SELECT_REPLACE );
    MapWorkspace_SetGridSize( ws, 64 ); MapWorkspace_SetSnapToGrid( ws, CY_TRUE );
    REQUIRE( session.Run( QStringLiteral( "map.tool.select" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.objects" ) ) == command_result_t::OK );
    auto *panel = window->findChild<QWidget *>( QStringLiteral( "MapToolProperties" ) ); REQUIRE( panel );
    auto *control = panel->findChild<QWidget *>( QStringLiteral( "editor.grid.geometry_snap" ) ); REQUIRE( control );
    auto *enabled = control->findChild<QCheckBox *>(); REQUIRE( enabled );
    CHECK( control->parentWidget()->property( "toolGroup" ).toString() == QStringLiteral( "Movement snapping" ) );
    REQUIRE_FALSE( enabled->isChecked() );
    const auto *document = ws->pDocument; const u64 geometryRevision = document->geometry.revision, selectionRevision = ws->selection.revision;
    const usize steps = EditorHistory_StepCount( &ws->history ); const auto token = UndoRedo_StateToken( ws->history.pUndo );
    enabled->click(); REQUIRE( enabled->isChecked() );
    const auto *descriptor = EditorSettings_Find( &Mason_Gui( session.pMason )->settings, StringView_FromCString( "editor.grid.geometry_snap" ) ); REQUIRE( descriptor );
    const auto setting = EditorSettings_Resolve( &Mason_Gui( session.pMason )->settings, *descriptor );
    CHECK( setting.value.bValue ); CHECK( setting.source == settings_scope_t::USER );
    CHECK( ws->pDocument == document ); CHECK( document->geometry.revision == geometryRevision );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps );

    MapWorkspace_Frame( ws, CY_FALSE ); MapViews_SetActivePane( views, 1 ); QCoreApplication::processEvents();
    auto *top = MapViews_PaneView( views, 1 ); REQUIRE( top ); REQUIRE( top->isVisible() );
    const auto bounds = [&]( u64 id ) {
        const auto *object = MapWireframe_FindObject( ws->wire, id ); REQUIRE( object ); return object->bounds;
    };
    const f64 gizmoPixels = 64.0 * EditorSettings_Real( &Mason_Gui( session.pMason )->settings, "editor.viewport.gizmo_scale", 1.0 );
    const QPointF start = MapOrthoView_WorldToView( top, { 32, 32 } ) + QPointF( gizmoPixels * 0.70, 0 );
    const QPointF end = start + QPointF( 78.0 * MapOrthoView_Zoom( top ), 0 );
    REQUIRE( top->rect().contains( start.toPoint() ) ); REQUIRE( top->rect().contains( end.toPoint() ) );
    top->setAttribute( Qt::WA_UnderMouse, true );
    MasonWorkflowMouse( top, QEvent::MouseMove, start, Qt::NoButton );
    CHECK( top->cursor().shape() == kViewportMovementCursor );
    MasonWorkflowMouse( top, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton );
    CHECK_FALSE( ws->editPreview.bActive );
    MasonWorkflowMouse( top, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton );
    REQUIRE( ws->editPreview.bActive ); REQUIRE( ws->editPreview.transform.kind == map_transform_preview_kind_t::TRANSLATE );
    CHECK_FALSE( ws->editPreview.transform.bClone ); CHECK_FALSE( ws->editPreview.transform.bResize );
    CheckMasonPosition( ws->editPreview.transform.delta, { 80, 0, 0 } );
    CheckMasonPosition( ws->editPreview.bounds.box.minimum, { 80, 0, 0 } ); CheckMasonPosition( ws->editPreview.bounds.box.maximum, { 144, 64, 64 } );
    CheckMasonPosition( bounds( sourceId ).box.minimum, source.box.minimum ); CheckMasonPosition( bounds( sourceId ).box.maximum, source.box.maximum );
    CheckMasonPosition( bounds( targetId ).box.minimum, target.box.minimum ); CheckMasonPosition( bounds( targetId ).box.maximum, target.box.maximum );
    CHECK( ws->pDocument == document ); CHECK( document->geometry.revision == geometryRevision ); CHECK( ws->selection.revision == selectionRevision );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws->history.pUndo ) ) );
    CHECK( ws->gridSize == 64 ); CHECK( ws->bSnapToGrid );
    // The ordinary regression has no artifact dependency. Set this variable
    // to capture the actual workspace and Top feedback during its live preview.
    if ( !qEnvironmentVariableIsEmpty( "CYPHER_GEOMETRY_SNAP_CAPTURE" ) ) {
        QCoreApplication::processEvents(); top->setAttribute( Qt::WA_UnderMouse, true );
        REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
        REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_geometry_snap_workspace.png" ) ) );
        REQUIRE( top->grab().save( QStringLiteral( "artifacts/mason_geometry_snap_top.png" ) ) );
    }
    MasonWorkflowMouse( top, QEvent::MouseButtonRelease, end, Qt::LeftButton );
    CHECK_FALSE( ws->editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1u );
    const auto checkCommitted = [&]() {
        CheckMasonPosition( bounds( sourceId ).box.minimum, { 80, 0, 0 } ); CheckMasonPosition( bounds( sourceId ).box.maximum, { 144, 64, 64 } );
        CheckMasonPosition( bounds( targetId ).box.minimum, target.box.minimum ); CheckMasonPosition( bounds( targetId ).box.maximum, target.box.maximum );
        CHECK( EditorSelection_Count( &ws->selection ) == 1u ); CHECK( EditorSelection_At( &ws->selection, 0u ) == sourceId );
        CHECK( ws->wire.objects.nCount == 2u ); CHECK( ws->gridSize == 64 ); CHECK( ws->bSnapToGrid );
    };
    checkCommitted();
    REQUIRE( session.Run( QStringLiteral( "edit.undo" ) ) == command_result_t::OK );
    CheckMasonPosition( bounds( sourceId ).box.minimum, source.box.minimum ); CheckMasonPosition( bounds( sourceId ).box.maximum, source.box.maximum );
    CheckMasonPosition( bounds( targetId ).box.minimum, target.box.minimum ); CheckMasonPosition( bounds( targetId ).box.maximum, target.box.maximum );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws->history.pUndo ) ) );
    REQUIRE( session.Run( QStringLiteral( "edit.redo" ) ) == command_result_t::OK ); checkCommitted();
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1u );
}

TEST_CASE( "Mason resizes a brush through a selected face in its Top viewport", "[mason][smoke][ortho-face-push-pull]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    window->resize( 1600, 1050 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER ); MapViews_SetPaneType( views, 1, map_view_type_t::TOP );
    auto *top = MapViews_PaneView( views, 1 ); auto *camera = MapViews_PaneView( views, 0 ); REQUIRE( top ); REQUIRE( camera );
    MapCameraView_SetRenderMode( camera, map_render_mode_t::FULLBRIGHT );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, 0 } ); MapBounds_AddPoint( box, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_CreateBox( ws, box ) ); const u64 id = EditorSelection_At( &ws->selection, 0u );
    MapWorkspace_SetGridSize( ws, 64 ); MapWorkspace_SetSnapToGrid( ws, CY_TRUE );
    REQUIRE( session.Run( QStringLiteral( "map.tool.select" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.faces" ) ) == command_result_t::OK );
    MapWorkspace_Frame( ws, CY_TRUE ); MapViews_SetActivePane( views, 1 ); QCoreApplication::processEvents();
    // Reserve space for the face's outward handle and its first grid step.
    const QPointF anchor = top->rect().center();
    QWheelEvent zoom( anchor, top->mapToGlobal( anchor ), {}, QPoint( 0, -240 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QCoreApplication::sendEvent( top, &zoom );
    u64 side = 0;
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) {
        const auto &face = ws->wire.faces.pData[i]; if ( face.id == id && face.normal.x > .99 ) { side = face.sideId; break; }
    }
    REQUIRE( side != 0u ); REQUIRE_FALSE( MapWorkspace_HasBrushFace( ws ) );
    const QPointF outline = MapOrthoView_WorldToView( top, { 128, 27 } );
    MasonWorkflowMouse( top, QEvent::MouseButtonPress, outline, Qt::LeftButton, Qt::LeftButton );
    MasonWorkflowMouse( top, QEvent::MouseButtonRelease, outline, Qt::LeftButton );
    REQUIRE( MapWorkspace_HasBrushFace( ws ) ); CHECK( ws->selectedBrushFaceObject == id ); CHECK( ws->selectedBrushFaceSide == side );
    const f64 pixels = 64.0 * EditorSettings_Real( &Mason_Gui( session.pMason )->settings, "editor.viewport.gizmo_scale", 1.0 );
    const QPointF start = MapOrthoView_WorldToView( top, { 128, 0 } ) + QPointF( pixels * .60, 5 );
    const QPointF end = start + QPointF( 57.0 * MapOrthoView_Zoom( top ), 0 );
    REQUIRE( QRectF( top->rect() ).contains( start ) ); REQUIRE( QRectF( top->rect() ).contains( end ) );
    const auto *document = ws->pDocument; const auto revision = document->geometry.revision, selection = ws->selection.revision;
    const usize steps = EditorHistory_StepCount( &ws->history ); const auto token = UndoRedo_StateToken( ws->history.pUndo );
    top->setAttribute( Qt::WA_UnderMouse, true );
    MasonWorkflowMouse( top, QEvent::MouseMove, start, Qt::NoButton ); CHECK( top->cursor().shape() == kViewportMovementCursor );
    MasonWorkflowMouse( top, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton );
    MasonWorkflowMouse( top, QEvent::MouseMove, start, Qt::NoButton, Qt::LeftButton ); CHECK_FALSE( ws->editPreview.bActive );
    MasonWorkflowMouse( top, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton );
    REQUIRE( MapWorkspace_HasFacePreview( ws ) ); REQUIRE( ws->editPreview.status == map_status_t::OK );
    CHECK( ws->editPreview.faceDistance == 64.0 ); CHECK( ws->editPreview.transform.kind == map_transform_preview_kind_t::NONE );
    CHECK( ws->editPreviewWire.points.nCount == 8u ); CHECK( ws->editPreviewWire.faces.nCount == 6u );
    CheckMasonPosition( ws->editPreview.bounds.box.minimum, box.box.minimum ); CheckMasonPosition( ws->editPreview.bounds.box.maximum, { 192, 128, 128 } );
    const auto *live = MapWireframe_FindObject( ws->wire, id ); REQUIRE( live );
    CheckMasonPosition( live->bounds.box.minimum, box.box.minimum ); CheckMasonPosition( live->bounds.box.maximum, box.box.maximum );
    CHECK( ws->pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( ws->selection.revision == selection );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws->history.pUndo ) ) );
    if ( !qEnvironmentVariableIsEmpty( "CYPHER_ORTHO_FACE_CAPTURE" ) ) {
        QCoreApplication::processEvents(); top->setAttribute( Qt::WA_UnderMouse, true );
        REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
        REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_ortho_face_workspace.png" ) ) );
        REQUIRE( top->grab().save( QStringLiteral( "artifacts/mason_ortho_face_top.png" ) ) );
        REQUIRE( camera->grab().save( QStringLiteral( "artifacts/mason_ortho_face_camera.png" ) ) );
    }
    MasonWorkflowMouse( top, QEvent::MouseButtonRelease, end, Qt::LeftButton );
    CHECK_FALSE( ws->editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1u );
    const auto checkCommitted = [&]() {
        const auto *object = MapWireframe_FindObject( ws->wire, id ); REQUIRE( object );
        CheckMasonPosition( object->bounds.box.minimum, box.box.minimum ); CheckMasonPosition( object->bounds.box.maximum, { 192, 128, 128 } );
        CHECK( ws->selectedBrushFaceObject == id ); CHECK( ws->selectedBrushFaceSide == side ); CHECK( ws->tool == map_tool_t::SELECT );
    };
    checkCommitted(); REQUIRE( session.Run( QStringLiteral( "edit.undo" ) ) == command_result_t::OK );
    live = MapWireframe_FindObject( ws->wire, id ); REQUIRE( live ); CheckMasonPosition( live->bounds.box.minimum, box.box.minimum ); CheckMasonPosition( live->bounds.box.maximum, box.box.maximum );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws->history.pUndo ) ) );
    REQUIRE( session.Run( QStringLiteral( "edit.redo" ) ) == command_result_t::OK ); checkCommitted();
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1u ); CHECK( ws->gridSize == 64 ); CHECK( ws->bSnapToGrid );
}

TEST_CASE( "Mason thin-wall resize uses a real 2D boundary without a control cage", "[mason][smoke][simple-2d-selection]" )
{
    mason_session_t session; auto *ws = Mason_MapWorkspace( session.pMason ); auto *window = Mason_Window( session.pMason );
    window->resize( 1600, 1050 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneType( views, 1, map_view_type_t::TOP ); MapViews_SetPaneType( views, 2, map_view_type_t::SIDE );
    auto *top = MapViews_PaneView( views, 1 ); auto *side = MapViews_PaneView( views, 2 ); REQUIRE( top ); REQUIRE( side );
    map_bounds_t floor{}, wall{};
    MapBounds_AddPoint( floor, { 16, -256, -16 } ); MapBounds_AddPoint( floor, { 512, 256, 0 } );
    MapBounds_AddPoint( wall, { 0, -256, 0 } ); MapBounds_AddPoint( wall, { 16, 256, 384 } );
    REQUIRE( MapWorkspace_CreateBox( ws, floor ) ); const u64 floorId = EditorSelection_At( &ws->selection, 0u );
    REQUIRE( MapWorkspace_CreateBox( ws, wall ) ); const u64 wallId = EditorSelection_At( &ws->selection, 0u );
    MapWorkspace_SetGridSize( ws, 64 ); MapWorkspace_SetSnapToGrid( ws, CY_TRUE );
    REQUIRE( session.Run( QStringLiteral( "map.tool.select" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.objects" ) ) == command_result_t::OK );
    MapWorkspace_Frame( ws, CY_TRUE ); MapViews_SetActivePane( views, 1 ); QCoreApplication::processEvents();
    top->setAttribute( Qt::WA_UnderMouse, true );
    // Framing fits the original wall tightly; reserve room for the next grid
    // step so both the real boundary cap and destination stay inside the pane.
    const QPointF center = top->rect().center();
    QWheelEvent outward( center, top->mapToGlobal( center ), {}, QPoint( 0, -240 ), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
    QCoreApplication::sendEvent( top, &outward );
    const f64 zoom = MapOrthoView_Zoom( top );
    REQUIRE( 8 * zoom < 48 ); REQUIRE( 256 * zoom >= 48 );
    const auto capture = [&]( const QString &suffix ) {
        if ( qEnvironmentVariableIsEmpty( "CYPHER_SIMPLE_2D_CAPTURE" ) ) { return; }
        QCoreApplication::processEvents(); REQUIRE( QDir().mkpath( QStringLiteral( "artifacts" ) ) );
        REQUIRE( window->grab().save( QStringLiteral( "artifacts/mason_simple_2d_workspace_" ) + suffix + QStringLiteral( ".png" ) ) );
        REQUIRE( top->grab().save( QStringLiteral( "artifacts/mason_simple_2d_top_" ) + suffix + QStringLiteral( ".png" ) ) );
        REQUIRE( side->grab().save( QStringLiteral( "artifacts/mason_simple_2d_side_" ) + suffix + QStringLiteral( ".png" ) ) );
    };
    MasonWorkflowMouse( top, QEvent::MouseMove, { 20, 30 }, Qt::NoButton ); capture( QStringLiteral( "idle" ) );
    const QPointF start = MapOrthoView_WorldToView( top, { 8, 256 } ) + QPointF( 3, 0 );
    const QPointF end = start - QPointF( 0, 64 * zoom );
    REQUIRE( QRectF( top->rect() ).adjusted( 8, 8, -8, -8 ).contains( start ) );
    REQUIRE( QRectF( top->rect() ).adjusted( 8, 8, -8, -8 ).contains( end ) );
    const auto *document = ws->pDocument; const auto revision = document->geometry.revision;
    const usize steps = EditorHistory_StepCount( &ws->history ); const auto token = UndoRedo_StateToken( ws->history.pUndo );
    MasonWorkflowMouse( top, QEvent::MouseMove, start, Qt::NoButton ); CHECK( top->cursor().shape() == Qt::SizeVerCursor );
    MasonWorkflowMouse( top, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton );
    MasonWorkflowMouse( top, QEvent::MouseMove, start, Qt::NoButton, Qt::LeftButton ); CHECK_FALSE( ws->editPreview.bActive );
    MasonWorkflowMouse( top, QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton );
    REQUIRE( ws->editPreview.bActive ); REQUIRE( ws->editPreview.transform.bResize ); CHECK_FALSE( ws->editPreview.transform.bClone );
    CheckMasonPosition( ws->editPreview.bounds.box.minimum, wall.box.minimum );
    CheckMasonPosition( ws->editPreview.bounds.box.maximum, { 16, 320, 384 } );
    CHECK( ws->pDocument == document ); CHECK( document->geometry.revision == revision );
    CHECK( EditorHistory_StepCount( &ws->history ) == steps ); CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws->history.pUndo ) ) );
    const auto *source = MapWireframe_FindObject( ws->wire, wallId ); REQUIRE( source );
    CheckMasonPosition( source->bounds.box.maximum, wall.box.maximum ); capture( QStringLiteral( "resize" ) );
    MasonWorkflowMouse( top, QEvent::MouseButtonRelease, end, Qt::LeftButton );
    CHECK_FALSE( ws->editPreview.bActive ); CHECK( EditorHistory_StepCount( &ws->history ) == steps + 1u );
    const auto checkCommitted = [&]() {
        const auto *object = MapWireframe_FindObject( ws->wire, wallId ); REQUIRE( object );
        CheckMasonPosition( object->bounds.box.minimum, wall.box.minimum ); CheckMasonPosition( object->bounds.box.maximum, { 16, 320, 384 } );
        const auto *unchanged = MapWireframe_FindObject( ws->wire, floorId ); REQUIRE( unchanged );
        CheckMasonPosition( unchanged->bounds.box.minimum, floor.box.minimum ); CheckMasonPosition( unchanged->bounds.box.maximum, floor.box.maximum );
        CHECK( EditorSelection_Count( &ws->selection ) == 1u ); CHECK( EditorSelection_At( &ws->selection, 0u ) == wallId );
    };
    checkCommitted(); REQUIRE( session.Run( QStringLiteral( "edit.undo" ) ) == command_result_t::OK );
    source = MapWireframe_FindObject( ws->wire, wallId ); REQUIRE( source ); CheckMasonPosition( source->bounds.box.maximum, wall.box.maximum );
    CHECK( UndoRedo_StateTokenEquals( token, UndoRedo_StateToken( ws->history.pUndo ) ) );
    REQUIRE( session.Run( QStringLiteral( "edit.redo" ) ) == command_result_t::OK ); checkCommitted();
    QTemporaryDir saved; REQUIRE( saved.isValid() ); const QString path = saved.path() + QStringLiteral( "/simple-wall.cymap" );
    REQUIRE( MapWorkspace_SaveAs( ws, path ).status == map_files_status_t::OK ); REQUIRE( MapWorkspace_Open( ws, path ).status == map_files_status_t::OK );
    MapWorkspace_Select( ws, wallId, MAP_SELECT_REPLACE ); checkCommitted();
}

// Writes pictures of the mesh component operations in Tool Properties:
// cypher_mason_smoke_tests "[.screenshot]"
TEST_CASE( "Screenshot of Mason edge and vertex operations", "[.screenshot]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason ); auto *ws = Mason_MapWorkspace( session.pMason );
    window->resize( 1600, 1000 );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, 0 } ); MapBounds_AddPoint( box, { 128, 128, 192 } );
    REQUIRE( MapWorkspace_CreateBox( ws, box ) );
    const u64 id = EditorSelection_At( &ws->selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
    const auto *mesh = geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { id } ); REQUIRE( mesh );
    geometry::mesh_source_description_t description{};
    REQUIRE( geometry::MeshSourceDescription_Init( &description, ws->pDocument->pAllocator, { id } ) == geometry::geometry_status_t::OK );
    REQUIRE( geometry::MeshSource_TryDescribe( mesh, &description ) == geometry::geometry_status_t::OK );
    const auto &face = description.faces.pData[0];
    const auto a = description.vertices.pData[description.corners.pData[face.iFirstCorner].iVertex].sourceId;
    const auto b = description.vertices.pData[description.corners.pData[face.iFirstCorner + 1].iVertex].sourceId;
    const auto c = description.vertices.pData[description.corners.pData[face.iFirstCorner + 2].iVertex].sourceId; // Off the bevelled edge.
    geometry::MeshSourceDescription_Shutdown( &description );
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.edges" ) ) == command_result_t::OK );
    REQUIRE( MapWorkspace_SelectMeshEdge( ws, id, geometry::MeshEdgeRef_Make( a, b ) ) );
    QCoreApplication::processEvents();
    CHECK( window->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_edge_operations.png" ).string() ) ) );
    REQUIRE( session.Run( QStringLiteral( "map.mesh.bevel" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.vertices" ) ) == command_result_t::OK );
    REQUIRE( MapWorkspace_SelectMeshVertex( ws, id, c ) );
    QCoreApplication::processEvents();
    CHECK( window->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_vertex_operations.png" ).string() ) ) );
    // Shaded, framed close: the candidate vertices in the 3D view's depth order.
    REQUIRE( session.Run( QStringLiteral( "map.render.shaded" ) ) == command_result_t::OK );
    MapWorkspace_Frame( ws, CY_TRUE );
    QCoreApplication::processEvents();
    CHECK( window->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_vertex_shaded.png" ).string() ) ) );
    // The component gizmo on the selected vertex.
    REQUIRE( session.Run( QStringLiteral( "map.tool.translate" ) ) == command_result_t::OK );
    QCoreApplication::processEvents();
    CHECK( window->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_vertex_gizmo.png" ).string() ) ) );
}

// The Entity I/O window on the example map's first wave:
// cypher_mason_smoke_tests "[.screenshot]"
TEST_CASE( "Screenshot of the Entity I/O window", "[.screenshot]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *ws = Mason_MapWorkspace( session.pMason );
    MapWorkspace_Select( ws, 120, MAP_SELECT_REPLACE );
    QDialog *dialog = MapEntityIo_Show( ws, Mason_Window( session.pMason ) );
    REQUIRE( dialog != nullptr );
    QCoreApplication::processEvents();
    CHECK( dialog->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_entity_io.png" ).string() ) ) );
    if ( auto *tabs = dialog->findChild<QTabWidget *>() ) { tabs->setCurrentIndex( 1 ); QCoreApplication::processEvents(); }
    CHECK( dialog->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_entity_io_inputs.png" ).string() ) ) );
}

// The right-click editing menu in the Top view, on a selected brush and in
// Vertices mode: cypher_mason_smoke_tests "[.screenshot]"
TEST_CASE( "Screenshot of the view editing menu", "[.screenshot]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason ); auto *ws = Mason_MapWorkspace( session.pMason );
    window->resize( 1600, 1000 );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -128, -128, 0 } ); MapBounds_AddPoint( box, { 128, 128, 192 } );
    REQUIRE( MapWorkspace_CreateBox( ws, box ) );
    QCoreApplication::processEvents();
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views != nullptr );
    QWidget *top = MapViews_PaneView( views, 1 ); REQUIRE( top != nullptr );
    const auto shoot = [&]( const char *file, QPointF world ) {
        const QPoint at = MapOrthoView_WorldToView( top, world ).toPoint();
        QContextMenuEvent context( QContextMenuEvent::Mouse, at, top->mapToGlobal( at ) );
        QCoreApplication::sendEvent( top, &context );
        QCoreApplication::processEvents();
        for ( QMenu *menu : top->findChildren<QMenu *>( QStringLiteral( "MapViewMenu" ) ) ) {
            if ( !menu->isVisible() ) { continue; }
            CHECK( menu->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / file ).string() ) ) );
            menu->close();
        }
        QCoreApplication::processEvents();
    };
    shoot( "mason_view_menu_objects.png", { -128, 0 } );
    REQUIRE( session.Run( QStringLiteral( "map.brush.to_mesh" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.vertices" ) ) == command_result_t::OK );
    shoot( "mason_view_menu_vertices.png", { 300, 300 } );
}

// Lists every toolbar button with its command state, for auditing what is
// wired: cypher_mason_smoke_tests "[.inventory]"
TEST_CASE( "Inventory of Mason toolbar commands", "[.inventory]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason ); auto *ws = Mason_MapWorkspace( session.pMason );
    const QByteArray mapPath = qgetenv( "MASON_INVENTORY_MAP" );
    if ( !mapPath.isEmpty() ) { REQUIRE( Mason_OpenMap( session.pMason, QString::fromUtf8( mapPath ) ) ); }
    QCoreApplication::processEvents();
    const auto *pGui = Mason_Gui( session.pMason );
    // Every action the window owns (menus, toolbars, dropdowns, tool strip),
    // once per command.
    QSet<QByteArray> seen;
    for ( QAction *action : window->findChildren<QAction *>() ) {
        if ( action->objectName().isEmpty() || seen.contains( action->objectName().toUtf8() ) ) { continue; }
        seen.insert( action->objectName().toUtf8() );
        const QByteArray id = action->objectName().toUtf8();
        const string_view_t sv{ id.constData(), static_cast<usize>( id.size() ) };
        const auto *command = EditorCommands_Find( &pGui->commands, sv );
        if ( command == nullptr ) { continue; }
        const bool planned = MapWorkspace_IsPlannedCommand( &pGui->commands, sv );
        const bool enabled = ( EditorCommands_State( &pGui->commands, sv ) & COMMAND_STATE_ENABLED ) != 0;
        std::printf( "ALL\t%s\t%s\t%s\t%s\n", id.constData(), planned ? "PLANNED" : "WIRED", enabled ? "enabled" : "disabled", command->pLabel );
    }
    for ( QToolBar *bar : window->findChildren<QToolBar *>() ) {
        for ( QAction *action : bar->actions() ) {
            const QByteArray id = action->objectName().toUtf8();
            if ( id.isEmpty() || action->isSeparator() ) { continue; }
            const bool planned = MapWorkspace_IsPlannedCommand( &pGui->commands, string_view_t{ id.constData(), static_cast<usize>( id.size() ) } );
            const bool known = EditorCommands_Find( &pGui->commands, string_view_t{ id.constData(), static_cast<usize>( id.size() ) } ) != nullptr;
            std::printf( "INVENTORY\t%s\t%s\t%s\t%s\n", bar->objectName().toUtf8().constData(), id.constData(),
                         planned ? "PLANNED" : known ? "WIRED" : "OTHER", action->isEnabled() ? "enabled" : "disabled" );
        }
    }
    // Every registered command, including those only the Tool Properties
    // panel or the console reach.
    for ( usize i = 0; i < EditorCommands_Count( &pGui->commands ); ++i ) {
        const auto *command = EditorCommands_At( &pGui->commands, i );
        const string_view_t sv = StringView_FromCString( command->pId );
        std::printf( "REGISTRY\t%s\t%s\t%s\n", command->pId, MapWorkspace_IsPlannedCommand( &pGui->commands, sv ) ? "PLANNED" : "WIRED", command->pLabel );
    }
    ( void )ws;
}

// Opens a map (MASON_INVENTORY_MAP), clicks the first brush's corner in the Top
// view in Edges mode and writes the window: cypher_mason_smoke_tests "[.edgeclick]"
TEST_CASE( "Screenshot of a brush edge clicked in Edges mode", "[.edgeclick]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason ); auto *ws = Mason_MapWorkspace( session.pMason );
    const QByteArray mapPath = qgetenv( "MASON_INVENTORY_MAP" );
    REQUIRE_FALSE( mapPath.isEmpty() );
    REQUIRE( Mason_OpenMap( session.pMason, QString::fromUtf8( mapPath ) ) );
    window->resize( 2000, 1250 ); window->show(); QCoreApplication::processEvents();
    const map_wire_object_t *brush = nullptr;
    for ( usize i = 0; i < ws->wire.objects.nCount && brush == nullptr; ++i ) {
        if ( ws->wire.objects.pData[i].kind == map_wire_kind_t::BRUSH ) { brush = &ws->wire.objects.pData[i]; }
    }
    REQUIRE( brush != nullptr );
    const u64 id = brush->id; const auto corner = brush->bounds.box.maximum;
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    QWidget *top = nullptr;
    for ( int i = 0; i < 4 && top == nullptr; ++i ) {
        QWidget *pane = MapViews_PaneView( views, i );
        if ( pane != nullptr && pane->objectName() == QStringLiteral( "mapTopView" ) ) { top = pane; }
    }
    REQUIRE( top != nullptr );
    MapWorkspace_Frame( ws, CY_FALSE ); QCoreApplication::processEvents();
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.edges" ) ) == command_result_t::OK );
    const QPointF at = MapOrthoView_WorldToView( top, { corner.x, ( brush->bounds.box.minimum.y + corner.y ) / 2 } );
    QMouseEvent press( QEvent::MouseButtonPress, at, top->mapToGlobal( at ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &press );
    QMouseEvent release( QEvent::MouseButtonRelease, at, top->mapToGlobal( at ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
    QCoreApplication::sendEvent( top, &release );
    QCoreApplication::processEvents();
    CHECK( geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { id } ) != nullptr );
    CHECK( ws->meshSelection.edges.nCount == 1u );
    CHECK( window->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_edge_click.png" ).string() ) ) );
}

TEST_CASE( "Screenshots of the Clip tool cutting a brush and a mesh across a face in 3D", "[.clip-screenshot]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason ); auto *ws = Mason_MapWorkspace( session.pMason );
    window->resize( 2000, 1250 ); window->show(); QCoreApplication::processEvents();
    const auto box = [&]( math::vec3d_t minimum, math::vec3d_t maximum ) {
        map_bounds_t bounds{}; MapBounds_AddPoint( bounds, minimum ); MapBounds_AddPoint( bounds, maximum );
        REQUIRE( MapWorkspace_CreateBox( ws, bounds ) ); return EditorSelection_At( &ws->selection, 0 );
    };
    box( { -384, -384, -16 }, { 384, 384, 0 } );
    const u64 brush = box( { -256, -96, 0 }, { -64, 96, 160 } );
    const u64 mesh = box( { 64, -96, 0 }, { 256, 96, 160 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
    MapWorkspace_Select( ws, brush, MAP_SELECT_REPLACE ); MapWorkspace_Select( ws, mesh, MAP_SELECT_ADD );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::SHADED );
    MapWorkspace_Frame( ws, CY_FALSE ); QCoreApplication::processEvents();
    REQUIRE( session.Run( QStringLiteral( "map.tool.clip" ) ) == command_result_t::OK );
    const auto *mode = EditorSettings_Find( &ws->pGui->settings, StringView_FromCString( "editor.map.clip_mode" ) ); REQUIRE( mode != nullptr );
    setting_value_t value{}; value.type = setting_type_t::ENUM; value.text = StringView_FromCString( "back" );
    REQUIRE( EditorSettings_Write( &ws->pGui->settings, settings_scope_t::USER, *mode, value ) == settings_registry_status_t::OK );
    QWidget *camera = MapViews_PaneView( views, 0 ); REQUIRE( camera != nullptr );
    const auto send = [&]( QEvent::Type type, math::vec3d_t p ) {
        QPointF at; REQUIRE( MapCameraView_WorldToView( camera, p, &at ) );
        QMouseEvent event( type, at, camera->mapToGlobal( at ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                           type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier );
        QCoreApplication::sendEvent( camera, &event );
    };
    // Across the top of the brush, diagonally.
    send( QEvent::MouseButtonPress, { -224, -64, 160 } ); send( QEvent::MouseMove, { -160, 0, 160 } ); send( QEvent::MouseMove, { -96, 64, 160 } );
    send( QEvent::MouseButtonRelease, { -96, 64, 160 } );
    QCoreApplication::processEvents();
    REQUIRE( ws->editPreview.bClip ); REQUIRE( ws->editPreview.status == map_status_t::OK );
    const auto folder = std::filesystem::temp_directory_path();
    CHECK( window->grab().save( QString::fromStdString( ( folder / "mason_clip_surface_staged.png" ).string() ) ) );
    QKeyEvent confirm( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &confirm );
    QCoreApplication::processEvents();
    CHECK_FALSE( ws->editPreview.bActive );
    CHECK( window->grab().save( QString::fromStdString( ( folder / "mason_clip_surface_done.png" ).string() ) ) );
}

TEST_CASE( "Screenshots of Vertices and Edges mode on the object being edited", "[.component-screenshot]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason ); auto *ws = Mason_MapWorkspace( session.pMason );
    window->resize( 2000, 1250 ); window->show(); QCoreApplication::processEvents();
    const auto box = [&]( math::vec3d_t minimum, math::vec3d_t maximum ) {
        map_bounds_t bounds{}; MapBounds_AddPoint( bounds, minimum ); MapBounds_AddPoint( bounds, maximum );
        REQUIRE( MapWorkspace_CreateBox( ws, bounds ) ); return EditorSelection_At( &ws->selection, 0 );
    };
    box( { -384, -384, -16 }, { 384, 384, 0 } );
    box( { 160, -96, 0 }, { 288, 96, 96 } );
    const u64 mesh = box( { -160, -96, 0 }, { 64, 96, 128 } );
    REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::SHADED );
    MapWorkspace_Frame( ws, CY_FALSE ); QCoreApplication::processEvents();
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.vertices" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "map.tool.select" ) ) == command_result_t::OK );
    MapWorkspace_Select( ws, mesh, MAP_SELECT_REPLACE );
    const auto *source = geometry::GeometryDocument_FindMesh( &ws->pDocument->geometry, { mesh } ); REQUIRE( source != nullptr );
    const auto *object = MapWireframe_FindObject( ws->wire, mesh ); REQUIRE( object != nullptr );
    REQUIRE( MapWorkspace_SelectMeshVertex( ws, mesh, { ws->wire.pointSourceIds.pData[object->iFirstPoint] } ) );
    QCoreApplication::processEvents();
    const auto folder = std::filesystem::temp_directory_path();
    CHECK( window->grab().save( QString::fromStdString( ( folder / "mason_vertices_mode.png" ).string() ) ) );
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.edges" ) ) == command_result_t::OK );
    MapWorkspace_Select( ws, mesh, MAP_SELECT_REPLACE );
    const auto &line = ws->wire.lines.pData[object->iFirstLine];
    REQUIRE( MapWorkspace_SelectMeshEdge( ws, mesh, geometry::MeshEdgeRef_Make( { ws->wire.pointSourceIds.pData[line.iA] }, { ws->wire.pointSourceIds.pData[line.iB] } ) ) );
    QCoreApplication::processEvents();
    CHECK( window->grab().save( QString::fromStdString( ( folder / "mason_edges_mode.png" ).string() ) ) );
}

TEST_CASE( "Object Properties shows an entity's Outputs and Inputs beside its properties", "[mason][smoke][entity-io]" )
{
    mason_session_t session;
    REQUIRE( Mason_OpenMap( session.pMason, ExampleRoot() ) );
    auto *window = Mason_Window( session.pMason ); auto *ws = Mason_MapWorkspace( session.pMason );
    window->resize( 2000, 1250 ); window->show(); QCoreApplication::processEvents();
    auto *tabs = window->findChild<QTabWidget *>( QStringLiteral( "MasonPropertiesTabs" ) ); REQUIRE( tabs != nullptr );
    REQUIRE( tabs->count() == 3 );
    CHECK( tabs->tabText( 0 ) == QStringLiteral( "Properties" ) );
    MapWorkspace_Select( ws, 120, MAP_SELECT_REPLACE ); QCoreApplication::processEvents();
    CHECK( tabs->tabText( 1 ).startsWith( QStringLiteral( "Outputs (" ) ) ); CHECK( tabs->tabText( 2 ).startsWith( QStringLiteral( "Inputs (" ) ) );
    auto *outputs = tabs->findChild<QTableWidget *>( QStringLiteral( "MapEntityOutputs" ) ); REQUIRE( outputs != nullptr );
    CHECK( tabs->tabText( 1 ) == QStringLiteral( "Outputs (%1)" ).arg( outputs->rowCount() ) );
    tabs->setCurrentIndex( 1 ); QCoreApplication::processEvents();
    CHECK( window->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_properties_outputs.png" ).string() ) ) );
}

TEST_CASE( "Screenshot of Faces mode with a mesh face picked", "[.faces-screenshot]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason ); auto *ws = Mason_MapWorkspace( session.pMason );
    window->resize( 2000, 1400 ); window->show(); QCoreApplication::processEvents();
    map_bounds_t bounds{}; MapBounds_AddPoint( bounds, { -128, -128, 0 } ); MapBounds_AddPoint( bounds, { 128, 128, 64 } );
    REQUIRE( MapWorkspace_CreateBox( ws, bounds ) ); const u64 mesh = EditorSelection_At( &ws->selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
    MapWorkspace_Frame( ws, CY_FALSE );
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.faces" ) ) == command_result_t::OK );
    u64 top = 0;
    for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) { if ( ws->wire.faces.pData[f].id == mesh && ws->wire.faces.pData[f].normal.z > 0.99 ) { top = ws->wire.faces.pData[f].faceId; } }
    MapWorkspace_SelectMeshFace( ws, mesh, top );
    QCoreApplication::processEvents();
    CHECK( window->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_faces_mode.png" ).string() ) ) );
}

TEST_CASE( "Screenshot of a subdivided, mirrored mesh with its cage", "[.modifiers-screenshot]" )
{
    mason_session_t session;
    auto *window = Mason_Window( session.pMason ); auto *ws = Mason_MapWorkspace( session.pMason );
    window->resize( 2000, 1250 ); window->show(); QCoreApplication::processEvents();
    map_bounds_t floor{}; MapBounds_AddPoint( floor, { -384, -384, -16 } ); MapBounds_AddPoint( floor, { 384, 384, 0 } );
    REQUIRE( MapWorkspace_CreateBox( ws, floor ) );
    map_bounds_t bounds{}; MapBounds_AddPoint( bounds, { 16, -96, 0 } ); MapBounds_AddPoint( bounds, { 176, 96, 160 } );
    REQUIRE( MapWorkspace_CreateBox( ws, bounds ) );
    REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::SHADED );
    REQUIRE( session.Run( QStringLiteral( "map.mesh.modifier_mirror_x" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "map.mesh.subdivision_level_2" ) ) == command_result_t::OK );
    MapWorkspace_Frame( ws, CY_FALSE ); QCoreApplication::processEvents();
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.vertices" ) ) == command_result_t::OK );
    QCoreApplication::processEvents();
    CHECK( window->grab().save( QString::fromStdString( ( std::filesystem::temp_directory_path() / "mason_modifiers.png" ).string() ) ) );
}

TEST_CASE( "Capture Mason selection handles and the 3D vertex clip", "[.clip-handles-capture]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    Mason_Window( session.pMason )->resize( 2200, 1360 );
    const auto box = [&]( math::vec3d_t minimum, math::vec3d_t maximum ) {
        map_bounds_t bounds{}; MapBounds_AddPoint( bounds, minimum ); MapBounds_AddPoint( bounds, maximum );
        REQUIRE( MapWorkspace_CreateBox( ws, bounds ) );
        return EditorSelection_At( &ws->selection, 0 );
    };
    box( { -384, -384, -16 }, { 384, 384, 0 } );
    const u64 block = box( { -128, -96, 0 }, { 128, 96, 128 } );
    auto *views = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::SHADED );
    MapWorkspace_Select( ws, block, MAP_SELECT_REPLACE );
    MapWorkspace_Frame( ws, CY_TRUE ); MapWorkspace_SetTool( ws, map_tool_t::SELECT );
    QCoreApplication::processEvents();
    const QString folder = QDir::tempPath();
    REQUIRE( Mason_Window( session.pMason )->grab().save( folder + QStringLiteral( "/mason_handles.png" ) ) );
    // The Clip tool in 3D: corner to corner across the top, then a third point on a side.
    MapWorkspace_SetTool( ws, map_tool_t::CLIP );
    QWidget *camera = MapViews_PaneView( views, 0 ); REQUIRE( camera != nullptr );
    const auto click = [&]( math::vec3d_t p, QPointF nudge ) {
        QPointF at; REQUIRE( MapCameraView_WorldToView( camera, p, &at ) ); at += nudge;
        QMouseEvent press( QEvent::MouseButtonPress, at, camera->mapToGlobal( at ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
        QCoreApplication::sendEvent( camera, &press );
        QMouseEvent release( QEvent::MouseButtonRelease, at, camera->mapToGlobal( at ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
        QCoreApplication::sendEvent( camera, &release );
    };
    click( { -128, -96, 128 }, { 4, 3 } );
    click( { 128, 96, 128 }, { -3, 4 } );
    QCoreApplication::processEvents();
    REQUIRE( ws->editPreview.bActive );
    REQUIRE( Mason_Window( session.pMason )->grab().save( folder + QStringLiteral( "/mason_clip_vertices.png" ) ) );
    const auto eye = MapCameraView_Position( camera );
    click( eye.x < -128 ? math::vec3d_t{ -128, 0, 32 } : eye.y < -96 ? math::vec3d_t{ 0, -96, 32 } : math::vec3d_t{ 128, 0, 32 }, {} );
    QCoreApplication::processEvents();
    REQUIRE( Mason_Window( session.pMason )->grab().save( folder + QStringLiteral( "/mason_clip_three.png" ) ) );
}

TEST_CASE( "Capture the Fast Texture window", "[.fast-texture-capture]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    map_bounds_t bounds{}; MapBounds_AddPoint( bounds, { -128, -128, 0 } ); MapBounds_AddPoint( bounds, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_CreateBox( ws, bounds ) );
    REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
    const u64 mesh = EditorSelection_At( &ws->selection, 0 );
    u64 top = 0;
    for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) { if ( ws->wire.faces.pData[f].id == mesh && ws->wire.faces.pData[f].normal.z > 0.99 ) { top = ws->wire.faces.pData[f].faceId; } }
    MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES );
    MapWorkspace_SelectMeshFace( ws, mesh, top );
    QDialog *window = MapFastTexture_Show( ws, Mason_Window( session.pMason ) );
    REQUIRE( window != nullptr );
    window->resize( 560, 640 ); QCoreApplication::processEvents();
    REQUIRE( MapFastTexture_Choose( window, QRectF( 0.25, 0.5, 0.5, 0.25 ) ) );
    QCoreApplication::processEvents();
    REQUIRE( window->grab().save( QDir::tempPath() + QStringLiteral( "/mason_fast_texture.png" ) ) );
}

TEST_CASE( "Capture the new block shapes, gizmo and group outlines", "[.shapes-capture]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    Mason_Window( session.pMason )->resize( 2200, 1360 );
    map_bounds_t floor{}; MapBounds_AddPoint( floor, { -640, -640, -16 } ); MapBounds_AddPoint( floor, { 640, 640, 0 } );
    REQUIRE( MapWorkspace_CreateBox( ws, floor ) );
    const map_primitive_kind_t kinds[]{ map_primitive_kind_t::ARCH, map_primitive_kind_t::STAIRS, map_primitive_kind_t::SPIRAL_STAIRS, map_primitive_kind_t::PIPE, map_primitive_kind_t::TORUS };
    std::vector<u64> made;
    for ( int i = 0; i < 5; ++i ) {
        map_primitive_desc_t desc = MapWorkspace_PrimitiveDefaults( ws, {} );
        desc.kind = kinds[i]; desc.nSides = 16;
        const f64 x = -560 + i * 230;
        desc.bounds = { { x, -96, 0 }, { x + 192, 96, i == 1 ? 128.0 : 160.0 } };
        if ( kinds[i] == map_primitive_kind_t::ARCH ) { desc.axis = 1; desc.bounds = { { x, -24, 0 }, { x + 192, 24, 192 } }; }
        REQUIRE( MapWorkspace_CreatePrimitive( ws, desc ) );
        made.push_back( EditorSelection_At( &ws->selection, 0 ) );
    }
    auto *views = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::SHADED );
    // Group the arch and the stairs, show Groups mode outlines.
    MapWorkspace_SetSelection( ws, made.data(), 2 );
    REQUIRE( EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( "map.group.create" ), {} ) == command_result_t::OK );
    MapWorkspace_SetElementMode( ws, map_element_mode_t::GROUPS );
    MapWorkspace_Select( ws, made[0], MAP_SELECT_REPLACE );
    MapWorkspace_Frame( ws, CY_FALSE ); MapWorkspace_SetTool( ws, map_tool_t::BLOCK );
    QCoreApplication::processEvents();
    REQUIRE( Mason_Window( session.pMason )->grab().save( QDir::tempPath() + QStringLiteral( "/mason_shapes.png" ) ) );
    MapWorkspace_SetTool( ws, map_tool_t::SELECT ); MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
    MapWorkspace_Select( ws, made[2], MAP_SELECT_REPLACE ); QCoreApplication::processEvents();
    REQUIRE( Mason_Window( session.pMason )->grab().save( QDir::tempPath() + QStringLiteral( "/mason_move_gizmo.png" ) ) );
    MapWorkspace_SetTool( ws, map_tool_t::DISPLACE ); QCoreApplication::processEvents();
    REQUIRE( Mason_Window( session.pMason )->grab().save( QDir::tempPath() + QStringLiteral( "/mason_displace_panel.png" ) ) );
}

TEST_CASE( "Capture the entity classes from the game profile and the selection sets", "[.game-capture]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    Mason_Window( session.pMason )->resize( 2200, 1360 );
    map_bounds_t floor{}; MapBounds_AddPoint( floor, { -640, -640, -16 } ); MapBounds_AddPoint( floor, { 640, 640, 0 } );
    REQUIRE( MapWorkspace_CreateBox( ws, floor ) );
    std::vector<u64> boxes;
    for ( int i = 0; i < 3; ++i ) {
        map_bounds_t box{}; MapBounds_AddPoint( box, { -400.0 + i * 300, -64, 0 } ); MapBounds_AddPoint( box, { -272.0 + i * 300, 64, 128 } );
        REQUIRE( MapWorkspace_CreateBox( ws, box ) );
        boxes.push_back( EditorSelection_At( &ws->selection, 0 ) );
    }
    MapWorkspace_SetSelection( ws, boxes.data(), 2 );
    REQUIRE( EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( "map.selection_set.create" ), {} ) == command_result_t::OK );
    MapWorkspace_SetSelection( ws, &boxes[2], 1 );
    REQUIRE( EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( "map.selection_set.create" ), {} ) == command_result_t::OK );
    INFO( MapGame_Status( ws ).toStdString() );
    REQUIRE( MapGame_Profile( ws ) != nullptr );
    MapWorkspace_SetTool( ws, map_tool_t::ENTITY ); QCoreApplication::processEvents();
    REQUIRE( Mason_Window( session.pMason )->grab().save( QDir::tempPath() + QStringLiteral( "/mason_entity_classes.png" ) ) );
}

// Drives every tool through the real panes (mouse and keys sent to the view
// widgets, as a user would) on a fresh scene, and prints what each changed.
// Run on demand: cypher_mason_smoke_tests "[.tool-audit]".
TEST_CASE( "Audit every tool through the real views", "[.tool-audit]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *window = Mason_Window( session.pMason );
    window->resize( 2200, 1360 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::TWO );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::SHADED );
    MapViews_SetPaneView( views, 1, map_view_type_t::TOP, map_render_mode_t::WIREFRAME );
    QCoreApplication::processEvents();
    QWidget *camera = MapViews_PaneView( views, 0 ), *top = MapViews_PaneView( views, 1 );
    REQUIRE( camera != nullptr ); REQUIRE( top != nullptr );

    struct scene_t { u64 floor{}, brush{}, mesh{}, grid{}; } scene;
    const auto box = [&]( math::vec3d_t lo, math::vec3d_t hi ) {
        map_bounds_t b{}; MapBounds_AddPoint( b, lo ); MapBounds_AddPoint( b, hi );
        REQUIRE( MapWorkspace_CreateBox( ws, b ) );
        return EditorSelection_At( &ws->selection, 0 );
    };
    const auto build = [&]() {
        MapWorkspace_SetTool( ws, map_tool_t::SELECT );
        MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
        REQUIRE( MapWorkspace_New( ws ) == map_status_t::OK );
        scene.floor = box( { -640, -640, -16 }, { 640, 640, 0 } );
        scene.brush = box( { -256, -64, 0 }, { -128, 64, 128 } );
        scene.mesh = box( { 0, -64, 0 }, { 128, 64, 128 } );
        REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
        const u64 base = box( { 256, -128, 0 }, { 512, 128, 32 } );
        u64 side = 0;
        for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) { if ( ws->wire.faces.pData[f].id == base && ws->wire.faces.pData[f].normal.z > 0.99 ) { side = ws->wire.faces.pData[f].sideId; } }
        MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES );
        MapWorkspace_SelectBrushFace( ws, base, side );
        REQUIRE( EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( "map.mesh.create_displacement" ), {} ) == command_result_t::OK );
        scene.grid = EditorSelection_At( &ws->selection, 0 );
        MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
        MapWorkspace_SetSelection( ws, nullptr, 0 );
        MapWorkspace_SetTool( ws, map_tool_t::SELECT );
        MapWorkspace_Frame( ws, CY_FALSE );
        MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "-" ) ); // So the report shows only what the tool says.
        QCoreApplication::processEvents();
    };
    const auto mouse = [&]( QWidget *view, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons held, Qt::KeyboardModifiers modifiers ) {
        QMouseEvent event( type, at, view->mapToGlobal( at ), button, held, modifiers );
        QCoreApplication::sendEvent( view, &event );
    };
    const auto click = [&]( QWidget *view, QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        mouse( view, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton, modifiers );
        mouse( view, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, modifiers );
        mouse( view, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, modifiers );
        QCoreApplication::processEvents();
    };
    const auto drag = [&]( QWidget *view, QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        mouse( view, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton, modifiers );
        mouse( view, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers );
        for ( int i = 1; i <= 12; ++i ) { mouse( view, QEvent::MouseMove, from + ( to - from ) * ( i / 12.0 ), Qt::NoButton, Qt::LeftButton, modifiers ); }
        mouse( view, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers );
        QCoreApplication::processEvents();
    };
    const auto key = [&]( QWidget *view, Qt::Key k ) {
        view->setFocus();
        QKeyEvent press( QEvent::KeyPress, k, Qt::NoModifier ); QCoreApplication::sendEvent( view, &press );
        QKeyEvent release( QEvent::KeyRelease, k, Qt::NoModifier ); QCoreApplication::sendEvent( view, &release );
        QCoreApplication::processEvents();
    };
    const auto at3 = [&]( math::vec3d_t p ) { QPointF out; const bool ok = MapCameraView_WorldToView( camera, p, &out ) != CY_FALSE; CHECK( ok ); return out; };
    const auto at2 = [&]( f64 x, f64 y ) { return MapOrthoView_WorldToView( top, { x, y } ); };
    const auto boundsOf = [&]( u64 id ) { const auto *o = MapWireframe_FindObject( ws->wire, id ); return o != nullptr ? o->bounds.box : math::aabbd_t{}; };
    const auto select = [&]( u64 id ) { MapWorkspace_SetSelection( ws, &id, 1 ); QCoreApplication::processEvents(); };

    struct snap_t { u64 revision{}; usize objects{}, entities{}, steps{}; std::vector<u64> selection; };
    const auto snap = [&]() {
        snap_t s{ ws->pDocument->geometry.revision, ws->wire.objects.nCount, ws->wire.entities.nCount, EditorHistory_StepCount( &ws->history ), {} };
        s.selection.assign( ws->selection.ids.pData, ws->selection.ids.pData + ws->selection.ids.nCount );
        return s;
    };
    int tool = 0;
    const auto report = [&]( const char *name, const snap_t &before, const char *expect ) {
        const snap_t after = snap();
        map_notice_t kind{};
        const QString notice = MapNotice_Last( ws, &kind );
        std::printf( "AUDIT %-12s steps %+d objects %+d entities %+d rev %s sel %zu | expect: %-28s | notice: %s\n", name,
            static_cast<int>( after.steps ) - static_cast<int>( before.steps ), static_cast<int>( after.objects ) - static_cast<int>( before.objects ),
            static_cast<int>( after.entities ) - static_cast<int>( before.entities ), after.revision != before.revision ? "changed" : "same ",
            after.selection.size(), expect, notice.toUtf8().constData() );
        ( void )window->grab().save( QDir::tempPath() + QStringLiteral( "/audit_%1_%2.png" ).arg( tool++, 2, 10, QLatin1Char( '0' ) ).arg( QString::fromLatin1( name ) ) );
    };

    // Selection tool: a click selects.
    build(); { const auto b = snap(); click( camera, at3( { -192, 0, 128 } ) ); report( "select", b, "selection = brush" ); CHECK( ws->selection.ids.nCount == 1 ); }
    // Move: drag the gizmo's centre in Top.
    build(); { select( scene.brush ); MapWorkspace_SetTool( ws, map_tool_t::TRANSLATE ); QCoreApplication::processEvents();
        const auto before = boundsOf( scene.brush ); const auto b = snap(); drag( top, at2( -192, 0 ), at2( -192, 128 ) );
        report( "translate", b, "brush moves +y" ); std::printf( "      brush y %.1f -> %.1f\n", before.minimum.y, boundsOf( scene.brush ).minimum.y ); }
    build(); { select( scene.brush ); MapWorkspace_SetTool( ws, map_tool_t::ROTATE ); QCoreApplication::processEvents();
        const auto b = snap(); drag( top, at2( -192 + 70, 0 ), at2( -192, 70 ) ); report( "rotate", b, "brush rotates" ); }
    build(); { select( scene.brush ); MapWorkspace_SetTool( ws, map_tool_t::SCALE ); QCoreApplication::processEvents();
        const auto b = snap(); drag( top, at2( -128, 0 ), at2( -64, 0 ) ); report( "scale", b, "brush grows in x" ); }
    // Block: drag a rectangle in Top, Enter.
    build(); { MapWorkspace_SetTool( ws, map_tool_t::BLOCK ); QCoreApplication::processEvents();
        const auto b = snap(); drag( top, at2( -500, 300 ), at2( -400, 400 ) ); key( top, Qt::Key_Return ); report( "block", b, "objects +1" ); }
    // Clip: a line across the brush in Top, Enter.
    build(); { select( scene.brush ); MapWorkspace_SetTool( ws, map_tool_t::CLIP ); QCoreApplication::processEvents();
        const auto b = snap(); drag( top, at2( -320, 0 ), at2( -64, 0 ) ); key( top, Qt::Key_Return ); report( "clip-2d", b, "brush split (objects +1)" ); }
    build(); { select( scene.mesh ); MapWorkspace_SetTool( ws, map_tool_t::CLIP ); QCoreApplication::processEvents();
        const auto b = snap(); click( camera, at3( { 0, 0, 128 } ) ); click( camera, at3( { 128, 0, 128 } ) ); key( camera, Qt::Key_Return ); report( "clip-3d", b, "mesh split" ); }
    // Vertex: drag a corner of the mesh in Top.
    build(); { select( scene.mesh ); MapWorkspace_SetTool( ws, map_tool_t::VERTEX ); QCoreApplication::processEvents();
        const auto b = snap(); drag( top, at2( 128, 64 ), at2( 192, 128 ) ); report( "vertex", b, "mesh changes" ); }
    build(); { select( scene.brush ); MapWorkspace_SetTool( ws, map_tool_t::VERTEX ); QCoreApplication::processEvents();
        const auto b = snap(); drag( top, at2( -128, 64 ), at2( -96, 96 ) ); report( "vertex-brush", b, "brush changes" ); }
    // Extrude: drag the mesh's top face up in 3D.
    build(); { select( scene.mesh ); MapWorkspace_SetTool( ws, map_tool_t::EXTRUDE ); QCoreApplication::processEvents();
        const auto b = snap(); drag( camera, at3( { 64, 0, 128 } ), at3( { 64, 0, 220 } ) ); report( "extrude", b, "mesh changes" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::EXTRUDE ); QCoreApplication::processEvents();
        const auto b = snap(); drag( camera, at3( { -192, 0, 128 } ), at3( { -192, 0, 220 } ) ); report( "extrude-brush", b, "brush changes" ); }
    // Texture: a click picks a face.
    build(); { MapWorkspace_SetTool( ws, map_tool_t::TEXTURE ); QCoreApplication::processEvents();
        const auto b = snap(); click( camera, at3( { -192, 0, 128 } ) ); report( "texture", b, "face picked" );
        std::printf( "      picked brush face: object %llu side %llu\n", static_cast<unsigned long long>( ws->selectedBrushFaceObject ), static_cast<unsigned long long>( ws->selectedBrushFaceSide ) ); }
    // Entity, decal, overlay: a click places one.
    build(); { MapWorkspace_SetTool( ws, map_tool_t::ENTITY ); QCoreApplication::processEvents(); const auto b = snap(); click( top, at2( 300, -400 ) ); report( "entity", b, "entities +1" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::DECAL ); QCoreApplication::processEvents(); const auto b = snap(); click( camera, at3( { -400, 0, 0 } ) ); report( "decal", b, "entities +1" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::OVERLAY ); QCoreApplication::processEvents(); const auto b = snap(); click( camera, at3( { -400, 0, 0 } ) ); report( "overlay", b, "entities +1" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::PATH ); QCoreApplication::processEvents(); const auto b = snap();
        click( top, at2( 300, -400 ) ); click( top, at2( 450, -400 ) ); key( top, Qt::Key_Return ); report( "path", b, "entities +2" ); }
    // Drawing tools in Top.
    build(); { MapWorkspace_SetTool( ws, map_tool_t::POLYGON ); QCoreApplication::processEvents(); const auto b = snap();
        click( top, at2( -500, -500 ) ); click( top, at2( -400, -500 ) ); click( top, at2( -400, -400 ) ); click( top, at2( -500, -400 ) ); key( top, Qt::Key_Return ); report( "polygon", b, "objects +1" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::CURVE ); QCoreApplication::processEvents(); const auto b = snap();
        click( top, at2( -500, -500 ) ); click( top, at2( -300, -450 ) ); click( top, at2( -100, -500 ) ); key( top, Qt::Key_Return ); report( "curve", b, "objects +1" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::PATCH ); QCoreApplication::processEvents(); const auto b = snap();
        drag( top, at2( -500, -500 ), at2( -350, -350 ) ); report( "patch", b, "objects +1" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::TERRAIN ); QCoreApplication::processEvents(); const auto b = snap();
        drag( top, at2( -500, -500 ), at2( -350, -350 ) ); report( "terrain", b, "objects +1 or sculpt" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::MEASURE ); QCoreApplication::processEvents(); const auto b = snap();
        drag( top, at2( -500, -500 ), at2( -350, -350 ) ); report( "measure", b, "no change, a measurement" ); }
    build(); { select( scene.brush ); MapWorkspace_SetTool( ws, map_tool_t::MIRROR ); QCoreApplication::processEvents(); const auto b = snap();
        drag( top, at2( -64, -200 ), at2( -64, 200 ) ); report( "mirror", b, "brush mirrored" ); }
    // Surface tools in 3D.
    build(); { MapWorkspace_SetTool( ws, map_tool_t::PAINT ); QCoreApplication::processEvents(); const auto b = snap();
        drag( camera, at3( { 20, -40, 128 } ), at3( { 110, 40, 128 } ) ); report( "paint-mesh", b, "steps +1" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::PAINT ); QCoreApplication::processEvents(); const auto b = snap();
        drag( camera, at3( { -240, -40, 128 } ), at3( { -140, 40, 128 } ) ); report( "paint-brush", b, "steps +1 (convert?)" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::EYEDROPPER ); QCoreApplication::processEvents(); const auto b = snap();
        click( camera, at3( { -192, 0, 128 } ) ); report( "eyedropper", b, "default material set" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::KNIFE ); QCoreApplication::processEvents(); const auto b = snap();
        click( camera, at3( { 0, 0, 128 } ) ); click( camera, at3( { 128, 0, 128 } ) ); key( camera, Qt::Key_Return ); report( "knife-mesh", b, "mesh cut" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::KNIFE ); QCoreApplication::processEvents(); const auto b = snap();
        click( camera, at3( { -256, 0, 128 } ) ); click( camera, at3( { -128, 0, 128 } ) ); key( camera, Qt::Key_Return ); report( "knife-brush", b, "brush cut (convert?)" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::LOOP_CUT ); QCoreApplication::processEvents(); const auto b = snap();
        click( camera, at3( { 128, -64, 64 } ) ); report( "loop-cut", b, "mesh cut" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::LOOP_CUT ); QCoreApplication::processEvents(); const auto b = snap();
        click( camera, at3( { -128, -64, 64 } ) ); report( "loop-brush", b, "brush cut (convert?)" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::EDGE_CUT ); QCoreApplication::processEvents(); const auto b = snap();
        click( camera, at3( { 0, 0, 128 } ) ); click( camera, at3( { 128, 0, 128 } ) ); key( camera, Qt::Key_Return ); report( "edge-cut", b, "mesh cut" ); }
    // Displacement: on the grid (selected), on the grid unselected, on a brush.
    build(); { select( scene.grid ); MapWorkspace_SetTool( ws, map_tool_t::DISPLACE ); QCoreApplication::processEvents(); const auto b = snap();
        drag( camera, at3( { 330, -60, 32 } ), at3( { 430, 60, 32 } ) ); report( "displace-sel", b, "grid sculpted" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::DISPLACE ); QCoreApplication::processEvents(); const auto b = snap();
        drag( camera, at3( { 330, -60, 32 } ), at3( { 430, 60, 32 } ) ); report( "displace-none", b, "grid sculpted" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::DISPLACE ); QCoreApplication::processEvents(); const auto b = snap();
        drag( camera, at3( { -230, -40, 128 } ), at3( { -150, 40, 128 } ) ); report( "displace-brush", b, "brush sculpted (convert?)" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::PIVOT ); QCoreApplication::processEvents(); const auto b = snap();
        click( top, at2( 0, 300 ) ); report( "pivot", b, "pivot placed, no change" ); }
    build(); { MapWorkspace_SetTool( ws, map_tool_t::WORKPLANE ); QCoreApplication::processEvents(); const auto b = snap();
        click( camera, at3( { -192, 0, 128 } ) ); report( "workplane", b, "workplane set, no change" ); }

    // Misses: every tool, nothing selected, used where it cannot work. Each
    // either does something or says why not; "SILENT" marks the rest.
    const std::pair<map_tool_t, const char *> tools[]{
        { map_tool_t::SELECT, "select" }, { map_tool_t::ENTITY, "entity" }, { map_tool_t::BLOCK, "block" }, { map_tool_t::TEXTURE, "texture" },
        { map_tool_t::DECAL, "decal" }, { map_tool_t::OVERLAY, "overlay" }, { map_tool_t::CLIP, "clip" }, { map_tool_t::VERTEX, "vertex" },
        { map_tool_t::PATH, "path" }, { map_tool_t::MEASURE, "measure" }, { map_tool_t::TERRAIN, "terrain" }, { map_tool_t::PATCH, "patch" },
        { map_tool_t::TRANSLATE, "translate" }, { map_tool_t::ROTATE, "rotate" }, { map_tool_t::SCALE, "scale" }, { map_tool_t::PIVOT, "pivot" },
        { map_tool_t::POLYGON, "polygon" }, { map_tool_t::MIRROR, "mirror" }, { map_tool_t::PAINT, "paint" }, { map_tool_t::EXTRUDE, "extrude" },
        { map_tool_t::KNIFE, "knife" }, { map_tool_t::LOOP_CUT, "loop_cut" }, { map_tool_t::EYEDROPPER, "eyedropper" }, { map_tool_t::WORKPLANE, "workplane" },
        { map_tool_t::CURVE, "curve" }, { map_tool_t::EDGE_CUT, "edge_cut" }, { map_tool_t::EDGE_ARC, "edge_arc" }, { map_tool_t::DISPLACE, "displace" } };
    for ( const auto &[kind, name] : tools ) {
        build();
        MapWorkspace_SetTool( ws, kind ); QCoreApplication::processEvents();
        const auto gesture = [&]( const char *what, auto &&act ) {
            MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "-" ) );
            const auto before = snap();
            act();
            const auto after = snap();
            map_notice_t kind2{};
            const QString notice = MapNotice_Last( ws, &kind2 );
            const bool changed = after.steps != before.steps || after.selection != before.selection;
            const bool said = notice != QStringLiteral( "-" );
            std::printf( "MISS %-10s %-12s %s %s\n", name, what, changed ? "changed" : said ? "said   " : "SILENT ", said ? notice.toUtf8().constData() : "" );
        };
        gesture( "sky-click", [&]() { click( camera, QPointF( 40, 40 ) ); } );
        gesture( "2d-click", [&]() { click( top, at2( -560, 560 ) ); } );
        gesture( "2d-drag", [&]() { drag( top, at2( -600, 600 ), at2( -520, 520 ) ); } );
        gesture( "enter", [&]() { key( top, Qt::Key_Return ); } );
        key( top, Qt::Key_Escape );
    }

    // The operations each selection mode offers, with a brush and with a mesh
    // picked by clicking in 3D, and which of them are enabled.
    struct pick_t { map_element_mode_t mode; const char *name; math::vec3d_t brushAt, meshAt; };
    const pick_t picks[]{
        { map_element_mode_t::VERTICES, "vertices", { -128, 64, 128 }, { 128, 64, 128 } },
        { map_element_mode_t::EDGES, "edges", { -192, 64, 128 }, { 64, 64, 128 } },
        { map_element_mode_t::FACES, "faces", { -192, 0, 128 }, { 64, 0, 128 } },
        { map_element_mode_t::MESHES, "meshes", { -192, 0, 128 }, { 64, 0, 128 } },
        { map_element_mode_t::OBJECTS, "objects", { -192, 0, 128 }, { 64, 0, 128 } } };
    for ( const auto &pick : picks ) {
        for ( const bool mesh : { false, true } ) {
            build();
            MapWorkspace_SetElementMode( ws, pick.mode ); QCoreApplication::processEvents();
            click( camera, at3( mesh ? pick.meshAt : pick.brushAt ) );
            if ( !mesh ) {
                const auto *object = MapWireframe_FindObject( ws->wire, scene.brush );
                std::printf( "      brush is now a %s\n", object == nullptr ? "(gone)" : object->kind == map_wire_kind_t::MESH ? "mesh" : "brush" );
                if ( pick.mode == map_element_mode_t::EDGES && !MapWorkspace_HasMeshEdges( ws ) ) {
                    click( camera, at3( pick.brushAt ) ); // A second click, now that it is a mesh.
                    std::printf( "      second click: edges %d\n", MapWorkspace_HasMeshEdges( ws ) ? 1 : 0 );
                }
            }
            QStringList on, off;
            for ( auto *button : window->findChildren<QToolButton *>() ) {
                if ( !button->property( "toolOperation" ).toBool() || !button->isVisibleTo( window ) ) { continue; }
                ( button->isEnabled() ? on : off ).append( button->objectName() );
            }
            std::printf( "OPS %-8s %-5s sel %zu verts %d edges %d faces %zu | on %lld: %s\n      off %lld: %s\n", pick.name, mesh ? "mesh" : "brush",
                static_cast<size_t>( ws->selection.ids.nCount ), MapWorkspace_HasMeshVertices( ws ) ? 1 : 0, MapWorkspace_HasMeshEdges( ws ) ? 1 : 0,
                static_cast<size_t>( MapWorkspace_MeshFaces( ws ).nCount + ( MapWorkspace_HasBrushFace( ws ) ? 1 : 0 ) ),
                static_cast<long long>( on.size() ), on.join( QLatin1Char( ' ' ) ).toUtf8().constData(), static_cast<long long>( off.size() ), off.join( QLatin1Char( ' ' ) ).toUtf8().constData() );
        }
    }
}

TEST_CASE( "Capture terrain sculpting and a displaced block face in Mason", "[.sculpt-capture]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *window = Mason_Window( session.pMason );
    window->resize( 2200, 1360 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::SHADED );
    map_terrain_create_desc_t terrain{}; terrain.origin = { -2048, -2048, 0 }; terrain.cellSize = 64.0; terrain.cellsX = 64; terrain.cellsY = 64;
    REQUIRE( MapWorkspace_CreateTerrain( ws, terrain ) );
    map_bounds_t block{}; MapBounds_AddPoint( block, { -256, -256, 0 } ); MapBounds_AddPoint( block, { 256, 256, 192 } );
    REQUIRE( MapWorkspace_CreateBox( ws, block ) );
    MapWorkspace_SetSelection( ws, nullptr, 0 );
    MapWorkspace_Frame( ws, CY_FALSE ); QCoreApplication::processEvents();
    QWidget *camera = MapViews_PaneView( views, 0 ); REQUIRE( camera != nullptr );
    const auto at = [&]( math::vec3d_t p ) { QPointF out; REQUIRE( MapCameraView_WorldToView( camera, p, &out ) ); return out; };
    const auto stroke = [&]( math::vec3d_t a, math::vec3d_t b ) {
        const QPointF from = at( a ), to = at( b );
        QMouseEvent move( QEvent::MouseMove, from, camera->mapToGlobal( from ), Qt::NoButton, Qt::NoButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &move );
        QMouseEvent press( QEvent::MouseButtonPress, from, camera->mapToGlobal( from ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &press );
        for ( int i = 1; i <= 16; ++i ) {
            const QPointF p = from + ( to - from ) * ( i / 16.0 );
            QMouseEvent drag( QEvent::MouseMove, p, camera->mapToGlobal( p ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &drag );
        }
        QMouseEvent release( QEvent::MouseButtonRelease, to, camera->mapToGlobal( to ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &release );
        QCoreApplication::processEvents();
    };
    MapWorkspace_SetTool( ws, map_tool_t::TERRAIN ); QCoreApplication::processEvents();
    for ( int i = 0; i < 6; ++i ) { stroke( { -1600, -1200.0 + i * 120, 0 }, { -700, -1300.0 + i * 120, 0 } ); }
    stroke( { 900, -1400, 0 }, { 1400, 1200, 0 } );
    MapWorkspace_SetTool( ws, map_tool_t::DISPLACE ); QCoreApplication::processEvents();
    for ( int i = 0; i < 4; ++i ) { stroke( { -150, -150.0 + i * 90, 192 }, { 150, -120.0 + i * 90, 192 } ); }
    MapWorkspace_SetTool( ws, map_tool_t::TERRAIN ); QCoreApplication::processEvents();
    const QPointF hover = at( { -400, 800, 0 } );
    QMouseEvent move( QEvent::MouseMove, hover, camera->mapToGlobal( hover ), Qt::NoButton, Qt::NoButton, Qt::NoModifier ); QCoreApplication::sendEvent( camera, &move );
    QCoreApplication::processEvents();
    REQUIRE( window->grab().save( QDir::tempPath() + QStringLiteral( "/mason_sculpt.png" ) ) );
}

// Opens assets/avia_split.cymap (made by "[.build-avia-split]") and takes
// screenshots: the whole lot from above, and the office close up.
TEST_CASE( "Capture the AVIA rent-a-car Split map", "[.avia-capture]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *window = Mason_Window( session.pMason );
    window->resize( 2200, 1360 );
    const QString path = QDir::cleanPath( QDir( QStringLiteral( CYPHER_MAP_EXAMPLE_DIR ) ).absoluteFilePath( QStringLiteral( "../../../../assets/avia_split.cymap" ) ) );
    REQUIRE( MapWorkspace_Open( ws, path ).status == map_files_status_t::OK );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::HAMMER );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::SHADED );
    QCoreApplication::processEvents();
    QWidget *camera = MapViews_PaneView( views, 0 ); REQUIRE( camera != nullptr );
    const auto groupNamed = [&]( const char *name ) {
        for ( u64 group : MapGroups_All( ws->pDocument ) ) { if ( MapGroups_Name( ws->pDocument, group ) == name ) { return group; } }
        return u64{ 0 };
    };
    const auto frameGroups = [&]( std::initializer_list<const char *> names ) {
        std::vector<u64> ids;
        for ( const char *name : names ) {
            vector_t<u64> members{}; REQUIRE( Vector_Init( &members, Allocator_GetSystem() ) );
            const u64 group = groupNamed( name ); REQUIRE( group != 0 );
            REQUIRE( MapDocument_GroupMembers( ws->pDocument, group, &members ) == map_status_t::OK );
            ids.insert( ids.end(), members.pData, members.pData + members.nCount );
            Vector_Shutdown( &members );
        }
        MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
        MapWorkspace_SetSelection( ws, ids.data(), ids.size() );
        MapWorkspace_Frame( ws, CY_TRUE ); QCoreApplication::processEvents();
        MapWorkspace_SetSelection( ws, nullptr, 0 ); QCoreApplication::processEvents();
    };
    // Orbit with the camera's own gesture (Alt + left drag).
    const auto orbit = [&]( f64 dx, f64 dy ) {
        const QPointF from( camera->width() * 0.5, camera->height() * 0.5 ), to = from + QPointF( dx, dy );
        QMouseEvent press( QEvent::MouseButtonPress, from, camera->mapToGlobal( from ), Qt::LeftButton, Qt::LeftButton, Qt::AltModifier ); QCoreApplication::sendEvent( camera, &press );
        for ( int i = 1; i <= 10; ++i ) {
            const QPointF p = from + ( to - from ) * ( i / 10.0 );
            QMouseEvent move( QEvent::MouseMove, p, camera->mapToGlobal( p ), Qt::NoButton, Qt::LeftButton, Qt::AltModifier ); QCoreApplication::sendEvent( camera, &move );
        }
        QMouseEvent release( QEvent::MouseButtonRelease, to, camera->mapToGlobal( to ), Qt::LeftButton, Qt::NoButton, Qt::AltModifier ); QCoreApplication::sendEvent( camera, &release );
        QCoreApplication::processEvents();
    };

    ( void )frameGroups; ( void )orbit;
    constexpr f64 M = 39.37;
    const auto shot = [&]( math::vec3d_t eye, math::vec3d_t target, const char *file, bool wholeWindow ) {
        MapCameraView_LookAt( camera, { eye.x * M, eye.y * M, eye.z * M }, { target.x * M, target.y * M, target.z * M } );
        QCoreApplication::processEvents();
        QWidget *source = wholeWindow ? static_cast<QWidget *>( window ) : camera;
        REQUIRE( source->grab().save( QDir::tempPath() + QStringLiteral( "/" ) + QString::fromLatin1( file ) ) );
    };
    shot( { -28, -42, 20 }, { 1, 2, 0 }, "avia_overview.png", true );
    shot( { -3, -12, 3.2 }, { 3, 5, 1.6 }, "avia_office.png", false );
    shot( { -14, -30, 5 }, { 2, -12, 0.5 }, "avia_road.png", false );
    shot( { -4, -5, 2.2 }, { 0, 2.5, 0.8 }, "avia_cars.png", false );
}

// Lists every command a menu or toolbar can show that still runs the
// "not yet available" placeholder. Run on demand:
// cypher_mason_smoke_tests "[.planned-audit]".
TEST_CASE( "List commands that are still placeholders", "[.planned-audit]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    const command_registry_t *pRegistry = &ws->pGui->commands;
    usize nPlanned = 0;
    for ( usize i = 0; i < EditorCommands_Count( pRegistry ); ++i ) {
        const command_desc_t *pCommand = EditorCommands_At( pRegistry, i );
        if ( pCommand == nullptr || !MapWorkspace_IsPlannedCommand( pRegistry, StringView_FromCString( pCommand->pId ) ) ) { continue; }
        std::printf( "PLANNED %-36s %s\n", pCommand->pId, pCommand->pLabel );
        ++nPlanned;
    }
    std::printf( "PLANNED total %zu of %zu commands\n", nPlanned, EditorCommands_Count( pRegistry ) );
}

// Drives the core tools as a user would and prints exact results (corner
// positions, bounds) so wrong-but-plausible behaviour shows. Run on demand:
// cypher_mason_smoke_tests "[.tool-deep]".
TEST_CASE( "Probe the core tools' exact results through the real views", "[.tool-deep]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *window = Mason_Window( session.pMason );
    window->resize( 2200, 1360 );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) );
    REQUIRE( views != nullptr );
    MapViews_SetArrangement( views, map_view_arrangement_t::TWO );
    MapViews_SetPaneView( views, 0, map_view_type_t::CAMERA, map_render_mode_t::SHADED );
    MapViews_SetPaneView( views, 1, map_view_type_t::TOP, map_render_mode_t::WIREFRAME );
    QCoreApplication::processEvents();
    QWidget *camera = MapViews_PaneView( views, 0 ), *top = MapViews_PaneView( views, 1 );
    REQUIRE( camera != nullptr ); REQUIRE( top != nullptr );

    const auto box = [&]( math::vec3d_t lo, math::vec3d_t hi ) {
        map_bounds_t b{}; MapBounds_AddPoint( b, lo ); MapBounds_AddPoint( b, hi );
        REQUIRE( MapWorkspace_CreateBox( ws, b ) );
        return EditorSelection_At( &ws->selection, 0 );
    };
    u64 brush = 0, mesh = 0;
    const auto build = [&]() {
        MapWorkspace_SetTool( ws, map_tool_t::SELECT );
        MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
        REQUIRE( MapWorkspace_New( ws ) == map_status_t::OK );
        box( { -640, -640, -16 }, { 640, 640, 0 } );
        brush = box( { -256, -64, 0 }, { -128, 64, 128 } );
        mesh = box( { 0, -64, 0 }, { 128, 64, 128 } );
        REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
        MapWorkspace_SetSelection( ws, nullptr, 0 );
        MapWorkspace_Frame( ws, CY_FALSE );
        QCoreApplication::processEvents();
    };
    const auto mouse = [&]( QWidget *view, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons held, Qt::KeyboardModifiers modifiers ) {
        QMouseEvent event( type, at, view->mapToGlobal( at ), button, held, modifiers );
        QCoreApplication::sendEvent( view, &event );
    };
    const auto click = [&]( QWidget *view, QPointF at, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        mouse( view, QEvent::MouseMove, at, Qt::NoButton, Qt::NoButton, modifiers );
        mouse( view, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton, modifiers );
        mouse( view, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton, modifiers );
        QCoreApplication::processEvents();
    };
    const auto drag = [&]( QWidget *view, QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        mouse( view, QEvent::MouseMove, from, Qt::NoButton, Qt::NoButton, modifiers );
        mouse( view, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers );
        for ( int i = 1; i <= 12; ++i ) { mouse( view, QEvent::MouseMove, from + ( to - from ) * ( i / 12.0 ), Qt::NoButton, Qt::LeftButton, modifiers ); }
        mouse( view, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers );
        QCoreApplication::processEvents();
    };
    const auto key = [&]( QWidget *view, Qt::Key k, Qt::KeyboardModifiers modifiers = Qt::NoModifier ) {
        view->setFocus();
        QKeyEvent press( QEvent::KeyPress, k, modifiers ); QCoreApplication::sendEvent( view, &press );
        QKeyEvent release( QEvent::KeyRelease, k, modifiers ); QCoreApplication::sendEvent( view, &release );
        QCoreApplication::processEvents();
    };
    const auto at3 = [&]( math::vec3d_t p ) { QPointF out; ( void )MapCameraView_WorldToView( camera, p, &out ); return out; };
    const auto at2 = [&]( f64 x, f64 y ) { return MapOrthoView_WorldToView( top, { x, y } ); };
    const auto select = [&]( u64 id ) { MapWorkspace_SetSelection( ws, &id, 1 ); QCoreApplication::processEvents(); };
    // The distinct corners of an object, sorted, as text.
    const auto corners = [&]( u64 id ) {
        std::vector<std::array<f64, 3>> points;
        const auto *o = MapWireframe_FindObject( ws->wire, id );
        for ( u32 l = 0; o != nullptr && l < o->nLines; ++l ) {
            for ( const u32 index : { ws->wire.lines.pData[o->iFirstLine + l].iA, ws->wire.lines.pData[o->iFirstLine + l].iB } ) {
                const auto p = ws->wire.points.pData[index];
                std::array<f64, 3> q{ std::round( p.x * 100 ) / 100, std::round( p.y * 100 ) / 100, std::round( p.z * 100 ) / 100 };
                if ( std::find( points.begin(), points.end(), q ) == points.end() ) { points.push_back( q ); }
            }
        }
        std::sort( points.begin(), points.end() );
        std::string text;
        for ( const auto &p : points ) { char buffer[64]; std::snprintf( buffer, sizeof( buffer ), "(%g %g %g) ", p[0], p[1], p[2] ); text += buffer; }
        return text;
    };
    const auto bounds = [&]( u64 id ) {
        const auto *o = MapWireframe_FindObject( ws->wire, id );
        char buffer[128]{};
        if ( o != nullptr ) { std::snprintf( buffer, sizeof( buffer ), "[%g %g %g .. %g %g %g]", o->bounds.box.minimum.x, o->bounds.box.minimum.y, o->bounds.box.minimum.z, o->bounds.box.maximum.x, o->bounds.box.maximum.y, o->bounds.box.maximum.z ); }
        return std::string( buffer );
    };
    const auto say = [&]( const char *name, const std::string &result, const char *expect ) {
        map_notice_t kind{};
        std::printf( "DEEP %-26s %s\n     expect: %s\n     notice: %s\n", name, result.c_str(), expect, MapNotice_Last( ws, &kind ).toUtf8().constData() );
        MapNotice_Post( ws, map_notice_t::INFO, QStringLiteral( "-" ) );
    };
    std::printf( "DEEP grid %g\n", ws->gridSize );

    // Translate gizmo: the X handle in Top, then the centre in 3D.
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::TRANSLATE ); QCoreApplication::processEvents();
    drag( top, at2( -192 + 40, 0 ), at2( -192 + 40 + 50, 30 ) );
    say( "translate x-handle top", bounds( brush ), "x moves +48 (grid), y unchanged" );
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::TRANSLATE ); QCoreApplication::processEvents();
    drag( camera, at3( { -192, 0, 64 } ), at3( { -192 + 64, 0, 64 } ) );
    say( "translate centre 3d", bounds( brush ), "moves about +64 in x on the ground plane, z unchanged" );
    // Rotate 90 in Top about the centre; check corners, not bounds.
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::ROTATE ); QCoreApplication::processEvents();
    drag( top, at2( -192 + 60, 0 ), at2( -192, 60 ) );
    say( "rotate 90 top", corners( brush ), "same square (rotated 90 about centre)" );
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::ROTATE ); QCoreApplication::processEvents();
    drag( top, at2( -192 + 60, 0 ), at2( -192 + 42.4, 42.4 ) );
    say( "rotate 45 top", corners( brush ), "diamond, corners at distance 90.5 from (-192,0)" );
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::SCALE ); QCoreApplication::processEvents();
    drag( top, at2( -192 + 60, 0 ), at2( -192 + 90, 0 ) );
    say( "scale centre drag top", bounds( brush ), "uniform scale x1.5 about centre in x/y" );
    // Block tool: drag a rectangle in Top, Enter.
    build(); MapWorkspace_SetTool( ws, map_tool_t::BLOCK ); QCoreApplication::processEvents();
    drag( top, at2( -500, 300 ), at2( -404, 404 ) ); key( top, Qt::Key_Return );
    say( "block top", bounds( EditorSelection_At( &ws->selection, 0 ) ), "x -500..-404 snapped (-496/-400?), y 304..400, z 0..? (height)" );
    // Block in 3D on the floor, in front of the objects (a floor point behind
    // the mesh is a press on the mesh's top).
    build(); MapWorkspace_SetTool( ws, map_tool_t::BLOCK ); QCoreApplication::processEvents();
    drag( camera, at3( { -32, -320, 0 } ), at3( { 96, -192, 0 } ) ); key( camera, Qt::Key_Return );
    say( "block 3d floor", ws->selection.ids.nCount ? bounds( EditorSelection_At( &ws->selection, 0 ) ) : std::string( "none" ), "x -32..96, y -320..-192 on the floor, z 0..64" );
    // Clip 2D across the brush: halves.
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::CLIP ); QCoreApplication::processEvents();
    drag( top, at2( -320, 0 ), at2( -64, 0 ) ); key( top, Qt::Key_Return );
    {
        std::string r;
        for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) { r += bounds( ws->selection.ids.pData[i] ) + " "; }
        say( "clip top y=0", r, "two halves: y -64..0 and 0..64" );
    }
    // Vertex tool on a brush corner in Top.
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::VERTEX ); QCoreApplication::processEvents();
    drag( top, at2( -128, 64 ), at2( -96, 96 ) );
    say( "vertex brush corner top", corners( brush ), "corner (-128 64 *) moves to (-96 96 *): both top and bottom corners (2D drags the column)" );
    // Extrude the mesh's top face in 3D.
    build(); select( mesh ); MapWorkspace_SetTool( ws, map_tool_t::EXTRUDE ); QCoreApplication::processEvents();
    drag( camera, at3( { 64, 0, 128 } ), at3( { 64, 0, 192 } ) );
    say( "extrude mesh top 3d", bounds( mesh ), "z max 128 -> ~192 (snapped), x/y unchanged" );
    // Faces mode: click the mesh's top face, Translate gizmo up in 3D.
    build(); select( mesh ); MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES ); MapWorkspace_SetTool( ws, map_tool_t::SELECT ); QCoreApplication::processEvents();
    click( camera, at3( { 64, 0, 128 } ) );
    say( "face pick 3d", std::to_string( MapWorkspace_MeshFaces( ws ).nCount ) + " faces", "1 face of the mesh" );
    MapWorkspace_SetTool( ws, map_tool_t::TRANSLATE ); QCoreApplication::processEvents();
    say( "face gizmo available?", MapToolGestures_CanTransformComponents( ws, map_component_transform_t::TRANSLATE ) ? std::string( "yes" ) : std::string( "no" ), "yes" );
    drag( camera, at3( { 64, 0, 128 } ), at3( { 64, 0, 192 } ) );
    say( "face gizmo centre drag 3d", corners( mesh ), "top face moves up (or along view plane)" );
    // Edges mode: click an edge of the mesh in 3D, drag it with Select.
    build(); select( mesh ); MapWorkspace_SetElementMode( ws, map_element_mode_t::EDGES ); MapWorkspace_SetTool( ws, map_tool_t::SELECT ); QCoreApplication::processEvents();
    click( camera, at3( { 64, -64, 128 } ) );
    say( "edge pick 3d", std::to_string( ws->meshSelection.edges.nCount ) + " edges", "1 edge (top front)" );
    drag( camera, at3( { 64, -64, 128 } ), at3( { 64, -64, 192 } ) );
    say( "edge drag 3d", corners( mesh ), "the two top front corners move up (or along view plane) by a grid step multiple" );
    // Vertices mode: click a corner in Top and drag it.
    build(); select( mesh ); MapWorkspace_SetElementMode( ws, map_element_mode_t::VERTICES ); MapWorkspace_SetTool( ws, map_tool_t::SELECT ); QCoreApplication::processEvents();
    drag( top, at2( 128, 64 ), at2( 160, 96 ) );
    say( "vertex drag top (mesh)", corners( mesh ), "corner column (128 64) -> (160 96)" );
    // Polygon tool: four clicks in Top, Enter.
    build(); MapWorkspace_SetTool( ws, map_tool_t::POLYGON ); QCoreApplication::processEvents();
    click( top, at2( -500, -500 ) ); click( top, at2( -400, -500 ) ); click( top, at2( -400, -400 ) ); key( top, Qt::Key_Return );
    say( "polygon triangle top", ws->selection.ids.nCount ? corners( EditorSelection_At( &ws->selection, 0 ) ) : std::string( "none" ), "triangular prism over those 3 points" );
    // Mirror across x = -64 in Top.
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::MIRROR ); QCoreApplication::processEvents();
    drag( top, at2( -64, -200 ), at2( -64, 200 ) );
    say( "mirror x=-64 top", bounds( brush ), "x 0..128 (mirrored across x=-64)" );
    // ---- Batch 2 ----
    const auto count = [&]() { return std::to_string( ws->wire.objects.nCount ) + " objects, sel " + std::to_string( ws->selection.ids.nCount ); };
    const auto run = [&]( const char *command ) {
        return std::string( EditorCommands_ResultName( EditorCommands_Execute( &ws->pGui->commands, StringView_FromCString( command ), {} ) ) );
    };
    // Rotate in 3D: drag around the gizmo ring on the floor.
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::ROTATE ); QCoreApplication::processEvents();
    drag( camera, at3( { -192 + 60, 0, 64 } ), at3( { -192, 60, 64 } ) );
    say( "rotate 3d ring", corners( brush ), "rotated about Z by a snapped angle (corners at r=90.5 or the square)" );
    // Clip mode cycling: Front keeps one half.
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::CLIP ); QCoreApplication::processEvents();
    drag( top, at2( -320, 0 ), at2( -64, 0 ) ); key( top, Qt::Key_Tab ); key( top, Qt::Key_Return );
    say( "clip with Tab then Enter", count() + " " + ( ws->selection.ids.nCount ? bounds( ws->selection.ids.pData[0] ) : std::string() ), "Tab cycles which side is kept; one half kept" );
    // Brush face push/pull in 3D with the Select tool in faces mode.
    build(); select( brush ); MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES ); MapWorkspace_SetTool( ws, map_tool_t::SELECT ); QCoreApplication::processEvents();
    click( camera, at3( { -192, 0, 128 } ) );
    say( "brush face pick 3d", std::to_string( ws->selectedBrushFaceObject ) + "/" + std::to_string( ws->selectedBrushFaceSide ), "the brush's top side" );
    MapWorkspace_SetTool( ws, map_tool_t::TRANSLATE ); QCoreApplication::processEvents();
    drag( camera, at3( { -192, 0, 128 } ), at3( { -192, 0, 192 } ), Qt::NoModifier );
    say( "brush face translate 3d", bounds( brush ), "top face moves (floor plane: in x/y) - or with the Z handle up" );
    // Extrude tool on a brush face in 2D (Front view would be needed; use Top side face).
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::EXTRUDE ); QCoreApplication::processEvents();
    drag( top, at2( -128, 0 ), at2( -64, 0 ) );
    say( "extrude brush side 2d", bounds( brush ), "east side pushed from x=-128 to x=-64" );
    // Shift-drag clone with the gizmo in Top.
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::TRANSLATE ); QCoreApplication::processEvents();
    drag( top, at2( -192, 0 ), at2( -192, 200 ), Qt::ShiftModifier );
    say( "shift-drag clone top", count() + " original " + bounds( brush ), "objects +1, original stays, clone at +y" );
    // Hollow.
    build(); select( brush ); say( "hollow", run( "map.brush.hollow" ) + " " + count(), "6 wall brushes replace the brush" );
    // Texture fit on the brush's top face.
    build(); select( brush ); MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES ); MapWorkspace_SetTool( ws, map_tool_t::SELECT ); QCoreApplication::processEvents();
    click( camera, at3( { -192, 0, 128 } ) );
    say( "texture fit", run( "map.texture.fit" ) + " steps " + std::to_string( EditorHistory_StepCount( &ws->history ) ), "OK, one step" );
    say( "texture align world", run( "map.texture.align_world" ), "OK" );
    say( "texture rotate", run( "map.texture.rotate" ), "OK" );
    // Edge commands on the mesh: bevel / connect / dissolve.
    build(); select( mesh ); MapWorkspace_SetElementMode( ws, map_element_mode_t::EDGES ); MapWorkspace_SetTool( ws, map_tool_t::SELECT ); QCoreApplication::processEvents();
    click( camera, at3( { 64, -64, 128 } ) );
    say( "edge bevel", run( "map.mesh.bevel" ) + " corners " + corners( mesh ), "OK; the top front edge becomes a chamfer face" );
    build(); select( mesh ); MapWorkspace_SetElementMode( ws, map_element_mode_t::EDGES ); MapWorkspace_SetTool( ws, map_tool_t::SELECT ); QCoreApplication::processEvents();
    click( camera, at3( { 64, -64, 128 } ) ); click( camera, at3( { 64, 64, 128 } ), Qt::ShiftModifier );
    say( "edge connect (2 edges)", std::to_string( ws->meshSelection.edges.nCount ) + " sel; " + run( "map.mesh.connect_edges" ), "2 selected; OK, a new edge across the top" );
    build(); select( mesh ); MapWorkspace_SetElementMode( ws, map_element_mode_t::EDGES ); MapWorkspace_SetTool( ws, map_tool_t::SELECT ); QCoreApplication::processEvents();
    click( camera, at3( { 64, -64, 128 } ) );
    say( "edge dissolve", run( "map.mesh.dissolve" ), "OK or a clear refusal (dissolving a box edge merges two faces into a non-planar one)" );
    // Box-select objects in Top with the Select tool.
    build(); MapWorkspace_SetTool( ws, map_tool_t::SELECT ); QCoreApplication::processEvents();
    drag( top, at2( -300, -100 ), at2( 200, 100 ) );
    say( "marquee top", count(), "brush and mesh selected (2)" );
    // Ctrl-click deselect in 3D.
    click( camera, at3( { -192, 0, 128 } ), Qt::ControlModifier );
    say( "ctrl-click 3d deselects", count(), "sel 1" );

    // ---- Batch 3: placement on surfaces, keyboard edits ----
    const auto entityOrigin = [&]() {
        if ( ws->wire.entities.nCount == 0 ) { return std::string( "no entity" ); }
        const auto &entity = ws->wire.entities.pData[ws->wire.entities.nCount - 1];
        char buffer[96]{}; std::snprintf( buffer, sizeof( buffer ), "(%g %g %g)", entity.origin.x, entity.origin.y, entity.origin.z );
        return std::string( buffer );
    };
    build(); MapWorkspace_SetTool( ws, map_tool_t::ENTITY ); QCoreApplication::processEvents();
    click( camera, at3( { -192, 0, 128 } ) );
    say( "entity on brush top 3d", entityOrigin(), "origin on the brush top: z = 128 (+ the class's floor offset)" );
    build(); MapWorkspace_SetTool( ws, map_tool_t::BLOCK ); QCoreApplication::processEvents();
    drag( camera, at3( { -240, -48, 128 } ), at3( { -144, 48, 128 } ) ); key( camera, Qt::Key_Return );
    say( "block on brush top 3d", ws->selection.ids.nCount ? bounds( EditorSelection_At( &ws->selection, 0 ) ) : std::string( "none" ), "a block standing on the brush top (z 128..192), as Hammer 5 draws on surfaces" );
    build(); MapWorkspace_SetTool( ws, map_tool_t::POLYGON ); QCoreApplication::processEvents();
    click( camera, at3( { -240, -48, 128 } ) ); click( camera, at3( { -144, -48, 128 } ) ); click( camera, at3( { -192, 48, 128 } ) ); key( camera, Qt::Key_Return );
    say( "polygon on brush top 3d", ws->selection.ids.nCount ? bounds( EditorSelection_At( &ws->selection, 0 ) ) : std::string( "none" ), "a prism standing on the brush top" );
    // Keyboard: nudge in Top, duplicate, delete, undo/redo.
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::SELECT ); QCoreApplication::processEvents();
    key( top, Qt::Key_Right );
    say( "nudge right top", bounds( brush ), "x +16 (one grid step)" );
    build(); select( brush ); QCoreApplication::processEvents();
    key( top, Qt::Key_D, Qt::ControlModifier );
    say( "ctrl+d duplicate", count(), "objects +1 (4), the copy selected" );
    build(); select( brush ); QCoreApplication::processEvents();
    key( top, Qt::Key_Delete );
    say( "delete key", count(), "objects -1 (2)" );
    ( void )MapWorkspace_Undo( ws ); QCoreApplication::processEvents();
    say( "undo delete", count(), "objects back to 3" );
    ( void )MapWorkspace_Redo( ws ); QCoreApplication::processEvents();
    say( "redo delete", count(), "objects 2" );

    // Undo after a translate restores exactly.
    build(); select( brush ); const std::string before = bounds( brush ); MapWorkspace_SetTool( ws, map_tool_t::TRANSLATE ); QCoreApplication::processEvents();
    drag( top, at2( -192, 0 ), at2( -192, 128 ) );
    ( void )MapWorkspace_Undo( ws ); QCoreApplication::processEvents();
    say( "translate then undo", bounds( brush ) + " vs " + before, "equal" );

    // ---- Batch 4: the remaining tools ----
    const auto faces = [&]( u64 id ) {
        usize n = 0;
        for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) { n += ws->wire.faces.pData[f].id == id ? 1u : 0u; }
        return std::to_string( n ) + " faces";
    };
    const auto selection = [&]() {
        std::string text = count();
        for ( usize i = 0; i < ws->selection.ids.nCount; ++i ) { text += " " + bounds( ws->selection.ids.pData[i] ); }
        return text;
    };
    const auto steps = [&]() { return EditorHistory_StepCount( &ws->history ); };
    const auto entities = [&]() {
        std::string text = std::to_string( ws->wire.entities.nCount ) + " entities";
        for ( usize i = 0; i < ws->wire.entities.nCount; ++i ) {
            const auto &entity = ws->wire.entities.pData[i];
            char buffer[160]{}; std::snprintf( buffer, sizeof( buffer ), " %s(%g %g %g)", entity.className, entity.origin.x, entity.origin.y, entity.origin.z );
            text += buffer;
        }
        return text;
    };
    // Clip in 3D: a line drawn across the brush top at x = -192.
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::CLIP ); QCoreApplication::processEvents();
    drag( camera, at3( { -192, -64, 128 } ), at3( { -192, 64, 128 } ) ); key( camera, Qt::Key_Return );
    say( "clip 3d line on top", selection(), "the brush cut at x = -192 (one or both halves kept)" );
    // Loop Cut: click the middle of the mesh's front vertical edge.
    build(); select( mesh ); MapWorkspace_SetTool( ws, map_tool_t::LOOP_CUT ); QCoreApplication::processEvents();
    click( camera, at3( { 0, -64, 64 } ) );
    say( "loop cut 3d", faces( mesh ), "6 -> 10 faces (a ring round the box at z 64)" );
    // Edge Cut: a point on the front vertical edge, one on the top front edge.
    build(); select( mesh ); MapWorkspace_SetTool( ws, map_tool_t::EDGE_CUT ); QCoreApplication::processEvents();
    click( camera, at3( { 0, -64, 64 } ) ); click( camera, at3( { 64, -64, 128 } ) ); key( camera, Qt::Key_Return );
    say( "edge cut 3d", faces( mesh ), "6 -> 7 faces (the front face cut corner to corner)" );
    // Edge Arc on one edge: there is no inner corner to move.
    build(); select( mesh ); MapWorkspace_SetTool( ws, map_tool_t::EDGE_ARC ); QCoreApplication::processEvents();
    click( camera, at3( { 64, -64, 128 } ) );
    { const auto before = steps(); drag( camera, at3( { 64, -96, 160 } ), at3( { 64, -128, 200 } ) );
      say( "edge arc single edge", std::to_string( steps() - before ) + " steps " + corners( mesh ), "a visible bend, or a clear notice of what is needed" ); }
    // Path: two corners on the floor, Enter.
    build(); MapWorkspace_SetTool( ws, map_tool_t::PATH ); QCoreApplication::processEvents();
    click( camera, at3( { -64, -320, 0 } ) ); click( camera, at3( { 64, -320, 0 } ) ); key( camera, Qt::Key_Return );
    say( "path 3d", entities(), "2 path_corner on the floor at z 0" );
    // Measure: a drag changes nothing.
    build(); MapWorkspace_SetTool( ws, map_tool_t::MEASURE ); QCoreApplication::processEvents();
    { const auto before = steps(); drag( camera, at3( { -192, 0, 128 } ), at3( { 64, 0, 128 } ) );
      say( "measure 3d", std::to_string( steps() - before ) + " steps", "0 steps (a readout only)" ); }
    // Eyedropper on the brush top.
    build(); MapWorkspace_SetTool( ws, map_tool_t::EYEDROPPER ); QCoreApplication::processEvents();
    click( camera, at3( { -192, 0, 128 } ) );
    say( "eyedropper 3d", std::string( "(see notice)" ), "the active material is the brush's" );
    // Decal and overlay on the mesh top.
    build(); MapWorkspace_SetTool( ws, map_tool_t::DECAL ); QCoreApplication::processEvents();
    click( camera, at3( { 64, 0, 128 } ) );
    say( "decal 3d", entities(), "infodecal at (64 0 128)" );
    build(); MapWorkspace_SetTool( ws, map_tool_t::OVERLAY ); QCoreApplication::processEvents();
    click( camera, at3( { 64, 0, 128 } ) );
    say( "overlay 3d", entities(), "info_overlay at (64 0 128)" );
    // Paint and Displace strokes on the mesh top.
    build(); select( mesh ); MapWorkspace_SetTool( ws, map_tool_t::PAINT ); QCoreApplication::processEvents();
    { const auto before = steps(); drag( camera, at3( { 16, -48, 128 } ), at3( { 112, 48, 128 } ) );
      say( "paint stroke 3d", std::to_string( steps() - before ) + " steps", "1 step" ); }
    build(); select( mesh ); MapWorkspace_SetTool( ws, map_tool_t::DISPLACE ); QCoreApplication::processEvents();
    { const auto before = steps(); drag( camera, at3( { 32, -32, 128 } ), at3( { 96, 32, 128 } ) );
      say( "displace stroke 3d", std::to_string( steps() - before ) + " steps " + bounds( mesh ), "1 step, the top raised (z max > 128)" ); }
    // Pivot on the brush top; Workplane on the brush's front side, then an entity on it.
    build(); MapWorkspace_SetTool( ws, map_tool_t::PIVOT ); QCoreApplication::processEvents();
    click( camera, at3( { -160, 32, 128 } ) );
    { math::vec3d_t pivot{}; const bool has = MapToolGestures_Pivot( ws, &pivot );
      char buffer[96]{}; std::snprintf( buffer, sizeof( buffer ), "%s (%g %g %g)", has ? "set" : "unset", pivot.x, pivot.y, pivot.z );
      say( "pivot 3d", buffer, "set (-160 32 128)" ); }
    build(); MapWorkspace_SetTool( ws, map_tool_t::WORKPLANE ); QCoreApplication::processEvents();
    click( camera, at3( { -192, -64, 64 } ) );
    MapWorkspace_SetTool( ws, map_tool_t::ENTITY ); QCoreApplication::processEvents();
    click( camera, at3( { -320, -64, 96 } ) );
    say( "workplane then entity", entities(), "the entity on the plane y = -64" );
    // Curve: three clicks on the floor, Enter.
    build(); MapWorkspace_SetTool( ws, map_tool_t::CURVE ); QCoreApplication::processEvents();
    click( camera, at3( { -64, -320, 0 } ) ); click( camera, at3( { 64, -320, 0 } ) ); click( camera, at3( { 64, -192, 0 } ) ); key( camera, Qt::Key_Return );
    { std::string settings;
      for ( const char *path : { "editor.map.curve_shape", "editor.map.curve_profile", "editor.map.curve_output", "editor.map.curve_width", "editor.map.curve_height" } ) {
          const auto text = EditorSettings_Text( &ws->pGui->settings, path, StringView_FromCString( "?" ) );
          settings += std::string( path + 11 ) + "=" + std::string( text.pData, text.cchLength ) + " ";
      }
      say( "curve 3d", selection() + " | " + settings, "a swept mesh or curve object created" ); }
    // Vertex tool in 3D: drag the brush's top front-right corner sideways.
    build(); select( brush ); MapWorkspace_SetTool( ws, map_tool_t::VERTEX ); QCoreApplication::processEvents();
    drag( camera, at3( { -128, -64, 128 } ), at3( { -96, -64, 128 } ) );
    say( "vertex tool 3d", corners( brush ), "the corner (-128 -64 128) slides to about (-96 -64 128)" );
    // Texture tool: click the brush top in 3D.
    build(); MapWorkspace_SetTool( ws, map_tool_t::TEXTURE ); QCoreApplication::processEvents();
    click( camera, at3( { -192, 0, 128 } ) );
    say( "texture tool pick 3d", std::to_string( ws->selectedBrushFaceObject ) + "/" + std::to_string( ws->selectedBrushFaceSide ), "the brush's top side" );
    // Terrain: the panel's Create Terrain button makes one, then a drag raises it.
    build(); say( "terrain create (opens panel)", run( "map.terrain.create" ), "OK" );
    QCoreApplication::processEvents();
    { auto *create = window->findChild<QPushButton *>( QStringLiteral( "MapTerrainCreate" ) );
      if ( create != nullptr && create->isEnabled() ) { create->click(); }
      QCoreApplication::processEvents();
      say( "terrain create button", std::string( create == nullptr ? "no button" : create->isEnabled() ? "clicked" : "disabled" ) + " " + std::to_string( ws->pDocument->geometry.heightFields.nCount ) + " fields",
           "1 field" ); }
    MapWorkspace_SetTool( ws, map_tool_t::TERRAIN ); QCoreApplication::processEvents();
    { u64 field = 0;
      for ( usize o = 0; o < ws->wire.objects.nCount; ++o ) { if ( ws->wire.objects.pData[o].kind == map_wire_kind_t::TERRAIN ) { field = ws->wire.objects.pData[o].id; } }
      const auto *object = MapWireframe_FindObject( ws->wire, field );
      const auto centre = object != nullptr ? MapBounds_Center( object->bounds ) : math::vec3d_t{};
      const auto before = steps(); drag( camera, at3( { centre.x - 64, centre.y, centre.z } ), at3( { centre.x + 64, centre.y, centre.z } ) );
      say( "terrain stroke", std::to_string( steps() - before ) + " steps " + bounds( field ), "1 step (raised)" ); }
}

// Renders the showcase map (assets/black_mesa_entrance.cymap) from fixed
// viewpoints into PNGs, to look at what the editor shows. Not a contract.
// CYPHER_SHOTS_DIR picks the folder (default: the system temp folder).
TEST_CASE( "Render the showcase map from fixed viewpoints", "[.showcase-shots]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *window = Mason_Window( session.pMason );
    const QString map = QDir( QStringLiteral( CYPHER_REPOSITORY_ROOT ) ).filePath( QStringLiteral( "assets/black_mesa_entrance.cymap" ) );
    REQUIRE( Mason_OpenMap( session.pMason, map ) );
    QCoreApplication::processEvents();
    const QByteArray env = qgetenv( "CYPHER_SHOTS_DIR" );
    const QDir out( env.isEmpty() ? QDir::temp().filePath( QStringLiteral( "cypher_showcase" ) ) : QString::fromUtf8( env ) );
    REQUIRE( QDir().mkpath( out.absolutePath() ) );

    // The editor itself, with the map open.
    window->resize( 1920, 1080 );
    QCoreApplication::processEvents();
    REQUIRE( window->grab().save( out.filePath( QStringLiteral( "00_mason.png" ) ) ) );

    // Trigger volumes would stand in front of the views (Hammer's "hide
    // triggers" for screenshots).
    MapWorkspace_SetVisgroupHidden( ws, map_visgroup_t::TRIGGERS, CY_TRUE );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, ws ) );
    camera->resize( 1600, 900 );
    camera->show();
    MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::SHADED );
    QCoreApplication::processEvents();
    struct shot_t { const char *name; math::vec3d_t eye, target; };
    const shot_t shots[]{
        { "01_overview", { -2600, -3200, 3000 }, { 0, 200, 200 } },
        { "02_tram_platform", { -1100, -700, 140 }, { 400, -900, 20 } },
        { "03_lobby_from_door", { 0, 150, 220 }, { 0, 720, 330 } },
        { "04_lobby_from_balcony", { 380, 980, 470 }, { -160, 240, 200 } },
        { "05_lobby_corner", { 720, 60, 600 }, { -320, 820, 240 } },
        { "06_checkpoint", { -360, -40, 270 }, { 300, -200, 170 } },
        { "07_stair_tower", { -880, 440, 250 }, { -1420, 450, 330 } },
        { "08_elevator_shaft", { 110, 1470, 720 }, { -40, 1330, 250 } },
        { "09_desert", { 1100, -1100, 520 }, { 3400, 1500, 640 } },
        { "10_stairs_up", { 0, -610, 70 }, { 0, -250, 230 } },
    };
    for ( const shot_t &shot : shots ) {
        MapCameraView_LookAt( camera.get(), shot.eye, shot.target );
        QCoreApplication::processEvents();
        QElapsedTimer timer;
        timer.start();
        const QPixmap pixels = camera->grab();
        std::printf( "%-24s %lld ms\n", shot.name, static_cast<long long>( timer.elapsed() ) );
        REQUIRE( pixels.save( out.filePath( QString::fromLatin1( shot.name ) + QStringLiteral( ".png" ) ) ) );
    }
    // The plan: Top view, wireframe, the whole map.
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, ws, map_ortho_axes_t::TOP ) );
    top->resize( 1600, 1200 );
    top->show();
    ws->frameBounds.box = { { -1700, -1200, -100 }, { 1700, 1600, 1100 } };
    ws->frameBounds.bHas = CY_TRUE;
    MapWorkspace_Notify( ws, MAP_CHANGE_FRAME );
    QCoreApplication::processEvents();
    REQUIRE( top->grab().save( out.filePath( QStringLiteral( "11_top_plan.png" ) ) ) );
    std::printf( "showcase shots: %s\n", out.absolutePath().toUtf8().constData() );
}

// Every menu command against every kind of selection: is it offered, and
// does running it change the map? Not a contract - a table to read:
// cypher_mason_smoke_tests "[.menu-audit]". "-" disabled, "+n" ran and added
// n undo steps, "0" ran and changed nothing, "F" ran and failed.
TEST_CASE( "Audit menu commands across selections", "[.menu-audit]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *gui = Mason_Gui( session.pMason );
    // Close any modal dialog a command might open.
    QTimer closer;
    QObject::connect( &closer, &QTimer::timeout, []() { if ( QWidget *modal = QApplication::activeModalWidget() ) { modal->close(); } } );
    closer.start( 50 );
    const auto box = [&]( math::vec3d_t lo, math::vec3d_t hi ) {
        map_bounds_t b{}; MapBounds_AddPoint( b, lo ); MapBounds_AddPoint( b, hi );
        REQUIRE( MapWorkspace_CreateBox( ws, b ) );
        return EditorSelection_At( &ws->selection, 0 );
    };
    const auto topFace = [&]( u64 id, u64 *pSide, u64 *pFace, std::vector<u64> *pPoints ) {
        for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) {
            const auto &face = ws->wire.faces.pData[f];
            if ( face.id != id || face.normal.z < 0.9 ) { continue; }
            if ( pSide ) { *pSide = face.sideId; }
            if ( pFace ) { *pFace = face.faceId; }
            for ( u32 k = 0; pPoints && k < face.nIndices; ++k ) { pPoints->push_back( ws->wire.pointSourceIds.pData[ws->wire.faceIndices.pData[face.iFirstIndex + k]] ); }
            return;
        }
    };
    const auto sideFace = [&]( u64 id ) {
        for ( usize f = 0; f < ws->wire.faces.nCount; ++f ) {
            const auto &face = ws->wire.faces.pData[f];
            if ( face.id == id && face.normal.x > 0.9 ) { return face.faceId; }
        }
        return u64{ 0 };
    };
    const auto fresh = [&]() {
        REQUIRE( MapWorkspace_New( ws ) == map_status_t::OK );
        MapWorkspace_SetTool( ws, map_tool_t::SELECT );
        MapWorkspace_SetElementMode( ws, map_element_mode_t::OBJECTS );
    };
    struct context_t { const char *name; std::function<void()> make; };
    u64 a = 0, b = 0;
    const std::vector<context_t> contexts{
        { "none", [&]() { fresh(); box( { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_SetSelection( ws, nullptr, 0 ); } },
        { "brush", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); } },
        { "2brush", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); b = box( { 0, 0, 0 }, { 128, 128, 64 } );
                            const u64 ids[]{ a, b }; MapWorkspace_SetSelection( ws, ids, 2 ); } },
        { "bface", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES );
                           u64 side = 0; topFace( a, &side, nullptr, nullptr ); MapWorkspace_SelectBrushFace( ws, a, side ); } },
        { "mesh", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) ); } },
        { "2mesh", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); b = box( { 0, 0, 0 }, { 128, 128, 64 } );
                           const u64 ids[]{ a, b }; MapWorkspace_SetSelection( ws, ids, 2 ); REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) ); } },
        { "mface", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
                           MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES ); u64 face = 0; topFace( a, nullptr, &face, nullptr ); MapWorkspace_SelectMeshFace( ws, a, face ); } },
        { "2mface", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
                            MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES ); u64 face = 0; topFace( a, nullptr, &face, nullptr ); MapWorkspace_SelectMeshFace( ws, a, face );
                            ( void )MapWorkspace_AddMeshFace( ws, a, sideFace( a ) ); } },
        { "medge", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
                           MapWorkspace_SetElementMode( ws, map_element_mode_t::EDGES ); std::vector<u64> p; topFace( a, nullptr, nullptr, &p );
                           REQUIRE( p.size() >= 2 ); REQUIRE( MapWorkspace_SelectMeshEdge( ws, a, geometry::MeshEdgeRef_Make( { p[0] }, { p[1] } ) ) ); } },
        { "2medge", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
                            MapWorkspace_SetElementMode( ws, map_element_mode_t::EDGES ); std::vector<u64> p; topFace( a, nullptr, nullptr, &p ); REQUIRE( p.size() >= 4 );
                            REQUIRE( MapWorkspace_SelectMeshEdge( ws, a, geometry::MeshEdgeRef_Make( { p[0] }, { p[1] } ) ) );
                            REQUIRE( MapWorkspace_SelectMeshEdge( ws, a, geometry::MeshEdgeRef_Make( { p[2] }, { p[3] } ), MAP_SELECT_ADD ) ); } },
        { "mvert", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
                           MapWorkspace_SetElementMode( ws, map_element_mode_t::VERTICES ); std::vector<u64> p; topFace( a, nullptr, nullptr, &p );
                           REQUIRE( MapWorkspace_SelectMeshVertex( ws, a, { p[0] } ) ); } },
        { "2mvert", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
                            MapWorkspace_SetElementMode( ws, map_element_mode_t::VERTICES ); std::vector<u64> p; topFace( a, nullptr, nullptr, &p );
                            REQUIRE( MapWorkspace_SelectMeshVertex( ws, a, { p[0] } ) ); REQUIRE( MapWorkspace_SelectMeshVertex( ws, a, { p[1] }, MAP_SELECT_ADD ) ); } },
        { "carve", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); b = box( { 0, 0, 0 }, { 128, 128, 64 } ); MapWorkspace_SetSelection( ws, &b, 1 ); } },
        { "group", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); b = box( { 256, 0, 0 }, { 320, 64, 64 } ); const u64 ids[]{ a, b };
                           MapWorkspace_SetSelection( ws, ids, 2 ); REQUIRE( EditorCommands_Execute( &gui->commands, StringView_FromCString( "map.group.create" ), {} ) == command_result_t::OK ); } },
        { "tied", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( EditorCommands_Execute( &gui->commands, StringView_FromCString( "map.entity.tie" ), {} ) == command_result_t::OK );
                          MapWorkspace_SetSelection( ws, &a, 1 ); } },
        { "sheet", [&]() { fresh(); map_primitive_desc_t desc = MapWorkspace_PrimitiveDefaults( ws, {} ); desc.kind = map_primitive_kind_t::QUAD; desc.axis = 2;
                           desc.bounds = { { -64, -64, 0 }, { 64, 64, 0 } }; REQUIRE( MapWorkspace_CreatePrimitive( ws, desc ) ); } },
        { "holed", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
                           MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES ); u64 face = 0; std::vector<u64> p; topFace( a, nullptr, &face, &p );
                           MapWorkspace_SelectMeshFace( ws, a, face ); REQUIRE( EditorCommands_Execute( &gui->commands, StringView_FromCString( "map.mesh.delete_face" ), {} ) == command_result_t::OK );
                           MapWorkspace_SetElementMode( ws, map_element_mode_t::EDGES ); ( void )MapWorkspace_SelectMeshEdge( ws, a, geometry::MeshEdgeRef_Make( { p[0] }, { p[1] } ) ); } },
        { "patch", [&]() { fresh(); u64 id = 0;
                           REQUIRE( MapCreate_Patch( ws->pDocument, map_patch_shape_t::PLANE, { { -64, -64, 0 }, { 64, 64, 0 } }, 2u, 8u, StringView_FromCString( "materials/dev/grid.cymat" ), &id ) == map_status_t::OK );
                           MapWorkspace_DocumentChanged( ws ); MapWorkspace_SetSelection( ws, &id, 1 ); } },
        // The setups commands need beyond a plain selection.
        { "modmesh", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); REQUIRE( MapWorkspace_ConvertBrushSelection( ws ) );
                             REQUIRE( EditorCommands_Execute( &gui->commands, StringView_FromCString( "map.mesh.modifier_mirror_x" ), {} ) == command_result_t::OK ); } },
        { "bigpatch", [&]() { fresh(); u64 id = 0;
                              REQUIRE( MapCreate_Patch( ws->pDocument, map_patch_shape_t::PLANE, { { -64, -64, 0 }, { 64, 64, 0 } }, 2u, 8u, StringView_FromCString( "materials/dev/grid.cymat" ), &id ) == map_status_t::OK );
                              MapWorkspace_DocumentChanged( ws ); MapWorkspace_SetSelection( ws, &id, 1 );
                              REQUIRE( EditorCommands_Execute( &gui->commands, StringView_FromCString( "map.patch.insert_row" ), {} ) == command_result_t::OK );
                              REQUIRE( EditorCommands_Execute( &gui->commands, StringView_FromCString( "map.patch.insert_column" ), {} ) == command_result_t::OK ); } },
        { "grp+1", [&]() { fresh(); a = box( { -64, -64, 0 }, { 64, 64, 128 } ); b = box( { 256, 0, 0 }, { 320, 64, 64 } ); const u64 ids[]{ a, b };
                           MapWorkspace_SetSelection( ws, ids, 2 ); REQUIRE( EditorCommands_Execute( &gui->commands, StringView_FromCString( "map.group.create" ), {} ) == command_result_t::OK );
                           const u64 c = box( { 512, 0, 0 }, { 576, 64, 64 } ); const u64 all[]{ a, b, c }; MapWorkspace_SetSelection( ws, all, 3 ); } },
        { "2ent", [&]() { fresh(); u64 e1 = 0, e2 = 0; const char *layer = ws->pDocument->layers.pData[0].id;
                          REQUIRE( MapDocument_AddEntity( ws->pDocument, StringView_FromCString( layer ), StringView_FromCString( "light" ), { 0, 0, 64 }, &e1 ) == map_status_t::OK );
                          REQUIRE( MapDocument_AddEntity( ws->pDocument, StringView_FromCString( layer ), StringView_FromCString( "light" ), { 256, 0, 64 }, &e2 ) == map_status_t::OK );
                          MapWorkspace_DocumentChanged( ws ); MapWorkspace_SetSelection( ws, &e1, 1 ); } },
        { "layer2", [&]() { fresh(); REQUIRE( EditorCommands_Execute( &gui->commands, StringView_FromCString( "map.layer.create" ), {} ) == command_result_t::OK );
                            a = box( { -64, -64, 0 }, { 64, 64, 128 } ); } },
    };
    // Every registered geometry, texture, patch, brush and component-selection
    // command (CYPHER_AUDIT_PREFIX narrows it, e.g. "map.mesh.").
    std::vector<std::string> ids;
    const QByteArray only = qgetenv( "CYPHER_AUDIT_PREFIX" );
    for ( usize i = 0; i < EditorCommands_Count( &gui->commands ); ++i ) {
        const std::string id = EditorCommands_At( &gui->commands, i )->pId;
        const bool family = id.rfind( "map.mesh.", 0 ) == 0 || id.rfind( "map.texture.", 0 ) == 0 || id.rfind( "map.patch.", 0 ) == 0 ||
                            id.rfind( "map.brush.", 0 ) == 0 || id.rfind( "map.select.", 0 ) == 0 || id.rfind( "map.group.", 0 ) == 0 ||
                            id.rfind( "map.transform.", 0 ) == 0 || id.rfind( "map.align.", 0 ) == 0 || id.rfind( "map.entity.", 0 ) == 0 ||
                            id.rfind( "map.layer.", 0 ) == 0 || id == "map.select_mode.navigation" || id == "map.tool.apply_material";
        if ( family && ( only.isEmpty() || id.rfind( only.constData(), 0 ) == 0 ) ) { ids.push_back( id ); }
    }
    const std::set<std::string> skip{ "map.texture.browser", "map.transform.dialog", "map.entity.class_browser", "assets.browse_materials", "assets.browser", "assets.database", "map.texture.fast", "map.texture.replace", "map.entity.io",
                                      "map.game.reload", "map.prefab.insert", "map.go_to", "map.info", "map.compile", "map.run", "map.stop", "map.leak.show", "map.check",
                                      "map.cordon.edit", "map.prefab.create" };
    std::vector<std::string> notes;
    std::printf( "%-30s", "command" );
    for ( const auto &context : contexts ) { std::printf( "%-8s", context.name ); }
    std::printf( "\n" );
    for ( const std::string &id : ids ) {
        std::printf( "%-30s", id.c_str() );
        for ( const auto &context : contexts ) {
            context.make();
            QCoreApplication::processEvents();
            const u32 state = EditorCommands_State( &gui->commands, StringView_FromCString( id.c_str() ) );
            std::string cell = "-";
            if ( ( state & COMMAND_STATE_ENABLED ) != 0u ) {
                if ( skip.count( id ) ) { cell = "on"; }
                else {
                    const usize before = EditorHistory_StepCount( &ws->history );
                    const auto result = EditorCommands_Execute( &gui->commands, StringView_FromCString( id.c_str() ), {} );
                    QCoreApplication::processEvents();
                    const usize after = EditorHistory_StepCount( &ws->history );
                    cell = result == command_result_t::OK ? ( after > before ? "+" + std::to_string( after - before ) : "0" ) : ( result == command_result_t::INVALID_ARGUMENTS ? "A" : "F" );
                    if ( cell == "F" || cell == "0" ) {
                        const auto *label = Mason_Window( session.pMason )->findChild<QLabel *>( QStringLiteral( "masonNotice" ) );
                        notes.push_back( id + " [" + context.name + "] " + cell + ": " + ( label != nullptr ? label->toolTip().toStdString() : std::string() ) );
                    }
                }
            }
            std::printf( "%-8s", cell.c_str() );
        }
        std::printf( "\n" );
    }
    for ( const std::string &note : notes ) { std::printf( "NOTE %s\n", note.c_str() ); }
}

// Object Properties for a light and a wave start of the showcase map, with
// the REAP game profile's typed editors: cypher_mason_smoke_tests
// "[.properties-shot]" (PNGs in CYPHER_SHOTS_DIR).
TEST_CASE( "Capture typed entity properties", "[.properties-shot]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    REQUIRE( Mason_OpenMap( session.pMason, QDir( QStringLiteral( CYPHER_REPOSITORY_ROOT ) ).filePath( QStringLiteral( "assets/black_mesa_entrance.cymap" ) ) ) );
    QCoreApplication::processEvents();
    std::printf( "profile: %s\n", MapGame_Status( ws ).toUtf8().constData() );
    const QByteArray env = qgetenv( "CYPHER_SHOTS_DIR" );
    const QDir out( env.isEmpty() ? QDir::temp().filePath( QStringLiteral( "cypher_showcase" ) ) : QString::fromUtf8( env ) );
    REQUIRE( QDir().mkpath( out.absolutePath() ) );
    auto *window = Mason_Window( session.pMason );
    window->resize( 2100, 1400 );
    auto *panel = window->findChild<QWidget *>( QStringLiteral( "MapObjectProperties" ) );
    REQUIRE( panel != nullptr );
    for ( const char *className : { "light", "info_wave_start", "info_enemy_spawn" } ) {
        u64 id = 0;
        for ( usize i = 0; i < ws->wire.entities.nCount && id == 0; ++i ) {
            if ( std::strcmp( ws->wire.entities.pData[i].className, className ) == 0 ) { id = ws->wire.entities.pData[i].id; }
        }
        if ( id == 0 ) { continue; }
        MapWorkspace_Select( ws, id, MAP_SELECT_REPLACE );
        QCoreApplication::processEvents();
        auto *tree = panel->findChild<QTreeWidget *>( QStringLiteral( "MapPropertiesTree" ) );
        for ( int i = 0; tree != nullptr && i < tree->topLevelItemCount(); ++i ) {
            tree->topLevelItem( i )->setExpanded( tree->topLevelItem( i )->text( 0 ) == QStringLiteral( "Entity keys" ) );
        }
        QCoreApplication::processEvents();
        REQUIRE( panel->grab().save( out.filePath( QStringLiteral( "properties_%1.png" ).arg( QString::fromLatin1( className ) ) ) ) );
    }
    // The views: class colours and sizes, helpers of the selected entities.
    std::vector<u64> picked;
    for ( const char *className : { "light", "info_player_start", "info_enemy_spawn" } ) {
        for ( usize i = 0; i < ws->wire.entities.nCount; ++i ) {
            if ( std::strcmp( ws->wire.entities.pData[i].className, className ) == 0 ) { picked.push_back( ws->wire.entities.pData[i].id ); break; }
        }
    }
    for ( usize i = 0; i < ws->wire.objects.nCount; ++i ) {
        if ( ws->wire.objects.pData[i].owner != 0 ) {
            const auto *owner = MapWireframe_FindEntity( ws->wire, ws->wire.objects.pData[i].owner );
            if ( owner != nullptr && std::strcmp( owner->className, "func_door" ) == 0 ) { picked.push_back( ws->wire.objects.pData[i].id ); break; }
        }
    }
    MapWorkspace_SetSelection( ws, picked.data(), picked.size() );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, ws, map_ortho_axes_t::TOP ) );
    top->resize( 1400, 1000 );
    top->show();
    MapWorkspace_Frame( ws, CY_TRUE );
    QCoreApplication::processEvents();
    REQUIRE( top->grab().save( out.filePath( QStringLiteral( "views_top_selected.png" ) ) ) );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, ws ) );
    camera->resize( 1400, 900 );
    camera->show();
    if ( !picked.empty() ) {
        const auto *first = MapWireframe_FindEntity( ws->wire, picked.front() );
        if ( first != nullptr ) { MapCameraView_LookAt( camera.get(), { first->origin.x - 500, first->origin.y - 500, first->origin.z + 260 }, first->origin ); }
    }
    QCoreApplication::processEvents();
    REQUIRE( camera->grab().save( out.filePath( QStringLiteral( "views_camera_selected.png" ) ) ) );
    std::printf( "property shots: %s\n", out.absolutePath().toUtf8().constData() );
}

// Prefab instances in the small arena: a stack of its crates made a prefab
// (in a scratch content folder) and placed three more times, turned:
// cypher_mason_smoke_tests "[.instances-shot]".
TEST_CASE( "Capture prefab instances", "[.instances-shot]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    REQUIRE( Mason_OpenMap( session.pMason, QDir( QStringLiteral( CYPHER_REPOSITORY_ROOT ) ).filePath( QStringLiteral( "assets/pump_station.cymap" ) ) ) );
    QTemporaryDir content;
    REQUIRE( content.isValid() );
    MapHotspots_SetResolver( ws, [&content]( const QString &path ) {
        const QString scratch = QDir( content.path() ).filePath( path );
        if ( QFileInfo::exists( scratch ) ) { return scratch; }
        const QString repository = QDir( QStringLiteral( CYPHER_REPOSITORY_ROOT "/assets" ) ).filePath( path );
        return QFileInfo::exists( repository ) ? repository : QString();
    } );
    MapAssets_SetRoots( ws, { content.path() } );
    // The crates: small boxes standing on the floor near the middle.
    std::vector<u64> crates;
    for ( usize i = 0; i < ws->wire.objects.nCount; ++i ) {
        const auto &object = ws->wire.objects.pData[i];
        const auto &box = object.bounds.box;
        const f64 cx = 0.5 * ( box.minimum.x + box.maximum.x ), cy = 0.5 * ( box.minimum.y + box.maximum.y );
        if ( object.kind == map_wire_kind_t::BRUSH && object.owner == 0 && std::abs( cx + 480 ) < 100 && std::abs( cy + 576 ) < 100 && box.maximum.z <= 130 ) { crates.push_back( object.id ); }
    }
    std::printf( "crates: %zu\n", crates.size() );
    REQUIRE( !crates.empty() );
    MapWorkspace_SetSelection( ws, crates.data(), crates.size() );
    REQUIRE( session.Run( QStringLiteral( "map.prefab.create_instance prefabs/crate_stack" ) ) == command_result_t::OK );
    for ( int copy = 0; copy < 3; ++copy ) {
        REQUIRE( map::MapWorkspace_TranslateSelection( ws, { 160, 96, 0 }, true ) );
        const u64 placed = EditorSelection_At( &ws->selection, 0 );
        map::map_instance_info_t info{};
        REQUIRE( map::MapInstances_Info( ws, placed, &info ) );
        REQUIRE( map::MapWorkspace_RotateSelection( ws, { 0, 0, 35.0 * ( copy + 1 ) }, info.origin ) );
    }
    const QByteArray env = qgetenv( "CYPHER_SHOTS_DIR" );
    const QDir out( env.isEmpty() ? QDir::temp().filePath( QStringLiteral( "cypher_showcase" ) ) : QString::fromUtf8( env ) );
    REQUIRE( QDir().mkpath( out.absolutePath() ) );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, ws ) );
    camera->resize( 1400, 900 );
    camera->show();
    MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::SHADED );
    map::map_instance_info_t info{};
    REQUIRE( map::MapInstances_Info( ws, EditorSelection_At( &ws->selection, 0 ), &info ) );
    MapCameraView_LookAt( camera.get(), { info.origin.x - 520, info.origin.y - 480, info.origin.z + 300 }, { info.origin.x - 160, info.origin.y - 60, info.origin.z } );
    QCoreApplication::processEvents();
    REQUIRE( camera->grab().save( out.filePath( QStringLiteral( "instances_camera.png" ) ) ) );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, ws, map_ortho_axes_t::TOP ) );
    top->resize( 1200, 900 );
    top->show();
    MapWorkspace_Frame( ws, CY_TRUE );
    QCoreApplication::processEvents();
    REQUIRE( top->grab().save( out.filePath( QStringLiteral( "instances_top.png" ) ) ) );
    auto *outliner = Mason_Window( session.pMason )->findChild<QWidget *>( QStringLiteral( "MapOutliner" ) );
    if ( outliner != nullptr ) { REQUIRE( outliner->grab().save( out.filePath( QStringLiteral( "instances_outliner.png" ) ) ) ); }
    std::printf( "instance shots: %s\n", out.absolutePath().toUtf8().constData() );
    MapAssets_SetRoots( ws, {} );
}

// The UV Editor on a floor face of the small arena, before and after a
// turn, a scale across the turned texture and a shear (the camera view has
// no textured mode; that comes with the GPU renderer):
// cypher_mason_smoke_tests "[.uv-shot]".
TEST_CASE( "Capture the UV Editor", "[.uv-shot]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    REQUIRE( Mason_OpenMap( session.pMason, QDir( QStringLiteral( CYPHER_REPOSITORY_ROOT ) ).filePath( QStringLiteral( "assets/pump_station.cymap" ) ) ) );
    // The widest textured floor face.
    u64 object = 0, side = 0;
    f64 widest = 0.0;
    for ( usize i = 0; i < ws->wire.faces.nCount; ++i ) {
        const auto &face = ws->wire.faces.pData[i];
        const auto *owner = MapWireframe_FindObject( ws->wire, face.id );
        if ( owner == nullptr || owner->kind != map_wire_kind_t::BRUSH || face.normal.z < 0.9 || face.material == 0 || face.sideId == 0 ) { continue; }
        const auto &box = owner->bounds.box;
        const f64 span = std::min( box.maximum.x - box.minimum.x, box.maximum.y - box.minimum.y );
        if ( span > widest ) { widest = span; object = face.id; side = face.sideId; }
    }
    REQUIRE( object != 0 );
    MapWorkspace_SetElementMode( ws, map_element_mode_t::FACES );
    MapWorkspace_SelectBrushFace( ws, object, side );
    const QByteArray env = qgetenv( "CYPHER_SHOTS_DIR" );
    const QDir out( env.isEmpty() ? QDir::temp().filePath( QStringLiteral( "cypher_showcase" ) ) : QString::fromUtf8( env ) );
    REQUIRE( QDir().mkpath( out.absolutePath() ) );
    std::unique_ptr<QWidget> panel( MapUvEditor_Create( nullptr, ws ) );
    panel->resize( 720, 760 );
    panel->show();
    QCoreApplication::processEvents();
    REQUIRE( panel->grab().save( out.filePath( QStringLiteral( "uv_editor_face.png" ) ) ) );
    struct job_t { map_surface_ref_t surface; map_uv_edit_t edit; } job{ { object, side }, {} };
    const auto apply = []( void *pContext, map_document_t *pCopy, vector_t<u64> * ) {
        auto *pJob = static_cast<job_t *>( pContext );
        return MapTextureEdit_EditUv( pCopy, { &pJob->surface, 1 }, pJob->edit );
    };
    job.edit.kind = map_uv_edit_kind_t::ROTATE;
    job.edit.pivot = MapUvEditor_Pivot( panel.get() );
    job.edit.radians = 0.5;
    REQUIRE( MapWorkspace_ApplyEdit( ws, "Rotate Texture", apply, &job ) );
    job.edit.kind = map_uv_edit_kind_t::SCALE;
    job.edit.factor = { 1.5, 1.0 };
    REQUIRE( MapWorkspace_ApplyEdit( ws, "Scale Texture", apply, &job ) );
    job.edit.kind = map_uv_edit_kind_t::SHEAR;
    job.edit.shear = { 0.25, 0.0 };
    REQUIRE( MapWorkspace_ApplyEdit( ws, "Shear Texture", apply, &job ) );
    MapWorkspace_SelectBrushFace( ws, object, side );
    QCoreApplication::processEvents();
    REQUIRE( panel->grab().save( out.filePath( QStringLiteral( "uv_editor_edited.png" ) ) ) );
    std::printf( "uv shots: %s\n", out.absolutePath().toUtf8().constData() );
}

// The Shear tool on the small arena's crate stack: the side hovered, then
// mid-drag in the Top view (the sheared ghost over the original bounds),
// then the result in 3D: cypher_mason_smoke_tests "[.shear-shot]".
TEST_CASE( "Capture the Shear tool", "[.shear-shot]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    REQUIRE( Mason_OpenMap( session.pMason, QDir( QStringLiteral( CYPHER_REPOSITORY_ROOT ) ).filePath( QStringLiteral( "assets/pump_station.cymap" ) ) ) );
    std::vector<u64> crates;
    for ( usize i = 0; i < ws->wire.objects.nCount; ++i ) {
        const auto &object = ws->wire.objects.pData[i];
        const auto &box = object.bounds.box;
        const f64 cx = 0.5 * ( box.minimum.x + box.maximum.x ), cy = 0.5 * ( box.minimum.y + box.maximum.y );
        if ( object.kind == map_wire_kind_t::BRUSH && object.owner == 0 && std::abs( cx + 480 ) < 100 && std::abs( cy + 576 ) < 100 && box.maximum.z <= 130 ) { crates.push_back( object.id ); }
    }
    REQUIRE( !crates.empty() );
    MapWorkspace_SetSelection( ws, crates.data(), crates.size() );
    MapWorkspace_SetTool( ws, map_tool_t::SHEAR );
    map_bounds_t bounds{};
    for ( u64 id : crates ) { MapBounds_AddBounds( bounds, MapWireframe_FindObject( ws->wire, id )->bounds ); }
    const QByteArray env = qgetenv( "CYPHER_SHOTS_DIR" );
    const QDir out( env.isEmpty() ? QDir::temp().filePath( QStringLiteral( "cypher_showcase" ) ) : QString::fromUtf8( env ) );
    REQUIRE( QDir().mkpath( out.absolutePath() ) );
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, ws, map_ortho_axes_t::FRONT ) );
    top->resize( 900, 700 );
    top->show();
    ws->frameBounds = bounds;
    ws->frameBounds.box.minimum.y -= 160; ws->frameBounds.box.maximum.y += 160;
    ws->frameBounds.box.minimum.z -= 64; ws->frameBounds.box.maximum.z += 96;
    MapWorkspace_Notify( ws, MAP_CHANGE_FRAME );
    QCoreApplication::processEvents();
    // Front view: y across, z up. Grab the top side and slide it along y.
    const f64 midY = 0.5 * ( bounds.box.minimum.y + bounds.box.maximum.y ), topZ = bounds.box.maximum.z;
    const QPointF grab = MapOrthoView_WorldToView( top.get(), { midY, topZ } ), to = MapOrthoView_WorldToView( top.get(), { midY + 64, topZ } );
    const auto mouse = [&]( QEvent::Type type, QPointF at, Qt::MouseButtons buttons ) {
        QMouseEvent event( type, at, top->mapToGlobal( at ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons, Qt::NoModifier );
        QCoreApplication::sendEvent( top.get(), &event );
        QCoreApplication::processEvents();
    };
    mouse( QEvent::MouseMove, grab, Qt::NoButton );
    REQUIRE( top->grab().save( out.filePath( QStringLiteral( "shear_hover.png" ) ) ) );
    mouse( QEvent::MouseButtonPress, grab, Qt::LeftButton );
    mouse( QEvent::MouseMove, ( grab + to ) / 2, Qt::LeftButton );
    mouse( QEvent::MouseMove, to, Qt::LeftButton );
    REQUIRE( top->grab().save( out.filePath( QStringLiteral( "shear_drag.png" ) ) ) );
    mouse( QEvent::MouseButtonRelease, to, Qt::NoButton );
    map_bounds_t after{};
    for ( u64 id : crates ) { MapBounds_AddBounds( after, MapWireframe_FindObject( ws->wire, id )->bounds ); }
    std::printf( "shear: y %.1f..%.1f -> %.1f..%.1f, z %.1f..%.1f -> %.1f..%.1f\n", bounds.box.minimum.y, bounds.box.maximum.y, after.box.minimum.y,
                 after.box.maximum.y, bounds.box.minimum.z, bounds.box.maximum.z, after.box.minimum.z, after.box.maximum.z );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, ws ) );
    camera->resize( 1100, 750 );
    camera->show();
    MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::SHADED );
    const math::vec3d_t centre = MapBounds_Center( bounds );
    MapCameraView_LookAt( camera.get(), { centre.x - 380, centre.y - 120, centre.z + 140 }, centre );
    QCoreApplication::processEvents();
    REQUIRE( camera->grab().save( out.filePath( QStringLiteral( "shear_camera.png" ) ) ) );
    std::printf( "shear shots: %s\n", out.absolutePath().toUtf8().constData() );
}

// Entity attachment in the small arena: three entities attached to a
// fourth, the parent selected, in the Top view and in 3D:
// cypher_mason_smoke_tests "[.hierarchy-shot]".
TEST_CASE( "Capture entity attachment", "[.hierarchy-shot]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    REQUIRE( Mason_OpenMap( session.pMason, QDir( QStringLiteral( CYPHER_REPOSITORY_ROOT ) ).filePath( QStringLiteral( "assets/pump_station.cymap" ) ) ) );
    std::vector<u64> placed;
    for ( usize i = 0; i < ws->wire.entities.nCount && placed.size() < 4u; ++i ) {
        if ( ws->wire.entities.pData[i].bHasOrigin ) { placed.push_back( ws->wire.entities.pData[i].id ); }
    }
    REQUIRE( placed.size() == 4u );
    // Picked one by one: the last is the parent.
    std::vector<u64> picked;
    for ( u64 id : placed ) { picked.push_back( id ); MapWorkspace_SetSelection( ws, picked.data(), picked.size() ); }
    REQUIRE( session.Run( QStringLiteral( "map.entity.reparent" ) ) == command_result_t::OK );
    MapWorkspace_SetSelection( ws, &placed.back(), 1 );
    const QByteArray env = qgetenv( "CYPHER_SHOTS_DIR" );
    const QDir out( env.isEmpty() ? QDir::temp().filePath( QStringLiteral( "cypher_showcase" ) ) : QString::fromUtf8( env ) );
    REQUIRE( QDir().mkpath( out.absolutePath() ) );
    map_bounds_t bounds{};
    for ( u64 id : placed ) { MapBounds_AddPoint( bounds, MapWireframe_FindEntity( ws->wire, id )->origin ); }
    std::unique_ptr<QWidget> top( MapOrthoView_Create( nullptr, ws, map_ortho_axes_t::TOP ) );
    top->resize( 1000, 800 );
    top->show();
    ws->frameBounds = bounds;
    for ( u32 a = 0; a < 3; ++a ) { ( a == 0 ? ws->frameBounds.box.minimum.x : a == 1 ? ws->frameBounds.box.minimum.y : ws->frameBounds.box.minimum.z ) -= 128; }
    ws->frameBounds.box.maximum.x += 128; ws->frameBounds.box.maximum.y += 128; ws->frameBounds.box.maximum.z += 128;
    MapWorkspace_Notify( ws, MAP_CHANGE_FRAME );
    QCoreApplication::processEvents();
    REQUIRE( top->grab().save( out.filePath( QStringLiteral( "hierarchy_top.png" ) ) ) );
    std::printf( "hierarchy shots: %s\n", out.absolutePath().toUtf8().constData() );
}

// The small arena (assets/pump_station.cymap) from a few viewpoints.
TEST_CASE( "Render the small arena map", "[.showcase-shots]" )
{
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    REQUIRE( Mason_OpenMap( session.pMason, QDir( QStringLiteral( CYPHER_REPOSITORY_ROOT ) ).filePath( QStringLiteral( "assets/pump_station.cymap" ) ) ) );
    QCoreApplication::processEvents();
    const QByteArray env = qgetenv( "CYPHER_SHOTS_DIR" );
    const QDir out( env.isEmpty() ? QDir::temp().filePath( QStringLiteral( "cypher_showcase" ) ) : QString::fromUtf8( env ) );
    REQUIRE( QDir().mkpath( out.absolutePath() ) );
    MapWorkspace_SetVisgroupHidden( ws, map_visgroup_t::TRIGGERS, CY_TRUE );
    std::unique_ptr<QWidget> camera( MapCameraView_Create( nullptr, ws ) );
    camera->resize( 1600, 900 );
    camera->show();
    MapCameraView_SetRenderMode( camera.get(), map_render_mode_t::SHADED );
    struct shot_t { const char *name; math::vec3d_t eye, target; };
    const shot_t shots[]{
        { "arena_01_overview", { -740, -740, 500 }, { 260, 300, -40 } },
        { "arena_02_bridge", { -600, -600, 320 }, { 200, 200, 0 } },
        { "arena_03_catwalk", { 700, -720, 330 }, { -300, 200, 80 } },
        { "arena_04_spawn_view", { -900, 0, 72 }, { 400, 0, 120 } },
    };
    for ( const shot_t &shot : shots ) {
        MapCameraView_LookAt( camera.get(), shot.eye, shot.target );
        QCoreApplication::processEvents();
        REQUIRE( camera->grab().save( out.filePath( QString::fromLatin1( shot.name ) + QStringLiteral( ".png" ) ) ) );
    }
    // Navigation mode on the same overview: floor green, too steep red.
    MapWorkspace_SetElementMode( ws, map_element_mode_t::NAVIGATION );
    MapCameraView_LookAt( camera.get(), shots[0].eye, shots[0].target );
    QCoreApplication::processEvents();
    REQUIRE( camera->grab().save( out.filePath( QStringLiteral( "arena_05_navigation.png" ) ) ) );
}

TEST_CASE( "Physical Shift+G continues a rotated edge strip around its captured custom pivot", "[mason][smoke][modelling-repeat][native-input]" )
{
    namespace geo = cypher::editor::geometry;
    mason_session_t session;
    auto *ws = Mason_MapWorkspace( session.pMason );
    auto *window = Mason_Window( session.pMason );
    auto *views = window->findChild<QWidget *>( QStringLiteral( "EditorViewGrid" ) ); REQUIRE( views );
    auto *top = MapViews_PaneView( views, 1 ); REQUIRE( top );
    window->activateWindow(); top->setFocus(); QCoreApplication::processEvents();
    const auto mouse = [&]( QEvent::Type type, QPointF at, Qt::MouseButtons held ) {
        QMouseEvent event( type, at, top->mapToGlobal( at ), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, held, Qt::NoModifier );
        QCoreApplication::sendEvent( top, &event ); QCoreApplication::processEvents();
    };
    const auto click = [&]( f64 x, f64 y ) {
        const auto at = MapOrthoView_WorldToView( top, { x, y } );
        mouse( QEvent::MouseButtonPress, at, Qt::LeftButton ); mouse( QEvent::MouseButtonRelease, at, Qt::NoButton );
    };
    const auto repeat = [&]() {
        top->setFocus(); QCoreApplication::processEvents();
        QKeyEvent preflight( QEvent::ShortcutOverride, Qt::Key_G, Qt::ShiftModifier, QStringLiteral( "G" ) ); QCoreApplication::sendEvent( top, &preflight );
        QKeyEvent press( QEvent::KeyPress, Qt::Key_G, Qt::ShiftModifier, QStringLiteral( "G" ) ); QCoreApplication::sendEvent( top, &press );
        QKeyEvent release( QEvent::KeyRelease, Qt::Key_G, Qt::ShiftModifier, QStringLiteral( "G" ) ); QCoreApplication::sendEvent( top, &release );
        QCoreApplication::processEvents();
    };
    map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.axis = 2;
    quad.bounds = { { 128, -64, 0 }, { 256, 64, 0 } };
    REQUIRE( MapWorkspace_CreatePrimitive( ws, quad ) );
    REQUIRE( ws->selection.ids.nCount == 1 ); const u64 mesh = ws->selection.ids.pData[0];
    REQUIRE( session.Run( QStringLiteral( "map.grid 32" ) ) == command_result_t::OK ); MapWorkspace_SetAngleSnap( ws, 15 );
    const auto *choice = EditorSettings_Find( &Mason_Gui( session.pMason )->settings, StringView_FromCString( "editor.map.edge_drag_action" ) ); REQUIRE( choice );
    setting_value_t value{}; value.type = setting_type_t::ENUM; value.text = StringView_FromCString( "extend" );
    REQUIRE( EditorSettings_Write( &Mason_Gui( session.pMason )->settings, settings_scope_t::USER, *choice, value ) == settings_registry_status_t::OK );
    REQUIRE( session.Run( QStringLiteral( "map.select_mode.edges" ) ) == command_result_t::OK );
    REQUIRE( session.Run( QStringLiteral( "map.tool.select" ) ) == command_result_t::OK );
    click( 192, 64 ); REQUIRE( ws->meshSelection.edges.nCount == 1 );
    const auto initial = ws->meshSelection.edges.pData[0];
    const auto position = [&]( geo::geometry_source_id_t id ) {
        const auto *source = geo::GeometryDocument_FindMesh( &ws->pDocument->geometry, { mesh } ); REQUIRE( source );
        geo::geometry_mesh_vertex_handle_t handle{}; REQUIRE( geo::MeshSource_TryFindVertex( source, id, &handle ) );
        return GenerationPool_Get( &source->mesh.vertices, handle )->position;
    };
    const auto initialA = position( initial.a ), initialB = position( initial.b );
    struct snapshot_t {
        geo::mesh_source_description_t value{};
        snapshot_t( const map_document_t *document, u64 object ) {
            REQUIRE( geo::MeshSourceDescription_Init( &value, Allocator_GetSystem(), { object } ) == geo::geometry_status_t::OK );
            REQUIRE( geo::MeshSource_TryDescribe( geo::GeometryDocument_FindMesh( &document->geometry, { object } ), &value ) == geo::geometry_status_t::OK );
        }
        ~snapshot_t() { geo::MeshSourceDescription_Shutdown( &value ); }
    } original( ws->pDocument, mesh );
    REQUIRE( session.Run( QStringLiteral( "map.tool.pivot" ) ) == command_result_t::OK ); click( 0, 0 );
    REQUIRE( session.Run( QStringLiteral( "map.tool.rotate" ) ) == command_result_t::OK );
    const f64 radians = 3.14159265358979323846 / 12.0;
    const f64 radius = 60.0 / MapOrthoView_Zoom( top );
    const QPointF start = MapOrthoView_WorldToView( top, { radius, 0 } );
    const QPointF finish = MapOrthoView_WorldToView( top, { radius * std::cos( radians ), radius * std::sin( radians ) } );
    const usize steps = EditorHistory_StepCount( &ws->history );
    mouse( QEvent::MouseButtonPress, start, Qt::LeftButton ); mouse( QEvent::MouseMove, finish, Qt::LeftButton );
    mouse( QEvent::MouseButtonRelease, finish, Qt::NoButton );
    REQUIRE( EditorHistory_StepCount( &ws->history ) == steps + 1 );
    const auto check = [&]( u32 strips ) {
        snapshot_t after( ws->pDocument, mesh );
        CHECK( after.value.faces.nCount == 1 + strips ); CHECK( after.value.vertices.nCount == 4 + strips * 2 );
        CHECK( EditorHistory_StepCount( &ws->history ) == steps + strips );
        for ( usize i = 0; i < original.value.vertices.nCount; ++i ) {
            const auto &before = original.value.vertices.pData[i]; const auto now = position( before.sourceId );
            CHECK( now.x == Catch::Approx( before.position.x ) ); CHECK( now.y == Catch::Approx( before.position.y ) ); CHECK( now.z == Catch::Approx( before.position.z ) );
        }
        REQUIRE( ws->meshSelection.edges.nCount == 1 ); const auto edge = ws->meshSelection.edges.pData[0];
        const auto turned = [&]( math::vec3d_t p ) {
            const f64 angle = radians * strips;
            return math::vec3d_t{ p.x * std::cos( angle ) - p.y * std::sin( angle ), p.x * std::sin( angle ) + p.y * std::cos( angle ), p.z };
        };
        const auto a = position( edge.a ), b = position( edge.b ); const auto expectedA = turned( initialA ), expectedB = turned( initialB );
        const auto close = []( math::vec3d_t p, math::vec3d_t q ) { return std::fabs( p.x - q.x ) < 1e-6 && std::fabs( p.y - q.y ) < 1e-6 && std::fabs( p.z - q.z ) < 1e-6; };
        CHECK( ( ( close( a, expectedA ) && close( b, expectedB ) ) || ( close( a, expectedB ) && close( b, expectedA ) ) ) );
        math::vec3d_t pivot{}; REQUIRE( MapToolGestures_Pivot( ws, &pivot ) ); CHECK( close( pivot, {} ) );
    };
    check( 1 );
    // A tool switch must not turn repeat into another activation of Rotate.
    REQUIRE( session.Run( QStringLiteral( "map.tool.translate" ) ) == command_result_t::OK );
    repeat(); check( 2 ); repeat(); check( 3 );
    REQUIRE( session.Run( QStringLiteral( "edit.undo" ) ) == command_result_t::OK );
    REQUIRE( ws->meshSelection.edges.nCount == 1 );
    REQUIRE( session.Run( QStringLiteral( "edit.redo" ) ) == command_result_t::OK );
    check( 3 ); repeat(); check( 4 );
    const auto rim = ws->meshSelection.edges.pData[0];
    MapWorkspace_SetElementMode( ws, map_element_mode_t::VERTICES ); repeat();
    CHECK( EditorHistory_StepCount( &ws->history ) == steps + 4 ); // Disabled recipe must not replay a tool command.
    MapWorkspace_SetElementMode( ws, map_element_mode_t::EDGES );
    REQUIRE( MapWorkspace_SelectMeshEdge( ws, mesh, rim, MAP_SELECT_REPLACE ) );
    // An explicitly selected older command has deliberate precedence over
    // the pointer recipe, even if that panel is no longer focused.
    REQUIRE( session.Run( QStringLiteral( "view.command_history" ) ) == command_result_t::OK );
    auto *history = window->findChild<QWidget *>( QStringLiteral( "EditorCommandHistory" ) ); REQUIRE( history );
    const auto rows = EditorCommandHistory_Rows( history ); const int gridRow = rows.indexOf( QStringLiteral( "map.grid 32" ) ); REQUIRE( gridRow >= 0 );
    EditorCommandHistory_SelectRows( history, { gridRow } ); MapWorkspace_SetGridSize( ws, 16 ); repeat();
    CHECK( ws->gridSize == 32 ); CHECK( EditorHistory_StepCount( &ws->history ) == steps + 4 );
    CHECK_FALSE( MapToolGestures_HasModellingRepeat( ws ) );
    EditorCommandHistory_SelectRows( history, {} );
    REQUIRE( session.Run( QStringLiteral( "map.grid.larger" ) ) == command_result_t::OK ); repeat(); CHECK( ws->gridSize == 128 );
}
