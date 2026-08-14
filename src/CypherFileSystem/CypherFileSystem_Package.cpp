//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherFileSystem/CypherFileSystem_Package.cpp
//  Purpose: Implements the CypherFileSystem FileSystem Package module.
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
#include <new>
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

bool CopyString( char *out, const common::u32 nOutSize, const char *text )
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
}

pak::pak_reader_t *PackageReader( const mount_t &mount )
{
    return static_cast<pak::pak_reader_t *>( mount.pPackageReader );
}

}       // namespace

fs_error_t CypherFileSystem_MountPackage(
    const char *szVirtualRoot,
    const char *szPackagePath,
    common::u32 flags,
    common::u32 priority )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );

    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( szPackagePath == nullptr || szPackagePath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }

    char szNormalizedVirtualRoot[CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH]{};
    const fs_error_t rootResult = CypherFileSystem_NormalizeVirtualRoot( szVirtualRoot, szNormalizedVirtualRoot, sizeof( szNormalizedVirtualRoot ) );
    if ( rootResult != fs_error_t::OK ) {
        return rootResult;
    }

    const common::u32 bAllowedFlags = CYPHER_FILESYSTEM_MOUNT_READ_ONLY | CYPHER_FILESYSTEM_MOUNT_OPTIONAL;
    if ( ( flags & ~bAllowedFlags ) != 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( ( flags & CYPHER_FILESYSTEM_MOUNT_READ_ONLY ) == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }
    if ( state.nMountCount >= CYPHER_FILESYSTEM_MAX_MOUNTS ) {
        return fs_error_t::ERR_TOO_MANY_MOUNTS;
    }

    const common::u32 nPackagePathLen = static_cast<common::u32>( std::strlen( szPackagePath ) );
    if ( nPackagePathLen + 1u > CYPHER_FILESYSTEM_MAX_PATH_LENGTH ) {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }

    std::error_code ec{};
    if ( !std::filesystem::exists( szPackagePath, ec ) ) {
        if ( !ec && ( flags & CYPHER_FILESYSTEM_MOUNT_OPTIONAL ) != 0u ) {
            LOG_WARNING( log::channel_t::FS, "optional package mount skipped: '%s' does not exist.", szPackagePath );
            return fs_error_t::OK;
        }
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_PATH_NOT_FOUND;
    }
    if ( !std::filesystem::is_regular_file( szPackagePath, ec ) || ec ) {
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_NOT_FILE;
    }

    for ( common::u32 i = 0u; i < state.nMountCount; ++i ) {
        const mount_t &existingMount = state.mounts[i];
        if ( existingMount.type == mount_type_t::CYPHER_FILESYSTEM_PACKAGE &&
             std::strcmp( existingMount.szVirtualRoot, szNormalizedVirtualRoot ) == 0 &&
             std::strcmp( existingMount.szPhysicalRoot, szPackagePath ) == 0 ) {
            return fs_error_t::ERR_ALREADY_EXISTS;
        }
    }

    pak::pak_reader_t *reader = new ( std::nothrow ) pak::pak_reader_t();
    if ( reader == nullptr ) {
        return fs_error_t::ERR_OUT_OF_MEMORY;
    }

    const pak::pak_error_t openResult = pak::CypherPak_OpenReader(
        szPackagePath,
        pak::CYPHER_PAK_OPEN_VERIFY_INDEX,
        *reader );
    if ( openResult != pak::pak_error_t::OK ) {
        delete reader;
        return PakErrorToFs( openResult );
    }

    mount_t mount{};
    mount.handle = CypherFileSystem_AllocateMountHandle( state );
    mount.type = mount_type_t::CYPHER_FILESYSTEM_PACKAGE;
    mount.flags = flags;
    mount.priority = priority;
    mount.pPackageReader = reader;
    std::memcpy( mount.szVirtualRoot, szNormalizedVirtualRoot, std::strlen( szNormalizedVirtualRoot ) + 1u );
    std::memcpy( mount.szPhysicalRoot, szPackagePath, nPackagePathLen + 1u );

    const fs_error_t insertResult = CypherFileSystem_InsertMountByPriority( state, mount );
    if ( insertResult != fs_error_t::OK ) {
        pak::CypherPak_CloseReader( *reader );
        delete reader;
        return insertResult;
    }

    LOG_INFO( log::channel_t::FS, "mounted package '%s' -> '%s' handle=%u flags=0x%x priority=%u.", szNormalizedVirtualRoot[0] ? szNormalizedVirtualRoot : "<root>", szPackagePath, mount.handle, flags, priority );
    return fs_error_t::OK;
}

fs_error_t CypherFileSystem_UnmountPackage( const char *szPackagePath )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( szPackagePath == nullptr || szPackagePath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }

    for ( common::u32 i = 0u; i < state.nMountCount; ++i ) {
        const mount_t &mount = state.mounts[i];
        if ( mount.type == mount_type_t::CYPHER_FILESYSTEM_PACKAGE &&
             std::strcmp( mount.szPhysicalRoot, szPackagePath ) == 0 ) {
            CypherFileSystem_RemoveMountAtIndex( state, i );
            return fs_error_t::OK;
        }
    }

    return fs_error_t::ERR_MOUNT_NOT_FOUND;
}

fs_error_t CypherFileSystem_GetPackageInfo( const char *szPackagePath, package_info_t &infoOut )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    infoOut = {};

    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( szPackagePath == nullptr || szPackagePath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }

    for ( common::u32 i = 0u; i < state.nMountCount; ++i ) {
        const mount_t &mount = state.mounts[i];
        if ( mount.type != mount_type_t::CYPHER_FILESYSTEM_PACKAGE ||
             std::strcmp( mount.szPhysicalRoot, szPackagePath ) != 0 ) {
            continue;
        }

        if ( !CopyString( infoOut.szVirtualRoot, sizeof( infoOut.szVirtualRoot ), mount.szVirtualRoot ) ||
             !CopyString( infoOut.szPackagePath, sizeof( infoOut.szPackagePath ), mount.szPhysicalRoot ) ) {
            infoOut = {};
            return fs_error_t::ERR_BUFFER_TOO_SMALL;
        }

        common::u32 nFileCount = 0u;
        pak::pak_reader_t *reader = PackageReader( mount );
        const pak::pak_error_t nCountResult = pak::CypherPak_GetFileCount( *reader, nFileCount );
        if ( nCountResult != pak::pak_error_t::OK ) {
            infoOut = {};
            return PakErrorToFs( nCountResult );
        }

        infoOut.nFileCount = nFileCount;
        infoOut.priority = mount.priority;
        infoOut.mounted = true;
        return fs_error_t::OK;
    }

    if ( !CopyString( infoOut.szPackagePath, sizeof( infoOut.szPackagePath ), szPackagePath ) ) {
        return fs_error_t::ERR_BUFFER_TOO_SMALL;
    }
    infoOut.mounted = false;
    return fs_error_t::ERR_MOUNT_NOT_FOUND;
}

bool CypherFileSystem_PackageIsMounted( const char *szPackagePath )
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    if ( !CypherFileSystem_RuntimeState().initialized || szPackagePath == nullptr || szPackagePath[0] == '\0' ) {
        return false;
    }

    const runtime_state_t &state = CypherFileSystem_RuntimeState();
    for ( common::u32 i = 0u; i < state.nMountCount; ++i ) {
        const mount_t &mount = state.mounts[i];
        if ( mount.type == mount_type_t::CYPHER_FILESYSTEM_PACKAGE &&
             std::strcmp( mount.szPhysicalRoot, szPackagePath ) == 0 ) {
            return true;
        }
    }

    return false;
}

}       // namespace cypher::engine::fs
