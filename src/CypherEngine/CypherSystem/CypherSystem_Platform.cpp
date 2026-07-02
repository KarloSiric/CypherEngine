//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherSystem/CypherSystem_Platform.cpp
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
CypherSystem_CopyPath
================
*/
bool CypherSystem_CopyPath( char *szOutPath, const common::u32 nOutPathSize, const std::filesystem::path &path )
{
    if ( szOutPath == nullptr || nOutPathSize == 0u ) {
        return false;
    }

    std::string szPathString = path.lexically_normal().string();

    if ( szPathString.size() >= nOutPathSize ) {
        szOutPath[0] = '\0';
        return false;
    }

    std::strncpy( szOutPath, szPathString.c_str(), nOutPathSize - 1u );
    szOutPath[nOutPathSize - 1u] = '\0';

    return true;
}

/*
================
CypherSystem_FindArgvValue
================
*/
const char *CypherSystem_FindArgvValue( const init_info_t &info, const char *szArgvName )
{
    if ( info.argv == nullptr || szArgvName == nullptr ) {
        return nullptr;
    }

    for ( int i = 1; i + 1 < info.argc; ++i ) {
        if ( std::strcmp( info.argv[i], szArgvName ) == 0 ) {
            return info.argv[i + 1];
        }
    }

    return nullptr;
}

sys_error_t CypherSystem_PlatformBuildPaths( const init_info_t &infoInit, paths_t &pathsOut );
void CypherSystem_PlatformSleepMilliseconds( common::u64 milliseconds );
common::usize CypherSystem_PlatformVirtualPageSize();
void *CypherSystem_PlatformVirtualReserve( common::usize size );
bool CypherSystem_PlatformVirtualCommit( void *memory, common::usize size );
bool CypherSystem_PlatformVirtualDecommit( void *memory, common::usize size );
bool CypherSystem_PlatformVirtualRelease( void *memory, common::usize size );

/*
================
CypherSystem_AlignVirtualMemorySize

Rounds virtual-memory operation sizes up to the platform page size.
================
*/
common::usize CypherSystem_AlignVirtualMemorySize( const common::usize size )
{
    if ( size == 0u ) {
        return 0u;
    }

    const common::usize nPageSize = CypherSystem_PlatformVirtualPageSize();
    if ( nPageSize == 0u ) {
        return size;
    }

    const common::usize remainder = size % nPageSize;
    if ( remainder == 0u ) {
        return size;
    }

    return size + ( nPageSize - remainder );
}

}       // namespace

/*
================
CypherSystem_PlatformType
================
*/
platform_t CypherSystem_PlatformType() {
#   ifdef CYPHER_PLATFORM_WINDOWS
                return platform_t::WINDOWS;
#   elif defined( CYPHER_PLATFORM_MACOS )
                return platform_t::MACOSX;
#   elif defined( CYPHER_PLATFORM_LINUX )
                return platform_t::LINUX;
#   else
                return platform_t::UNKNOWN;
#   endif
}

/*
================
CypherSystem_CompilerType
================
*/
compiler_t CypherSystem_CompilerType() {
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
CypherSystem_Init

Copies startup info and builds platform paths.
================
*/
sys_error_t CypherSystem_Init( const init_info_t &infoInit ) {
    if ( s_SysRuntimeState.initialized ) {
        return sys_error_t::ERR_IS_INIT;
    }
    if ( infoInit.szAppName == nullptr || infoInit.szAppName[0] == '\0' ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( infoInit.szOrganizationName == nullptr || infoInit.szOrganizationName[0] == '\0' ) {
        return sys_error_t::ERR_INVALID_ARGUMENT;
    }
    s_SysRuntimeState = {};

    std::strncpy(
                 s_SysRuntimeState.szAppName,
                 infoInit.szAppName,
                 sizeof( s_SysRuntimeState.szAppName ) - 1u
    );

    std::strncpy(
                 s_SysRuntimeState.szOrganizationName,
                 infoInit.szOrganizationName,
                 sizeof( s_SysRuntimeState.szOrganizationName ) - 1u
    );

    s_SysRuntimeState.argc = infoInit.argc;
    s_SysRuntimeState.argv = infoInit.argv;

    sys_error_t pathsResult = CypherSystem_PlatformBuildPaths( infoInit, s_SysRuntimeState.sysPaths );

    if ( pathsResult != sys_error_t::OK ) {
        s_SysRuntimeState = {};
        return pathsResult;
    }

    s_SysRuntimeState.initialized = true;

    return sys_error_t::OK;
}

/*
================
CypherSystem_Shutdown
================
*/
sys_error_t CypherSystem_Shutdown() {
    if ( !s_SysRuntimeState.initialized ) {
        return sys_error_t::ERR_NOT_INIT;
    }

    s_SysRuntimeState = {};

    s_SysRuntimeState.initialized = false;

    return sys_error_t::OK;
}

/*
================
CypherSystem_IsInitialized
================
*/
bool CypherSystem_IsInitialized() {
    return s_SysRuntimeState.initialized;
}

/*
================
CypherSystem_PlatformName
================
*/
const char *CypherSystem_PlatformName( platform_t type ) {
    switch( type ) {
        case platform_t::WINDOWS:   return "Windows";
    case platform_t::LINUX:         return "Linux";
    case platform_t::MACOSX:        return "MacOS";
        default:                    return "Unknown";
    }
}

/*
================
CypherSystem_CompilerName
================
*/
const char *CypherSystem_CompilerName( compiler_t type ) {
    switch( type ) {
        case compiler_t::CLANG: return "Clang";
        case compiler_t::GCC:   return "GCC";
        case compiler_t::MSVC:  return "MSVC";
        default:                return "Unknown";
    }
}

/*
================
CypherSystem_PathBasename
================
*/
const char *CypherSystem_PathBasename( const char *path ) {
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
CypherSystem_TimeNowSeconds
================
*/
common::f64 CypherSystem_TimeNowSeconds() {
    const auto now = std::chrono::steady_clock::now();
    const auto seconds = std::chrono::duration<common::f64>( now.time_since_epoch() );
    return seconds.count();
}

/*
================
CypherSystem_LocalTime
================
*/
bool CypherSystem_LocalTime( std::time_t timeValue, std::tm &timeOut ) {

#   ifdef CYPHER_PLATFORM_WINDOWS
                return localtime_s( &timeOut, &timeValue ) == 0;
#   else
                return localtime_r( &timeValue, &timeOut ) != nullptr;
#   endif

}

/*
================
CypherSystem_Paths
================
*/
const paths_t &CypherSystem_Paths() {
    return s_SysRuntimeState.sysPaths;
}

/*
================
CypherSystem_GetPaths
================
*/
sys_error_t CypherSystem_GetPaths( paths_t &pathsOut ) {
    if ( !s_SysRuntimeState.initialized ) {
        return sys_error_t::ERR_NOT_INIT;
    }

    pathsOut = s_SysRuntimeState.sysPaths;

    return sys_error_t::OK;
}

/*
================
CypherSystem_SleepMilliseconds
================
*/
void CypherSystem_SleepMilliseconds( common::u64 milliseconds ) {
    CypherSystem_PlatformSleepMilliseconds( milliseconds );
    return ;
}

/*
================
CypherSystem_VirtualPageSize
================
*/
common::usize CypherSystem_VirtualPageSize()
{
    return CypherSystem_PlatformVirtualPageSize();
}

/*
================
CypherSystem_VirtualReserve
================
*/
void *CypherSystem_VirtualReserve( const common::usize size )
{
    return CypherSystem_PlatformVirtualReserve( CypherSystem_AlignVirtualMemorySize( size ) );
}

/*
================
CypherSystem_VirtualCommit
================
*/
sys_error_t CypherSystem_VirtualCommit( void *memory, common::usize size )
{
    if ( CypherSystem_PlatformVirtualCommit( memory, CypherSystem_AlignVirtualMemorySize( size ) ) ) {
        return sys_error_t::OK;
    }

    return sys_error_t::ERR_INTERNAL_ERROR;
}

/*
================
CypherSystem_VirtualDecommit
================
*/
sys_error_t CypherSystem_VirtualDecommit( void *memory, common::usize size )
{
    if ( CypherSystem_PlatformVirtualDecommit( memory, CypherSystem_AlignVirtualMemorySize( size ) ) ) {
        return sys_error_t::OK;
    }

    return sys_error_t::ERR_INTERNAL_ERROR;
}

/*
================
CypherSystem_VirtualRelease
================
*/
sys_error_t CypherSystem_VirtualRelease( void *memory, common::usize size )
{
    if ( CypherSystem_PlatformVirtualRelease( memory, CypherSystem_AlignVirtualMemorySize( size ) ) ) {
        return sys_error_t::OK;
    }

    return sys_error_t::ERR_INTERNAL_ERROR;
}

namespace {

#ifdef CYPHER_PLATFORM_WINDOWS

/*
================
CypherSystem_PlatformBuildPaths

Builds Win32 executable, base and user paths.
================
*/
sys_error_t CypherSystem_PlatformBuildPaths( const init_info_t &infoInit, paths_t &pathsOut )
{
    pathsOut = {};

    std::error_code ec{};

    const std::filesystem::path workingDir = std::filesystem::current_path( ec );
    if ( ec ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }

    char pExecutableBuffer[SYS_MAX_PATH_LENGTH]{};

    const DWORD nExecutableLength = GetModuleFileNameA(
        nullptr,
        pExecutableBuffer,
        static_cast<DWORD>( sizeof( pExecutableBuffer ) )
    );

    if ( nExecutableLength == 0u ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }

    if ( nExecutableLength >= sizeof( pExecutableBuffer ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    pExecutableBuffer[nExecutableLength] = '\0';

    std::filesystem::path szExecutablePath = std::filesystem::weakly_canonical( pExecutableBuffer, ec );

    if ( ec ) {
        ec.clear();
        szExecutablePath = pExecutableBuffer;
    }

    const std::filesystem::path executableDir = szExecutablePath.parent_path();
    const char *szBasePathOverride = CypherSystem_FindArgvValue( infoInit, "-basedir" );

    const std::filesystem::path szBasePath =
        ( szBasePathOverride != nullptr && szBasePathOverride[0] != '\0' ) ? std::filesystem::path( szBasePathOverride ) : workingDir;

    const char *szUserPathOverride = CypherSystem_FindArgvValue( infoInit, "-userpath" );
    std::filesystem::path szUserPath{};

    if ( szUserPathOverride != nullptr && szUserPathOverride[0] != '\0' ) {
        szUserPath = szUserPathOverride;
    } else {
        char pAppdataBuffer[SYS_MAX_PATH_LENGTH]{};

        const DWORD nAppdataLength = GetEnvironmentVariableA(
            "APPDATA",
            pAppdataBuffer,
            static_cast<DWORD>( sizeof( pAppdataBuffer ) )
        );

        if ( nAppdataLength == 0u ) {
            return sys_error_t::ERR_PATH_QUERY_FAILED;
        }

        if ( nAppdataLength >= sizeof( pAppdataBuffer ) ) {
            return sys_error_t::ERR_PATH_TOO_LONG;
        }

        pAppdataBuffer[nAppdataLength] = '\0';

        szUserPath = std::filesystem::path( pAppdataBuffer ) / infoInit.szAppName;
    }

    std::filesystem::create_directories( szUserPath, ec );
    if ( ec ) {
        return sys_error_t::ERR_DIRECTORY_CREATE_FAILED;
    }

    if ( !CypherSystem_CopyPath( pathsOut.szExecutablePath, sizeof( pathsOut.szExecutablePath ), szExecutablePath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !CypherSystem_CopyPath( pathsOut.executableDir, sizeof( pathsOut.executableDir ), executableDir ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !CypherSystem_CopyPath( pathsOut.workingDir, sizeof( pathsOut.workingDir ), workingDir ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !CypherSystem_CopyPath( pathsOut.szBasePath, sizeof( pathsOut.szBasePath ), szBasePath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !CypherSystem_CopyPath( pathsOut.szUserPath, sizeof( pathsOut.szUserPath ), szUserPath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    return sys_error_t::OK;
}

/*
================
CypherSystem_PlatformSleepMilliseconds
================
*/
void CypherSystem_PlatformSleepMilliseconds( common::u64 milliseconds )
{
    Sleep( static_cast<DWORD>( milliseconds ) );
}

/*
================
CypherSystem_PlatformVirtualPageSize
================
*/
common::usize CypherSystem_PlatformVirtualPageSize()
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
CypherSystem_PlatformVirtualReserve
================
*/
void *CypherSystem_PlatformVirtualReserve( common::usize size )
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
CypherSystem_PlatformVirtualCommit
================
*/
bool CypherSystem_PlatformVirtualCommit( void *memory, common::usize size )
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
CypherSystem_PlatformVirtualDecommit
================
*/
bool CypherSystem_PlatformVirtualDecommit( void *memory, common::usize size )
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
CypherSystem_PlatformVirtualRelease
================
*/
bool CypherSystem_PlatformVirtualRelease( void *memory, common::usize size )
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
CypherSystem_PlatformBuildPaths

Builds macOS executable, base and user paths.
================
*/
sys_error_t CypherSystem_PlatformBuildPaths( const init_info_t &infoInit, paths_t &pathsOut )
{
    pathsOut = {};

    std::error_code ec{};

    const std::filesystem::path workingDir = std::filesystem::current_path( ec );
    if ( ec ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }

    char pExecutableBuffer[SYS_MAX_PATH_LENGTH]{};
    std::uint32_t nExecutableBufferSize = static_cast<std::uint32_t>( sizeof( pExecutableBuffer ) );

    if ( _NSGetExecutablePath( pExecutableBuffer, &nExecutableBufferSize ) != 0 ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    std::filesystem::path szExecutablePath = std::filesystem::weakly_canonical( pExecutableBuffer, ec );

    if ( ec ) {
        ec.clear();
        szExecutablePath = pExecutableBuffer;
    }

    const std::filesystem::path executableDir = szExecutablePath.parent_path();
    const char *szBasePathOverride = CypherSystem_FindArgvValue( infoInit, "-basedir" );

    const std::filesystem::path szBasePath =
        ( szBasePathOverride != nullptr && szBasePathOverride[0] != '\0' ) ? std::filesystem::path( szBasePathOverride ) : workingDir;

    const char *szUserPathOverride = CypherSystem_FindArgvValue( infoInit, "-userpath" );

    std::filesystem::path szUserPath{};

    if ( szUserPathOverride != nullptr && szUserPathOverride[0] != '\0' ) {
        szUserPath = szUserPathOverride;
    } else {
        const char *home = std::getenv( "HOME" );

        if ( home == nullptr || home[0] == '\0' ) {
            return sys_error_t::ERR_PATH_QUERY_FAILED;
        }

        szUserPath = std::filesystem::path( home ) /
                    "Library" /
                    "Application Support" /
                    infoInit.szAppName;
    }

    std::filesystem::create_directories( szUserPath, ec );
    if ( ec ) {
        return sys_error_t::ERR_DIRECTORY_CREATE_FAILED;
    }

    if ( !CypherSystem_CopyPath( pathsOut.szExecutablePath, sizeof( pathsOut.szExecutablePath ), szExecutablePath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !CypherSystem_CopyPath( pathsOut.executableDir, sizeof( pathsOut.executableDir ), executableDir ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !CypherSystem_CopyPath( pathsOut.workingDir, sizeof( pathsOut.workingDir ), workingDir ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !CypherSystem_CopyPath( pathsOut.szBasePath, sizeof( pathsOut.szBasePath ), szBasePath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !CypherSystem_CopyPath( pathsOut.szUserPath, sizeof( pathsOut.szUserPath ), szUserPath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    return sys_error_t::OK;
}

/*
================
CypherSystem_PlatformSleepMilliseconds
================
*/
void CypherSystem_PlatformSleepMilliseconds( common::u64 milliseconds )
{
    timespec request{};
    request.tv_sec = static_cast<time_t>( milliseconds / 1000u );
    request.tv_nsec = static_cast<long>( ( milliseconds % 1000u ) * 1000000u );

    while ( nanosleep( &request, &request ) == -1 && errno == EINTR ) {
    }
}

/*
================
CypherSystem_PlatformVirtualPageSize
================
*/
common::usize CypherSystem_PlatformVirtualPageSize()
{
    constexpr common::usize DEFAULT_PAGE_SIZE = 4096u;

    const long nPageSize = sysconf( _SC_PAGESIZE );
    if ( nPageSize <= 0 ) {
        LOG_WARNING( log::channel_t::PLATFORM, "sysconf(_SC_PAGESIZE) failed; using default page size %zu.", DEFAULT_PAGE_SIZE );
        return DEFAULT_PAGE_SIZE;
    }

    return static_cast<common::usize>( nPageSize );
}

/*
================
CypherSystem_PlatformVirtualReserve
================
*/
void *CypherSystem_PlatformVirtualReserve( common::usize size )
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
CypherSystem_PlatformVirtualCommit
================
*/
bool CypherSystem_PlatformVirtualCommit( void *memory, common::usize size )
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
CypherSystem_PlatformVirtualDecommit
================
*/
bool CypherSystem_PlatformVirtualDecommit( void *memory, common::usize size )
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
CypherSystem_PlatformVirtualRelease
================
*/
bool CypherSystem_PlatformVirtualRelease( void *memory, common::usize size )
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
CypherSystem_PlatformBuildPaths

Builds Linux executable, base and user paths.
================
*/
sys_error_t CypherSystem_PlatformBuildPaths( const init_info_t &infoInit, paths_t &pathsOut )
{
    std::error_code ec{};
    pathsOut = {};

    const std::filesystem::path workingDir = std::filesystem::current_path( ec );
    if ( ec ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }

    char pExecutableBuffer[SYS_MAX_PATH_LENGTH]{};

    const ssize_t nExecutableLength = readlink(
        "/proc/self/exe",
        pExecutableBuffer,
        sizeof( pExecutableBuffer ) - 1u
    );

    if ( nExecutableLength < 0 ) {
        return sys_error_t::ERR_PATH_QUERY_FAILED;
    }

    pExecutableBuffer[nExecutableLength] = '\0';

    std::filesystem::path szExecutablePath = std::filesystem::weakly_canonical( pExecutableBuffer, ec );

    if ( ec ) {
        ec.clear();
        szExecutablePath = pExecutableBuffer;
    }

    const std::filesystem::path executableDir = szExecutablePath.parent_path();
    const char *szBasePathOverride = CypherSystem_FindArgvValue( infoInit, "-basedir" );

    const std::filesystem::path szBasePath =
        ( szBasePathOverride != nullptr && szBasePathOverride[0] != '\0' ) ? std::filesystem::path( szBasePathOverride ) : workingDir;

    const char *szUserPathOverride = CypherSystem_FindArgvValue( infoInit, "-userpath" );

    std::filesystem::path szUserPath{};

    if ( szUserPathOverride != nullptr && szUserPathOverride[0] != '\0' ) {
        szUserPath = szUserPathOverride;
    } else {
        const char *home = std::getenv( "HOME" );

        if ( home == nullptr || home[0] == '\0' ) {
            return sys_error_t::ERR_PATH_QUERY_FAILED;
        }

        szUserPath = std::filesystem::path( home ) /
                    ".local" /
                    "share" /
                    infoInit.szAppName;
    }

    std::filesystem::create_directories( szUserPath, ec );

    if ( ec ) {
        return sys_error_t::ERR_DIRECTORY_CREATE_FAILED;
    }

    if ( !CypherSystem_CopyPath( pathsOut.szExecutablePath, sizeof( pathsOut.szExecutablePath ), szExecutablePath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !CypherSystem_CopyPath( pathsOut.executableDir, sizeof( pathsOut.executableDir ), executableDir ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !CypherSystem_CopyPath( pathsOut.workingDir, sizeof( pathsOut.workingDir ), workingDir ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !CypherSystem_CopyPath( pathsOut.szBasePath, sizeof( pathsOut.szBasePath ), szBasePath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    if ( !CypherSystem_CopyPath( pathsOut.szUserPath, sizeof( pathsOut.szUserPath ), szUserPath ) ) {
        return sys_error_t::ERR_PATH_TOO_LONG;
    }

    return sys_error_t::OK;
}

/*
================
CypherSystem_PlatformSleepMilliseconds
================
*/
void CypherSystem_PlatformSleepMilliseconds( common::u64 milliseconds )
{
    timespec request{};
    request.tv_sec = static_cast<time_t>( milliseconds / 1000u );
    request.tv_nsec = static_cast<long>( ( milliseconds % 1000u ) * 1000000u );

    while ( nanosleep( &request, &request ) == -1 && errno == EINTR ) {
    }
}

/*
================
CypherSystem_PlatformVirtualPageSize
================
*/
common::usize CypherSystem_PlatformVirtualPageSize()
{
    constexpr common::usize DEFAULT_PAGE_SIZE = 4096u;
    const long nPageSize = sysconf( _SC_PAGESIZE );

    if ( nPageSize <= 0 ) {
        LOG_WARNING( log::channel_t::PLATFORM, "sysconf(_SC_PAGESIZE) failed; using default page size %zu.", DEFAULT_PAGE_SIZE );
        return DEFAULT_PAGE_SIZE;
    }

    return static_cast<common::usize>( nPageSize );
}

/*
================
CypherSystem_PlatformVirtualReserve
================
*/
void *CypherSystem_PlatformVirtualReserve( common::usize size )
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
CypherSystem_PlatformVirtualCommit
================
*/
bool CypherSystem_PlatformVirtualCommit( void *memory, common::usize size )
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
CypherSystem_PlatformVirtualDecommit
================
*/
bool CypherSystem_PlatformVirtualDecommit( void *memory, common::usize size )
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
CypherSystem_PlatformVirtualRelease
================
*/
bool CypherSystem_PlatformVirtualRelease( void *memory, common::usize size )
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
