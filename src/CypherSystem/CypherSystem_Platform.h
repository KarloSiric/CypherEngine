//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_Platform.h
//  Purpose: Declares the CypherSystem System Platform module.
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

#ifndef CYPHER_ENGINE_SYSTEM_PLATFORM_H
#define CYPHER_ENGINE_SYSTEM_PLATFORM_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherSystem_Error.h"
#include "Engine/CypherCommon.h"

#include <ctime>       // std::time_t / std::tm for local time conversion.

namespace cypher::engine::sys
{

constexpr common::u32 SYS_MAX_PATH_LENGTH = 1024u;   // Fixed storage for normalized host paths, including terminator.
constexpr common::u32 SYS_MAX_NAME_LENGTH = 256u;    // Fixed storage for application/organization names.

/*
================
Platform Detection Types
================
*/
enum class platform_t : common::u8 {
    UNKNOWN = 0,  // Build target could not be identified.
    WINDOWS,      // Microsoft Windows desktop target.
    LINUX,        // Linux desktop target.
    MACOSX        // Apple macOS desktop target.
};

enum class compiler_t : common::u8 {
    UNKNOWN = 0,  // Toolchain could not be identified.
    CLANG,        // LLVM Clang or Apple Clang.
    GCC,          // GNU Compiler Collection.
    MSVC          // Microsoft Visual C++ compiler.
};

/*
================
System Startup Data
================
*/
struct init_info_t {
    int argc{ 0 };                                  // Process argument count borrowed for runtime queries.
    const char *const *argv{ nullptr };             // Process arguments borrowed until system shutdown.

    const char *szAppName{ nullptr };                // Required application name copied during initialization.
    const char *szOrganizationName{ nullptr };       // Required organization name copied during initialization.
};

struct paths_t {
    char szExecutablePath[SYS_MAX_PATH_LENGTH]{};    // Absolute path to the running executable.
    char executableDir[SYS_MAX_PATH_LENGTH]{};       // Absolute directory containing the executable.
    char workingDir[SYS_MAX_PATH_LENGTH]{};          // Process working directory captured at startup.

    char szBasePath[SYS_MAX_PATH_LENGTH]{};          // Default engine/content base directory.
    char szUserPath[SYS_MAX_PATH_LENGTH]{};          // Per-user writable application directory.
};

struct runtime_state_t {
    bool initialized{ false };                       // True only after every platform query succeeds.

    char szAppName[SYS_MAX_NAME_LENGTH]{};           // Owned copy of application name.
    char szOrganizationName[SYS_MAX_NAME_LENGTH]{};  // Owned copy of organization name.

    int argc{ 0 };                                   // Borrowed process argument count.
    const char *const *argv{ nullptr };              // Borrowed process argument vector.
    paths_t sysPaths{};                              // Cached platform path discovery results.

};

/*
================
System API
================
*/
sys_error_t CypherSystem_Init( const init_info_t &initInfo );
sys_error_t CypherSystem_Shutdown();

bool CypherSystem_IsInitialized();

platform_t CypherSystem_PlatformType();
compiler_t CypherSystem_CompilerType();

const char *CypherSystem_PlatformName( platform_t type );
const char *CypherSystem_CompilerName( compiler_t type );

const paths_t &CypherSystem_Paths();
sys_error_t CypherSystem_GetPaths( paths_t &pathsOut );

const char *CypherSystem_PathBasename( const char *path );

common::f64 CypherSystem_TimeNowSeconds();
void CypherSystem_SleepMilliseconds( common::u64 milliseconds );

bool CypherSystem_LocalTime( std::time_t timeValue, std::tm &timeOut );

common::usize CypherSystem_VirtualPageSize();

void *CypherSystem_VirtualReserve( const common::usize size ); // Reserves inaccessible address space without committing pages.

sys_error_t CypherSystem_VirtualCommit( void *memory, common::usize size ); // Makes reserved pages readable and writable.

sys_error_t CypherSystem_VirtualDecommit( void *memory, common::usize size ); // Discards page contents but retains the address range.

sys_error_t CypherSystem_VirtualRelease( void *memory, common::usize size ); // Releases the complete reservation to the OS.

/*
================
Cross Platform And Compiler Detection
================
*/

#   if defined( _WIN32 ) || defined( __WIN32__ ) || defined( WIN32 ) || defined( MINGW32 )
#       define CYPHER_PLATFORM_WINDOWS    1
#   elif defined( __APPLE__ ) && defined( __MACH__ )
#       define CYPHER_PLATFORM_MACOS      1
#   elif defined( __linux__ )
#       define CYPHER_PLATFORM_LINUX      1
#   else
#       error "Unsupported platform for CypherSystem platform detection."
#   endif

#   if defined( _MSC_VER )
#       define CYPHER_COMPILER_MSVC       1
#   elif defined( __clang__ )
#       define CYPHER_COMPILER_CLANG      1
#   elif defined( __GNUC__ )
#       define CYPHER_COMPILER_GCC        1
#   else
#       error "Unsupported compiler for CypherSystem compiler detection."
#   endif

}       // namespace cypher::engine::sys

#endif // CYPHER_ENGINE_SYSTEM_PLATFORM_H
