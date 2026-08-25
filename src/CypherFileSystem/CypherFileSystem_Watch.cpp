//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherFileSystem/CypherFileSystem_Watch.cpp
//  Purpose: Implements the CypherFileSystem FileSystem Watch module.
//  Details: This file participates in the virtual filesystem layer that maps engine
//           paths to mounted physical or package-backed data. Keep path validation
//           strict because every asset pipeline will depend on it.
//
//  History:
//  - Created by Karlo Siric on 2026-06-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherFileSystem.h"
#include "CypherFileSystem_Runtime.h"
#include "CypherSystem_Platform.h"

#include <algorithm>        // std::sort for deterministic snapshot comparison.
#include <cstddef>          // offsetof
#include <cstring>          // memcpy etc...
#include <chrono>           // for system_clock etc...
#include <string>           // for std::string etc...
#include <filesystem>       // for std::filesystem::exists and etc...
#include <system_error>     // for error_codes etc...

// @Windows Including and Checking
#if defined( CYPHER_PLATFORM_WINDOWS )
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined( CYPHER_PLATFORM_LINUX )
#include <cerrno>
#include <sys/inotify.h>
#include <unistd.h>
#elif defined( CYPHER_PLATFORM_MACOS )
#include <cerrno>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace cypher::engine::fs
{

namespace {

#if defined( CYPHER_PLATFORM_WINDOWS )

struct windows_watch_t {
    bool used{ false };                                      // is this current watch in use or not.
    HANDLE hDirectory{ INVALID_HANDLE_VALUE };                        // Windows OS directory handle for kernel/user object
    HANDLE hEvent{ nullptr };                            // native Windows OS event handle reference for kernel/
                                                    // user object
    OVERLAPPED overlapped{};                          //
    alignas( std::max_align_t ) unsigned char buffer[64u * 1024u]{}; // buffer into which the changes will be written
    bool bReadPending{ false };                     // one currently active ReadDirectoryChangesW
    bool bWatchFile{ false };

    char szWatchPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szWatchVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szFileFilter[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};

    char szPendingRenameOldVirtual[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    bool bPendingRename{ false };
};

static windows_watch_t s_WindowsWatches[CYPHER_FILESYSTEM_MAX_WATCHES]{};

#elif defined( CYPHER_PLATFORM_LINUX )

constexpr common::u32 LINUX_MAX_WATCH_DIRS = CYPHER_FILESYSTEM_MAX_WATCH_SNAPSHOT_ENTRIES;

struct linux_watch_dir_t {
    int nWatchDescriptor{ -1 };
    char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
};

struct linux_watch_t {
    bool used{ false };
    int fd{ -1 };
    bool bWatchFile{ false };
    bool recursive{ false };
    alignas( std::max_align_t ) unsigned char buffer[64u * 1024u]{};

    linux_watch_dir_t dirs[LINUX_MAX_WATCH_DIRS]{};
    common::u32 nDirCount{ 0u };

    char szFileFilter[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szPendingRenameOldVirtual[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    common::u32 nPendingRenameCookie{ 0u };
    bool bPendingRename{ false };
};

static linux_watch_t s_LinuxWatches[CYPHER_FILESYSTEM_MAX_WATCHES]{};

#elif defined( CYPHER_PLATFORM_MACOS )

struct macos_watch_t {
    bool used{ false };
    int nKqueueFd{ -1 };
    int nWatchedFd{ -1 };
    bool bWatchFile{ false };

    char szWatchPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szWatchVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szFileFilter[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
};

static macos_watch_t s_MacosWatches[CYPHER_FILESYSTEM_MAX_WATCHES]{};

#endif

static fs_error_t ResolveWatchPhysicalPath( runtime_state_t &state,
                                            const char *szVirtualPath,
                                            char *szOutNormalizedPath,
                                            common::u32 nOutNormalizedPathSize,
                                            char *szOutPhysicalPath,
                                            common::u32 nOutPhysicalPathSize,
                                            bool &bOutIsDirectory )
{
    bOutIsDirectory = false;
    if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }
    if ( szOutNormalizedPath == nullptr || nOutNormalizedPathSize == 0u || szOutPhysicalPath == nullptr || nOutPhysicalPathSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    szOutNormalizedPath[0] = '\0';
    szOutPhysicalPath[0] = '\0';

    fs_error_t result = FS_NormalizeVirtualPath( szVirtualPath, szOutNormalizedPath, nOutNormalizedPathSize );
    if ( result != fs_error_t::OK ) {
        return result;
    }
    for ( common::u32 i = 0; i < state.nMountCount; ++i ) {
        const mount_t &mount = state.mounts[i];
        if ( mount.type != mount_type_t::CYPHER_FILESYSTEM_DIRECTORY ) {
            continue;
        }
        const char *szRelativePath = nullptr;
        if ( !FS_VirtualPathStartsWithRoot( szOutNormalizedPath, mount.szVirtualRoot, &szRelativePath ) ) {
            continue;
        }
        if ( szRelativePath == nullptr ) {
            continue;
        }
        char szPossiblePath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
        result = FS_BuildPhysicalPath( mount.szPhysicalRoot, szRelativePath, szPossiblePath, sizeof( szPossiblePath ) );
        if ( result != fs_error_t::OK ) {
            return result;
        }
        std::error_code ec{};
        if ( !std::filesystem::exists( szPossiblePath, ec ) ) {
            if ( ec ) {
                return fs_error_t::ERR_IO_ERROR;
            }
            continue;
        }
        const bool bIsDirectory = std::filesystem::is_directory( szPossiblePath, ec );
        if ( ec ) {
            return fs_error_t::ERR_IO_ERROR;
        }
        const common::usize nPossiblePathLen = std::strlen( szPossiblePath );
        if ( nPossiblePathLen + 1u > nOutPhysicalPathSize ) {
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }
        std::memcpy( szOutPhysicalPath, szPossiblePath, nPossiblePathLen + 1u );
        bOutIsDirectory = bIsDirectory;
        return fs_error_t::OK;
    }
    return fs_error_t::ERR_PATH_NOT_FOUND;
}       // ResolveWatchPhysicalPath

static fs_error_t ValidateWatchFlags( common::u32 flags, bool bIsDirectory )
{
    const common::u32 bAllowedFlags = CYPHER_FILESYSTEM_WATCH_FILE |
                                      CYPHER_FILESYSTEM_WATCH_DIRECTORY |
                                      CYPHER_FILESYSTEM_WATCH_RECURSIVE;
    if ( ( flags & ~bAllowedFlags ) != 0 ) {
        return fs_error_t::ERR_INVALID_FLAGS;
    }
    const bool bWantsFile = ( flags & CYPHER_FILESYSTEM_WATCH_FILE ) != 0u;
    const bool bWantsDirectory = ( flags & CYPHER_FILESYSTEM_WATCH_DIRECTORY ) != 0u;
    const bool bWantsRecursive = ( flags & CYPHER_FILESYSTEM_WATCH_RECURSIVE ) != 0u;
    if ( !bWantsFile && !bWantsDirectory ) {
        return fs_error_t::ERR_INVALID_FLAGS;
    }
    if ( bWantsFile && bWantsDirectory ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( bWantsFile && bIsDirectory ) {
        return fs_error_t::ERR_NOT_FILE;
    }
    if ( bWantsDirectory && !bIsDirectory ) {
        return fs_error_t::ERR_NOT_DIRECTORY;
    }
    if ( bWantsRecursive && !bIsDirectory ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    return fs_error_t::OK;
}       // ValidateWatchFlags

static watch_handle_t AllocateWatchHandle( runtime_state_t &state )
{
    watch_handle_t handle = state.nNextWatchHandle++;
    if ( handle == CYPHER_FILESYSTEM_INVALID_WATCH ) {
        handle = state.nNextWatchHandle++;
    }
    return handle;
}       // AllocateWatchHandle

static void ResetWatch( watch_t &watch )
{
    std::memset( &watch, 0, sizeof( watch ) );
}       // ResetWatch

static bool CopyString( char *out, common::u32 nOutSize, const char *text )
{
    if ( out == nullptr || nOutSize == 0u || text == nullptr ) {
        return false;
    }
    const common::usize len = std::strlen( text );
    if ( len + 1u > nOutSize ) {
        out[0] = '\0';
        return false;
    }
    std::memcpy( out, text, len + 1u );
    return true;
}       // CopyString

static bool WatchSnapshotEntryLess( const watch_snapshot_entry_t &left, const watch_snapshot_entry_t &right )
{
    return std::strcmp( left.szVirtualPath, right.szVirtualPath ) < 0;
}       // WatchSnapshotEntryLess

static void SortWatchSnapshot( watch_t &watch )
{
    std::sort( watch.snapshot, watch.snapshot + watch.nSnapshotCount, WatchSnapshotEntryLess );
}       // SortWatchSnapshot

static bool WatchSnapshotMetadataChanged(
    const watch_snapshot_entry_t &oldEntry,
    const watch_snapshot_entry_t &newEntry )
{
    return oldEntry.size != newEntry.size ||
           oldEntry.nModifiedTime != newEntry.nModifiedTime ||
           oldEntry.bIsDirectory != newEntry.bIsDirectory;
}       // WatchSnapshotMetadataChanged

static fs_error_t EmitWatchEvent(
    watch_event_t *events,
    common::u32 nMaxEvents,
    common::u32 &nOutEventCount,
    watch_event_type_t type,
    const char *szVirtualPath,
    const char *szOldVirtualPath )
{
    if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }

    const char *szOldPath = ( szOldVirtualPath != nullptr ) ? szOldVirtualPath : "";
    for ( common::u32 i = 0u; i < nOutEventCount; ++i ) {
        const watch_event_t &event = events[i];
        if ( event.type == type &&
             std::strcmp( event.szVirtualPath, szVirtualPath ) == 0 &&
             std::strcmp( event.szOldVirtualPath, szOldPath ) == 0 )
        {
            return fs_error_t::OK;
        }
    }

    if ( nOutEventCount >= nMaxEvents ) {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    watch_event_t &event = events[nOutEventCount];
    event = {};
    event.type = type;

    if ( !CopyString( event.szVirtualPath, sizeof( event.szVirtualPath ), szVirtualPath ) ) {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    if ( szOldVirtualPath != nullptr && szOldVirtualPath[0] != '\0' ) {
        if ( !CopyString( event.szOldVirtualPath, sizeof( event.szOldVirtualPath ), szOldVirtualPath ) ) {
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }
    } else {
        event.szOldVirtualPath[0] = '\0';
    }

    ++nOutEventCount;
    return fs_error_t::OK;
}       // EmitWatchEvent

static void CopyWatchSnapshot( watch_t &dst, const watch_t &src )
{
    dst.nSnapshotCount = src.nSnapshotCount;
    for ( common::u32 snapshotIdx = 0; snapshotIdx < src.nSnapshotCount; ++snapshotIdx ) {
        dst.snapshot[snapshotIdx] = src.snapshot[snapshotIdx];
    }
}       // CopyWatchSnapshot

static fs_error_t BuildWatchEventVirtualPath(
    const char *pWatchVirtualBase,
    const char *szRelativePath,
    char *szOutVirtualPath,
    common::u32 nOutVirtualPathSize )
{
    if ( szOutVirtualPath == nullptr || nOutVirtualPathSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    szOutVirtualPath[0] = '\0';

    if ( pWatchVirtualBase == nullptr || szRelativePath == nullptr ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }

    if ( szRelativePath[0] == '\0' ) {
        if ( pWatchVirtualBase[0] == '\0' ) {
            return fs_error_t::ERR_INVALID_PATH;
        }
        return FS_NormalizeVirtualPath( pWatchVirtualBase, szOutVirtualPath, nOutVirtualPathSize );
    }

    if ( pWatchVirtualBase[0] == '\0' ) {
        return FS_NormalizeVirtualPath( szRelativePath, szOutVirtualPath, nOutVirtualPathSize );
    }

    return FS_PathJoin( pWatchVirtualBase, szRelativePath, szOutVirtualPath, nOutVirtualPathSize );
}       // BuildWatchEventVirtualPath

static fs_error_t DiffWatchSnapshots(
    watch_t &oldWatch,
    watch_t &newWatch,
    watch_event_t *events,
    common::u32 nMaxEvents,
    common::u32 &nOutEventCount )
{
    common::u32 oldSnapshotIdx = 0u;
    common::u32 newSnapshotIdx = 0u;

    while ( oldSnapshotIdx < oldWatch.nSnapshotCount ||
            newSnapshotIdx < newWatch.nSnapshotCount ) {
        if ( oldSnapshotIdx >= oldWatch.nSnapshotCount ) {
            const watch_snapshot_entry_t &newEntry = newWatch.snapshot[newSnapshotIdx];
            const fs_error_t result = EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::CREATED, newEntry.szVirtualPath, nullptr );
            if ( result != fs_error_t::OK ) {
                return result;
            }
            ++newSnapshotIdx;
            continue;
        }

        if ( newSnapshotIdx >= newWatch.nSnapshotCount ) {
            const watch_snapshot_entry_t &oldEntry = oldWatch.snapshot[oldSnapshotIdx];
            const fs_error_t result = EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::DELETED, oldEntry.szVirtualPath, nullptr );
            if ( result != fs_error_t::OK ) {
                return result;
            }
            ++oldSnapshotIdx;
            continue;
        }

        const watch_snapshot_entry_t &oldEntry = oldWatch.snapshot[oldSnapshotIdx];
        const watch_snapshot_entry_t &newEntry = newWatch.snapshot[newSnapshotIdx];
        const int szPathCompare = std::strcmp( oldEntry.szVirtualPath, newEntry.szVirtualPath );

        if ( szPathCompare == 0 ) {
            if ( WatchSnapshotMetadataChanged( oldEntry, newEntry ) ) {
                const fs_error_t result = EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::MODIFIED, newEntry.szVirtualPath, nullptr );
                if ( result != fs_error_t::OK ) {
                    return result;
                }
            }
            ++oldSnapshotIdx;
            ++newSnapshotIdx;
            continue;
        }

        if ( szPathCompare < 0 ) {
            const fs_error_t result = EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::DELETED, oldEntry.szVirtualPath, nullptr );
            if ( result != fs_error_t::OK ) {
                return result;
            }
            ++oldSnapshotIdx;
        } else {
            const fs_error_t result = EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::CREATED, newEntry.szVirtualPath, nullptr );
            if ( result != fs_error_t::OK ) {
                return result;
            }
            ++newSnapshotIdx;
        }
    }

    CopyWatchSnapshot( oldWatch, newWatch );
    return fs_error_t::OK;
}       // DiffWatchSnapshots

fs_error_t BuildWatchSnapshot( watch_t &watch )
{
    watch.nSnapshotCount = 0u;
    const bool bWatchDirectory  = ( ( watch.flags & CYPHER_FILESYSTEM_WATCH_DIRECTORY ) != 0u );
    const bool bWatchFile       = ( ( watch.flags & CYPHER_FILESYSTEM_WATCH_FILE ) != 0u );
    const bool bWatchRecursive  = ( ( watch.flags & CYPHER_FILESYSTEM_WATCH_RECURSIVE ) != 0u );
    // @NOTE: Writing lambda function instead of using too many helpers.
    auto addEntry = [&]( const char *szVirtualPath, const std::filesystem::path &szPhysicalPath ) -> fs_error_t {
        if ( watch.nSnapshotCount >= CYPHER_FILESYSTEM_MAX_WATCH_SNAPSHOT_ENTRIES ) {
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }
        watch_snapshot_entry_t &entry = watch.snapshot[watch.nSnapshotCount];
        entry = {};
        if ( !CopyString( entry.szVirtualPath, sizeof( entry.szVirtualPath ), szVirtualPath ) ||
             !CopyString( entry.szPhysicalPath, sizeof( entry.szPhysicalPath ), szPhysicalPath.string().c_str() ) )
        {
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }
        std::error_code ec{};
        entry.exists = std::filesystem::exists( entry.szPhysicalPath, ec );
        if ( ec ) {
            return fs_error_t::ERR_IO_ERROR;
        }
        if ( entry.exists ) {
            entry.bIsDirectory = std::filesystem::is_directory( szPhysicalPath, ec );
            if ( ec ) {
                return fs_error_t::ERR_IO_ERROR;
            }
            if ( !entry.bIsDirectory ) {
                entry.size = static_cast<common::u64>( std::filesystem::file_size( szPhysicalPath, ec ) );
                if ( ec ) {
                    return fs_error_t::ERR_IO_ERROR;
                }

            }
            const auto bFileTime = std::filesystem::last_write_time( szPhysicalPath, ec );
            if ( !ec ) {
                const auto nSystemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>( bFileTime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now() );
                entry.nModifiedTime = std::chrono::system_clock::to_time_t( nSystemTime );
            }
        }
        ++watch.nSnapshotCount;
        return fs_error_t::OK;
    };      // Lambda Function
    fs_error_t result = addEntry( watch.szVirtualPath, watch.szPhysicalPath );
    std::error_code ec{};
    if ( result != fs_error_t::OK ) {
        return result;
    }
    if ( bWatchFile ) {
        SortWatchSnapshot( watch );
        return fs_error_t::OK;
    }
    if ( bWatchDirectory && !bWatchRecursive ) {
        // @NOTE-> otherwise continue scanning directory contents further more
        // This is non recursive first, recursive is later!
        for ( const std::filesystem::directory_entry &directory_entry : std::filesystem::directory_iterator( watch.szPhysicalPath, ec ) )
        {
            if ( ec ) {
                return fs_error_t::ERR_IO_ERROR;
            }
            // directory_entry.path() is the physical child path of the provided parent path
            std::string szChildName = directory_entry.path().filename().generic_string();
            char szChildVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
            result = FS_PathJoin( watch.szVirtualPath, szChildName.c_str(), szChildVirtualPath, sizeof( szChildVirtualPath ) );
            if ( result != fs_error_t::OK ) {
                return result;
            }
            result = addEntry( szChildVirtualPath, directory_entry.path() );
            if ( result != fs_error_t::OK ) {
                return result;
            }
        }
    }
    // @NOTE: Using recursive mode for watching things here.
    else if ( bWatchDirectory && bWatchRecursive ) {
        // recursive watching now!
        for ( const std::filesystem::directory_entry &directory_entry : std::filesystem::recursive_directory_iterator( watch.szPhysicalPath, ec ) ) {
            if ( ec ) {
                return fs_error_t::ERR_IO_ERROR;
            }
            // @note: Now we need to compute the so called relative_path because there might be more folders with recursive ~ Karlo 15.6.2026
            std::filesystem::path szRelativePath = std::filesystem::relative( directory_entry.path(), watch.szPhysicalPath, ec );
            if ( ec ) {
                return fs_error_t::ERR_IO_ERROR;
            }
            std::string relativeString = szRelativePath.generic_string();
            char szChildVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
            result = FS_PathJoin( watch.szVirtualPath, relativeString.c_str(), szChildVirtualPath, sizeof( szChildVirtualPath ) );
            if ( result != fs_error_t::OK ) {
                return result;
            }
            result = addEntry( szChildVirtualPath, directory_entry.path() );
            if ( result != fs_error_t::OK ) {
                return result;
            }
        }
    }
    SortWatchSnapshot( watch );
    return fs_error_t::OK;
}

static fs_error_t BuildFreshWatchSnapshot( const watch_t &watch, watch_t &watchOut )
{
    ResetWatch( watchOut );
    watchOut.handle = watch.handle;
    watchOut.flags = watch.flags;
    if ( !CopyString( watchOut.szVirtualPath, sizeof( watchOut.szVirtualPath ), watch.szVirtualPath ) ||
         !CopyString( watchOut.szPhysicalPath, sizeof( watchOut.szPhysicalPath ), watch.szPhysicalPath ) )
    {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    return BuildWatchSnapshot( watchOut );
}

// @WindowsAPI Implementations ~ Karlo 17.06.2026
#if defined( CYPHER_PLATFORM_WINDOWS )

static void WindowsResetNativeWatch( windows_watch_t &winWatch )
{
    winWatch                   = {};
    winWatch.hDirectory  = INVALID_HANDLE_VALUE;
    winWatch.hEvent      = nullptr;
}

static windows_watch_t *WindowsAllocateNativeWatch()
{
    for ( common::u32 i = 0; i < CYPHER_FILESYSTEM_MAX_WATCHES; ++i )
    {
        windows_watch_t &winWatch = s_WindowsWatches[i];
        if ( !winWatch.used ) {
            WindowsResetNativeWatch( winWatch );
            winWatch.used = true;
            return &winWatch;
        }
    }
    return nullptr;
}

static windows_watch_t *WindowsGetNativeWatch( watch_t &watch )
{
    return static_cast<windows_watch_t *>( watch.pNativeHandle );
}

static fs_error_t WindowsUtf8ToWide( const char *text, wchar_t *out, common::u32 nOutSize )
{
    if ( text == nullptr || out == nullptr || nOutSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    const int result = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, static_cast<int>( nOutSize ) );
    if ( result == 0 ) {
        return fs_error_t::ERR_INVALID_PATH;
    }

    return fs_error_t::OK;
}

static void WindowsFinishPendingRead( windows_watch_t &winWatch )
{
    if ( !winWatch.bReadPending || winWatch.hDirectory == INVALID_HANDLE_VALUE ) {
        return;
    }

    const BOOL cancelOk = CancelIoEx( winWatch.hDirectory, &winWatch.overlapped );
    DWORD nBytesIgnored = 0u;

    if ( cancelOk ) {
        GetOverlappedResult( winWatch.hDirectory, &winWatch.overlapped, &nBytesIgnored, TRUE );
    } else if ( GetLastError() == ERROR_NOT_FOUND ) {
        GetOverlappedResult( winWatch.hDirectory, &winWatch.overlapped, &nBytesIgnored, FALSE );
    }

    winWatch.bReadPending = false;
}

static void WindowsDestroyNativeWatch( watch_t &watch )
{
    windows_watch_t *winWatch = WindowsGetNativeWatch( watch );
    if ( winWatch == nullptr ) {
        return ;
    }
    WindowsFinishPendingRead( *winWatch );
    if ( winWatch->hEvent != nullptr ) {
        CloseHandle( winWatch->hEvent );
    }
    if ( winWatch->hDirectory != INVALID_HANDLE_VALUE ) {
        CloseHandle( winWatch->hDirectory );
    }
    WindowsResetNativeWatch( *winWatch );
    watch.pNativeHandle = nullptr;
    return ;
}

static fs_error_t WindowsArmNativeWatch( watch_t &watch )
{
    windows_watch_t *winWatch = WindowsGetNativeWatch( watch );
    if ( winWatch == nullptr || !winWatch->used ) {
        return fs_error_t::ERR_INVALID_HANDLE;
    }
    if ( winWatch->bReadPending ) {
        return fs_error_t::OK;
    }

    std::memset( &winWatch->overlapped, 0, sizeof( winWatch->overlapped ) );
    std::memset( winWatch->buffer, 0, sizeof( winWatch->buffer ) );

    ResetEvent( winWatch->hEvent );
    winWatch->overlapped.hEvent = winWatch->hEvent;

    const BOOL recursive = ( watch.flags & CYPHER_FILESYSTEM_WATCH_RECURSIVE ) != 0u ? TRUE : FALSE;
    const DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME   |
                         FILE_NOTIFY_CHANGE_DIR_NAME    |
                         FILE_NOTIFY_CHANGE_ATTRIBUTES  |
                         FILE_NOTIFY_CHANGE_SIZE        |
                         FILE_NOTIFY_CHANGE_LAST_WRITE  |
                         FILE_NOTIFY_CHANGE_CREATION;
    const BOOL ok = ReadDirectoryChangesW(
                    winWatch->hDirectory,
                    winWatch->buffer,
                    sizeof( winWatch->buffer ),
                    recursive,
                    filter,
                    nullptr,
                    &winWatch->overlapped,
                    nullptr
                    );
    if ( !ok ) {
        const DWORD error = GetLastError();
        if ( error != ERROR_IO_PENDING ) {
            return fs_error_t::ERR_IO_ERROR;
        }
    }
    winWatch->bReadPending = true;
    return fs_error_t::OK;
}

static fs_error_t WindowsCreateNativeWatch( watch_t &watch )
{
    windows_watch_t *winWatch = WindowsAllocateNativeWatch();
    if ( winWatch == nullptr ) {
        return fs_error_t::ERR_TOO_MANY_WATCHES;
    }
    watch.pNativeHandle = winWatch;

    const bool bWatchIsFile = ( watch.flags & CYPHER_FILESYSTEM_WATCH_FILE ) != 0u;
    const bool bWatchIsDirectory = ( watch.flags & CYPHER_FILESYSTEM_WATCH_DIRECTORY ) != 0u;

    if ( bWatchIsFile ) {
        std::filesystem::path szPhysicalPath( watch.szPhysicalPath );
        std::string physicalParent = szPhysicalPath.parent_path().string();
        if ( !CopyString( winWatch->szWatchPhysicalPath, sizeof( winWatch->szWatchPhysicalPath ), physicalParent.c_str() ) ) {
            WindowsDestroyNativeWatch( watch );
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }
        fs_error_t dirnameResult = FS_PathDirname( watch.szVirtualPath, winWatch->szWatchVirtualPath, sizeof( winWatch->szWatchVirtualPath ) );
        if ( dirnameResult != fs_error_t::OK ) {
            WindowsDestroyNativeWatch( watch );
            return dirnameResult;
        }
        const char *basename = FS_PathBasename( watch.szVirtualPath );
        if ( basename == nullptr || basename[0] == '\0' ) {
            WindowsDestroyNativeWatch( watch );
            return fs_error_t::ERR_INVALID_PATH;
        }
        if ( !CopyString( winWatch->szFileFilter, sizeof( winWatch->szFileFilter ), basename ) ) {
            WindowsDestroyNativeWatch( watch );
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }
        winWatch->bWatchFile = true;
    } else if ( bWatchIsDirectory ) {
        if ( !CopyString( winWatch->szWatchPhysicalPath, sizeof( winWatch->szWatchPhysicalPath ), watch.szPhysicalPath ) ||
             !CopyString( winWatch->szWatchVirtualPath, sizeof( winWatch->szWatchVirtualPath ), watch.szVirtualPath ) ) {
            WindowsDestroyNativeWatch( watch );
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }
        winWatch->szFileFilter[0] = '\0';
        winWatch->bWatchFile = false;
    } else {
        WindowsDestroyNativeWatch( watch );
        return fs_error_t::ERR_INVALID_FLAGS;
    }

    wchar_t szWidePath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    fs_error_t wideResult = WindowsUtf8ToWide(
        winWatch->szWatchPhysicalPath,
        szWidePath,
        static_cast<common::u32>( sizeof( szWidePath ) / sizeof( szWidePath[0] ) ) );

    if ( wideResult != fs_error_t::OK ) {
        WindowsDestroyNativeWatch( watch );
        return wideResult;
    }

    winWatch->hDirectory = CreateFileW(
        szWidePath,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr );

    if ( winWatch->hDirectory == INVALID_HANDLE_VALUE ) {
        WindowsDestroyNativeWatch( watch );
        return fs_error_t::ERR_IO_ERROR;
    }

    winWatch->hEvent = CreateEventW( nullptr, TRUE, FALSE, nullptr );
    if ( winWatch->hEvent == nullptr ) {
        WindowsDestroyNativeWatch( watch );
        return fs_error_t::ERR_IO_ERROR;
    }

    fs_error_t armResult = WindowsArmNativeWatch( watch );
    if ( armResult != fs_error_t::OK ) {
        WindowsDestroyNativeWatch( watch );
        return armResult;
    }

    return fs_error_t::OK;
}

static fs_error_t WindowsWideSpanToUtf8(
    const wchar_t *text,
    int nTextCount,
    char *out,
    common::u32 nOutSize )
{
    if ( text == nullptr || nTextCount < 0 || out == nullptr || nOutSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    out[0] = '\0';

    const int result = WideCharToMultiByte(
        CP_UTF8,
        0,
        text,
        nTextCount,
        out,
        static_cast<int>( nOutSize - 1u ),
        nullptr,
        nullptr );

    if ( result <= 0 ) {
        return fs_error_t::ERR_INVALID_PATH;
    }

    out[result] = '\0';
    return fs_error_t::OK;
}

static fs_error_t WindowsBuildEventVirtualPath(
    const windows_watch_t &winWatch,
    const char *szRelativePath,
    char *szOutVirtualPath,
    common::u32 nOutVirtualPathSize,
    bool &shouldEmitOut )
{
    shouldEmitOut = false;
    if ( szRelativePath == nullptr || szOutVirtualPath == nullptr || nOutVirtualPathSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }

    char normalizedRelative[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    fs_error_t result = FS_NormalizeVirtualPath( szRelativePath, normalizedRelative, sizeof( normalizedRelative ) );
    if ( result != fs_error_t::OK ) {
        return result;
    }

    if ( winWatch.bWatchFile && std::strcmp( normalizedRelative, winWatch.szFileFilter ) != 0 ) {
        return fs_error_t::OK;
    }

    result = BuildWatchEventVirtualPath(
        winWatch.szWatchVirtualPath,
        normalizedRelative,
        szOutVirtualPath,
        nOutVirtualPathSize );

    if ( result != fs_error_t::OK ) {
        return result;
    }

    shouldEmitOut = true;
    return fs_error_t::OK;
}

static fs_error_t WindowsEmitNativeAction(
    windows_watch_t &winWatch,
    DWORD action,
    const char *szRelativePath,
    watch_event_t *events,
    common::u32 nMaxEvents,
    common::u32 &nOutEventCount )
{
    char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    bool bShouldEmit = false;
    fs_error_t result = WindowsBuildEventVirtualPath(
        winWatch,
        szRelativePath,
        szVirtualPath,
        sizeof( szVirtualPath ),
        bShouldEmit );

    if ( result != fs_error_t::OK ) {
        return result;
    }

    switch ( action ) {
    case FILE_ACTION_ADDED:
        return bShouldEmit ? EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::CREATED, szVirtualPath, nullptr ) : fs_error_t::OK;
    case FILE_ACTION_REMOVED:
        return bShouldEmit ? EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::DELETED, szVirtualPath, nullptr ) : fs_error_t::OK;
    case FILE_ACTION_MODIFIED:
        return bShouldEmit ? EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::MODIFIED, szVirtualPath, nullptr ) : fs_error_t::OK;
    case FILE_ACTION_RENAMED_OLD_NAME:
        if ( bShouldEmit ) {
            if ( !CopyString( winWatch.szPendingRenameOldVirtual, sizeof( winWatch.szPendingRenameOldVirtual ), szVirtualPath ) ) {
                return fs_error_t::ERR_BUFFER_TOO_SMALL;
            }
            winWatch.bPendingRename = true;
        } else {
            winWatch.szPendingRenameOldVirtual[0] = '\0';
            winWatch.bPendingRename = false;
        }
        return fs_error_t::OK;
    case FILE_ACTION_RENAMED_NEW_NAME:
        if ( winWatch.bPendingRename && bShouldEmit ) {
            result = EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::RENAMED, szVirtualPath, winWatch.szPendingRenameOldVirtual );
        } else if ( winWatch.bPendingRename && !bShouldEmit ) {
            result = EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::DELETED, winWatch.szPendingRenameOldVirtual, nullptr );
        } else if ( !winWatch.bPendingRename && bShouldEmit ) {
            result = EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::CREATED, szVirtualPath, nullptr );
        }
        winWatch.szPendingRenameOldVirtual[0] = '\0';
        winWatch.bPendingRename = false;
        return result;
    default:
        return fs_error_t::OK;
    }
}

static fs_error_t WindowsParseNativeEvents(
    watch_t &watch,
    common::u32 nBytesReturned,
    watch_event_t *events,
    common::u32 nMaxEvents,
    common::u32 &nOutEventCount )
{
    windows_watch_t *winWatch = WindowsGetNativeWatch( watch );
    if ( winWatch == nullptr || !winWatch->used ) {
        return fs_error_t::ERR_INVALID_HANDLE;
    }

    constexpr common::u32 nHeaderSize = static_cast<common::u32>( offsetof( FILE_NOTIFY_INFORMATION, FileName ) );
    common::u32 nOffset = 0u;
    while ( nOffset < nBytesReturned ) {
        if ( nBytesReturned - nOffset < nHeaderSize ) {
            return fs_error_t::ERR_IO_ERROR;
        }

        FILE_NOTIFY_INFORMATION info{};
        std::memcpy( &info, winWatch->buffer + nOffset, nHeaderSize );

        if ( ( info.FileNameLength % sizeof( wchar_t ) ) != 0u ) {
            return fs_error_t::ERR_IO_ERROR;
        }
        if ( info.FileNameLength > nBytesReturned - nOffset - nHeaderSize ) {
            return fs_error_t::ERR_IO_ERROR;
        }

        const wchar_t *pFileName =
            reinterpret_cast<const wchar_t *>( winWatch->buffer + nOffset + nHeaderSize );

        char szRelativePath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
        const int nWcharCount = static_cast<int>( info.FileNameLength / sizeof( wchar_t ) );
        fs_error_t result = WindowsWideSpanToUtf8( pFileName, nWcharCount, szRelativePath, sizeof( szRelativePath ) );
        if ( result != fs_error_t::OK ) {
            return result;
        }

        result = WindowsEmitNativeAction(
            *winWatch,
            info.Action,
            szRelativePath,
            events,
            nMaxEvents,
            nOutEventCount );

        if ( result != fs_error_t::OK ) {
            return result;
        }

        if ( info.NextEntryOffset == 0u ) {
            break;
        }
        if ( info.NextEntryOffset < nHeaderSize || info.NextEntryOffset > nBytesReturned - nOffset ) {
            return fs_error_t::ERR_IO_ERROR;
        }
        nOffset += info.NextEntryOffset;
    }

    return fs_error_t::OK;
}

static fs_error_t WindowsPollNativeWatch(
    watch_t &watch,
    watch_event_t *events,
    common::u32 nMaxEvents,
    common::u32 &nOutEventCount )
{
    windows_watch_t *winWatch = WindowsGetNativeWatch( watch );
    if ( winWatch == nullptr || !winWatch->used ) {
        return fs_error_t::ERR_INVALID_HANDLE;
    }

    if ( !winWatch->bReadPending ) {
        return WindowsArmNativeWatch( watch );
    }

    const DWORD waitResult = WaitForSingleObject( winWatch->hEvent, 0u );
    if ( waitResult == WAIT_TIMEOUT ) {
        return fs_error_t::OK;
    }
    if ( waitResult != WAIT_OBJECT_0 ) {
        return fs_error_t::ERR_IO_ERROR;
    }

    DWORD nBytesReturned = 0u;
    const BOOL ok = GetOverlappedResult(
        winWatch->hDirectory,
        &winWatch->overlapped,
        &nBytesReturned,
        FALSE );

    winWatch->bReadPending = false;

    fs_error_t parseResult = fs_error_t::OK;
    if ( !ok ) {
        const DWORD error = GetLastError();
        if ( error != ERROR_OPERATION_ABORTED ) {
            parseResult = fs_error_t::ERR_IO_ERROR;
        }
    } else if ( nBytesReturned != 0u ) {
        parseResult = WindowsParseNativeEvents( watch, static_cast<common::u32>( nBytesReturned ), events, nMaxEvents, nOutEventCount );
    }

    const fs_error_t armResult = WindowsArmNativeWatch( watch );
    if ( parseResult != fs_error_t::OK ) {
        return parseResult;
    }
    return armResult;
}

#elif defined( CYPHER_PLATFORM_LINUX )

static void LinuxResetNativeWatch( linux_watch_t &linuxWatch )
{
    linuxWatch = {};
    linuxWatch.fd = -1;
    for ( common::u32 i = 0; i < LINUX_MAX_WATCH_DIRS; ++i ) {
        linuxWatch.dirs[i].nWatchDescriptor = -1;
    }
}

static linux_watch_t *LinuxAllocateNativeWatch()
{
    for ( common::u32 i = 0; i < CYPHER_FILESYSTEM_MAX_WATCHES; ++i ) {
        linux_watch_t &linuxWatch = s_LinuxWatches[i];
        if ( !linuxWatch.used ) {
            LinuxResetNativeWatch( linuxWatch );
            linuxWatch.used = true;
            return &linuxWatch;
        }
    }
    return nullptr;
}

static linux_watch_t *LinuxGetNativeWatch( watch_t &watch )
{
    return static_cast<linux_watch_t *>( watch.pNativeHandle );
}

static linux_watch_dir_t *LinuxFindWatchDir( linux_watch_t &linuxWatch, int nWatchDescriptor )
{
    for ( common::u32 i = 0; i < linuxWatch.nDirCount; ++i ) {
        if ( linuxWatch.dirs[i].nWatchDescriptor == nWatchDescriptor ) {
            return &linuxWatch.dirs[i];
        }
    }
    return nullptr;
}

static fs_error_t LinuxAddWatchDir(
    linux_watch_t &linuxWatch,
    const char *szPhysicalPath,
    const char *szVirtualPath,
    bool directory )
{
    if ( linuxWatch.nDirCount >= LINUX_MAX_WATCH_DIRS ) {
        return fs_error_t::ERR_TOO_MANY_WATCHES;
    }

    common::u32 mask =
        IN_ATTRIB |
        IN_CLOSE_WRITE |
        IN_CREATE |
        IN_DELETE |
        IN_DELETE_SELF |
        IN_MODIFY |
        IN_MOVE_SELF |
        IN_MOVED_FROM |
        IN_MOVED_TO;

    if ( directory ) {
        mask |= IN_ONLYDIR;
    }

    const int nWatchDescriptor = inotify_add_watch( linuxWatch.fd, szPhysicalPath, mask );
    if ( nWatchDescriptor < 0 ) {
        return fs_error_t::ERR_IO_ERROR;
    }

    linux_watch_dir_t &dir = linuxWatch.dirs[linuxWatch.nDirCount];
    dir.nWatchDescriptor = nWatchDescriptor;
    if ( !CopyString( dir.szPhysicalPath, sizeof( dir.szPhysicalPath ), szPhysicalPath ) ||
         !CopyString( dir.szVirtualPath, sizeof( dir.szVirtualPath ), szVirtualPath ) ) {
        inotify_rm_watch( linuxWatch.fd, nWatchDescriptor );
        dir = {};
        dir.nWatchDescriptor = -1;
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    ++linuxWatch.nDirCount;
    return fs_error_t::OK;
}

static fs_error_t LinuxAddRecursiveWatchDirs(
    linux_watch_t &linuxWatch,
    const char *szPhysicalRoot,
    const char *szVirtualRoot )
{
    fs_error_t result = LinuxAddWatchDir( linuxWatch, szPhysicalRoot, szVirtualRoot, true );
    if ( result != fs_error_t::OK ) {
        return result;
    }

    std::error_code ec{};
    for ( const std::filesystem::directory_entry &entry : std::filesystem::recursive_directory_iterator( szPhysicalRoot, ec ) ) {
        if ( ec ) {
            return fs_error_t::ERR_IO_ERROR;
        }
        if ( !entry.is_directory( ec ) || ec ) {
            if ( ec ) {
                return fs_error_t::ERR_IO_ERROR;
            }
            continue;
        }

        std::filesystem::path szRelativePath = std::filesystem::relative( entry.path(), szPhysicalRoot, ec );
        if ( ec ) {
            return fs_error_t::ERR_IO_ERROR;
        }

        char szChildVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
        result = BuildWatchEventVirtualPath(
            szVirtualRoot,
            szRelativePath.generic_string().c_str(),
            szChildVirtualPath,
            sizeof( szChildVirtualPath ) );
        if ( result != fs_error_t::OK ) {
            return result;
        }

        result = LinuxAddWatchDir( linuxWatch, entry.path().string().c_str(), szChildVirtualPath, true );
        if ( result != fs_error_t::OK ) {
            return result;
        }
    }

    return fs_error_t::OK;
}

static void LinuxDestroyNativeWatch( watch_t &watch )
{
    linux_watch_t *linuxWatch = LinuxGetNativeWatch( watch );
    if ( linuxWatch == nullptr ) {
        return;
    }

    if ( linuxWatch->fd >= 0 ) {
        for ( common::u32 i = 0; i < linuxWatch->nDirCount; ++i ) {
            if ( linuxWatch->dirs[i].nWatchDescriptor >= 0 ) {
                inotify_rm_watch( linuxWatch->fd, linuxWatch->dirs[i].nWatchDescriptor );
            }
        }
        close( linuxWatch->fd );
    }

    LinuxResetNativeWatch( *linuxWatch );
    watch.pNativeHandle = nullptr;
}

static fs_error_t LinuxCreateNativeWatch( watch_t &watch )
{
    linux_watch_t *linuxWatch = LinuxAllocateNativeWatch();
    if ( linuxWatch == nullptr ) {
        return fs_error_t::ERR_TOO_MANY_WATCHES;
    }
    watch.pNativeHandle = linuxWatch;

    linuxWatch->fd = inotify_init1( IN_NONBLOCK | IN_CLOEXEC );
    if ( linuxWatch->fd < 0 ) {
        LinuxDestroyNativeWatch( watch );
        return fs_error_t::ERR_IO_ERROR;
    }

    linuxWatch->bWatchFile = ( watch.flags & CYPHER_FILESYSTEM_WATCH_FILE ) != 0u;
    linuxWatch->recursive = ( watch.flags & CYPHER_FILESYSTEM_WATCH_RECURSIVE ) != 0u;

    fs_error_t result = fs_error_t::OK;
    if ( linuxWatch->bWatchFile ) {
        const char *basename = FS_PathBasename( watch.szVirtualPath );
        if ( basename == nullptr || basename[0] == '\0' ||
             !CopyString( linuxWatch->szFileFilter, sizeof( linuxWatch->szFileFilter ), basename ) ) {
            LinuxDestroyNativeWatch( watch );
            return fs_error_t::ERR_INVALID_PATH;
        }
        result = LinuxAddWatchDir( *linuxWatch, watch.szPhysicalPath, watch.szVirtualPath, false );
    } else if ( linuxWatch->recursive ) {
        result = LinuxAddRecursiveWatchDirs( *linuxWatch, watch.szPhysicalPath, watch.szVirtualPath );
    } else {
        result = LinuxAddWatchDir( *linuxWatch, watch.szPhysicalPath, watch.szVirtualPath, true );
    }

    if ( result != fs_error_t::OK ) {
        LinuxDestroyNativeWatch( watch );
        return result;
    }

    return fs_error_t::OK;
}

static fs_error_t LinuxBuildEventVirtualPath(
    linux_watch_t &linuxWatch,
    const linux_watch_dir_t &dir,
    const inotify_event &nativeEvent,
    const char *szNativeName,
    char *szOutVirtualPath,
    common::u32 nOutVirtualPathSize )
{
    if ( linuxWatch.bWatchFile || nativeEvent.len == 0u || szNativeName == nullptr || szNativeName[0] == '\0' ) {
        return FS_NormalizeVirtualPath( dir.szVirtualPath, szOutVirtualPath, nOutVirtualPathSize );
    }

    return BuildWatchEventVirtualPath( dir.szVirtualPath, szNativeName, szOutVirtualPath, nOutVirtualPathSize );
}

static fs_error_t LinuxMaybeAddMovedOrCreatedDirectory(
    linux_watch_t &linuxWatch,
    const linux_watch_dir_t &parentDir,
    const inotify_event &nativeEvent,
    const char *szNativeName )
{
    if ( !linuxWatch.recursive || ( nativeEvent.mask & IN_ISDIR ) == 0 || nativeEvent.len == 0u ) {
        return fs_error_t::OK;
    }

    const std::filesystem::path szPhysicalPath = std::filesystem::path( parentDir.szPhysicalPath ) / szNativeName;
    char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    fs_error_t result = BuildWatchEventVirtualPath( parentDir.szVirtualPath, szNativeName, szVirtualPath, sizeof( szVirtualPath ) );
    if ( result != fs_error_t::OK ) {
        return result;
    }

    return LinuxAddRecursiveWatchDirs( linuxWatch, szPhysicalPath.string().c_str(), szVirtualPath );
}

static fs_error_t LinuxEmitNativeEvent(
    linux_watch_t &linuxWatch,
    linux_watch_dir_t &dir,
    const inotify_event &nativeEvent,
    const char *szNativeName,
    watch_event_t *events,
    common::u32 nMaxEvents,
    common::u32 &nOutEventCount )
{
    char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    fs_error_t result = LinuxBuildEventVirtualPath( linuxWatch, dir, nativeEvent, szNativeName, szVirtualPath, sizeof( szVirtualPath ) );
    if ( result != fs_error_t::OK ) {
        return result;
    }

    if ( ( nativeEvent.mask & IN_MOVED_FROM ) != 0u ) {
        if ( !CopyString( linuxWatch.szPendingRenameOldVirtual, sizeof( linuxWatch.szPendingRenameOldVirtual ), szVirtualPath ) ) {
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }
        linuxWatch.nPendingRenameCookie = nativeEvent.cookie;
        linuxWatch.bPendingRename = true;
        return fs_error_t::OK;
    }

    if ( ( nativeEvent.mask & IN_MOVED_TO ) != 0u ) {
        result = LinuxMaybeAddMovedOrCreatedDirectory( linuxWatch, dir, nativeEvent, szNativeName );
        if ( result != fs_error_t::OK ) {
            return result;
        }

        if ( linuxWatch.bPendingRename && linuxWatch.nPendingRenameCookie == nativeEvent.cookie ) {
            result = EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::RENAMED, szVirtualPath, linuxWatch.szPendingRenameOldVirtual );
        } else {
            result = EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::CREATED, szVirtualPath, nullptr );
        }

        linuxWatch.bPendingRename = false;
        linuxWatch.nPendingRenameCookie = 0u;
        linuxWatch.szPendingRenameOldVirtual[0] = '\0';
        return result;
    }

    if ( ( nativeEvent.mask & IN_CREATE ) != 0u ) {
        result = LinuxMaybeAddMovedOrCreatedDirectory( linuxWatch, dir, nativeEvent, szNativeName );
        if ( result != fs_error_t::OK ) {
            return result;
        }
        return EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::CREATED, szVirtualPath, nullptr );
    }

    if ( ( nativeEvent.mask & ( IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF ) ) != 0u ) {
        return EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::DELETED, szVirtualPath, nullptr );
    }

    if ( ( nativeEvent.mask & ( IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB ) ) != 0u ) {
        return EmitWatchEvent( events, nMaxEvents, nOutEventCount, watch_event_type_t::MODIFIED, szVirtualPath, nullptr );
    }

    return fs_error_t::OK;
}

static fs_error_t LinuxPollNativeWatch(
    watch_t &watch,
    watch_event_t *events,
    common::u32 nMaxEvents,
    common::u32 &nOutEventCount )
{
    linux_watch_t *linuxWatch = LinuxGetNativeWatch( watch );
    if ( linuxWatch == nullptr || !linuxWatch->used || linuxWatch->fd < 0 ) {
        return fs_error_t::ERR_INVALID_HANDLE;
    }

    for ( ;; ) {
        const ssize_t nBytesRead = read( linuxWatch->fd, linuxWatch->buffer, sizeof( linuxWatch->buffer ) );
        if ( nBytesRead < 0 ) {
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                return fs_error_t::OK;
            }
            if ( errno == EINTR ) {
                continue;
            }
            return fs_error_t::ERR_IO_ERROR;
        }
        if ( nBytesRead == 0 ) {
            return fs_error_t::OK;
        }

        ssize_t nOffset = 0;
        while ( nOffset < nBytesRead ) {
            if ( nBytesRead - nOffset < static_cast<ssize_t>( sizeof( inotify_event ) ) ) {
                return fs_error_t::ERR_IO_ERROR;
            }

            inotify_event nativeEvent{};
            std::memcpy( &nativeEvent, linuxWatch->buffer + nOffset, sizeof( nativeEvent ) );

            if ( static_cast<ssize_t>( nativeEvent.len ) > nBytesRead - nOffset - static_cast<ssize_t>( sizeof( inotify_event ) ) ) {
                return fs_error_t::ERR_IO_ERROR;
            }

            char szNativeName[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
            if ( nativeEvent.len != 0u ) {
                if ( nativeEvent.len >= sizeof( szNativeName ) ) {
                    return fs_error_t::ERR_BUFFER_TOO_SMALL;
                }
                std::memcpy( szNativeName, linuxWatch->buffer + nOffset + sizeof( inotify_event ), nativeEvent.len );
                szNativeName[sizeof( szNativeName ) - 1u] = '\0';
            }

            linux_watch_dir_t *dir = LinuxFindWatchDir( *linuxWatch, nativeEvent.wd );
            if ( dir != nullptr && ( nativeEvent.mask & IN_IGNORED ) == 0u ) {
                const fs_error_t result = LinuxEmitNativeEvent(
                    *linuxWatch,
                    *dir,
                    nativeEvent,
                    szNativeName,
                    events,
                    nMaxEvents,
                    nOutEventCount );
                if ( result != fs_error_t::OK ) {
                    return result;
                }
            }

            nOffset += static_cast<ssize_t>( sizeof( inotify_event ) + nativeEvent.len );
        }
    }
}

#elif defined( CYPHER_PLATFORM_MACOS )

static void MacOSResetNativeWatch( macos_watch_t &macosWatch )
{
    macosWatch = {};
    macosWatch.nKqueueFd = -1;
    macosWatch.nWatchedFd = -1;
}

static macos_watch_t *MacOSAllocateNativeWatch()
{
    for ( common::u32 i = 0; i < CYPHER_FILESYSTEM_MAX_WATCHES; ++i ) {
        macos_watch_t &macosWatch = s_MacosWatches[i];
        if ( !macosWatch.used ) {
            MacOSResetNativeWatch( macosWatch );
            macosWatch.used = true;
            return &macosWatch;
        }
    }
    return nullptr;
}

static macos_watch_t *MacOSGetNativeWatch( watch_t &watch )
{
    return static_cast<macos_watch_t *>( watch.pNativeHandle );
}

static void MacOSDestroyNativeWatch( watch_t &watch )
{
    macos_watch_t *macosWatch = MacOSGetNativeWatch( watch );
    if ( macosWatch == nullptr ) {
        return;
    }

    if ( macosWatch->nWatchedFd >= 0 ) {
        close( macosWatch->nWatchedFd );
    }
    if ( macosWatch->nKqueueFd >= 0 ) {
        close( macosWatch->nKqueueFd );
    }

    MacOSResetNativeWatch( *macosWatch );
    watch.pNativeHandle = nullptr;
}

static fs_error_t MacOSCreateNativeWatch( watch_t &watch )
{
    macos_watch_t *macosWatch = MacOSAllocateNativeWatch();
    if ( macosWatch == nullptr ) {
        return fs_error_t::ERR_TOO_MANY_WATCHES;
    }
    watch.pNativeHandle = macosWatch;

    macosWatch->bWatchFile = ( watch.flags & CYPHER_FILESYSTEM_WATCH_FILE ) != 0u;

    if ( !CopyString( macosWatch->szWatchPhysicalPath, sizeof( macosWatch->szWatchPhysicalPath ), watch.szPhysicalPath ) ||
         !CopyString( macosWatch->szWatchVirtualPath, sizeof( macosWatch->szWatchVirtualPath ), watch.szVirtualPath ) ) {
        MacOSDestroyNativeWatch( watch );
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    if ( macosWatch->bWatchFile ) {
        const char *basename = FS_PathBasename( watch.szVirtualPath );
        if ( basename == nullptr || basename[0] == '\0' ||
             !CopyString( macosWatch->szFileFilter, sizeof( macosWatch->szFileFilter ), basename ) ) {
            MacOSDestroyNativeWatch( watch );
            return fs_error_t::ERR_INVALID_PATH;
        }
    }

    macosWatch->nKqueueFd = kqueue();
    if ( macosWatch->nKqueueFd < 0 ) {
        MacOSDestroyNativeWatch( watch );
        return fs_error_t::ERR_IO_ERROR;
    }

    macosWatch->nWatchedFd = open( macosWatch->szWatchPhysicalPath, O_EVTONLY );
    if ( macosWatch->nWatchedFd < 0 ) {
        MacOSDestroyNativeWatch( watch );
        return fs_error_t::ERR_IO_ERROR;
    }

    struct kevent event{};
    EV_SET(
        &event,
        static_cast<uintptr_t>( macosWatch->nWatchedFd ),
        EVFILT_VNODE,
        EV_ADD | EV_CLEAR,
        NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE | NOTE_RENAME | NOTE_REVOKE,
        0,
        nullptr );

    if ( kevent( macosWatch->nKqueueFd, &event, 1, nullptr, 0, nullptr ) < 0 ) {
        MacOSDestroyNativeWatch( watch );
        return fs_error_t::ERR_IO_ERROR;
    }

    return fs_error_t::OK;
}

static fs_error_t MacOSPollNativeWatch( watch_t &watch )
{
    macos_watch_t *macosWatch = MacOSGetNativeWatch( watch );
    if ( macosWatch == nullptr || !macosWatch->used || macosWatch->nKqueueFd < 0 ) {
        return fs_error_t::ERR_INVALID_HANDLE;
    }

    struct kevent event{};
    timespec timeout{};
    const int result = kevent( macosWatch->nKqueueFd, nullptr, 0, &event, 1, &timeout );
    if ( result < 0 ) {
        if ( errno == EINTR ) {
            return fs_error_t::OK;
        }
        return fs_error_t::ERR_IO_ERROR;
    }
    if ( result == 0 ) {
        return fs_error_t::OK;
    }
    if ( ( event.flags & EV_ERROR ) != 0 ) {
        return fs_error_t::ERR_IO_ERROR;
    }

    return fs_error_t::OK;
}

#endif

}           // namespace

fs_error_t FS_WatchPath(
    const char *szVirtualPath,
    common::u32 flags,
    watch_handle_t &watchOut )
{
    std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
    runtime_state_t &state = FS_RuntimeState();
    watchOut = CYPHER_FILESYSTEM_INVALID_WATCH;
    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }
    if ( state.nWatchCount >= CYPHER_FILESYSTEM_MAX_WATCHES ) {
        return fs_error_t::ERR_TOO_MANY_WATCHES;
    }
    char szNormalizedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    bool bIsDir = false;
    fs_error_t result = ResolveWatchPhysicalPath( state, szVirtualPath, szNormalizedPath, sizeof( szNormalizedPath ), szPhysicalPath, sizeof( szPhysicalPath ), bIsDir );
    if ( result != fs_error_t::OK ) {
        return result;
    }
    if ( flags == CYPHER_FILESYSTEM_WATCH_NONE ) {
        flags = bIsDir ? CYPHER_FILESYSTEM_WATCH_DIRECTORY : CYPHER_FILESYSTEM_WATCH_FILE;
    }
    result = ValidateWatchFlags( flags, bIsDir );
    if ( result != fs_error_t::OK ) {
        return result;
    }
    for ( common::u32 i = 0; i < state.nWatchCount; ++i ) {
        if ( std::strcmp( state.watches[i].szVirtualPath, szNormalizedPath ) == 0 ) {
            return fs_error_t::ERR_ALREADY_EXISTS;
        }
    }
    watch_t &watch = state.watches[state.nWatchCount];
    ResetWatch( watch );
    watch.handle = AllocateWatchHandle( state );
    watch.flags = flags;

    if ( !CopyString( watch.szVirtualPath, sizeof( watch.szVirtualPath ), szNormalizedPath ) ||
         !CopyString( watch.szPhysicalPath, sizeof( watch.szPhysicalPath ), szPhysicalPath ) )
    {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }
    result = BuildWatchSnapshot( watch );
    if ( result != fs_error_t::OK ) {
        ResetWatch( watch );
        return result;
    }

#if defined( CYPHER_PLATFORM_WINDOWS )
    result = WindowsCreateNativeWatch( watch );
#elif defined( CYPHER_PLATFORM_LINUX )
    result = LinuxCreateNativeWatch( watch );
#elif defined( CYPHER_PLATFORM_MACOS )
    result = MacOSCreateNativeWatch( watch );
#endif
#if defined( CYPHER_PLATFORM_WINDOWS ) || defined( CYPHER_PLATFORM_LINUX ) || defined( CYPHER_PLATFORM_MACOS )
    if ( result != fs_error_t::OK ) {
        ResetWatch( watch );
        return result;
    }
#endif

    ++state.nWatchCount;
    watchOut = watch.handle;

    return fs_error_t::OK;
}

fs_error_t FS_UnwatchPath( watch_handle_t nWatchHandle )
{
    std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
    runtime_state_t &state = FS_RuntimeState();
    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( nWatchHandle == CYPHER_FILESYSTEM_INVALID_WATCH ) {
        return fs_error_t::ERR_INVALID_HANDLE;
    }

    for ( common::u32 i = 0; i < state.nWatchCount; ++i ) {
        watch_t &watch = state.watches[i];
        if ( watch.handle == nWatchHandle ) {
#if defined( CYPHER_PLATFORM_WINDOWS )
            WindowsDestroyNativeWatch( watch );
#elif defined( CYPHER_PLATFORM_LINUX )
            LinuxDestroyNativeWatch( watch );
#elif defined( CYPHER_PLATFORM_MACOS )
            MacOSDestroyNativeWatch( watch );
#endif
            // @note -> shift all the watches and fill the holes
            for ( common::u32 j = i; j + 1 < state.nWatchCount; ++j ) {
                state.watches[j] = state.watches[j + 1];
            }
            --state.nWatchCount;
            ResetWatch( state.watches[state.nWatchCount] );
            return fs_error_t::OK;
        }
    }
    return fs_error_t::ERR_INVALID_HANDLE;
}

fs_error_t FS_PollChanges(
    watch_event_t *events,
    common::u32 nMaxEvents,
    common::u32 &nOutEventCount )
{
    std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
    runtime_state_t &state = FS_RuntimeState();
    nOutEventCount = 0u;
    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( events == nullptr && nMaxEvents != 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    for ( common::u32 watchIdx = 0; watchIdx < state.nWatchCount; ++watchIdx ) {
        watch_t &oldWatch = state.watches[watchIdx];
        fs_error_t result = fs_error_t::OK;
        const common::u32 nNativeEventCountBefore = nOutEventCount;

#if defined( CYPHER_PLATFORM_WINDOWS )
        result = WindowsPollNativeWatch( oldWatch, events, nMaxEvents, nOutEventCount );
        if ( result != fs_error_t::OK ) {
            return result;
        }
        ( void )nNativeEventCountBefore;
#elif defined( CYPHER_PLATFORM_LINUX )
        result = LinuxPollNativeWatch( oldWatch, events, nMaxEvents, nOutEventCount );
        if ( result != fs_error_t::OK ) {
            return result;
        }
        ( void )nNativeEventCountBefore;
#elif defined( CYPHER_PLATFORM_MACOS )
        result = MacOSPollNativeWatch( oldWatch );
        if ( result != fs_error_t::OK ) {
            return result;
        }
#endif

        watch_t &newWatch = state.watchScratch;
        result = BuildFreshWatchSnapshot( oldWatch, newWatch );
        if ( result != fs_error_t::OK ) {
            return result;
        }
        result = DiffWatchSnapshots( oldWatch, newWatch, events, nMaxEvents, nOutEventCount );
        if ( result != fs_error_t::OK ) {
            return result;
        }
    }
    return fs_error_t::OK;
}

}       // namespace cypher::engine::fs
