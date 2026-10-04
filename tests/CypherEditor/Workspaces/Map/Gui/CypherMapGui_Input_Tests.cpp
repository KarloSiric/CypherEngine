//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherMapGui_Input_Tests.cpp
//  Purpose: Verifies viewport binding precedence, preflight, user overrides,
//           and the boundary between camera movement and editor commands.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherMapGui_Input.h"
#include "CypherMapGui_Workspace.h"

#include "CypherCommon/Tier1/CypherCommon_Allocator.h"
#include "CypherEditor/Geometry/Document/CypherGeometry_DocumentBrushAttributes.h"

#include <catch2/catch_test_macros.hpp>

#include <QKeyEvent>
#include <QApplication>
#include <QMouseEvent>
#include <QWheelEvent>

#include <array>

using namespace cypher::common;
using namespace cypher::editor;
using namespace cypher::editor::map;

namespace
{

struct execution_t {
    int nCalls{ 0 };
    bool enabled{ true };
    command_result_t result{ command_result_t::OK };
};

command_result_t Execute( void *pContext, const command_args_t &args )
{
    CHECK( args.nArgs == 0u );
    auto &execution = *static_cast<execution_t *>( pContext );
    ++execution.nCalls;
    return execution.result;
}

u32 State( void *pContext )
{
    return static_cast<const execution_t *>( pContext )->enabled ? COMMAND_STATE_ENABLED : COMMAND_STATE_NONE;
}

void Observe( void *pContext, const command_desc_t &, const command_args_t &, command_result_t )
{
    ++*static_cast<int *>( pContext );
}

struct input_fixture_t {
    cypher::editor::gui::editor_gui_t gui{};
    settings_document_t base{}, user{};
    map_workspace_t workspace{};
    std::array<execution_t, 8> executions{};
    int nObserved{ 0 };

    explicit input_fixture_t( const char *pKeymap )
    {
        REQUIRE( EditorCommands_Init( &gui.commands, Allocator_GetSystem() ) == command_registry_status_t::OK );
        REQUIRE( SettingsDocument_Init( &base, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
        REQUIRE( SettingsDocument_Init( &user, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
        REQUIRE( SettingsDocument_Load( &base, StringView_FromCString( pKeymap ) ).status == settings_document_status_t::OK );
        gui.keymapChain[0] = SettingsDocument_Root( &base );
        gui.nKeymapChain = 1u;
        workspace.pGui = &gui;
        constexpr const char *ids[]{ "test.tool", "test.family3d", "test.family2d", "test.viewport", "test.map", "test.global",
                                    "test.disabled", "test.failed" };
        std::array<command_desc_t, 8> commands{};
        for ( usize i = 0u; i < commands.size(); ++i ) {
            commands[i].pId = ids[i];
            commands[i].pLabel = ids[i];
            commands[i].pfnExecute = Execute;
            commands[i].pfnState = State;
            commands[i].pContext = &executions[i];
        }
        executions[6].enabled = false;
        executions[7].result = command_result_t::FAILED;
        REQUIRE( EditorCommands_Register( &gui.commands, commands.data(), commands.size() ) == command_registry_status_t::OK );
        EditorCommands_SetObserver( &gui.commands, Observe, &nObserved );
    }

    ~input_fixture_t()
    {
        workspace.pGui = nullptr;
        EditorCommands_Shutdown( &gui.commands );
    }

    void Override( const char *pText )
    {
        REQUIRE( SettingsDocument_Load( &user, StringView_FromCString( pText ) ).status == settings_document_status_t::OK );
        gui.keymapChain[0] = SettingsDocument_Root( &user );
        gui.keymapChain[1] = SettingsDocument_Root( &base );
        gui.nKeymapChain = 2u;
    }

    int Calls( const char *pCommand ) const
    {
        const command_desc_t *pCommandDesc = EditorCommands_Find( &gui.commands, StringView_FromCString( pCommand ) );
        REQUIRE( pCommandDesc != nullptr );
        return static_cast<const execution_t *>( pCommandDesc->pContext )->nCalls;
    }
};

constexpr const char *kContextKeymap = R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{
    id = "input_tests"
    bindings = {
        "map.tool.vertex" = { "test.tool" = [ "C" ] }
        "map.viewport.3d" = { "test.family3d" = [ "C" ] }
        "map.viewport.2d" = { "test.family2d" = [ "C" ] }
        "map.viewport" = { "test.viewport" = [ "V" ] }
        map = { "test.map" = [ "M" ] }
        global = {
            "test.global" = [ "C", "Ctrl+S", "E" ]
            "test.disabled" = [ "D" ]
            "test.failed" = [ "F" ]
            "future.unregistered" = [ "U" ]
        }
    }
    held = { "map.viewport.3d" = {
        "map.camera.forward" = [ "W", "Up" ]
        "map.camera.up" = [ "E" ]
        "map.camera.fast" = [ "Shift" ]
        "map.camera.slow" = [ "Alt" ]
    } }
}
)cykv";

constexpr const char *kCameraKeymap = R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests"
  mouse = { "map.viewport.3d" = {
    "map.camera.look" = [ "RightDrag", "Alt+MiddleDrag" ]
    "map.camera.orbit" = [ "Alt+LeftDrag" ]
    "map.camera.pan" = [ "MiddleDrag", "Space+LeftDrag" ]
    "map.camera.dolly" = [ "Alt+RightDrag", "Wheel" ]
  } }
  held = { "map.viewport.3d" = {
    "map.camera.forward" = [ "W", "Up" ]
    "map.camera.up" = [ "E" ]
    "map.camera.fast" = [ "Shift" ]
    "map.camera.slow" = [ "Alt" ]
  } }
}
)cykv";

map_camera_gesture_t CameraDrag( const map_workspace_t *workspace, Qt::MouseButton button,
                               Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool spaceHeld = false )
{
    QMouseEvent event( QEvent::MouseButtonPress, QPointF( 12, 24 ), QPointF( 12, 24 ), button, button, modifiers );
    return MapInput_CameraDragGesture( workspace, &event, spaceHeld );
}

bool CameraWheel( const map_workspace_t *workspace, QPoint angle = { 0, 120 },
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier, QPoint pixel = {} )
{
    QWheelEvent event( QPointF( 12, 24 ), QPointF( 12, 24 ), pixel, angle, Qt::NoButton, modifiers, Qt::NoScrollPhase, false );
    return MapInput_CameraWheelGesture( workspace, &event );
}

struct default_input_fixture_t {
    gui::editor_gui_t gui{};
    map_workspace_t workspace{};
    settings_document_t settings{};
    default_input_fixture_t()
    {
        auto *app = qobject_cast<QApplication *>( QCoreApplication::instance() );
        REQUIRE( gui::EditorGui_Init( &gui, app, Allocator_GetSystem() ) == gui::editor_gui_status_t::OK );
        REQUIRE( MapWorkspace_Init( &workspace, &gui ) );
        REQUIRE( MapWorkspace_RegisterCommands( &workspace, &gui.commands ) == command_registry_status_t::OK );
        REQUIRE( SettingsDocument_Init( &settings, Allocator_GetSystem(), EditorSettings_FileIdentity() ) == settings_document_status_t::OK );
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, &settings );
    }
    ~default_input_fixture_t()
    {
        EditorSettings_SetScope( &gui.settings, settings_scope_t::USER, nullptr );
        MapWorkspace_Shutdown( &workspace );
        gui::EditorGui_Shutdown( &gui );
    }
};

} // namespace

TEST_CASE( "Camera gesture resolution prefers constrained drags and keeps command modifiers explicit", "[map][gui][input][camera]" )
{
    input_fixture_t f( kCameraKeymap );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::LOOK );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::ShiftModifier ) == map_camera_gesture_t::LOOK );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::AltModifier ) == map_camera_gesture_t::DOLLY );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::AltModifier | Qt::ShiftModifier ) == map_camera_gesture_t::DOLLY );
    CHECK( CameraDrag( &f.workspace, Qt::MiddleButton ) == map_camera_gesture_t::PAN );
    CHECK( CameraDrag( &f.workspace, Qt::MiddleButton, Qt::AltModifier ) == map_camera_gesture_t::LOOK );
    CHECK( CameraDrag( &f.workspace, Qt::LeftButton ) == map_camera_gesture_t::NONE );
    CHECK( CameraDrag( &f.workspace, Qt::LeftButton, Qt::AltModifier ) == map_camera_gesture_t::ORBIT );
    CHECK( CameraDrag( &f.workspace, Qt::LeftButton, Qt::NoModifier, true ) == map_camera_gesture_t::PAN );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::ControlModifier ) == map_camera_gesture_t::NONE );
    CHECK( CameraDrag( &f.workspace, Qt::MiddleButton, Qt::MetaModifier ) == map_camera_gesture_t::NONE );
    CHECK( CameraDrag( &f.workspace, Qt::NoButton ) == map_camera_gesture_t::NONE );
    CHECK( CameraDrag( nullptr, Qt::RightButton ) == map_camera_gesture_t::NONE );
    CHECK( MapInput_CameraDragGesture( &f.workspace, nullptr, false ) == map_camera_gesture_t::NONE );
    CHECK( f.nObserved == 0 );
}

TEST_CASE( "Camera mouse contexts override less-specific views and unknown actions reserve gestures", "[map][gui][input][camera]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" mouse = {
  "map.tool.block" = { "map.camera.pan" = [ "RightDrag" ] "future.unknown" = [ "Alt+MiddleDrag" ] }
  "map.viewport.3d" = { "map.camera.look" = [ "RightDrag", "MiddleDrag" ] }
  "map.viewport" = { "map.camera.orbit" = [ "LeftDrag" ] }
  map = { "map.camera.dolly" = [ "BackDrag" ] }
  global = { "map.camera.pan" = [ "ForwardDrag" ] }
} }
)cykv" );
    f.workspace.tool = map_tool_t::BLOCK;
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::PAN );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::AltModifier ) == map_camera_gesture_t::PAN );
    CHECK( CameraDrag( &f.workspace, Qt::MiddleButton, Qt::AltModifier ) == map_camera_gesture_t::NONE );
    CHECK( CameraDrag( &f.workspace, Qt::MiddleButton ) == map_camera_gesture_t::LOOK );
    CHECK( CameraDrag( &f.workspace, Qt::LeftButton ) == map_camera_gesture_t::ORBIT );
    CHECK( CameraDrag( &f.workspace, Qt::BackButton ) == map_camera_gesture_t::DOLLY );
    CHECK( CameraDrag( &f.workspace, Qt::ForwardButton ) == map_camera_gesture_t::PAN );
    f.workspace.tool = map_tool_t::SELECT;
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::LOOK );
    CHECK( CameraDrag( &f.workspace, Qt::MiddleButton, Qt::AltModifier ) == map_camera_gesture_t::LOOK );
}

TEST_CASE( "Held Space and modifiers make mouse chords more specific within one context", "[map][gui][input][camera]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" mouse = { "map.viewport.3d" = {
  "map.camera.pan" = [ "LeftDrag" ]
  "map.camera.orbit" = [ "Space+LeftDrag" ]
  "map.camera.dolly" = [ "Alt+Space+LeftDrag" ]
} } }
)cykv" );
    CHECK( CameraDrag( &f.workspace, Qt::LeftButton ) == map_camera_gesture_t::PAN );
    CHECK( CameraDrag( &f.workspace, Qt::LeftButton, Qt::NoModifier, true ) == map_camera_gesture_t::ORBIT );
    CHECK( CameraDrag( &f.workspace, Qt::LeftButton, Qt::AltModifier, true ) == map_camera_gesture_t::DOLLY );
    CHECK( CameraDrag( &f.workspace, Qt::LeftButton, Qt::AltModifier | Qt::ShiftModifier, true ) == map_camera_gesture_t::DOLLY );
    CHECK( CameraDrag( &f.workspace, Qt::LeftButton, Qt::AltModifier ) == map_camera_gesture_t::PAN );
}

TEST_CASE( "Camera remaps and unbindings never restore default gestures", "[map][gui][input][camera]" )
{
    input_fixture_t f( kCameraKeymap );
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests" mouse = { "map.viewport.3d" = {
  "map.camera.look" = [ "Ctrl+MiddleDrag" ]
  "map.camera.pan" = []
  "map.camera.dolly" = []
} } }
)cykv" );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::NONE );
    CHECK( CameraDrag( &f.workspace, Qt::MiddleButton ) == map_camera_gesture_t::NONE );
    CHECK( CameraDrag( &f.workspace, Qt::LeftButton, Qt::NoModifier, true ) == map_camera_gesture_t::NONE );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::AltModifier ) == map_camera_gesture_t::NONE );
    CHECK( CameraDrag( &f.workspace, Qt::MiddleButton, Qt::ControlModifier ) == map_camera_gesture_t::LOOK );
    CHECK_FALSE( CameraWheel( &f.workspace ) );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::LOOK ) == QStringList{ QStringLiteral( "Ctrl+MiddleDrag" ) } );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::PAN ).isEmpty() );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::DOLLY ).isEmpty() );
}

TEST_CASE( "A more-specific unbound drag reserves its inherited modifiers against a basic action", "[map][gui][input][camera]" )
{
    input_fixture_t f( kCameraKeymap );
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests" mouse = { "map.viewport.3d" = { "map.camera.dolly" = [] } } }
)cykv" );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::LOOK );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::ShiftModifier ) == map_camera_gesture_t::LOOK );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::AltModifier ) == map_camera_gesture_t::NONE );
    CHECK_FALSE( CameraWheel( &f.workspace ) );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::LOOK ) ==
           QStringList{ QStringLiteral( "Alt+MiddleDrag" ), QStringLiteral( "RightDrag" ) } );
}

TEST_CASE( "Camera unbinding collisions preserve deciding keymap and platform precedence", "[map][gui][input][camera]" )
{
    SECTION( "An inherited action cannot revive an equally specific user-unbound drag" ) {
        input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" mouse = { "map.viewport.3d" = {
  "map.camera.look" = [ "Alt+RightDrag" ]
  "map.camera.pan" = [ "Alt+RightDrag" ]
} } }
)cykv" );
        f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests" mouse = { "map.viewport.3d" = { "map.camera.look" = [] } } }
)cykv" );
        CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::AltModifier ) == map_camera_gesture_t::NONE );
        CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::PAN ).isEmpty() );
    }
    SECTION( "A same-tier explicit reassignment can reuse the unbound drag" ) {
        input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" mouse = { "map.viewport.3d" = { "map.camera.look" = [ "Alt+RightDrag" ] } } }
)cykv" );
        f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests" mouse = { "map.viewport.3d" = {
  "map.camera.look" = []
  "map.camera.pan" = [ "Alt+RightDrag" ]
} } }
)cykv" );
        CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::AltModifier ) == map_camera_gesture_t::PAN );
        CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::PAN ) == QStringList{ QStringLiteral( "Alt+RightDrag" ) } );
    }
    SECTION( "A main-section action cannot revive a platform-unbound drag" ) {
        input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests"
  mouse = { "map.viewport.3d" = { "map.camera.look" = [ "Alt+RightDrag" ] "map.camera.pan" = [ "Alt+RightDrag" ] } }
  platforms = {
    macos = { mouse = { "map.viewport.3d" = { "map.camera.look" = [] } } }
    windows = { mouse = { "map.viewport.3d" = { "map.camera.look" = [] } } }
    linux = { mouse = { "map.viewport.3d" = { "map.camera.look" = [] } } }
  }
}
)cykv" );
        REQUIRE( EditorKeymap_HostPlatform() != keymap_platform_t::NONE );
        CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::AltModifier ) == map_camera_gesture_t::NONE );
        CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::PAN ).isEmpty() );
    }
    SECTION( "A same-overlay reassignment can reuse its unbound drag" ) {
        input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests"
  mouse = { "map.viewport.3d" = { "map.camera.look" = [ "Alt+RightDrag" ] } }
  platforms = {
    macos = { mouse = { "map.viewport.3d" = { "map.camera.look" = [] "map.camera.pan" = [ "Alt+RightDrag" ] } } }
    windows = { mouse = { "map.viewport.3d" = { "map.camera.look" = [] "map.camera.pan" = [ "Alt+RightDrag" ] } } }
    linux = { mouse = { "map.viewport.3d" = { "map.camera.look" = [] "map.camera.pan" = [ "Alt+RightDrag" ] } } }
  }
}
)cykv" );
        REQUIRE( EditorKeymap_HostPlatform() != keymap_platform_t::NONE );
        CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::AltModifier ) == map_camera_gesture_t::PAN );
        CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::PAN ) == QStringList{ QStringLiteral( "Alt+RightDrag" ) } );
    }
}

TEST_CASE( "Camera unbinding in a tool shadows inherited input from a lower context", "[map][gui][input][camera]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" mouse = {
  "map.tool.block" = { "map.camera.look" = [ "RightDrag" ] }
  "map.viewport.3d" = { "map.camera.look" = [ "RightDrag", "MiddleDrag" ] }
} }
)cykv" );
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests" mouse = { "map.tool.block" = { "map.camera.look" = [] } } }
)cykv" );
    f.workspace.tool = map_tool_t::BLOCK;
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::NONE );
    CHECK( CameraDrag( &f.workspace, Qt::MiddleButton ) == map_camera_gesture_t::LOOK );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::LOOK ) == QStringList{ QStringLiteral( "MiddleDrag" ) } );
    f.workspace.tool = map_tool_t::SELECT;
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::LOOK );
}

TEST_CASE( "Camera unbinding reserves only the closest inherited replacement", "[map][gui][input][camera]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" mouse = {
  "map.tool.block" = { "map.camera.look" = [ "RightDrag" ] }
  "map.viewport.3d" = { "map.camera.pan" = [ "RightDrag", "MiddleDrag" ] }
} }
)cykv" );
    settings_document_t intermediate{};
    REQUIRE( SettingsDocument_Init( &intermediate, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &intermediate, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "middle" base = "input_tests" mouse = { "map.tool.block" = { "map.camera.look" = [ "MiddleDrag" ] } } }
)cykv" ) ).status == settings_document_status_t::OK );
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "middle" mouse = { "map.tool.block" = { "map.camera.look" = [] } } }
)cykv" );
    f.gui.keymapChain[2] = f.gui.keymapChain[1];
    f.gui.keymapChain[1] = SettingsDocument_Root( &intermediate ); f.gui.nKeymapChain = 3u;
    f.workspace.tool = map_tool_t::BLOCK;
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::PAN );
    CHECK( CameraDrag( &f.workspace, Qt::MiddleButton ) == map_camera_gesture_t::NONE );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::PAN ) == QStringList{ QStringLiteral( "RightDrag" ) } );
}

TEST_CASE( "Camera platform overlays replace or explicitly unbind main-section gestures", "[map][gui][input][camera]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests"
  mouse = { "map.viewport.3d" = { "map.camera.look" = [ "RightDrag" ] "map.camera.pan" = [ "MiddleDrag" ] } }
  platforms = {
    macos = { mouse = { "map.viewport.3d" = { "map.camera.look" = [] "map.camera.pan" = [ "Meta+LeftDrag" ] } } }
    windows = { mouse = { "map.viewport.3d" = { "map.camera.look" = [] "map.camera.pan" = [ "Meta+LeftDrag" ] } } }
    linux = { mouse = { "map.viewport.3d" = { "map.camera.look" = [] "map.camera.pan" = [ "Meta+LeftDrag" ] } } }
  }
}
)cykv" );
    REQUIRE( EditorKeymap_HostPlatform() != keymap_platform_t::NONE );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::NONE );
    CHECK( CameraDrag( &f.workspace, Qt::MiddleButton ) == map_camera_gesture_t::NONE );
    CHECK( CameraDrag( &f.workspace, Qt::LeftButton, Qt::MetaModifier ) == map_camera_gesture_t::PAN );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::LOOK ).isEmpty() );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::PAN ) == QStringList{ QStringLiteral( "Meta+LeftDrag" ) } );
}

TEST_CASE( "Camera wheels obey direction modifiers and reservation without horizontal gestures", "[map][gui][input][camera]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" mouse = { "map.viewport.3d" = {
  "map.camera.dolly" = [ "Wheel", "Ctrl+WheelUp", "WheelLeft" ]
  "future.unknown" = [ "Alt+WheelUp" ]
} } }
)cykv" );
    CHECK( CameraWheel( &f.workspace ) );
    CHECK( CameraWheel( &f.workspace, { 0, -120 } ) );
    CHECK( CameraWheel( &f.workspace, {}, Qt::NoModifier, { 0, 6 } ) );
    CHECK_FALSE( CameraWheel( &f.workspace, { -120, 0 } ) );
    CHECK_FALSE( CameraWheel( &f.workspace, {}, Qt::NoModifier, { 6, 0 } ) );
    CHECK_FALSE( CameraWheel( &f.workspace, {} ) );
    CHECK_FALSE( CameraWheel( &f.workspace, { 0, 120 }, Qt::AltModifier ) );
    CHECK( CameraWheel( &f.workspace, { 0, -120 }, Qt::AltModifier ) );
    CHECK( CameraWheel( &f.workspace, { 0, 120 }, Qt::ControlModifier ) );
    CHECK_FALSE( CameraWheel( &f.workspace, { 0, -120 }, Qt::ControlModifier ) );
    CHECK_FALSE( MapInput_CameraWheelGesture( &f.workspace, nullptr ) );
    CHECK_FALSE( CameraWheel( nullptr ) );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::DOLLY ) ==
           QStringList{ QStringLiteral( "Ctrl+WheelUp" ), QStringLiteral( "Wheel" ) } );
}

TEST_CASE( "Camera gesture help advertises only supported effective mouse input", "[map][gui][input][camera][help]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" mouse = {
  "map.tool.block" = { "future.unknown" = [ "Alt+LeftDrag" ] }
  "map.viewport.3d" = {
    "map.camera.look" = [ "RightDrag", "alt+middledrag", "Q+LeftDrag", "RightClick", "Wheel" ]
    "map.camera.orbit" = [ "Alt+LeftDrag" ]
    "map.camera.pan" = [ "MiddleDrag", "Space+LeftDrag", "Space+Wheel" ]
    "map.camera.dolly" = [ "WheelLeft", "Alt+RightDrag", "WheelDown" ]
  }
} }
)cykv" );
    f.workspace.tool = map_tool_t::BLOCK;
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::LOOK ) ==
           QStringList{ QStringLiteral( "Alt+MiddleDrag" ), QStringLiteral( "RightDrag" ) } );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::ORBIT ).isEmpty() );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::PAN ) ==
           QStringList{ QStringLiteral( "MiddleDrag" ), QStringLiteral( "Space+LeftDrag" ) } );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::DOLLY ) ==
           QStringList{ QStringLiteral( "Alt+RightDrag" ), QStringLiteral( "WheelDown" ) } );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::NONE ).isEmpty() );
    CHECK( MapInput_CameraGestureBindings( nullptr, map_camera_gesture_t::LOOK ).isEmpty() );
}

TEST_CASE( "Camera fallback input and help apply only in the absence of a keymap chain", "[map][gui][input][camera][help]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" }
)cykv" );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::NONE );
    CHECK_FALSE( CameraWheel( &f.workspace ) );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::LOOK ).isEmpty() );
    CHECK( MapInput_NavigationBindings( &f.workspace, MAP_NAVIGATION_FORWARD ).isEmpty() );
    QKeyEvent forward( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &forward ) == 0u );
    f.gui.nKeymapChain = 0u;
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::LOOK );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton, Qt::AltModifier ) == map_camera_gesture_t::DOLLY );
    CHECK( CameraDrag( &f.workspace, Qt::MiddleButton, Qt::AltModifier ) == map_camera_gesture_t::LOOK );
    CHECK( CameraDrag( &f.workspace, Qt::LeftButton, Qt::NoModifier, true ) == map_camera_gesture_t::PAN );
    CHECK( CameraWheel( &f.workspace ) );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::DOLLY ) ==
           QStringList{ QStringLiteral( "Alt+RightDrag" ), QStringLiteral( "Wheel" ) } );
    CHECK( MapInput_NavigationBindings( &f.workspace, MAP_NAVIGATION_FORWARD ) == QStringList{ QStringLiteral( "Up" ), QStringLiteral( "W" ) } );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &forward ) == MAP_NAVIGATION_FORWARD );
    f.workspace.pGui = nullptr;
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::NONE );
    CHECK_FALSE( CameraWheel( &f.workspace ) );
}

TEST_CASE( "Held camera navigation and help honor the active context and unknown reservations", "[map][gui][input][camera][help]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" held = {
  "map.tool.block" = { "future.unknown" = [ "W" ] "map.camera.up" = [ "R" ] }
  "map.viewport.3d" = { "map.camera.forward" = [ "W", "Up" ] "map.camera.up" = [ "E" ] }
  "map.viewport" = { "map.camera.right" = [ "D" ] }
  map = { "map.camera.left" = [ "A" ] }
  global = { "map.camera.back" = [ "S" ] }
} }
)cykv" );
    f.workspace.tool = map_tool_t::BLOCK;
    QKeyEvent forward( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier );
    QKeyEvent up( QEvent::KeyPress, Qt::Key_R, Qt::NoModifier );
    QKeyEvent down( QEvent::KeyPress, Qt::Key_E, Qt::NoModifier );
    QKeyEvent right( QEvent::KeyPress, Qt::Key_D, Qt::NoModifier );
    QKeyEvent left( QEvent::KeyPress, Qt::Key_A, Qt::NoModifier );
    QKeyEvent back( QEvent::KeyPress, Qt::Key_S, Qt::NoModifier );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &forward ) == 0u );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &up ) == MAP_NAVIGATION_UP );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &down ) == MAP_NAVIGATION_UP );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &right ) == MAP_NAVIGATION_RIGHT );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &left ) == MAP_NAVIGATION_LEFT );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &back ) == MAP_NAVIGATION_BACK );
    CHECK( MapInput_NavigationBindings( &f.workspace, MAP_NAVIGATION_FORWARD ) == QStringList{ QStringLiteral( "Up" ) } );
    CHECK( MapInput_NavigationBindings( &f.workspace, MAP_NAVIGATION_UP ) == QStringList{ QStringLiteral( "E" ), QStringLiteral( "R" ) } );
    CHECK( MapInput_NavigationBindings( &f.workspace, 0u ).isEmpty() );
    CHECK( MapInput_NavigationBindings( nullptr, MAP_NAVIGATION_FORWARD ).isEmpty() );
    f.workspace.tool = map_tool_t::SELECT;
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &forward ) == MAP_NAVIGATION_FORWARD );
}

TEST_CASE( "Held camera help reflects platform replacement and tool unbinding shadows", "[map][gui][input][camera][help]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests"
  held = {
    "map.tool.block" = { "map.camera.forward" = [ "W" ] }
    "map.viewport.3d" = { "map.camera.forward" = [ "W", "Up" ] "map.camera.up" = [ "E" ] }
  }
}
)cykv" );
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests"
  held = { "map.tool.block" = { "map.camera.forward" = [] } }
  platforms = {
    macos = { held = { "map.viewport.3d" = { "map.camera.up" = [ "Ctrl+Y" ] } } }
    windows = { held = { "map.viewport.3d" = { "map.camera.up" = [ "Ctrl+Y" ] } } }
    linux = { held = { "map.viewport.3d" = { "map.camera.up" = [ "Ctrl+Y" ] } } }
  }
}
)cykv" );
    f.workspace.tool = map_tool_t::BLOCK;
    QKeyEvent forward( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier );
    QKeyEvent oldUp( QEvent::KeyPress, Qt::Key_E, Qt::NoModifier );
    QKeyEvent up( QEvent::KeyPress, Qt::Key_Y, Qt::ControlModifier );
    REQUIRE( EditorKeymap_HostPlatform() != keymap_platform_t::NONE );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &forward ) == 0u );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &oldUp ) == 0u );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &up ) == MAP_NAVIGATION_UP );
    CHECK( MapInput_NavigationBindings( &f.workspace, MAP_NAVIGATION_FORWARD ) == QStringList{ QStringLiteral( "Up" ) } );
    CHECK( MapInput_NavigationBindings( &f.workspace, MAP_NAVIGATION_UP ) == QStringList{ QStringLiteral( "Ctrl+Y" ) } );
}

TEST_CASE( "Viewport preflight is side-effect free and execution uses the command registry", "[map][gui][input]" )
{
    input_fixture_t f( kContextKeymap );
    QKeyEvent save( QEvent::KeyPress, Qt::Key_S, Qt::ControlModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &save, true, false, false ) );
    CHECK( f.Calls( "test.global" ) == 0 );
    CHECK( f.nObserved == 0 );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &save, true, false, true ) );
    CHECK( f.Calls( "test.global" ) == 1 );
    CHECK( f.nObserved == 1 );
}

TEST_CASE( "The active tool overrides its viewport family and shared viewport bindings fall through", "[map][gui][input]" )
{
    input_fixture_t f( kContextKeymap );
    QKeyEvent c( QEvent::KeyPress, Qt::Key_C, Qt::NoModifier );
    f.workspace.tool = map_tool_t::VERTEX;
    REQUIRE( MapInput_DispatchKey( &f.workspace, &c, true, false, true ) );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &c, false, false, true ) );
    CHECK( f.Calls( "test.tool" ) == 2 );
    CHECK( f.Calls( "test.family3d" ) == 0 );
    f.workspace.tool = map_tool_t::SELECT;
    REQUIRE( MapInput_DispatchKey( &f.workspace, &c, true, false, true ) );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &c, false, false, true ) );
    CHECK( f.Calls( "test.family3d" ) == 1 );
    CHECK( f.Calls( "test.family2d" ) == 1 );
    CHECK( f.Calls( "test.global" ) == 0 );
    QKeyEvent v( QEvent::KeyPress, Qt::Key_V, Qt::NoModifier );
    QKeyEvent m( QEvent::KeyPress, Qt::Key_M, Qt::NoModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &v, true, false, true ) );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &m, false, false, true ) );
    CHECK( f.Calls( "test.viewport" ) == 1 );
    CHECK( f.Calls( "test.map" ) == 1 );
}

TEST_CASE( "Disabled and unknown bindings reserve keys without execution and failed commands are consumed once", "[map][gui][input]" )
{
    input_fixture_t f( kContextKeymap );
    QKeyEvent disabled( QEvent::KeyPress, Qt::Key_D, Qt::NoModifier );
    QKeyEvent unknown( QEvent::KeyPress, Qt::Key_U, Qt::NoModifier );
    CHECK( MapInput_DispatchKey( &f.workspace, &disabled, false, false, false ) );
    CHECK( MapInput_DispatchKey( &f.workspace, &disabled, false, false, true ) );
    CHECK( MapInput_DispatchKey( &f.workspace, &unknown, true, false, true ) );
    CHECK( f.nObserved == 0 );
    QKeyEvent failed( QEvent::KeyPress, Qt::Key_F, Qt::NoModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &failed, true, false, true ) );
    CHECK( f.Calls( "test.failed" ) == 1 );
    CHECK( f.nObserved == 1 );
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests" bindings = { "map.tool.vertex" = { "test.disabled" = [ "C" ] } } }
)cykv" );
    f.workspace.tool = map_tool_t::VERTEX;
    QKeyEvent c( QEvent::KeyPress, Qt::Key_C, Qt::NoModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &c, true, false, true ) );
    CHECK( f.Calls( "test.family3d" ) == 0 );
    CHECK( f.Calls( "test.global" ) == 0 );
    CHECK( f.Calls( "test.disabled" ) == 0 );
    CHECK( f.nObserved == 1 );
}

TEST_CASE( "Selection modes resolve between tool and viewport bindings in every view family", "[map][gui][input][mesh-workflow]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" bindings = {
    "map.tool.block" = { "test.tool" = [ "F" ] }
    "map.selection.vertices" = { "test.viewport" = [ "F" ] }
    "map.selection.edges" = { "test.viewport" = [ "F" ] }
    "map.selection.faces" = { "test.viewport" = [ "F" ] }
    "map.selection.meshes" = { "test.viewport" = [ "F" ] }
    "map.selection.objects" = { "test.viewport" = [ "F" ] }
    "map.selection.groups" = { "test.viewport" = [ "F" ] }
    "map.selection.navigation" = { "test.viewport" = [ "F" ] }
    "map.viewport.3d" = { "test.family3d" = [ "F" ] }
    "map.viewport.2d" = { "test.family2d" = [ "F" ] }
    global = { "test.global" = [ "F" ] }
} }
)cykv" );
    QKeyEvent key( QEvent::KeyPress, Qt::Key_F, Qt::NoModifier );
    f.workspace.tool = map_tool_t::SELECT;
    for ( usize mode = 0u; mode < static_cast<usize>( map_element_mode_t::COUNT ); ++mode ) {
        // Direct assignment verifies names/resolution without enabling these
        // modes in the editor or claiming component operations exist.
        f.workspace.elementMode = static_cast<map_element_mode_t>( mode );
        for ( const bool camera : { false, true } ) {
            const int before = f.Calls( "test.viewport" );
            REQUIRE( MapInput_DispatchKey( &f.workspace, &key, camera, false, false ) );
            CHECK( f.Calls( "test.viewport" ) == before );
            REQUIRE( MapInput_DispatchKey( &f.workspace, &key, camera, false, true ) );
            CHECK( f.Calls( "test.viewport" ) == before + 1 );
        }
    }
    CHECK( f.Calls( "test.family3d" ) == 0 );
    CHECK( f.Calls( "test.family2d" ) == 0 );
    CHECK( f.Calls( "test.global" ) == 0 );
    CHECK( MapInput_CommandBindings( &f.workspace, "test.viewport", true ) == QStringList{ QStringLiteral( "F" ) } );
    f.workspace.tool = map_tool_t::BLOCK;
    REQUIRE( MapInput_DispatchKey( &f.workspace, &key, true, false, true ) );
    CHECK( f.Calls( "test.tool" ) == 1 );
    CHECK( f.Calls( "test.viewport" ) == 2 * static_cast<int>( map_element_mode_t::COUNT ) );
    CHECK( MapInput_CommandBindings( &f.workspace, "test.viewport", true ).isEmpty() );
    CHECK( MapInput_CommandBindings( &f.workspace, "test.tool", true ) == QStringList{ QStringLiteral( "F" ) } );
}

TEST_CASE( "Selection bindings preserve explicit unbinding and remaps without reviving inherited keys", "[map][gui][input][mesh-workflow]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" bindings = {
    "map.selection.objects" = { "test.viewport" = [ "F" ] }
    "map.selection.groups" = { "test.viewport" = [ "F" ] }
    global = { "test.global" = [ "F", "J" ] }
} }
)cykv" );
    f.workspace.tool = map_tool_t::SELECT;
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests" bindings = {
    "map.selection.objects" = { "test.viewport" = [] }
    "map.selection.groups" = { "test.viewport" = [ "J" ] }
} }
)cykv" );
    QKeyEvent oldKey( QEvent::KeyPress, Qt::Key_F, Qt::NoModifier );
    QKeyEvent replacement( QEvent::KeyPress, Qt::Key_J, Qt::NoModifier );
    f.workspace.elementMode = map_element_mode_t::OBJECTS;
    REQUIRE( MapInput_DispatchKey( &f.workspace, &oldKey, true, false, true ) );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &oldKey, false, false, true ) );
    CHECK( f.nObserved == 0 );
    CHECK( MapInput_CommandBindings( &f.workspace, "test.viewport", true ).isEmpty() );
    f.workspace.elementMode = map_element_mode_t::GROUPS;
    REQUIRE( MapInput_DispatchKey( &f.workspace, &replacement, false, false, true ) );
    CHECK( f.Calls( "test.viewport" ) == 1 );
    CHECK( MapInput_CommandBindings( &f.workspace, "test.viewport", false ) == QStringList{ QStringLiteral( "J" ) } );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &oldKey, true, false, true ) );
    CHECK( f.Calls( "test.global" ) == 1 ); // A replacement releases F; [] reserves it.
    f.workspace.elementMode = map_element_mode_t::FACES;
    REQUIRE( MapInput_DispatchKey( &f.workspace, &replacement, true, false, true ) );
    CHECK( f.Calls( "test.global" ) == 2 );
    CHECK( f.Calls( "test.viewport" ) == 1 );
}

TEST_CASE( "Disabled selection commands reserve destructive fallback keys and platform unbindings", "[map][gui][input][mesh-workflow]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" bindings = {
    "map.selection.edges" = { "test.disabled" = [ "Backspace" ] }
    "map.selection.objects" = { "test.viewport" = [ "F" ] }
    global = { "test.global" = [ "Backspace", "F" ] }
  }
  platforms = {
    macos = { bindings = { "map.selection.objects" = { "test.viewport" = [] } } }
    windows = { bindings = { "map.selection.objects" = { "test.viewport" = [] } } }
    linux = { bindings = { "map.selection.objects" = { "test.viewport" = [] } } }
  }
}
)cykv" );
    f.workspace.tool = map_tool_t::SELECT;
    f.workspace.elementMode = map_element_mode_t::EDGES;
    QKeyEvent backspace( QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &backspace, true, false, false ) );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &backspace, false, false, true ) );
    CHECK( f.Calls( "test.disabled" ) == 0 );
    CHECK( f.Calls( "test.global" ) == 0 );
    CHECK( MapInput_CommandBindings( &f.workspace, "test.disabled", true ) == QStringList{ QStringLiteral( "Backspace" ) } );
    f.workspace.elementMode = map_element_mode_t::OBJECTS;
    QKeyEvent key( QEvent::KeyPress, Qt::Key_F, Qt::NoModifier );
    REQUIRE( EditorKeymap_HostPlatform() != keymap_platform_t::NONE );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &key, true, false, true ) );
    CHECK( f.Calls( "test.viewport" ) == 0 );
    CHECK( f.Calls( "test.global" ) == 0 );
    CHECK( f.nObserved == 0 );
}

TEST_CASE( "Selection context gesture help follows the same resolver as confirmation and command help", "[map][gui][input][gesture][mesh-workflow]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" bindings = {
    "map.tool.block" = { "test.disabled" = [ "Enter" ] }
    "map.selection.objects" = { "map.tool.confirm" = [ "Enter" ] }
    "map.viewport" = { "map.tool.cancel" = [ "Escape" ] }
} }
)cykv" );
    f.workspace.tool = map_tool_t::SELECT;
    f.workspace.elementMode = map_element_mode_t::OBJECTS;
    QKeyEvent key( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &key, true ) == map_tool_gesture_key_t::CONFIRM );
    CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::CONFIRM, true ) ==
           MapInput_CommandBindings( &f.workspace, "map.tool.confirm", true ) );
    CHECK( MapInput_CommandBindings( &f.workspace, "map.tool.confirm", true ) == QStringList{ QStringLiteral( "Enter" ) } );
    f.workspace.tool = map_tool_t::BLOCK;
    CHECK( MapInput_ToolGestureKey( &f.workspace, &key, true ) == map_tool_gesture_key_t::NONE );
    CHECK( MapInput_CommandBindings( &f.workspace, "map.tool.confirm", true ).isEmpty() );
    CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::CONFIRM, true ).isEmpty() );
    f.workspace.tool = map_tool_t::SELECT;
    f.workspace.elementMode = map_element_mode_t::FACES;
    CHECK( MapInput_ToolGestureKey( &f.workspace, &key, true ) == map_tool_gesture_key_t::NONE );
    CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::CONFIRM, true ).isEmpty() );
    CHECK( f.nObserved == 0 );
}

TEST_CASE( "Selection mouse and held contexts share precedence and camera flight retains key ownership", "[map][gui][input][camera][mesh-workflow]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests"
  bindings = {
    "map.selection.objects" = { "test.viewport" = [ "E" ] }
    global = { "test.global" = [ "E" ] }
  }
  mouse = {
    "map.tool.block" = { "map.camera.orbit" = [ "RightDrag" ] }
    "map.selection.objects" = { "map.camera.pan" = [ "RightDrag" ] }
    "map.viewport.3d" = { "map.camera.look" = [ "RightDrag" ] }
  }
  held = {
    "map.tool.block" = { "map.camera.forward" = [ "E" ] }
    "map.selection.objects" = { "map.camera.up" = [ "E" ] }
    "map.viewport.3d" = { "map.camera.down" = [ "E" ] }
  }
}
)cykv" );
    f.workspace.tool = map_tool_t::SELECT;
    f.workspace.elementMode = map_element_mode_t::OBJECTS;
    QKeyEvent key( QEvent::KeyPress, Qt::Key_E, Qt::NoModifier );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::PAN );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &key ) == MAP_NAVIGATION_UP );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::PAN ) == QStringList{ QStringLiteral( "RightDrag" ) } );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::LOOK ).isEmpty() );
    CHECK_FALSE( MapInput_DispatchKey( &f.workspace, &key, true, true, false ) );
    CHECK_FALSE( MapInput_DispatchKey( &f.workspace, &key, true, true, true ) );
    CHECK( f.nObserved == 0 );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &key, true, false, true ) );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &key, false, true, true ) );
    CHECK( f.Calls( "test.viewport" ) == 2 );
    f.workspace.tool = map_tool_t::BLOCK;
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::ORBIT );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &key ) == MAP_NAVIGATION_FORWARD );
    f.workspace.tool = map_tool_t::SELECT;
    f.workspace.elementMode = map_element_mode_t::FACES;
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::LOOK );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &key ) == MAP_NAVIGATION_DOWN );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &key, true, false, true ) );
    CHECK( f.Calls( "test.global" ) == 1 );
    f.workspace.elementMode = map_element_mode_t::OBJECTS;
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests"
  mouse = { "map.selection.objects" = { "map.camera.pan" = [] } }
  held = { "map.selection.objects" = { "map.camera.up" = [] } }
}
)cykv" );
    CHECK( CameraDrag( &f.workspace, Qt::RightButton ) == map_camera_gesture_t::NONE );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &key ) == 0u );
    CHECK( MapInput_CameraGestureBindings( &f.workspace, map_camera_gesture_t::PAN ).isEmpty() );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &key, true, true, true ) );
    CHECK( f.Calls( "test.viewport" ) == 3 );
}

TEST_CASE( "Default mesh reversal is contextual and brush conversion is explicitly remappable", "[map][gui][input][mesh-workflow]" )
{
    default_input_fixture_t f;
    key_chord_t key{};
    REQUIRE( EditorKeyChord_Parse( StringView_FromCString( "F" ), &key ) );
    for ( const auto *name : { "map.selection.objects", "map.selection.groups" } ) {
        const string_view_t context = StringView_FromCString( name );
        CHECK( StringView_Equals( EditorKeymap_FindCommandInStack( f.gui.keymapChain, f.gui.nKeymapChain,
                                                                 EditorKeymap_HostPlatform(), &context, 1u, key ),
                                 StringView_FromCString( "map.mesh.flip_normals" ) ) );
    }
    f.workspace.elementMode = map_element_mode_t::OBJECTS;
    CHECK( MapInput_CommandBindings( &f.workspace, "map.mesh.flip_normals", true ) == QStringList{ QStringLiteral( "F" ) } );
    f.workspace.elementMode = map_element_mode_t::GROUPS;
    CHECK( MapInput_CommandBindings( &f.workspace, "map.mesh.flip_normals", false ) == QStringList{ QStringLiteral( "F" ) } );
    f.workspace.elementMode = map_element_mode_t::FACES;
    CHECK( MapInput_CommandBindings( &f.workspace, "map.mesh.flip_normals", true ).isEmpty() );
    CHECK( MapInput_CommandBindings( &f.workspace, "map.brush.to_mesh", true ).isEmpty() );
    CHECK( MapInput_CommandBindings( nullptr, "map.mesh.flip_normals", true ).isEmpty() );
    CHECK( MapInput_CommandBindings( &f.workspace, nullptr, true ).isEmpty() );
    CHECK( MapInput_CommandBindings( &f.workspace, "", true ).isEmpty() );
    for ( const auto *name : { "map.selection.vertices", "map.selection.edges", "map.selection.faces", "map.selection.meshes",
                              "map.viewport", "map" } ) {
        const string_view_t context = StringView_FromCString( name );
        CHECK( EditorKeymap_FindCommandInStack( f.gui.keymapChain, f.gui.nKeymapChain, EditorKeymap_HostPlatform(),
                                               &context, 1u, key ).cchLength == 0u );
    }
    keymap_binding_t conversion{};
    CHECK( EditorKeymap_FindBindingOn( f.gui.keymapChain, f.gui.nKeymapChain, EditorKeymap_HostPlatform(),
                                     StringView_FromCString( "map" ), StringView_FromCString( "map.brush.to_mesh" ),
                                     &conversion ) == keymap_lookup_t::UNBOUND );
}

TEST_CASE( "Viewport bindings honor user unbinding and replacements in the active inheritance chain", "[map][gui][input]" )
{
    input_fixture_t f( kContextKeymap );
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests" bindings = {
    "map.tool.vertex" = { "test.tool" = [] }
    "map.viewport.2d" = { "test.family2d" = [ "X" ] }
} }
)cykv" );
    f.workspace.tool = map_tool_t::VERTEX;
    QKeyEvent c( QEvent::KeyPress, Qt::Key_C, Qt::NoModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &c, true, false, true ) );
    CHECK( f.Calls( "test.tool" ) == 0 );
    CHECK( f.Calls( "test.family3d" ) == 0 );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &c, false, false, true ) );
    CHECK( f.Calls( "test.family2d" ) == 0 );
    CHECK( f.Calls( "test.global" ) == 0 );
    QKeyEvent x( QEvent::KeyPress, Qt::Key_X, Qt::NoModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &x, false, false, true ) );
    CHECK( f.Calls( "test.family2d" ) == 1 );
    // Replacing C with X in a context releases C; an empty entry instead
    // reserves its inherited C and stops the less-specific action above.
    f.workspace.tool = map_tool_t::SELECT;
    REQUIRE( MapInput_DispatchKey( &f.workspace, &c, false, false, true ) );
    CHECK( f.Calls( "test.global" ) == 1 );
}

TEST_CASE( "Held camera keys outrank tool shortcuts only while navigating the camera", "[map][gui][input]" )
{
    input_fixture_t f( kContextKeymap );
    QKeyEvent e( QEvent::KeyPress, Qt::Key_E, Qt::NoModifier );
    REQUIRE( MapInput_IsNavigationKey( &f.workspace, &e ) );
    CHECK_FALSE( MapInput_DispatchKey( &f.workspace, &e, true, true, false ) );
    CHECK_FALSE( MapInput_DispatchKey( &f.workspace, &e, true, true, true ) );
    CHECK( f.nObserved == 0 );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &e, true, false, true ) );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &e, false, true, true ) );
    CHECK( f.Calls( "test.global" ) == 2 );
    QKeyEvent fastUp( QEvent::KeyPress, Qt::Key_E, Qt::ShiftModifier | Qt::AltModifier );
    QKeyEvent commandUp( QEvent::KeyPress, Qt::Key_E, Qt::ControlModifier );
    QKeyEvent shift( QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier );
    CHECK( MapInput_IsNavigationKey( &f.workspace, &fastUp ) );
    CHECK( MapInput_IsNavigationKey( &f.workspace, &shift ) );
    CHECK_FALSE( MapInput_IsNavigationKey( &f.workspace, &commandUp ) );
}

TEST_CASE( "A platform unbinding reserves the main-section chord in the same keymap", "[map][gui][input]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests"
  bindings = {
    "map.tool.vertex" = { "test.tool" = [ "C" ] }
    global = { "test.global" = [ "C", "M" ] }
  }
  platforms = {
    macos = { bindings = { "map.tool.vertex" = { "test.tool" = [] } } }
    windows = { bindings = { "map.tool.vertex" = { "test.tool" = [] } } }
    linux = { bindings = { "map.tool.vertex" = { "test.tool" = [] } } }
  }
}
)cykv" );
    f.workspace.tool = map_tool_t::VERTEX;
    QKeyEvent c( QEvent::KeyPress, Qt::Key_C, Qt::NoModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &c, true, false, false ) );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &c, true, false, true ) );
    CHECK( f.Calls( "test.tool" ) == 0 );
    CHECK( f.Calls( "test.global" ) == 0 );
    QKeyEvent m( QEvent::KeyPress, Qt::Key_M, Qt::NoModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &m, true, false, true ) );
    CHECK( f.Calls( "test.global" ) == 1 );
}

TEST_CASE( "An explicit unbinding reserves only the nearest inherited chords", "[map][gui][input]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" bindings = {
    "map.tool.vertex" = { "test.tool" = [ "C" ] }
    global = { "test.global" = [ "C", "D" ] }
} }
)cykv" );
    settings_document_t intermediate{};
    REQUIRE( SettingsDocument_Init( &intermediate, Allocator_GetSystem(), EditorKeymap_Identity() ) == settings_document_status_t::OK );
    REQUIRE( SettingsDocument_Load( &intermediate, StringView_FromCString( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "middle" base = "input_tests" bindings = { "map.tool.vertex" = { "test.tool" = [ "D" ] } } }
)cykv" ) ).status == settings_document_status_t::OK );
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "middle" bindings = { "map.tool.vertex" = { "test.tool" = [] } } }
)cykv" );
    f.gui.keymapChain[2] = f.gui.keymapChain[1];
    f.gui.keymapChain[1] = SettingsDocument_Root( &intermediate );
    f.gui.nKeymapChain = 3u;
    f.workspace.tool = map_tool_t::VERTEX;
    QKeyEvent d( QEvent::KeyPress, Qt::Key_D, Qt::NoModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &d, false, false, true ) );
    CHECK( f.Calls( "test.global" ) == 0 );
    QKeyEvent c( QEvent::KeyPress, Qt::Key_C, Qt::NoModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &c, false, false, true ) );
    CHECK( f.Calls( "test.global" ) == 1 );
}

TEST_CASE( "Held camera overrides do not retain the old keys", "[map][gui][input]" )
{
    input_fixture_t f( kContextKeymap );
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests" held = { "map.viewport.3d" = {
    "map.camera.forward" = []
    "map.camera.up" = [ "Y" ]
} } }
)cykv" );
    QKeyEvent e( QEvent::KeyPress, Qt::Key_E, Qt::NoModifier );
    QKeyEvent y( QEvent::KeyPress, Qt::Key_Y, Qt::NoModifier );
    QKeyEvent w( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier );
    CHECK_FALSE( MapInput_IsNavigationKey( &f.workspace, &e ) );
    CHECK_FALSE( MapInput_IsNavigationKey( &f.workspace, &w ) );
    CHECK( MapInput_IsNavigationKey( &f.workspace, &y ) );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &e, true, true, true ) );
}

TEST_CASE( "Navigation masks expose configured movement and speed actions without capturing command chords", "[map][gui][input]" )
{
    input_fixture_t f( kContextKeymap );
    QKeyEvent forward( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier );
    QKeyEvent up( QEvent::KeyPress, Qt::Key_E, Qt::NoModifier );
    QKeyEvent fast( QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier );
    QKeyEvent slow( QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier );
    QKeyEvent command( QEvent::KeyPress, Qt::Key_W, Qt::ControlModifier );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &forward ) == MAP_NAVIGATION_FORWARD );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &up ) == MAP_NAVIGATION_UP );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &fast ) == MAP_NAVIGATION_FAST );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &slow ) == MAP_NAVIGATION_SLOW );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &command ) == 0u );
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests" held = { "map.viewport.3d" = {
    "map.camera.forward" = [ "Y" ]
    "map.camera.fast" = [ "Y" ]
    "map.camera.up" = []
    "map.camera.down" = [ "Ctrl+Q" ]
} } }
)cykv" );
    QKeyEvent y( QEvent::KeyPress, Qt::Key_Y, Qt::NoModifier );
    QKeyEvent explicitCommand( QEvent::KeyPress, Qt::Key_Q, Qt::ControlModifier );
    QKeyEvent differentCommand( QEvent::KeyPress, Qt::Key_Q, Qt::MetaModifier );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &forward ) == 0u );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &up ) == 0u );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &y ) == ( MAP_NAVIGATION_FORWARD | MAP_NAVIGATION_FAST ) );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &explicitCommand ) == MAP_NAVIGATION_DOWN );
    CHECK( MapInput_NavigationKeyMask( &f.workspace, &differentCommand ) == 0u );
    CHECK_FALSE( MapInput_IsNavigationKey( &f.workspace, &up ) );
    CHECK( MapInput_IsNavigationKey( &f.workspace, &y ) );
}

TEST_CASE( "Viewport dispatcher preserves keypad identity and does not execute partial multi-stroke chords", "[map][gui][input]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" bindings = { "map.tool.block" = {
    "test.tool" = [ "NumEnter" ]
    "test.map" = [ "Ctrl+K, Ctrl+C" ]
} } }
)cykv" );
    f.workspace.tool = map_tool_t::BLOCK;
    QKeyEvent numEnter( QEvent::KeyPress, Qt::Key_Enter, Qt::KeypadModifier );
    QKeyEvent enter( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    QKeyEvent firstStroke( QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &numEnter, false, false, true ) );
    CHECK_FALSE( MapInput_DispatchKey( &f.workspace, &enter, false, false, true ) );
    CHECK_FALSE( MapInput_DispatchKey( &f.workspace, &firstStroke, false, false, true ) );
    CHECK( f.Calls( "test.tool" ) == 1 );
    CHECK( f.Calls( "test.map" ) == 0 );
    CHECK_FALSE( MapInput_DispatchKey( nullptr, &enter, false, false, true ) );
    CHECK_FALSE( MapInput_DispatchKey( &f.workspace, nullptr, false, false, true ) );
}

TEST_CASE( "Logical gesture keys share context precedence without executing commands", "[map][gui][input][gesture]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" bindings = {
    "map.tool.block" = { "map.tool.confirm" = [ "Enter" ] "map.tool.cancel" = [ "Escape" ] }
    "map.tool.clip" = { "test.disabled" = [ "Enter" ] }
    "map.viewport.3d" = { "test.family3d" = [ "Enter" ] }
    "map.viewport" = { "map.tool.confirm" = [ "Enter", "NumEnter" ] "map.tool.cancel" = [ "Escape" ] }
    global = { "test.global" = [ "Enter", "Escape" ] }
} }
)cykv" );
    f.workspace.tool = map_tool_t::BLOCK;
    QKeyEvent enter( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    QKeyEvent numEnter( QEvent::KeyPress, Qt::Key_Enter, Qt::KeypadModifier );
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &enter, true ) == map_tool_gesture_key_t::CONFIRM );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &enter, false ) == map_tool_gesture_key_t::CONFIRM );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &numEnter, true ) == map_tool_gesture_key_t::CONFIRM );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &escape, true ) == map_tool_gesture_key_t::CANCEL );
    CHECK( f.nObserved == 0 );
    f.workspace.tool = map_tool_t::SELECT;
    // A more-specific view action shadows shared tool confirmation.
    CHECK( MapInput_ToolGestureKey( &f.workspace, &enter, true ) == map_tool_gesture_key_t::NONE );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &enter, false ) == map_tool_gesture_key_t::CONFIRM );
    f.workspace.tool = map_tool_t::CLIP;
    CHECK( MapInput_ToolGestureKey( &f.workspace, &enter, false ) == map_tool_gesture_key_t::NONE );
    CHECK( MapInput_DispatchKey( &f.workspace, &enter, false, false, true ) );
    CHECK( f.nObserved == 0 );
    CHECK( MapInput_ToolGestureKey( nullptr, &enter, false ) == map_tool_gesture_key_t::NONE );
    CHECK( MapInput_ToolGestureKey( &f.workspace, nullptr, false ) == map_tool_gesture_key_t::NONE );
}

TEST_CASE( "Gesture remaps release old chords and explicit unbindings shadow lower contexts", "[map][gui][input][gesture]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" bindings = {
    "map.tool.block" = { "map.tool.confirm" = [ "Enter" ] }
    "map.viewport" = { "map.tool.confirm" = [ "Enter" ] "map.tool.cancel" = [ "Escape" ] }
    global = { "test.global" = [ "Enter", "Escape" ] }
} }
)cykv" );
    f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests" bindings = {
    "map.tool.block" = { "map.tool.confirm" = [] }
    "map.viewport" = { "map.tool.confirm" = [ "C" ] "map.tool.cancel" = [ "Ctrl+G" ] }
} }
)cykv" );
    f.workspace.tool = map_tool_t::BLOCK;
    QKeyEvent enter( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    QKeyEvent confirm( QEvent::KeyPress, Qt::Key_C, Qt::NoModifier );
    QKeyEvent cancel( QEvent::KeyPress, Qt::Key_G, Qt::ControlModifier );
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &enter, true ) == map_tool_gesture_key_t::NONE );
    CHECK( MapInput_DispatchKey( &f.workspace, &enter, true, false, true ) );
    CHECK( f.Calls( "test.global" ) == 0 );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &confirm, true ) == map_tool_gesture_key_t::CONFIRM );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &cancel, false ) == map_tool_gesture_key_t::CANCEL );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &escape, false ) == map_tool_gesture_key_t::NONE );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &escape, false, false, true ) );
    CHECK( f.Calls( "test.global" ) == 1 );
}

TEST_CASE( "Gesture help includes only effective single-stroke keys in each viewport family", "[map][gui][input][gesture][help]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" bindings = {
    "map.tool.block" = { "map.tool.confirm" = [ "V", "Ctrl+K, Ctrl+C" ] "map.tool.cancel" = [ "C" ] }
    "map.viewport.2d" = { "map.tool.confirm" = [ "F2" ] "test.disabled" = [ "Enter" ] }
    "map.viewport.3d" = { "map.tool.confirm" = [ "F3" ] "test.disabled" = [ "Escape" ] "future.unregistered" = [ "G" ] }
    "map.viewport" = { "map.tool.confirm" = [ "Enter", "V", "NumEnter" ] "map.tool.cancel" = [ "Escape", "C" ] }
    map = { "map.tool.confirm" = [ "M" ] "map.tool.cancel" = [ "D" ] }
    global = { "map.tool.confirm" = [ "G" ] "map.tool.cancel" = [ "H" ] }
} }
)cykv" );
    f.workspace.tool = map_tool_t::BLOCK;
    CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::CONFIRM, false ) ==
           QStringList{ QStringLiteral( "F2" ), QStringLiteral( "G" ), QStringLiteral( "M" ), QStringLiteral( "NumEnter" ), QStringLiteral( "V" ) } );
    CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::CONFIRM, true ) ==
           QStringList{ QStringLiteral( "Enter" ), QStringLiteral( "F3" ), QStringLiteral( "M" ), QStringLiteral( "NumEnter" ), QStringLiteral( "V" ) } );
    CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::CANCEL, false ) ==
           QStringList{ QStringLiteral( "C" ), QStringLiteral( "D" ), QStringLiteral( "Escape" ), QStringLiteral( "H" ) } );
    CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::CANCEL, true ) ==
           QStringList{ QStringLiteral( "C" ), QStringLiteral( "D" ), QStringLiteral( "H" ) } );
    QKeyEvent enter( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    QKeyEvent globalConfirm( QEvent::KeyPress, Qt::Key_G, Qt::NoModifier );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &enter, false ) == map_tool_gesture_key_t::NONE );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &enter, true ) == map_tool_gesture_key_t::CONFIRM );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &escape, true ) == map_tool_gesture_key_t::NONE );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &escape, false ) == map_tool_gesture_key_t::CANCEL );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &globalConfirm, true ) == map_tool_gesture_key_t::NONE );
    CHECK( f.nObserved == 0 ); CHECK( f.Calls( "test.disabled" ) == 0 );
    CHECK( MapInput_ToolGestureBindings( nullptr, map_tool_gesture_key_t::CONFIRM, false ).isEmpty() );
    CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::NONE, false ).isEmpty() );
    f.workspace.pGui = nullptr;
    CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::CONFIRM, false ).isEmpty() );
}

TEST_CASE( "Gesture help honors inherited unbinding shadows and platform remaps", "[map][gui][input][gesture][help]" )
{
    input_fixture_t f( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "input_tests" bindings = {
    "map.tool.block" = { "map.tool.confirm" = [ "Enter" ] "map.tool.cancel" = [ "Escape" ] }
    "map.viewport" = { "map.tool.confirm" = [ "Enter" ] "map.tool.cancel" = [ "Escape" ] }
    global = { "map.tool.confirm" = [ "G" ] "map.tool.cancel" = [ "H" ] }
} }
)cykv" );
    f.workspace.tool = map_tool_t::BLOCK;
    SECTION( "An empty tool entry reserves its inherited chord from shared viewport help" ) {
        f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests" bindings = {
    "map.tool.block" = { "map.tool.confirm" = [] "map.tool.cancel" = [] }
} }
)cykv" );
        for ( bool camera : { false, true } ) {
            CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::CONFIRM, camera ) == QStringList{ QStringLiteral( "G" ) } );
            CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::CANCEL, camera ) == QStringList{ QStringLiteral( "H" ) } );
        }
    }
    SECTION( "Platform gesture bindings replace main-section bindings without leaking old texts" ) {
        f.Override( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "user" base = "input_tests"
  bindings = { "map.tool.block" = { "map.tool.confirm" = [ "V" ] "map.tool.cancel" = [ "C" ] } }
  platforms = {
    macos = { bindings = { "map.tool.block" = { "map.tool.confirm" = [ "F9" ] "map.tool.cancel" = [ "F10" ] } } }
    windows = { bindings = { "map.tool.block" = { "map.tool.confirm" = [ "F9" ] "map.tool.cancel" = [ "F10" ] } } }
    linux = { bindings = { "map.tool.block" = { "map.tool.confirm" = [ "F9" ] "map.tool.cancel" = [ "F10" ] } } }
  }
}
)cykv" );
        REQUIRE( EditorKeymap_HostPlatform() != keymap_platform_t::NONE );
        for ( bool camera : { false, true } ) {
            CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::CONFIRM, camera ) ==
                   QStringList{ QStringLiteral( "Enter" ), QStringLiteral( "F9" ), QStringLiteral( "G" ) } );
            CHECK( MapInput_ToolGestureBindings( &f.workspace, map_tool_gesture_key_t::CANCEL, camera ) ==
                   QStringList{ QStringLiteral( "Escape" ), QStringLiteral( "F10" ), QStringLiteral( "H" ) } );
        }
    }
}

TEST_CASE( "Built-in geometry tool shortcuts select only implemented tools", "[map][gui][input][defaults]" )
{
    default_input_fixture_t f;
    const struct { int key; Qt::KeyboardModifiers modifiers; map_tool_t tool; } keys[]{
        { Qt::Key_B, Qt::ShiftModifier, map_tool_t::BLOCK }, { Qt::Key_X, Qt::ShiftModifier, map_tool_t::CLIP },
        { Qt::Key_A, Qt::ShiftModifier, map_tool_t::TEXTURE }, { Qt::Key_T, Qt::NoModifier, map_tool_t::TRANSLATE },
        { Qt::Key_R, Qt::NoModifier, map_tool_t::ROTATE }, { Qt::Key_E, Qt::NoModifier, map_tool_t::SCALE },
        { Qt::Key_X, Qt::NoModifier, map_tool_t::EXTRUDE }
    };
    for ( bool camera : { false, true } ) {
        for ( const auto &binding : keys ) {
            CAPTURE( camera, binding.key, static_cast<int>( binding.tool ) );
            MapWorkspace_SetTool( &f.workspace, map_tool_t::SELECT );
            QKeyEvent key( QEvent::KeyPress, binding.key, binding.modifiers );
            REQUIRE( MapInput_DispatchKey( &f.workspace, &key, camera, false, false ) );
            CHECK( f.workspace.tool == map_tool_t::SELECT );
            REQUIRE( MapInput_DispatchKey( &f.workspace, &key, camera, false, true ) );
            CHECK( f.workspace.tool == binding.tool );
        }
        MapWorkspace_SetTool( &f.workspace, map_tool_t::SELECT );
        QKeyEvent vertex( QEvent::KeyPress, Qt::Key_V, Qt::ShiftModifier );
        REQUIRE( MapInput_DispatchKey( &f.workspace, &vertex, camera, false, true ) );
        CHECK( f.workspace.tool == map_tool_t::SELECT );
    }
}

TEST_CASE( "The built-in keymap leaves unavailable toggle mouselook unbound", "[map][gui][input][defaults][camera]" )
{
    default_input_fixture_t f;
    keymap_binding_t binding{};
    CHECK( EditorKeymap_FindBindingOn( f.gui.keymapChain, f.gui.nKeymapChain, EditorKeymap_HostPlatform(),
                                      StringView_FromCString( "map.viewport.3d" ), StringView_FromCString( "map.camera.mouselook" ),
                                      &binding ) == keymap_lookup_t::NOT_DEFINED );
    QKeyEvent toggle( QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier );
    CHECK_FALSE( MapInput_DispatchKey( &f.workspace, &toggle, true, false, false ) );
    CHECK_FALSE( MapInput_DispatchKey( &f.workspace, &toggle, true, false, true ) );
}

TEST_CASE( "Built-in aliases adjust grid in 2D and camera speed in 3D while preserving keypad and modifier identity", "[map][gui][input][defaults][camera-speed]" )
{
    default_input_fixture_t f;
    const auto *speedDescriptor = EditorSettings_Find( &f.gui.settings, StringView_FromCString( "editor.camera.move_speed" ) );
    REQUIRE( speedDescriptor != nullptr );
    const auto speed = [&]() { return EditorSettings_Real( &f.gui.settings, "editor.camera.move_speed", 0.0 ); };
    const auto resetSpeed = [&]() {
        setting_value_t value{}; value.type = setting_type_t::REAL; value.flValue = 1000.0;
        REQUIRE( EditorSettings_Write( &f.gui.settings, settings_scope_t::USER, *speedDescriptor, value ) == settings_registry_status_t::OK );
    };
    QString executed;
    EditorCommands_SetObserver( &f.gui.commands,
        []( void *context, const command_desc_t &command, const command_args_t &, command_result_t ) {
            *static_cast<QString *>( context ) = QString::fromUtf8( command.pId );
        }, &executed );
    const struct { int key; Qt::KeyboardModifiers modifiers; bool larger; } keys[]{
        { Qt::Key_BracketLeft, Qt::NoModifier, false }, { Qt::Key_Minus, Qt::NoModifier, false },
        { Qt::Key_Minus, Qt::KeypadModifier, false }, { Qt::Key_BracketRight, Qt::NoModifier, true },
        { Qt::Key_Plus, Qt::NoModifier, true }, { Qt::Key_Plus, Qt::ShiftModifier, true },
        { Qt::Key_Plus, Qt::KeypadModifier, true }
    };
    for ( bool camera : { false, true } ) {
        for ( const auto &binding : keys ) {
            CAPTURE( camera, binding.key, static_cast<int>( binding.modifiers ) );
            MapWorkspace_SetGridSize( &f.workspace, 8 );
            resetSpeed(); executed.clear();
            const bool changesSpeed = camera && binding.key != Qt::Key_BracketLeft && binding.key != Qt::Key_BracketRight;
            QKeyEvent key( QEvent::KeyPress, binding.key, binding.modifiers );
            REQUIRE( MapInput_DispatchKey( &f.workspace, &key, camera, false, false ) );
            CHECK( f.workspace.gridSize == 8 ); CHECK( speed() == 1000.0 ); CHECK( executed.isEmpty() );
            REQUIRE( MapInput_DispatchKey( &f.workspace, &key, camera, false, true ) );
            CHECK( f.workspace.gridSize == ( changesSpeed ? 8 : binding.larger ? 16 : 4 ) );
            CHECK( speed() == ( changesSpeed ? ( binding.larger ? 2000.0 : 500.0 ) : 1000.0 ) );
            CHECK( executed == QString::fromLatin1( changesSpeed ?
                ( binding.larger ? "map.camera.speed_increase" : "map.camera.speed_decrease" ) :
                ( binding.larger ? "map.grid.larger" : "map.grid.smaller" ) ) );
        }
    }
    MapWorkspace_SetGridSize( &f.workspace, MAP_GRID_MAX );
    QKeyEvent plus( QEvent::KeyPress, Qt::Key_Plus, Qt::ShiftModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &plus, false, false, true ) );
    CHECK( f.workspace.gridSize == MAP_GRID_MAX );
    QKeyEvent commandPlus( QEvent::KeyPress, Qt::Key_Plus, Qt::ControlModifier );
    CHECK_FALSE( MapInput_DispatchKey( &f.workspace, &commandPlus, false, false, true ) );
    QKeyEvent equal( QEvent::KeyPress, Qt::Key_Equal, Qt::NoModifier );
    CHECK_FALSE( MapInput_DispatchKey( &f.workspace, &equal, false, false, true ) );
    resetSpeed(); executed.clear();
    REQUIRE( MapInput_DispatchKey( &f.workspace, &equal, true, false, false ) );
    CHECK( speed() == 1000.0 ); CHECK( executed.isEmpty() );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &equal, true, false, true ) );
    CHECK( speed() == 2000.0 ); CHECK( f.workspace.gridSize == MAP_GRID_MAX );
    CHECK( executed == QStringLiteral( "map.camera.speed_increase" ) );
    CHECK_FALSE( MapInput_DispatchKey( &f.workspace, &commandPlus, true, false, true ) );
    CHECK( speed() == 2000.0 ); CHECK( f.workspace.gridSize == MAP_GRID_MAX );
    EditorCommands_SetObserver( &f.gui.commands, nullptr, nullptr );
}

TEST_CASE( "Repeating the effective Clip shortcut cycles retained halves without editing the map", "[map][gui][input][defaults][clip]" )
{
    default_input_fixture_t f;
    auto &ws = f.workspace;
    map_bounds_t box{};
    MapBounds_AddPoint( box, { 0, 0, 0 } ); MapBounds_AddPoint( box, { 128, 128, 128 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) );
    const auto *document = ws.pDocument;
    const auto revision = document->geometry.revision;
    const usize steps = EditorHistory_StepCount( &ws.history );
    const u64 selected = ws.selection.ids.pData[0];
    QKeyEvent clip( QEvent::KeyPress, Qt::Key_X, Qt::ShiftModifier );
    const auto modeIs = [&]( map_brush_clip_mode_t expected ) { CHECK( MapWorkspace_ClipMode( &ws ) == expected ); };
    const auto unchangedMap = [&]() {
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
        CHECK( ws.wire.objects.nCount == 1u ); CHECK( ws.selection.ids.nCount == 1u );
        CHECK( ws.selection.ids.pData[0] == selected ); CHECK( EditorHistory_StepCount( &ws.history ) == steps );
    };
    const auto writeMode = [&]( const char *text ) {
        const auto *descriptor = EditorSettings_Find( &f.gui.settings, StringView_FromCString( "editor.map.clip_mode" ) );
        REQUIRE( descriptor != nullptr );
        setting_value_t value{}; value.type = setting_type_t::ENUM; value.text = StringView_FromCString( text );
        REQUIRE( EditorSettings_Write( &f.gui.settings, settings_scope_t::USER, *descriptor, value ) == settings_registry_status_t::OK );
    };

    SECTION( "First activation preserves the configured half-space" ) {
        writeMode( "front" );
        REQUIRE( MapInput_DispatchKey( &ws, &clip, false, false, false ) );
        CHECK( ws.tool == map_tool_t::SELECT ); modeIs( map_brush_clip_mode_t::FRONT ); unchangedMap();
        REQUIRE( MapInput_DispatchKey( &ws, &clip, false, false, true ) );
        CHECK( ws.tool == map_tool_t::CLIP ); modeIs( map_brush_clip_mode_t::FRONT ); unchangedMap();
        REQUIRE( MapInput_DispatchKey( &ws, &clip, true, false, true ) );
        modeIs( map_brush_clip_mode_t::BOTH ); unchangedMap();
    }
    SECTION( "A staged plane survives preflight, idempotent direct activation and every cycle" ) {
        modeIs( map_brush_clip_mode_t::BOTH );
        REQUIRE( MapInput_DispatchKey( &ws, &clip, false, false, true ) );
        REQUIRE( ws.tool == map_tool_t::CLIP ); modeIs( map_brush_clip_mode_t::BOTH );
        const cypher::math::planed_t plane{ { 1, 0, 0 }, -64 };
        MapWorkspace_SetClipPreview( &ws, plane );
        REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.bClip ); REQUIRE( ws.editPreview.status == map_status_t::OK );
        REQUIRE( ws.editPreviewWire.objects.nCount == 2u );
        for ( const auto expected : { map_brush_clip_mode_t::BACK, map_brush_clip_mode_t::FRONT,
                                    map_brush_clip_mode_t::BOTH, map_brush_clip_mode_t::BACK } ) {
            CAPTURE( static_cast<int>( expected ) );
            const auto previous = MapWorkspace_ClipMode( &ws );
            MapWorkspace_SetTool( &ws, map_tool_t::CLIP );
            modeIs( previous );
            REQUIRE( MapInput_DispatchKey( &ws, &clip, true, false, false ) );
            modeIs( previous ); unchangedMap();
            REQUIRE( MapInput_DispatchKey( &ws, &clip, true, false, true ) );
            modeIs( expected ); unchangedMap();
            REQUIRE( ws.editPreview.bActive ); REQUIRE( ws.editPreview.bClip ); CHECK( ws.editPreview.status == map_status_t::OK );
            CHECK( ws.editPreview.clipMode == expected ); CHECK( ws.editPreview.clipPlane.normal.x == 1.0 );
            CHECK( ws.editPreview.clipPlane.normal.y == 0.0 ); CHECK( ws.editPreview.clipPlane.normal.z == 0.0 ); CHECK( ws.editPreview.clipPlane.d == -64.0 );
            REQUIRE( ws.editPreviewWire.objects.nCount == ( expected == map_brush_clip_mode_t::BOTH ? 2u : 1u ) );
            if ( expected != map_brush_clip_mode_t::BOTH ) {
                const auto &bounds = ws.editPreviewWire.objects.pData[0].bounds.box;
                CHECK( bounds.minimum.x == ( expected == map_brush_clip_mode_t::BACK ? 0.0 : 64.0 ) );
                CHECK( bounds.maximum.x == ( expected == map_brush_clip_mode_t::BACK ? 64.0 : 128.0 ) );
            }
        }
        REQUIRE( gui::EditorGui_AddKeymap( &f.gui, QStringLiteral( R"cykv(@cykv 1
@schema "cypher.editor_keymap" 2
{ id = "clip_shortcut" base = "cypher_default" bindings = { map = { "map.tool.clip" = [ "Shift+K" ] } } }
)cykv" ) ) == gui::editor_gui_status_t::OK );
        REQUIRE( gui::EditorGui_SelectKeymap( &f.gui, StringView_FromCString( "clip_shortcut" ) ) == gui::editor_gui_status_t::OK );
        CHECK_FALSE( MapInput_DispatchKey( &ws, &clip, false, false, true ) ); modeIs( map_brush_clip_mode_t::BACK );
        QKeyEvent remapped( QEvent::KeyPress, Qt::Key_K, Qt::ShiftModifier );
        REQUIRE( MapInput_DispatchKey( &ws, &remapped, false, false, false ) ); modeIs( map_brush_clip_mode_t::BACK );
        REQUIRE( MapInput_DispatchKey( &ws, &remapped, false, false, true ) ); modeIs( map_brush_clip_mode_t::FRONT ); unchangedMap();
        REQUIRE( ws.editPreview.bActive ); CHECK( ws.editPreview.clipPlane.d == plane.d );
        REQUIRE( ws.editPreviewWire.objects.nCount == 1u ); CHECK( ws.editPreviewWire.objects.pData[0].bounds.box.minimum.x == 64.0 );
    }
}

TEST_CASE( "Built-in material apply and inspector chords resolve working commands", "[map][gui][input][defaults]" )
{
    default_input_fixture_t f;
    execution_t inspect;
    const command_desc_t command{ "view.properties.open", "Inspect", "", "", nullptr, COMMAND_FLAG_NONE, Execute, State, &inspect };
    REQUIRE( EditorCommands_Register( &f.gui.commands, &command, 1 ) == command_registry_status_t::OK );
    QKeyEvent inspector( QEvent::KeyPress, Qt::Key_Return, Qt::AltModifier );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &inspector, true, false, true ) );
    CHECK( inspect.nCalls == 1 );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &inspector, true ) == map_tool_gesture_key_t::NONE );
    QKeyEvent apply( QEvent::KeyPress, Qt::Key_T, Qt::ShiftModifier );
    const auto revision = f.workspace.pDocument->geometry.revision;
    REQUIRE( MapInput_DispatchKey( &f.workspace, &apply, true, false, true ) );
    CHECK( f.workspace.pDocument->geometry.revision == revision ); // No face: unavailable.
    map_bounds_t box{};
    box.bHas = CY_TRUE; box.box = { { 0, 0, 0 }, { 64, 64, 64 } };
    REQUIRE( MapWorkspace_CreateBox( &f.workspace, box ) );
    const u64 id = f.workspace.selection.ids.pData[0];
    const auto *brush = cypher::editor::geometry::GeometryDocument_FindBrush( &f.workspace.pDocument->geometry, { id } );
    REQUIRE( brush != nullptr );
    const u64 side = brush->sides.pData[0].sourceId.value;
    MapWorkspace_SelectBrushFace( &f.workspace, id, side );
    const auto *descriptor = EditorSettings_Find( &f.gui.settings, StringView_FromCString( "editor.map.default_material" ) );
    REQUIRE( descriptor != nullptr );
    setting_value_t material{};
    material.type = descriptor->type; material.text = StringView_FromCString( "materials/dev/test_apply.cymat" );
    REQUIRE( EditorSettings_Write( &f.gui.settings, settings_scope_t::USER, *descriptor, material ) == settings_registry_status_t::OK );
    REQUIRE( MapInput_DispatchKey( &f.workspace, &apply, true, false, false ) );
    const auto beforeApply = f.workspace.pDocument->geometry.revision;
    REQUIRE( MapInput_DispatchKey( &f.workspace, &apply, true, false, true ) );
    CHECK( f.workspace.pDocument->geometry.revision > beforeApply );
    brush = cypher::editor::geometry::GeometryDocument_FindBrush( &f.workspace.pDocument->geometry, { id } );
    const auto *attributes = cypher::editor::geometry::GeometryDocument_FindBrushAttributes( &f.workspace.pDocument->geometry, { id } );
    REQUIRE( brush != nullptr ); REQUIRE( attributes != nullptr );
    const auto ref = attributes->records.pData[brush->sides.pData[0].iAttributeIndex].material.value;
    CHECK( StringView_Equals( MapMaterials_Path( &f.workspace.pDocument->materials, ref ), material.text ) );
    QKeyEvent enter( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
    QKeyEvent numEnter( QEvent::KeyPress, Qt::Key_Enter, Qt::KeypadModifier );
    QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &enter, true ) == map_tool_gesture_key_t::CONFIRM );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &numEnter, true ) == map_tool_gesture_key_t::CONFIRM );
    CHECK( MapInput_ToolGestureKey( &f.workspace, &escape, true ) == map_tool_gesture_key_t::CANCEL );
}

TEST_CASE( "The built-in Merge shortcut dispatches one undoable union in either viewport family", "[map][gui][input][defaults][merge]" )
{
    default_input_fixture_t f;
    auto &ws = f.workspace;
    map_bounds_t box{};
    MapBounds_AddPoint( box, { 0, 0, 0 } ); MapBounds_AddPoint( box, { 64, 64, 64 } );
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 first = EditorSelection_At( &ws.selection, 0 );
    box.box.minimum.x = 64; box.box.maximum.x = 128;
    REQUIRE( MapWorkspace_CreateBox( &ws, box ) ); const u64 second = EditorSelection_At( &ws.selection, 0 );
    const u64 pair[]{ first, second };
    MapWorkspace_SetSelection( &ws, pair, 2 );
    const usize steps = EditorHistory_StepCount( &ws.history );
    QKeyEvent merge( QEvent::KeyPress, Qt::Key_M, Qt::ControlModifier | Qt::ShiftModifier );
    for ( bool camera : { false, true } ) {
        CAPTURE( camera );
        const auto *document = ws.pDocument;
        const auto revision = document->geometry.revision;
        REQUIRE( MapInput_DispatchKey( &ws, &merge, camera, false, false ) );
        CHECK( ws.pDocument == document ); CHECK( ws.pDocument->geometry.revision == revision );
        CHECK( ws.wire.objects.nCount == 2u );
        REQUIRE( MapInput_DispatchKey( &ws, &merge, camera, false, true ) );
        REQUIRE( ws.wire.objects.nCount == 1u );
        CHECK( ws.wire.objects.pData[0].bounds.box.minimum.x == 0.0 );
        CHECK( ws.wire.objects.pData[0].bounds.box.maximum.x == 128.0 );
        CHECK( ws.tool == map_tool_t::SELECT );
        CHECK( EditorHistory_StepCount( &ws.history ) == steps + 1u );
        REQUIRE( MapWorkspace_Undo( &ws ) == editor_history_status_t::OK );
        CHECK( ws.wire.objects.nCount == 2u ); CHECK( EditorSelection_Count( &ws.selection ) == 2u );
    }
    MapWorkspace_Select( &ws, first, MAP_SELECT_REPLACE );
    const auto *document = ws.pDocument;
    REQUIRE( MapInput_DispatchKey( &ws, &merge, false, false, true ) ); // A disabled bound command is consumed without editing.
    CHECK( ws.pDocument == document ); CHECK( ws.wire.objects.nCount == 2u );
}
