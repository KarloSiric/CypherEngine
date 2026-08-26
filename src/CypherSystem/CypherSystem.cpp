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

#include <chrono>     // std::chrono::steady_clock for monotonic runtime timing.
#include <cstring>    // std::strcmp and std::strncpy for fixed startup storage.
#include <string>     // Temporary normalized path representation.

namespace cypher::engine::sys
{

namespace {

runtime_state_t s_RuntimeState{}; // Process-wide System state; Host owns its lifetime.

} // namespace

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

    const std::string normalizedPath = path.lexically_normal().string();
    if ( normalizedPath.size() >= pathCapacity ) {
        pathOut[0] = '\0';
        return false;
    }

    std::strncpy( pathOut, normalizedPath.c_str(), pathCapacity - 1u );
    pathOut[pathCapacity - 1u] = '\0';
    return true;
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

    for ( int iArgument = 1; iArgument + 1 < initInfo.argc; ++iArgument ) {
        if ( std::strcmp( initInfo.argv[iArgument], argumentName ) == 0 ) {
            return initInfo.argv[iArgument + 1];
        }
    }

    return nullptr;
}

/*
================
Sys_Init
================
*/
sys_error_t Sys_Init( const init_info_t &initInfo )
{
    if ( s_RuntimeState.initialized ) {
        return sys_error_t::ERR_IS_INIT;
    }
    if ( initInfo.appName == nullptr || initInfo.appName[0] == '\0' ||
         initInfo.organizationName == nullptr || initInfo.organizationName[0] == '\0' ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }

    s_RuntimeState = {};
    std::strncpy( s_RuntimeState.appName, initInfo.appName, sizeof( s_RuntimeState.appName ) - 1u );
    std::strncpy(
        s_RuntimeState.organizationName,
        initInfo.organizationName,
        sizeof( s_RuntimeState.organizationName ) - 1u );
    s_RuntimeState.argc = initInfo.argc;
    s_RuntimeState.argv = initInfo.argv;

    const sys_error_t pathResult = Sys_PlatformBuildPaths( initInfo, s_RuntimeState.paths );
    if ( pathResult != sys_error_t::OK ) {
        s_RuntimeState = {};
        return pathResult;
    }

    s_RuntimeState.initialized = true;
    return sys_error_t::OK;
}

/*
================
Sys_Shutdown
================
*/
sys_error_t Sys_Shutdown()
{
    if ( !s_RuntimeState.initialized ) {
        return sys_error_t::ERR_NOT_INIT;
    }

    s_RuntimeState = {};
    return sys_error_t::OK;
}

bool Sys_IsInitialized()
{
    return s_RuntimeState.initialized;
}

const paths_t &Sys_Paths()
{
    return s_RuntimeState.paths;
}

sys_error_t Sys_GetPaths( paths_t &pathsOut )
{
    if ( !s_RuntimeState.initialized ) {
        return sys_error_t::ERR_NOT_INIT;
    }

    pathsOut = s_RuntimeState.paths;
    return sys_error_t::OK;
}

const char *Sys_PathBasename( const char *path )
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

common::f64 Sys_TimeNowSeconds()
{
    const auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<common::f64>( now.time_since_epoch() ).count();
}

void Sys_SleepMilliseconds( const common::u64 milliseconds )
{
    Sys_PlatformSleepMilliseconds( milliseconds );
}

bool Sys_LocalTime( const std::time_t timeValue, std::tm &timeOut )
{
    return Sys_PlatformLocalTime( timeValue, timeOut );
}

common::usize Sys_VirtualPageSize()
{
    return ::cypher::common::Cy_PlatformMemoryGetInfo().nPageSize;
}

void *Sys_VirtualReserve( const common::usize size )
{
    return ::cypher::common::Cy_PlatformMemoryReserve( size );
}

sys_error_t Sys_VirtualCommit( void *memory, const common::usize size )
{
    return ::cypher::common::Cy_PlatformMemoryCommit( memory, size ) == ::cypher::common::CY_TRUE
        ? sys_error_t::OK
        : sys_error_t::ERR_INTERNAL_ERROR;
}

sys_error_t Sys_VirtualDecommit( void *memory, const common::usize size )
{
    return ::cypher::common::Cy_PlatformMemoryDecommit( memory, size ) == ::cypher::common::CY_TRUE
        ? sys_error_t::OK
        : sys_error_t::ERR_INTERNAL_ERROR;
}

sys_error_t Sys_VirtualRelease( void *memory, const common::usize size )
{
    return ::cypher::common::Cy_PlatformMemoryRelease( memory, size ) == ::cypher::common::CY_TRUE
        ? sys_error_t::OK
        : sys_error_t::ERR_INTERNAL_ERROR;
}

} // namespace cypher::engine::sys
