//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_ToolPanels_Tests.cpp
//  Purpose: Exercises tool-property presentation against real commands,
//           settings, selection, and tool availability.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_ToolPanels.h"
#include "CypherMapGui_Input.h"
#include "CypherEditorGui_Section.h"
#include "CypherGeometry_DocumentBrushAttributes.h"
#include "CypherGeometry_DocumentMeshes.h"
#include "CypherCommon/Mathlib/CypherMath_Scalar.h"
#include "CypherCommon/Tier1/CypherCommon_Allocator.h"

#include <catch2/catch_test_macros.hpp>

#include <QApplication>
#include <QAbstractItemModel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMenu>
#include <QSpinBox>
#include <QPushButton>
#include <QPointer>
#include <QPersistentModelIndex>
#include <QToolButton>
#include <QTreeWidget>

#include <filesystem>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

using namespace cypher;
using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{

struct session_t {
    session_t()
    {
        auto *pApp = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, pApp, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( SettingsDocument_Init( &settings, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, &settings );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        REQUIRE( MapWorkspace_RegisterCommands( &workspace, &gui.commands ) == command_registry_status_t::OK );
        const QString path = QString::fromStdString( ( std::filesystem::path( CYPHER_MAP_EXAMPLE_DIR ) / "facility.cymap" ).string() );
        REQUIRE( MapWorkspace_Open( &workspace, path ).status == map_files_status_t::OK );
    }
    gui::editor_gui_t gui{};
    settings_document_t settings{};
    map_workspace_t workspace{};
};

QToolButton *Operation( QWidget &panel, const char *pId )
{
    auto *pButton = panel.findChild<QToolButton *>( QString::fromUtf8( pId ) );
    REQUIRE( pButton != nullptr );
    return pButton;
}

template <typename T> T *Setting( QWidget &panel, const char *pPath )
{
    auto *pControl = panel.findChild<QWidget *>( QString::fromUtf8( pPath ) );
    REQUIRE( pControl != nullptr );
    auto *pEditor = pControl->findChild<T *>();
    REQUIRE( pEditor != nullptr );
    return pEditor;
}

QToolButton *ShapeButton( QWidget &panel, const char *shape )
{
    auto *button = panel.findChild<QToolButton *>( QStringLiteral( "MapPrimitive.%1" ).arg( QString::fromLatin1( shape ) ) );
    REQUIRE( button != nullptr );
    return button;
}

void InspectPlannedTool( session_t &session, map_tool_t tool )
{
    // The tool command remains disabled until the viewport interaction is
    // connected. Inspect its property schema directly without enabling it.
    session.workspace.tool = tool;
    MapWorkspace_Notify( &session.workspace, MAP_CHANGE_VIEW );
}

void RegisterReferenceCommand( session_t &session, const char *id, const char *label )
{
    // Application-owned commands do not depend on Mason in these map-GUI
    // presentation tests. Their execution is covered by the real workspace.
    if ( EditorCommands_Find( &session.gui.commands, StringView_FromCString( id ) ) != nullptr ) { return; }
    const command_desc_t command{ id, label, nullptr, nullptr, nullptr, COMMAND_FLAG_NONE,
        []( void *, const command_args_t & ) noexcept { return command_result_t::OK; }, nullptr, nullptr };
    REQUIRE( EditorCommands_Register( &session.gui.commands, &command, 1u ) == command_registry_status_t::OK );
}

QStringList ReferenceRowsContaining( QWidget &panel, const char *text )
{
    QStringList rows;
    for ( const QString &row : MapToolProperties_KeyRows( &panel ) ) {
        if ( row.contains( QString::fromUtf8( text ) ) ) { rows.append( row ); }
    }
    return rows;
}

struct tool_cancel_probe_t {
    map_workspace_t *workspace{};
    int overrides{};
    int presses{};
    int handled{};
    bool decline{};
    bool leaveOnPress{};
};

bool ProbeToolCancel( void *context, QEvent *event )
{
    auto &probe = *static_cast<tool_cancel_probe_t *>( context );
    if ( event->type() == QEvent::ShortcutOverride ) { ++probe.overrides; }
    else if ( event->type() == QEvent::KeyPress ) { ++probe.presses; }
    else { FAIL( "The tool bridge must forward only key presses and shortcut overrides" ); return false; }
    if ( probe.decline || MapInput_ToolGestureKey( probe.workspace, static_cast<QKeyEvent *>( event ), false ) != map_tool_gesture_key_t::CANCEL ) { return false; }
    ++probe.handled;
    event->accept();
    // Exercise the callback contract: the panel and its handler survive,
    // while the button that is currently filtering this event is destroyed.
    if ( event->type() == QEvent::KeyPress && probe.leaveOnPress ) { MapWorkspace_SetTool( probe.workspace, map_tool_t::NONE ); }
    return true;
}

void FocusToolControl( QWidget *control, bool exact = true )
{
    REQUIRE( control ); REQUIRE( control->isEnabled() );
    control->window()->show(); control->window()->activateWindow();
    QCoreApplication::processEvents(); control->setFocus( Qt::OtherFocusReason ); QCoreApplication::processEvents();
    QWidget *focused = QApplication::focusWidget(); REQUIRE( focused );
    REQUIRE( ( focused == control || ( !exact && control->isAncestorOf( focused ) ) ) );
}

bool SendToolKey( QWidget *control, QEvent::Type type, int key = Qt::Key_Escape )
{
    QKeyEvent event( type, key, Qt::NoModifier ); event.setAccepted( false );
    ( void )QCoreApplication::sendEvent( control, &event );
    return event.isAccepted();
}

} // namespace

TEST_CASE( "Tool properties expose the key reference and execute selection operations", "[map][gui][toolpanels]" )
{
    session_t session;
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    panel->resize( 270, 800 );
    panel->show();
    QCoreApplication::processEvents();
    auto *pTitle = panel->findChild<QLabel *>( QStringLiteral( "MapToolTitle" ) );
    REQUIRE( pTitle != nullptr );
    CHECK( pTitle->text() == QStringLiteral( "Selection Tool" ) );
    CHECK( pTitle->font().bold() );
    auto *pKeys = panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) );
    REQUIRE( pKeys != nullptr );
    CHECK( pKeys->headerItem()->text( 0 ) == QStringLiteral( "Key" ) );
    CHECK( pKeys->headerItem()->text( 1 ) == QStringLiteral( "Operation" ) );
    CHECK( pKeys->isVisible() );
    CHECK( pKeys->height() < 250 );
    bool foundToggle = false;
    bool foundDrag = false;
    for ( int row = 0; row < pKeys->topLevelItemCount(); ++row ) {
        const auto *item = pKeys->topLevelItem( row );
        if ( item->toolTip( 0 ) == QStringLiteral( "[Ctrl+LeftClick]" ) ) {
            foundToggle = true;
            CHECK( item->text( 0 ) == QStringLiteral( "Ctrl+LMB" ) );
            CHECK( pKeys->fontMetrics().horizontalAdvance( item->text( 0 ) ) + 8 <= pKeys->columnWidth( 0 ) );
        }
        if ( item->toolTip( 0 ) == QStringLiteral( "[LeftDrag]" ) ) {
            foundDrag = true;
            CHECK( item->text( 0 ) == QStringLiteral( "LMB drag" ) );
        }
    }
    CHECK( foundToggle );
    CHECK( foundDrag ); // Marquee selection is connected in every viewport.
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Ctrl+LeftClick] Select toggle" ) ) );
    auto *pReference = panel->findChild<QWidget *>( QStringLiteral( "MapToolShortcuts" ) );
    REQUIRE( pReference != nullptr );
    CHECK( gui::EditorSection_IsExpanded( pReference ) );
    // Turning a 2D aid off must not also remove it from the perspective view.
    auto *bounds2d = Setting<QCheckBox>( *panel, "editor.viewport.show_selection_bounds" );
    auto *bounds3d = Setting<QCheckBox>( *panel, "editor.viewport.perspective.show_selection_bounds" );
    REQUIRE( bounds2d->isChecked() );
    REQUIRE( bounds3d->isChecked() );
    bounds2d->setChecked( false );
    CHECK( bounds3d->isChecked() );
    CHECK( EditorSettings_Bool( &session.gui.settings, "editor.viewport.perspective.show_selection_bounds", CY_FALSE ) );
    bounds2d->setChecked( true );
    auto *pHide = Operation( *panel, "map.hide.selected" );
    CHECK_FALSE( pHide->isEnabled() );
    CHECK( pHide->toolButtonStyle() == Qt::ToolButtonIconOnly );
    CHECK_FALSE( pHide->icon().isNull() );
    CHECK( pHide->accessibleName() == QStringLiteral( "Hide Selected" ) );
    MapWorkspace_Select( &session.workspace, 1000u, MAP_SELECT_REPLACE );
    CHECK( pHide == Operation( *panel, "map.hide.selected" ) ); // Selection refresh must preserve controls.
    CHECK( pHide->isEnabled() );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolSelection" ) )->text().startsWith( QStringLiteral( "1 selected" ) ) );
    pHide->click();
    CHECK( EditorSelection_Contains( &session.workspace.hidden, 1000u ) );
    auto *pShow = Operation( *panel, "map.hide.show_all" );
    REQUIRE( pShow->isEnabled() );
    pShow->click();
    CHECK_FALSE( EditorSelection_Contains( &session.workspace.hidden, 1000u ) );
    CHECK_FALSE( pShow->isEnabled() );
    CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
}

TEST_CASE( "Tool gesture references follow effective remaps and distinguish viewport families", "[map][gui][toolpanels][gesture][help]" )
{
    session_t session;
    MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    const auto gestureRows = [&]() {
        QStringList rows;
        for ( const auto &row : MapToolProperties_KeyRows( panel.get() ) ) {
            if ( ( row.contains( QStringLiteral( "current gesture" ) ) || row.contains( QStringLiteral( "idle: enter Navigation" ) ) ||
                   row.contains( QStringLiteral( "Create staged primitive" ) ) || row.contains( QStringLiteral( "Discard footprint/staged primitive" ) ) ) &&
                 !row.startsWith( QStringLiteral( "[Focus loss]" ) ) ) { rows.append( row ); }
        }
        return rows;
    };
    REQUIRE( gestureRows() == QStringList{ QStringLiteral( "[Enter / NumEnter] Create staged primitive (one undo step)" ),
                                          QStringLiteral( "[Escape] Discard footprint/staged primitive; idle: enter Navigation (keep selection)" ) } );
    const auto useKeymap = [&]( const char *text, const char *id ) {
        REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QString::fromUtf8( text ) ) == gui::editor_gui_status_t::OK );
        REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( id ) ) == gui::editor_gui_status_t::OK );
    };
    SECTION( "A tool-only remap replaces the old reference when both families agree" ) {
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "gesture_help" bindings = {
    "map.tool.block" = { "map.tool.confirm" = [ "V" ] "map.tool.cancel" = [ "C" ] }
} }
)cykv", "gesture_help" );
        CHECK( gestureRows() == QStringList{ QStringLiteral( "[V] Create staged primitive (one undo step)" ), QStringLiteral( "[C] Discard footprint/staged primitive; idle: enter Navigation (keep selection)" ) } );
        // A tool change must recompute the reference; these keys are Block-only.
        MapWorkspace_SetTool( &session.workspace, map_tool_t::TRANSLATE );
        CHECK( gestureRows().isEmpty() );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
        CHECK( gestureRows().size() == 2 );
    }
    SECTION( "Clip advertises its own configurable confirm and cancel gestures" ) {
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "gesture_help" bindings = {
    "map.tool.block" = { "map.tool.confirm" = [ "V" ] "map.tool.cancel" = [ "C" ] }
    "map.tool.clip" = { "map.tool.confirm" = [ "K" ] "map.tool.cancel" = [ "J" ] }
} }
)cykv", "gesture_help" );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::CLIP );
        CHECK( gestureRows() == QStringList{ QStringLiteral( "[K] Confirm current gesture" ), QStringLiteral( "[J] Cancel drag/stage; idle: enter Navigation (keep selection)" ) } );
        CHECK( Setting<QComboBox>( *panel, "editor.map.clip_mode" )->isEnabled() );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
        CHECK( gestureRows() == QStringList{ QStringLiteral( "[V] Create staged primitive (one undo step)" ), QStringLiteral( "[C] Discard footprint/staged primitive; idle: enter Navigation (keep selection)" ) } );
    }
    SECTION( "Different effective family bindings receive explicit 2D and 3D labels" ) {
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "gesture_help" bindings = {
    "map.tool.block" = { "map.tool.confirm" = [ "V" ] }
    "map.viewport.2d" = { "map.tool.confirm" = [ "F2" ] "map.tool.cancel" = [ "C" ] "future.disabled" = [ "Enter" ] }
    "map.viewport.3d" = { "map.tool.confirm" = [ "F3" ] "map.tool.cancel" = [ "D" ] "future.disabled" = [ "Escape" ] }
    "map.viewport" = { "map.tool.confirm" = [ "Enter" ] "map.tool.cancel" = [ "Escape" ] }
} }
)cykv", "gesture_help" );
        CHECK( gestureRows() == QStringList{
            QStringLiteral( "[F2 / V] Create staged primitive (one undo step) (2D)" ), QStringLiteral( "[Enter / F3 / V] Create staged primitive (one undo step) (3D)" ),
            QStringLiteral( "[C / Escape] Discard footprint/staged primitive; idle: enter Navigation (keep selection) (2D)" ), QStringLiteral( "[D] Discard footprint/staged primitive; idle: enter Navigation (keep selection) (3D)" ) } );
    }
    SECTION( "Explicit tool unbinding hides inherited chords that shared viewport bindings repeat" ) {
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "gesture_help_base" bindings = {
    "map.tool.block" = { "map.tool.confirm" = [ "Enter" ] "map.tool.cancel" = [ "Escape" ] }
    "map.viewport" = { "map.tool.confirm" = [ "Enter" ] "map.tool.cancel" = [ "Escape" ] }
    global = { "map.tool.confirm" = [ "G" ] "map.tool.cancel" = [ "H" ] }
} }
)cykv", "gesture_help_base" );
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "gesture_help" base = "gesture_help_base" bindings = {
    "map.tool.block" = { "map.tool.confirm" = [] "map.tool.cancel" = [] }
} }
)cykv", "gesture_help" );
        CHECK( gestureRows() == QStringList{ QStringLiteral( "[G] Create staged primitive (one undo step)" ), QStringLiteral( "[H] Discard footprint/staged primitive; idle: enter Navigation (keep selection)" ) } );
    }
    SECTION( "Unbound and unsupported sequence gestures produce no promised shortcut rows" ) {
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "gesture_help" bindings = {
    "map.tool.block" = { "map.tool.confirm" = [ "Ctrl+K, Ctrl+C" ] "map.tool.cancel" = [] }
} }
)cykv", "gesture_help" );
        CHECK( gestureRows().isEmpty() );
        CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Focus loss] Cancel active drag; idle stage retained" ) ) );
    }
    SECTION( "Platform overrides advertise their effective chords instead of main-section texts" ) {
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "gesture_help"
  bindings = { "map.tool.block" = { "map.tool.confirm" = [ "V" ] "map.tool.cancel" = [ "C" ] } }
  platforms = {
    macos = { bindings = { "map.tool.block" = { "map.tool.confirm" = [ "F9" ] "map.tool.cancel" = [ "F10" ] } } }
    windows = { bindings = { "map.tool.block" = { "map.tool.confirm" = [ "F9" ] "map.tool.cancel" = [ "F10" ] } } }
    linux = { bindings = { "map.tool.block" = { "map.tool.confirm" = [ "F9" ] "map.tool.cancel" = [ "F10" ] } } }
  }
}
)cykv", "gesture_help" );
        REQUIRE( EditorKeymap_HostPlatform() != keymap_platform_t::NONE );
        CHECK( gestureRows() == QStringList{ QStringLiteral( "[F9] Create staged primitive (one undo step)" ), QStringLiteral( "[F10] Discard footprint/staged primitive; idle: enter Navigation (keep selection)" ) } );
    }
}

TEST_CASE( "Nudge help distinguishes grid and fine movement in supported selection tools", "[map][gui][toolpanels][keyboard][help]" )
{
    session_t session;
    for ( const char *id : { "map.nudge.left", "map.nudge.right", "map.nudge.up", "map.nudge.down",
                            "map.nudge_fine.left", "map.nudge_fine.right", "map.nudge_fine.up", "map.nudge_fine.down" } ) {
        RegisterReferenceCommand( session, id, "Nudge" );
    }
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    const QStringList expected{
        QStringLiteral( "[Left] Nudge left by the authored grid step (2D)" ),
        QStringLiteral( "[Right] Nudge right by the authored grid step (2D)" ),
        QStringLiteral( "[Up] Nudge up by the authored grid step (2D)" ),
        QStringLiteral( "[Down] Nudge down by the authored grid step (2D)" ),
        QStringLiteral( "[Shift+Left] Nudge left by one unit (2D)" ),
        QStringLiteral( "[Shift+Right] Nudge right by one unit (2D)" ),
        QStringLiteral( "[Shift+Up] Nudge up by one unit (2D)" ),
        QStringLiteral( "[Shift+Down] Nudge down by one unit (2D)" ) };
    for ( const map_element_mode_t mode : { map_element_mode_t::OBJECTS, map_element_mode_t::GROUPS } ) {
        MapWorkspace_SetElementMode( &session.workspace, mode );
        for ( const map_tool_t tool : { map_tool_t::SELECT, map_tool_t::TRANSLATE, map_tool_t::ROTATE, map_tool_t::SCALE } ) {
            MapWorkspace_SetTool( &session.workspace, tool );
            CHECK( ReferenceRowsContaining( *panel, "Nudge " ) == expected );
        }
    }
    MapWorkspace_SetElementMode( &session.workspace, map_element_mode_t::FACES );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::SELECT );
    CHECK( ReferenceRowsContaining( *panel, "Nudge " ).isEmpty() );
    MapWorkspace_SetElementMode( &session.workspace, map_element_mode_t::OBJECTS );
    for ( const map_tool_t tool : { map_tool_t::BLOCK, map_tool_t::CAMERA } ) {
        MapWorkspace_SetTool( &session.workspace, tool );
        CHECK( ReferenceRowsContaining( *panel, "Nudge " ).isEmpty() );
    }
}

TEST_CASE( "Nudge help follows effective 2D remaps without advertising other contexts", "[map][gui][toolpanels][keyboard][help]" )
{
    session_t session;
    for ( const char *id : { "map.nudge.left", "map.nudge.right", "map.nudge.up", "map.nudge.down",
                            "map.nudge_fine.left", "map.nudge_fine.right", "map.nudge_fine.up", "map.nudge_fine.down" } ) {
        RegisterReferenceCommand( session, id, "Nudge" );
    }
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    const auto useKeymap = [&]( const char *text, const char *id ) {
        REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QString::fromUtf8( text ) ) == gui::editor_gui_status_t::OK );
        REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( id ) ) == gui::editor_gui_status_t::OK );
    };
    SECTION( "Tool shadows, unsupported sequences, and 3D-only bindings are absent" ) {
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "nudge_help" bindings = {
    "map.viewport.2d" = {
        "map.nudge.left" = [ "F9" ] "map.nudge.right" = [ "F10" ]
        "map.nudge.down" = [ "Ctrl+K, Ctrl+C" ] "map.nudge_fine.left" = [ "F12" ]
    }
    "map.tool.select" = { "future.disabled" = [ "F9" ] }
    "map.viewport.3d" = { "map.nudge.up" = [ "F8" ] }
} }
)cykv", "nudge_help" );
        CHECK( ReferenceRowsContaining( *panel, "Nudge " ) == QStringList{
            QStringLiteral( "[F10] Nudge right by the authored grid step (2D)" ),
            QStringLiteral( "[F12] Nudge left by one unit (2D)" ) } );
        // Changing the tool changes key ownership without rebuilding keymaps.
        MapWorkspace_SetTool( &session.workspace, map_tool_t::TRANSLATE );
        CHECK( ReferenceRowsContaining( *panel, "Nudge " ).contains( QStringLiteral( "[F9] Nudge left by the authored grid step (2D)" ) ) );
    }
    SECTION( "Explicit unbinding removes an inherited nudge reference" ) {
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "nudge_help_base" bindings = { "map.viewport.2d" = { "map.nudge.left" = [ "Left" ] } } }
)cykv", "nudge_help_base" );
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "nudge_help" base = "nudge_help_base" bindings = { "map.viewport.2d" = { "map.nudge.left" = [] } } }
)cykv", "nudge_help" );
        CHECK( ReferenceRowsContaining( *panel, "Nudge " ).isEmpty() );
    }
    SECTION( "Shared viewport declarations cannot bypass the object-mode help restriction" ) {
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "nudge_help" bindings = { "map.viewport" = { "map.nudge.left" = [ "F9" ] } } }
)cykv", "nudge_help" );
        CHECK( ReferenceRowsContaining( *panel, "Nudge " ) == QStringList{ QStringLiteral( "[F9] Nudge left by the authored grid step (2D)" ) } );
        MapWorkspace_SetElementMode( &session.workspace, map_element_mode_t::FACES );
        CHECK( ReferenceRowsContaining( *panel, "Nudge " ).isEmpty() );
    }
}

TEST_CASE( "Viewport command references use effective shortcuts and expose pane cycling", "[map][gui][toolpanels][keyboard][help]" )
{
    session_t session;
    RegisterReferenceCommand( session, "map.view.cycle", "Cycle Active Pane" );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    REQUIRE( ReferenceRowsContaining( *panel, "Cycle Active Pane" ) == QStringList{ QStringLiteral( "[Tab] Cycle Active Pane" ) } );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::CAMERA );
    CHECK( ReferenceRowsContaining( *panel, "Cycle Active Pane" ) == QStringList{ QStringLiteral( "[Tab] Cycle Active Pane" ) } );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::SELECT );
    const auto useKeymap = [&]( const char *text, const char *id ) {
        REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QString::fromUtf8( text ) ) == gui::editor_gui_status_t::OK );
        REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( id ) ) == gui::editor_gui_status_t::OK );
    };
    SECTION( "Shadowed and multistroke declarations do not appear as executable bindings" ) {
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "viewport_help" bindings = {
    map = { "map.tool.select" = [ "Shift+S" ] }
    "map.viewport" = { "map.view.cycle" = [ "Tab", "F9", "Ctrl+K, Ctrl+C" ] "map.select_mode.faces" = [ "3" ] }
    "map.viewport.2d" = { "future.disabled" = [ "Tab" ] }
    "map.tool.select" = { "future.disabled" = [ "3", "F9", "Shift+S" ] }
} }
)cykv", "viewport_help" );
        CHECK( ReferenceRowsContaining( *panel, "Cycle Active Pane" ) == QStringList{ QStringLiteral( "[Tab] Cycle Active Pane (3D)" ) } );
        CHECK( ReferenceRowsContaining( *panel, "Select this tool" ).isEmpty() );
        for ( const QString &row : MapToolProperties_KeyRows( panel.get() ) ) {
            CHECK_FALSE( row.startsWith( QStringLiteral( "[3]" ) ) );
        }
    }
    SECTION( "Different family remaps have explicit 2D and 3D suffixes" ) {
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "viewport_help" bindings = {
    "map.viewport" = { "map.view.cycle" = [] }
    "map.viewport.2d" = { "map.view.cycle" = [ "F2" ] }
    "map.viewport.3d" = { "map.view.cycle" = [ "F3" ] }
} }
)cykv", "viewport_help" );
        CHECK( ReferenceRowsContaining( *panel, "Cycle Active Pane" ) == QStringList{
            QStringLiteral( "[F2] Cycle Active Pane (2D)" ), QStringLiteral( "[F3] Cycle Active Pane (3D)" ) } );
    }
    SECTION( "An explicitly unbound inherited command produces no reference" ) {
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "viewport_help_base" bindings = { "map.viewport" = { "map.view.cycle" = [ "Tab" ] } } }
)cykv", "viewport_help_base" );
        useKeymap( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "viewport_help" base = "viewport_help_base" bindings = { "map.viewport" = { "map.view.cycle" = [] } } }
)cykv", "viewport_help" );
        CHECK( ReferenceRowsContaining( *panel, "Cycle Active Pane" ).isEmpty() );
    }
}

TEST_CASE( "Movement snapping controls follow root selection profiles and Translate groups", "[map][gui][toolpanels][geometry-snap][selection-profile]" )
{
    session_t session; auto &ws = session.workspace;
    MapWorkspace_SetTool( &ws, map_tool_t::SELECT ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    panel->resize( 320, 1000 ); panel->show(); QCoreApplication::processEvents();
    QPointer<QCheckBox> enabled = Setting<QCheckBox>( *panel, "editor.grid.geometry_snap" );
    QPointer<QDoubleSpinBox> distance = Setting<QDoubleSpinBox>( *panel, "editor.grid.geometry_snap_pixels" );
    auto *control = panel->findChild<QWidget *>( QStringLiteral( "editor.grid.geometry_snap" ) ); REQUIRE( control );
    QPointer<QWidget> group = control->parentWidget(); REQUIRE( group );
    QPointer<QWidget> section = group->parentWidget(); REQUIRE( section );
    CHECK( group->property( "toolGroup" ).toString() == QStringLiteral( "Movement snapping" ) );
    auto *title = section->findChild<QLabel *>( QStringLiteral( "EditorSectionTitle" ) ); REQUIRE( title );
    CHECK( title->text() == QStringLiteral( "Movement snapping" ) );
    CHECK( distance->parentWidget()->parentWidget() == group.data() );
    CHECK_FALSE( enabled->isChecked() ); CHECK( distance->value() == 8.0 );
    CHECK( distance->minimum() == 2.0 ); CHECK( distance->maximum() == 24.0 );
    const auto *document = ws.pDocument; const u64 revision = document->geometry.revision;
    const usize steps = EditorHistory_StepCount( &ws.history ); const bool modified = MapWorkspace_IsModified( &ws );
    const usize listeners = session.gui.settings.nListeners;
    for ( const auto mode : { map_element_mode_t::OBJECTS, map_element_mode_t::GROUPS, map_element_mode_t::MESHES } ) {
        CAPTURE( static_cast<int>( mode ) ); MapWorkspace_SetElementMode( &ws, mode );
        CHECK_FALSE( section->isHidden() ); CHECK( enabled->isVisibleTo( panel.get() ) ); CHECK( distance->isVisibleTo( panel.get() ) );
        for ( const char *path : { "editor.grid.size", "editor.grid.snap", "editor.grid.geometry_snap", "editor.grid.geometry_snap_pixels" } ) {
            CHECK( MapToolProperties_Options( panel.get() ).count( QString::fromLatin1( path ) ) == 1 );
            auto *field = group->findChild<QWidget *>( QString::fromLatin1( path ) ); REQUIRE( field );
            CHECK( field->parentWidget() == group.data() );
        }
        CHECK( enabled.data() == Setting<QCheckBox>( *panel, "editor.grid.geometry_snap" ) );
        CHECK( distance.data() == Setting<QDoubleSpinBox>( *panel, "editor.grid.geometry_snap_pixels" ) );
        CHECK( session.gui.settings.nListeners == listeners );
    }
    for ( const auto mode : { map_element_mode_t::VERTICES, map_element_mode_t::EDGES, map_element_mode_t::FACES } ) {
        CAPTURE( static_cast<int>( mode ) ); MapWorkspace_SetElementMode( &ws, mode );
        CHECK( section->isHidden() ); CHECK_FALSE( enabled->isVisibleTo( panel.get() ) ); CHECK_FALSE( distance->isVisibleTo( panel.get() ) );
        CHECK_FALSE( MapToolProperties_Options( panel.get() ).contains( QStringLiteral( "editor.grid.geometry_snap" ) ) );
        CHECK_FALSE( MapToolProperties_Options( panel.get() ).contains( QStringLiteral( "editor.grid.geometry_snap_pixels" ) ) );
        CHECK( session.gui.settings.nListeners == listeners );
    }
    MapWorkspace_SetTool( &ws, map_tool_t::NONE );
    CHECK( MapToolProperties_Options( panel.get() ).isEmpty() );
    CHECK( panel->findChild<QWidget *>( QStringLiteral( "editor.grid.geometry_snap" ) ) == nullptr );
    CHECK( panel->findChild<QWidget *>( QStringLiteral( "editor.grid.geometry_snap_pixels" ) ) == nullptr );
    CHECK( enabled.isNull() ); CHECK( distance.isNull() ); CHECK( group.isNull() ); CHECK( section.isNull() );

    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); MapWorkspace_SetTool( &ws, map_tool_t::TRANSLATE );
    for ( const char *path : { "editor.grid.size", "editor.grid.snap", "editor.grid.geometry_snap", "editor.grid.geometry_snap_pixels" } ) {
        CHECK( MapToolProperties_Options( panel.get() ).count( QString::fromLatin1( path ) ) == 1 );
        auto *field = panel->findChild<QWidget *>( QString::fromLatin1( path ) ); REQUIRE( field );
        CHECK( field->parentWidget()->property( "toolGroup" ).toString() == QStringLiteral( "Move steps" ) );
    }
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( MapWorkspace_IsModified( &ws ) == modified );
}

TEST_CASE( "Geometry snapping fields save the user preferences and reload without editing the map", "[map][gui][toolpanels][geometry-snap][settings]" )
{
    session_t session; auto &ws = session.workspace;
    settings_document_t project{}, workspaceSettings{};
    REQUIRE( SettingsDocument_Init( &project, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Init( &workspaceSettings, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::PROJECT, &project );
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::WORKSPACE, &workspaceSettings );
    MapWorkspace_SetTool( &ws, map_tool_t::SELECT ); MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
    MapWorkspace_Select( &ws, 1000u, MAP_SELECT_REPLACE );
    const auto *document = ws.pDocument; const u64 revision = document->geometry.revision, selectionRevision = ws.selection.revision;
    const usize steps = EditorHistory_StepCount( &ws.history ); const bool modified = MapWorkspace_IsModified( &ws );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    auto *enabled = Setting<QCheckBox>( *panel, "editor.grid.geometry_snap" );
    auto *distance = Setting<QDoubleSpinBox>( *panel, "editor.grid.geometry_snap_pixels" );
    REQUIRE_FALSE( enabled->isChecked() ); REQUIRE( distance->value() == 8.0 );
    enabled->click(); distance->setValue( 12.5 );
    CHECK( enabled->isChecked() ); CHECK( distance->value() == 12.5 );
    // The actual numeric editor enforces the registered range before saving.
    distance->setValue( -10.0 ); CHECK( distance->value() == 2.0 );
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.grid.geometry_snap_pixels", 0.0 ) == 2.0 );
    distance->setValue( 100.0 ); CHECK( distance->value() == 24.0 );
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.grid.geometry_snap_pixels", 0.0 ) == 24.0 );
    distance->setValue( 12.5 );
    for ( const char *path : { "editor.grid.geometry_snap", "editor.grid.geometry_snap_pixels" } ) {
        const auto *descriptor = EditorSettings_Find( &session.gui.settings, StringView_FromCString( path ) ); REQUIRE( descriptor );
        CHECK( EditorSettings_Resolve( &session.gui.settings, *descriptor ).source == settings_scope_t::USER );
        setting_value_t stored{};
        REQUIRE( Setting_Read( SettingsDocument_Root( &session.settings ), *descriptor, &stored, nullptr ) == setting_read_status_t::VALUE );
        if ( stored.type == setting_type_t::BOOL ) { CHECK( stored.bValue ); }
        else { CHECK( stored.flValue == 12.5 ); }
        CHECK( Setting_Read( SettingsDocument_Root( &project ), *descriptor, &stored, nullptr ) == setting_read_status_t::ABSENT );
        CHECK( Setting_Read( SettingsDocument_Root( &workspaceSettings ), *descriptor, &stored, nullptr ) == setting_read_status_t::ABSENT );
    }

    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( SettingsDocument_Write( &session.settings, &text ) == settings_document_status_t::OK );
    settings_document_t restored{};
    REQUIRE( SettingsDocument_Init( &restored, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &restored, { TextBuffer_Data( &text ), TextBuffer_Length( &text ) } ).status == settings_document_status_t::OK );
    TextBuffer_Shutdown( &text );
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, nullptr );
    CHECK_FALSE( enabled->isChecked() ); CHECK( distance->value() == 8.0 );
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, &restored );
    CHECK( enabled->isChecked() ); CHECK( distance->value() == 12.5 );
    MapWorkspace_SetTool( &ws, map_tool_t::TRANSLATE );
    CHECK( Setting<QCheckBox>( *panel, "editor.grid.geometry_snap" )->isChecked() );
    CHECK( Setting<QDoubleSpinBox>( *panel, "editor.grid.geometry_snap_pixels" )->value() == 12.5 );
    CHECK( ws.pDocument == document ); CHECK( document->geometry.revision == revision ); CHECK( ws.selection.revision == selectionRevision );
    CHECK( EditorSelection_Count( &ws.selection ) == 1u ); CHECK( EditorSelection_At( &ws.selection, 0u ) == 1000u );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( MapWorkspace_IsModified( &ws ) == modified );
    panel.reset();
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, &session.settings );
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::PROJECT, nullptr );
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::WORKSPACE, nullptr );
}

TEST_CASE( "Camera property groups write actual settings and release listeners", "[map][gui][toolpanels]" )
{
    session_t session;
    const usize listeners = session.gui.settings.nListeners;
    const usize styleListeners = session.gui.nStyleListeners;
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.tool.camera" ) ) == command_result_t::OK );
    CHECK( MapToolProperties_Title( panel.get() ) == QStringLiteral( "Camera Tool" ) );
    CHECK( MapToolProperties_Options( panel.get() ).size() > 4 );
    auto *pSpeed = Setting<QDoubleSpinBox>( *panel, "editor.camera.move_speed" );
    pSpeed->setValue( 1250.0 );
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 1250.0 );
    Setting<QCheckBox>( *panel, "editor.camera.invert_wheel" )->setChecked( true );
    CHECK( EditorSettings_Bool( &session.gui.settings, "editor.camera.invert_wheel", CY_FALSE ) );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.tool.select" ) ) == command_result_t::OK );
    CHECK( panel->findChild<QWidget *>( QStringLiteral( "editor.camera.move_speed" ) ) == nullptr );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.tool.camera" ) ) == command_result_t::OK );
    CHECK( Setting<QDoubleSpinBox>( *panel, "editor.camera.move_speed" )->value() == 1250.0 );
    panel.reset();
    CHECK( session.gui.settings.nListeners == listeners );
    CHECK( session.gui.nStyleListeners == styleListeners );
}

TEST_CASE( "Camera properties expose persisted speed controls with live availability", "[map][gui][toolpanels][camera-speed]" )
{
    session_t session; auto &ws = session.workspace;
    MapWorkspace_SetTool( &ws, map_tool_t::CAMERA );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    QPointer<QDoubleSpinBox> speed = Setting<QDoubleSpinBox>( *panel, "editor.camera.move_speed" );
    auto *increase = Operation( *panel, "map.camera.speed_increase" );
    auto *decrease = Operation( *panel, "map.camera.speed_decrease" );
    auto *reset = Operation( *panel, "map.camera.speed_reset" );
    for ( const char *id : { "map.camera.speed_increase", "map.camera.speed_decrease", "map.camera.speed_reset" } ) {
        CHECK( MapToolProperties_Operations( panel.get() ).count( QString::fromLatin1( id ) ) == 1 );
        CHECK( Operation( *panel, id )->parentWidget()->property( "toolGroup" ).toString() == QStringLiteral( "Movement" ) );
    }
    CHECK( speed->value() == 1000.0 ); CHECK( increase->isEnabled() ); CHECK( decrease->isEnabled() ); CHECK_FALSE( reset->isEnabled() );
    const auto *document = ws.pDocument; const usize steps = EditorHistory_StepCount( &ws.history );
    const bool modified = MapWorkspace_IsModified( &ws );
    increase->click();
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.camera.move_speed", 0.0 ) == 2000.0 );
    CHECK( speed->value() == 2000.0 ); CHECK( reset->isEnabled() );
    decrease->click(); CHECK( speed->value() == 1000.0 ); CHECK_FALSE( reset->isEnabled() );
    speed->setValue( 1250.0 ); CHECK( reset->isEnabled() );
    reset->click(); CHECK( speed->value() == 1000.0 ); CHECK_FALSE( reset->isEnabled() );
    speed->setValue( 100000.0 ); CHECK_FALSE( increase->isEnabled() ); CHECK( decrease->isEnabled() );
    increase->click(); CHECK( speed->value() == 100000.0 );
    speed->setValue( 10.0 ); CHECK_FALSE( decrease->isEnabled() ); CHECK( increase->isEnabled() );
    decrease->click(); CHECK( speed->value() == 10.0 );
    ws.pDocument->bReadOnly = CY_TRUE; MapWorkspace_Notify( &ws, MAP_CHANGE_DOCUMENT );
    REQUIRE( increase->isEnabled() ); increase->click(); CHECK( speed->value() == 20.0 );
    reset->click(); CHECK( speed->value() == 1000.0 );
    CHECK( speed.data() == Setting<QDoubleSpinBox>( *panel, "editor.camera.move_speed" ) );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    CHECK( MapWorkspace_IsModified( &ws ) == modified );
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, nullptr );
    CHECK_FALSE( increase->isEnabled() ); CHECK_FALSE( decrease->isEnabled() ); CHECK_FALSE( reset->isEnabled() );
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, &session.settings );
    CHECK( increase->isEnabled() ); CHECK( decrease->isEnabled() ); CHECK_FALSE( reset->isEnabled() );
}

TEST_CASE( "Camera navigation reference exposes only registered destinations and effective speed and framing keys", "[map][gui][toolpanels][camera-speed][keyboard][help]" )
{
    session_t session; auto &ws = session.workspace;
    RegisterReferenceCommand( session, "test.navigation.unrelated", "Unrelated map action" );
    MapWorkspace_SetTool( &ws, map_tool_t::CAMERA );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QStringLiteral( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "camera_navigation_reference" bindings = { "map" = {
    "map.go_to" = [ "F5" ] "map.view.frame_all" = [ "F6" ]
    "map.view.center_selection_2d" = [ "F7" ] "map.view.center_selection_3d" = [ "F8" ]
    "map.camera.speed_increase" = [ "F9" ] "map.camera.speed_decrease" = [ "F10" ] "map.camera.speed_reset" = [ "F11" ]
    "test.navigation.unrelated" = [ "F12" ]
} } }
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "camera_navigation_reference" ) ) == gui::editor_gui_status_t::OK );
    CHECK_FALSE( MapToolProperties_Operations( panel.get() ).contains( QStringLiteral( "map.go_to" ) ) );
    CHECK( ReferenceRowsContaining( *panel, "Go To" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "Unrelated map action" ).isEmpty() );
    RegisterReferenceCommand( session, "map.go_to", "Go To..." );
    MapWorkspace_SetTool( &ws, map_tool_t::SELECT ); MapWorkspace_SetTool( &ws, map_tool_t::CAMERA );
    auto *destination = Operation( *panel, "map.go_to" ); REQUIRE( destination->isEnabled() );
    CHECK( destination->parentWidget()->property( "toolGroup" ).toString() == QStringLiteral( "Navigation" ) );
    CHECK( MapToolProperties_Operations( panel.get() ).count( QStringLiteral( "map.go_to" ) ) == 1 );
    for ( const auto &[key, label] : {
              std::pair{ "F5", "Go To..." }, std::pair{ "F6", "Frame Map" },
              std::pair{ "F7", "Center 2D Views on Selection" }, std::pair{ "F8", "Center 3D View on Selection" },
              std::pair{ "F9", "Increase Camera Speed" }, std::pair{ "F10", "Decrease Camera Speed" },
              std::pair{ "F11", "Reset Camera Speed" } } ) {
        CAPTURE( key, label );
        CHECK( ReferenceRowsContaining( *panel, label ) == QStringList{ QStringLiteral( "[%1] %2" ).arg( QString::fromLatin1( key ), QString::fromLatin1( label ) ) } );
    }
    QPointer<QDoubleSpinBox> speed = Setting<QDoubleSpinBox>( *panel, "editor.camera.move_speed" );
    QPointer<QToolButton> retainedDestination = destination;
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QStringLiteral( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "camera_navigation_unbound" base = "camera_navigation_reference" bindings = { "map" = {
    "map.go_to" = [] "map.view.frame_all" = [ "Ctrl+K, Ctrl+C" ] "map.camera.speed_increase" = []
    "map.camera.speed_decrease" = [ "F4" ]
} } }
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "camera_navigation_unbound" ) ) == gui::editor_gui_status_t::OK );
    CHECK( ReferenceRowsContaining( *panel, "Go To" ).isEmpty() ); CHECK( ReferenceRowsContaining( *panel, "Frame Map" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "Increase Camera Speed" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "Decrease Camera Speed" ) == QStringList{ QStringLiteral( "[F4] Decrease Camera Speed" ) } );
    CHECK( retainedDestination.data() == Operation( *panel, "map.go_to" ) );
    CHECK( speed.data() == Setting<QDoubleSpinBox>( *panel, "editor.camera.move_speed" ) );
    MapWorkspace_SetTool( &ws, map_tool_t::NONE );
    CHECK( ReferenceRowsContaining( *panel, "Decrease Camera Speed" ) == QStringList{ QStringLiteral( "[F4] Decrease Camera Speed" ) } );
}

TEST_CASE( "Select and Block expose one persisted centered resize preference and contextual help", "[map][gui][toolpanels][centered-resize]" )
{
    session_t session;
    const usize listeners = session.gui.settings.nListeners;
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::SELECT );
    auto *center = Setting<QCheckBox>( *panel, "editor.map.resize_from_center" );
    CHECK_FALSE( center->isChecked() );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Shift+LeftDrag] Bounds handle: resize both sides around the center" ) ) );
    center->setChecked( true );
    CHECK( EditorSettings_Bool( &session.gui.settings, "editor.map.resize_from_center", CY_FALSE ) );
    for ( int repeat = 0; repeat < 3; ++repeat ) {
        MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
        CHECK( Setting<QCheckBox>( *panel, "editor.map.resize_from_center" )->isChecked() );
        CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Shift+LeftDrag] Bounds handle: resize the staged brush around its center" ) ) );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::SELECT );
        CHECK( Setting<QCheckBox>( *panel, "editor.map.resize_from_center" )->isChecked() );
    }
    panel.reset();
    CHECK( session.gui.settings.nListeners == listeners );
}

TEST_CASE( "Tool property rebuilding does not leave child observers in the workspace", "[map][gui][toolpanels][geometry-edit]" )
{
    session_t session;
    const auto listeners = session.workspace.nListeners;
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    for ( int pass = 0; pass < 3; ++pass ) {
        for ( const auto tool : { map_tool_t::BLOCK, map_tool_t::TRANSLATE, map_tool_t::ROTATE, map_tool_t::SCALE, map_tool_t::SELECT, map_tool_t::CAMERA, map_tool_t::NONE } ) {
            MapWorkspace_SetTool( &session.workspace, tool );
            CHECK( session.workspace.tool == tool );
            CHECK( session.workspace.nListeners == listeners + 1u );
            CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
        }
    }
    panel.reset();
    CHECK( session.workspace.nListeners == listeners );
}

TEST_CASE( "Tool replacement releases old settings observers before subscribing new controls", "[map][gui][toolpanels][settings][listeners]" )
{
    session_t session;
    auto &registry = session.gui.settings;
    const usize baseline = registry.nListeners;
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    const usize selectListeners = registry.nListeners - baseline;
    MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
    const usize blockListeners = registry.nListeners - baseline;
    MapWorkspace_SetTool( &session.workspace, map_tool_t::SELECT );
    REQUIRE( registry.nListeners == baseline + selectListeners );

    // Other open panes legitimately consume the remaining slots. Each tool
    // fits on its own; keeping both generations alive during replacement
    // would silently leave some replacement controls unsubscribed.
    const usize largestTool = std::max( selectListeners, blockListeners );
    REQUIRE( largestTool > 0u );
    REQUIRE( baseline + largestTool <= EDITOR_SETTINGS_MAX_LISTENERS );
    const usize reserved = EDITOR_SETTINGS_MAX_LISTENERS - baseline - largestTool;
    std::array<usize, EDITOR_SETTINGS_MAX_LISTENERS> changed{};
    const settings_listener_fn count = []( void *context, string_view_t ) noexcept { ++*static_cast<usize *>( context ); };
    for ( usize i = 0u; i < reserved; ++i ) { REQUIRE( EditorSettings_AddListener( &registry, count, &changed[i] ) ); }
    const auto *shape = EditorSettings_Find( &registry, StringView_FromCString( "editor.map.new_brush_shape" ) );
    REQUIRE( shape != nullptr );
    for ( int pass = 0; pass < 4; ++pass ) {
        CAPTURE( pass );
        MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
        CHECK( registry.nListeners == baseline + reserved + blockListeners );
        const char *name = pass % 2 == 0 ? "cylinder" : "wedge";
        setting_value_t value{}; value.type = setting_type_t::ENUM; value.text = StringView_FromCString( name );
        REQUIRE( EditorSettings_Write( &registry, settings_scope_t::USER, *shape, value ) == settings_registry_status_t::OK );
        CHECK( ShapeButton( *panel, name )->isChecked() ); // External writes still reach every replacement.
        for ( usize i = 0u; i < reserved; ++i ) { CHECK( changed[i] >= static_cast<usize>( pass + 1 ) ); }
        MapWorkspace_SetTool( &session.workspace, map_tool_t::SELECT );
        CHECK( registry.nListeners == baseline + reserved + selectListeners );
    }
    panel.reset();
    CHECK( registry.nListeners == baseline + reserved );
    for ( usize i = 0u; i < reserved; ++i ) { EditorSettings_RemoveListener( &registry, count, &changed[i] ); }
    CHECK( registry.nListeners == baseline );
}

TEST_CASE( "A settings notification can replace the tool without calling destroyed controls", "[map][gui][toolpanels][settings][listeners]" )
{
    session_t session;
    auto &registry = session.gui.settings;
    const usize baseline = registry.nListeners;
    // Subscribe before the controls so replacement happens while their old
    // observer entries remain later in the registry's notification snapshot.
    const settings_listener_fn replace = []( void *context, string_view_t path ) noexcept {
        if ( StringView_Equals( path, StringView_FromCString( "editor.map.new_brush_shape" ) ) ) {
            MapWorkspace_SetTool( static_cast<map_workspace_t *>( context ), map_tool_t::BLOCK );
        }
    };
    REQUIRE( EditorSettings_AddListener( &registry, replace, &session.workspace ) );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    const auto *shape = EditorSettings_Find( &registry, StringView_FromCString( "editor.map.new_brush_shape" ) );
    REQUIRE( shape != nullptr );
    for ( const char *name : { "cylinder", "wedge" } ) {
        setting_value_t value{}; value.type = setting_type_t::ENUM; value.text = StringView_FromCString( name );
        REQUIRE( EditorSettings_Write( &registry, settings_scope_t::USER, *shape, value ) == settings_registry_status_t::OK );
        CHECK( session.workspace.tool == map_tool_t::BLOCK );
        CHECK( ShapeButton( *panel, name )->isChecked() );
        // The test's replacement observer and the panel's operation-state
        // observer accompany one observer per setting control.
        CHECK( registry.nListeners == baseline + 2u + static_cast<usize>( MapToolProperties_Options( panel.get() ).size() ) );
    }
    panel.reset();
    CHECK( registry.nListeners == baseline + 1u );
    EditorSettings_RemoveListener( &registry, replace, &session.workspace );
    CHECK( registry.nListeners == baseline );
}

TEST_CASE( "Texture properties expose material assignment while keeping unwired UV commands disabled", "[map][gui][toolpanels]" )
{
    session_t session;
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.tool.texture" ) ) == command_result_t::OK );
    InspectPlannedTool( session, map_tool_t::TEXTURE );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    CHECK( MapToolProperties_Title( panel.get() ) == QStringLiteral( "Texture Application" ) );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolUnavailable" ) ) == nullptr );
    MapWorkspace_Select( &session.workspace, 1000u, MAP_SELECT_REPLACE );
    for ( const char *pId : { "map.tool.apply_material", "map.texture.fit", "map.texture.align_world", "map.texture.align_face",
                            "map.texture.justify_left", "map.texture.justify_right", "map.texture.justify_top", "map.texture.justify_bottom",
                            "map.texture.justify_center", "map.texture.rotate", "map.texture.scale", "map.texture.shift", "map.texture.unwrap", "map.texture.replace" } ) {
        auto *pButton = Operation( *panel, pId );
        CHECK_FALSE( pButton->isEnabled() );
        CHECK( pButton->toolTip().contains( QString::fromUtf8( pId ) ) );
        CHECK_FALSE( pButton->icon().isNull() );
        CHECK( MapToolProperties_Operations( panel.get() ).contains( QString::fromUtf8( pId ) ) );
    }
    Setting<QDoubleSpinBox>( *panel, "editor.map.default_texture_scale" )->setValue( 0.5 );
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.map.default_texture_scale", 0.0 ) == 0.5 );
    Setting<QCheckBox>( *panel, "editor.map.texture_lock" )->setChecked( false );
    CHECK_FALSE( session.workspace.bTextureLock );
    CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) ); // These are defaults, not face transforms.
}

TEST_CASE( "Primitive properties expose registered shape and brush construction settings", "[map][gui][toolpanels]" )
{
    session_t session;
    InspectPlannedTool( session, map_tool_t::BLOCK );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    ShapeButton( *panel, "cylinder" )->click();
    CHECK( StringView_Equals( EditorSettings_Text( &session.gui.settings, "editor.map.new_brush_shape", {} ), StringView_FromCString( "cylinder" ) ) );
    Setting<QSpinBox>( *panel, "editor.map.cylinder_sides" )->setValue( 24 );
    CHECK( EditorSettings_Integer( &session.gui.settings, "editor.map.cylinder_sides", 0 ) == 24 );
    Setting<QDoubleSpinBox>( *panel, "editor.map.hollow_thickness" )->setValue( 32.0 );
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.map.hollow_thickness", 0.0 ) == 32.0 );
    CHECK( MapToolProperties_Options( panel.get() ).size() > 4 );
    CHECK_FALSE( Operation( *panel, "map.brush.hollow" )->isEnabled() );
    CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
}

TEST_CASE( "Numeric block controls create canonical geometry in one undo step", "[map][gui][toolpanels][geometry-edit]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    auto *pBox = panel->findChild<QWidget *>( QStringLiteral( "MapBoxControls" ) );
    REQUIRE( pBox != nullptr );
    const auto set = [&]( const char *pName, double value ) {
        auto *pField = pBox->findChild<QDoubleSpinBox *>( QString::fromUtf8( pName ) );
        REQUIRE( pField != nullptr );
        pField->setValue( value );
    };
    set( "MapBoxCenterX", 160.0 ); set( "MapBoxCenterY", -32.0 ); set( "MapBoxCenterZ", 24.0 );
    set( "MapBoxSizeX", 64.0 ); set( "MapBoxSizeY", 32.0 ); set( "MapBoxSizeZ", 16.0 );
    auto *pCreate = pBox->findChild<QPushButton *>( QStringLiteral( "MapNumericApply" ) );
    REQUIRE( pCreate != nullptr );
    REQUIRE( pCreate->isEnabled() );
    pCreate->click();
    REQUIRE( session.workspace.wire.objects.nCount == 1u );
    const map_bounds_t bounds = session.workspace.wire.objects.pData[0].bounds;
    CHECK( bounds.bHas );
    CHECK( bounds.box.minimum.x == 128.0 ); CHECK( bounds.box.maximum.x == 192.0 );
    CHECK( bounds.box.minimum.y == -48.0 ); CHECK( bounds.box.maximum.y == -16.0 );
    CHECK( bounds.box.minimum.z == 16.0 ); CHECK( bounds.box.maximum.z == 32.0 );
    CHECK( MapWorkspace_IsModified( &session.workspace ) );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( session.workspace.wire.objects.nCount == 0u );
    CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
    REQUIRE( MapWorkspace_Redo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( session.workspace.wire.objects.nCount == 1u );
}

TEST_CASE( "Block properties edit the captured construction preview before one confirmed publication", "[map][gui][toolpanels][staged-block][geometry-edit]" )
{
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    auto *controls = panel->findChild<QWidget *>( QStringLiteral( "MapBoxControls" ) ); REQUIRE( controls != nullptr );
    const auto field = [&]( const char *name ) {
        auto *value = controls->findChild<QDoubleSpinBox *>( QString::fromUtf8( name ) ); REQUIRE( value != nullptr ); return value;
    };
    const auto *document = ws.pDocument;
    map_bounds_t bounds{}; MapBounds_AddPoint( bounds, { 10.123456, -32, 0 } ); MapBounds_AddPoint( bounds, { 74.765432, 32, 64 } );
    MapWorkspace_SetEditPreview( &ws, bounds ); REQUIRE( ws.editPreview.status == map_status_t::OK );
    REQUIRE( MapWorkspace_StageBlockPreview( &ws ) );
    REQUIRE( MapWorkspace_HasBlockPreview( &ws ) );
    CHECK( std::abs( field( "MapBoxCenterX" )->value() - 42.444 ) < 1e-6 );
    CHECK( field( "MapBoxSizeY" )->value() == 64 ); CHECK( field( "MapBoxSizeZ" )->value() == 64 );
    field( "MapBoxSizeZ" )->setValue( 128 );
    CHECK( ws.editPreview.bounds.box.minimum.x == bounds.box.minimum.x );
    CHECK( ws.editPreview.bounds.box.maximum.x == bounds.box.maximum.x );
    CHECK( ws.editPreview.bounds.box.minimum.z == -32 ); CHECK( ws.editPreview.bounds.box.maximum.z == 96 );
    field( "MapBoxCenterY" )->setValue( 96 );
    CHECK( ws.editPreview.bounds.box.minimum.y == 64 ); CHECK( ws.editPreview.bounds.box.maximum.y == 128 );
    CHECK( ws.pDocument == document ); CHECK( EditorHistory_StepCount( &ws.history ) == 0 ); CHECK_FALSE( MapWorkspace_IsModified( &ws ) );
    // Changing defaults belongs to the next footprint; the staged descriptor
    // and its numeric confirmation must still create the displayed box.
    ShapeButton( *panel, "cylinder" )->click();
    CHECK( ws.editPreview.primitive.kind == map_primitive_kind_t::BOX );
    const auto shown = ws.editPreview.bounds;
    auto *apply = controls->findChild<QPushButton *>( QStringLiteral( "MapNumericApply" ) ); REQUIRE( apply != nullptr );
    CHECK( apply->text() == QStringLiteral( "Create Staged Primitive" ) ); apply->click();
    REQUIRE_FALSE( ws.editPreview.bActive ); REQUIRE( ws.pDocument->geometry.brushes.nCount == 1 );
    CHECK( ws.pDocument->geometry.brushes.pData[0]->sides.nCount == 6 ); CHECK( EditorHistory_StepCount( &ws.history ) == 1 );
    REQUIRE( ws.wire.objects.nCount == 1 );
    CHECK( std::abs( ws.wire.objects.pData[0].bounds.box.minimum.x - shown.box.minimum.x ) < 1e-6 );
    CHECK( ws.wire.objects.pData[0].bounds.box.maximum.z == shown.box.maximum.z );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ws.wire.objects.nCount == 0 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ws.wire.objects.nCount == 1 );
}

TEST_CASE( "Numeric creation uses the shape and construction options edited in tool properties", "[map][gui][toolpanels][geometry-edit][primitive]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    panel->resize( 340, 900 ); panel->show(); QCoreApplication::processEvents();
    for ( const char *name : { "box", "quad", "wedge", "cylinder", "spike", "sphere" } ) { REQUIRE( !ShapeButton( *panel, name )->icon().isNull() ); }
    ShapeButton( *panel, "cylinder" )->click();
    Setting<QComboBox>( *panel, "editor.map.primitive_axis" )->setCurrentText( QStringLiteral( "X" ) );
    Setting<QSpinBox>( *panel, "editor.map.cylinder_sides" )->setValue( 12 );
    Setting<QDoubleSpinBox>( *panel, "editor.map.default_texture_scale" )->setValue( 0.5 );
    CHECK( StringView_Equals( EditorSettings_Text( &session.gui.settings, "editor.map.new_brush_shape", {} ), StringView_FromCString( "cylinder" ) ) );
    CHECK( StringView_Equals( EditorSettings_Text( &session.gui.settings, "editor.map.primitive_axis", {} ), StringView_FromCString( "X" ) ) );
    CHECK( EditorSettings_Integer( &session.gui.settings, "editor.map.cylinder_sides", 0 ) == 12 );
    CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
    auto *controls = panel->findChild<QWidget *>( QStringLiteral( "MapBoxControls" ) );
    REQUIRE( controls != nullptr );
    const auto set = [&]( const char *name, double value ) {
        auto *field = controls->findChild<QDoubleSpinBox *>( QString::fromUtf8( name ) );
        REQUIRE( field != nullptr ); field->setValue( value );
    };
    set( "MapBoxCenterX", 160 ); set( "MapBoxCenterY", -32 ); set( "MapBoxCenterZ", 24 );
    set( "MapBoxSizeX", 64 ); set( "MapBoxSizeY", 32 ); set( "MapBoxSizeZ", 16 );
    auto *create = controls->findChild<QPushButton *>( QStringLiteral( "MapNumericApply" ) );
    REQUIRE( create != nullptr ); REQUIRE( create->isEnabled() );
    CHECK( create->text() == QStringLiteral( "Create Brush" ) );
    create->click();
    REQUIRE( session.workspace.pDocument->geometry.brushes.nCount == 1 );
    REQUIRE( session.workspace.selection.ids.nCount == 1 );
    const u64 id = session.workspace.selection.ids.pData[0];
    const auto *brush = geometry::GeometryDocument_FindBrush( &session.workspace.pDocument->geometry, { id } );
    REQUIRE( brush != nullptr );
    CHECK( brush->sides.nCount == 14 ); // Twelve wall planes and two caps.
    const auto *object = MapWireframe_FindObject( session.workspace.wire, id );
    REQUIRE( object != nullptr );
    CHECK( std::abs( object->bounds.box.minimum.x - 128 ) < 1e-6 );
    CHECK( std::abs( object->bounds.box.maximum.x - 192 ) < 1e-6 );
    CHECK( std::abs( object->bounds.box.minimum.y + 48 ) < 1e-6 );
    CHECK( std::abs( object->bounds.box.maximum.z - 32 ) < 1e-6 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 0 );
    CHECK_FALSE( MapWorkspace_IsModified( &session.workspace ) );
    REQUIRE( MapWorkspace_Redo( &session.workspace ) == editor_history_status_t::OK );
    REQUIRE( session.workspace.selection.ids.nCount == 1 );
    CHECK( session.workspace.selection.ids.pData[0] == id );
    brush = geometry::GeometryDocument_FindBrush( &session.workspace.pDocument->geometry, { id } );
    REQUIRE( brush != nullptr ); CHECK( brush->sides.nCount == 14 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 1 );
}

TEST_CASE( "Brush subtraction requires an explicit visible cutter choice and applies one undoable cut", "[map][gui][toolpanels][geometry-edit][subtract]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t outer{}, cutterBounds{}, hiddenBounds{};
    MapBounds_AddPoint( outer, { 0, 0, 0 } ); MapBounds_AddPoint( outer, { 128, 128, 128 } );
    MapBounds_AddPoint( cutterBounds, { 48, -32, -32 } ); MapBounds_AddPoint( cutterBounds, { 80, 160, 160 } );
    MapBounds_AddPoint( hiddenBounds, { 256, 0, 0 } ); MapBounds_AddPoint( hiddenBounds, { 320, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, outer ) ); const u64 target = session.workspace.selection.ids.pData[0];
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, cutterBounds ) ); const u64 cutter = session.workspace.selection.ids.pData[0];
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, hiddenBounds ) ); const u64 hidden = session.workspace.selection.ids.pData[0];
    REQUIRE( EditorSelection_Apply( &session.workspace.hidden, hidden, EDITOR_SELECT_ADD ) );
    MapWorkspace_Select( &session.workspace, target, MAP_SELECT_REPLACE );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::SELECT );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    auto *choices = panel->findChild<QComboBox *>( QStringLiteral( "MapSubtractCutter" ) );
    auto *apply = panel->findChild<QPushButton *>( QStringLiteral( "MapSubtractApply" ) );
    REQUIRE( choices != nullptr ); REQUIRE( apply != nullptr );
    CHECK( choices->currentData().toULongLong() == 0 ); CHECK_FALSE( apply->isEnabled() );
    CHECK( choices->count() == 2 ); // Placeholder and the unselected visible cutter.
    CHECK( choices->findData( QVariant::fromValue<qulonglong>( target ) ) == -1 );
    CHECK( choices->findData( QVariant::fromValue<qulonglong>( hidden ) ) == -1 );
    const int index = choices->findData( QVariant::fromValue<qulonglong>( cutter ) ); REQUIRE( index > 0 );
    choices->setCurrentIndex( index ); REQUIRE( apply->isEnabled() );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 3 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 3 );
    apply->click();
    REQUIRE( session.workspace.pDocument->geometry.brushes.nCount == 4 );
    REQUIRE( session.workspace.selection.ids.nCount == 2 );
    CHECK( geometry::GeometryDocument_FindBrush( &session.workspace.pDocument->geometry, { target } ) == nullptr );
    CHECK( geometry::GeometryDocument_FindBrush( &session.workspace.pDocument->geometry, { cutter } ) != nullptr );
    CHECK_FALSE( EditorSelection_Contains( &session.workspace.selection, cutter ) );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == 4 );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( session.workspace.pDocument->geometry.brushes.nCount == 3 );
    REQUIRE( session.workspace.selection.ids.nCount == 1 ); CHECK( session.workspace.selection.ids.pData[0] == target );
    CHECK( geometry::GeometryDocument_FindBrush( &session.workspace.pDocument->geometry, { target } ) != nullptr );
    CHECK( choices->currentData().toULongLong() == cutter );
    CHECK( apply->isEnabled() );
}

TEST_CASE( "The subtraction cutter picker retains its model during previews and rejects stale choices", "[map][gui][toolpanels][geometry-edit][subtract][cache]" )
{
    session_t session;
    auto &ws = session.workspace;
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t targetBounds{}, cutterBounds{}, otherBounds{};
    MapBounds_AddPoint( targetBounds, { 0, 0, 0 } ); MapBounds_AddPoint( targetBounds, { 128, 128, 128 } );
    MapBounds_AddPoint( cutterBounds, { 48, -32, -32 } ); MapBounds_AddPoint( cutterBounds, { 80, 160, 160 } );
    MapBounds_AddPoint( otherBounds, { 256, 0, 0 } ); MapBounds_AddPoint( otherBounds, { 320, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, targetBounds ) ); const u64 target = ws.selection.ids.pData[0];
    REQUIRE( MapWorkspace_CreateBox( &ws, cutterBounds ) ); const u64 cutter = ws.selection.ids.pData[0];
    REQUIRE( MapWorkspace_CreateBox( &ws, otherBounds ) );
    MapWorkspace_Select( &ws, target, MAP_SELECT_REPLACE );
    MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
    int inserted = 0, removed = 0;
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    auto *choices = panel->findChild<QComboBox *>( QStringLiteral( "MapSubtractCutter" ) );
    auto *apply = panel->findChild<QPushButton *>( QStringLiteral( "MapSubtractApply" ) );
    REQUIRE( choices != nullptr ); REQUIRE( apply != nullptr );
    REQUIRE( choices->count() == 3 ); // Placeholder and two visible, unselected brushes.
    QObject::connect( choices->model(), &QAbstractItemModel::rowsInserted, panel.get(),
                      [&]( const QModelIndex &, int, int ) { ++inserted; } );
    QObject::connect( choices->model(), &QAbstractItemModel::rowsRemoved, panel.get(),
                      [&]( const QModelIndex &, int, int ) { ++removed; } );
    const auto choose = [&]() {
        const int index = choices->findData( QVariant::fromValue<qulonglong>( cutter ) );
        REQUIRE( index > 0 ); choices->setCurrentIndex( index ); REQUIRE( apply->isEnabled() );
    };
    const auto rebuilt = [&]() {
        CHECK( inserted > 0 ); CHECK( removed > 0 ); inserted = removed = 0;
    };
    const auto rejected = [&]() {
        CHECK( choices->findData( QVariant::fromValue<qulonglong>( cutter ) ) == -1 );
        CHECK( choices->currentData().toULongLong() == 0 ); CHECK_FALSE( apply->isEnabled() );
    };
    choose();

    SECTION( "Construction previews and unrelated view notifications never replace candidate rows" ) {
        const QPersistentModelIndex chosen( choices->model()->index( choices->currentIndex(), choices->modelColumn() ) );
        for ( int offset : { 0, 16, 32, 32 } ) {
            map_bounds_t preview{};
            MapBounds_AddPoint( preview, { 512.0 + offset, 0, 0 } );
            MapBounds_AddPoint( preview, { 576.0 + offset, 64, 64 } );
            MapWorkspace_SetEditPreview( &ws, preview );
            REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.status == map_status_t::OK );
            MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW );
        }
        MapWorkspace_ClearEditPreview( &ws );
        MapWorkspace_SetGridSize( &ws, 16 );
        MapWorkspace_SetTextureLock( &ws, CY_FALSE );
        CHECK( inserted == 0 ); CHECK( removed == 0 );
        CHECK( chosen.isValid() ); CHECK( choices->count() == 3 );
        CHECK( choices->currentData().toULongLong() == cutter ); CHECK( apply->isEnabled() );
    }
    SECTION( "Hidden-object changes remove and restore the cutter" ) {
        REQUIRE( EditorSelection_Apply( &ws.hidden, cutter, EDITOR_SELECT_ADD ) );
        MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW );
        rebuilt(); rejected();
        REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.hide.show_all" ) ) == command_result_t::OK );
        rebuilt(); choose();
    }
    SECTION( "Selecting the cutter makes it ineligible until removed from the targets" ) {
        MapWorkspace_Select( &ws, cutter, MAP_SELECT_ADD );
        rebuilt(); rejected();
        MapWorkspace_Select( &ws, cutter, MAP_SELECT_REMOVE );
        rebuilt(); choose();
    }
    SECTION( "Visibility groups remove and restore brush candidates" ) {
        MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::BRUSHES, CY_TRUE );
        rebuilt(); rejected(); CHECK( choices->count() == 1 );
        MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::BRUSHES, CY_FALSE );
        rebuilt(); choose();
    }
    SECTION( "Cordon bounds and activation invalidate candidate visibility independently" ) {
        map_bounds_t restricted{};
        MapBounds_AddPoint( restricted, { 0, 0, 0 } ); MapBounds_AddPoint( restricted, { 32, 128, 128 } );
        MapWorkspace_SetCordon( &ws, restricted );
        rebuilt(); rejected();
        MapWorkspace_SetCordonActive( &ws, CY_FALSE );
        rebuilt(); choose();
        MapWorkspace_SetCordonActive( &ws, CY_TRUE );
        rebuilt(); rejected();
        map_bounds_t expanded{};
        MapBounds_AddPoint( expanded, { -64, -64, -64 } ); MapBounds_AddPoint( expanded, { 384, 192, 192 } );
        MapWorkspace_SetCordon( &ws, expanded );
        rebuilt(); choose();
    }
    SECTION( "In-place document edits invalidate the model even when the cache identities are unchanged" ) {
        const auto *document = ws.pDocument;
        const auto selectionRevision = ws.selection.revision;
        REQUIRE( MapDocument_RemoveObject( ws.pDocument, cutter ) == map_status_t::OK );
        MapWorkspace_DocumentChanged( &ws );
        CHECK( ws.pDocument == document ); CHECK( ws.selection.revision == selectionRevision );
        rebuilt(); rejected();
    }
    SECTION( "Adopting another document discards its predecessor's cutter" ) {
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        rebuilt(); rejected(); CHECK( choices->count() == 1 );
    }
    SECTION( "Read-only state refreshes apply eligibility without replacing the candidate model" ) {
        ws.pDocument->bReadOnly = CY_TRUE;
        MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW );
        CHECK_FALSE( apply->isEnabled() ); CHECK( choices->currentData().toULongLong() == cutter );
        ws.pDocument->bReadOnly = CY_FALSE;
        MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW );
        CHECK( apply->isEnabled() ); CHECK( inserted == 0 ); CHECK( removed == 0 );
    }
}

TEST_CASE( "Modeless numeric transforms track selection and clone without mutating unsupported objects", "[map][gui][toolpanels][geometry-edit]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { -32.0, -16.0, -8.0 } );
    MapBounds_AddPoint( box, { 32.0, 16.0, 8.0 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    REQUIRE( session.workspace.wire.objects.nCount == 1u );
    MapWorkspace_Select( &session.workspace, session.workspace.wire.objects.pData[0].id, MAP_SELECT_REPLACE );
    std::unique_ptr<QDialog> dialog( MapTransformDialog_Create( nullptr, &session.workspace ) );
    REQUIRE( dialog != nullptr );
    CHECK_FALSE( dialog->isModal() );
    auto *pMove = dialog->findChild<QWidget *>( QStringLiteral( "MapMoveControls" ) );
    REQUIRE( pMove != nullptr );
    auto *pX = pMove->findChild<QDoubleSpinBox *>( QStringLiteral( "MapMoveX" ) );
    auto *pClone = pMove->findChild<QPushButton *>( QStringLiteral( "MapNumericClone" ) );
    REQUIRE( pX != nullptr ); REQUIRE( pClone != nullptr );
    REQUIRE( pClone->isEnabled() );
    pX->setValue( 128.0 );
    pClone->click();
    REQUIRE( session.workspace.wire.objects.nCount == 2u );
    REQUIRE( EditorSelection_Count( &session.workspace.selection ) == 1u );
    CHECK( session.workspace.wire.objects.pData[1].bounds.box.minimum.x == 96.0 );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( session.workspace.wire.objects.nCount == 1u );
    MapWorkspace_Select( &session.workspace, 0u, MAP_SELECT_REPLACE );
    CHECK_FALSE( pClone->isEnabled() );
    CHECK_FALSE( pMove->findChild<QPushButton *>( QStringLiteral( "MapNumericApply" ) )->isEnabled() );
    pClone->click();
    CHECK( session.workspace.wire.objects.nCount == 1u );
    CHECK( dialog->findChild<QWidget *>( QStringLiteral( "MapMoveControls" ) ) == pMove );
}

TEST_CASE( "Numeric transforms cannot reinterpret a selected face as a whole brush or mesh edit", "[map][gui][toolpanels][geometry-edit][component-transform-safety]" )
{
    for ( const bool mesh : { false, true } ) {
        CAPTURE( mesh );
        session_t session; auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{};
        MapBounds_AddPoint( box, { -32.0, -16.0, -8.0 } ); MapBounds_AddPoint( box, { 32.0, 16.0, 8.0 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
        const u64 object = EditorSelection_At( &ws.selection, 0 );
        if ( mesh ) { REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) ); }
        std::unique_ptr<QDialog> dialog( MapTransformDialog_Create( nullptr, &ws ) );
        REQUIRE( dialog != nullptr );
        auto *move = dialog->findChild<QWidget *>( QStringLiteral( "MapMoveControls" ) );
        auto *rotate = dialog->findChild<QWidget *>( QStringLiteral( "MapRotateControls" ) );
        auto *scale = dialog->findChild<QWidget *>( QStringLiteral( "MapScaleControls" ) );
        REQUIRE( move != nullptr ); REQUIRE( rotate != nullptr ); REQUIRE( scale != nullptr );
        auto *moveX = move->findChild<QDoubleSpinBox *>( QStringLiteral( "MapMoveX" ) );
        auto *rotateZ = rotate->findChild<QDoubleSpinBox *>( QStringLiteral( "MapRotateZ" ) );
        auto *scaleX = scale->findChild<QDoubleSpinBox *>( QStringLiteral( "MapScaleX" ) );
        REQUIRE( moveX != nullptr ); REQUIRE( rotateZ != nullptr ); REQUIRE( scaleX != nullptr );
        moveX->setValue( 32.0 ); rotateZ->setValue( 45.0 ); scaleX->setValue( 2.0 );
        QPushButton *buttons[]{ move->findChild<QPushButton *>( QStringLiteral( "MapNumericApply" ) ),
                               move->findChild<QPushButton *>( QStringLiteral( "MapNumericClone" ) ),
                               rotate->findChild<QPushButton *>( QStringLiteral( "MapNumericApply" ) ),
                               scale->findChild<QPushButton *>( QStringLiteral( "MapNumericApply" ) ) };
        for ( const auto *button : buttons ) { REQUIRE( button != nullptr ); REQUIRE( button->isEnabled() ); }
        u64 face = 0;
        for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) {
            const auto &candidate = ws.wire.faces.pData[i];
            if ( candidate.id == object && candidate.normal.z > 0.99 ) { face = mesh ? candidate.faceId : candidate.sideId; }
        }
        REQUIRE( face != 0 );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
        if ( mesh ) { MapWorkspace_SelectMeshFace( &ws, object, face ); }
        else { MapWorkspace_SelectBrushFace( &ws, object, face ); }
        REQUIRE( ws.elementMode == map_element_mode_t::FACES );
        REQUIRE( ( mesh ? MapWorkspace_HasMeshFace( &ws ) : MapWorkspace_HasBrushFace( &ws ) ) );
        const auto *document = ws.pDocument;
        const auto revision = document->geometry.revision;
        const auto nextId = document->nextId;
        const auto selectionRevision = ws.selection.revision;
        const usize steps = EditorHistory_StepCount( &ws.history );
        const auto *root = MapWireframe_FindObject( ws.wire, object );
        REQUIRE( root != nullptr ); REQUIRE( root->nPoints == 8u );
        const auto originalBounds = root->bounds;
        std::array<math::vec3d_t, 8> originalPoints{};
        std::array<u64, 8> originalVertexIds{};
        for ( u32 point = 0; point < 8; ++point ) {
            originalPoints[point] = ws.wire.points.pData[root->iFirstPoint + point];
            originalVertexIds[point] = ws.wire.pointSourceIds.pData[root->iFirstPoint + point];
        }
        const auto checkUnchanged = [&]() {
            REQUIRE( ws.pDocument != nullptr ); CHECK( ws.pDocument == document );
            CHECK( ws.pDocument->geometry.revision == revision ); CHECK( ws.pDocument->nextId == nextId );
            CHECK( EditorHistory_StepCount( &ws.history ) == steps );
            CHECK( ws.selection.revision == selectionRevision ); CHECK( EditorSelection_Count( &ws.selection ) == 1u );
            CHECK( EditorSelection_At( &ws.selection, 0 ) == object );
            CHECK( ( mesh ? ws.selectedMeshFaceId == face : ws.selectedBrushFaceSide == face ) );
            CHECK( ws.wire.objects.nCount == 1u );
            const auto *retained = MapWireframe_FindObject( ws.wire, object ); REQUIRE( retained != nullptr ); REQUIRE( retained->nPoints == 8u );
            CHECK( retained->bounds.box.minimum.x == originalBounds.box.minimum.x );
            CHECK( retained->bounds.box.minimum.y == originalBounds.box.minimum.y );
            CHECK( retained->bounds.box.minimum.z == originalBounds.box.minimum.z );
            CHECK( retained->bounds.box.maximum.x == originalBounds.box.maximum.x );
            CHECK( retained->bounds.box.maximum.y == originalBounds.box.maximum.y );
            CHECK( retained->bounds.box.maximum.z == originalBounds.box.maximum.z );
            for ( u32 point = 0; point < 8; ++point ) {
                const auto current = ws.wire.points.pData[retained->iFirstPoint + point];
                CHECK( current.x == originalPoints[point].x ); CHECK( current.y == originalPoints[point].y ); CHECK( current.z == originalPoints[point].z );
                CHECK( ws.wire.pointSourceIds.pData[retained->iFirstPoint + point] == originalVertexIds[point] );
            }
            CHECK_FALSE( ws.editPreview.bActive );
        };
        for ( auto *button : buttons ) {
            CHECK_FALSE( button->isEnabled() );
            button->click();
            // A disabled QPushButton suppresses native clicks. Invoke its
            // signal directly as a stale/programmatic caller to require the
            // Apply handler to check the current component mode too.
            REQUIRE( QMetaObject::invokeMethod( button, "clicked", Qt::DirectConnection, Q_ARG( bool, false ) ) );
            checkUnchanged();
        }
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS );
        CHECK( dialog->findChild<QWidget *>( QStringLiteral( "MapMoveControls" ) ) == move );
        CHECK( dialog->findChild<QWidget *>( QStringLiteral( "MapRotateControls" ) ) == rotate );
        CHECK( dialog->findChild<QWidget *>( QStringLiteral( "MapScaleControls" ) ) == scale );
        for ( const auto *button : buttons ) { CHECK( button->isEnabled() ); }
        CHECK( moveX->value() == 32.0 ); CHECK( rotateZ->value() == 45.0 ); CHECK( scaleX->value() == 2.0 );
        buttons[0]->click();
        CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
        const auto *moved = MapWireframe_FindObject( ws.wire, object ); REQUIRE( moved != nullptr );
        CHECK( moved->bounds.box.minimum.x == originalBounds.box.minimum.x + 32.0 );
        CHECK( moved->bounds.box.maximum.x == originalBounds.box.maximum.x + 32.0 );
        CHECK( moved->bounds.box.minimum.y == originalBounds.box.minimum.y ); CHECK( moved->bounds.box.minimum.z == originalBounds.box.minimum.z );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        const auto *restored = MapWireframe_FindObject( ws.wire, object ); REQUIRE( restored != nullptr ); REQUIRE( restored->nPoints == 8u );
        for ( u32 point = 0; point < 8; ++point ) {
            const auto current = ws.wire.points.pData[restored->iFirstPoint + point];
            CHECK( current.x == originalPoints[point].x ); CHECK( current.y == originalPoints[point].y ); CHECK( current.z == originalPoints[point].z );
            CHECK( ws.wire.pointSourceIds.pData[restored->iFirstPoint + point] == originalVertexIds[point] );
        }
        CHECK( EditorSelection_At( &ws.selection, 0 ) == object );
    }
}

TEST_CASE( "Numeric rotation and scale apply about the chosen pivot", "[map][gui][toolpanels][geometry-edit]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { 96.0, -16.0, -8.0 } );
    MapBounds_AddPoint( box, { 160.0, 16.0, 8.0 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    MapWorkspace_Select( &session.workspace, session.workspace.wire.objects.pData[0].id, MAP_SELECT_REPLACE );
    std::unique_ptr<QDialog> dialog( MapTransformDialog_Create( nullptr, &session.workspace ) );
    auto *pRotate = dialog->findChild<QWidget *>( QStringLiteral( "MapRotateControls" ) );
    REQUIRE( pRotate != nullptr );
    pRotate->findChild<QDoubleSpinBox *>( QStringLiteral( "MapRotateZ" ) )->setValue( 90.0 );
    pRotate->findChild<QPushButton *>( QStringLiteral( "MapNumericApply" ) )->click();
    REQUIRE( session.workspace.wire.objects.nCount == 1u );
    auto bounds = session.workspace.wire.objects.pData[0].bounds.box;
    CHECK( std::abs( bounds.minimum.x - 112.0 ) < 1e-7 );
    CHECK( std::abs( bounds.maximum.x - 144.0 ) < 1e-7 );
    CHECK( std::abs( bounds.minimum.y + 32.0 ) < 1e-7 );
    CHECK( std::abs( bounds.maximum.y - 32.0 ) < 1e-7 );
    auto *pScale = dialog->findChild<QWidget *>( QStringLiteral( "MapScaleControls" ) );
    REQUIRE( pScale != nullptr );
    pScale->findChild<QDoubleSpinBox *>( QStringLiteral( "MapScaleX" ) )->setValue( 2.0 );
    pScale->findChild<QPushButton *>( QStringLiteral( "MapNumericApply" ) )->click();
    bounds = session.workspace.wire.objects.pData[0].bounds.box;
    CHECK( std::abs( bounds.minimum.x - 96.0 ) < 1e-7 );
    CHECK( std::abs( bounds.maximum.x - 160.0 ) < 1e-7 );
    CHECK( std::abs( MapBounds_Center( session.workspace.wire.objects.pData[0].bounds ).x - 128.0 ) < 1e-7 );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( session.workspace.wire.objects.pData[0].bounds.box.minimum.x == 96.0 );
    CHECK( session.workspace.wire.objects.pData[0].bounds.box.maximum.x == 160.0 );
}

TEST_CASE( "Brush face controls require explicit selection and apply push pull with undo", "[map][gui][toolpanels][geometry-edit]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { -32.0, -16.0, -8.0 } );
    MapBounds_AddPoint( box, { 32.0, 16.0, 8.0 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    MapWorkspace_Select( &session.workspace, session.workspace.wire.objects.pData[0].id, MAP_SELECT_REPLACE );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    auto *pFaces = panel->findChild<QComboBox *>( QStringLiteral( "MapBrushFaceSelector" ) );
    auto *pPushPull = panel->findChild<QToolButton *>( QStringLiteral( "MapBrushFacePushPull" ) );
    auto *pDistance = panel->findChild<QDoubleSpinBox *>( QStringLiteral( "MapBrushFaceDistance" ) );
    REQUIRE( pFaces != nullptr ); REQUIRE( pPushPull != nullptr ); REQUIRE( pDistance != nullptr );
    CHECK( pFaces->count() == 7 );
    CHECK( pFaces->currentIndex() == 0 );
    CHECK_FALSE( MapWorkspace_HasBrushFace( &session.workspace ) );
    CHECK_FALSE( pPushPull->isEnabled() );
    int index = -1;
    for ( int i = 1; i < pFaces->count(); ++i ) { if ( pFaces->itemText( i ).contains( QStringLiteral( "+X" ) ) ) { index = i; } }
    REQUIRE( index > 0 );
    pFaces->setCurrentIndex( index );
    REQUIRE( MapWorkspace_HasBrushFace( &session.workspace ) );
    REQUIRE( pPushPull->isEnabled() );
    CHECK( session.workspace.elementMode == map_element_mode_t::FACES );
    const auto side = session.workspace.selectedBrushFaceSide;
    pDistance->setValue( 16.0 );
    pPushPull->click();
    CHECK( session.workspace.wire.objects.pData[0].bounds.box.maximum.x == 48.0 );
    CHECK( session.workspace.selectedBrushFaceSide == side );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( session.workspace.wire.objects.pData[0].bounds.box.maximum.x == 32.0 );
    CHECK( pFaces == panel->findChild<QComboBox *>( QStringLiteral( "MapBrushFaceSelector" ) ) );
    MapWorkspace_ClearBrushFace( &session.workspace );
    CHECK( pFaces->currentIndex() == 0 );
    CHECK_FALSE( pPushPull->isEnabled() );
}

TEST_CASE( "Numeric brush clipping rejects a zero normal and cuts with one undo step", "[map][gui][toolpanels][geometry-edit]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { -32.0, -32.0, -32.0 } );
    MapBounds_AddPoint( box, { 32.0, 32.0, 32.0 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    MapWorkspace_Select( &session.workspace, session.workspace.wire.objects.pData[0].id, MAP_SELECT_REPLACE );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::CLIP );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    auto *pApply = panel->findChild<QPushButton *>( QStringLiteral( "MapClipApply" ) );
    auto *pX = panel->findChild<QDoubleSpinBox *>( QStringLiteral( "MapClipNormalX" ) );
    REQUIRE( pApply != nullptr ); REQUIRE( pX != nullptr ); REQUIRE( pApply->isEnabled() );
    Setting<QComboBox>( *panel, "editor.map.clip_mode" )->setCurrentText( QStringLiteral( "back" ) );
    const usize steps = EditorHistory_StepCount( &session.workspace.history );
    pX->setValue( 0.0 );
    pApply->click();
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == steps );
    CHECK( session.workspace.wire.objects.pData[0].bounds.box.maximum.x == 32.0 );
    pX->setValue( 2.0 ); // The UI normalizes coefficients, preserving the plane.
    pApply->click();
    REQUIRE( session.workspace.wire.objects.nCount == 1u );
    CHECK( session.workspace.wire.objects.pData[0].bounds.box.maximum.x == 0.0 );
    CHECK( EditorHistory_StepCount( &session.workspace.history ) == steps + 1u );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( session.workspace.wire.objects.pData[0].bounds.box.maximum.x == 32.0 );
    CHECK( pApply == panel->findChild<QPushButton *>( QStringLiteral( "MapClipApply" ) ) );
}

TEST_CASE( "Numeric clipping applies the enabled retained-half control and normalizes the exact plane", "[map][gui][toolpanels][geometry-edit][clip]" )
{
    session_t session;
    auto &ws = session.workspace;
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { 0, 0, 0 } ); MapBounds_AddPoint( box, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    const u64 original = ws.selection.ids.pData[0];
    MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    auto *mode = Setting<QComboBox>( *panel, "editor.map.clip_mode" );
    auto *axis = Setting<QComboBox>( *panel, "editor.map.clip_axis" );
    auto *apply = panel->findChild<QPushButton *>( QStringLiteral( "MapClipApply" ) );
    auto *normal = panel->findChild<QDoubleSpinBox *>( QStringLiteral( "MapClipNormalX" ) );
    auto *distance = panel->findChild<QDoubleSpinBox *>( QStringLiteral( "MapClipPlaneDistance" ) );
    auto *rule = panel->findChild<QLabel *>( QStringLiteral( "MapClipRetainRule" ) );
    REQUIRE( mode != nullptr ); REQUIRE( apply != nullptr ); REQUIRE( normal != nullptr ); REQUIRE( distance != nullptr ); REQUIRE( rule != nullptr );
    REQUIRE( mode->isEnabled() ); CHECK( mode->currentText() == QStringLiteral( "both" ) );
    REQUIRE( axis->isEnabled() ); CHECK( axis->currentText() == QStringLiteral( "z" ) );
    CHECK( axis->count() == 3 ); CHECK( axis->itemText( 0 ) == QStringLiteral( "x" ) );
    CHECK( axis->itemText( 1 ) == QStringLiteral( "y" ) ); CHECK( axis->itemText( 2 ) == QStringLiteral( "z" ) );
    CHECK( MapWorkspace_ClipAxis( &ws ) == 2u );
    CHECK( MapToolProperties_Options( panel.get() ).contains( QStringLiteral( "editor.map.clip_axis" ) ) );
    CHECK( MapToolProperties_Options( panel.get() ).contains( QStringLiteral( "editor.map.clip_mode" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[LeftDrag] 2D: stage clipping plane; release to preview" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[LeftDrag] 3D: draw on the plane perpendicular to the 3D plane axis" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[LeftDrag] Staged endpoint: move it; release to preview" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Shift+X] Cycle retained half-space while active" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Enter / NumEnter] Confirm current gesture" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Escape] Cancel drag/stage; idle: enter Navigation (keep selection)" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Focus loss] Cancel unreleased drag; staged plane retained" ) ) );
    CHECK_FALSE( Operation( *panel, "map.mesh.slice" )->isEnabled() );
    normal->setValue( 2.0 ); distance->setValue( -128.0 ); // Normalized result is x=64, not x=128.
    const usize steps = EditorHistory_StepCount( &ws.history );
    const auto revision = ws.pDocument->geometry.revision;

    SECTION( "Back, Front and Both produce their requested actual geometry with one history entry" ) {
        for ( const char *text : { "back", "front", "both" } ) {
            CAPTURE( text );
            const usize beforeModeSteps = EditorHistory_StepCount( &ws.history );
            mode->setCurrentText( QString::fromUtf8( text ) );
            CHECK( EditorHistory_StepCount( &ws.history ) == beforeModeSteps ); CHECK( ws.pDocument->geometry.revision == revision );
            CHECK( EditorHistory_AppliedStepCount( &ws.history ) == steps );
            CHECK( rule->text().startsWith( QString::fromUtf8( text ).at( 0 ).toUpper() ) );
            REQUIRE( apply->isEnabled() ); apply->click();
            const bool both = mode->currentText() == QStringLiteral( "both" );
            REQUIRE( ws.wire.objects.nCount == ( both ? 2u : 1u ) );
            CHECK( ws.selection.ids.nCount == ws.wire.objects.nCount );
            CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
            if ( both ) {
                bool back = false, front = false;
                for ( usize i = 0u; i < ws.wire.objects.nCount; ++i ) {
                    const auto &object = ws.wire.objects.pData[i];
                    back |= object.bounds.box.minimum.x == 0.0 && object.bounds.box.maximum.x == 64.0;
                    front |= object.bounds.box.minimum.x == 64.0 && object.bounds.box.maximum.x == 128.0;
                    CHECK( object.id != original );
                }
                CHECK( back ); CHECK( front );
            } else {
                const auto &object = ws.wire.objects.pData[0];
                CHECK( object.id == original );
                CHECK( object.bounds.box.minimum.x == ( mode->currentText() == QStringLiteral( "back" ) ? 0.0 : 64.0 ) );
                CHECK( object.bounds.box.maximum.x == ( mode->currentText() == QStringLiteral( "back" ) ? 64.0 : 128.0 ) );
            }
            REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
            REQUIRE( ws.wire.objects.nCount == 1u ); CHECK( ws.wire.objects.pData[0].id == original );
            CHECK( ws.wire.objects.pData[0].bounds.box.minimum.x == 0.0 ); CHECK( ws.wire.objects.pData[0].bounds.box.maximum.x == 128.0 );
            CHECK( mode == Setting<QComboBox>( *panel, "editor.map.clip_mode" ) );
        }
    }
    SECTION( "A wholly discarded half-space removes the brush and undo restores it" ) {
        mode->setCurrentText( QStringLiteral( "back" ) );
        normal->setValue( 1.0 ); distance->setValue( 64.0 );
        apply->click();
        CHECK( ws.wire.objects.nCount == 0u ); CHECK( ws.selection.ids.nCount == 0u );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        REQUIRE( ws.wire.objects.nCount == 1u ); CHECK( ws.wire.objects.pData[0].id == original );
    }
    SECTION( "Staged planes update numeric inspection once and retain drafts through mode changes" ) {
        auto *normalY = panel->findChild<QDoubleSpinBox *>( QStringLiteral( "MapClipNormalY" ) );
        auto *normalZ = panel->findChild<QDoubleSpinBox *>( QStringLiteral( "MapClipNormalZ" ) );
        REQUIRE( normalY != nullptr ); REQUIRE( normalZ != nullptr );
        int reflectedSignals = 0;
        QObject::connect( normal, &QDoubleSpinBox::valueChanged, panel.get(), [&]( double ) { ++reflectedSignals; } );
        QObject::connect( normalY, &QDoubleSpinBox::valueChanged, panel.get(), [&]( double ) { ++reflectedSignals; } );
        QObject::connect( normalZ, &QDoubleSpinBox::valueChanged, panel.get(), [&]( double ) { ++reflectedSignals; } );
        QObject::connect( distance, &QDoubleSpinBox::valueChanged, panel.get(), [&]( double ) { ++reflectedSignals; } );
        MapWorkspace_SetClipPreview( &ws, { { 0, 1, 0 }, -32 } );
        REQUIRE( ws.editPreview.status == map_status_t::OK );
        CHECK( normal->value() == 0.0 ); CHECK( normalY->value() == 1.0 ); CHECK( normalZ->value() == 0.0 ); CHECK( distance->value() == -32.0 );
        CHECK( reflectedSignals == 0 );
        normalY->setValue( 2.0 ); distance->setValue( -100.0 );
        reflectedSignals = 0;
        mode->setCurrentText( QStringLiteral( "front" ) );
        CHECK( ws.editPreview.clipMode == map_brush_clip_mode_t::FRONT ); CHECK( ws.editPreview.clipPlane.d == -32.0 );
        MapWorkspace_Notify( &ws, MAP_CHANGE_VIEW );
        CHECK( normalY->value() == 2.0 ); CHECK( distance->value() == -100.0 ); CHECK( reflectedSignals == 0 );
        MapWorkspace_SetClipPreview( &ws, { { 0, 0, 1 }, -96 } );
        CHECK( normal->value() == 0.0 ); CHECK( normalY->value() == 0.0 ); CHECK( normalZ->value() == 1.0 ); CHECK( distance->value() == -96.0 );
        CHECK( reflectedSignals == 0 );
        MapWorkspace_ClearEditPreview( &ws );
        distance->setValue( -75.0 ); reflectedSignals = 0;
        MapWorkspace_SetClipPreview( &ws, { { 0, 0, 1 }, -96 } ); // A new gesture with the same plane reflects again.
        CHECK( distance->value() == -96.0 ); CHECK( reflectedSignals == 0 );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( ws.pDocument->geometry.revision == revision );
    }
    SECTION( "Endpoint coordinates reflect the captured guide and changing the axis discards it without history" ) {
        auto *guideLabel = panel->findChild<QLabel *>( QStringLiteral( "MapClipGuide" ) );
        REQUIRE( guideLabel != nullptr ); CHECK( guideLabel->isHidden() );
        CHECK( guideLabel->textInteractionFlags() == Qt::TextSelectableByMouse );
        MapWorkspace_SetClipPreview( &ws, { { 1, 0, 0 }, -64 } );
        map_clip_guide_t guide{}; guide.bHas = CY_TRUE; guide.extrusionAxis = 2u;
        guide.points[0] = { 64, 16, 64 }; guide.points[1] = { 64, 112, 64 };
        MapWorkspace_SetClipGuide( &ws, guide );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.clipGuide.bHas );
        CHECK_FALSE( guideLabel->isHidden() );
        CHECK( guideLabel->text() == QStringLiteral( "Extrude Z\nP1  X 64 · Y 16 · Z 64\nP2  X 64 · Y 112 · Z 64" ) );
        guide.points[1].y = 96;
        MapWorkspace_SetClipGuide( &ws, guide );
        CHECK( guideLabel->text().contains( QStringLiteral( "P2  X 64 · Y 96 · Z 64" ) ) );
        const auto displayedGuide = guideLabel->text();
        mode->setCurrentText( QStringLiteral( "front" ) );
        REQUIRE( ws.editPreview.bActive ); CHECK( guideLabel->text() == displayedGuide );
        normal->setValue( 2.0 ); distance->setValue( -100.0 );
        axis->setCurrentText( QStringLiteral( "y" ) );
        CHECK( MapWorkspace_ClipAxis( &ws ) == 1u );
        CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.objects.nCount == 0u );
        CHECK( guideLabel->isHidden() ); CHECK( guideLabel->text().isEmpty() );
        CHECK( normal->value() == 2.0 ); CHECK( distance->value() == -100.0 ); // Numeric drafts retain their independent meaning.
        CHECK( ws.tool == map_tool_t::CLIP ); CHECK( axis == Setting<QComboBox>( *panel, "editor.map.clip_axis" ) );
        axis->setCurrentText( QStringLiteral( "x" ) ); CHECK( MapWorkspace_ClipAxis( &ws ) == 0u );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps ); CHECK( ws.pDocument->geometry.revision == revision );
    }
}

TEST_CASE( "Workspace notifications clear stale clipping previews before panels observe them", "[map][gui][toolpanels][clip][preview]" )
{
    session_t session;
    auto &ws = session.workspace;
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { 0, 0, 0 } ); MapBounds_AddPoint( box, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    const u64 selected = ws.selection.ids.pData[0];
    MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    MapWorkspace_SetClipPreview( &ws, { { 1, 0, 0 }, -64 } );
    REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreviewWire.objects.nCount == 2u );
    const usize steps = EditorHistory_StepCount( &ws.history );
    struct observation_t { map_workspace_t *workspace; int calls{}; u32 changes{}; } observed{ &ws };
    const auto listener = []( void *context, u32 changes ) noexcept {
        auto &observation = *static_cast<observation_t *>( context );
        ++observation.calls; observation.changes |= changes;
        CHECK_FALSE( observation.workspace->editPreview.bActive );
        CHECK( observation.workspace->editPreviewWire.objects.nCount == 0u );
        CHECK( observation.workspace->editPreviewWire.points.nCount == 0u );
    };
    REQUIRE( MapWorkspace_AddListener( &ws, listener, &observed ) );
    SECTION( "An explicit hide invalidates a programmatically staged preview" ) {
        REQUIRE( EditorSelection_Apply( &ws.hidden, selected, EDITOR_SELECT_ADD ) );
        MapWorkspace_Notify( &ws, MAP_CHANGE_SELECTION );
    }
    SECTION( "An automatic visibility group can hide the source without changing selection" ) {
        MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::BRUSHES, CY_TRUE );
    }
    SECTION( "A cordon excludes the source without changing selection" ) {
        map_bounds_t excluded{};
        MapBounds_AddPoint( excluded, { 256, 256, 256 } ); MapBounds_AddPoint( excluded, { 512, 512, 512 } );
        MapWorkspace_SetCordon( &ws, excluded );
    }
    SECTION( "The selected identities change" ) {
        MapWorkspace_Select( &ws, 0u, MAP_SELECT_REPLACE );
    }
    SECTION( "A direct tool change has no viewport owner to cancel the preview" ) {
        MapWorkspace_SetTool( &ws, map_tool_t::SELECT );
    }
    SECTION( "An actual element mode change discards the staged plane" ) {
        MapWorkspace_SetElementMode( &ws, ws.elementMode );
        CHECK( ws.editPreview.bActive ); CHECK( observed.calls == 0 );
        MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    }
    SECTION( "The source document changes in place" ) {
        ++ws.pDocument->geometry.revision;
        MapWorkspace_Notify( &ws, MAP_CHANGE_DOCUMENT );
    }
    SECTION( "A read-only document cannot retain an actionable preview" ) {
        ws.pDocument->bReadOnly = CY_TRUE;
        MapWorkspace_Notify( &ws, MAP_CHANGE_TITLE );
    }
    CHECK( observed.calls == 1 ); CHECK( ( observed.changes & MAP_CHANGE_VIEW ) != 0u );
    CHECK_FALSE( ws.editPreview.bActive ); CHECK( ws.editPreviewWire.objects.nCount == 0u );
    CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    MapWorkspace_RemoveListener( &ws, listener, &observed );
}

TEST_CASE( "Hollow operation uses the editable thickness and preserves undo", "[map][gui][toolpanels][geometry-edit]" )
{
    session_t session;
    REQUIRE( MapWorkspace_New( &session.workspace ) == map_status_t::OK );
    map_bounds_t box{};
    MapBounds_AddPoint( box, { -32.0, -32.0, -32.0 } );
    MapBounds_AddPoint( box, { 32.0, 32.0, 32.0 } );
    REQUIRE( MapWorkspace_CreateBox( &session.workspace, box ) );
    MapWorkspace_Select( &session.workspace, session.workspace.wire.objects.pData[0].id, MAP_SELECT_REPLACE );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    Setting<QDoubleSpinBox>( *panel, "editor.map.hollow_thickness" )->setValue( 8.0 );
    auto *pHollow = Operation( *panel, "map.brush.hollow" );
    REQUIRE( pHollow->isEnabled() );
    pHollow->click();
    CHECK( session.workspace.wire.objects.nCount == 6u );
    REQUIRE( MapWorkspace_Undo( &session.workspace ) == editor_history_status_t::OK );
    CHECK( session.workspace.wire.objects.nCount == 1u );
    CHECK( session.workspace.wire.objects.pData[0].bounds.box.minimum.x == -32.0 );
    CHECK( session.workspace.wire.objects.pData[0].bounds.box.maximum.x == 32.0 );
}

TEST_CASE( "Merge tool properties keep compact brush operations and explain compatibility", "[map][gui][toolpanels][geometry-edit][merge]" )
{
    for ( const auto tool : { map_tool_t::SELECT, map_tool_t::BLOCK } ) {
        CAPTURE( static_cast<int>( tool ) );
        session_t session;
        auto &ws = session.workspace;
        REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
        map_bounds_t box{};
        MapBounds_AddPoint( box, { 0, 0, 0 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 first = EditorSelection_At( &ws.selection, 0 );
        box.box.minimum.x = 64; box.box.maximum.x = 128;
        REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 second = EditorSelection_At( &ws.selection, 0 );
        MapWorkspace_SetTool( &ws, tool );
        std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
        auto *merge = Operation( *panel, "map.brush.merge" );
        CHECK_FALSE( merge->isEnabled() );
        CHECK( merge->toolButtonStyle() == Qt::ToolButtonIconOnly );
        CHECK_FALSE( merge->icon().isNull() );
        CHECK( merge->accessibleName() == QStringLiteral( "Merge" ) );
        CHECK( merge->parentWidget()->property( "toolGroup" ).toString() == QStringLiteral( "Brush operations" ) );
        auto *grid = merge->parentWidget()->findChild<QGridLayout *>();
        REQUIRE( grid != nullptr );
        int row{}, column{}, rowSpan{}, columnSpan{};
        grid->getItemPosition( grid->indexOf( merge ), &row, &column, &rowSpan, &columnSpan );
        CHECK( row == 0 ); CHECK( column == ( tool == map_tool_t::BLOCK ? 2 : 1 ) );
        CHECK( rowSpan == 1 ); CHECK( columnSpan == 1 );
        auto *help = panel->findChild<QLabel *>( QStringLiteral( "MapBrushMergeHelp" ) );
        REQUIRE( help != nullptr );
        CHECK( help->text().contains( QStringLiteral( "exactly two visible" ) ) );
        CHECK( help->text().contains( QStringLiteral( "same layer and entity" ) ) );
        CHECK( help->text().contains( QStringLiteral( "lowest-ID" ) ) );
        CHECK( help->text().contains( QStringLiteral( "convex" ) ) );
        const u64 pair[]{ first, second };
        MapWorkspace_SetSelection( &ws, pair, 2 );
        REQUIRE( merge == Operation( *panel, "map.brush.merge" ) );
        REQUIRE( merge->isEnabled() );
        CHECK( merge->toolTip().contains( QStringLiteral( "source-face materials and UVs" ) ) );
        const usize steps = EditorHistory_StepCount( &ws.history );
        merge->click();
        REQUIRE( ws.wire.objects.nCount == 1u );
        CHECK( ws.tool == tool ); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
        CHECK_FALSE( merge->isEnabled() );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        CHECK( ws.wire.objects.nCount == 2u ); CHECK( merge->isEnabled() );
        ws.pDocument->bReadOnly = CY_TRUE;
        MapWorkspace_Notify( &ws, MAP_CHANGE_DOCUMENT );
        CHECK_FALSE( merge->isEnabled() );
    }
}

TEST_CASE( "Auto Vis Groups save, load, and delete visibility presets", "[map][gui][visgroups]" )
{
    session_t s;
    std::unique_ptr<QWidget> panel( MapVisgroups_Create( nullptr, &s.workspace ) );
    REQUIRE( panel != nullptr );
    CHECK( MapVisgroups_Presets( panel.get() ).isEmpty() );
    MapWorkspace_SetVisgroupHidden( &s.workspace, map_visgroup_t::LIGHTS, CY_TRUE );
    REQUIRE( MapVisgroups_SavePreset( panel.get(), QStringLiteral( "No lights" ) ) );
    CHECK( MapVisgroups_Presets( panel.get() ) == QStringList{ QStringLiteral( "No lights" ) } );
    // Show everything, then the preset hides the lights again.
    for ( u32 g = 0u; g < static_cast<u32>( map_visgroup_t::COUNT ); ++g ) {
        MapWorkspace_SetVisgroupHidden( &s.workspace, static_cast<map_visgroup_t>( g ), CY_FALSE );
    }
    CHECK_FALSE( MapWorkspace_IsVisgroupHidden( &s.workspace, map_visgroup_t::LIGHTS ) );
    REQUIRE( MapVisgroups_LoadPreset( panel.get(), QStringLiteral( "No lights" ) ) );
    CHECK( MapWorkspace_IsVisgroupHidden( &s.workspace, map_visgroup_t::LIGHTS ) );
    CHECK_FALSE( MapWorkspace_IsVisgroupHidden( &s.workspace, map_visgroup_t::BRUSHES ) );
    // Saving the same name replaces it; separators cannot sneak in.
    REQUIRE( MapVisgroups_SavePreset( panel.get(), QStringLiteral( "No lights" ) ) );
    CHECK( MapVisgroups_Presets( panel.get() ).size() == 1 );
    CHECK_FALSE( MapVisgroups_SavePreset( panel.get(), QStringLiteral( ";=" ) ) );
    CHECK_FALSE( MapVisgroups_LoadPreset( panel.get(), QStringLiteral( "Missing" ) ) );
    CHECK( MapVisgroups_DeletePreset( panel.get(), QStringLiteral( "No lights" ) ) );
    CHECK( MapVisgroups_Presets( panel.get() ).isEmpty() );
}

TEST_CASE( "Mesh face controls retain persistent selection and live controls across edits and mode changes", "[map][gui][toolpanels][mesh-face][lifetime]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 object = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    u64 face = 0;
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) {
        const auto &candidate = ws.wire.faces.pData[i];
        if ( candidate.id == object && candidate.normal.z > 0.99 ) { face = candidate.faceId; }
    }
    REQUIRE( face != 0 );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    QPointer<QComboBox> selector = panel->findChild<QComboBox *>( QStringLiteral( "MapMeshFaceSelector" ) );
    QPointer<QPushButton> extrude = panel->findChild<QPushButton *>( QStringLiteral( "map.mesh.extrude" ) );
    QPointer<QPushButton> inset = panel->findChild<QPushButton *>( QStringLiteral( "map.mesh.inset" ) );
    QPointer<QDoubleSpinBox> distance = Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_extrude_distance" );
    QPointer<QDoubleSpinBox> margin = Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_inset_distance" );
    REQUIRE( selector ); REQUIRE( extrude ); REQUIRE( inset );
    auto *meshBody = panel->findChild<QWidget *>( QStringLiteral( "MapMeshFaceControls" ) );
    auto *brushBody = panel->findChild<QWidget *>( QStringLiteral( "MapBrushFaceControls" ) );
    REQUIRE( meshBody != nullptr ); REQUIRE( brushBody != nullptr );
    QPointer<QWidget> meshSection = meshBody->parentWidget(), brushSection = brushBody->parentWidget();
    REQUIRE( meshSection ); REQUIRE( brushSection );
    CHECK_FALSE( gui::EditorSection_IsExpanded( meshSection ) ); CHECK( meshBody->isHidden() );
    CHECK_FALSE( gui::EditorSection_IsExpanded( brushSection ) ); CHECK( brushBody->isHidden() );
    // Component widgets exist for a lifetime-safe mode switch, but their
    // inventory is absent while the SELECT profile is Objects.
    CHECK( meshSection->isHidden() ); CHECK( brushSection->isHidden() );
    CHECK_FALSE( MapToolProperties_Options( panel.get() ).contains( QStringLiteral( "editor.map.mesh_extrude_distance" ) ) );
    CHECK_FALSE( MapToolProperties_Options( panel.get() ).contains( QStringLiteral( "editor.map.mesh_inset_distance" ) ) );
    CHECK_FALSE( MapToolProperties_Operations( panel.get() ).contains( QStringLiteral( "map.mesh.extrude" ) ) );
    CHECK_FALSE( MapToolProperties_Operations( panel.get() ).contains( QStringLiteral( "map.mesh.inset" ) ) );
    CHECK( selector->count() == 7 ); CHECK( selector->currentIndex() == 0 );
    CHECK_FALSE( extrude->isEnabled() ); CHECK_FALSE( inset->isEnabled() );
    const auto checkControls = [&]() {
        REQUIRE( selector ); REQUIRE( extrude ); REQUIRE( inset ); REQUIRE( distance ); REQUIRE( margin );
        CHECK( selector.data() == panel->findChild<QComboBox *>( QStringLiteral( "MapMeshFaceSelector" ) ) );
        CHECK( extrude.data() == panel->findChild<QPushButton *>( QStringLiteral( "map.mesh.extrude" ) ) );
        CHECK( inset.data() == panel->findChild<QPushButton *>( QStringLiteral( "map.mesh.inset" ) ) );
        CHECK( distance.data() == Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_extrude_distance" ) );
        CHECK( margin.data() == Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_inset_distance" ) );
        REQUIRE( meshSection ); REQUIRE( brushSection );
        CHECK( gui::EditorSection_Body( meshSection ) == panel->findChild<QWidget *>( QStringLiteral( "MapMeshFaceControls" ) ) );
        CHECK( gui::EditorSection_Body( brushSection ) == panel->findChild<QWidget *>( QStringLiteral( "MapBrushFaceControls" ) ) );
    };
    const int index = selector->findData( QVariant::fromValue( static_cast<qulonglong>( face ) ) ); REQUIRE( index > 0 );
    // This emits currentIndexChanged while workspace observers refresh;
    // the originating combo and its sibling controls must remain alive.
    selector->setCurrentIndex( index ); checkControls(); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
    CHECK_FALSE( meshSection->isHidden() ); CHECK_FALSE( brushSection->isHidden() );
    CHECK( MapToolProperties_Options( panel.get() ).contains( QStringLiteral( "editor.map.mesh_extrude_distance" ) ) );
    CHECK( MapToolProperties_Options( panel.get() ).contains( QStringLiteral( "editor.map.mesh_inset_distance" ) ) );
    CHECK( MapToolProperties_Operations( panel.get() ).count( QStringLiteral( "map.mesh.extrude" ) ) == 1 );
    CHECK( MapToolProperties_Operations( panel.get() ).count( QStringLiteral( "map.mesh.inset" ) ) == 1 );
    CHECK( gui::EditorSection_IsExpanded( meshSection ) ); CHECK_FALSE( meshBody->isHidden() );
    CHECK_FALSE( gui::EditorSection_IsExpanded( brushSection ) ); CHECK( brushBody->isHidden() );
    CHECK( ws.selectedMeshFaceObject == object ); CHECK( ws.selectedMeshFaceId == face );
    REQUIRE( extrude->isEnabled() ); REQUIRE( inset->isEnabled() );
    // Once the user collapses the current component section, ordinary
    // refreshes must respect that choice until another component is picked.
    gui::EditorSection_SetExpanded( meshSection, false );
    distance->setValue( 40 ); margin->setValue( 12 );
    CHECK_FALSE( gui::EditorSection_IsExpanded( meshSection ) ); CHECK( meshBody->isHidden() );
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.map.mesh_extrude_distance", 0 ) == 40 );
    CHECK( EditorSettings_Real( &session.gui.settings, "editor.map.mesh_inset_distance", 0 ) == 12 );
    const usize steps = EditorHistory_StepCount( &ws.history );
    extrude->click(); checkControls(); CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1 );
    CHECK( MapWireframe_FindObject( ws.wire, object )->bounds.box.maximum.z == 168 );
    CHECK( ws.selectedMeshFaceId == face ); CHECK( selector->currentData().toULongLong() == face );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); checkControls();
    CHECK( MapWireframe_FindObject( ws.wire, object )->bounds.box.maximum.z == 128 );
    inset->click(); checkControls(); CHECK( selector->count() == 11 ); CHECK( selector->currentData().toULongLong() == face );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); checkControls(); CHECK( selector->count() == 7 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); checkControls();
    CHECK_FALSE( extrude->isEnabled() ); CHECK_FALSE( inset->isEnabled() ); CHECK( selector->currentIndex() == 0 );
    selector->setCurrentIndex( selector->findData( QVariant::fromValue( static_cast<qulonglong>( face ) ) ) );
    checkControls(); REQUIRE( MapWorkspace_HasMeshFace( &ws ) ); REQUIRE( extrude->isEnabled() );
    CHECK( gui::EditorSection_IsExpanded( meshSection ) ); CHECK_FALSE( meshBody->isHidden() );
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QStringLiteral( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "mesh_face_panel" bindings = { "map.selection.faces" = { "map.mesh.extrude" = [ "X" ] "map.mesh.inset" = [ "I" ] } } }
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "mesh_face_panel" ) ) == gui::editor_gui_status_t::OK );
    checkControls(); CHECK( selector->currentData().toULongLong() == face );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[X] Mesh face: extrude along its normal (Faces)" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[I] Mesh face: inset corners toward its center (Faces)" ) ) );
    ws.pDocument->bReadOnly = CY_TRUE; MapWorkspace_Notify( &ws, MAP_CHANGE_DOCUMENT ); checkControls();
    CHECK_FALSE( extrude->isEnabled() ); CHECK_FALSE( inset->isEnabled() );
    ws.pDocument->bReadOnly = CY_FALSE; MapWorkspace_Notify( &ws, MAP_CHANGE_DOCUMENT );
    MapWorkspace_ClearMeshFace( &ws ); checkControls(); CHECK( selector->currentIndex() == 0 );
    CHECK_FALSE( extrude->isEnabled() ); CHECK_FALSE( inset->isEnabled() );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 brush = EditorSelection_At( &ws.selection, 0 );
    u64 side = 0;
    for ( usize i = 0; i < ws.wire.faces.nCount; ++i ) {
        const auto &candidate = ws.wire.faces.pData[i]; if ( candidate.id == brush && candidate.normal.z > 0.99 ) { side = candidate.sideId; }
    }
    REQUIRE( side != 0 ); MapWorkspace_SelectBrushFace( &ws, brush, side ); checkControls();
    CHECK( gui::EditorSection_IsExpanded( brushSection ) ); CHECK_FALSE( brushBody->isHidden() );
    CHECK_FALSE( gui::EditorSection_IsExpanded( meshSection ) ); CHECK( meshBody->isHidden() );
}

TEST_CASE( "Mesh face numeric parameters survive settings serialization and tool replacement", "[map][gui][toolpanels][mesh-face][settings]" )
{
    session_t session; auto &ws = session.workspace; const usize listeners = session.gui.settings.nListeners;
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_extrude_distance" )->setValue( 48 );
    Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_inset_distance" )->setValue( 10 );
    text_buffer_t text{}; REQUIRE( TextBuffer_Init( &text, Allocator_GetSystem() ) );
    REQUIRE( SettingsDocument_Write( &session.settings, &text ) == settings_document_status_t::OK );
    settings_document_t restored{};
    REQUIRE( SettingsDocument_Init( &restored, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &restored, TextBuffer_View( &text ) ).status == settings_document_status_t::OK );
    Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_extrude_distance" )->setValue( 8 );
    Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_inset_distance" )->setValue( 4 );
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, &restored );
    for ( map_tool_t tool : { map_tool_t::SELECT, map_tool_t::EXTRUDE, map_tool_t::SELECT } ) {
        MapWorkspace_SetTool( &ws, tool );
        CHECK( Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_extrude_distance" )->value() == 48 );
        CHECK( Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_inset_distance" )->value() == 10 );
    }
    EditorSettings_SetScope( &session.gui.settings, settings_scope_t::USER, &session.settings );
    CHECK( Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_extrude_distance" )->value() == 8 );
    CHECK( Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_inset_distance" )->value() == 4 );
    panel.reset(); CHECK( session.gui.settings.nListeners == listeners );
}

TEST_CASE( "Quad icons create a flat authored mesh and staged plane identity survives default changes", "[map][gui][toolpanels][quad][geometry-edit]" )
{
    session_t session;
    auto &ws = session.workspace;
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    MapWorkspace_SetTool( &ws, map_tool_t::BLOCK );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    panel->resize( 340, 900 ); panel->show(); QCoreApplication::processEvents();
    ShapeButton( *panel, "quad" )->click();
    CHECK( ShapeButton( *panel, "quad" )->isChecked() );
    CHECK_FALSE( ShapeButton( *panel, "box" )->isChecked() );
    Setting<QComboBox>( *panel, "editor.map.primitive_axis" )->setCurrentText( QStringLiteral( "Z" ) );
    auto *sizeZ = panel->findChild<QDoubleSpinBox *>( QStringLiteral( "MapBoxSizeZ" ) ); REQUIRE( sizeZ != nullptr );
    CHECK_FALSE( sizeZ->isEnabled() ); CHECK( sizeZ->value() == 0.0 );
    CHECK_FALSE( Setting<QSpinBox>( *panel, "editor.map.cylinder_sides" )->isVisible() );
    auto *centerZ = panel->findChild<QDoubleSpinBox *>( QStringLiteral( "MapBoxCenterZ" ) ); REQUIRE( centerZ != nullptr ); centerZ->setValue( 48 );
    auto *create = panel->findChild<QPushButton *>( QStringLiteral( "MapNumericApply" ) ); REQUIRE( create != nullptr );
    CHECK( create->text() == QStringLiteral( "Create Quad" ) ); create->click();
    REQUIRE( ws.pDocument->geometry.meshes.nCount == 1 );
    CHECK( ws.pDocument->geometry.brushes.nCount == 0 );
    const auto *mesh = ws.pDocument->geometry.meshes.pData[0];
    CHECK( GenerationPool_Count( &mesh->mesh.vertices ) == 4 ); CHECK( GenerationPool_Count( &mesh->mesh.faces ) == 1 );
    REQUIRE( ws.wire.objects.nCount == 1 );
    CHECK( ws.wire.objects.pData[0].bounds.box.minimum.z == 48 );
    CHECK( ws.wire.objects.pData[0].bounds.box.maximum.z == 48 );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( ws.pDocument->geometry.meshes.nCount == 0 );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( ws.pDocument->geometry.meshes.nCount == 1 );
    map_bounds_t bounds{}; bounds.bHas = CY_TRUE; bounds.box = { { -32, -32, 24 }, { 32, 32, 24 } };
    MapWorkspace_SetEditPreview( &ws, bounds, 2u );
    REQUIRE( MapWorkspace_StageBlockPreview( &ws, 2u ) );
    Setting<QComboBox>( *panel, "editor.map.primitive_axis" )->setCurrentText( QStringLiteral( "X" ) );
    ShapeButton( *panel, "box" )->click();
    CHECK( ws.editPreview.primitive.kind == map_primitive_kind_t::QUAD );
    CHECK( ws.editPreview.primitive.axis == 2u );
    CHECK_FALSE( sizeZ->isEnabled() ); CHECK( sizeZ->value() == 0.0 );
    centerZ->setValue( 64 ); CHECK( ws.editPreview.bounds.box.minimum.z == 64 ); CHECK( ws.editPreview.bounds.box.maximum.z == 64 );
    create->click(); REQUIRE_FALSE( ws.editPreview.bActive ); CHECK( ws.pDocument->geometry.meshes.nCount == 2 );
    CHECK( ws.pDocument->geometry.brushes.nCount == 0 );
    CHECK( sizeZ->isEnabled() ); CHECK( sizeZ->value() > 0.0 );
}

TEST_CASE( "Select offers persisted individual and group bounds resizing choices", "[map][gui][toolpanels][individual-resize]" )
{
    session_t session; std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::SELECT );
    auto *mode = Setting<QComboBox>( *panel, "editor.map.resize_mode" );
    REQUIRE( mode->count() == 2 ); CHECK( mode->itemText( 0 ) == QStringLiteral( "Each object" ) );
    CHECK( mode->itemText( 1 ) == QStringLiteral( "Selection bounds" ) ); CHECK( mode->currentIndex() == 0 );
    mode->setCurrentIndex( 1 );
    CHECK( StringView_Equals( EditorSettings_Text( &session.gui.settings, "editor.map.resize_mode", {} ), StringView_FromCString( "Selection bounds" ) ) );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK );
    CHECK( panel->findChild<QWidget *>( QStringLiteral( "editor.map.resize_mode" ) ) == nullptr );
    MapWorkspace_SetTool( &session.workspace, map_tool_t::SELECT );
    CHECK( Setting<QComboBox>( *panel, "editor.map.resize_mode" )->currentIndex() == 1 );
}

TEST_CASE( "Faces expose authored texture state and compact material and push pull controls", "[map][gui][toolpanels][face-clarity]" )
{
    session_t session; auto &ws = session.workspace;
    REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -32, -16, -8 } ); MapBounds_AddPoint( box, { 32, 16, 8 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 object = EditorSelection_At( &ws.selection, 0 );
    const auto *solid = geometry::GeometryDocument_FindBrush( &ws.pDocument->geometry, { object } ); REQUIRE( solid != nullptr );
    usize sideIndex = 0; for ( usize i = 0; i < solid->sides.nCount; ++i ) { if ( solid->sides.pData[i].plane.normal.z > 0.99 ) { sideIndex = i; } }
    const u64 side = solid->sides.pData[sideIndex].sourceId.value;
    auto *attributes = geometry::GeometryDocument_FindBrushAttributesMutable( &ws.pDocument->geometry, { object } ); REQUIRE( attributes != nullptr );
    // A valid authored projection with deliberately different U/V units.
    // The panel must read this surface rather than the creation defaults.
    auto &uv = attributes->records.pData[solid->sides.pData[sideIndex].iAttributeIndex].uvProjection;
    uv.worldUnitsPerUv = { 96, 192 }; uv.offset = { 0.25, -0.5 }; uv.rotationRadians = math::Scalar_DegreesToRadians( 30.0 );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) ); panel->resize( 300, 1000 ); panel->show(); QCoreApplication::processEvents();
    auto *stateSection = panel->findChild<QWidget *>( QStringLiteral( "MapFaceTextureSection" ) ); REQUIRE( stateSection ); CHECK( stateSection->isHidden() );
    QPointer<QLineEdit> originalField = panel->findChild<QLineEdit *>( QStringLiteral( "MapFaceTextureSizeU" ) ); REQUIRE( originalField );
    MapWorkspace_SelectBrushFace( &ws, object, side ); CHECK_FALSE( stateSection->isHidden() );
    CHECK( originalField.data() == panel->findChild<QLineEdit *>( QStringLiteral( "MapFaceTextureSizeU" ) ) );
    const auto field = [&]( const char *name ) { auto *value = panel->findChild<QLineEdit *>( QString::fromLatin1( name ) ); REQUIRE( value != nullptr ); CHECK( value->isReadOnly() ); return value; };
    QPointer<QLineEdit> sizeU = field( "MapFaceTextureSizeU" ), sizeV = field( "MapFaceTextureSizeV" );
    QPointer<QLineEdit> shiftU = field( "MapFaceTextureShiftU" ), shiftV = field( "MapFaceTextureShiftV" );
    QPointer<QLineEdit> rotation = field( "MapFaceTextureRotation" ), material = field( "MapFaceTextureMaterial" );
    CHECK( sizeU->text() == QStringLiteral( "96 u/repeat" ) ); CHECK( sizeV->text() == QStringLiteral( "192 u/repeat" ) );
    CHECK( shiftU->text() == QStringLiteral( "0.25 repeats" ) ); CHECK( shiftV->text() == QStringLiteral( "-0.5 repeats" ) );
    CHECK( rotation->text() == QStringLiteral( "30°" ) );
    CHECK( sizeU->accessibleName() == QStringLiteral( "Repeat size U" ) ); CHECK( shiftV->accessibleName() == QStringLiteral( "Shift V" ) );
    REQUIRE( panel->findChild<QLabel *>( QStringLiteral( "MapFaceTextureStateNote" ) ) );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapFaceTextureStateNote" ) )->text().contains( QStringLiteral( "read only" ) ) );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapFaceTextureBasis" ) )->text().contains( QStringLiteral( "Normal:" ) ) );
    auto *faceSelector = panel->findChild<QComboBox *>( QStringLiteral( "MapBrushFaceSelector" ) ); REQUIRE( faceSelector );
    CHECK_FALSE( faceSelector->itemIcon( faceSelector->currentIndex() ).isNull() );
    auto *push = panel->findChild<QToolButton *>( QStringLiteral( "MapBrushFacePushPull" ) );
    auto *apply = panel->findChild<QToolButton *>( QStringLiteral( "MapBrushFaceApplyMaterial" ) );
    auto *browse = panel->findChild<QToolButton *>( QStringLiteral( "MapBrushFaceBrowseMaterial" ) );
    REQUIRE( push ); REQUIRE( apply ); REQUIRE( browse );
    CHECK( push->accessibleName() == QStringLiteral( "Push / Pull Face" ) ); CHECK( push->text().isEmpty() );
    INFO( "Push/Pull size=" << push->width() << "x" << push->height() << "; min=" << push->minimumWidth() << "x" << push->minimumHeight() << "; max=" << push->maximumWidth() << "x" << push->maximumHeight() );
    CHECK( push->size() == QSize( 30, 30 ) ); CHECK( apply->size() == QSize( 30, 30 ) ); CHECK( browse->size() == QSize( 30, 30 ) );
    CHECK( apply->toolButtonStyle() == Qt::ToolButtonIconOnly );
    CHECK_FALSE( push->icon().isNull() ); CHECK_FALSE( apply->icon().isNull() ); CHECK_FALSE( browse->icon().isNull() );
    CHECK( push->toolTip().contains( QStringLiteral( "normal handle" ) ) ); CHECK( apply->toolTip().contains( QStringLiteral( "One Undo" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[LeftDrag] Brush face-normal handle: push / pull by the grid step (3D)" ) ) );
    const auto originalMaterial = material->text(); const usize originalSteps = EditorHistory_StepCount( &ws.history );
    const auto *scaleDefault = EditorSettings_Find( &session.gui.settings, StringView_FromCString( "editor.map.default_texture_scale" ) ); REQUIRE( scaleDefault );
    setting_value_t value{}; value.type = setting_type_t::REAL; value.flValue = 0.5;
    REQUIRE( EditorSettings_Write( &session.gui.settings, settings_scope_t::USER, *scaleDefault, value ) == settings_registry_status_t::OK );
    CHECK( sizeU->text() == QStringLiteral( "96 u/repeat" ) ); CHECK( EditorHistory_StepCount( &ws.history ) == originalSteps );
    const auto *materialDefault = EditorSettings_Find( &session.gui.settings, StringView_FromCString( "editor.map.default_material" ) ); REQUIRE( materialDefault );
    value = {}; value.type = setting_type_t::STRING; value.text = StringView_FromCString( "materials/face_clarity.cymat" );
    REQUIRE( EditorSettings_Write( &session.gui.settings, settings_scope_t::USER, *materialDefault, value ) == settings_registry_status_t::OK );
    REQUIRE( apply->isEnabled() ); apply->click(); REQUIRE( material );
    CHECK( material->text() == QStringLiteral( "materials/face_clarity.cymat" ) ); CHECK( EditorHistory_StepCount( &ws.history ) == originalSteps + 1 );
    CHECK( material.data() == panel->findChild<QLineEdit *>( QStringLiteral( "MapFaceTextureMaterial" ) ) );
    CHECK( rotation->text() == QStringLiteral( "30°" ) ); CHECK( sizeV->text() == QStringLiteral( "192 u/repeat" ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( material->text() == originalMaterial );
    REQUIRE( MapWorkspace_Redo( &ws ) == editor_history_status_t::OK ); CHECK( material->text() == QStringLiteral( "materials/face_clarity.cymat" ) );
    panel->findChild<QDoubleSpinBox *>( QStringLiteral( "MapBrushFaceDistance" ) )->setValue( 16 ); push->click();
    CHECK( MapWireframe_FindObject( ws.wire, object )->bounds.box.maximum.z == 24 ); CHECK( rotation->text() == QStringLiteral( "30°" ) );
    REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK ); CHECK( MapWireframe_FindObject( ws.wire, object )->bounds.box.maximum.z == 8 );
    ws.pDocument->bReadOnly = CY_TRUE; MapWorkspace_Notify( &ws, MAP_CHANGE_DOCUMENT );
    CHECK_FALSE( push->isEnabled() ); CHECK_FALSE( apply->isEnabled() ); CHECK( sizeU->isEnabled() ); // Read-only maps remain inspectable/copyable.
    ws.pDocument->bReadOnly = CY_FALSE;
    MapWorkspace_ClearBrushFace( &ws ); REQUIRE( sizeU ); CHECK( sizeU->text().isEmpty() ); CHECK_FALSE( sizeU->isEnabled() ); CHECK( material->text().isEmpty() );
}

TEST_CASE( "Mesh texture state shows corner UV ranges without fabricated planar scale", "[map][gui][toolpanels][face-clarity]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.bounds = { { 0, 0, 0 }, { 64, 128, 0 } }; quad.worldUnitsPerUv = 128;
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, quad ) ); const u64 object = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( ws.wire.faces.nCount == 1 ); const u64 face = ws.wire.faces.pData[0].faceId;
    MapWorkspace_SelectMeshFace( &ws, object, face ); REQUIRE( MapWorkspace_HasMeshFace( &ws ) );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) ); panel->resize( 300, 1000 ); panel->show(); QCoreApplication::processEvents();
    auto *rangeU = panel->findChild<QLineEdit *>( QStringLiteral( "MapFaceTextureRangeU" ) );
    auto *rangeV = panel->findChild<QLineEdit *>( QStringLiteral( "MapFaceTextureRangeV" ) );
    REQUIRE( rangeU ); REQUIRE( rangeV ); CHECK( rangeU->isReadOnly() ); CHECK( rangeV->isReadOnly() );
    CHECK( rangeU->text() == QStringLiteral( "0 … 0.5" ) ); CHECK( rangeV->text() == QStringLiteral( "0 … 1" ) );
    CHECK_FALSE( rangeU->isHidden() ); CHECK_FALSE( rangeV->isHidden() );
    CHECK( panel->findChild<QLineEdit *>( QStringLiteral( "MapFaceTextureSizeU" ) )->isHidden() );
    CHECK( panel->findChild<QLineEdit *>( QStringLiteral( "MapFaceTextureRotation" ) )->isHidden() );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapFaceTextureIdentity" ) )->text().contains( QStringLiteral( "4 UV corners" ) ) );
    MapWorkspace_ClearMeshFace( &ws ); CHECK( rangeU->text().isEmpty() ); CHECK( rangeV->text().isEmpty() );
}

TEST_CASE( "Unwired UV operations use one collapsed icon group with honest availability", "[map][gui][toolpanels][face-clarity]" )
{
    session_t session; MapWorkspace_SetTool( &session.workspace, map_tool_t::TEXTURE );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) ); panel->resize( 300, 1000 ); panel->show(); QCoreApplication::processEvents();
    auto *planned = panel->findChild<QWidget *>( QStringLiteral( "MapTexturePlannedOperations" ) ); REQUIRE( planned );
    CHECK_FALSE( gui::EditorSection_IsExpanded( planned ) ); CHECK( planned->toolTip().contains( QStringLiteral( "not connected" ) ) );
    gui::EditorSection_SetExpanded( planned, true );
    for ( const char *id : { "map.texture.align_world", "map.texture.align_face", "map.texture.fit", "map.texture.shift", "map.texture.scale", "map.texture.rotate",
                            "map.texture.justify_left", "map.texture.justify_center", "map.texture.justify_right", "map.texture.justify_top", "map.texture.justify_bottom", "map.texture.unwrap" } ) {
        auto *button = Operation( *panel, id ); CHECK( button->toolButtonStyle() == Qt::ToolButtonIconOnly );
        CHECK_FALSE( button->icon().isNull() ); CHECK_FALSE( button->accessibleName().isEmpty() ); CHECK_FALSE( button->isEnabled() );
        CHECK( planned->isAncestorOf( button ) ); CHECK( MapToolProperties_Operations( panel.get() ).count( QString::fromLatin1( id ) ) == 1 );
    }
    CHECK( panel->findChild<QLineEdit *>( QStringLiteral( "MapFaceTextureSizeU" ) )->text().isEmpty() );
}

TEST_CASE( "Face gesture reference follows brush mesh and no-component selection without replacing controls", "[map][gui][toolpanels][face-clarity][gesture]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -32, -32, 0 } ); MapBounds_AddPoint( box, { 32, 32, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 brush = EditorSelection_At( &ws.selection, 0 );
    map_primitive_desc_t quad{}; quad.kind = map_primitive_kind_t::QUAD; quad.bounds = { { 128, 0, 0 }, { 256, 128, 0 } };
    REQUIRE( MapWorkspace_CreatePrimitive( &ws, quad ) ); const u64 mesh = EditorSelection_At( &ws.selection, 0 );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::FACES );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    QPointer<QComboBox> brushSelector = panel->findChild<QComboBox *>( QStringLiteral( "MapBrushFaceSelector" ) );
    QPointer<QComboBox> meshSelector = panel->findChild<QComboBox *>( QStringLiteral( "MapMeshFaceSelector" ) );
    QPointer<QLineEdit> state = panel->findChild<QLineEdit *>( QStringLiteral( "MapFaceTextureMaterial" ) );
    REQUIRE( brushSelector ); REQUIRE( meshSelector ); REQUIRE( state );
    const auto checkControls = [&]() {
        REQUIRE( brushSelector ); REQUIRE( meshSelector ); REQUIRE( state );
        CHECK( brushSelector.data() == panel->findChild<QComboBox *>( QStringLiteral( "MapBrushFaceSelector" ) ) );
        CHECK( meshSelector.data() == panel->findChild<QComboBox *>( QStringLiteral( "MapMeshFaceSelector" ) ) );
        CHECK( state.data() == panel->findChild<QLineEdit *>( QStringLiteral( "MapFaceTextureMaterial" ) ) );
    };
    const auto checkMeshReference = [&]( bool shown ) {
        CHECK( !ReferenceRowsContaining( *panel, "Mesh face: extrude" ).isEmpty() == shown );
        CHECK( !ReferenceRowsContaining( *panel, "Mesh face: inset" ).isEmpty() == shown );
        CHECK( !ReferenceRowsContaining( *panel, "Mesh face: slice a quad" ).isEmpty() == shown );
    };
    // The discovery reference remains available before choosing a component.
    CHECK_FALSE( MapWorkspace_HasMeshFace( &ws ) ); CHECK_FALSE( MapWorkspace_HasBrushFace( &ws ) ); checkMeshReference( true );
    CHECK( ReferenceRowsContaining( *panel, "move a selected object" ).isEmpty() );
    MapWorkspace_Select( &ws, brush, MAP_SELECT_REPLACE ); REQUIRE( brushSelector->count() == 7 );
    int top = -1; for ( int i = 1; i < brushSelector->count(); ++i ) { if ( brushSelector->itemText( i ).contains( QStringLiteral( "+Z" ) ) ) { top = i; } }
    REQUIRE( top > 0 );
    // This signal synchronously selects a brush face and refreshes the
    // reference table. The originating selector must remain alive.
    brushSelector->setCurrentIndex( top ); checkControls(); REQUIRE( MapWorkspace_HasBrushFace( &ws ) );
    checkMeshReference( false );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[LeftDrag] Brush face-normal handle: push / pull by the grid step (3D)" ) ) );
    CHECK( ReferenceRowsContaining( *panel, "move a selected object" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "Objects/Groups RGB arrow" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "Move handle: clone" ).isEmpty() );
    MapWorkspace_ClearBrushFace( &ws ); checkControls(); checkMeshReference( true );
    CHECK( ReferenceRowsContaining( *panel, "Brush face-normal handle" ).isEmpty() );
    MapWorkspace_Select( &ws, mesh, MAP_SELECT_REPLACE ); REQUIRE( meshSelector->count() == 2 );
    meshSelector->setCurrentIndex( 1 ); checkControls(); REQUIRE( MapWorkspace_HasMeshFace( &ws ) ); checkMeshReference( true );
    CHECK( ReferenceRowsContaining( *panel, "Brush face-normal handle" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "move a selected object" ).isEmpty() );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::OBJECTS ); checkControls();
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[LeftDrag] Objects/Groups: move a selected object" ) ) );
    CHECK( ReferenceRowsContaining( *panel, "Brush face-normal handle" ).isEmpty() );
}

TEST_CASE( "Navigation properties preserve inspection context without exposing editing controls", "[map][gui][toolpanels][neutral-tool][escape]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { 0, 0, 0 } ); MapBounds_AddPoint( box, { 64, 128, 32 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 object = EditorSelection_At( &ws.selection, 0 );
    const auto *solid = geometry::GeometryDocument_FindBrush( &ws.pDocument->geometry, { object } ); REQUIRE( solid ); REQUIRE( solid->sides.nCount > 0 );
    const u64 side = solid->sides.pData[0].sourceId.value;
    bool face = false;
    SECTION( "Root object selection is retained" ) { REQUIRE( ws.elementMode == map_element_mode_t::OBJECTS ); }
    SECTION( "Selected brush component is retained" ) {
        MapWorkspace_SelectBrushFace( &ws, object, side ); REQUIRE( MapWorkspace_HasBrushFace( &ws ) ); face = true;
    }
    const auto mode = ws.elementMode; const usize historySteps = EditorHistory_StepCount( &ws.history );
    const bool modified = MapWorkspace_IsModified( &ws ); const usize settingsListeners = session.gui.settings.nListeners;
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.tool.navigation" ) ) == command_result_t::OK );
    CHECK( ws.tool == map_tool_t::NONE ); CHECK( MapWorkspace_IsToolAvailable( map_tool_t::NONE ) );
    CHECK( MapToolProperties_Title( panel.get() ) == QStringLiteral( "Navigation" ) );
    auto *status = panel->findChild<QLabel *>( QStringLiteral( "MapToolNavigationStatus" ) ); REQUIRE( status );
    CHECK( status->text() == QStringLiteral( "No editing tool active. Selection is retained for inspection." ) );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolSelection" ) )->text().startsWith( QStringLiteral( "1 selected" ) ) );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolUnavailable" ) ) == nullptr );
    CHECK( MapToolProperties_Options( panel.get() ).isEmpty() ); CHECK( MapToolProperties_Operations( panel.get() ).isEmpty() );
    for ( const char *name : { "MapBrushFacePushPull", "MapBrushFaceApplyMaterial", "MapMeshFaceSelector", "MapPrimitive.box", "MapClipApply", "MapGeometryCreate" } ) {
        CHECK( panel->findChild<QWidget *>( QString::fromLatin1( name ) ) == nullptr );
    }
    CHECK( ReferenceRowsContaining( *panel, "Bounds handle" ).isEmpty() ); CHECK( ReferenceRowsContaining( *panel, "Move selection" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "Confirm current gesture" ).isEmpty() );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Shift+S] Resume editing with Select" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Escape] Clear preserved selection" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[LeftDrag / MiddleDrag / Space+LeftDrag] 2D: pan the view" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Wheel] 2D: zoom the view" ) ) );
    for ( const auto &[gesture, label] : {
              std::pair{ map_camera_gesture_t::LOOK, "3D: look around" }, std::pair{ map_camera_gesture_t::ORBIT, "3D: orbit the selection or view center" },
              std::pair{ map_camera_gesture_t::PAN, "3D: pan in the view plane" }, std::pair{ map_camera_gesture_t::DOLLY, "3D: move forward / backward" } } ) {
        const auto keys = MapInput_CameraGestureBindings( &ws, gesture ); REQUIRE_FALSE( keys.isEmpty() );
        CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[%1] %2" ).arg( keys.join( QStringLiteral( " / " ) ), QString::fromLatin1( label ) ) ) );
    }
    CHECK( EditorSelection_Count( &ws.selection ) == 1u ); CHECK( EditorSelection_Contains( &ws.selection, object ) );
    CHECK( ws.elementMode == mode ); CHECK( MapWorkspace_HasBrushFace( &ws ) == face );
    if ( face ) { CHECK( ws.selectedBrushFaceSide == side ); }
    CHECK( EditorHistory_StepCount( &ws.history ) == historySteps ); CHECK( MapWorkspace_IsModified( &ws ) == modified );
    CHECK( ( EditorCommands_State( &session.gui.commands, StringView_FromCString( "map.tool.navigation" ) ) & COMMAND_STATE_CHECKED ) != 0u );
    for ( const char *id : { "map.tool.select", "map.tool.camera", "map.tool.block", "map.tool.translate", "map.tool.rotate", "map.tool.scale", "map.tool.extrude", "map.tool.clip" } ) {
        CHECK( ( EditorCommands_State( &session.gui.commands, StringView_FromCString( id ) ) & COMMAND_STATE_CHECKED ) == 0u );
    }
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.tool.select" ) ) == command_result_t::OK );
    CHECK( MapToolProperties_Title( panel.get() ) == ( face ? QStringLiteral( "Face Editing" ) : QStringLiteral( "Selection Tool" ) ) );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolNavigationStatus" ) ) == nullptr );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Escape] Cancel drag; idle: enter Navigation (keep selection)" ) ) );
    CHECK( EditorSelection_Contains( &ws.selection, object ) ); CHECK( MapWorkspace_HasBrushFace( &ws ) == face );
    panel.reset(); CHECK( session.gui.settings.nListeners == settingsListeners );
}

TEST_CASE( "Navigation reference follows effective camera and reentry remaps", "[map][gui][toolpanels][neutral-tool][keyboard][help]" )
{
    session_t session; auto &ws = session.workspace; MapWorkspace_SetTool( &ws, map_tool_t::NONE );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QStringLiteral( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "navigation_help" bindings = {
    "map.tool.navigation" = { "map.tool.select" = [ "F9" ] "map.tool.cancel" = [ "F10" ] }
  }
  held = { "map.viewport.3d" = { "map.camera.forward" = [ "J" ] } }
  mouse = { "map.viewport.3d" = { "map.camera.look" = [ "RightDrag" ] "map.camera.pan" = [ "Ctrl+MiddleDrag" ] } }
}
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "navigation_help" ) ) == gui::editor_gui_status_t::OK );
    CHECK( ReferenceRowsContaining( *panel, "Resume editing with Select" ) == QStringList{ QStringLiteral( "[F9] Resume editing with Select" ) } );
    CHECK( ReferenceRowsContaining( *panel, "Clear preserved selection" ) == QStringList{ QStringLiteral( "[F10] Clear preserved selection" ) } );
    CHECK( ReferenceRowsContaining( *panel, "Camera forward" ) == QStringList{ QStringLiteral( "[J] Camera forward" ) } );
    CHECK( ReferenceRowsContaining( *panel, "3D: look around" ) == QStringList{ QStringLiteral( "[RightDrag] 3D: look around" ) } );
    CHECK( ReferenceRowsContaining( *panel, "3D: pan in the view plane" ) == QStringList{ QStringLiteral( "[Ctrl+MiddleDrag] 3D: pan in the view plane" ) } );
    CHECK( ReferenceRowsContaining( *panel, "Camera backward" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "3D: orbit" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "Select this tool" ).isEmpty() );
    // Updating the keymap refreshes the current controls, without returning
    // to an editing tool or keeping stale inherited activation hints.
    QPointer<QLabel> status = panel->findChild<QLabel *>( QStringLiteral( "MapToolNavigationStatus" ) ); REQUIRE( status );
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QStringLiteral( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "navigation_help_unbound" base = "navigation_help" bindings = {
    "map.tool.navigation" = { "map.tool.select" = [] "map.tool.cancel" = [] }
  }
}
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "navigation_help_unbound" ) ) == gui::editor_gui_status_t::OK );
    CHECK( ws.tool == map_tool_t::NONE ); REQUIRE( status ); CHECK( status.data() == panel->findChild<QLabel *>( QStringLiteral( "MapToolNavigationStatus" ) ) );
    CHECK( ReferenceRowsContaining( *panel, "Resume editing with Select" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "Clear preserved selection" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "Camera forward" ) == QStringList{ QStringLiteral( "[J] Camera forward" ) } );
}

TEST_CASE( "Tool properties cancellation bridge survives source deletion and rebuilt key controls", "[map][gui][toolpanels][neutral-tool][escape][cancel-bridge]" )
{
    session_t session; tool_cancel_probe_t probe{ &session.workspace };
    // This map-only fixture does not register Mason's application-owned
    // edit.select_all action. Use a genuine enabled map operation instead.
    MapWorkspace_Select( &session.workspace, 1000u, MAP_SELECT_REPLACE );
    REQUIRE( EditorSelection_Contains( &session.workspace.selection, 1000u ) );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) ); panel->resize( 320, 1000 );
    MapToolProperties_SetCancelHandler( panel.get(), &ProbeToolCancel, &probe );
    QPointer<QToolButton> button = Operation( *panel, "map.hide.selected" ); FocusToolControl( button );
    CHECK( SendToolKey( button, QEvent::ShortcutOverride ) ); CHECK( probe.overrides == 1 ); CHECK( probe.handled == 1 );
    CHECK( session.workspace.tool == map_tool_t::SELECT );
    probe.decline = true;
    CHECK_FALSE( SendToolKey( button, QEvent::ShortcutOverride ) ); CHECK( probe.overrides == 2 ); CHECK( probe.handled == 1 );
    probe.decline = false; probe.leaveOnPress = true;
    const int previousPresses = probe.presses;
    CHECK( SendToolKey( button, QEvent::KeyPress ) );
    CHECK( probe.presses == previousPresses + 1 ); CHECK( session.workspace.tool == map_tool_t::NONE ); CHECK( button.isNull() );
    auto *keys = panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ); REQUIRE( keys );
    CHECK( keys->editTriggers() == QAbstractItemView::NoEditTriggers ); FocusToolControl( keys );
    const int previousOverrides = probe.overrides;
    CHECK( SendToolKey( keys, QEvent::ShortcutOverride ) ); CHECK( probe.overrides == previousOverrides + 1 );
    MapToolProperties_SetCancelHandler( panel.get(), nullptr, nullptr );
    const int detachedOverrides = probe.overrides;
    ( void )SendToolKey( keys, QEvent::ShortcutOverride ); CHECK( probe.overrides == detachedOverrides );
    // The handler is replaceable without requiring another content rebuild.
    MapToolProperties_SetCancelHandler( panel.get(), &ProbeToolCancel, &probe );
    CHECK( SendToolKey( keys, QEvent::ShortcutOverride ) ); CHECK( probe.overrides == detachedOverrides + 1 );
}

TEST_CASE( "Tool cancellation bridge delegates effective remapping and explicit unbinding", "[map][gui][toolpanels][neutral-tool][escape][cancel-bridge][keyboard]" )
{
    session_t session; tool_cancel_probe_t probe{ &session.workspace };
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) ); panel->resize( 320, 1000 );
    MapToolProperties_SetCancelHandler( panel.get(), &ProbeToolCancel, &probe );
    auto *keys = panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ); REQUIRE( keys ); FocusToolControl( keys );
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QStringLiteral( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "bridge_cancel_remap" bindings = { "map.tool.select" = { "map.tool.cancel" = [ "F9" ] } } }
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "bridge_cancel_remap" ) ) == gui::editor_gui_status_t::OK );
    const int handled = probe.handled;
    ( void )SendToolKey( keys, QEvent::ShortcutOverride ); CHECK( probe.handled == handled );
    CHECK( SendToolKey( keys, QEvent::ShortcutOverride, Qt::Key_F9 ) ); CHECK( probe.handled == handled + 1 );
    CHECK( session.workspace.tool == map_tool_t::SELECT );
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QStringLiteral( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "bridge_cancel_unbound" base = "bridge_cancel_remap" bindings = { "map.tool.select" = { "map.tool.cancel" = [] } } }
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "bridge_cancel_unbound" ) ) == gui::editor_gui_status_t::OK );
    ( void )SendToolKey( keys, QEvent::ShortcutOverride, Qt::Key_F9 ); CHECK( probe.handled == handled + 1 );
    CHECK( keys == panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ) );
}

TEST_CASE( "Tool cancellation bridge excludes fields unfocused controls popups and dialogs", "[map][gui][toolpanels][neutral-tool][escape][cancel-bridge]" )
{
    session_t session; MapWorkspace_SetTool( &session.workspace, map_tool_t::BLOCK ); tool_cancel_probe_t probe{ &session.workspace };
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &session.workspace ) ); panel->resize( 360, 1600 );
    MapToolProperties_SetCancelHandler( panel.get(), &ProbeToolCancel, &probe );
    auto *button = ShapeButton( *panel, "box" );
    SECTION( "Actual text numeric and combo controls retain their own input" ) {
        auto *text = Setting<QLineEdit>( *panel, "editor.map.default_material" );
        auto *number = panel->findChild<QDoubleSpinBox *>( QStringLiteral( "MapBoxSizeX" ) ); REQUIRE( number );
        auto *combo = Setting<QComboBox>( *panel, "editor.map.primitive_axis" );
        for ( QWidget *field : { static_cast<QWidget *>( text ), static_cast<QWidget *>( number ), static_cast<QWidget *>( combo ) } ) {
            FocusToolControl( field, false ); const int calls = probe.overrides + probe.presses;
            QWidget *focused = QApplication::focusWidget();
            ( void )SendToolKey( focused, QEvent::ShortcutOverride ); ( void )SendToolKey( focused, QEvent::KeyPress );
            CHECK( probe.overrides + probe.presses == calls ); CHECK( session.workspace.tool == map_tool_t::BLOCK );
        }
        FocusToolControl( text ); const int calls = probe.overrides + probe.presses;
        ( void )SendToolKey( button, QEvent::ShortcutOverride ); CHECK( probe.overrides + probe.presses == calls );
    }
    SECTION( "Popup Escape is not forwarded to the geometric pane" ) {
        FocusToolControl( button ); QMenu popup( panel.get() ); popup.addAction( QStringLiteral( "Popup action" ) ); popup.popup( button->mapToGlobal( QPoint( 0, button->height() ) ) );
        QCoreApplication::processEvents(); REQUIRE( QApplication::activePopupWidget() == &popup );
        const int calls = probe.overrides + probe.presses;
        ( void )SendToolKey( button, QEvent::ShortcutOverride ); ( void )SendToolKey( button, QEvent::KeyPress );
        CHECK( probe.overrides + probe.presses == calls ); CHECK( session.workspace.tool == map_tool_t::BLOCK ); popup.close();
    }
    SECTION( "Modal dialog Escape is not forwarded to the geometric pane" ) {
        FocusToolControl( button ); QDialog dialog( panel.get() ); dialog.setModal( true ); dialog.show();
        QCoreApplication::processEvents(); REQUIRE( QApplication::activeModalWidget() == &dialog );
        const int calls = probe.overrides + probe.presses;
        ( void )SendToolKey( button, QEvent::ShortcutOverride ); ( void )SendToolKey( button, QEvent::KeyPress );
        CHECK( probe.overrides + probe.presses == calls ); CHECK( session.workspace.tool == map_tool_t::BLOCK ); dialog.close();
    }
}

TEST_CASE( "Select properties show each editing profile without selected geometry", "[map][gui][toolpanels][selection-profile]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    // Mason supplies the numeric-transform command used by the Object and
    // Group profiles. Model that application-owned descriptor in this map-
    // only fixture; real command execution is covered by Mason's suite.
    RegisterReferenceCommand( session, "map.transform.dialog", "Transform Selection" );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    panel->resize( 310, 900 ); panel->show(); QCoreApplication::processEvents();
    const usize listeners = session.gui.settings.nListeners;
    const usize mapListeners = ws.nListeners;
    auto *texture = panel->findChild<QWidget *>( QStringLiteral( "MapFaceTextureSection" ) ); REQUIRE( texture );
    auto *brush = panel->findChild<QWidget *>( QStringLiteral( "MapBrushFaceControls" ) ); REQUIRE( brush );
    auto *mesh = panel->findChild<QWidget *>( QStringLiteral( "MapMeshFaceControls" ) ); REQUIRE( mesh );
    struct profile_t { const char *command; const char *title; const char *section; map_element_mode_t mode; };
    const profile_t profiles[]{
        { "map.select_mode.vertices", "Vertex Editing", "MapToolModeVertices", map_element_mode_t::VERTICES },
        { "map.select_mode.edges", "Edge Editing", "MapToolModeEdges", map_element_mode_t::EDGES },
        { "map.select_mode.faces", "Face Editing", "MapToolModeFaces", map_element_mode_t::FACES },
        { "map.select_mode.meshes", "Mesh Editing", "MapToolModeMeshes", map_element_mode_t::MESHES },
        { "map.select_mode.objects", "Selection Tool", "MapToolModeObjects", map_element_mode_t::OBJECTS },
        { "map.select_mode.groups", "Selection Tool", "MapToolModeGroups", map_element_mode_t::GROUPS },
    };
    for ( const auto &profile : profiles ) {
        CAPTURE( profile.command );
        REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( profile.command ) ) == command_result_t::OK );
        CHECK( ws.tool == map_tool_t::SELECT ); CHECK( ws.elementMode == profile.mode ); CHECK( EditorSelection_Count( &ws.selection ) == 0 );
        CHECK( MapToolProperties_Title( panel.get() ) == QString::fromUtf8( profile.title ) );
        for ( const auto &candidate : profiles ) {
            auto *section = panel->findChild<QWidget *>( QString::fromUtf8( candidate.section ) ); REQUIRE( section );
            CHECK( section->isHidden() == ( candidate.mode != profile.mode ) );
        }
        const bool faces = profile.mode == map_element_mode_t::FACES;
        const bool roots = profile.mode == map_element_mode_t::OBJECTS || profile.mode == map_element_mode_t::GROUPS || profile.mode == map_element_mode_t::MESHES;
        CHECK( texture->isHidden() == !faces ); CHECK( brush->parentWidget()->isHidden() == !faces ); CHECK( mesh->parentWidget()->isHidden() == !faces );
        CHECK( MapToolProperties_Options( panel.get() ).contains( QStringLiteral( "editor.map.resize_mode" ) ) == roots );
        CHECK( MapToolProperties_Options( panel.get() ).contains( QStringLiteral( "editor.map.mesh_extrude_distance" ) ) == faces );
        CHECK( MapToolProperties_Operations( panel.get() ).contains( QStringLiteral( "map.mesh.extrude" ) ) == faces );
        CHECK( MapToolProperties_Operations( panel.get() ).contains( QStringLiteral( "map.transform.dialog" ) ) == roots );
        CHECK( session.gui.settings.nListeners == listeners ); CHECK( ws.nListeners == mapListeners );
    }
    // Navigation retains the mode but exposes none of its editing controls.
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.tool.navigation" ) ) == command_result_t::OK );
    CHECK( MapToolProperties_Title( panel.get() ) == QStringLiteral( "Navigation" ) );
    CHECK( MapToolProperties_Options( panel.get() ).isEmpty() ); CHECK( MapToolProperties_Operations( panel.get() ).isEmpty() );
    CHECK( panel->findChild<QWidget *>( QStringLiteral( "MapToolModeEdges" ) ) == nullptr );
    CHECK( panel->findChild<QWidget *>( QStringLiteral( "MapBrushFaceControls" ) ) == nullptr );
}

TEST_CASE( "Edge editing exposes the named operations without substituting face extrusion", "[map][gui][toolpanels][selection-profile]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.select_mode.edges" ) ) == command_result_t::OK );
    auto *section = panel->findChild<QWidget *>( QStringLiteral( "MapToolModeEdges" ) ); REQUIRE( section ); CHECK_FALSE( section->isHidden() );
    const std::pair<const char *, const char *> operations[]{
        { "map.mesh.dissolve", "Dissolve" }, { "map.mesh.collapse", "Collapse" }, { "map.mesh.bevel", "Bevel" },
        { "map.mesh.extrude_edges", "Extrude" }, { "map.mesh.connect_edges", "Connect" }, { "map.mesh.extend_edges", "Extend" },
        { "map.mesh.merge", "Merge" }, { "map.mesh.split_edges", "Split" }, { "map.mesh.snap_edge_to_edge", "Snap Edge to Edge" },
        { "map.mesh.fill_hole", "Fill Hole" }, { "map.mesh.bridge", "Bridge" }, { "map.mesh.normals_hard", "Hard Normals" },
        { "map.mesh.normals_soft", "Soft Normals" }, { "map.mesh.normals_default", "Default Normals" }, { "map.texture.weld_uvs", "Weld UVs" },
        { "map.select.loop", "Select Loop" }, { "map.select.ring", "Select Ring" }, { "map.select.ribs", "Select Ribs" },
        { "map.pivot.clear", "Clear Pivot" }, { "map.tool.edge_cut", "Edge Cut Tool" }, { "map.tool.edge_arc", "Edge Arc Tool" },
        { "map.mesh.radial_align", "Radial Align" },
    };
    REQUIRE( std::size( operations ) == 22 );
    CHECK( MapToolProperties_Operations( panel.get() ).size() == 24 ); // 22 actions plus the two common view-centering commands.
    const usize steps = EditorHistory_StepCount( &ws.history );
    for ( const auto &[id, label] : operations ) {
        CAPTURE( id ); auto *button = Operation( *section, id );
        CHECK( button->text() == QString::fromUtf8( label ) ); CHECK( button->accessibleName() == button->text() );
        CHECK( button->toolButtonStyle() == Qt::ToolButtonTextBesideIcon ); CHECK_FALSE( button->icon().isNull() );
        CHECK_FALSE( button->isEnabled() ); CHECK( button->toolTip().contains( QStringLiteral( "Unavailable" ) ) );
        CHECK( MapToolProperties_Operations( panel.get() ).count( QString::fromUtf8( id ) ) == 1 );
        button->click(); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    }
    CHECK_FALSE( MapToolProperties_Operations( panel.get() ).contains( QStringLiteral( "map.mesh.extrude" ) ) );
    CHECK_FALSE( MapToolProperties_Operations( panel.get() ).contains( QStringLiteral( "map.mesh.inset" ) ) );
    CHECK_FALSE( MapToolProperties_Options( panel.get() ).contains( QStringLiteral( "editor.map.hollow_thickness" ) ) );
    auto *note = section->findChild<QLabel *>( QStringLiteral( "MapToolModeAvailability" ) ); REQUIRE( note );
    CHECK( note->text().contains( QStringLiteral( "planned" ) ) );
    CHECK( note->text().contains( QStringLiteral( "Select Loop or Select Ring" ) ) );
    CHECK( note->text().contains( QStringLiteral( "Convert brushes to meshes first" ) ) );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolSelection" ) )->text() == QStringLiteral( "0 edges selected · Edges mode" ) );
    gui::EditorSection_SetExpanded( section, false );
    CHECK( MapToolProperties_Operations( panel.get() ).count( QStringLiteral( "map.mesh.extrude_edges" ) ) == 1 );
}

TEST_CASE( "Edge properties enable mesh selection queries with persistent controls and accurate component counts", "[map][gui][toolpanels][selection-profile][mesh-edge][lifetime]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 object = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    const auto *source = geometry::GeometryDocument_FindMesh( &ws.pDocument->geometry, { object } ); REQUIRE( source );
    geometry::mesh_source_description_t description{};
    REQUIRE( geometry::MeshSourceDescription_Init( &description, ws.pDocument->pAllocator, source->sourceId ) == geometry::geometry_status_t::OK );
    REQUIRE( geometry::MeshSource_TryDescribe( source, &description ) == geometry::geometry_status_t::OK );
    REQUIRE( description.faces.nCount != 0 ); const auto &face = description.faces.pData[0]; REQUIRE( face.cCorners >= 3 );
    const auto edge = geometry::MeshEdgeRef_Make(
        description.vertices.pData[description.corners.pData[face.iFirstCorner].iVertex].sourceId,
        description.vertices.pData[description.corners.pData[face.iFirstCorner + 1].iVertex].sourceId );
    geometry::MeshSourceDescription_Shutdown( &description );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.select_mode.edges" ) ) == command_result_t::OK );
    QPointer<QWidget> section = panel->findChild<QWidget *>( QStringLiteral( "MapToolModeEdges" ) ); REQUIRE( section );
    QPointer<QToolButton> loop = Operation( *section, "map.select.loop" );
    QPointer<QToolButton> ring = Operation( *section, "map.select.ring" );
    QPointer<QLabel> count = panel->findChild<QLabel *>( QStringLiteral( "MapToolSelection" ) ); REQUIRE( count );
    QPointer<QTreeWidget> keys = panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ); REQUIRE( keys );
    CHECK_FALSE( loop->isEnabled() ); CHECK_FALSE( ring->isEnabled() );
    REQUIRE( MapWorkspace_SelectMeshEdge( &ws, object, edge ) );
    CHECK( loop->isEnabled() ); CHECK( ring->isEnabled() );
    CHECK( count->text() == QStringLiteral( "1 edge selected · Edges mode" ) );
    CHECK_FALSE( loop->toolTip().contains( QStringLiteral( "Unavailable" ) ) );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolTitle" ) )->toolTip().contains( QStringLiteral( "last picked edge" ) ) );
    const usize steps = EditorHistory_StepCount( &ws.history );
    ws.pDocument->bReadOnly = CY_TRUE; MapWorkspace_Notify( &ws, MAP_CHANGE_DOCUMENT );
    REQUIRE( loop->isEnabled() ); REQUIRE( ring->isEnabled() );
    for ( const char *id : { "map.select.loop", "map.select.ring" } ) {
        REQUIRE( MapWorkspace_SelectMeshEdge( &ws, object, edge ) );
        Operation( *section, id )->click(); REQUIRE( MapWorkspace_HasMeshEdges( &ws ) );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps );
        const usize edges = ws.meshSelection.edges.nCount;
        CHECK( count->text() == ( edges == 1 ? QStringLiteral( "%1 edge selected · Edges mode" ) :
            QStringLiteral( "%1 edges selected · Edges mode" ) ).arg( edges ) );
        CHECK( section.data() == panel->findChild<QWidget *>( QStringLiteral( "MapToolModeEdges" ) ) );
        CHECK( loop.data() == Operation( *section, "map.select.loop" ) );
        CHECK( ring.data() == Operation( *section, "map.select.ring" ) );
        CHECK( keys.data() == panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ) );
    }
    CHECK( ws.meshSelection.edges.nCount > 1 ); // The quad cube's ring includes opposite edges.
    CHECK_FALSE( Operation( *section, "map.mesh.extrude_edges" )->isEnabled() );
    CHECK_FALSE( Operation( *section, "map.mesh.bevel" )->isEnabled() );
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QStringLiteral( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "mesh_edge_panel" bindings = { "map.selection.edges" = { "map.select.loop" = [ "F9" ] "map.select.ring" = [ "F10" ] } } }
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "mesh_edge_panel" ) ) == gui::editor_gui_status_t::OK );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[F9] Select Loop" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[F10] Select Ring" ) ) );
    CHECK( ReferenceRowsContaining( *panel, "Select Loop (planned)" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "Select Ring (planned)" ).isEmpty() );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Shift+LeftClick] Add an edge on the same mesh" ) ) );
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QStringLiteral( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "mesh_edge_panel_unbound" bindings = { "map.selection.edges" = { "map.select.loop" = [] "map.select.ring" = [] } } }
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "mesh_edge_panel_unbound" ) ) == gui::editor_gui_status_t::OK );
    CHECK( ReferenceRowsContaining( *panel, "Select Loop" ).isEmpty() );
    CHECK( ReferenceRowsContaining( *panel, "Select Ring" ).isEmpty() );
    const usize retained = ws.meshSelection.edges.nCount;
    MapWorkspace_SetTool( &ws, map_tool_t::NONE );
    CHECK( MapWorkspace_HasMeshEdges( &ws ) ); CHECK( ws.meshSelection.edges.nCount == retained );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolSelection" ) )->text() == QStringLiteral( "%1 edges selected · Edges mode" ).arg( retained ) );
    MapWorkspace_SetTool( &ws, map_tool_t::SELECT );
    REQUIRE( MapWorkspace_HasMeshEdges( &ws ) ); REQUIRE( Operation( *panel, "map.select.loop" )->isEnabled() );
    MapWorkspace_SetVisgroupHidden( &ws, map_visgroup_t::MESHES, CY_TRUE );
    CHECK_FALSE( Operation( *panel, "map.select.loop" )->isEnabled() );
    CHECK_FALSE( Operation( *panel, "map.select.ring" )->isEnabled() );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolSelection" ) )->text() == QStringLiteral( "0 edges selected · Edges mode" ) );
}

TEST_CASE( "Vertex properties describe working picking and count components without enabling topology edits", "[map][gui][toolpanels][selection-profile][mesh-vertex][lifetime]" )
{
    session_t session; auto &ws = session.workspace; REQUIRE( MapWorkspace_New( &ws ) == map_status_t::OK );
    map_bounds_t box{}; MapBounds_AddPoint( box, { -64, -64, 0 } ); MapBounds_AddPoint( box, { 64, 64, 128 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 object = EditorSelection_At( &ws.selection, 0 );
    REQUIRE( MapWorkspace_ConvertBrushSelection( &ws ) );
    const auto *source = geometry::GeometryDocument_FindMesh( &ws.pDocument->geometry, { object } ); REQUIRE( source );
    geometry::mesh_source_description_t description{};
    REQUIRE( geometry::MeshSourceDescription_Init( &description, ws.pDocument->pAllocator, source->sourceId ) == geometry::geometry_status_t::OK );
    REQUIRE( geometry::MeshSource_TryDescribe( source, &description ) == geometry::geometry_status_t::OK );
    REQUIRE( description.vertices.nCount >= 2 );
    const auto first = description.vertices.pData[0].sourceId; const auto second = description.vertices.pData[1].sourceId;
    geometry::MeshSourceDescription_Shutdown( &description );
    std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.select_mode.vertices" ) ) == command_result_t::OK );
    QPointer<QWidget> section = panel->findChild<QWidget *>( QStringLiteral( "MapToolModeVertices" ) ); REQUIRE( section );
    QPointer<QLabel> count = panel->findChild<QLabel *>( QStringLiteral( "MapToolSelection" ) ); REQUIRE( count );
    QPointer<QTreeWidget> keys = panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ); REQUIRE( keys );
    CHECK( count->text() == QStringLiteral( "0 vertices selected · Vertices mode" ) );
    auto *note = section->findChild<QLabel *>( QStringLiteral( "MapToolModeAvailability" ) ); REQUIRE( note );
    CHECK( note->text().contains( QStringLiteral( "Pick mesh vertices" ) ) );
    CHECK( note->text().contains( QStringLiteral( "Convert brushes to meshes first" ) ) );
    CHECK( note->text().contains( QStringLiteral( "topology edits are planned" ) ) );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolTitle" ) )->toolTip().contains( QStringLiteral( "does not move the parent mesh" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[LeftClick] Pick an authored mesh vertex" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Shift+LeftClick] Add a vertex on the same mesh" ) ) );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[Ctrl/Command+LeftClick] Toggle a vertex on the same mesh" ) ) );
    const auto *document = ws.pDocument; const usize steps = EditorHistory_StepCount( &ws.history );
    ws.pDocument->bReadOnly = CY_TRUE; MapWorkspace_Notify( &ws, MAP_CHANGE_DOCUMENT );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, first ) );
    CHECK( count->text() == QStringLiteral( "1 vertex selected · Vertices mode" ) );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, second, MAP_SELECT_ADD ) );
    CHECK( count->text() == QStringLiteral( "2 vertices selected · Vertices mode" ) );
    CHECK( section.data() == panel->findChild<QWidget *>( QStringLiteral( "MapToolModeVertices" ) ) );
    CHECK( keys.data() == panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ) );
    CHECK( count.data() == panel->findChild<QLabel *>( QStringLiteral( "MapToolSelection" ) ) );
    for ( const char *id : { "map.mesh.merge", "map.mesh.collapse", "map.mesh.bevel", "map.mesh.dissolve", "map.mesh.fill_hole",
                            "map.select.grow", "map.select.shrink", "map.pivot.clear" } ) {
        CAPTURE( id ); auto *button = Operation( *section, id ); CHECK_FALSE( button->isEnabled() );
        CHECK( button->toolTip().contains( QStringLiteral( "Unavailable" ) ) );
        button->click(); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    }
    CHECK( ws.pDocument == document ); CHECK( ws.meshSelection.vertices.nCount == 2 );
    MapWorkspace_SetTool( &ws, map_tool_t::NONE );
    CHECK( MapWorkspace_HasMeshVertices( &ws ) ); CHECK( ws.meshSelection.vertices.nCount == 2 );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolSelection" ) )->text() == QStringLiteral( "2 vertices selected · Vertices mode" ) );
    MapWorkspace_SetTool( &ws, map_tool_t::SELECT );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, second, MAP_SELECT_TOGGLE ) );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolSelection" ) )->text() == QStringLiteral( "1 vertex selected · Vertices mode" ) );
    MapWorkspace_ClearMeshVertices( &ws );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolSelection" ) )->text() == QStringLiteral( "0 vertices selected · Vertices mode" ) );
    REQUIRE( MapWorkspace_SelectMeshVertex( &ws, object, first ) );
    MapWorkspace_SetElementMode( &ws, map_element_mode_t::EDGES );
    CHECK( ws.meshSelection.vertices.nCount == 0 );
    CHECK( panel->findChild<QLabel *>( QStringLiteral( "MapToolSelection" ) )->text() == QStringLiteral( "0 edges selected · Edges mode" ) );
}

TEST_CASE( "Selection profiles preserve control drafts and refresh contextual keymaps in place", "[map][gui][toolpanels][selection-profile][lifetime]" )
{
    session_t session; auto &ws = session.workspace; std::unique_ptr<QWidget> panel( MapToolProperties_Create( nullptr, &ws ) );
    QPointer<QTreeWidget> keys = panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ); REQUIRE( keys );
    QPointer<QComboBox> selector = panel->findChild<QComboBox *>( QStringLiteral( "MapMeshFaceSelector" ) ); REQUIRE( selector );
    QPointer<QDoubleSpinBox> distance = Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_extrude_distance" );
    QPointer<QDoubleSpinBox> inset = Setting<QDoubleSpinBox>( *panel, "editor.map.mesh_inset_distance" );
    QPointer<QWidget> edge = panel->findChild<QWidget *>( QStringLiteral( "MapToolModeEdges" ) ); REQUIRE( edge );
    distance->setValue( 48 ); inset->setValue( 12 ); const usize listeners = session.gui.settings.nListeners;
    for ( const char *command : { "map.select_mode.faces", "map.select_mode.edges", "map.select_mode.vertices", "map.select_mode.meshes", "map.select_mode.objects", "map.select_mode.groups", "map.select_mode.faces" } ) {
        REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( command ) ) == command_result_t::OK );
        REQUIRE( keys ); REQUIRE( selector ); REQUIRE( distance ); REQUIRE( inset ); REQUIRE( edge );
        CHECK( keys.data() == panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ) );
        CHECK( selector.data() == panel->findChild<QComboBox *>( QStringLiteral( "MapMeshFaceSelector" ) ) );
        CHECK( distance->value() == 48 ); CHECK( inset->value() == 12 ); CHECK( session.gui.settings.nListeners == listeners );
    }
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QStringLiteral( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "edge_profile_keys" bindings = { "map.selection.edges" = { "map.mesh.extrude_edges" = [ "F9" ] } } }
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "edge_profile_keys" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( EditorCommands_ExecuteLine( &session.gui.commands, StringView_FromCString( "map.select_mode.edges" ) ) == command_result_t::OK );
    CHECK( MapToolProperties_KeyRows( panel.get() ).contains( QStringLiteral( "[F9] Extrude (planned)" ) ) );
    CHECK( keys.data() == panel->findChild<QTreeWidget *>( QStringLiteral( "MapToolKeys" ) ) ); CHECK( distance->value() == 48 );
    REQUIRE( gui::EditorGui_AddKeymap( &session.gui, QStringLiteral( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "edge_profile_unbound" bindings = { "map.selection.edges" = { "map.mesh.extrude_edges" = [] } } }
)cykv" ) ) == gui::editor_gui_status_t::OK );
    REQUIRE( gui::EditorGui_SelectKeymap( &session.gui, StringView_FromCString( "edge_profile_unbound" ) ) == gui::editor_gui_status_t::OK );
    CHECK( ReferenceRowsContaining( *panel, "Extrude (planned)" ).isEmpty() );
    REQUIRE( keys ); REQUIRE( distance ); REQUIRE( selector ); REQUIRE( edge ); CHECK( distance->value() == 48 );
}
