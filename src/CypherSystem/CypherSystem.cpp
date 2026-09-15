//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem.cpp
//  Purpose: Implements platform-independent CypherSystem behavior.
//  Details: Native path and timing calls enter through private platform adapters;
//           virtual-memory calls reuse the single Tier0 implementation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSystem_Local.h"
#include "CypherCommon_PlatformMemory.h"

#include <atomic>     // Cross-thread cooperative quit request.
#include <chrono>     // std::chrono::steady_clock for monotonic runtime timing.
#include <cstdio>     // Bootstrap stderr output independent of CypherLog.
#include <cstdlib>    // std::abort for fatal termination.
#include <cstring>    // std::strcmp and bounded startup copies.
#include <mutex>      // Prevents interleaved bootstrap/fatal diagnostics.
#include <string>     // Temporary normalized path representation.
#include <utility>    // std::move for resolved path ownership.

namespace cypher::engine::sys
{

namespace {

runtime_state_t sysState{}; // Process-wide System state; Host owns its lifetime.
std::atomic_bool quitRequested{ false }; // Worker threads may request an orderly Host shutdown.
std::mutex bootstrapOutputMutex{};       // Serializes complete emergency-output records.

void Sys_DebugVPrintfUnlocked( const char *format, std::va_list arguments ) noexcept
{
    if ( format == nullptr ) {
        return;
    }

    std::vfprintf( stderr, format, arguments );
    std::fflush( stderr );
}

} // namespace

std::filesystem::path Sys_PathFromUtf8( const char *utf8Path )
{
    if ( utf8Path == nullptr || utf8Path[0] == '\0' ) {
        return {};
    }

    const auto *first = reinterpret_cast<const char8_t *>( utf8Path );
    return std::filesystem::path( std::u8string( first ) );
}

/*
================

Sys_CopyPath

Normalizes a host path and copies it into fixed engine storage. Paths that do
not fit fail instead of being silently truncated.

================
*/
bool Sys_CopyPath( char *pathOut, const common::usize pathCapacity, const std::filesystem::path &path )
{
    if ( pathOut == nullptr || pathCapacity == 0u ) {
        return false;
    }

    const std::u8string normalizedPath = path.lexically_normal().u8string();
    if ( normalizedPath.size() >= pathCapacity ) {
        pathOut[0] = '\0';
        return false;
    }

    std::memcpy( pathOut, normalizedPath.data(), normalizedPath.size() );
    pathOut[normalizedPath.size()] = '\0';
    return true;
}

/*
================
Sys_ResolveAbsolutePath

Resolves command-line overrides against the startup working directory and then
normalizes the result. weakly_canonical permits output directories whose final
components do not exist yet.
================
*/
bool Sys_ResolveAbsolutePath(
    const std::filesystem::path &path,
    std::filesystem::path &absolutePathOut )
{
    if ( path.empty() ) {
        absolutePathOut.clear();
        return false;
    }

    std::error_code error{};
    std::filesystem::path absolutePath = path;
    if ( !absolutePath.is_absolute() ) {
        absolutePath = std::filesystem::absolute( absolutePath, error );
        if ( error ) {
            absolutePathOut.clear();
            return false;
        }
    }

    std::filesystem::path normalizedPath =
        std::filesystem::weakly_canonical( absolutePath, error );
    if ( error ) {
        error.clear();
        normalizedPath = absolutePath.lexically_normal();
    }

    absolutePathOut = std::move( normalizedPath );
    return true;
}

static bool Sys_EventIsValid( const sys_event_t &event ) noexcept
{
    const common::u16 type = static_cast<common::u16>( event.type );
    if ( type <= static_cast<common::u16>( sys_event_type_t::NONE ) ||
         type >= static_cast<common::u16>( sys_event_type_t::COUNT ) ) {
        return false;
    }

    const auto modifiersAreValid = []( const sys_key_modifiers_t modifiers ) noexcept {
        return ( modifiers & ~SYS_KEYMODIFIER_MASK ) == 0u;
    };

    switch ( event.type ) {
        case sys_event_type_t::QUIT_REQUESTED:
            return true;

        case sys_event_type_t::WINDOW_CLOSE_REQUESTED:
        case sys_event_type_t::WINDOW_FOCUS_GAINED:
        case sys_event_type_t::WINDOW_FOCUS_LOST:
        case sys_event_type_t::WINDOW_MINIMIZED:
        case sys_event_type_t::WINDOW_RESTORED:
            return event.payload.window.windowId != SYS_INVALID_WINDOW_ID;

        case sys_event_type_t::WINDOW_RESIZED:
        case sys_event_type_t::WINDOW_PIXEL_SIZE_CHANGED:
            return event.payload.window.windowId != SYS_INVALID_WINDOW_ID &&
                event.payload.window.width > 0u && event.payload.window.height > 0u;

        case sys_event_type_t::KEY:
            return event.payload.key.windowId != SYS_INVALID_WINDOW_ID &&
                event.payload.key.key > sys_key_t::NONE &&
                event.payload.key.key < sys_key_t::COUNT &&
                event.payload.key.action > sys_input_action_t::NONE &&
                event.payload.key.action < sys_input_action_t::COUNT &&
                modifiersAreValid( event.payload.key.modifiers );

        case sys_event_type_t::TEXT_INPUT: {
            const common::usize byteCount = event.payload.textInput.byteCount;
            return event.payload.textInput.windowId != SYS_INVALID_WINDOW_ID &&
                byteCount > 0u && byteCount < SYS_TEXT_INPUT_CAPACITY &&
                std::memchr( event.payload.textInput.utf8, '\0', byteCount ) == nullptr &&
                event.payload.textInput.utf8[byteCount] == '\0';
        }

        case sys_event_type_t::MOUSE_MOTION:
            return event.payload.mouseMotion.windowId != SYS_INVALID_WINDOW_ID;

        case sys_event_type_t::MOUSE_BUTTON:
            return event.payload.mouseButton.windowId != SYS_INVALID_WINDOW_ID &&
                event.payload.mouseButton.button > sys_mouse_button_t::NONE &&
                event.payload.mouseButton.button < sys_mouse_button_t::COUNT &&
                ( event.payload.mouseButton.action == sys_input_action_t::PRESSED ||
                  event.payload.mouseButton.action == sys_input_action_t::RELEASED ) &&
                modifiersAreValid( event.payload.mouseButton.modifiers );

        case sys_event_type_t::MOUSE_WHEEL:
            return event.payload.mouseWheel.windowId != SYS_INVALID_WINDOW_ID &&
                modifiersAreValid( event.payload.mouseWheel.modifiers );

        case sys_event_type_t::NONE:
        case sys_event_type_t::COUNT:
        default:
            return false;
    }
}

static bool Sys_VirtualRangeIsValid( const void *memory, const common::usize size ) noexcept
{
    const common::usize pageSize = Sys_VirtualPageSize();
    return memory != nullptr && size != 0u && pageSize != 0u &&
        ( reinterpret_cast<::cypher::common::uintptr>( memory ) % pageSize ) == 0u &&
        ( size % pageSize ) == 0u;
}

/*
================

Sys_FindArgvValue

Returns the value following an exact command-line option, or nullptr when the
option is absent or has no following value.

================
*/
const char *Sys_FindArgvValue( const init_info_t &initInfo, const char *argumentName )
{
    if ( initInfo.argv == nullptr || argumentName == nullptr ) {
        return nullptr;
    }

    for ( int argumentIndex = 1; argumentIndex + 1 < initInfo.argc; ++argumentIndex ) {
        if ( std::strcmp( initInfo.argv[argumentIndex], argumentName ) == 0 ) {
            return initInfo.argv[argumentIndex + 1];
        }
    }

    return nullptr;
}

/*
================

Sys_Init

================
*/
sys_error_t Sys_Init( const init_info_t &initInfo ) noexcept
{
    if ( sysState.initialized ) {
        return sys_error_t::ERR_IS_INIT;
    }
    if ( Sys_PlatformHasActiveWindow() || GLimp_HasActiveContext() ) {
        return sys_error_t::ERR_RESOURCE_BUSY;
    }
    if ( initInfo.argc < 0 || ( initInfo.argc > 0 && initInfo.argv == nullptr ) ||
         initInfo.appName == nullptr || initInfo.appName[0] == '\0' ||
         initInfo.organizationName == nullptr || initInfo.organizationName[0] == '\0' ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }
    for ( int argumentIndex = 0; argumentIndex < initInfo.argc; ++argumentIndex ) {
        if ( initInfo.argv[argumentIndex] == nullptr ) {
            return sys_error_t::ERR_INVALID_ARGUMENT;
        }
    }
    if ( std::strlen( initInfo.appName ) >= SYS_MAX_NAME_LENGTH ||
         std::strlen( initInfo.organizationName ) >= SYS_MAX_NAME_LENGTH ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    sysState = {};
    const sys_error_t pathResult = Sys_PlatformBuildPaths( initInfo, sysState.paths );
    if ( pathResult != sys_error_t::OK ) {
        sysState = {};
        return pathResult;
    }

    if ( ::cypher::common::Cy_SystemInfoInit() != ::cypher::common::CY_TRUE ) {
        sysState = {};
        return sys_error_t::ERR_INTERNAL_ERROR;
    }

    quitRequested.store( false, std::memory_order_release );
    sysState.initialized = true;
    return sys_error_t::OK;
}

/*
================

Sys_Shutdown

================
*/
sys_error_t Sys_Shutdown() noexcept
{
    if ( !sysState.initialized ) {
        return sys_error_t::ERR_NOT_INIT;
    }
    if ( GLimp_HasActiveContext() || Sys_PlatformHasActiveWindow() ) {
        return sys_error_t::ERR_RESOURCE_BUSY;
    }

    Sys_WindowSubsystemShutdown();
    sysState = {};
    quitRequested.store( false, std::memory_order_release );
    return sys_error_t::OK;
}

bool Sys_IsInitialized() noexcept
{
    return sysState.initialized;
}

void Sys_RequestQuit() noexcept
{
    quitRequested.store( true, std::memory_order_release );
}

bool Sys_IsQuitRequested() noexcept
{
    return quitRequested.load( std::memory_order_acquire );
}

void Sys_Quit( const common::i32 exitCode ) noexcept
{
    ::cypher::common::Cy_ProcessExit( exitCode );
}

void Sys_DebugPrintf( const char *format, ... ) noexcept
{
    std::va_list arguments;
    va_start( arguments, format );
    Sys_DebugVPrintf( format, arguments );
    va_end( arguments );
}

void Sys_DebugVPrintf( const char *format, std::va_list arguments ) noexcept
{
    const std::lock_guard<std::mutex> lock( bootstrapOutputMutex );
    Sys_DebugVPrintfUnlocked( format, arguments );
}

void Sys_Error( const char *format, ... ) noexcept
{
    std::va_list arguments;
    va_start( arguments, format );
    Sys_VError( format, arguments );
}

void Sys_VError( const char *format, std::va_list arguments ) noexcept
{
    {
        const std::lock_guard<std::mutex> lock( bootstrapOutputMutex );
        std::fputs( "FATAL: ", stderr );
        Sys_DebugVPrintfUnlocked( format != nullptr ? format : "unknown fatal error", arguments );
        std::fputc( '\n', stderr );
        std::fflush( stderr );
    }

    std::abort();
}

/*
================

Sys_QueueEvent

Appends one normalized OS event to the process queue. Retaining the newest input
is more useful than retaining stale input, so overflow displaces the oldest event.

================
*/
bool Sys_QueueEvent( const sys_event_t &event ) noexcept
{
    if ( !Sys_EventIsValid( event ) ) {
        return false;
    }

    bool withoutOverflow = true;

    if ( sysState.eventCount == SYS_EVENT_QUEUE_CAPACITY ) {
        sysState.eventHead =
            ( sysState.eventHead + 1u ) % SYS_EVENT_QUEUE_CAPACITY;
        --sysState.eventCount;
        ++sysState.droppedEventCount;
        withoutOverflow = false;
    }

    const common::u32 eventTail =
        ( sysState.eventHead + sysState.eventCount ) %
        SYS_EVENT_QUEUE_CAPACITY;

    sysState.events[eventTail] = event;
    ++sysState.eventCount;
    return withoutOverflow;
}

/*
================

Sys_PollEvent

Removes the oldest pending event. Empty polling also clears the destination so
callers cannot accidentally process a value left over from an earlier frame.

================
*/
bool Sys_PollEvent( sys_event_t &eventOut ) noexcept
{
    if ( sysState.eventCount == 0u ) {
        eventOut = {};
        return false;
    }

    eventOut = sysState.events[sysState.eventHead];
    sysState.events[sysState.eventHead] = {};
    sysState.eventHead =
        ( sysState.eventHead + 1u ) % SYS_EVENT_QUEUE_CAPACITY;
    --sysState.eventCount;

    if ( sysState.eventCount == 0u ) {
        sysState.eventHead = 0u;
    }

    return true;
}

void Sys_ClearEvents() noexcept
{
    while ( sysState.eventCount > 0u ) {
        sysState.events[sysState.eventHead] = {};
        sysState.eventHead =
            ( sysState.eventHead + 1u ) % SYS_EVENT_QUEUE_CAPACITY;
        --sysState.eventCount;
    }

    sysState.eventHead = 0u;
}

common::u32 Sys_EventCount() noexcept
{
    return sysState.eventCount;
}

common::u64 Sys_DroppedEventCount() noexcept
{
    return sysState.droppedEventCount;
}

const paths_t &Sys_Paths() noexcept
{
    return sysState.paths;
}

sys_error_t Sys_GetPaths( paths_t &pathsOut ) noexcept
{
    pathsOut = {};
    if ( !sysState.initialized ) {
        return sys_error_t::ERR_NOT_INIT;
    }

    pathsOut = sysState.paths;
    return sys_error_t::OK;
}

const char *Sys_PathBasename( const char *path ) noexcept
{
    if ( path == nullptr || path[0] == '\0' ) {
        return "";
    }

    const char *basename = path;
    for ( const char *cursor = path; *cursor != '\0'; ++cursor ) {
        if ( *cursor == '/' || *cursor == '\\' ) {
            basename = cursor + 1;
        }
    }
    return basename;
}

common::u64 Sys_TimeNowNanoseconds() noexcept
{
    const auto now = std::chrono::steady_clock::now();
    return static_cast<common::u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>( now.time_since_epoch() ).count() );
}

common::f64 Sys_TimeNowSeconds() noexcept
{
    return static_cast<common::f64>( Sys_TimeNowNanoseconds() ) / 1000000000.0;
}

void Sys_SleepMilliseconds( const common::u64 milliseconds ) noexcept
{
    Sys_PlatformSleepMilliseconds( milliseconds );
}

bool Sys_LocalTime( const std::time_t timeValue, std::tm &timeOut ) noexcept
{
    return Sys_PlatformLocalTime( timeValue, timeOut );
}

const system_info_t *Sys_GetSystemInfo() noexcept
{
    return ::cypher::common::Cy_SystemInfoGet();
}

system_memory_status_t Sys_QueryMemoryStatus() noexcept
{
    return ::cypher::common::Cy_SystemInfoQueryMemoryStatus();
}

system_disk_status_t Sys_QueryDiskStatus( const char *path ) noexcept
{
    return ::cypher::common::Cy_SystemInfoQueryDiskStatus( path );
}

system_power_state_t Sys_QueryPowerState() noexcept
{
    return ::cypher::common::Cy_SystemInfoQueryPowerState();
}

common::usize Sys_FormatSystemReport(
    char *destination,
    const common::usize destinationCapacity ) noexcept
{
    return ::cypher::common::Cy_SystemInfoFormatReport(
        destination,
        destinationCapacity );
}

void Sys_PrintSystemReport() noexcept
{
    char report[::cypher::common::CY_SYSTEMINFO_REPORT_MAX]{};
    (void)Sys_FormatSystemReport( report, sizeof( report ) );
    Sys_DebugPrintf( "%s", report );
}

bool Sys_InitCpuMonitor( cpu_monitor_t &monitor ) noexcept
{
    return ::cypher::common::Cy_CPUMonitorInit( &monitor ) ==
        ::cypher::common::CY_TRUE;
}

bool Sys_ResetCpuMonitor( cpu_monitor_t &monitor ) noexcept
{
    return ::cypher::common::Cy_CPUMonitorReset( &monitor ) ==
        ::cypher::common::CY_TRUE;
}

bool Sys_SampleCpuMonitor(
    cpu_monitor_t &monitor,
    cpu_monitor_sample_t &sampleOut ) noexcept
{
    return ::cypher::common::Cy_CPUMonitorSample( &monitor, &sampleOut ) ==
        ::cypher::common::CY_TRUE;
}

process_id_t Sys_GetCurrentProcessId() noexcept
{
    return ::cypher::common::Cy_ProcessGetCurrentId();
}

environment_get_result_t Sys_GetEnvironment(
    const char *name,
    char *destination,
    const common::usize destinationCapacity ) noexcept
{
    return ::cypher::common::Cy_EnvironmentGet(
        name,
        destination,
        destinationCapacity );
}

bool Sys_SetEnvironment( const char *name, const char *value ) noexcept
{
    return ::cypher::common::Cy_EnvironmentSet( name, value ) ==
        ::cypher::common::CY_TRUE;
}

bool Sys_UnsetEnvironment( const char *name ) noexcept
{
    return ::cypher::common::Cy_EnvironmentUnset( name ) ==
        ::cypher::common::CY_TRUE;
}

bool Sys_HasEnvironment( const char *name ) noexcept
{
    return ::cypher::common::Cy_EnvironmentHas( name ) ==
        ::cypher::common::CY_TRUE;
}

bool Sys_InitLibrary( dynamic_library_t &library ) noexcept
{
    return ::cypher::common::Cy_DynamicLibraryInit( &library ) ==
        ::cypher::common::CY_TRUE;
}

bool Sys_LoadLibrary(
    dynamic_library_t &library,
    const char *path,
    const dynamic_library_flags_t flags ) noexcept
{
    return ::cypher::common::Cy_DynamicLibraryLoadEx( &library, path, flags ) ==
        ::cypher::common::CY_TRUE;
}

bool Sys_UnloadLibrary( dynamic_library_t &library ) noexcept
{
    return ::cypher::common::Cy_DynamicLibraryUnload( &library ) ==
        ::cypher::common::CY_TRUE;
}

bool Sys_IsLibraryLoaded( const dynamic_library_t &library ) noexcept
{
    return ::cypher::common::Cy_DynamicLibraryIsLoaded( &library ) ==
        ::cypher::common::CY_TRUE;
}

void *Sys_GetLibrarySymbol(
    dynamic_library_t &library,
    const char *symbolName ) noexcept
{
    return ::cypher::common::Cy_DynamicLibraryGetSymbol( &library, symbolName );
}

const char *Sys_GetLibraryError( const dynamic_library_t &library ) noexcept
{
    return ::cypher::common::Cy_DynamicLibraryGetLastError( &library );
}

common::usize Sys_VirtualPageSize() noexcept
{
    return ::cypher::common::Cy_PlatformMemoryGetInfo().nPageSize;
}

void *Sys_VirtualReserve( const common::usize size ) noexcept
{
    const common::usize pageSize = Sys_VirtualPageSize();
    if ( size == 0u || pageSize == 0u || ( size % pageSize ) != 0u ) {
        return nullptr;
    }
    return ::cypher::common::Cy_PlatformMemoryReserve( size );
}

sys_error_t Sys_VirtualCommit( void *memory, const common::usize size ) noexcept
{
    if ( !Sys_VirtualRangeIsValid( memory, size ) ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }
    return ::cypher::common::Cy_PlatformMemoryCommit( memory, size ) == ::cypher::common::CY_TRUE
        ? sys_error_t::OK
        : sys_error_t::ERR_INTERNAL_ERROR;
}

sys_error_t Sys_VirtualDecommit( void *memory, const common::usize size ) noexcept
{
    if ( !Sys_VirtualRangeIsValid( memory, size ) ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }
    return ::cypher::common::Cy_PlatformMemoryDecommit( memory, size ) == ::cypher::common::CY_TRUE
        ? sys_error_t::OK
        : sys_error_t::ERR_INTERNAL_ERROR;
}

sys_error_t Sys_VirtualRelease( void *memory, const common::usize size ) noexcept
{
    if ( !Sys_VirtualRangeIsValid( memory, size ) ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }
    return ::cypher::common::Cy_PlatformMemoryRelease( memory, size ) == ::cypher::common::CY_TRUE
        ? sys_error_t::OK
        : sys_error_t::ERR_INTERNAL_ERROR;
}

} // namespace cypher::engine::sys
