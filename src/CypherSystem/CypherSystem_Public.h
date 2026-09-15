//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_Public.h
//  Purpose: Declares the engine-facing operating-system service contract.
//  Details: Runtime code includes this header instead of platform-native headers.
//           Platform implementations are selected by CMake and remain private to
//           CypherSystem.
//
//  History:
//  - Created by Karlo Siric on 2026-08-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_SYSTEM_PUBLIC_H
#define CYPHER_ENGINE_SYSTEM_PUBLIC_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSystem_Error.h"
#include "CypherCommon_Annotations.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_CPUMonitoring.h"
#include "CypherCommon_Defines.h" // CYPHER_BIT32 used by System input flags.
#include "CypherCommon_DynamicLibrary.h"
#include "CypherCommon_Environment.h"
#include "CypherCommon_Process.h"
#include "CypherCommon_SystemInfo.h"

#include <cstdarg> // std::va_list used by preformatted diagnostic output.
#include <ctime>   // std::time_t and std::tm used by the calendar-time boundary.
#include <type_traits> // Compile-time guarantees for allocation-free event records.

namespace cypher::engine::sys
{

constexpr common::u32 SYS_MAX_PATH_LENGTH = 4096u; // UTF-8 host path storage, including the terminator.
constexpr common::u32 SYS_MAX_NAME_LENGTH = 256u;  // Storage for copied application and organization names.
constexpr common::u32 SYS_EVENT_QUEUE_CAPACITY = 256u; // Maximum pending OS events; storage never grows at runtime.
constexpr common::u32 SYS_TEXT_INPUT_CAPACITY = 32u;   // UTF-8 bytes carried by one text event, including its terminator.

using sys_window_id_t = common::u32;
constexpr sys_window_id_t SYS_INVALID_WINDOW_ID = 0u; // Native window identifiers are normalized to nonzero values.

/*
================

System Startup Data

The process owns argv. CypherSystem reads all startup strings during Sys_Init and
does not retain their pointers afterward.
Host must call lifecycle operations from the main thread. Sys_RequestQuit and
the bootstrap output functions are the explicitly thread-safe exceptions.

================
*/
struct init_info_t {
    int argc{ 0 };                            // Number of process arguments available through argv.
    const char *const *argv{ nullptr };       // Borrowed process argument vector, read only during Sys_Init.
    const char *appName{ nullptr };           // Required application name used for writable paths.
    const char *organizationName{ nullptr };  // Required organization name reserved for host integration.
};

struct paths_t {
    char executablePath[SYS_MAX_PATH_LENGTH]{}; // Absolute path to the running executable.
    char executableDir[SYS_MAX_PATH_LENGTH]{};  // Absolute directory containing the executable.
    char workingDir[SYS_MAX_PATH_LENGTH]{};     // Process working directory captured during startup.
    char basePath[SYS_MAX_PATH_LENGTH]{};       // Engine/content root, optionally overridden by -basedir.
    char userPath[SYS_MAX_PATH_LENGTH]{};       // Writable per-user root, optionally overridden by -userpath.
};

/*
================
System Service Types

CypherSystem is the runtime-facing facade. Tier0 remains the sole owner of the
native implementations, while these aliases keep engine code out of Common's
platform service headers and prevent a second set of OS wrappers from forming.
================
*/
using system_info_t = ::cypher::common::cy_system_info_t;
using system_memory_status_t = ::cypher::common::cy_system_memory_status_t;
using system_disk_status_t = ::cypher::common::cy_system_disk_status_t;
using system_power_state_t = ::cypher::common::cy_system_power_state_t;
using environment_get_result_t = ::cypher::common::cy_environment_get_result_t;
using process_id_t = ::cypher::common::process_id_t;
using dynamic_library_t = ::cypher::common::dynamic_library_t;
using dynamic_library_flags_t = ::cypher::common::flags32_t;
using cpu_monitor_t = ::cypher::common::cy_cpu_monitor_t;
using cpu_monitor_sample_t = ::cypher::common::cy_cpu_monitor_sample_t;

constexpr dynamic_library_flags_t SYS_LIBRARY_NONE =
    ::cypher::common::CY_DYNAMIC_LIBRARY_NONE;
constexpr dynamic_library_flags_t SYS_LIBRARY_RESOLVE_LAZY =
    ::cypher::common::CY_DYNAMIC_LIBRARY_RESOLVE_LAZY;
constexpr dynamic_library_flags_t SYS_LIBRARY_GLOBAL_SYMBOLS =
    ::cypher::common::CY_DYNAMIC_LIBRARY_GLOBAL_SYMBOLS;

/*
===============================================================================

    System Events

===============================================================================
*/

enum class sys_event_type_t : common::u16 {
    NONE = 0u,                  // Empty or uninitialized event.

    QUIT_REQUESTED,             // Process-wide orderly shutdown requested.
    WINDOW_CLOSE_REQUESTED,     // A particular window requested closure.
    WINDOW_RESIZED,             // Logical window dimensions changed.
    WINDOW_PIXEL_SIZE_CHANGED,  // Renderable framebuffer dimensions changed.
    WINDOW_FOCUS_GAINED,        // Window became the active input target.
    WINDOW_FOCUS_LOST,          // Window stopped receiving active input.
    WINDOW_MINIMIZED,           // Window entered its minimized state.
    WINDOW_RESTORED,            // Window returned from minimized state.

    KEY,                        // Keyboard transition event.
    TEXT_INPUT,                 // One or more UTF-8 input characters.
    MOUSE_MOTION,               // Absolute and relative mouse movement.
    MOUSE_BUTTON,               // Mouse-button transition event.
    MOUSE_WHEEL,                // Horizontal and vertical wheel movement.

    COUNT                       // Number of event types; never queued.
};

enum class sys_input_action_t : common::u8 {
    NONE = 0u, // No valid transition was recorded.
    PRESSED,   // Input changed from released to pressed.
    RELEASED,  // Input changed from pressed to released.
    REPEATED,  // The OS repeated an already-held keyboard key.
    COUNT      // Number of input actions; never stored in an event.
};

/*
================
System Key Identities

Keys describe physical controls rather than produced characters. Platform code
maps native scan codes into this stable engine vocabulary; text entry arrives
separately through TEXT_INPUT so keyboard layouts and IME input remain correct.
Numeric enum values are runtime indices and must not be serialized directly.
================
*/
enum class sys_key_t : common::u16 {
    NONE = 0u,

    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

    DIGIT_0, DIGIT_1, DIGIT_2, DIGIT_3, DIGIT_4,
    DIGIT_5, DIGIT_6, DIGIT_7, DIGIT_8, DIGIT_9,

    ENTER,
    ESCAPE,
    BACKSPACE,
    TAB,
    SPACE,
    MINUS,
    EQUALS,
    LEFT_BRACKET,
    RIGHT_BRACKET,
    BACKSLASH,
    SEMICOLON,
    APOSTROPHE,
    GRAVE,
    COMMA,
    PERIOD,
    SLASH,
    CAPS_LOCK,

    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    F13, F14, F15, F16, F17, F18, F19, F20, F21, F22, F23, F24,

    PRINT_SCREEN,
    SCROLL_LOCK,
    PAUSE,
    INSERT,
    HOME,
    PAGE_UP,
    DELETE_KEY,
    END,
    PAGE_DOWN,
    RIGHT,
    LEFT,
    DOWN,
    UP,

    NUM_LOCK,
    KEYPAD_DIVIDE,
    KEYPAD_MULTIPLY,
    KEYPAD_SUBTRACT,
    KEYPAD_ADD,
    KEYPAD_ENTER,
    KEYPAD_0,
    KEYPAD_1,
    KEYPAD_2,
    KEYPAD_3,
    KEYPAD_4,
    KEYPAD_5,
    KEYPAD_6,
    KEYPAD_7,
    KEYPAD_8,
    KEYPAD_9,
    KEYPAD_DECIMAL,
    KEYPAD_EQUALS,

    APPLICATION,
    LEFT_CONTROL,
    LEFT_SHIFT,
    LEFT_ALT,
    LEFT_SUPER,
    RIGHT_CONTROL,
    RIGHT_SHIFT,
    RIGHT_ALT,
    RIGHT_SUPER,

    COUNT // Number of key identities; valid for fixed key-state arrays.
};

enum class sys_mouse_button_t : common::u8 {
    NONE = 0u,
    LEFT,
    MIDDLE,
    RIGHT,
    X1,
    X2,
    COUNT // Number of mouse buttons; never stored as a button value.
};

using sys_key_modifiers_t = ::cypher::common::flags32_t;

enum sys_key_modifier_flag_t : sys_key_modifiers_t {
    SYS_KEYMODIFIER_NONE        = 0u,
    SYS_KEYMODIFIER_SHIFT       = CYPHER_BIT32( 0 ), // Either Shift key is active.
    SYS_KEYMODIFIER_CONTROL     = CYPHER_BIT32( 1 ), // Either Control key is active.
    SYS_KEYMODIFIER_ALT         = CYPHER_BIT32( 2 ), // Either Alt/Option key is active.
    SYS_KEYMODIFIER_SUPER       = CYPHER_BIT32( 3 ), // Windows, Command, or Super key is active.
    SYS_KEYMODIFIER_CAPS_LOCK   = CYPHER_BIT32( 4 ), // Caps Lock mode is enabled.
    SYS_KEYMODIFIER_NUM_LOCK    = CYPHER_BIT32( 5 ), // Num Lock mode is enabled.
    SYS_KEYMODIFIER_SCROLL_LOCK = CYPHER_BIT32( 6 )  // Scroll Lock mode is enabled.
};

constexpr sys_key_modifiers_t SYS_KEYMODIFIER_MASK =
    SYS_KEYMODIFIER_SHIFT |
    SYS_KEYMODIFIER_CONTROL |
    SYS_KEYMODIFIER_ALT |
    SYS_KEYMODIFIER_SUPER |
    SYS_KEYMODIFIER_CAPS_LOCK |
    SYS_KEYMODIFIER_NUM_LOCK |
    SYS_KEYMODIFIER_SCROLL_LOCK;

/*
================
System Event Payloads

Every payload is trivial fixed-size data so native event pumps can queue events
without allocating memory. The event type determines which union member is live.
================
*/
struct sys_window_event_t {
    sys_window_id_t windowId;
    common::u32 width;
    common::u32 height;
};

struct sys_key_event_t {
    sys_window_id_t windowId;
    sys_key_t key;
    sys_input_action_t action;
    common::u8 reserved;
    sys_key_modifiers_t modifiers;
};

struct sys_text_input_event_t {
    sys_window_id_t windowId;
    common::u8 byteCount;
    char utf8[SYS_TEXT_INPUT_CAPACITY];
};

struct sys_mouse_motion_event_t {
    sys_window_id_t windowId;
    common::f32 x;
    common::f32 y;
    common::f32 deltaX;
    common::f32 deltaY;
};

struct sys_mouse_button_event_t {
    sys_window_id_t windowId;
    common::f32 x;
    common::f32 y;
    sys_mouse_button_t button;
    sys_input_action_t action;
    common::u8 clickCount;
    common::u8 reserved;
    sys_key_modifiers_t modifiers;
};

struct sys_mouse_wheel_event_t {
    sys_window_id_t windowId;
    common::f32 x;
    common::f32 y;
    common::f32 mouseX;
    common::f32 mouseY;
    sys_key_modifiers_t modifiers;
};

union sys_event_payload_t {
    sys_window_event_t window;
    sys_key_event_t key;
    sys_text_input_event_t textInput;
    sys_mouse_motion_event_t mouseMotion;
    sys_mouse_button_event_t mouseButton;
    sys_mouse_wheel_event_t mouseWheel;
};

struct sys_event_t {
    sys_event_type_t type{ sys_event_type_t::NONE }; // Selects the active payload member.
    common::u64 timestampNanoseconds{ 0u };          // Monotonic time assigned by the native producer.
    sys_event_payload_t payload{};                   // Fixed storage; no ownership crosses the queue.
};

static_assert( sizeof( sys_event_t ) <= 64u, "System events must remain cheap to queue and copy." );
static_assert( std::is_trivially_copyable_v<sys_event_t>,
    "System events cross the native queue by value and must remain trivially copyable." );

/*
================
System Event Queue

The process owns one fixed-capacity FIFO queue. Native event pumping and Host
consumption occur on the main thread. Invalid event records are rejected without
changing queue state. On overflow, Sys_QueueEvent discards the oldest pending
event, queues the new event, increments the dropped-event count, and returns false
so development diagnostics can report the loss.
================
*/
CYPHER_NODISCARD bool Sys_QueueEvent( const sys_event_t &event ) noexcept;
CYPHER_NODISCARD bool Sys_PollEvent( sys_event_t &eventOut ) noexcept;
void Sys_ClearEvents() noexcept;
CYPHER_NODISCARD common::u32 Sys_EventCount() noexcept;
CYPHER_NODISCARD common::u64 Sys_DroppedEventCount() noexcept;


/*
===============================================================================

    System lifecycle

===============================================================================
*/
CYPHER_NODISCARD sys_error_t Sys_Init( const init_info_t &initInfo ) noexcept; // Initializes process-wide System state.
sys_error_t Sys_Shutdown() noexcept;                                           // Releases System state after dependent modules stop.
CYPHER_NODISCARD bool Sys_IsInitialized() noexcept;                            // Reports whether initialization completed successfully.

void Sys_RequestQuit() noexcept;                                // Records a cooperative quit request for Host to process.
CYPHER_NODISCARD bool Sys_IsQuitRequested() noexcept;            // Reports whether any thread requested an orderly shutdown.
CYPHER_NORETURN void Sys_Quit( common::i32 exitCode ) noexcept;  // Terminates after Host has completed ordered shutdown.

/*
===============================================================================

    Bootstrap and emergency output

===============================================================================
*/

void Sys_DebugPrintf(
    CY_PRINTF_FORMAT_STRING const char *format,
    ... ) noexcept CY_PRINTF_LIKE( 1, 2 ); // Writes raw bootstrap diagnostics without depending on CypherLog.

void Sys_DebugVPrintf(
    CY_PRINTF_FORMAT_STRING const char *format,
    std::va_list arguments ) noexcept CY_PRINTF_LIKE( 1, 0 ); // va_list form used by higher-level diagnostic wrappers.

CYPHER_NORETURN void Sys_Error(
    CY_PRINTF_FORMAT_STRING const char *format,
    ... ) noexcept CY_PRINTF_LIKE( 1, 2 ); // Reports an unrecoverable error and terminates without returning.

CYPHER_NORETURN void Sys_VError(
    CY_PRINTF_FORMAT_STRING const char *format,
    std::va_list arguments ) noexcept CY_PRINTF_LIKE( 1, 0 ); // va_list form of Sys_Error.

CYPHER_NODISCARD const paths_t &Sys_Paths() noexcept; // Returns cached paths; valid after successful initialization.

CYPHER_NODISCARD sys_error_t Sys_GetPaths( paths_t &pathsOut ) noexcept; // Copies cached paths after checking initialization.

CYPHER_NODISCARD const char *Sys_PathBasename( const char *path ) noexcept; // Returns a view after the final separator.

/*
===============================================================================

    Runtime machine information

The immutable snapshot is process-lifetime storage. Memory, disk, and power
queries are live values and may change from one call to the next. Graphics API,
GPU, VRAM, and driver information belongs to the active renderer backend.

===============================================================================
*/
CYPHER_NODISCARD const system_info_t *Sys_GetSystemInfo() noexcept;
CYPHER_NODISCARD system_memory_status_t Sys_QueryMemoryStatus() noexcept;
CYPHER_NODISCARD system_disk_status_t Sys_QueryDiskStatus( const char *path ) noexcept;
CYPHER_NODISCARD system_power_state_t Sys_QueryPowerState() noexcept;
CYPHER_NODISCARD common::usize Sys_FormatSystemReport(
    char *destination,
    common::usize destinationCapacity ) noexcept;
void Sys_PrintSystemReport() noexcept;
CYPHER_NODISCARD bool Sys_InitCpuMonitor( cpu_monitor_t &monitor ) noexcept;
CYPHER_NODISCARD bool Sys_ResetCpuMonitor( cpu_monitor_t &monitor ) noexcept;
CYPHER_NODISCARD bool Sys_SampleCpuMonitor(
    cpu_monitor_t &monitor,
    cpu_monitor_sample_t &sampleOut ) noexcept;

/*
===============================================================================

    Process environment and dynamic modules

These calls delegate to the hardened Tier0 implementations. Module ABI checks,
hot reload, and plugin ownership remain responsibilities of higher-level code.

===============================================================================
*/
CYPHER_NODISCARD process_id_t Sys_GetCurrentProcessId() noexcept;

CYPHER_NODISCARD environment_get_result_t Sys_GetEnvironment(
    const char *name,
    char *destination,
    common::usize destinationCapacity ) noexcept;
CYPHER_NODISCARD bool Sys_SetEnvironment( const char *name, const char *value ) noexcept;
CYPHER_NODISCARD bool Sys_UnsetEnvironment( const char *name ) noexcept;
CYPHER_NODISCARD bool Sys_HasEnvironment( const char *name ) noexcept;

CYPHER_NODISCARD bool Sys_InitLibrary( dynamic_library_t &library ) noexcept;
CYPHER_NODISCARD bool Sys_LoadLibrary(
    dynamic_library_t &library,
    const char *path,
    dynamic_library_flags_t flags = SYS_LIBRARY_NONE ) noexcept;
CYPHER_NODISCARD bool Sys_UnloadLibrary( dynamic_library_t &library ) noexcept;
CYPHER_NODISCARD bool Sys_IsLibraryLoaded( const dynamic_library_t &library ) noexcept;
CYPHER_NODISCARD void *Sys_GetLibrarySymbol(
    dynamic_library_t &library,
    const char *symbolName ) noexcept;
CYPHER_NODISCARD const char *Sys_GetLibraryError(
    const dynamic_library_t &library ) noexcept;

/*
================

System Time

================
*/
CYPHER_NODISCARD common::u64 Sys_TimeNowNanoseconds() noexcept; // Monotonic clock used by System event timestamps.
CYPHER_NODISCARD common::f64 Sys_TimeNowSeconds() noexcept;     // Monotonic process-independent clock in seconds.
void Sys_SleepMilliseconds( common::u64 milliseconds ) noexcept; // Suspends this thread for at least the requested interval.
CYPHER_NODISCARD bool Sys_LocalTime( std::time_t timeValue, std::tm &timeOut ) noexcept; // Thread-safe calendar conversion.

/*
================

System Virtual Memory

These are the engine-facing page operations. The implementation delegates to
the Tier0 platform-memory primitive so only one native VM backend exists. Every
size and address supplied here must be aligned to Sys_VirtualPageSize(); invalid
ranges fail before an operating-system call is attempted.

================
*/
CYPHER_NODISCARD common::usize Sys_VirtualPageSize() noexcept;
CYPHER_NODISCARD void *Sys_VirtualReserve( common::usize size ) noexcept;
CYPHER_NODISCARD sys_error_t Sys_VirtualCommit( void *memory, common::usize size ) noexcept;
CYPHER_NODISCARD sys_error_t Sys_VirtualDecommit( void *memory, common::usize size ) noexcept;
CYPHER_NODISCARD sys_error_t Sys_VirtualRelease( void *memory, common::usize size ) noexcept;

} // namespace cypher::engine::sys

#endif // CYPHER_ENGINE_SYSTEM_PUBLIC_H
