//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_Local.h
//  Purpose: Declares private state and adapters shared by CypherSystem sources.
//  Details: This header is not an engine API. Only CypherSystem implementation
//           files and native platform backends may include it.
//
//  History:
//  - Created by Karlo Siric on 2026-08-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_SYSTEM_LOCAL_H
#define CYPHER_ENGINE_SYSTEM_LOCAL_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSystem_Public.h"
#include "CypherSystem_OpenGL.h"

#include <filesystem> // std::filesystem::path used while discovering host paths.

namespace cypher::engine::sys
{

struct runtime_state_t {
    bool initialized{ false }; // True after all required startup queries complete.
    paths_t paths{};           // Cached platform path discovery results.

    sys_event_t events[SYS_EVENT_QUEUE_CAPACITY]{}; // Fixed FIFO storage owned by the main System thread.
    common::u32 eventHead{ 0u };                    // Physical index of the oldest pending event.
    common::u32 eventCount{ 0u };                   // Number of live entries currently in the queue.
    common::u64 droppedEventCount{ 0u };            // Lifetime count of events displaced by overflow.
};

// Shared helpers used by platform path implementations.
std::filesystem::path Sys_PathFromUtf8( const char *utf8Path );
bool Sys_CopyPath( char *pathOut, common::usize pathCapacity, const std::filesystem::path &path );
bool Sys_ResolveAbsolutePath(
    const std::filesystem::path &path,
    std::filesystem::path &absolutePathOut );
const char *Sys_FindArgvValue( const init_info_t &initInfo, const char *argumentName );

// Native adapters supplied by exactly one platform source set selected by CMake.
sys_error_t Sys_PlatformBuildPaths( const init_info_t &initInfo, paths_t &pathsOut );
void Sys_PlatformSleepMilliseconds( common::u64 milliseconds ) noexcept;
bool Sys_PlatformLocalTime( std::time_t timeValue, std::tm &timeOut ) noexcept;

// Window ownership remains private, but lifecycle shutdown must reject a live
// native window so SDL resources cannot outlive CypherSystem.
bool Sys_PlatformHasActiveWindow() noexcept;
void Sys_WindowSubsystemShutdown() noexcept; // Releases SDL video only when CypherSystem initialized it.

// OpenGL attributes must be applied after SDL video startup but before window
// creation. These adapters remain private because Renderer uses the public
// context operations rather than mutating SDL attributes directly.
sys_error_t GLimp_ApplyWindowAttributes( const gl_context_desc_t &description ) noexcept;
void GLimp_ResetWindowAttributes() noexcept;
bool GLimp_HasActiveContext() noexcept;
bool GLimp_WindowOwnsActiveContext( sys_window_id_t windowId ) noexcept;

} // namespace cypher::engine::sys

#endif // CYPHER_ENGINE_SYSTEM_LOCAL_H
