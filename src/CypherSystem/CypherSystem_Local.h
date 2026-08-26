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

#include <filesystem> // std::filesystem::path used while discovering host paths.

namespace cypher::engine::sys
{

struct runtime_state_t {
    bool initialized{ false };                     // True after all required startup queries complete.
    char appName[SYS_MAX_NAME_LENGTH]{};           // Owned application name copied from init_info_t.
    char organizationName[SYS_MAX_NAME_LENGTH]{};  // Owned organization name copied from init_info_t.
    int argc{ 0 };                                 // Borrowed process argument count.
    const char *const *argv{ nullptr };             // Borrowed process argument vector.
    paths_t paths{};                               // Cached platform path discovery results.
};

// Shared helpers used by platform path implementations.
bool Sys_CopyPath( char *pathOut, common::usize pathCapacity, const std::filesystem::path &path );
const char *Sys_FindArgvValue( const init_info_t &initInfo, const char *argumentName );

// Native adapters supplied by exactly one platform source set selected by CMake.
sys_error_t Sys_PlatformBuildPaths( const init_info_t &initInfo, paths_t &pathsOut );
void Sys_PlatformSleepMilliseconds( common::u64 milliseconds );
bool Sys_PlatformLocalTime( std::time_t timeValue, std::tm &timeOut );

} // namespace cypher::engine::sys

#endif // CYPHER_ENGINE_SYSTEM_LOCAL_H
