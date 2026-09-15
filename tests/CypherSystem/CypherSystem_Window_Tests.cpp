//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherSystem/CypherSystem_Window_Tests.cpp
//  Purpose: Verifies SDL window ownership and native-to-System event translation.
//  Details: SDL's headless video driver keeps this test portable in CI while
//           synthetic events exercise the same pump used by the runtime.
//
//  History:
//  - Created by Karlo Siric on 2026-08-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSystem_Window.h"

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <string>

using namespace cypher::engine::sys;

TEST_CASE( "System translates SDL window and input events", "[CypherSystem][Window][Events]" )
{
    REQUIRE( Sys_SetEnvironment( "SDL_VIDEODRIVER", "dummy" ) );

    window_desc_t description{};
    description.title = "CypherSystem Window Test";
    description.width = 640u;
    description.height = 360u;
    description.graphicsApi = window_graphics_api_t::NONE;

    REQUIRE( Sys_WindowDescIsValid( description ) );
    window_desc_t malformedDescription = description;
    malformedDescription.title = nullptr;
    REQUIRE_FALSE( Sys_WindowDescIsValid( malformedDescription ) );
    malformedDescription = description;
    malformedDescription.flags = CYPHER_BIT32( 31 );
    REQUIRE_FALSE( Sys_WindowDescIsValid( malformedDescription ) );
    malformedDescription = description;
    malformedDescription.sizeLimits = { 800u, 600u, 640u, 480u };
    REQUIRE_FALSE( Sys_WindowDescIsValid( malformedDescription ) );
    malformedDescription = description;
    malformedDescription.exclusiveMode.width = 1920u;
    REQUIRE_FALSE( Sys_WindowDescIsValid( malformedDescription ) );

    window_t window{};
    REQUIRE( Sys_CreateWindow( description, window ) == sys_error_t::ERR_NOT_INIT );

    const std::filesystem::path userPath = std::filesystem::temp_directory_path() /
        ( "cypher_system_window_" + std::to_string( Sys_GetCurrentProcessId() ) );
    const std::string userPathString = userPath.string();
    const char *arguments[] = {
        "cypher_system_window_tests",
        "-basedir",
        ".",
        "-userpath",
        userPathString.c_str()
    };
    const init_info_t initInfo = {
        5,
        arguments,
        "CypherSystemWindowTests",
        "CypherTests"
    };
    REQUIRE( Sys_Init( initInfo ) == sys_error_t::OK );

    display_list_result_t displayList{};
    REQUIRE( Sys_GetDisplays( nullptr, 0u, displayList ) == sys_error_t::OK );
    REQUIRE( displayList.displaysRequired > 0u );
    REQUIRE( displayList.displaysWritten == 0u );

    sys_display_id_t displays[8]{};
    REQUIRE( Sys_GetDisplays( displays, 8u, displayList ) == sys_error_t::OK );
    REQUIRE( displayList.displaysWritten > 0u );

    sys_display_id_t primaryDisplay = SYS_INVALID_DISPLAY_ID;
    REQUIRE( Sys_GetPrimaryDisplay( primaryDisplay ) == sys_error_t::OK );
    REQUIRE( primaryDisplay != SYS_INVALID_DISPLAY_ID );

    display_info_t displayInfo{};
    REQUIRE( Sys_GetDisplayInfo( primaryDisplay, displayInfo ) == sys_error_t::OK );
    REQUIRE( displayInfo.id == primaryDisplay );
    REQUIRE( displayInfo.name[0] != '\0' );
    REQUIRE( displayInfo.bounds.width > 0u );
    REQUIRE( displayInfo.bounds.height > 0u );

    window_desc_t invalidDescription = description;
    invalidDescription.width = 0u;
    REQUIRE( Sys_CreateWindow( invalidDescription, window ) == sys_error_t::ERR_INVALID_ARGUMENT );

    window_desc_t invalidOpenGLDescription = description;
    invalidOpenGLDescription.graphicsApi = window_graphics_api_t::OPENGL;
    REQUIRE( Sys_CreateWindow( invalidOpenGLDescription, window ) ==
        sys_error_t::ERR_INVALID_ARGUMENT );

    REQUIRE( Sys_CreateWindow( description, window ) == sys_error_t::OK );
    REQUIRE( window.valid );
    REQUIRE( window.id != SYS_INVALID_WINDOW_ID );
    REQUIRE( window.logicalWidth == 640u );
    REQUIRE( window.logicalHeight == 360u );

    window_t secondWindow{};
    REQUIRE( Sys_CreateWindow( description, secondWindow ) == sys_error_t::ERR_RESOURCE_BUSY );
    REQUIRE( Sys_Shutdown() == sys_error_t::ERR_RESOURCE_BUSY );

    gl_context_t unavailableContext{};
    REQUIRE( GLimp_CreateContext( window, unavailableContext ) ==
        sys_error_t::ERR_INVALID_ARGUMENT );
    REQUIRE_FALSE( GLimp_IsContextValid( unavailableContext ) );

    REQUIRE( Sys_SetWindowTitle( window, "Updated Window Test" ) == sys_error_t::OK );
    REQUIRE( Sys_SetWindowFullscreen( window, false ) == sys_error_t::OK );
    REQUIRE( Sys_SetWindowSizeLimits( window, { 320u, 200u, 1920u, 1080u } ) ==
        sys_error_t::OK );
    REQUIRE( Sys_SetWindowPosition( window, 12, 24 ) == sys_error_t::OK );
    REQUIRE( Sys_SetWindowSize( window, 700u, 400u ) == sys_error_t::OK );
    REQUIRE( Sys_SetWindowResizable( window, false ) == sys_error_t::OK );
    REQUIRE( ( window.flags & SYS_WINDOW_RESIZABLE ) == 0u );
    REQUIRE( Sys_SetWindowResizable( window, true ) == sys_error_t::OK );
    REQUIRE( ( window.flags & SYS_WINDOW_RESIZABLE ) != 0u );
    REQUIRE( Sys_SetWindowBordered( window, false ) == sys_error_t::OK );
    REQUIRE( ( window.flags & SYS_WINDOW_BORDERLESS ) != 0u );
    REQUIRE( Sys_SetWindowBordered( window, true ) == sys_error_t::OK );
    REQUIRE( ( window.flags & SYS_WINDOW_BORDERLESS ) == 0u );
    REQUIRE( Sys_SetWindowAlwaysOnTop( window, true ) == sys_error_t::OK );
    REQUIRE( ( window.flags & SYS_WINDOW_ALWAYS_ON_TOP ) != 0u );
    REQUIRE( Sys_SetWindowAlwaysOnTop( window, false ) == sys_error_t::OK );
    REQUIRE( ( window.flags & SYS_WINDOW_ALWAYS_ON_TOP ) == 0u );
    REQUIRE( Sys_SetWindowMouseGrab( window, false ) == sys_error_t::OK );
    REQUIRE( Sys_HideWindow( window ) == sys_error_t::OK );
    REQUIRE_FALSE( window.visible );
    REQUIRE( Sys_ShowWindow( window ) == sys_error_t::OK );
    REQUIRE( window.visible );
    REQUIRE( Sys_SyncWindow( window ) == sys_error_t::OK );
    REQUIRE( Sys_SetRelativeMouseMode( window, false ) == sys_error_t::OK );
    REQUIRE( Sys_SetCursorVisible( true ) == sys_error_t::OK );
    REQUIRE( Sys_SetTextInputEnabled( window, true ) == sys_error_t::OK );
    REQUIRE( window.textInputEnabled );
    REQUIRE( Sys_SetTextInputEnabled( window, false ) == sys_error_t::OK );

    Sys_PollWindowEvents( window ); // Drain native creation events before injection.
    Sys_ClearEvents();

    SDL_Event nativeEvent{};
    nativeEvent.type = SDL_EVENT_KEY_DOWN;
    nativeEvent.key.type = SDL_EVENT_KEY_DOWN;
    nativeEvent.key.windowID = window.id;
    nativeEvent.key.scancode = SDL_SCANCODE_A;
    nativeEvent.key.mod = SDL_KMOD_SHIFT;
    nativeEvent.key.down = true;
    REQUIRE( SDL_PushEvent( &nativeEvent ) );

    nativeEvent = {};
    nativeEvent.type = SDL_EVENT_TEXT_INPUT;
    nativeEvent.text.type = SDL_EVENT_TEXT_INPUT;
    nativeEvent.text.windowID = window.id;
    nativeEvent.text.text = "\xC3\xA9";
    REQUIRE( SDL_PushEvent( &nativeEvent ) );

    nativeEvent = {};
    nativeEvent.type = SDL_EVENT_MOUSE_MOTION;
    nativeEvent.motion.type = SDL_EVENT_MOUSE_MOTION;
    nativeEvent.motion.windowID = window.id;
    nativeEvent.motion.x = 12.0f;
    nativeEvent.motion.y = 20.0f;
    nativeEvent.motion.xrel = 3.0f;
    nativeEvent.motion.yrel = -2.0f;
    REQUIRE( SDL_PushEvent( &nativeEvent ) );

    nativeEvent = {};
    nativeEvent.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    nativeEvent.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    nativeEvent.button.windowID = window.id;
    nativeEvent.button.button = SDL_BUTTON_LEFT;
    nativeEvent.button.down = true;
    nativeEvent.button.clicks = 2u;
    nativeEvent.button.x = 14.0f;
    nativeEvent.button.y = 22.0f;
    REQUIRE( SDL_PushEvent( &nativeEvent ) );

    nativeEvent = {};
    nativeEvent.type = SDL_EVENT_MOUSE_WHEEL;
    nativeEvent.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    nativeEvent.wheel.windowID = window.id;
    nativeEvent.wheel.x = 1.0f;
    nativeEvent.wheel.y = 2.0f;
    nativeEvent.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    REQUIRE( SDL_PushEvent( &nativeEvent ) );

    nativeEvent = {};
    nativeEvent.type = SDL_EVENT_WINDOW_RESIZED;
    nativeEvent.window.type = SDL_EVENT_WINDOW_RESIZED;
    nativeEvent.window.windowID = window.id;
    nativeEvent.window.data1 = 800;
    nativeEvent.window.data2 = 600;
    REQUIRE( SDL_PushEvent( &nativeEvent ) );

    nativeEvent = {};
    nativeEvent.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    nativeEvent.window.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    nativeEvent.window.windowID = window.id;
    nativeEvent.window.data1 = 1600;
    nativeEvent.window.data2 = 1200;
    REQUIRE( SDL_PushEvent( &nativeEvent ) );

    nativeEvent = {};
    nativeEvent.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    nativeEvent.window.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    nativeEvent.window.windowID = window.id;
    REQUIRE( SDL_PushEvent( &nativeEvent ) );

    nativeEvent = {};
    nativeEvent.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    nativeEvent.window.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    nativeEvent.window.windowID = window.id;
    REQUIRE( SDL_PushEvent( &nativeEvent ) );

    nativeEvent = {};
    nativeEvent.type = SDL_EVENT_WINDOW_MINIMIZED;
    nativeEvent.window.type = SDL_EVENT_WINDOW_MINIMIZED;
    nativeEvent.window.windowID = window.id;
    REQUIRE( SDL_PushEvent( &nativeEvent ) );

    nativeEvent = {};
    nativeEvent.type = SDL_EVENT_WINDOW_RESTORED;
    nativeEvent.window.type = SDL_EVENT_WINDOW_RESTORED;
    nativeEvent.window.windowID = window.id;
    REQUIRE( SDL_PushEvent( &nativeEvent ) );

    nativeEvent = {};
    nativeEvent.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    nativeEvent.window.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    nativeEvent.window.windowID = window.id;
    REQUIRE( SDL_PushEvent( &nativeEvent ) );

    Sys_PollWindowEvents( window );
    REQUIRE( window.logicalWidth == 800u );
    REQUIRE( window.logicalHeight == 600u );
    REQUIRE( window.width == 1600u );
    REQUIRE( window.height == 1200u );
    REQUIRE( window.focused );
    REQUIRE_FALSE( window.minimized );
    REQUIRE( Sys_WindowShouldClose( window ) );

    bool foundKey = false;
    bool foundText = false;
    bool foundMotion = false;
    bool foundButton = false;
    bool foundWheel = false;
    bool foundLogicalResize = false;
    bool foundPixelResize = false;
    bool foundFocusLost = false;
    bool foundFocusGained = false;
    bool foundMinimized = false;
    bool foundRestored = false;
    bool foundClose = false;
    sys_event_t event{};
    while ( Sys_PollEvent( event ) ) {
        switch ( event.type ) {
            case sys_event_type_t::KEY:
                foundKey = event.payload.key.key == sys_key_t::A &&
                    event.payload.key.action == sys_input_action_t::PRESSED &&
                    ( event.payload.key.modifiers & SYS_KEYMODIFIER_SHIFT ) != 0u;
                break;
            case sys_event_type_t::TEXT_INPUT:
                foundText = event.payload.textInput.byteCount == 2u &&
                    std::memcmp( event.payload.textInput.utf8, "\xC3\xA9", 2u ) == 0;
                break;
            case sys_event_type_t::MOUSE_MOTION:
                foundMotion = event.payload.mouseMotion.deltaX == 3.0f &&
                    event.payload.mouseMotion.deltaY == -2.0f;
                break;
            case sys_event_type_t::MOUSE_BUTTON:
                foundButton = event.payload.mouseButton.button == sys_mouse_button_t::LEFT &&
                    event.payload.mouseButton.action == sys_input_action_t::PRESSED &&
                    event.payload.mouseButton.clickCount == 2u &&
                    event.payload.mouseButton.x == 14.0f &&
                    event.payload.mouseButton.y == 22.0f;
                break;
            case sys_event_type_t::MOUSE_WHEEL:
                foundWheel = event.payload.mouseWheel.x == -1.0f &&
                    event.payload.mouseWheel.y == -2.0f;
                break;
            case sys_event_type_t::WINDOW_RESIZED:
                foundLogicalResize = event.payload.window.width == 800u &&
                    event.payload.window.height == 600u;
                break;
            case sys_event_type_t::WINDOW_PIXEL_SIZE_CHANGED:
                foundPixelResize = event.payload.window.width == 1600u &&
                    event.payload.window.height == 1200u;
                break;
            case sys_event_type_t::WINDOW_FOCUS_LOST:
                foundFocusLost = true;
                break;
            case sys_event_type_t::WINDOW_FOCUS_GAINED:
                foundFocusGained = true;
                break;
            case sys_event_type_t::WINDOW_MINIMIZED:
                foundMinimized = true;
                break;
            case sys_event_type_t::WINDOW_RESTORED:
                foundRestored = true;
                break;
            case sys_event_type_t::WINDOW_CLOSE_REQUESTED:
                foundClose = true;
                break;
            default:
                break;
        }
    }

    REQUIRE( foundKey );
    REQUIRE( foundText );
    REQUIRE( foundMotion );
    REQUIRE( foundButton );
    REQUIRE( foundWheel );
    REQUIRE( foundLogicalResize );
    REQUIRE( foundPixelResize );
    REQUIRE( foundFocusLost );
    REQUIRE( foundFocusGained );
    REQUIRE( foundMinimized );
    REQUIRE( foundRestored );
    REQUIRE( foundClose );

    REQUIRE( Sys_DestroyWindow( window ) == sys_error_t::OK );
    REQUIRE_FALSE( window.valid );
    REQUIRE( Sys_DestroyWindow( window ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( Sys_SetWindowTitle( window, "Invalid" ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( Sys_SetWindowFullscreen( window, false ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( Sys_SetWindowPosition( window, 0, 0 ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( Sys_SetWindowSize( window, 640u, 360u ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( Sys_SetWindowMouseGrab( window, false ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( Sys_ShowWindow( window ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( Sys_HideWindow( window ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( Sys_SetRelativeMouseMode( window, false ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( Sys_SetTextInputEnabled( window, false ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( Sys_SetCursorVisible( true ) == sys_error_t::ERR_NOT_INIT );
    REQUIRE( Sys_Shutdown() == sys_error_t::OK );
    REQUIRE( Sys_UnsetEnvironment( "SDL_VIDEODRIVER" ) );

    std::error_code error{};
    std::filesystem::remove_all( userPath, error );
}
