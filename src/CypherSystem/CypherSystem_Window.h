//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_Window.h
//  Purpose: Declares the CypherSystem System Window module.
//  Details: This file owns platform-facing system, window, and graphics context
//           boundaries. Keep OS-specific code isolated enough that higher-level
//           runtime code remains portable.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_SYSTEM_WINDOW_H
#define CYPHER_ENGINE_SYSTEM_WINDOW_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSystem_OpenGL.h"
#include "CypherSystem_Public.h"

namespace cypher::engine::sys
{

constexpr common::u32 SYS_MAX_DISPLAY_NAME_LENGTH = 128u; // Includes the trailing null byte.
constexpr common::u32 SYS_MAX_PLATFORM_BACKEND_NAME_LENGTH = 32u; // Includes the trailing null byte.
constexpr common::u32 SYS_MAX_PLATFORM_BACKEND_REVISION_LENGTH = 128u; // Includes the trailing null byte.
constexpr common::u32 SYS_MAX_VIDEO_DRIVER_NAME_LENGTH = 64u; // Includes the trailing null byte.

using sys_display_id_t = common::u32;
constexpr sys_display_id_t SYS_INVALID_DISPLAY_ID = 0u; // SDL and Cypher both reserve zero as invalid.

/*
================
System Window Types

The public engine-facing wrapper around native SDL window state. System V1 owns
one runtime window; separate Qt tools own their own GUI windows outside this API.
Creation, mutation, destruction, and event pumping are main-thread operations.
window_t is an owning record and must not be copied while its native window lives.
================
*/
enum class window_graphics_api_t : common::u8 {
    NONE = 0u, // No graphics context flag; suitable for software output.
    OPENGL,    // Window may own an SDL OpenGL context.
    VULKAN,    // Window may create a Vulkan presentation surface.
    COUNT      // Number of graphics-window policies; never selected.
};

enum class window_mode_t : common::u8 {
    WINDOWED = 0u,        // Decorated or borderless desktop window.
    BORDERLESS_FULLSCREEN,// Desktop-sized fullscreen without changing display mode.
    EXCLUSIVE_FULLSCREEN, // Fullscreen using a concrete display resolution and refresh rate.
    COUNT                 // Number of window modes; never selected.
};

enum class display_orientation_t : common::u8 {
    UNKNOWN = 0u,       // Platform did not report a usable orientation.
    LANDSCAPE,          // Normal landscape orientation.
    LANDSCAPE_FLIPPED,  // Landscape rotated by 180 degrees.
    PORTRAIT,           // Normal portrait orientation.
    PORTRAIT_FLIPPED,   // Portrait rotated by 180 degrees.
    COUNT
};

using window_flags_t = ::cypher::common::flags32_t;

enum window_flag_t : window_flags_t {
    SYS_WINDOW_NONE               = 0u,
    SYS_WINDOW_RESIZABLE          = CYPHER_BIT32( 0 ), // User may resize the window in windowed mode.
    SYS_WINDOW_HIGH_PIXEL_DENSITY = CYPHER_BIT32( 1 ), // Request a Retina/HiDPI drawable when available.
    SYS_WINDOW_HIDDEN             = CYPHER_BIT32( 2 ), // Create the window hidden for controlled startup.
    SYS_WINDOW_BORDERLESS         = CYPHER_BIT32( 3 ), // Remove decorations while remaining windowed.
    SYS_WINDOW_ALWAYS_ON_TOP      = CYPHER_BIT32( 4 )  // Ask the desktop compositor to keep the window above peers.
};

constexpr window_flags_t SYS_WINDOW_FLAG_MASK =
    SYS_WINDOW_RESIZABLE |
    SYS_WINDOW_HIGH_PIXEL_DENSITY |
    SYS_WINDOW_HIDDEN |
    SYS_WINDOW_BORDERLESS |
    SYS_WINDOW_ALWAYS_ON_TOP;

struct window_rect_t {
    common::i32 x{ 0 };           // Desktop-space left edge in logical coordinates.
    common::i32 y{ 0 };           // Desktop-space top edge in logical coordinates.
    common::u32 width{ 0u };      // Logical width; zero marks an unavailable rectangle.
    common::u32 height{ 0u };     // Logical height; zero marks an unavailable rectangle.
};

struct display_mode_t {
    sys_display_id_t displayId{ SYS_INVALID_DISPLAY_ID }; // Display that owns this mode.
    common::u32 width{ 0u };                              // Logical mode width.
    common::u32 height{ 0u };                             // Logical mode height.
    common::f32 pixelDensity{ 1.0f };                     // Physical-pixel to logical-pixel ratio.
    common::f32 refreshRateHz{ 0.0f };                    // Approximate refresh rate; zero means unspecified.
    common::i32 refreshRateNumerator{ 0 };                // Exact refresh numerator when reported.
    common::i32 refreshRateDenominator{ 0 };              // Exact refresh denominator when reported.
};

struct display_info_t {
    sys_display_id_t id{ SYS_INVALID_DISPLAY_ID };        // Stable identifier for the current display connection.
    char name[SYS_MAX_DISPLAY_NAME_LENGTH]{};             // Copied UTF-8 display name; never borrowed from SDL.
    window_rect_t bounds{};                               // Complete desktop-space display rectangle.
    window_rect_t usableBounds{};                         // Bounds excluding menu bars, docks, and taskbars.
    display_mode_t desktopMode{};                         // Desktop mode before exclusive fullscreen changes it.
    display_mode_t currentMode{};                         // Mode currently scanned out by the display.
    display_orientation_t orientation{ display_orientation_t::UNKNOWN }; // Current physical orientation.
    common::f32 contentScale{ 1.0f };                     // OS-requested UI/content scale.
    bool primary{ false };                                // True when this is the desktop primary display.
    bool hdrEnabled{ false };                             // Informational HDR desktop state, not renderer support.
};

struct display_list_result_t {
    common::u32 displaysRequired{ 0u }; // Number of connected displays at query time.
    common::u32 displaysWritten{ 0u };  // Number copied into the caller's bounded array.
};

// Copied identity for the native window/event backend. SDL types and headers
// remain private to CypherSystem while Host diagnostics can verify the linked
// runtime and selected desktop video driver.
struct platform_backend_info_t {
    char name[SYS_MAX_PLATFORM_BACKEND_NAME_LENGTH]{};
    common::u32 compiledVersionMajor{ 0u };
    common::u32 compiledVersionMinor{ 0u };
    common::u32 compiledVersionPatch{ 0u };
    common::u32 runtimeVersionMajor{ 0u };
    common::u32 runtimeVersionMinor{ 0u };
    common::u32 runtimeVersionPatch{ 0u };
    char revision[SYS_MAX_PLATFORM_BACKEND_REVISION_LENGTH]{};
    char videoDriver[SYS_MAX_VIDEO_DRIVER_NAME_LENGTH]{};
};

struct window_size_limits_t {
    common::u32 minimumWidth{ 0u };  // Zero leaves the platform minimum unchanged.
    common::u32 minimumHeight{ 0u }; // Zero leaves the platform minimum unchanged.
    common::u32 maximumWidth{ 0u };  // Zero means no explicit maximum.
    common::u32 maximumHeight{ 0u }; // Zero means no explicit maximum.
};

struct window_desc_t {
    const char *title{ common::COM_GAME_INFO.name }; // Borrowed UTF-8 title used during creation.
    common::u32 width{ 1280u };                     // Requested logical client width.
    common::u32 height{ 720u };                     // Requested logical client height.
    sys_display_id_t displayId{ SYS_INVALID_DISPLAY_ID }; // Preferred display; zero selects the primary display.
    window_mode_t mode{ window_mode_t::WINDOWED };  // Windowed, borderless fullscreen, or exclusive fullscreen.
    window_flags_t flags{ SYS_WINDOW_RESIZABLE | SYS_WINDOW_HIGH_PIXEL_DENSITY }; // Creation-time desktop policy.
    window_size_limits_t sizeLimits{};              // Optional constraints applied after native creation.
    display_mode_t exclusiveMode{};                 // Requested scan-out mode for exclusive fullscreen.
    bool fullscreen{ false };                       // Compatibility request; true maps WINDOWED to borderless fullscreen.
    window_graphics_api_t graphicsApi{ window_graphics_api_t::NONE }; // Renderer explicitly selects presentation support.
    gl_context_desc_t openGL{};                     // Used only when graphicsApi is OPENGL.
};

struct window_t {
    void *nativeWindow{ nullptr };                  // Opaque SDL_Window pointer owned by this wrapper.
    sys_window_id_t id{ SYS_INVALID_WINDOW_ID };    // Stable SDL window identifier copied into events.

    common::u32 logicalWidth{ 0u };                 // Last known client width in logical coordinates.
    common::u32 logicalHeight{ 0u };                // Last known client height in logical coordinates.
    common::u32 width{ 0u };                        // Last known renderable width in physical pixels.
    common::u32 height{ 0u };                       // Last known renderable height in physical pixels.

    common::i32 x{ 0 };                             // Last known desktop-space logical x position.
    common::i32 y{ 0 };                             // Last known desktop-space logical y position.
    sys_display_id_t displayId{ SYS_INVALID_DISPLAY_ID }; // Display containing the window center.
    common::f32 pixelDensity{ 1.0f };               // Physical-pixel to logical-pixel ratio.
    common::f32 displayScale{ 1.0f };               // OS-requested content scale for this window.

    window_mode_t mode{ window_mode_t::WINDOWED };  // Current fullscreen/windowed policy.
    window_flags_t flags{ SYS_WINDOW_NONE };        // Current mutable desktop-window flags.

    bool fullscreen{ false };                       // Current fullscreen policy.
    bool shouldClose{ false };                      // Set after a platform close request.
    bool focused{ false };                          // Window currently owns keyboard focus.
    bool mouseFocused{ false };                     // Pointer currently lies inside the client area.
    bool minimized{ false };                        // Window is currently minimized.
    bool maximized{ false };                        // Window is currently maximized.
    bool visible{ false };                          // Window is mapped by the desktop compositor.
    bool mouseGrabbed{ false };                     // Pointer is constrained to this window.
    bool textInputEnabled{ false };                 // SDL text/IME event production is active.
    bool relativeMouseEnabled{ false };             // Mouse reports unbounded relative motion.
    bool valid{ false };                            // Native window was created and has not been destroyed.
    window_graphics_api_t graphicsApi{ window_graphics_api_t::NONE }; // Native presentation capability.
};

/*
================
System Window API

Only one live window is accepted. Sys_Shutdown returns ERR_RESOURCE_BUSY until
that window is destroyed, which makes the required Host shutdown order explicit.
================
*/
CYPHER_NODISCARD bool Sys_WindowDescIsValid(
    const window_desc_t &windowDescription ) noexcept;

// Uses the usual required/written query pattern so callers can size storage
// without relying on a fixed maximum display count.
CYPHER_NODISCARD sys_error_t Sys_GetDisplays(
    sys_display_id_t *displays,
    common::u32 displayCapacity,
    display_list_result_t &resultOut ) noexcept;

// Returns the desktop's current primary display identifier.
CYPHER_NODISCARD sys_error_t Sys_GetPrimaryDisplay(
    sys_display_id_t &displayOut ) noexcept;

// Returns SDL compile/runtime identity and the current native video driver.
CYPHER_NODISCARD sys_error_t Sys_GetPlatformBackendInfo(
    platform_backend_info_t &infoOut ) noexcept;

// Copies volatile platform display data into an engine-owned value record.
CYPHER_NODISCARD sys_error_t Sys_GetDisplayInfo(
    sys_display_id_t display,
    display_info_t &infoOut ) noexcept;

// Reports the number of exclusive modes currently advertised by a display.
CYPHER_NODISCARD sys_error_t Sys_GetDisplayModeCount(
    sys_display_id_t display,
    common::u32 &modeCountOut ) noexcept;

// Copies one indexed exclusive mode; modeIndex must be below the queried count.
CYPHER_NODISCARD sys_error_t Sys_GetDisplayMode(
    sys_display_id_t display,
    common::u32 modeIndex,
    display_mode_t &modeOut ) noexcept;

// Creates the process-wide runtime window and transfers native ownership into
// windowOut. The description must already contain renderer-required attributes.
CYPHER_NODISCARD sys_error_t Sys_CreateWindow(
    const window_desc_t &windowDescription,
    window_t &windowOut ) noexcept;

// Releases the native window and invalidates every field in the owning record.
CYPHER_NODISCARD sys_error_t Sys_DestroyWindow( window_t &window ) noexcept;

// Translates all pending SDL events into System events and refreshes window state.
void Sys_PollWindowEvents( window_t &window ) noexcept;

// Rejects stale copies as well as null or already-destroyed window records.
CYPHER_NODISCARD bool Sys_WindowIsValid( const window_t &window ) noexcept;

// Returns the close request latched by the most recent event pump.
CYPHER_NODISCARD bool Sys_WindowShouldClose( const window_t &window ) noexcept;

// Requeries mutable desktop and drawable properties from the native window.
CYPHER_NODISCARD sys_error_t Sys_RefreshWindowState(
    window_t &window ) noexcept;

// Replaces the desktop-visible UTF-8 title; the platform copies the text.
CYPHER_NODISCARD sys_error_t Sys_SetWindowTitle(
    window_t &window,
    const char *title ) noexcept;

// Compatibility helper mapping true to borderless fullscreen and false to windowed.
CYPHER_NODISCARD sys_error_t Sys_SetWindowFullscreen(
    window_t &window,
    bool fullscreen ) noexcept;

// Changes windowed/fullscreen policy; exclusive mode is required only for an
// exclusive transition and is copied before this call returns.
CYPHER_NODISCARD sys_error_t Sys_SetWindowMode(
    window_t &window,
    window_mode_t mode,
    const display_mode_t *exclusiveMode = nullptr ) noexcept;

// Moves the outer window origin in desktop logical coordinates.
CYPHER_NODISCARD sys_error_t Sys_SetWindowPosition(
    window_t &window,
    common::i32 x,
    common::i32 y ) noexcept;

// Changes the logical client size; drawable size is refreshed separately.
CYPHER_NODISCARD sys_error_t Sys_SetWindowSize(
    window_t &window,
    common::u32 width,
    common::u32 height ) noexcept;

// Applies caller-owned minimum/maximum constraints to future desktop resizes.
CYPHER_NODISCARD sys_error_t Sys_SetWindowSizeLimits(
    window_t &window,
    const window_size_limits_t &limits ) noexcept;

// Enables or disables interactive desktop resizing.
CYPHER_NODISCARD sys_error_t Sys_SetWindowResizable(
    window_t &window,
    bool resizable ) noexcept;

// Adds or removes platform decorations without changing fullscreen policy.
CYPHER_NODISCARD sys_error_t Sys_SetWindowBordered(
    window_t &window,
    bool bordered ) noexcept;

// Changes compositor stacking policy when the host desktop supports it.
CYPHER_NODISCARD sys_error_t Sys_SetWindowAlwaysOnTop(
    window_t &window,
    bool alwaysOnTop ) noexcept;

// Maps a hidden window without implicitly changing focus or size.
CYPHER_NODISCARD sys_error_t Sys_ShowWindow( window_t &window ) noexcept;

// Unmaps the window while preserving its native object and graphics context.
CYPHER_NODISCARD sys_error_t Sys_HideWindow( window_t &window ) noexcept;

// Requests desktop focus and front ordering; the window manager may refuse it.
CYPHER_NODISCARD sys_error_t Sys_RaiseWindow( window_t &window ) noexcept;

// Requests the desktop's minimized state.
CYPHER_NODISCARD sys_error_t Sys_MinimizeWindow( window_t &window ) noexcept;

// Requests the desktop's maximized state.
CYPHER_NODISCARD sys_error_t Sys_MaximizeWindow( window_t &window ) noexcept;

// Restores a minimized or maximized window to its normal state.
CYPHER_NODISCARD sys_error_t Sys_RestoreWindow( window_t &window ) noexcept;

// Waits until asynchronous desktop window operations have been applied.
CYPHER_NODISCARD sys_error_t Sys_SyncWindow( window_t &window ) noexcept;

// Constrains pointer motion to the native client area without making it relative.
CYPHER_NODISCARD sys_error_t Sys_SetWindowMouseGrab(
    window_t &window,
    bool grabbed ) noexcept;

// Enables unbounded relative mouse deltas for first-person camera input.
CYPHER_NODISCARD sys_error_t Sys_SetRelativeMouseMode(
    window_t &window,
    bool enabled ) noexcept;

// Starts or stops UTF-8 text and IME composition events for this window.
CYPHER_NODISCARD sys_error_t Sys_SetTextInputEnabled(
    window_t &window,
    bool enabled ) noexcept;

// Changes global pointer visibility; cursor image/shape policy comes later.
CYPHER_NODISCARD sys_error_t Sys_SetCursorVisible( bool visible ) noexcept;

}       // namespace cypher::engine::sys

#endif // CYPHER_ENGINE_SYSTEM_WINDOW_H
