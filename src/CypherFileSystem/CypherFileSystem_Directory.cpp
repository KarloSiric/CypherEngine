//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherFileSystem/CypherFileSystem_Directory.cpp
//  Purpose: Implements the CypherFileSystem FileSystem Directory module.
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

#include <filesystem>
#include <limits>
#include <system_error>
#include <vector>

namespace cypher::engine::fs
{

/*
================
FS_CreateDirectory

Creates a directory below the configured write path. Existing directories are OK.
================
*/
fs_error_t FS_CreateDirectory( const char *szVirtualPath )
{
    std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
    char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    const fs_error_t buildResult = FS_BuildWritePath( szVirtualPath, szPhysicalPath, sizeof( szPhysicalPath ) );
    if ( buildResult != fs_error_t::OK ) {
        return buildResult;
    }

    std::error_code ec{};
    if ( std::filesystem::exists( szPhysicalPath, ec ) ) {
        if ( ec ) {
            return fs_error_t::ERR_IO_ERROR;
        }
        return std::filesystem::is_directory( szPhysicalPath, ec ) && !ec ? fs_error_t::OK : fs_error_t::ERR_NOT_DIRECTORY;
    }
    if ( ec ) {
        return fs_error_t::ERR_IO_ERROR;
    }

    std::filesystem::create_directories( szPhysicalPath, ec );
    if ( ec ) {
        return fs_error_t::ERR_IO_ERROR;
    }

    return fs_error_t::OK;
}

/*
================
FS_DeleteFile

Deletes a file below the configured write path. It never deletes mounted read-only content.
================
*/
fs_error_t FS_DeleteFile( const char *szVirtualPath )
{
    std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
    char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    const fs_error_t buildResult = FS_BuildWritePath( szVirtualPath, szPhysicalPath, sizeof( szPhysicalPath ) );
    if ( buildResult != fs_error_t::OK ) {
        return buildResult;
    }

    std::error_code ec{};
    if ( !std::filesystem::exists( szPhysicalPath, ec ) ) {
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_PATH_NOT_FOUND;
    }
    if ( std::filesystem::is_directory( szPhysicalPath, ec ) ) {
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_NOT_FILE;
    }

    const bool removed = std::filesystem::remove( szPhysicalPath, ec );
    if ( ec ) {
        return fs_error_t::ERR_IO_ERROR;
    }

    return removed ? fs_error_t::OK : fs_error_t::ERR_PATH_NOT_FOUND;
}

/*
================
FS_RemoveDirectory

Removes an empty directory below the configured write path.
================
*/
fs_error_t FS_RemoveDirectory( const char *szVirtualPath )
{
    std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
    char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    const fs_error_t buildResult = FS_BuildWritePath( szVirtualPath, szPhysicalPath, sizeof( szPhysicalPath ) );
    if ( buildResult != fs_error_t::OK ) {
        return buildResult;
    }

    std::error_code ec{};
    if ( !std::filesystem::exists( szPhysicalPath, ec ) ) {
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_PATH_NOT_FOUND;
    }
    if ( !std::filesystem::is_directory( szPhysicalPath, ec ) || ec ) {
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_NOT_DIRECTORY;
    }

    const bool removed = std::filesystem::remove( szPhysicalPath, ec );
    if ( ec == std::errc::directory_not_empty ) {
        return fs_error_t::ERR_DIRECTORY_NOT_EMPTY;
    }
    if ( ec ) {
        return fs_error_t::ERR_IO_ERROR;
    }

    return removed ? fs_error_t::OK : fs_error_t::ERR_DIRECTORY_NOT_EMPTY;
}

/*
================
FS_RemoveDirectoryTree

Recursively removes a directory below the configured write path.
================
*/
fs_error_t FS_RemoveDirectoryTree( const char *szVirtualPath )
{
    std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
    char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    const fs_error_t buildResult = FS_BuildWritePath( szVirtualPath, szPhysicalPath, sizeof( szPhysicalPath ) );
    if ( buildResult != fs_error_t::OK ) {
        return buildResult;
    }

    std::error_code ec{};
    if ( !std::filesystem::exists( szPhysicalPath, ec ) ) {
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_PATH_NOT_FOUND;
    }
    if ( !std::filesystem::is_directory( szPhysicalPath, ec ) || ec ) {
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_NOT_DIRECTORY;
    }

    std::filesystem::remove_all( szPhysicalPath, ec );
    return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::OK;
}

/*
================
FS_Rename

Renames a write-path file or directory. Destination must not already exist.
================
*/
fs_error_t FS_Rename( const char *szFromVirtualPath, const char *szToVirtualPath )
{
    std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
    char szFromPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szToPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};

    fs_error_t buildResult = FS_BuildWritePath( szFromVirtualPath, szFromPhysicalPath, sizeof( szFromPhysicalPath ) );
    if ( buildResult != fs_error_t::OK ) {
        return buildResult;
    }

    buildResult = FS_BuildWritePath( szToVirtualPath, szToPhysicalPath, sizeof( szToPhysicalPath ) );
    if ( buildResult != fs_error_t::OK ) {
        return buildResult;
    }

    std::error_code ec{};
    if ( !std::filesystem::exists( szFromPhysicalPath, ec ) ) {
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_PATH_NOT_FOUND;
    }
    if ( std::filesystem::exists( szToPhysicalPath, ec ) ) {
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_ALREADY_EXISTS;
    }

    const std::filesystem::path parent_path = std::filesystem::path( szToPhysicalPath ).parent_path();
    if ( !parent_path.empty() ) {
        std::filesystem::create_directories( parent_path, ec );
        if ( ec ) {
            return fs_error_t::ERR_IO_ERROR;
        }
    }

    std::filesystem::rename( szFromPhysicalPath, szToPhysicalPath, ec );
    return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::OK;
}

/*
================
FS_CopyFile

Copies from the readable virtual view into the configured write path.
================
*/
fs_error_t FS_CopyFile( const char *szFromVirtualPath, const char *szToVirtualPath )
{
    std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
    char szToPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};

    file_info_t szSourceInfo{};
    fs_error_t result = FS_GetFileInfo( szFromVirtualPath, szSourceInfo );
    if ( result != fs_error_t::OK ) {
        return result;
    }
    if ( szSourceInfo.bIsDirectory ) {
        return fs_error_t::ERR_NOT_FILE;
    }

    result = FS_BuildWritePath( szToVirtualPath, szToPhysicalPath, sizeof( szToPhysicalPath ) );
    if ( result != fs_error_t::OK ) {
        return result;
    }

    std::error_code ec{};
    if ( std::filesystem::exists( szToPhysicalPath, ec ) ) {
        return ec ? fs_error_t::ERR_IO_ERROR : fs_error_t::ERR_ALREADY_EXISTS;
    }

    const std::filesystem::path parent_path = std::filesystem::path( szToPhysicalPath ).parent_path();
    if ( !parent_path.empty() ) {
        std::filesystem::create_directories( parent_path, ec );
        if ( ec ) {
            return fs_error_t::ERR_IO_ERROR;
        }
    }

    if ( szSourceInfo.backend == file_backend_t::OS_FILE && szSourceInfo.szResolvedPath[0] != '\0' ) {
        const bool copied = std::filesystem::copy_file( szSourceInfo.szResolvedPath, szToPhysicalPath, std::filesystem::copy_options::none, ec );
        if ( ec ) {
            return fs_error_t::ERR_IO_ERROR;
        }
        return copied ? fs_error_t::OK : fs_error_t::ERR_FILE_WRITE_FAILED;
    }

    if ( szSourceInfo.nFileSize == 0u ) {
        return FS_WriteEntireFile( szToVirtualPath, nullptr, 0u );
    }

    if ( szSourceInfo.nFileSize > static_cast<common::u64>( std::numeric_limits<common::usize>::max() ) ) {
        return fs_error_t::ERR_OUT_OF_MEMORY;
    }

    std::vector<common::u8> buffer( static_cast<common::usize>( szSourceInfo.nFileSize ) );
    common::u64 nBytesRead = 0u;
    result = FS_ReadEntireFile( szFromVirtualPath, buffer.data(), szSourceInfo.nFileSize, nBytesRead );
    if ( result != fs_error_t::OK ) {
        return result;
    }
    if ( nBytesRead != szSourceInfo.nFileSize ) {
        return fs_error_t::ERR_FILE_READ_FAILED;
    }

    return FS_WriteEntireFile( szToVirtualPath, buffer.data(), nBytesRead );
}

/*
================
FS_DirectoryExists

Checks the readable mounted filesystem view.
================
*/
bool FS_DirectoryExists( const char *szVirtualPath )
{
    std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
    if ( !FS_RuntimeState().initialized ) {
        return false;
    }

    common::u32 nEntryCount = 0u;
    const fs_error_t result = FS_ListDirectory( szVirtualPath, nullptr, 0u, nEntryCount );
    return result == fs_error_t::OK || result == fs_error_t::ERR_BUFFER_TOO_SMALL;
}

}       // namespace cypher::engine::fs
