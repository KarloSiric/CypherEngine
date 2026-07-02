//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherFileSystem/CypherFileSystem_Mount.cpp
//  Purpose: Implements the CypherFileSystem FileSystem Mount module.
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

#include "CypherFileSystem_Runtime.h"
#include "CypherLog.h"
#include "CypherPak.h"

#include <cstring>
#include <filesystem>
#include <system_error>

namespace cypher::engine::fs
{

namespace {

fs_error_t PakErrorToFs( const pak::pak_error_t error )
{
    switch ( error ) {
    case pak::pak_error_t::OK:
        return fs_error_t::OK;
    case pak::pak_error_t::ERR_INVALID_ARGUMENT:
        return fs_error_t::ERR_INVALID_ARGUMENT;
    case pak::pak_error_t::ERR_INVALID_PATH:
        return fs_error_t::ERR_INVALID_PATH;
    case pak::pak_error_t::ERR_INVALID_HANDLE:
        return fs_error_t::ERR_INVALID_HANDLE;
    case pak::pak_error_t::ERR_ENTRY_NOT_FOUND:
    case pak::pak_error_t::ERR_PATH_NOT_FOUND:
        return fs_error_t::ERR_PATH_NOT_FOUND;
    case pak::pak_error_t::ERR_BUFFER_TOO_SMALL:
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    case pak::pak_error_t::ERR_OUT_OF_MEMORY:
        return fs_error_t::ERR_OUT_OF_MEMORY;
    case pak::pak_error_t::ERR_PERMISSION_DENIED:
        return fs_error_t::ERR_PERMISSION_DENIED;
    case pak::pak_error_t::ERR_NOT_IMPLEMENTED:
        return fs_error_t::ERR_NOT_IMPLEMENTED;
    case pak::pak_error_t::ERR_UNSUPPORTED_COMPRESSION:
        return fs_error_t::ERR_UNSUPPORTED_BACKEND;
    default:
        return fs_error_t::ERR_IO_ERROR;
    }
}

pak::pak_reader_t *PackageReader( const mount_t &mount )
{
    return static_cast<pak::pak_reader_t *>( mount.pPackageReader );
}

void FillMountInfo( const mount_t &mount, mount_info_t &infoOut )
{
    infoOut = {};
    infoOut.handle = mount.handle;
    infoOut.type = mount.type;
    infoOut.flags = mount.flags;
    infoOut.priority = mount.priority;
    std::memcpy( infoOut.szVirtualRoot, mount.szVirtualRoot, std::strlen( mount.szVirtualRoot ) + 1u );
    std::memcpy( infoOut.szPhysicalRoot, mount.szPhysicalRoot, std::strlen( mount.szPhysicalRoot ) + 1u );
}

}       // namespace

mount_handle_t CypherFileSystem_AllocateMountHandle( runtime_state_t &state )
{
    mount_handle_t handle = state.nNextMountHandle++;
    if ( handle == CYPHER_FILESYSTEM_INVALID_MOUNT ) {
        handle = state.nNextMountHandle++;
    }
    return handle;
}

fs_error_t CypherFileSystem_InsertMountByPriority( runtime_state_t &state, const mount_t &mount )
{
    if ( state.nMountCount >= CYPHER_FILESYSTEM_MAX_MOUNTS ) {
        return fs_error_t::ERR_TOO_MANY_MOUNTS;
    }

    common::u32 nInsertIndex = state.nMountCount;
    while ( nInsertIndex > 0u && state.mounts[nInsertIndex - 1u].priority < mount.priority ) {
        state.mounts[nInsertIndex] = state.mounts[nInsertIndex - 1u];
        --nInsertIndex;
    }
    state.mounts[nInsertIndex] = mount;
    ++state.nMountCount;
    return fs_error_t::OK;
}

void CypherFileSystem_RemoveMountAtIndex( runtime_state_t &state, const common::u32 index )
{
    if ( index >= state.nMountCount ) {
        return;
    }

    mount_t &mount = state.mounts[index];
    if ( mount.type == mount_type_t::CYPHER_FILESYSTEM_PACKAGE && mount.pPackageReader != nullptr ) {
        pak::pak_reader_t *reader = PackageReader( mount );
        pak::CypherPak_CloseReader( *reader );
        delete reader;
        mount.pPackageReader = nullptr;
    }

    for ( common::u32 j = index; j + 1u < state.nMountCount; ++j ) {
        state.mounts[j] = state.mounts[j + 1u];
    }
    --state.nMountCount;
    state.mounts[state.nMountCount] = {};
}

common::u32 CypherFileSystem_MountCount()
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    return CypherFileSystem_RuntimeState().nMountCount;
}

fs_error_t CypherFileSystem_MountDirectory( const char *szVirtualRoot, const char *szPhysicalPath, common::u32 flags, common::u32 priority )
{
    mount_handle_t nIgnoredHandle = CYPHER_FILESYSTEM_INVALID_MOUNT;
    return CypherFileSystem_MountDirectoryWithHandle( szVirtualRoot, szPhysicalPath, flags, priority, nIgnoredHandle );
}

fs_error_t CypherFileSystem_MountDirectoryWithHandle(
    const char *szVirtualRoot,
    const char *szPhysicalPath,
    common::u32 flags,
    common::u32 priority,
    mount_handle_t &nOutHandle )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    nOutHandle = CYPHER_FILESYSTEM_INVALID_MOUNT;

    if ( !state.initialized ) {
        LOG_ERROR( log::channel_t::FS, "mount failed: filesystem is not initialized." );
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( szPhysicalPath == nullptr || szPhysicalPath[0] == '\0' ) {
        LOG_ERROR( log::channel_t::FS, "mount failed: physical path is invalid." );
        return fs_error_t::ERR_INVALID_PATH;
    }

    char szNormalizedVirtualRoot[CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH]{};
    const fs_error_t rootResult = CypherFileSystem_NormalizeVirtualRoot( szVirtualRoot, szNormalizedVirtualRoot, sizeof( szNormalizedVirtualRoot ) );
    if ( rootResult != fs_error_t::OK ) {
        LOG_ERROR( log::channel_t::FS, "mount failed for '%s': invalid virtual root.", szPhysicalPath );
        return rootResult;
    }

    const common::u32 nAccessFlags = CYPHER_FILESYSTEM_MOUNT_READ_ONLY | CYPHER_FILESYSTEM_MOUNT_WRITABLE;
    const common::u32 bAllowedFlags = nAccessFlags | CYPHER_FILESYSTEM_MOUNT_OPTIONAL;
    if ( ( flags & ~bAllowedFlags ) != 0u ) {
        LOG_ERROR( log::channel_t::FS, "mount failed for '%s': invalid flags 0x%x.", szPhysicalPath, flags );
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( ( flags & nAccessFlags ) == 0u ) {
        LOG_ERROR( log::channel_t::FS, "mount failed for '%s': no mount access flags set.", szPhysicalPath );
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( ( flags & CYPHER_FILESYSTEM_MOUNT_WRITABLE ) != 0u ) {
        LOG_ERROR( log::channel_t::FS, "mount failed for '%s': writable mounts are not implemented; use the filesystem write path.", szPhysicalPath );
        return fs_error_t::ERR_NOT_IMPLEMENTED;
    }
    if ( state.nMountCount >= CYPHER_FILESYSTEM_MAX_MOUNTS ) {
        LOG_ERROR( log::channel_t::FS, "mount failed for '%s': mount table full (%u).", szPhysicalPath, CYPHER_FILESYSTEM_MAX_MOUNTS );
        return fs_error_t::ERR_TOO_MANY_MOUNTS;
    }

    const common::u32 nPhysicalPathLength = static_cast<common::u32>( std::strlen( szPhysicalPath ) );
    if ( nPhysicalPathLength + 1u > CYPHER_FILESYSTEM_MAX_PATH_LENGTH ) {
        LOG_ERROR( log::channel_t::FS, "mount failed for '%s': physical path is too long.", szPhysicalPath );
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    std::error_code ec{};
    if ( !std::filesystem::exists( szPhysicalPath, ec ) ) {
        if ( !ec && ( flags & CYPHER_FILESYSTEM_MOUNT_OPTIONAL ) != 0u ) {
            LOG_WARNING( log::channel_t::FS, "optional mount skipped: '%s' does not exist.", szPhysicalPath );
            return fs_error_t::OK;
        }
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_PATH_NOT_FOUND;
    }
    if ( !std::filesystem::is_directory( szPhysicalPath, ec ) || ec ) {
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_NOT_DIRECTORY;
    }

    for ( common::u32 i = 0u; i < state.nMountCount; ++i ) {
        const mount_t &existingMount = state.mounts[i];
        if ( std::strcmp( existingMount.szVirtualRoot, szNormalizedVirtualRoot ) == 0 &&
             std::strcmp( existingMount.szPhysicalRoot, szPhysicalPath ) == 0 ) {
            return fs_error_t::ERR_ALREADY_EXISTS;
        }
    }

    mount_t mount{};
    mount.handle = CypherFileSystem_AllocateMountHandle( state );
    mount.type = mount_type_t::CYPHER_FILESYSTEM_DIRECTORY;

    const common::u32 nVirtualRootLength = static_cast<common::u32>( std::strlen( szNormalizedVirtualRoot ) );
    std::memcpy( mount.szVirtualRoot, szNormalizedVirtualRoot, nVirtualRootLength + 1u );
    std::memcpy( mount.szPhysicalRoot, szPhysicalPath, nPhysicalPathLength + 1u );
    mount.flags = flags;
    mount.priority = priority;

    const fs_error_t insertResult = CypherFileSystem_InsertMountByPriority( state, mount );
    if ( insertResult != fs_error_t::OK ) {
        return insertResult;
    }
    nOutHandle = mount.handle;

    LOG_INFO( log::channel_t::FS, "mounted '%s' -> '%s' handle=%u flags=0x%x priority=%u.", szNormalizedVirtualRoot[0] ? szNormalizedVirtualRoot : "<root>", szPhysicalPath, mount.handle, flags, priority );
    return fs_error_t::OK;
}

fs_error_t CypherFileSystem_UnmountDirectory( const char *szVirtualRoot )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );

    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }

    char szNormalizedVirtualRoot[CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH]{};
    const fs_error_t rootResult = CypherFileSystem_NormalizeVirtualRoot( szVirtualRoot, szNormalizedVirtualRoot, sizeof( szNormalizedVirtualRoot ) );
    if ( rootResult != fs_error_t::OK ) {
        return rootResult;
    }

    for ( common::u32 i = 0u; i < state.nMountCount; ++i ) {
        if ( state.mounts[i].type == mount_type_t::CYPHER_FILESYSTEM_DIRECTORY &&
             std::strcmp( state.mounts[i].szVirtualRoot, szNormalizedVirtualRoot ) == 0 ) {
            CypherFileSystem_RemoveMountAtIndex( state, i );
            return fs_error_t::OK;
        }
    }

    return fs_error_t::ERR_MOUNT_NOT_FOUND;
}

fs_error_t CypherFileSystem_Unmount( mount_handle_t mount )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );

    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( mount == CYPHER_FILESYSTEM_INVALID_MOUNT ) {
        return fs_error_t::ERR_INVALID_HANDLE;
    }

    for ( common::u32 i = 0u; i < state.nMountCount; ++i ) {
        if ( state.mounts[i].handle == mount ) {
            CypherFileSystem_RemoveMountAtIndex( state, i );
            return fs_error_t::OK;
        }
    }

    return fs_error_t::ERR_MOUNT_NOT_FOUND;
}

fs_error_t CypherFileSystem_GetMountInfo( common::u32 nMountIndex, mount_info_t &infoOut )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );

    infoOut = {};

    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( nMountIndex >= state.nMountCount ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }

    const mount_t &mount = state.mounts[nMountIndex];
    FillMountInfo( mount, infoOut );
    return fs_error_t::OK;
}

fs_error_t CypherFileSystem_GetMountInfoByHandle( mount_handle_t mount, mount_info_t &infoOut )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );

    infoOut = {};

    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( mount == CYPHER_FILESYSTEM_INVALID_MOUNT ) {
        return fs_error_t::ERR_INVALID_HANDLE;
    }

    for ( common::u32 i = 0u; i < state.nMountCount; ++i ) {
        if ( state.mounts[i].handle == mount ) {
            FillMountInfo( state.mounts[i], infoOut );
            return fs_error_t::OK;
        }
    }

    return fs_error_t::ERR_MOUNT_NOT_FOUND;
}

fs_error_t CypherFileSystem_ResolvePath( const char *szVirtualPath, char *szOutResolvedPath, common::u32 nOutResolvedPathSize )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }
    if ( szOutResolvedPath == nullptr || nOutResolvedPathSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    szOutResolvedPath[0] = '\0';
    resolved_file_t resolvedFile{};
    const fs_error_t resolveResult = CypherFileSystem_ResolveReadableFile( szVirtualPath, resolvedFile );
    if ( resolveResult != fs_error_t::OK ) {
        return resolveResult;
    }
    if ( resolvedFile.backend != file_backend_t::OS_FILE ) {
        return fs_error_t::ERR_UNSUPPORTED_BACKEND;
    }
    const common::u32 szResolvedPathLength = static_cast<common::u32>( std::strlen( resolvedFile.szPhysicalPath ) );
    if ( szResolvedPathLength + 1u > nOutResolvedPathSize ) {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }
    std::memcpy( szOutResolvedPath, resolvedFile.szPhysicalPath, szResolvedPathLength + 1u );
    return fs_error_t::OK;
}

fs_error_t CypherFileSystem_ResolveReadableFile( const char *szVirtualPath, resolved_file_t &fileOut )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );

    fileOut = {};

    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }

    const fs_error_t normalizeResult = CypherFileSystem_NormalizeVirtualPath(
        szVirtualPath,
        fileOut.szNormalizedPath,
        sizeof( fileOut.szNormalizedPath ) );
    if ( normalizeResult != fs_error_t::OK ) {
        return normalizeResult;
    }

    for ( common::u32 i = 0u; i < state.nMountCount; ++i ) {
        const mount_t &mount = state.mounts[i];

        const char *szRelativePath = nullptr;
        if ( !CypherFileSystem_VirtualPathStartsWithRoot( fileOut.szNormalizedPath, mount.szVirtualRoot, &szRelativePath ) ) {
            continue;
        }
        if ( szRelativePath == nullptr || szRelativePath[0] == '\0' ) {
            continue;
        }

        if ( mount.type == mount_type_t::CYPHER_FILESYSTEM_DIRECTORY ) {
            char szCandidatePath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
            const fs_error_t buildPathResult = CypherFileSystem_BuildPhysicalPath( mount.szPhysicalRoot, szRelativePath, szCandidatePath, sizeof( szCandidatePath ) );
            if ( buildPathResult != fs_error_t::OK ) {
                return buildPathResult;
            }

            std::error_code ec{};
            if ( !std::filesystem::exists( szCandidatePath, ec ) ) {
                continue;
            }
            if ( ec ) {
                return fs_error_t::ERR_IO_ERROR;
            }
            if ( std::filesystem::is_directory( szCandidatePath, ec ) || ec ) {
                if ( ec ) {
                    return fs_error_t::ERR_IO_ERROR;
                }
                continue;
            }

            fileOut.backend = file_backend_t::OS_FILE;
            fileOut.mount = mount.handle;
            fileOut.nMountIndex = i;
            fileOut.bIsDirectory = false;

            const common::u32 nCandidateLen = static_cast<common::u32>( std::strlen( szCandidatePath ) );
            if ( nCandidateLen + 1u > sizeof( fileOut.szPhysicalPath ) ) {
                return fs_error_t::ERR_BUFFER_TOO_SMALL;
            }
            std::memcpy( fileOut.szPhysicalPath, szCandidatePath, nCandidateLen + 1u );

            fileOut.nFileSize = static_cast<common::u64>( std::filesystem::file_size( szCandidatePath, ec ) );
            return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::OK;
        }

        if ( mount.type == mount_type_t::CYPHER_FILESYSTEM_PACKAGE ) {
            pak::pak_reader_t *reader = PackageReader( mount );
            if ( reader == nullptr ) {
                continue;
            }

            pak::pak_file_index_t nPackageIndex = pak::CYPHER_PAK_INVALID_FILE_INDEX;
            const pak::pak_error_t findResult = pak::CypherPak_FindFile( *reader, szRelativePath, nPackageIndex );
            if ( findResult == pak::pak_error_t::ERR_ENTRY_NOT_FOUND ) {
                continue;
            }
            if ( findResult != pak::pak_error_t::OK ) {
                return PakErrorToFs( findResult );
            }

            pak::pak_file_info_t packageInfo{};
            const pak::pak_error_t infoResult = pak::CypherPak_GetFileInfo( *reader, nPackageIndex, packageInfo );
            if ( infoResult != pak::pak_error_t::OK ) {
                return PakErrorToFs( infoResult );
            }

            fileOut.backend = file_backend_t::PACKAGE_FILE;
            fileOut.mount = mount.handle;
            fileOut.nMountIndex = i;
            fileOut.nPackageFileIndex = nPackageIndex;
            fileOut.pPackageReader = reader;
            fileOut.nFileSize = packageInfo.nUnpackedSize;
            fileOut.bIsDirectory = false;

            const common::u32 nPackagePathLen = static_cast<common::u32>( std::strlen( mount.szPhysicalRoot ) );
            if ( nPackagePathLen + 1u > sizeof( fileOut.szPackagePath ) ) {
                return fs_error_t::ERR_BUFFER_TOO_SMALL;
            }
            std::memcpy( fileOut.szPackagePath, mount.szPhysicalRoot, nPackagePathLen + 1u );
            return fs_error_t::OK;
        }
    }

    state.stats.nFailedLookupCount++;
    return fs_error_t::ERR_PATH_NOT_FOUND;
}

fs_error_t CypherFileSystem_TraceResolve( const char *szVirtualPath, resolve_trace_t &traceOut )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );

    traceOut = resolve_trace_t{};

    if ( !state.initialized ) {
        traceOut.result = fs_error_t::ERR_NOT_INIT;
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
        traceOut.result = fs_error_t::ERR_INVALID_PATH;
        return fs_error_t::ERR_INVALID_PATH;
    }

    const common::usize nRequestedLen = std::strlen( szVirtualPath );
    if ( nRequestedLen + 1u > sizeof( traceOut.szRequestedPath ) ) {
        traceOut.result = fs_error_t::ERR_BUFFER_TOO_SMALL;
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }
    std::memcpy( traceOut.szRequestedPath, szVirtualPath, nRequestedLen + 1u );

    const fs_error_t normalizeResult = CypherFileSystem_NormalizeVirtualPath( szVirtualPath, traceOut.szNormalizedPath, sizeof( traceOut.szNormalizedPath ) );
    if ( normalizeResult != fs_error_t::OK ) {
        traceOut.result = normalizeResult;
        return normalizeResult;
    }

    for ( common::u32 i = 0u; i < state.nMountCount; ++i ) {
        const mount_t &mount = state.mounts[i];
        resolve_trace_entry_t &entry = traceOut.entries[traceOut.nCheckedMountCount++];
        entry.mount = mount.handle;
        entry.type = mount.type;
        std::memcpy( entry.szVirtualRoot, mount.szVirtualRoot, std::strlen( mount.szVirtualRoot ) + 1u );

        const char *szRelativePath = nullptr;
        if ( !CypherFileSystem_VirtualPathStartsWithRoot( traceOut.szNormalizedPath, mount.szVirtualRoot, &szRelativePath ) ) {
            continue;
        }

        entry.bRootMatched = true;

        if ( mount.type == mount_type_t::CYPHER_FILESYSTEM_PACKAGE ) {
            pak::pak_reader_t *reader = PackageReader( mount );
            if ( reader == nullptr || szRelativePath == nullptr || szRelativePath[0] == '\0' ) {
                continue;
            }

            const common::usize nPackagePathLen = std::strlen( mount.szPhysicalRoot );
            if ( nPackagePathLen + 1u > sizeof( entry.szPhysicalPath ) ) {
                traceOut.result = fs_error_t::ERR_BUFFER_TOO_SMALL;
                return fs_error_t::ERR_BUFFER_TOO_SMALL;
            }
            std::memcpy( entry.szPhysicalPath, mount.szPhysicalRoot, nPackagePathLen + 1u );

            pak::pak_file_index_t nPackageIndex = pak::CYPHER_PAK_INVALID_FILE_INDEX;
            const pak::pak_error_t findResult = pak::CypherPak_FindFile( *reader, szRelativePath, nPackageIndex );
            if ( findResult == pak::pak_error_t::ERR_ENTRY_NOT_FOUND ) {
                continue;
            }
            if ( findResult != pak::pak_error_t::OK ) {
                traceOut.result = PakErrorToFs( findResult );
                return traceOut.result;
            }

            entry.bPathExists = true;
            std::memcpy( traceOut.szResolvedPath, entry.szPhysicalPath, nPackagePathLen + 1u );
            traceOut.resolved = true;
            traceOut.result = fs_error_t::OK;
            return fs_error_t::OK;
        }

        if ( mount.type != mount_type_t::CYPHER_FILESYSTEM_DIRECTORY ) {
            continue;
        }

        const fs_error_t buildPathResult = CypherFileSystem_BuildPhysicalPath( mount.szPhysicalRoot, szRelativePath, entry.szPhysicalPath, sizeof( entry.szPhysicalPath ) );
        if ( buildPathResult != fs_error_t::OK ) {
            traceOut.result = buildPathResult;
            return buildPathResult;
        }

        std::error_code ec{};
        entry.bPathExists = std::filesystem::exists( entry.szPhysicalPath, ec );
        if ( ec ) {
            traceOut.result = fs_error_t::ERR_IO_ERROR;
            return fs_error_t::ERR_IO_ERROR;
        }
        if ( !entry.bPathExists ) {
            continue;
        }

        const common::usize bResolvedLen = std::strlen( entry.szPhysicalPath );
        if ( bResolvedLen + 1u > sizeof( traceOut.szResolvedPath ) ) {
            traceOut.result = fs_error_t::ERR_BUFFER_TOO_SMALL;
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }

        std::memcpy( traceOut.szResolvedPath, entry.szPhysicalPath, bResolvedLen + 1u );
        traceOut.resolved = true;
        traceOut.result = fs_error_t::OK;
        return fs_error_t::OK;
    }

    traceOut.result = fs_error_t::ERR_PATH_NOT_FOUND;
    return fs_error_t::ERR_PATH_NOT_FOUND;
}

bool CypherFileSystem_Exists( const char *szVirtualPath )
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    if ( !CypherFileSystem_RuntimeState().initialized ) {
        return false;
    }

    resolved_file_t resolvedFile{};
    if ( CypherFileSystem_ResolveReadableFile( szVirtualPath, resolvedFile ) == fs_error_t::OK ) {
        return true;
    }

    common::u32 nEntryCount = 0u;
    const fs_error_t listResult = CypherFileSystem_ListDirectory( szVirtualPath, nullptr, 0u, nEntryCount );
    return listResult == fs_error_t::OK || listResult == fs_error_t::ERR_BUFFER_TOO_SMALL;
}

bool CypherFileSystem_FileExists( const char *szVirtualPath )
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    if ( !CypherFileSystem_RuntimeState().initialized ) {
        return false;
    }

    file_info_t info{};
    return CypherFileSystem_GetFileInfo( szVirtualPath, info ) == fs_error_t::OK && info.exists && !info.bIsDirectory;
}

fs_error_t CypherFileSystem_GetFileInfo( const char *szVirtualPath, file_info_t &infoOut )
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    if ( !CypherFileSystem_RuntimeState().initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }

    infoOut = {};

    if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }

    char szNormalizedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    fs_error_t err = CypherFileSystem_NormalizeVirtualPath( szVirtualPath, szNormalizedPath, sizeof( szNormalizedPath ) );
    if ( err != fs_error_t::OK ) {
        return err;
    }

    std::memcpy( infoOut.szVirtualPath, szNormalizedPath, std::strlen( szNormalizedPath ) + 1u );

    resolved_file_t resolvedFile{};
    err = CypherFileSystem_ResolveReadableFile( szNormalizedPath, resolvedFile );
    if ( err == fs_error_t::OK ) {
        infoOut.exists = true;
        infoOut.bIsDirectory = false;
        infoOut.backend = resolvedFile.backend;
        infoOut.nFileSize = resolvedFile.nFileSize;
        infoOut.bIsPackageFile = resolvedFile.backend == file_backend_t::PACKAGE_FILE;

        if ( resolvedFile.backend == file_backend_t::OS_FILE ) {
            std::memcpy( infoOut.szResolvedPath, resolvedFile.szPhysicalPath, std::strlen( resolvedFile.szPhysicalPath ) + 1u );
        } else if ( resolvedFile.backend == file_backend_t::PACKAGE_FILE ) {
            std::memcpy( infoOut.szPackagePath, resolvedFile.szPackagePath, std::strlen( resolvedFile.szPackagePath ) + 1u );
        }

        return fs_error_t::OK;
    }
    if ( err != fs_error_t::ERR_PATH_NOT_FOUND ) {
        return err;
    }

    common::u32 nEntryCount = 0u;
    err = CypherFileSystem_ListDirectory( szNormalizedPath, nullptr, 0u, nEntryCount );
    if ( err != fs_error_t::OK && err != fs_error_t::ERR_BUFFER_TOO_SMALL ) {
        infoOut = {};
        return err;
    }

    infoOut.exists = true;
    infoOut.bIsDirectory = true;
    infoOut.backend = file_backend_t::OS_FILE;
    infoOut.bIsPackageFile = false;
    return fs_error_t::OK;
}

}       // namespace cypher::engine::fs
