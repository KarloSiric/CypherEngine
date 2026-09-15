//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_MacOS/CypherSystem_MacOS_Main.cpp
//  Purpose: Implements the macOS-specific CypherSystem root adapter.
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

#if CYPHER_PLATFORM_MACOS

#include "CypherSystem_Local.h"

#include <mach-o/dyld.h> // _NSGetExecutablePath.

#include <cstdint>      // std::uint32_t required by _NSGetExecutablePath.
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
    std::uint32_t executableBufferSize = static_cast<std::uint32_t>( sizeof( executableBuffer ) );
    if ( _NSGetExecutablePath( executableBuffer, &executableBufferSize ) != 0 ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    std::filesystem::path executablePath = std::filesystem::weakly_canonical(
        Sys_PathFromUtf8( executableBuffer ),
        error );
    if ( error ) {
        error.clear();
        executablePath = Sys_PathFromUtf8( executableBuffer );
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
        const char *home = std::getenv( "HOME" );
        if ( home == nullptr || home[0] == '\0' ) {
            return sys_error_t::ERR_PATH_QUERY_FAILED;
        }
        userPath = Sys_PathFromUtf8( home ) /
            "Library" / "Application Support" /
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

} // namespace cypher::engine::sys

#endif // CYPHER_PLATFORM_MACOS
