//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_Platform.cpp
//  Purpose: Implements the CypherSystem System Platform module.
//  Details: This file owns platform-facing system, window, and graphics context
//           boundaries. Keep OS-specific code isolated enough that higher-level
//           runtime code remains portable.
//
//  History:
//  - Created by Karlo Siric on 2026-04-20
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherSystem_Platform.h"
#include "CypherLog.h"

#ifdef CYPHER_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif

    #ifndef NOMINMAX
        #define NOMINMAX
    #endif

    #include <windows.h>    // Win32 path, sleep and virtual memory APIs.
#endif

#ifdef CYPHER_PLATFORM_MACOS
    #include <mach-o/dyld.h> // _NSGetExecutablePath.
#endif

#if defined( CYPHER_PLATFORM_MACOS ) || defined( CYPHER_PLATFORM_LINUX )
    #include <sys/mman.h>   // mmap / mprotect / madvise / munmap.
    #include <unistd.h>     // readlink / nanosleep / sysconf.
#endif

#include <cerrno>       // errno / EINTR.
#include <chrono>      // steady_clock timing.
#include <cstdint>     // std::uint32_t for macOS executable API.
#include <cstdlib>     // getenv.
#include <cstring>     // strcmp / strncpy for path buffers.
#include <ctime>       // nanosleep / timespec.
#include <filesystem>  // Path normalization and directory creation.
#include <string>      // Temporary path strings.
#include <system_error> // std::error_code for non-throwing filesystem calls.

namespace cypher::engine::sys
{

static runtime_state_t s_SysRuntimeState;

namespace {

/*
================
Sys_CopyPath
================
*/
bool Sys_CopyPath( char *outPath, const common::u32 outPathSize, const std::filesystem::path &path )
{
    if ( outPath == nullptr || outPathSize == 0u ) {
        return false;
    }

    std::string pathString = path.lexically_normal().string();

    if ( pathString.size() >= outPathSize ) {
        outPath[0] = '\0';
        return false;
    }

    std::strncpy( outPath, pathString.c_str(), outPathSize - 1u );
    outPath[outPathSize - 1u] = '\0';

    return true;
}

/*
================
Sys_FindArgvValue
================
*/
const char *Sys_FindArgvValue( const init_info_t &info, const char *argumentName )
{
    if ( info.argv == nullptr || argumentName == nullptr ) {
        return nullptr;
    }

    for ( int i = 1; i + 1 < info.argc; ++i ) {
        if ( std::strcmp( info.argv[i], argumentName ) == 0 ) {
            return info.argv[i + 1];
        }
    }

    return nullptr;
}

sys_error_t Sys_PlatformBuildPaths( const init_info_t &infoInit, paths_t &pathsOut );
void Sys_PlatformSleepMilliseconds( common::u64 milliseconds );
common::usize Sys_PlatformVirtualPageSize();
void *Sys_PlatformVirtualReserve( common::usize size );
bool Sys_PlatformVirtualCommit( void *memory, common::usize size );
bool Sys_PlatformVirtualDecommit( void *memory, common::usize size );
bool Sys_PlatformVirtualRelease( void *memory, common::usize size );

/*
================
Sys_AlignVirtualMemorySize

Rounds virtual-memory operation sizes up to the platform page size.
================
*/
common::usize Sys_AlignVirtualMemorySize( const common::usize size )
{
    if ( size == 0u ) {
        return 0u;
    }

    const common::usize pageSize = Sys_PlatformVirtualPageSize();
    if ( pageSize == 0u ) {
        return size;
    }

    const common::usize remainder = size % pageSize;
    if ( remainder == 0u ) {
        return size;
    }

    return size + ( pageSize - remainder );
}

}       // namespace

/*
================
Sys_PlatformType
================
*/
platform_t Sys_PlatformType() {
#   ifdef CYPHER_PLATFORM_WINDOWS
                return platform_t::WINDOWS;
#   elif defined( CYPHER_PLATFORM_MACOS )
                return platform_t::MACOS;
#   elif defined( CYPHER_PLATFORM_LINUX )
                return platform_t::LINUX;
#   else
                return platform_t::UNKNOWN;
#   endif
}

/*
================
Sys_CompilerType
================
*/
compiler_t Sys_CompilerType() {
#   ifdef CYPHER_COMPILER_MSVC
                return compiler_t::MSVC;
#   elif defined( CYPHER_COMPILER_CLANG )
                return compiler_t::CLANG;
#   elif defined( CYPHER_COMPILER_GCC )
                return compiler_t::GCC;
#   else
                return compiler_t::UNKNOWN;
#   endif
}

/*
================
Sys_Init

Copies startup info and builds platform paths.
================
*/
sys_error_t Sys_Init( const init_info_t &infoInit ) {
    if ( s_SysRuntimeState.initialized ) {
        return sys_error_t::ERR_IS_INIT;
    }
    if ( infoInit.appName == nullptr || infoInit.appName[0] == '\0' ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( infoInit.organizationName == nullptr || infoInit.organizationName[0] == '\0' ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }
    s_SysRuntimeState = {};

    std::strncpy(
                 s_SysRuntimeState.appName,
                 infoInit.appName,
                 sizeof( s_SysRuntimeState.appName ) - 1u
    );

    std::strncpy(
                 s_SysRuntimeState.organizationName,
                 infoInit.organizationName,
                 sizeof( s_SysRuntimeState.organizationName ) - 1u
    );

    s_SysRuntimeState.argc = infoInit.argc;
    s_SysRuntimeState.argv = infoInit.argv;

    sys_error_t pathsResult = Sys_PlatformBuildPaths( infoInit, s_SysRuntimeState.sysPaths );

    if ( pathsResult != sys_error_t::OK ) {
        s_SysRuntimeState = {};
        return pathsResult;
    }

    s_SysRuntimeState.initialized = true;

    return sys_error_t::OK;
}

/*
================
Sys_Shutdown
================
*/
sys_error_t Sys_Shutdown() {
    if ( !s_SysRuntimeState.initialized ) {
        return sys_error_t::ERR_NOT_INIT;
    }

    s_SysRuntimeState = {};

    s_SysRuntimeState.initialized = false;

    return sys_error_t::OK;
}

/*
================
Sys_IsInitialized
================
*/
bool Sys_IsInitialized() {
    return s_SysRuntimeState.initialized;
}

/*
================
Sys_PlatformName
================
*/
const char *Sys_PlatformName( platform_t type ) {
    switch( type ) {
        case platform_t::WINDOWS:   return "Windows";
    case platform_t::LINUX:         return "Linux";
    case platform_t::MACOS:         return "macOS";
        default:                    return "Unknown";
    }
}

/*
================
Sys_CompilerName
================
*/
const char *Sys_CompilerName( compiler_t type ) {
    switch( type ) {
        case compiler_t::CLANG: return "Clang";
        case compiler_t::GCC:   return "GCC";
        case compiler_t::MSVC:  return "MSVC";
        default:                return "Unknown";
    }
}

/*
================
Sys_PathBasename
================
*/
const char *Sys_PathBasename( const char *path ) {
    if ( path == nullptr || path[0] == '\0' ) {
        return "";
    }

    const char *basename = path;

    for ( const char *it = path; *it != '\0'; ++it ) {
        if ( *it == '/' || *it == '\\' ) {
            basename = it + 1;
        }
    }

    return basename;
}

/*
================
Sys_TimeNowSeconds
================
*/
common::f64 Sys_TimeNowSeconds() {
    const auto now = std::chrono::steady_clock::now();
    const auto seconds = std::chrono::duration<common::f64>( now.time_since_epoch() );
    return seconds.count();
}

/*
================
Sys_LocalTime
================
*/
bool Sys_LocalTime( std::time_t timeValue, std::tm &timeOut ) {

#   ifdef CYPHER_PLATFORM_WINDOWS
                return localtime_s( &timeOut, &timeValue ) == 0;
#   else
                return localtime_r( &timeValue, &timeOut ) != nullptr;
#   endif

}

/*
================
Sys_Paths
================
*/
const paths_t &Sys_Paths() {
    return s_SysRuntimeState.sysPaths;
}

/*
================
Sys_GetPaths
================
*/
sys_error_t Sys_GetPaths( paths_t &pathsOut ) {
    if ( !s_SysRuntimeState.initialized ) {
        return sys_error_t::ERR_NOT_INIT;
    }

    pathsOut = s_SysRuntimeState.sysPaths;

    return sys_error_t::OK;
}

/*
================
Sys_SleepMilliseconds
================
*/
void Sys_SleepMilliseconds( common::u64 milliseconds ) {
    Sys_PlatformSleepMilliseconds( milliseconds );
    return ;
}

/*
================
Sys_VirtualPageSize
================
*/
common::usize Sys_VirtualPageSize()
{
    return Sys_PlatformVirtualPageSize();
}

/*
================
Sys_VirtualReserve
================
*/
void *Sys_VirtualReserve( const common::usize size )
{
    return Sys_PlatformVirtualReserve( Sys_AlignVirtualMemorySize( size ) );
}

/*
================
Sys_VirtualCommit
================
*/
sys_error_t Sys_VirtualCommit( void *memory, common::usize size )
{
    if ( Sys_PlatformVirtualCommit( memory, Sys_AlignVirtualMemorySize( size ) ) ) {
        return sys_error_t::OK;
    }

    return sys_error_t::ERR_INTERNAL_ERROR;
}

/*
================
Sys_VirtualDecommit
================
*/
sys_error_t Sys_VirtualDecommit( void *memory, common::usize size )
{
    if ( Sys_PlatformVirtualDecommit( memory, Sys_AlignVirtualMemorySize( size ) ) ) {
        return sys_error_t::OK;
    }

    return sys_error_t::ERR_INTERNAL_ERROR;
}

/*
================
Sys_VirtualRelease
================
*/
sys_error_t Sys_VirtualRelease( void *memory, common::usize size )
{
    if ( Sys_PlatformVirtualRelease( memory, Sys_AlignVirtualMemorySize( size ) ) ) {
        return sys_error_t::OK;
    }

    return sys_error_t::ERR_INTERNAL_ERROR;
}

namespace {

#ifdef CYPHER_PLATFORM_WINDOWS

/*
================
Sys_PlatformBuildPaths

Builds Win32 executable, base and user paths.
================
*/
sys_error_t Sys_PlatformBuildPaths( const init_info_t &infoInit, paths_t &pathsOut )
{
    pathsOut = {};

    std::error_code ec{};

    const std::filesystem::path workingDir = std::filesystem::current_path( ec );
    if ( ec ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }

    char executableBuffer[SYS_MAX_PATH_LENGTH]{};

    const DWORD executableLength = GetModuleFileNameA(
        nullptr,
        executableBuffer,
        static_cast<DWORD>( sizeof( executableBuffer ) )
    );

    if ( executableLength == 0u ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }

    if ( executableLength >= sizeof( executableBuffer ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    executableBuffer[executableLength] = '\0';

    std::filesystem::path executablePath = std::filesystem::weakly_canonical( executableBuffer, ec );

    if ( ec ) {
        ec.clear();
        executablePath = executableBuffer;
    }

    const std::filesystem::path executableDir = executablePath.parent_path();
    const char *basePathOverride = Sys_FindArgvValue( infoInit, "-basedir" );

    const std::filesystem::path basePath =
        ( basePathOverride != nullptr && basePathOverride[0] != '\0' ) ? std::filesystem::path( basePathOverride ) : workingDir;

    const char *userPathOverride = Sys_FindArgvValue( infoInit, "-userpath" );
    std::filesystem::path userPath{};

    if ( userPathOverride != nullptr && userPathOverride[0] != '\0' ) {
        userPath = userPathOverride;
    } else {
        char appDataBuffer[SYS_MAX_PATH_LENGTH]{};

        const DWORD appDataLength = GetEnvironmentVariableA(
            "APPDATA",
            appDataBuffer,
            static_cast<DWORD>( sizeof( appDataBuffer ) )
        );

        if ( appDataLength == 0u ) {
            return sys_error_t::ERR_PATH_QUERY_FAILED;
        }

        if ( appDataLength >= sizeof( appDataBuffer ) ) {
            return sys_error_t::ERR_PATH_TOO_LONG;
        }

        appDataBuffer[appDataLength] = '\0';

        userPath = std::filesystem::path( appDataBuffer ) / infoInit.appName;
    }

    std::filesystem::create_directories( userPath, ec );
    if ( ec ) {
        return sys_error_t::ERR_DIRECTORY_CREATE_FAILED;
    }

    if ( !Sys_CopyPath( pathsOut.executablePath, sizeof( pathsOut.executablePath ), executablePath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !Sys_CopyPath( pathsOut.executableDir, sizeof( pathsOut.executableDir ), executableDir ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !Sys_CopyPath( pathsOut.workingDir, sizeof( pathsOut.workingDir ), workingDir ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !Sys_CopyPath( pathsOut.basePath, sizeof( pathsOut.basePath ), basePath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !Sys_CopyPath( pathsOut.userPath, sizeof( pathsOut.userPath ), userPath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    return sys_error_t::OK;
}

/*
================
Sys_PlatformSleepMilliseconds
================
*/
void Sys_PlatformSleepMilliseconds( common::u64 milliseconds )
{
    Sleep( static_cast<DWORD>( milliseconds ) );
}

/*
================
Sys_PlatformVirtualPageSize
================
*/
common::usize Sys_PlatformVirtualPageSize()
{
    constexpr common::usize DEFAULT_PAGE_SIZE = 4096u;
    SYSTEM_INFO info{};
    GetSystemInfo( &info );

    if ( info.dwPageSize == 0u ) {
        LOG_WARNING( log::channel_t::PLATFORM, "GetSystemInfo page size query failed; using default page size %zu.", DEFAULT_PAGE_SIZE );
        return DEFAULT_PAGE_SIZE;
    }

    return static_cast<common::usize>( info.dwPageSize );
}

/*
================
Sys_PlatformVirtualReserve
================
*/
void *Sys_PlatformVirtualReserve( common::usize size )
{
    if ( size == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual reserve failed: requested size is zero." );
        return nullptr;
    }

    void *memory = VirtualAlloc( nullptr, size, MEM_RESERVE, PAGE_NOACCESS );

    if ( memory == nullptr ) {
        const DWORD error = GetLastError();
        LOG_ERROR( log::channel_t::PLATFORM, "virtual reserve failed: size=%zu, win32_error=%lu.", size, static_cast<unsigned long>( error ) );
        return nullptr;
    }

    return memory;
}

/*
================
Sys_PlatformVirtualCommit
================
*/
bool Sys_PlatformVirtualCommit( void *memory, common::usize size )
{
    if ( memory == nullptr || size == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual commit failed: memory=%p, size=%zu.", memory, size );
        return false;
    }

    void *result = VirtualAlloc( memory, size, MEM_COMMIT, PAGE_READWRITE );
    if ( result == nullptr ) {
        const DWORD error = GetLastError();
        LOG_ERROR( log::channel_t::PLATFORM, "virtual commit failed: memory=%p, size=%zu, win32_error=%lu.", memory, size, static_cast<unsigned long>( error ) );
        return false;
    }

    return true;
}

/*
================
Sys_PlatformVirtualDecommit
================
*/
bool Sys_PlatformVirtualDecommit( void *memory, common::usize size )
{
    if ( memory == nullptr || size == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual decommit failed: memory=%p, size=%zu.", memory, size );
        return false;
    }

    BOOL result = VirtualFree( memory, size, MEM_DECOMMIT );
    if ( result == 0 ) {
        const DWORD error = GetLastError();
        LOG_ERROR( log::channel_t::PLATFORM, "virtual decommit failed: memory=%p, size=%zu, win32_error=%lu.", memory, size, static_cast<unsigned long>( error ) );
        return false;
    }

    return true;
}

/*
================
Sys_PlatformVirtualRelease
================
*/
bool Sys_PlatformVirtualRelease( void *memory, common::usize size )
{
    if ( memory == nullptr || size == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual release failed: memory=%p, size=%zu.", memory, size );
        return false;
    }

    BOOL result = VirtualFree( memory, 0, MEM_RELEASE );
    if ( result == 0 ) {
        const DWORD error = GetLastError();
        LOG_ERROR( log::channel_t::PLATFORM, "virtual release failed: memory=%p, size=%zu, win32_error=%lu.", memory, size, static_cast<unsigned long>( error ) );
        return false;
    }

    return true;
}

#endif          // CYPHER_PLATFORM_WINDOWS

#ifdef CYPHER_PLATFORM_MACOS

/*
================
Sys_PlatformBuildPaths

Builds macOS executable, base and user paths.
================
*/
sys_error_t Sys_PlatformBuildPaths( const init_info_t &infoInit, paths_t &pathsOut )
{
    pathsOut = {};

    std::error_code ec{};

    const std::filesystem::path workingDir = std::filesystem::current_path( ec );
    if ( ec ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }

    char executableBuffer[SYS_MAX_PATH_LENGTH]{};
    std::uint32_t executableBufferSize = static_cast<std::uint32_t>( sizeof( executableBuffer ) );

    if ( _NSGetExecutablePath( executableBuffer, &executableBufferSize ) != 0 ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    std::filesystem::path executablePath = std::filesystem::weakly_canonical( executableBuffer, ec );

    if ( ec ) {
        ec.clear();
        executablePath = executableBuffer;
    }

    const std::filesystem::path executableDir = executablePath.parent_path();
    const char *basePathOverride = Sys_FindArgvValue( infoInit, "-basedir" );

    const std::filesystem::path basePath =
        ( basePathOverride != nullptr && basePathOverride[0] != '\0' ) ? std::filesystem::path( basePathOverride ) : workingDir;

    const char *userPathOverride = Sys_FindArgvValue( infoInit, "-userpath" );

    std::filesystem::path userPath{};

    if ( userPathOverride != nullptr && userPathOverride[0] != '\0' ) {
        userPath = userPathOverride;
    } else {
        const char *home = std::getenv( "HOME" );

        if ( home == nullptr || home[0] == '\0' ) {
            return sys_error_t::ERR_PATH_QUERY_FAILED;
        }

        userPath = std::filesystem::path( home ) /
                    "Library" /
                    "Application Support" /
                    infoInit.appName;
    }

    std::filesystem::create_directories( userPath, ec );
    if ( ec ) {
        return sys_error_t::ERR_DIRECTORY_CREATE_FAILED;
    }

    if ( !Sys_CopyPath( pathsOut.executablePath, sizeof( pathsOut.executablePath ), executablePath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !Sys_CopyPath( pathsOut.executableDir, sizeof( pathsOut.executableDir ), executableDir ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !Sys_CopyPath( pathsOut.workingDir, sizeof( pathsOut.workingDir ), workingDir ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !Sys_CopyPath( pathsOut.basePath, sizeof( pathsOut.basePath ), basePath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !Sys_CopyPath( pathsOut.userPath, sizeof( pathsOut.userPath ), userPath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    return sys_error_t::OK;
}

/*
================
Sys_PlatformSleepMilliseconds
================
*/
void Sys_PlatformSleepMilliseconds( common::u64 milliseconds )
{
    timespec request{};
    request.tv_sec = static_cast<time_t>( milliseconds / 1000u );
    request.tv_nsec = static_cast<long>( ( milliseconds % 1000u ) * 1000000u );

    while ( nanosleep( &request, &request ) == -1 && errno == EINTR ) {
    }
}

/*
================
Sys_PlatformVirtualPageSize
================
*/
common::usize Sys_PlatformVirtualPageSize()
{
    constexpr common::usize DEFAULT_PAGE_SIZE = 4096u;

    const long pageSize = sysconf( _SC_PAGESIZE );
    if ( pageSize <= 0 ) {
        LOG_WARNING( log::channel_t::PLATFORM, "sysconf(_SC_PAGESIZE) failed; using default page size %zu.", DEFAULT_PAGE_SIZE );
        return DEFAULT_PAGE_SIZE;
    }

    return static_cast<common::usize>( pageSize );
}

/*
================
Sys_PlatformVirtualReserve
================
*/
void *Sys_PlatformVirtualReserve( common::usize size )
{
    if ( size == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual reserve failed: requested size is zero." );
        return nullptr;
    }

    void *memory = mmap(
        nullptr,
        size,
        PROT_NONE,
        MAP_PRIVATE | MAP_ANON,
        -1,
        0
    );

    if ( memory == MAP_FAILED ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual reserve failed: size=%zu, errno=%d.", size, errno );
        return nullptr;
    }

    return memory;
}

/*
================
Sys_PlatformVirtualCommit
================
*/
bool Sys_PlatformVirtualCommit( void *memory, common::usize size )
{
    if ( memory == nullptr || size == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual commit failed: memory=%p, size=%zu.", memory, size );
        return false;
    }

    const int result = mprotect( memory, size, PROT_READ | PROT_WRITE );
    if ( result != 0 ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual commit failed: memory=%p, size=%zu, errno=%d.", memory, size, errno );
        return false;
    }

    return true;
}

/*
================
Sys_PlatformVirtualDecommit
================
*/
bool Sys_PlatformVirtualDecommit( void *memory, common::usize size )
{
    if ( memory == nullptr || size == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual decommit failed: memory=%p, size=%zu.", memory, size );
        return false;
    }

    int result = madvise( memory, size, MADV_FREE );
    if ( result != 0 ) {
        LOG_WARNING( log::channel_t::PLATFORM, "virtual decommit madvise warning: memory=%p, size=%zu, errno=%d.", memory, size, errno );
    }

    result = mprotect( memory, size, PROT_NONE );
    if ( result != 0 ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual decommit failed: memory=%p, size=%zu, errno=%d.", memory, size, errno );
        return false;
    }

    return true;
}

/*
================
Sys_PlatformVirtualRelease
================
*/
bool Sys_PlatformVirtualRelease( void *memory, common::usize size )
{
    if ( memory == nullptr || size == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual release failed: memory=%p, size=%zu.", memory, size );
        return false;
    }

    const int result = munmap( memory, size );
    if ( result != 0 ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual release failed: memory=%p, size=%zu, errno=%d.", memory, size, errno );
        return false;
    }

    return true;
}

#endif          // CYPHER_PLATFORM_MACOS

#ifdef CYPHER_PLATFORM_LINUX

/*
================
Sys_PlatformBuildPaths

Builds Linux executable, base and user paths.
================
*/
sys_error_t Sys_PlatformBuildPaths( const init_info_t &infoInit, paths_t &pathsOut )
{
    std::error_code ec{};
    pathsOut = {};

    const std::filesystem::path workingDir = std::filesystem::current_path( ec );
    if ( ec ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }

    char executableBuffer[SYS_MAX_PATH_LENGTH]{};

    const ssize_t executableLength = readlink(
        "/proc/self/exe",
        executableBuffer,
        sizeof( executableBuffer ) - 1u
    );

    if ( executableLength < 0 ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }

    executableBuffer[executableLength] = '\0';

    std::filesystem::path executablePath = std::filesystem::weakly_canonical( executableBuffer, ec );

    if ( ec ) {
        ec.clear();
        executablePath = executableBuffer;
    }

    const std::filesystem::path executableDir = executablePath.parent_path();
    const char *basePathOverride = Sys_FindArgvValue( infoInit, "-basedir" );

    const std::filesystem::path basePath =
        ( basePathOverride != nullptr && basePathOverride[0] != '\0' ) ? std::filesystem::path( basePathOverride ) : workingDir;

    const char *userPathOverride = Sys_FindArgvValue( infoInit, "-userpath" );

    std::filesystem::path userPath{};

    if ( userPathOverride != nullptr && userPathOverride[0] != '\0' ) {
        userPath = userPathOverride;
    } else {
        const char *home = std::getenv( "HOME" );

        if ( home == nullptr || home[0] == '\0' ) {
            return sys_error_t::ERR_PATH_QUERY_FAILED;
        }

        userPath = std::filesystem::path( home ) /
                    ".local" /
                    "share" /
                    infoInit.appName;
    }

    std::filesystem::create_directories( userPath, ec );

    if ( ec ) {
        return sys_error_t::ERR_DIRECTORY_CREATE_FAILED;
    }

    if ( !Sys_CopyPath( pathsOut.executablePath, sizeof( pathsOut.executablePath ), executablePath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !Sys_CopyPath( pathsOut.executableDir, sizeof( pathsOut.executableDir ), executableDir ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !Sys_CopyPath( pathsOut.workingDir, sizeof( pathsOut.workingDir ), workingDir ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !Sys_CopyPath( pathsOut.basePath, sizeof( pathsOut.basePath ), basePath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !Sys_CopyPath( pathsOut.userPath, sizeof( pathsOut.userPath ), userPath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    return sys_error_t::OK;
}

/*
================
Sys_PlatformSleepMilliseconds
================
*/
void Sys_PlatformSleepMilliseconds( common::u64 milliseconds )
{
    timespec request{};
    request.tv_sec = static_cast<time_t>( milliseconds / 1000u );
    request.tv_nsec = static_cast<long>( ( milliseconds % 1000u ) * 1000000u );

    while ( nanosleep( &request, &request ) == -1 && errno == EINTR ) {
    }
}

/*
================
Sys_PlatformVirtualPageSize
================
*/
common::usize Sys_PlatformVirtualPageSize()
{
    constexpr common::usize DEFAULT_PAGE_SIZE = 4096u;
    const long pageSize = sysconf( _SC_PAGESIZE );

    if ( pageSize <= 0 ) {
        LOG_WARNING( log::channel_t::PLATFORM, "sysconf(_SC_PAGESIZE) failed; using default page size %zu.", DEFAULT_PAGE_SIZE );
        return DEFAULT_PAGE_SIZE;
    }

    return static_cast<common::usize>( pageSize );
}

/*
================
Sys_PlatformVirtualReserve
================
*/
void *Sys_PlatformVirtualReserve( common::usize size )
{
    if ( size == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual reserve failed: requested size is zero." );
        return nullptr;
    }

    void *memory = mmap(
        nullptr,
        size,
        PROT_NONE,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0
    );

    if ( memory == MAP_FAILED ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual reserve failed: size=%zu, errno=%d.", size, errno );
        return nullptr;
    }

    return memory;
}

/*
================
Sys_PlatformVirtualCommit
================
*/
bool Sys_PlatformVirtualCommit( void *memory, common::usize size )
{
    if ( memory == nullptr || size == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual commit failed: memory=%p, size=%zu.", memory, size );
        return false;
    }

    const int result = mprotect( memory, size, PROT_READ | PROT_WRITE );
    if ( result != 0 ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual commit failed: memory=%p, size=%zu, errno=%d.", memory, size, errno );
        return false;
    }

    return true;
}

/*
================
Sys_PlatformVirtualDecommit
================
*/
bool Sys_PlatformVirtualDecommit( void *memory, common::usize size )
{
    if ( memory == nullptr || size == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual decommit failed: memory=%p, size=%zu.", memory, size );
        return false;
    }

    int result = madvise( memory, size, MADV_DONTNEED );
    if ( result != 0 ) {
        LOG_WARNING( log::channel_t::PLATFORM, "virtual decommit madvise warning: memory=%p, size=%zu, errno=%d.", memory, size, errno );
    }

    result = mprotect( memory, size, PROT_NONE );
    if ( result != 0 ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual decommit failed: memory=%p, size=%zu, errno=%d.", memory, size, errno );
        return false;
    }

    return true;
}

/*
================
Sys_PlatformVirtualRelease
================
*/
bool Sys_PlatformVirtualRelease( void *memory, common::usize size )
{
    if ( memory == nullptr || size == 0u ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual release failed: memory=%p, size=%zu.", memory, size );
        return false;
    }

    const int result = munmap( memory, size );
    if ( result != 0 ) {
        LOG_ERROR( log::channel_t::PLATFORM, "virtual release failed: memory=%p, size=%zu, errno=%d.", memory, size, errno );
        return false;
    }

    return true;
}

#endif          // CYPHER_PLATFORM_LINUX

}               // namespace

}               // namespace cypher::engine::sys
