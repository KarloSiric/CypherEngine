//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_Win32/CypherSystem_Win32_Main.cpp
//  Purpose: Implements the Win32-specific CypherSystem root adapter.
//  Details: This unit owns Windows path, sleep, and calendar-time calls. It does
//           not define the application's process entry point.
//
//  History:
//  - Created by Karlo Siric on 2026-08-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Platform.h"

#if CYPHER_PLATFORM_WINDOWS

#include "CypherSystem_Local.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h> // Sleep and thread-safe Windows calendar conversion.

#include <filesystem>   // Non-throwing path discovery and normalization.
#include <system_error> // std::error_code.

namespace cypher::engine::sys
{

sys_error_t Sys_PlatformBuildPaths( const init_info_t &initInfo, paths_t &pathsOut )
{
    pathsOut = {};
    std::error_code error{};

    const std::filesystem::path workingDir = std::filesystem::current_path( error );
    if ( error ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }

    const char *executableUtf8 = ::cypher::common::Cy_ProcessGetExecutablePath();
    if ( executableUtf8 == nullptr || executableUtf8[0] == '\0' ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }

    std::filesystem::path executablePath = std::filesystem::weakly_canonical(
        Sys_PathFromUtf8( executableUtf8 ),
        error );
    if ( error ) {
        error.clear();
        executablePath = Sys_PathFromUtf8( executableUtf8 );
    }

    const std::filesystem::path executableDir = executablePath.parent_path();
    const char *baseOverride = Sys_FindArgvValue( initInfo, "-basedir" );
    const std::filesystem::path requestedBasePath = baseOverride != nullptr && baseOverride[0] != '\0'
        ? Sys_PathFromUtf8( baseOverride )
        : workingDir;

    const char *userOverride = Sys_FindArgvValue( initInfo, "-userpath" );
    std::filesystem::path userPath{};
    if ( userOverride != nullptr && userOverride[0] != '\0' ) {
        userPath = Sys_PathFromUtf8( userOverride );
    } else {
        char appDataBuffer[SYS_MAX_PATH_LENGTH]{};
        const ::cypher::common::cy_environment_get_result_t appDataResult =
            ::cypher::common::Cy_EnvironmentGet(
                "APPDATA",
                appDataBuffer,
                sizeof( appDataBuffer ) );
        if ( appDataResult.exists == ::cypher::common::CY_FALSE ||
             appDataResult.cchRequired == 0u ) {
            return sys_error_t::ERR_PATH_QUERY_FAILED;
        }
        if ( appDataResult.isTruncated == ::cypher::common::CY_TRUE ) {
            return sys_error_t::ERR_PATH_TOO_LONG;
        }
        userPath = Sys_PathFromUtf8( appDataBuffer ) /
            Sys_PathFromUtf8( initInfo.organizationName ) /
            Sys_PathFromUtf8( initInfo.appName );
    }

    std::filesystem::path basePath{};
    std::filesystem::path resolvedUserPath{};
    if ( !Sys_ResolveAbsolutePath( requestedBasePath, basePath ) ||
         !Sys_ResolveAbsolutePath( userPath, resolvedUserPath ) ) {
        return sys_error_t::ERR_INVALID_PATH;
    }

    std::filesystem::create_directories( resolvedUserPath, error );
    if ( error ) {
        return sys_error_t::ERR_DIRECTORY_CREATE_FAILED;
    }
    if ( !Sys_CopyPath( pathsOut.executablePath, sizeof( pathsOut.executablePath ), executablePath ) ||
         !Sys_CopyPath( pathsOut.executableDir, sizeof( pathsOut.executableDir ), executableDir ) ||
         !Sys_CopyPath( pathsOut.workingDir, sizeof( pathsOut.workingDir ), workingDir ) ||
         !Sys_CopyPath( pathsOut.basePath, sizeof( pathsOut.basePath ), basePath ) ||
         !Sys_CopyPath( pathsOut.userPath, sizeof( pathsOut.userPath ), resolvedUserPath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    return sys_error_t::OK;
}

void Sys_PlatformSleepMilliseconds( common::u64 milliseconds ) noexcept
{
    // Sleep accepts a 32-bit interval. Chunk very long waits instead of wrapping
    // the caller's 64-bit duration into a much shorter delay.
    while ( milliseconds > static_cast<common::u64>( MAXDWORD - 1u ) ) {
        Sleep( MAXDWORD - 1u );
        milliseconds -= static_cast<common::u64>( MAXDWORD - 1u );
    }
    Sleep( static_cast<DWORD>( milliseconds ) );
}

bool Sys_PlatformLocalTime( const std::time_t timeValue, std::tm &timeOut ) noexcept
{
    return localtime_s( &timeOut, &timeValue ) == 0;
}

} // namespace cypher::engine::sys

#endif // CYPHER_PLATFORM_WINDOWS
