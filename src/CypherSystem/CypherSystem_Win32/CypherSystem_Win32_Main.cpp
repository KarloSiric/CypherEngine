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

#include "CypherSystem_Local.h"
#include "CypherCommon_Platform.h"

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h> // GetModuleFileNameA, GetEnvironmentVariableA and Sleep.

#include <filesystem>   // Non-throwing path discovery and normalization.
#include <system_error> // std::error_code.

#if !CYPHER_PLATFORM_WINDOWS
    #error "CypherSystem_Win32_Main.cpp may only be built for Windows targets."
#endif

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

    char executableBuffer[SYS_MAX_PATH_LENGTH]{};
    const DWORD executableLength = GetModuleFileNameA(
        nullptr,
        executableBuffer,
        static_cast<DWORD>( sizeof( executableBuffer ) ) );
    if ( executableLength == 0u ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }
    if ( executableLength >= sizeof( executableBuffer ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }
    executableBuffer[executableLength] = '\0';

    std::filesystem::path executablePath = std::filesystem::weakly_canonical( executableBuffer, error );
    if ( error ) {
        error.clear();
        executablePath = executableBuffer;
    }

    const std::filesystem::path executableDir = executablePath.parent_path();
    const char *baseOverride = Sys_FindArgvValue( initInfo, "-basedir" );
    const std::filesystem::path basePath = baseOverride != nullptr && baseOverride[0] != '\0'
        ? std::filesystem::path( baseOverride )
        : workingDir;

    const char *userOverride = Sys_FindArgvValue( initInfo, "-userpath" );
    std::filesystem::path userPath{};
    if ( userOverride != nullptr && userOverride[0] != '\0' ) {
        userPath = userOverride;
    } else {
        char appDataBuffer[SYS_MAX_PATH_LENGTH]{};
        const DWORD appDataLength = GetEnvironmentVariableA(
            "APPDATA",
            appDataBuffer,
            static_cast<DWORD>( sizeof( appDataBuffer ) ) );
        if ( appDataLength == 0u ) {
            return sys_error_t::ERR_PATH_QUERY_FAILED;
        }
        if ( appDataLength >= sizeof( appDataBuffer ) ) {
            return sys_error_t::ERR_PATH_TOO_LONG;
        }
        appDataBuffer[appDataLength] = '\0';
        userPath = std::filesystem::path( appDataBuffer ) / initInfo.appName;
    }

    std::filesystem::create_directories( userPath, error );
    if ( error ) {
        return sys_error_t::ERR_DIRECTORY_CREATE_FAILED;
    }

    if ( !Sys_CopyPath( pathsOut.executablePath, sizeof( pathsOut.executablePath ), executablePath ) ||
         !Sys_CopyPath( pathsOut.executableDir, sizeof( pathsOut.executableDir ), executableDir ) ||
         !Sys_CopyPath( pathsOut.workingDir, sizeof( pathsOut.workingDir ), workingDir ) ||
         !Sys_CopyPath( pathsOut.basePath, sizeof( pathsOut.basePath ), basePath ) ||
         !Sys_CopyPath( pathsOut.userPath, sizeof( pathsOut.userPath ), userPath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    return sys_error_t::OK;
}

void Sys_PlatformSleepMilliseconds( const common::u64 milliseconds )
{
    Sleep( static_cast<DWORD>( milliseconds ) );
}

bool Sys_PlatformLocalTime( const std::time_t timeValue, std::tm &timeOut )
{
    return localtime_s( &timeOut, &timeValue ) == 0;
}

} // namespace cypher::engine::sys
