//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_Linux/CypherSystem_Linux_Main.cpp
//  Purpose: Implements the Linux-specific CypherSystem root adapter.
//  Details: This unit discovers process and user paths; shared POSIX services
//           remain in CypherSystem_POSIX.
//
//  History:
//  - Created by Karlo Siric on 2026-08-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Platform.h"

#if CYPHER_PLATFORM_LINUX

#include "CypherSystem_Local.h"

#include <unistd.h> // readlink for /proc/self/exe.

#include <cstdlib>      // std::getenv for HOME.
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

    char executableBuffer[SYS_MAX_PATH_LENGTH]{};
    const ssize_t executableLength = readlink(
        "/proc/self/exe",
        executableBuffer,
        sizeof( executableBuffer ) - 1u );
    if ( executableLength < 0 ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
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
        const char *home = std::getenv( "HOME" );
        if ( home == nullptr || home[0] == '\0' ) {
            return sys_error_t::ERR_PATH_QUERY_FAILED;
        }
        userPath = std::filesystem::path( home ) / ".local" / "share" / initInfo.appName;
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

} // namespace cypher::engine::sys

#endif // CYPHER_PLATFORM_LINUX
