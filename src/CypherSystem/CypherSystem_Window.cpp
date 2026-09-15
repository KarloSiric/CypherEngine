//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_Window.cpp
//  Purpose: Implements native window state and normalized input event delivery.
//  Details: SDL remains private to CypherSystem. Host, input, console, and future
//           tools consume fixed-size sys_event_t records instead of SDL objects.
//
//  History:
//  - Created by Karlo Siric on 2026-05-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSystem_Window.h"
#include "CypherSystem_Local.h"

#include <SDL3/SDL.h> // Cross-platform native windows and event collection.

#include <algorithm> // std::min for bounded UTF-8 event chunks.
#include <cmath>     // std::isfinite for display and DPI values reported by SDL.
#include <cstring>   // std::strlen and std::memcpy for text input payloads.
#include <limits>    // Rejects dimensions that SDL's signed API cannot hold.

namespace cypher::engine::sys
{

namespace
{

SDL_Window *activeWindow = nullptr; // System V1 owns one game/runtime window.
bool ownsVideoSubsystem = false;    // Prevents CypherSystem from shutting down externally-owned SDL video.

bool Window_DimensionFitsSDL( const common::u32 value ) noexcept
{
    return value <= static_cast<common::u32>( std::numeric_limits<int>::max() );
}

bool Window_SizeLimitsAreValid( const window_size_limits_t &limits ) noexcept
{
    if ( !Window_DimensionFitsSDL( limits.minimumWidth ) ||
         !Window_DimensionFitsSDL( limits.minimumHeight ) ||
         !Window_DimensionFitsSDL( limits.maximumWidth ) ||
         !Window_DimensionFitsSDL( limits.maximumHeight ) ) {
        return false;
    }

    if ( limits.maximumWidth != 0u && limits.minimumWidth > limits.maximumWidth ) {
        return false;
    }
    if ( limits.maximumHeight != 0u && limits.minimumHeight > limits.maximumHeight ) {
        return false;
    }

    return true;
}

sys_error_t Window_EnsureVideoSubsystem() noexcept
{
    if ( ( SDL_WasInit( SDL_INIT_VIDEO ) & SDL_INIT_VIDEO ) != 0u ) {
        return sys_error_t::OK;
    }

    if ( !SDL_InitSubSystem( SDL_INIT_VIDEO ) ) {
        Sys_DebugPrintf( "CypherSystem: SDL video initialization failed: %s.\n", SDL_GetError() );
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    ownsVideoSubsystem = true;
    return sys_error_t::OK;
}

display_orientation_t Window_MapOrientation(
    const SDL_DisplayOrientation orientation ) noexcept
{
    switch ( orientation ) {
        case SDL_ORIENTATION_LANDSCAPE: return display_orientation_t::LANDSCAPE;
        case SDL_ORIENTATION_LANDSCAPE_FLIPPED: return display_orientation_t::LANDSCAPE_FLIPPED;
        case SDL_ORIENTATION_PORTRAIT: return display_orientation_t::PORTRAIT;
        case SDL_ORIENTATION_PORTRAIT_FLIPPED: return display_orientation_t::PORTRAIT_FLIPPED;
        case SDL_ORIENTATION_UNKNOWN:
        default: return display_orientation_t::UNKNOWN;
    }
}

bool Window_CopyRect( const SDL_Rect &source, window_rect_t &destination ) noexcept
{
    if ( source.w < 0 || source.h < 0 ) {
        destination = {};
        return false;
    }

    destination.x = static_cast<common::i32>( source.x );
    destination.y = static_cast<common::i32>( source.y );
    destination.width = static_cast<common::u32>( source.w );
    destination.height = static_cast<common::u32>( source.h );
    return true;
}

bool Window_CopyDisplayMode(
    const SDL_DisplayMode *source,
    display_mode_t &destination ) noexcept
{
    destination = {};
    if ( source == nullptr || source->displayID == 0u ||
         source->w <= 0 || source->h <= 0 ) {
        return false;
    }

    destination.displayId = static_cast<sys_display_id_t>( source->displayID );
    destination.width = static_cast<common::u32>( source->w );
    destination.height = static_cast<common::u32>( source->h );
    destination.pixelDensity = std::isfinite( source->pixel_density ) &&
        source->pixel_density > 0.0f ? source->pixel_density : 1.0f;
    destination.refreshRateHz = std::isfinite( source->refresh_rate ) &&
        source->refresh_rate >= 0.0f ? source->refresh_rate : 0.0f;
    destination.refreshRateNumerator = static_cast<common::i32>(
        source->refresh_rate_numerator );
    destination.refreshRateDenominator = static_cast<common::i32>(
        source->refresh_rate_denominator );
    return true;
}

window_flags_t Window_FlagsFromSDL( const SDL_WindowFlags nativeFlags ) noexcept
{
    window_flags_t flags = SYS_WINDOW_NONE;
    if ( ( nativeFlags & SDL_WINDOW_RESIZABLE ) != 0u ) flags |= SYS_WINDOW_RESIZABLE;
    if ( ( nativeFlags & SDL_WINDOW_HIGH_PIXEL_DENSITY ) != 0u ) flags |= SYS_WINDOW_HIGH_PIXEL_DENSITY;
    if ( ( nativeFlags & SDL_WINDOW_HIDDEN ) != 0u ) flags |= SYS_WINDOW_HIDDEN;
    if ( ( nativeFlags & SDL_WINDOW_BORDERLESS ) != 0u ) flags |= SYS_WINDOW_BORDERLESS;
    if ( ( nativeFlags & SDL_WINDOW_ALWAYS_ON_TOP ) != 0u ) flags |= SYS_WINDOW_ALWAYS_ON_TOP;
    return flags;
}

window_mode_t Window_ModeFromSDL(
    SDL_Window *window,
    const SDL_WindowFlags nativeFlags ) noexcept
{
    if ( ( nativeFlags & SDL_WINDOW_FULLSCREEN ) == 0u ) {
        return window_mode_t::WINDOWED;
    }

    return SDL_GetWindowFullscreenMode( window ) == nullptr
        ? window_mode_t::BORDERLESS_FULLSCREEN
        : window_mode_t::EXCLUSIVE_FULLSCREEN;
}

sys_error_t Window_RefreshState( SDL_Window *nativeWindow, window_t &window ) noexcept
{
    int logicalWidth = 0;
    int logicalHeight = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    int x = 0;
    int y = 0;

    if ( !SDL_GetWindowSize( nativeWindow, &logicalWidth, &logicalHeight ) ||
         !SDL_GetWindowSizeInPixels( nativeWindow, &pixelWidth, &pixelHeight ) ||
         !SDL_GetWindowPosition( nativeWindow, &x, &y ) ||
         logicalWidth < 0 || logicalHeight < 0 || pixelWidth < 0 || pixelHeight < 0 ) {
        Sys_DebugPrintf( "CypherSystem: SDL window-state query failed: %s.\n", SDL_GetError() );
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    const SDL_WindowFlags nativeFlags = SDL_GetWindowFlags( nativeWindow );
    const SDL_DisplayID displayId = SDL_GetDisplayForWindow( nativeWindow );
    const common::f32 pixelDensity = SDL_GetWindowPixelDensity( nativeWindow );
    const common::f32 displayScale = SDL_GetWindowDisplayScale( nativeWindow );

    window.logicalWidth = static_cast<common::u32>( logicalWidth );
    window.logicalHeight = static_cast<common::u32>( logicalHeight );
    window.width = static_cast<common::u32>( pixelWidth );
    window.height = static_cast<common::u32>( pixelHeight );
    window.x = static_cast<common::i32>( x );
    window.y = static_cast<common::i32>( y );
    window.displayId = static_cast<sys_display_id_t>( displayId );
    window.pixelDensity = std::isfinite( pixelDensity ) && pixelDensity > 0.0f
        ? pixelDensity : 1.0f;
    window.displayScale = std::isfinite( displayScale ) && displayScale > 0.0f
        ? displayScale : 1.0f;
    window.mode = Window_ModeFromSDL( nativeWindow, nativeFlags );
    window.flags = Window_FlagsFromSDL( nativeFlags );
    window.fullscreen = window.mode != window_mode_t::WINDOWED;
    window.focused = ( nativeFlags & SDL_WINDOW_INPUT_FOCUS ) != 0u;
    window.mouseFocused = ( nativeFlags & SDL_WINDOW_MOUSE_FOCUS ) != 0u;
    window.minimized = ( nativeFlags & SDL_WINDOW_MINIMIZED ) != 0u;
    window.maximized = ( nativeFlags & SDL_WINDOW_MAXIMIZED ) != 0u;
    window.visible = ( nativeFlags & SDL_WINDOW_HIDDEN ) == 0u;
    window.mouseGrabbed = ( nativeFlags & SDL_WINDOW_MOUSE_GRABBED ) != 0u;
    window.relativeMouseEnabled = ( nativeFlags & SDL_WINDOW_MOUSE_RELATIVE_MODE ) != 0u;
    return sys_error_t::OK;
}

sys_error_t Window_ApplySizeLimits(
    SDL_Window *nativeWindow,
    const window_size_limits_t &limits ) noexcept
{
    if ( !Window_SizeLimitsAreValid( limits ) ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    if ( !SDL_SetWindowMinimumSize(
             nativeWindow,
             static_cast<int>( limits.minimumWidth ),
             static_cast<int>( limits.minimumHeight ) ) ||
         !SDL_SetWindowMaximumSize(
             nativeWindow,
             static_cast<int>( limits.maximumWidth ),
             static_cast<int>( limits.maximumHeight ) ) ) {
        Sys_DebugPrintf( "CypherSystem: SDL rejected window size limits: %s.\n", SDL_GetError() );
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    return sys_error_t::OK;
}

sys_error_t Window_CenterOnDisplay(
    SDL_Window *nativeWindow,
    const sys_display_id_t requestedDisplay,
    const common::u32 windowWidth,
    const common::u32 windowHeight ) noexcept
{
    SDL_DisplayID displayId = static_cast<SDL_DisplayID>( requestedDisplay );
    if ( displayId == 0u ) {
        displayId = SDL_GetPrimaryDisplay();
    }

    SDL_Rect bounds{};
    if ( displayId == 0u || !SDL_GetDisplayBounds( displayId, &bounds ) ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    const int centeredX = bounds.x + ( bounds.w - static_cast<int>( windowWidth ) ) / 2;
    const int centeredY = bounds.y + ( bounds.h - static_cast<int>( windowHeight ) ) / 2;
    if ( !SDL_SetWindowPosition( nativeWindow, centeredX, centeredY ) ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    return sys_error_t::OK;
}

sys_error_t Window_SetMode(
    SDL_Window *nativeWindow,
    const window_mode_t mode,
    const display_mode_t *exclusiveMode ) noexcept
{
    if ( mode >= window_mode_t::COUNT ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    if ( mode == window_mode_t::WINDOWED ) {
        if ( !SDL_SetWindowFullscreenMode( nativeWindow, nullptr ) ||
             !SDL_SetWindowFullscreen( nativeWindow, false ) ) {
            return sys_error_t::ERR_INTERNAL_ERROR;
        }
        return sys_error_t::OK;
    }

    if ( mode == window_mode_t::BORDERLESS_FULLSCREEN ) {
        if ( !SDL_SetWindowFullscreenMode( nativeWindow, nullptr ) ||
             !SDL_SetWindowFullscreen( nativeWindow, true ) ) {
            return sys_error_t::ERR_INTERNAL_ERROR;
        }
        return sys_error_t::OK;
    }

    if ( exclusiveMode == nullptr || exclusiveMode->width == 0u ||
         exclusiveMode->height == 0u ||
         !Window_DimensionFitsSDL( exclusiveMode->width ) ||
         !Window_DimensionFitsSDL( exclusiveMode->height ) ||
         !std::isfinite( exclusiveMode->refreshRateHz ) ||
         exclusiveMode->refreshRateHz < 0.0f ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    SDL_DisplayID displayId = static_cast<SDL_DisplayID>( exclusiveMode->displayId );
    if ( displayId == 0u ) {
        displayId = SDL_GetDisplayForWindow( nativeWindow );
    }
    if ( displayId == 0u ) {
        displayId = SDL_GetPrimaryDisplay();
    }

    SDL_DisplayMode closest{};
    if ( displayId == 0u ||
         !SDL_GetClosestFullscreenDisplayMode(
             displayId,
             static_cast<int>( exclusiveMode->width ),
             static_cast<int>( exclusiveMode->height ),
             exclusiveMode->refreshRateHz,
             true,
             &closest ) ||
         !SDL_SetWindowFullscreenMode( nativeWindow, &closest ) ||
         !SDL_SetWindowFullscreen( nativeWindow, true ) ) {
        Sys_DebugPrintf( "CypherSystem: SDL rejected fullscreen mode: %s.\n", SDL_GetError() );
        return sys_error_t::ERR_GRAPHICS_CONFIG_UNSUPPORTED;
    }

    return sys_error_t::OK;
}

SDL_Window *Window_GetNative( window_t &window ) noexcept
{
    return static_cast<SDL_Window *>( window.nativeWindow );
}

const SDL_Window *Window_GetNative( const window_t &window ) noexcept
{
    return static_cast<const SDL_Window *>( window.nativeWindow );
}

bool Window_IsUsable( const window_t &window ) noexcept
{
    return window.valid && window.nativeWindow != nullptr &&
        window.nativeWindow == activeWindow &&
        window.id != SYS_INVALID_WINDOW_ID;
}

void Window_QueueEvent( const sys_event_t &event ) noexcept
{
    // Overflow telemetry is retained by CypherSystem. The native pump must keep
    // draining SDL even when one frame produces more events than the queue holds.
    (void)Sys_QueueEvent( event );
}

sys_key_t Window_MapScancode( const SDL_Scancode scancode ) noexcept
{
    if ( scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z ) {
        const common::u16 offset = static_cast<common::u16>(
            scancode - SDL_SCANCODE_A );
        return static_cast<sys_key_t>(
            static_cast<common::u16>( sys_key_t::A ) + offset );
    }

    if ( scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9 ) {
        const common::u16 offset = static_cast<common::u16>(
            scancode - SDL_SCANCODE_1 );
        return static_cast<sys_key_t>(
            static_cast<common::u16>( sys_key_t::DIGIT_1 ) + offset );
    }

    if ( scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12 ) {
        const common::u16 offset = static_cast<common::u16>(
            scancode - SDL_SCANCODE_F1 );
        return static_cast<sys_key_t>(
            static_cast<common::u16>( sys_key_t::F1 ) + offset );
    }

    if ( scancode >= SDL_SCANCODE_F13 && scancode <= SDL_SCANCODE_F24 ) {
        const common::u16 offset = static_cast<common::u16>(
            scancode - SDL_SCANCODE_F13 );
        return static_cast<sys_key_t>(
            static_cast<common::u16>( sys_key_t::F13 ) + offset );
    }

    switch ( scancode ) {
        case SDL_SCANCODE_0: return sys_key_t::DIGIT_0;
        case SDL_SCANCODE_RETURN: return sys_key_t::ENTER;
        case SDL_SCANCODE_ESCAPE: return sys_key_t::ESCAPE;
        case SDL_SCANCODE_BACKSPACE: return sys_key_t::BACKSPACE;
        case SDL_SCANCODE_TAB: return sys_key_t::TAB;
        case SDL_SCANCODE_SPACE: return sys_key_t::SPACE;
        case SDL_SCANCODE_MINUS: return sys_key_t::MINUS;
        case SDL_SCANCODE_EQUALS: return sys_key_t::EQUALS;
        case SDL_SCANCODE_LEFTBRACKET: return sys_key_t::LEFT_BRACKET;
        case SDL_SCANCODE_RIGHTBRACKET: return sys_key_t::RIGHT_BRACKET;
        case SDL_SCANCODE_BACKSLASH:        // FIXME: we will determine what happens here alright...
        case SDL_SCANCODE_NONUSBACKSLASH: return sys_key_t::BACKSLASH;
        case SDL_SCANCODE_SEMICOLON: return sys_key_t::SEMICOLON;
        case SDL_SCANCODE_APOSTROPHE: return sys_key_t::APOSTROPHE;
        case SDL_SCANCODE_GRAVE: return sys_key_t::GRAVE;
        case SDL_SCANCODE_COMMA: return sys_key_t::COMMA;
        case SDL_SCANCODE_PERIOD: return sys_key_t::PERIOD;
        case SDL_SCANCODE_SLASH: return sys_key_t::SLASH;
        case SDL_SCANCODE_CAPSLOCK: return sys_key_t::CAPS_LOCK;
        case SDL_SCANCODE_PRINTSCREEN: return sys_key_t::PRINT_SCREEN;
        case SDL_SCANCODE_SCROLLLOCK: return sys_key_t::SCROLL_LOCK;
        case SDL_SCANCODE_PAUSE: return sys_key_t::PAUSE;
        case SDL_SCANCODE_INSERT: return sys_key_t::INSERT;
        case SDL_SCANCODE_HOME: return sys_key_t::HOME;
        case SDL_SCANCODE_PAGEUP: return sys_key_t::PAGE_UP;
        case SDL_SCANCODE_DELETE: return sys_key_t::DELETE_KEY;
        case SDL_SCANCODE_END: return sys_key_t::END;
        case SDL_SCANCODE_PAGEDOWN: return sys_key_t::PAGE_DOWN;
        case SDL_SCANCODE_RIGHT: return sys_key_t::RIGHT;
        case SDL_SCANCODE_LEFT: return sys_key_t::LEFT;
        case SDL_SCANCODE_DOWN: return sys_key_t::DOWN;
        case SDL_SCANCODE_UP: return sys_key_t::UP;
        case SDL_SCANCODE_NUMLOCKCLEAR: return sys_key_t::NUM_LOCK;
        case SDL_SCANCODE_KP_DIVIDE: return sys_key_t::KEYPAD_DIVIDE;
        case SDL_SCANCODE_KP_MULTIPLY: return sys_key_t::KEYPAD_MULTIPLY;
        case SDL_SCANCODE_KP_MINUS: return sys_key_t::KEYPAD_SUBTRACT;
        case SDL_SCANCODE_KP_PLUS: return sys_key_t::KEYPAD_ADD;
        case SDL_SCANCODE_KP_ENTER: return sys_key_t::KEYPAD_ENTER;
        case SDL_SCANCODE_KP_0: return sys_key_t::KEYPAD_0;
        case SDL_SCANCODE_KP_1: return sys_key_t::KEYPAD_1;
        case SDL_SCANCODE_KP_2: return sys_key_t::KEYPAD_2;
        case SDL_SCANCODE_KP_3: return sys_key_t::KEYPAD_3;
        case SDL_SCANCODE_KP_4: return sys_key_t::KEYPAD_4;
        case SDL_SCANCODE_KP_5: return sys_key_t::KEYPAD_5;
        case SDL_SCANCODE_KP_6: return sys_key_t::KEYPAD_6;
        case SDL_SCANCODE_KP_7: return sys_key_t::KEYPAD_7;
        case SDL_SCANCODE_KP_8: return sys_key_t::KEYPAD_8;
        case SDL_SCANCODE_KP_9: return sys_key_t::KEYPAD_9;
        case SDL_SCANCODE_KP_PERIOD: return sys_key_t::KEYPAD_DECIMAL;
        case SDL_SCANCODE_KP_EQUALS: return sys_key_t::KEYPAD_EQUALS;
        case SDL_SCANCODE_APPLICATION: return sys_key_t::APPLICATION;
        case SDL_SCANCODE_LCTRL: return sys_key_t::LEFT_CONTROL;
        case SDL_SCANCODE_LSHIFT: return sys_key_t::LEFT_SHIFT;
        case SDL_SCANCODE_LALT: return sys_key_t::LEFT_ALT;
        case SDL_SCANCODE_LGUI: return sys_key_t::LEFT_SUPER;
        case SDL_SCANCODE_RCTRL: return sys_key_t::RIGHT_CONTROL;
        case SDL_SCANCODE_RSHIFT: return sys_key_t::RIGHT_SHIFT;
        case SDL_SCANCODE_RALT: return sys_key_t::RIGHT_ALT;
        case SDL_SCANCODE_RGUI: return sys_key_t::RIGHT_SUPER;
        default: return sys_key_t::NONE;
    }
}

sys_key_modifiers_t Window_MapModifiers( const SDL_Keymod modifiers ) noexcept
{
    sys_key_modifiers_t result = SYS_KEYMODIFIER_NONE;
    if ( ( modifiers & SDL_KMOD_SHIFT ) != 0u ) result |= SYS_KEYMODIFIER_SHIFT;
    if ( ( modifiers & SDL_KMOD_CTRL ) != 0u ) result |= SYS_KEYMODIFIER_CONTROL;
    if ( ( modifiers & SDL_KMOD_ALT ) != 0u ) result |= SYS_KEYMODIFIER_ALT;
    if ( ( modifiers & SDL_KMOD_GUI ) != 0u ) result |= SYS_KEYMODIFIER_SUPER;
    if ( ( modifiers & SDL_KMOD_CAPS ) != 0u ) result |= SYS_KEYMODIFIER_CAPS_LOCK;
    if ( ( modifiers & SDL_KMOD_NUM ) != 0u ) result |= SYS_KEYMODIFIER_NUM_LOCK;
    if ( ( modifiers & SDL_KMOD_SCROLL ) != 0u ) result |= SYS_KEYMODIFIER_SCROLL_LOCK;
    return result;
}

sys_mouse_button_t Window_MapMouseButton( const common::u8 button ) noexcept
{
    switch ( button ) {
        case SDL_BUTTON_LEFT: return sys_mouse_button_t::LEFT;
        case SDL_BUTTON_MIDDLE: return sys_mouse_button_t::MIDDLE;
        case SDL_BUTTON_RIGHT: return sys_mouse_button_t::RIGHT;
        case SDL_BUTTON_X1: return sys_mouse_button_t::X1;
        case SDL_BUTTON_X2: return sys_mouse_button_t::X2;
        default: return sys_mouse_button_t::NONE;
    }
}

void Window_QueueWindowEvent(
    const sys_event_type_t type,
    const SDL_WindowEvent &nativeEvent ) noexcept
{
    sys_event_t event{};
    event.type = type;
    event.timestampNanoseconds = Sys_TimeNowNanoseconds();
    event.payload.window.windowId = static_cast<sys_window_id_t>( nativeEvent.windowID );
    event.payload.window.width = nativeEvent.data1 > 0
        ? static_cast<common::u32>( nativeEvent.data1 )
        : 0u;
    event.payload.window.height = nativeEvent.data2 > 0
        ? static_cast<common::u32>( nativeEvent.data2 )
        : 0u;
    Window_QueueEvent( event );
}

void Window_QueueTextInput( const SDL_TextInputEvent &nativeEvent ) noexcept
{
    if ( nativeEvent.text == nullptr || nativeEvent.text[0] == '\0' ) {
        return;
    }

    const char *read = nativeEvent.text;
    common::usize bytesRemaining = std::strlen( read );
    const common::u64 timestamp = Sys_TimeNowNanoseconds();

    while ( bytesRemaining > 0u ) {
        common::usize byteCount = std::min(
            bytesRemaining,
            static_cast<common::usize>( SYS_TEXT_INPUT_CAPACITY - 1u ) );

        // If this is not the final chunk, back up to a UTF-8 code-point boundary.
        if ( byteCount < bytesRemaining ) {
            while ( byteCount > 0u &&
                    ( static_cast<unsigned char>( read[byteCount] ) & 0xC0u ) == 0x80u ) {
                --byteCount;
            }
        }
        if ( byteCount == 0u ) {
            byteCount = 1u; // SDL promises UTF-8; this only guarantees progress on malformed input.
        }

        sys_event_t event{};
        event.type = sys_event_type_t::TEXT_INPUT;
        event.timestampNanoseconds = timestamp;
        event.payload.textInput.windowId =
            static_cast<sys_window_id_t>( nativeEvent.windowID );
        event.payload.textInput.byteCount = static_cast<common::u8>( byteCount );
        std::memcpy( event.payload.textInput.utf8, read, byteCount );
        event.payload.textInput.utf8[byteCount] = '\0';
        Window_QueueEvent( event );

        read += byteCount;
        bytesRemaining -= byteCount;
    }
}

void Window_ProcessEvent( const SDL_Event &nativeEvent, window_t &window ) noexcept
{
    switch ( nativeEvent.type ) {
        case SDL_EVENT_QUIT: {
            sys_event_t event{};
            event.type = sys_event_type_t::QUIT_REQUESTED;
            event.timestampNanoseconds = Sys_TimeNowNanoseconds();
            Window_QueueEvent( event );
            Sys_RequestQuit();
            window.shouldClose = true;
            break;
        }

        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            Window_QueueWindowEvent( sys_event_type_t::WINDOW_CLOSE_REQUESTED, nativeEvent.window );
            if ( nativeEvent.window.windowID == window.id ) {
                window.shouldClose = true;
            }
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            Window_QueueWindowEvent( sys_event_type_t::WINDOW_RESIZED, nativeEvent.window );
            if ( nativeEvent.window.windowID == window.id &&
                 nativeEvent.window.data1 > 0 && nativeEvent.window.data2 > 0 ) {
                window.logicalWidth = static_cast<common::u32>( nativeEvent.window.data1 );
                window.logicalHeight = static_cast<common::u32>( nativeEvent.window.data2 );
            }
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            Window_QueueWindowEvent(
                sys_event_type_t::WINDOW_PIXEL_SIZE_CHANGED,
                nativeEvent.window );
            if ( nativeEvent.window.windowID == window.id &&
                 nativeEvent.window.data1 > 0 && nativeEvent.window.data2 > 0 ) {
                window.width = static_cast<common::u32>( nativeEvent.window.data1 );
                window.height = static_cast<common::u32>( nativeEvent.window.data2 );
            }
            break;

        case SDL_EVENT_WINDOW_SHOWN:
            if ( nativeEvent.window.windowID == window.id ) window.visible = true;
            break;

        case SDL_EVENT_WINDOW_HIDDEN:
            if ( nativeEvent.window.windowID == window.id ) window.visible = false;
            break;

        case SDL_EVENT_WINDOW_MOVED:
            if ( nativeEvent.window.windowID == window.id ) {
                window.x = static_cast<common::i32>( nativeEvent.window.data1 );
                window.y = static_cast<common::i32>( nativeEvent.window.data2 );
            }
            break;

        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            Window_QueueWindowEvent( sys_event_type_t::WINDOW_FOCUS_GAINED, nativeEvent.window );
            if ( nativeEvent.window.windowID == window.id ) window.focused = true;
            break;

        case SDL_EVENT_WINDOW_FOCUS_LOST:
            Window_QueueWindowEvent( sys_event_type_t::WINDOW_FOCUS_LOST, nativeEvent.window );
            if ( nativeEvent.window.windowID == window.id ) window.focused = false;
            break;

        case SDL_EVENT_WINDOW_MINIMIZED:
            Window_QueueWindowEvent( sys_event_type_t::WINDOW_MINIMIZED, nativeEvent.window );
            if ( nativeEvent.window.windowID == window.id ) {
                window.minimized = true;
                window.maximized = false;
            }
            break;

        case SDL_EVENT_WINDOW_MAXIMIZED:
            if ( nativeEvent.window.windowID == window.id ) {
                window.minimized = false;
                window.maximized = true;
            }
            break;

        case SDL_EVENT_WINDOW_RESTORED:
            Window_QueueWindowEvent( sys_event_type_t::WINDOW_RESTORED, nativeEvent.window );
            if ( nativeEvent.window.windowID == window.id ) {
                window.minimized = false;
                window.maximized = false;
            }
            break;

        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            if ( nativeEvent.window.windowID == window.id ) window.mouseFocused = true;
            break;

        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            if ( nativeEvent.window.windowID == window.id ) window.mouseFocused = false;
            break;

        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            if ( nativeEvent.window.windowID == window.id ) {
                (void)Window_RefreshState( Window_GetNative( window ), window );
            }
            break;

        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            if ( nativeEvent.window.windowID == window.id ) {
                (void)Window_RefreshState( Window_GetNative( window ), window );
            }
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            const sys_key_t key = Window_MapScancode( nativeEvent.key.scancode );
            if ( key == sys_key_t::NONE ) {
                break;
            }

            sys_event_t event{};
            event.type = sys_event_type_t::KEY;
            event.timestampNanoseconds = Sys_TimeNowNanoseconds();
            event.payload.key.windowId = static_cast<sys_window_id_t>( nativeEvent.key.windowID );
            event.payload.key.key = key;
            event.payload.key.action = nativeEvent.key.down
                ? ( nativeEvent.key.repeat ? sys_input_action_t::REPEATED : sys_input_action_t::PRESSED )
                : sys_input_action_t::RELEASED;
            event.payload.key.modifiers = Window_MapModifiers( nativeEvent.key.mod );
            Window_QueueEvent( event );
            break;
        }

        case SDL_EVENT_TEXT_INPUT:
            Window_QueueTextInput( nativeEvent.text );
            break;

        case SDL_EVENT_MOUSE_MOTION: {
            sys_event_t event{};
            event.type = sys_event_type_t::MOUSE_MOTION;
            event.timestampNanoseconds = Sys_TimeNowNanoseconds();
            event.payload.mouseMotion.windowId =
                static_cast<sys_window_id_t>( nativeEvent.motion.windowID );
            event.payload.mouseMotion.x = nativeEvent.motion.x;
            event.payload.mouseMotion.y = nativeEvent.motion.y;
            event.payload.mouseMotion.deltaX = nativeEvent.motion.xrel;
            event.payload.mouseMotion.deltaY = nativeEvent.motion.yrel;
            Window_QueueEvent( event );
            break;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const sys_mouse_button_t button = Window_MapMouseButton( nativeEvent.button.button );
            if ( button == sys_mouse_button_t::NONE ) {
                break;
            }

            sys_event_t event{};
            event.type = sys_event_type_t::MOUSE_BUTTON;
            event.timestampNanoseconds = Sys_TimeNowNanoseconds();
            event.payload.mouseButton.windowId =
                static_cast<sys_window_id_t>( nativeEvent.button.windowID );
            event.payload.mouseButton.x = nativeEvent.button.x;
            event.payload.mouseButton.y = nativeEvent.button.y;
            event.payload.mouseButton.button = button;
            event.payload.mouseButton.action = nativeEvent.button.down
                ? sys_input_action_t::PRESSED
                : sys_input_action_t::RELEASED;
            event.payload.mouseButton.clickCount = nativeEvent.button.clicks;
            event.payload.mouseButton.modifiers = Window_MapModifiers( SDL_GetModState() );
            Window_QueueEvent( event );
            break;
        }

        case SDL_EVENT_MOUSE_WHEEL: {
            const common::f32 direction = nativeEvent.wheel.direction == SDL_MOUSEWHEEL_FLIPPED
                ? -1.0f
                : 1.0f;
            sys_event_t event{};
            event.type = sys_event_type_t::MOUSE_WHEEL;
            event.timestampNanoseconds = Sys_TimeNowNanoseconds();
            event.payload.mouseWheel.windowId =
                static_cast<sys_window_id_t>( nativeEvent.wheel.windowID );
            event.payload.mouseWheel.x = nativeEvent.wheel.x * direction;
            event.payload.mouseWheel.y = nativeEvent.wheel.y * direction;
            event.payload.mouseWheel.mouseX = nativeEvent.wheel.mouse_x;
            event.payload.mouseWheel.mouseY = nativeEvent.wheel.mouse_y;
            event.payload.mouseWheel.modifiers = Window_MapModifiers( SDL_GetModState() );
            Window_QueueEvent( event );
            break;
        }

        default:
            break;
    }
}

} // namespace

bool Sys_WindowDescIsValid( const window_desc_t &windowDescription ) noexcept
{
    if ( windowDescription.title == nullptr || windowDescription.title[0] == '\0' ||
         windowDescription.width == 0u || windowDescription.height == 0u ||
         !Window_DimensionFitsSDL( windowDescription.width ) ||
         !Window_DimensionFitsSDL( windowDescription.height ) ||
         windowDescription.mode >= window_mode_t::COUNT ||
         windowDescription.graphicsApi >= window_graphics_api_t::COUNT ||
         ( windowDescription.flags & ~SYS_WINDOW_FLAG_MASK ) != 0u ||
         !Window_SizeLimitsAreValid( windowDescription.sizeLimits ) ) {
        return false;
    }

    const display_mode_t &exclusiveMode = windowDescription.exclusiveMode;
    if ( ( exclusiveMode.width == 0u ) != ( exclusiveMode.height == 0u ) ||
         !Window_DimensionFitsSDL( exclusiveMode.width ) ||
         !Window_DimensionFitsSDL( exclusiveMode.height ) ||
         !std::isfinite( exclusiveMode.pixelDensity ) || exclusiveMode.pixelDensity <= 0.0f ||
         !std::isfinite( exclusiveMode.refreshRateHz ) || exclusiveMode.refreshRateHz < 0.0f ) {
        return false;
    }

    if ( windowDescription.graphicsApi == window_graphics_api_t::OPENGL &&
         !GLimp_ContextDescIsValid( windowDescription.openGL ) ) {
        return false;
    }

    return true;
}

sys_error_t Sys_GetDisplays(
    sys_display_id_t *displays,
    const common::u32 displayCapacity,
    display_list_result_t &resultOut ) noexcept
{
    resultOut = {};
    if ( !Sys_IsInitialized() ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( displayCapacity > 0u && displays == nullptr ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    const sys_error_t videoResult = Window_EnsureVideoSubsystem();
    if ( videoResult != sys_error_t::OK ) {
        return videoResult;
    }

    int displayCount = 0;
    SDL_DisplayID *nativeDisplays = SDL_GetDisplays( &displayCount );
    if ( nativeDisplays == nullptr || displayCount < 0 ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    resultOut.displaysRequired = static_cast<common::u32>( displayCount );
    resultOut.displaysWritten = std::min( displayCapacity, resultOut.displaysRequired );
    for ( common::u32 displayIndex = 0u; displayIndex < resultOut.displaysWritten; ++displayIndex ) {
        displays[displayIndex] = static_cast<sys_display_id_t>( nativeDisplays[displayIndex] );
    }

    SDL_free( nativeDisplays );
    return sys_error_t::OK;
}

sys_error_t Sys_GetPrimaryDisplay( sys_display_id_t &displayOut ) noexcept
{
    displayOut = SYS_INVALID_DISPLAY_ID;
    if ( !Sys_IsInitialized() ) {
        return sys_error_t::ERR_NOT_INIT;
    }

    const sys_error_t videoResult = Window_EnsureVideoSubsystem();
    if ( videoResult != sys_error_t::OK ) {
        return videoResult;
    }

    const SDL_DisplayID displayId = SDL_GetPrimaryDisplay();
    if ( displayId == 0u ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    displayOut = static_cast<sys_display_id_t>( displayId );
    return sys_error_t::OK;
}

sys_error_t Sys_GetDisplayInfo(
    const sys_display_id_t display,
    display_info_t &infoOut ) noexcept
{
    infoOut = {};
    if ( !Sys_IsInitialized() ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( display == SYS_INVALID_DISPLAY_ID ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    const sys_error_t videoResult = Window_EnsureVideoSubsystem();
    if ( videoResult != sys_error_t::OK ) {
        return videoResult;
    }

    const SDL_DisplayID nativeDisplay = static_cast<SDL_DisplayID>( display );
    const char *name = SDL_GetDisplayName( nativeDisplay );
    SDL_Rect bounds{};
    SDL_Rect usableBounds{};
    const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode( nativeDisplay );
    const SDL_DisplayMode *currentMode = SDL_GetCurrentDisplayMode( nativeDisplay );
    const common::f32 contentScale = SDL_GetDisplayContentScale( nativeDisplay );
    if ( name == nullptr ||
         !SDL_GetDisplayBounds( nativeDisplay, &bounds ) ||
         !SDL_GetDisplayUsableBounds( nativeDisplay, &usableBounds ) ||
         desktopMode == nullptr || currentMode == nullptr ||
         !Window_CopyRect( bounds, infoOut.bounds ) ||
         !Window_CopyRect( usableBounds, infoOut.usableBounds ) ||
         !Window_CopyDisplayMode( desktopMode, infoOut.desktopMode ) ||
         !Window_CopyDisplayMode( currentMode, infoOut.currentMode ) ) {
        infoOut = {};
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    const common::usize nameLength = std::min(
        std::strlen( name ),
        static_cast<common::usize>( SYS_MAX_DISPLAY_NAME_LENGTH - 1u ) );
    std::memcpy( infoOut.name, name, nameLength );
    infoOut.name[nameLength] = '\0';
    infoOut.id = display;
    infoOut.orientation = Window_MapOrientation(
        SDL_GetCurrentDisplayOrientation( nativeDisplay ) );
    infoOut.contentScale = std::isfinite( contentScale ) && contentScale > 0.0f
        ? contentScale : 1.0f;
    infoOut.primary = nativeDisplay == SDL_GetPrimaryDisplay();

    const SDL_PropertiesID properties = SDL_GetDisplayProperties( nativeDisplay );
    infoOut.hdrEnabled = properties != 0u && SDL_GetBooleanProperty(
        properties,
        SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN,
        false );
    return sys_error_t::OK;
}

sys_error_t Sys_GetDisplayModeCount(
    const sys_display_id_t display,
    common::u32 &modeCountOut ) noexcept
{
    modeCountOut = 0u;
    if ( !Sys_IsInitialized() ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( display == SYS_INVALID_DISPLAY_ID ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    const sys_error_t videoResult = Window_EnsureVideoSubsystem();
    if ( videoResult != sys_error_t::OK ) {
        return videoResult;
    }

    int modeCount = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(
        static_cast<SDL_DisplayID>( display ),
        &modeCount );
    if ( modes == nullptr || modeCount < 0 ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    modeCountOut = static_cast<common::u32>( modeCount );
    SDL_free( modes );
    return sys_error_t::OK;
}

sys_error_t Sys_GetDisplayMode(
    const sys_display_id_t display,
    const common::u32 modeIndex,
    display_mode_t &modeOut ) noexcept
{
    modeOut = {};
    if ( !Sys_IsInitialized() ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( display == SYS_INVALID_DISPLAY_ID ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    const sys_error_t videoResult = Window_EnsureVideoSubsystem();
    if ( videoResult != sys_error_t::OK ) {
        return videoResult;
    }

    int modeCount = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(
        static_cast<SDL_DisplayID>( display ),
        &modeCount );
    if ( modes == nullptr || modeCount < 0 ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    const bool validIndex = modeIndex < static_cast<common::u32>( modeCount );
    const bool copied = validIndex && Window_CopyDisplayMode( modes[modeIndex], modeOut );
    SDL_free( modes );
    return copied ? sys_error_t::OK : sys_error_t::ERR_INVALID_ARGUMENT;
}

bool Sys_PlatformHasActiveWindow() noexcept
{
    return activeWindow != nullptr;
}

void Sys_WindowSubsystemShutdown() noexcept
{
    if ( activeWindow == nullptr && ownsVideoSubsystem ) {
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
        ownsVideoSubsystem = false;
    }
}

/*
================
Sys_CreateWindow

Creates one SDL window and records logical and physical dimensions separately.
The graphics flag reserves only the native presentation capability; the renderer
still owns graphics-device and context creation.
================
*/
sys_error_t Sys_CreateWindow(
    const window_desc_t &windowDescription,
    window_t &windowOut ) noexcept
{
    if ( !Sys_IsInitialized() ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( activeWindow != nullptr ) {
        return sys_error_t::ERR_RESOURCE_BUSY;
    }
    if ( !Sys_WindowDescIsValid( windowDescription ) ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( windowOut.nativeWindow != nullptr || windowOut.valid ) {
        return sys_error_t::ERR_IS_INIT;
    }

    const sys_error_t videoResult = Window_EnsureVideoSubsystem();
    if ( videoResult != sys_error_t::OK ) {
        return videoResult;
    }

    window_mode_t requestedMode = windowDescription.mode;
    if ( windowDescription.fullscreen && requestedMode == window_mode_t::WINDOWED ) {
        requestedMode = window_mode_t::BORDERLESS_FULLSCREEN;
    }

    SDL_WindowFlags flags = 0u;
    if ( ( windowDescription.flags & SYS_WINDOW_RESIZABLE ) != 0u ) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if ( ( windowDescription.flags & SYS_WINDOW_HIGH_PIXEL_DENSITY ) != 0u ) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    if ( ( windowDescription.flags & SYS_WINDOW_BORDERLESS ) != 0u ) {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    if ( ( windowDescription.flags & SYS_WINDOW_ALWAYS_ON_TOP ) != 0u ) {
        flags |= SDL_WINDOW_ALWAYS_ON_TOP;
    }

    // Fullscreen setup is performed after positioning and mode selection. Keep
    // the native window hidden until that sequence is complete to avoid a
    // visible windowed frame during startup.
    const bool mustConfigureFullscreen = requestedMode != window_mode_t::WINDOWED;
    if ( ( windowDescription.flags & SYS_WINDOW_HIDDEN ) != 0u || mustConfigureFullscreen ) {
        flags |= SDL_WINDOW_HIDDEN;
    }

    switch ( windowDescription.graphicsApi ) {
        case window_graphics_api_t::NONE:
            break;
        case window_graphics_api_t::OPENGL:
            flags |= SDL_WINDOW_OPENGL;
            break;
        case window_graphics_api_t::VULKAN:
            flags |= SDL_WINDOW_VULKAN;
            break;
        default:
            return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    if ( windowDescription.graphicsApi == window_graphics_api_t::OPENGL ) {
        const sys_error_t attributeResult =
            GLimp_ApplyWindowAttributes( windowDescription.openGL );
        if ( attributeResult != sys_error_t::OK ) {
            return attributeResult;
        }
    }

    SDL_Window *nativeWindow = SDL_CreateWindow(
        windowDescription.title,
        static_cast<int>( windowDescription.width ),
        static_cast<int>( windowDescription.height ),
        flags );
    if ( nativeWindow == nullptr ) {
        Sys_DebugPrintf( "Sys_CreateWindow: SDL window creation failed: %s.\n", SDL_GetError() );
        if ( windowDescription.graphicsApi == window_graphics_api_t::OPENGL ) {
            GLimp_ResetWindowAttributes();
        }
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    const SDL_WindowID windowId = SDL_GetWindowID( nativeWindow );
    if ( windowId == 0u ) {
        Sys_DebugPrintf( "Sys_CreateWindow: SDL did not assign a window identifier: %s.\n", SDL_GetError() );
        SDL_DestroyWindow( nativeWindow );
        if ( windowDescription.graphicsApi == window_graphics_api_t::OPENGL ) {
            GLimp_ResetWindowAttributes();
        }
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    sys_error_t setupResult = Window_ApplySizeLimits(
        nativeWindow,
        windowDescription.sizeLimits );
    if ( setupResult == sys_error_t::OK ) {
        setupResult = Window_CenterOnDisplay(
            nativeWindow,
            windowDescription.displayId,
            windowDescription.width,
            windowDescription.height );
    }

    display_mode_t requestedExclusiveMode = windowDescription.exclusiveMode;
    if ( requestedExclusiveMode.width == 0u ) {
        requestedExclusiveMode.width = windowDescription.width;
        requestedExclusiveMode.height = windowDescription.height;
    }
    if ( requestedExclusiveMode.displayId == SYS_INVALID_DISPLAY_ID ) {
        requestedExclusiveMode.displayId = windowDescription.displayId;
        if ( requestedExclusiveMode.displayId == SYS_INVALID_DISPLAY_ID ) {
            requestedExclusiveMode.displayId = static_cast<sys_display_id_t>(
                SDL_GetPrimaryDisplay() );
        }
    }

    if ( setupResult == sys_error_t::OK && requestedMode == window_mode_t::BORDERLESS_FULLSCREEN ) {
        setupResult = SDL_SetWindowFullscreenMode( nativeWindow, nullptr ) &&
            SDL_SetWindowFullscreen( nativeWindow, true )
            ? sys_error_t::OK
            : sys_error_t::ERR_INTERNAL_ERROR;
    } else if ( setupResult == sys_error_t::OK &&
                requestedMode == window_mode_t::EXCLUSIVE_FULLSCREEN ) {
        SDL_DisplayMode nativeMode{};
        const bool modeFound = requestedExclusiveMode.displayId != SYS_INVALID_DISPLAY_ID &&
            SDL_GetClosestFullscreenDisplayMode(
                static_cast<SDL_DisplayID>( requestedExclusiveMode.displayId ),
                static_cast<int>( requestedExclusiveMode.width ),
                static_cast<int>( requestedExclusiveMode.height ),
                requestedExclusiveMode.refreshRateHz,
                true,
                &nativeMode );
        setupResult = modeFound &&
            SDL_SetWindowFullscreenMode( nativeWindow, &nativeMode ) &&
            SDL_SetWindowFullscreen( nativeWindow, true )
            ? sys_error_t::OK
            : sys_error_t::ERR_GRAPHICS_CONFIG_UNSUPPORTED;
    }

    if ( setupResult == sys_error_t::OK && mustConfigureFullscreen ) {
        setupResult = SDL_SyncWindow( nativeWindow )
            ? sys_error_t::OK
            : sys_error_t::ERR_INTERNAL_ERROR;
    }
    if ( setupResult == sys_error_t::OK &&
         ( windowDescription.flags & SYS_WINDOW_HIDDEN ) == 0u &&
         !SDL_ShowWindow( nativeWindow ) ) {
        setupResult = sys_error_t::ERR_INTERNAL_ERROR;
    }

    if ( setupResult != sys_error_t::OK ) {
        Sys_DebugPrintf( "Sys_CreateWindow: native window setup failed: %s.\n", SDL_GetError() );
        SDL_DestroyWindow( nativeWindow );
        if ( windowDescription.graphicsApi == window_graphics_api_t::OPENGL ) {
            GLimp_ResetWindowAttributes();
        }
        return setupResult;
    }

    window_t createdWindow{};
    createdWindow.nativeWindow = nativeWindow;
    createdWindow.id = static_cast<sys_window_id_t>( windowId );
    createdWindow.graphicsApi = windowDescription.graphicsApi;
    createdWindow.valid = true;
    setupResult = Window_RefreshState( nativeWindow, createdWindow );
    if ( setupResult != sys_error_t::OK ||
         createdWindow.logicalWidth == 0u || createdWindow.logicalHeight == 0u ||
         createdWindow.width == 0u || createdWindow.height == 0u ) {
        SDL_DestroyWindow( nativeWindow );
        if ( windowDescription.graphicsApi == window_graphics_api_t::OPENGL ) {
            GLimp_ResetWindowAttributes();
        }
        return setupResult == sys_error_t::OK
            ? sys_error_t::ERR_INTERNAL_ERROR
            : setupResult;
    }

    windowOut = createdWindow;
    activeWindow = nativeWindow;

    return sys_error_t::OK;
}

sys_error_t Sys_DestroyWindow( window_t &window ) noexcept
{
    if ( !Sys_IsInitialized() ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( window.nativeWindow == nullptr ) {
        window = {};
        return sys_error_t::ERR_NOT_INIT;
    }

    SDL_Window *nativeWindow = Window_GetNative( window );
    if ( nativeWindow != activeWindow ) {
        // A copied or stale wrapper must not destroy another owner's SDL object.
        window = {};
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( GLimp_WindowOwnsActiveContext( window.id ) ) {
        // Renderer owns context teardown. Destroying the window first would leave
        // its context and all loaded OpenGL entry points dangling.
        return sys_error_t::ERR_RESOURCE_BUSY;
    }
    if ( window.textInputEnabled ) {
        (void)SDL_StopTextInput( nativeWindow );
    }
    if ( window.relativeMouseEnabled ) {
        (void)SDL_SetWindowRelativeMouseMode( nativeWindow, false );
    }

    activeWindow = nullptr;
    SDL_DestroyWindow( nativeWindow );
    if ( window.graphicsApi == window_graphics_api_t::OPENGL ) {
        GLimp_ResetWindowAttributes();
    }
    window = {};
    return sys_error_t::OK;
}

void Sys_PollWindowEvents( window_t &window ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return;
    }

    SDL_Event nativeEvent{};
    while ( SDL_PollEvent( &nativeEvent ) ) {
        Window_ProcessEvent( nativeEvent, window );
    }
}

bool Sys_WindowIsValid( const window_t &window ) noexcept
{
    return Window_IsUsable( window );
}

bool Sys_WindowShouldClose( const window_t &window ) noexcept
{
    return window.shouldClose;
}

sys_error_t Sys_RefreshWindowState( window_t &window ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return sys_error_t::ERR_NOT_INIT;
    }

    return Window_RefreshState( Window_GetNative( window ), window );
}

sys_error_t Sys_SetWindowTitle( window_t &window, const char *title ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( title == nullptr || title[0] == '\0' ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }
    return SDL_SetWindowTitle( Window_GetNative( window ), title )
        ? sys_error_t::OK
        : sys_error_t::ERR_INTERNAL_ERROR;
}

sys_error_t Sys_SetWindowFullscreen( window_t &window, const bool fullscreen ) noexcept
{
    return Sys_SetWindowMode(
        window,
        fullscreen ? window_mode_t::BORDERLESS_FULLSCREEN : window_mode_t::WINDOWED );
}

sys_error_t Sys_SetWindowMode(
    window_t &window,
    const window_mode_t mode,
    const display_mode_t *exclusiveMode ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( mode >= window_mode_t::COUNT ||
         ( mode == window_mode_t::EXCLUSIVE_FULLSCREEN && exclusiveMode == nullptr ) ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    SDL_Window *nativeWindow = Window_GetNative( window );
    const sys_error_t modeResult = Window_SetMode(
        nativeWindow,
        mode,
        exclusiveMode );
    if ( modeResult != sys_error_t::OK ) {
        return modeResult;
    }
    if ( !SDL_SyncWindow( nativeWindow ) ) {
        Sys_DebugPrintf( "Sys_SetWindowMode: native mode change failed: %s.\n", SDL_GetError() );
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    return Window_RefreshState( nativeWindow, window );
}

sys_error_t Sys_SetWindowPosition(
    window_t &window,
    const common::i32 x,
    const common::i32 y ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( !SDL_SetWindowPosition(
            Window_GetNative( window ),
            static_cast<int>( x ),
            static_cast<int>( y ) ) ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    window.x = x;
    window.y = y;
    return sys_error_t::OK;
}

sys_error_t Sys_SetWindowSize(
    window_t &window,
    const common::u32 width,
    const common::u32 height ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( width == 0u || height == 0u ||
         !Window_DimensionFitsSDL( width ) || !Window_DimensionFitsSDL( height ) ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( !SDL_SetWindowSize(
            Window_GetNative( window ),
            static_cast<int>( width ),
            static_cast<int>( height ) ) ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    window.logicalWidth = width;
    window.logicalHeight = height;
    return sys_error_t::OK;
}

sys_error_t Sys_SetWindowSizeLimits(
    window_t &window,
    const window_size_limits_t &limits ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return sys_error_t::ERR_NOT_INIT;
    }

    return Window_ApplySizeLimits( Window_GetNative( window ), limits );
}

sys_error_t Sys_SetWindowResizable( window_t &window, const bool resizable ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( !SDL_SetWindowResizable( Window_GetNative( window ), resizable ) ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    if ( resizable ) window.flags |= SYS_WINDOW_RESIZABLE;
    else window.flags &= ~SYS_WINDOW_RESIZABLE;
    return sys_error_t::OK;
}

sys_error_t Sys_SetWindowBordered( window_t &window, const bool bordered ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( !SDL_SetWindowBordered( Window_GetNative( window ), bordered ) ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    if ( bordered ) window.flags &= ~SYS_WINDOW_BORDERLESS;
    else window.flags |= SYS_WINDOW_BORDERLESS;
    return sys_error_t::OK;
}

sys_error_t Sys_SetWindowAlwaysOnTop( window_t &window, const bool alwaysOnTop ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( !SDL_SetWindowAlwaysOnTop( Window_GetNative( window ), alwaysOnTop ) ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    if ( alwaysOnTop ) window.flags |= SYS_WINDOW_ALWAYS_ON_TOP;
    else window.flags &= ~SYS_WINDOW_ALWAYS_ON_TOP;
    return sys_error_t::OK;
}

sys_error_t Sys_ShowWindow( window_t &window ) noexcept
{
    if ( !Window_IsUsable( window ) ) return sys_error_t::ERR_NOT_INIT;
    if ( !SDL_ShowWindow( Window_GetNative( window ) ) ) return sys_error_t::ERR_INTERNAL_ERROR;
    window.visible = true;
    window.flags &= ~SYS_WINDOW_HIDDEN;
    return sys_error_t::OK;
}

sys_error_t Sys_HideWindow( window_t &window ) noexcept
{
    if ( !Window_IsUsable( window ) ) return sys_error_t::ERR_NOT_INIT;
    if ( !SDL_HideWindow( Window_GetNative( window ) ) ) return sys_error_t::ERR_INTERNAL_ERROR;
    window.visible = false;
    window.flags |= SYS_WINDOW_HIDDEN;
    return sys_error_t::OK;
}

sys_error_t Sys_RaiseWindow( window_t &window ) noexcept
{
    if ( !Window_IsUsable( window ) ) return sys_error_t::ERR_NOT_INIT;
    return SDL_RaiseWindow( Window_GetNative( window ) )
        ? sys_error_t::OK : sys_error_t::ERR_INTERNAL_ERROR;
}

sys_error_t Sys_MinimizeWindow( window_t &window ) noexcept
{
    if ( !Window_IsUsable( window ) ) return sys_error_t::ERR_NOT_INIT;
    if ( !SDL_MinimizeWindow( Window_GetNative( window ) ) ) return sys_error_t::ERR_INTERNAL_ERROR;
    window.minimized = true;
    window.maximized = false;
    return sys_error_t::OK;
}

sys_error_t Sys_MaximizeWindow( window_t &window ) noexcept
{
    if ( !Window_IsUsable( window ) ) return sys_error_t::ERR_NOT_INIT;
    if ( !SDL_MaximizeWindow( Window_GetNative( window ) ) ) return sys_error_t::ERR_INTERNAL_ERROR;
    window.minimized = false;
    window.maximized = true;
    return sys_error_t::OK;
}

sys_error_t Sys_RestoreWindow( window_t &window ) noexcept
{
    if ( !Window_IsUsable( window ) ) return sys_error_t::ERR_NOT_INIT;
    if ( !SDL_RestoreWindow( Window_GetNative( window ) ) ) return sys_error_t::ERR_INTERNAL_ERROR;
    window.minimized = false;
    window.maximized = false;
    return sys_error_t::OK;
}

sys_error_t Sys_SyncWindow( window_t &window ) noexcept
{
    if ( !Window_IsUsable( window ) ) return sys_error_t::ERR_NOT_INIT;
    if ( !SDL_SyncWindow( Window_GetNative( window ) ) ) return sys_error_t::ERR_INTERNAL_ERROR;
    return Sys_RefreshWindowState( window );
}

sys_error_t Sys_SetWindowMouseGrab( window_t &window, const bool grabbed ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( !SDL_SetWindowMouseGrab( Window_GetNative( window ), grabbed ) ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    window.mouseGrabbed = grabbed;
    return sys_error_t::OK;
}

sys_error_t Sys_SetRelativeMouseMode( window_t &window, const bool enabled ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( !SDL_SetWindowRelativeMouseMode( Window_GetNative( window ), enabled ) ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }
    window.relativeMouseEnabled = enabled;
    return sys_error_t::OK;
}

sys_error_t Sys_SetTextInputEnabled( window_t &window, const bool enabled ) noexcept
{
    if ( !Window_IsUsable( window ) ) {
        return sys_error_t::ERR_NOT_INIT;
    }

    SDL_Window *nativeWindow = Window_GetNative( window );
    const bool succeeded = enabled
        ? SDL_StartTextInput( nativeWindow )
        : SDL_StopTextInput( nativeWindow );
    if ( !succeeded ) {
        return sys_error_t::ERR_INTERNAL_ERROR;
    }
    window.textInputEnabled = enabled;
    return sys_error_t::OK;
}

sys_error_t Sys_SetCursorVisible( const bool visible ) noexcept
{
    if ( !Sys_IsInitialized() || activeWindow == nullptr ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    const bool succeeded = visible ? SDL_ShowCursor() : SDL_HideCursor();
    return succeeded ? sys_error_t::OK : sys_error_t::ERR_INTERNAL_ERROR;
}

} // namespace cypher::engine::sys
