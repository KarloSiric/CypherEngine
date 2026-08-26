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

#include <cstdarg> // std::va_list used by preformatted diagnostic output.
#include <ctime>   // std::time_t and std::tm used by the calendar-time boundary.

namespace cypher::engine::sys
{

constexpr common::u32 SYS_MAX_PATH_LENGTH = 1024u; // Storage for one normalized host path and its terminator.
constexpr common::u32 SYS_MAX_NAME_LENGTH = 256u;  // Storage for copied application and organization names.

/*
================

System Startup Data

The process owns argv. CypherSystem borrows those pointers until Sys_Shutdown.
Application and organization names are copied during Sys_Init.

================
*/
struct init_info_t {
    int argc{ 0 };                            // Number of process arguments available through argv.
    const char *const *argv{ nullptr };       // Borrowed process argument vector.
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
    CY_PRINTF_FORMAT_STRING const char *pFormat,
    ... ) noexcept CY_PRINTF_LIKE( 1, 2 ); // Writes raw bootstrap diagnostics without depending on CypherLog.

void Sys_DebugVPrintf(
    CY_PRINTF_FORMAT_STRING const char *pFormat,
    std::va_list arguments ) noexcept CY_PRINTF_LIKE( 1, 0 ); // va_list form used by higher-level diagnostic wrappers.

CYPHER_NORETURN void Sys_FatalError(
    CY_PRINTF_FORMAT_STRING const char *pFormat,
    ... ) noexcept CY_PRINTF_LIKE( 1, 2 ); // Reports an unrecoverable error and terminates without returning.

CYPHER_NORETURN void Sys_FatalVError(
    CY_PRINTF_FORMAT_STRING const char *pFormat,
    std::va_list arguments ) noexcept CY_PRINTF_LIKE( 1, 0 ); // va_list form of Sys_FatalError.

const paths_t &Sys_Paths();                    // Returns the cached path set; valid after successful initialization.

sys_error_t Sys_GetPaths( paths_t &pathsOut ); // Copies the cached path set after checking initialization.

const char *Sys_PathBasename( const char *path ); // Returns a pointer into path after its final native separator.

/*
================

System Time

================
*/
CYPHER_NODISCARD common::f64 Sys_TimeNowSeconds() noexcept; // Monotonic process-independent clock in seconds.
void Sys_SleepMilliseconds( common::u64 milliseconds ); // Suspends the calling thread for at least the requested interval.
bool Sys_LocalTime( std::time_t timeValue, std::tm &timeOut ); // Thread-safe conversion to local calendar time.

/*
================

System Virtual Memory

These are the engine-facing page operations. The implementation delegates to
the Tier0 platform-memory primitive so only one native VM backend exists.

================
*/
common::usize Sys_VirtualPageSize();
void *Sys_VirtualReserve( common::usize size );
sys_error_t Sys_VirtualCommit( void *memory, common::usize size );
sys_error_t Sys_VirtualDecommit( void *memory, common::usize size );
sys_error_t Sys_VirtualRelease( void *memory, common::usize size );

} // namespace cypher::engine::sys

#endif // CYPHER_ENGINE_SYSTEM_PUBLIC_H
