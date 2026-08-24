//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherFileSystem/CypherFileSystem_Smoke.cpp
//  Purpose: Tests FileSystem Smoke behavior.
//  Details: This test file guards expected behavior for the corresponding runtime
//           module. It should prefer focused edge cases over broad demonstrations.
//
//  History:
//  - Created by Karlo Siric on 2026-07-03
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherFileSystem.h"
#include "CypherPak.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = cypher::engine::fs;
namespace pak = cypher::engine;

namespace {

bool Check( bool condition, const char *message )
{
    if ( condition ) {
        return true;
    }

    std::fprintf( stderr, "filesystem smoke failed: %s\n", message );
    return false;
}

bool CheckError( fs::fs_error_t actual, fs::fs_error_t expected, const char *message )
{
    if ( actual == expected ) {
        return true;
    }

    std::fprintf(
        stderr,
        "filesystem smoke failed: %s: expected %s, got %s\n",
        message,
        fs::CypherFileSystem_ErrorName( expected ),
        fs::CypherFileSystem_ErrorName( actual ) );
    return false;
}

void TraceStep( const char *message )
{
    std::fprintf( stderr, "filesystem smoke step: %s\n", message );
    std::fflush( stderr );
}

bool WritePhysicalTextFile( const std::filesystem::path &path, const char *text )
{
    std::error_code ec{};
    std::filesystem::create_directories( path.parent_path(), ec );
    if ( ec ) {
        return false;
    }

    std::FILE *file = std::fopen( path.string().c_str(), "wb" );
    if ( file == nullptr ) {
        return false;
    }

    const std::size_t nTextLen = std::strlen( text );
    const bool bWriteOk = std::fwrite( text, 1u, nTextLen, file ) == nTextLen;
    const bool bCloseOk = std::fclose( file ) == 0;
    return bWriteOk && bCloseOk;
}

bool HasDirectoryEntry(
    const fs::directory_entry_t *entries,
    const cypher::engine::common::u32 nEntryCount,
    const char *szVirtualPath )
{
    for ( cypher::engine::common::u32 i = 0u; i < nEntryCount; ++i ) {
        if ( std::strcmp( entries[i].szVirtualPath, szVirtualPath ) == 0 ) {
            return true;
        }
    }

    return false;
}

bool HasWatchEvent(
    const fs::watch_event_t *events,
    const cypher::engine::common::u32 nEventCount,
    const fs::watch_event_type_t type,
    const char *szVirtualPath )
{
    for ( cypher::engine::common::u32 i = 0u; i < nEventCount; ++i ) {
        if ( events[i].type == type && std::strcmp( events[i].szVirtualPath, szVirtualPath ) == 0 ) {
            return true;
        }
    }

    return false;
}

bool TraceResolvedThroughMount( const fs::resolve_trace_t &trace, const fs::mount_handle_t mount )
{
    if ( !trace.resolved ) {
        return false;
    }

    for ( cypher::engine::common::u32 i = 0u; i < trace.nCheckedMountCount; ++i ) {
        const fs::resolve_trace_entry_t &entry = trace.entries[i];
        if ( entry.mount == mount && entry.bRootMatched && entry.bPathExists ) {
            return true;
        }
    }

    return false;
}

}       // namespace

int main()
{
    TraceStep( "setup temp root" );

    const std::filesystem::path szRootPath =
        std::filesystem::temp_directory_path() / "CypherFileSystem_Smoke";

    std::error_code ec{};
    std::filesystem::remove_all( szRootPath, ec );
    std::filesystem::create_directories( szRootPath, ec );

    if ( ec ) {
        std::fprintf( stderr, "filesystem smoke failed: temp directory setup failed\n" );
        return 1;
    }

    const std::string szWritePath = szRootPath.string();

    TraceStep( "init and path policy" );

    if ( !CheckError( fs::CypherFileSystem_Init(), fs::fs_error_t::OK, "init" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_SetWritePath( szWritePath.c_str() ), fs::fs_error_t::OK, "set write path" ) ) {
        return 1;
    }

    char szPathBuffer[fs::CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    if ( !CheckError( fs::CypherFileSystem_NormalizeVirtualPath( "Profiles\\Player1\\CONFIG.CFG", szPathBuffer, sizeof( szPathBuffer ) ), fs::fs_error_t::OK, "normalize virtual path" ) ) {
        return 1;
    }
    if ( !Check( std::strcmp( szPathBuffer, "profiles/player1/config.cfg" ) == 0, "normalized path value" ) ) {
        return 1;
    }
    if ( !Check( fs::CypherFileSystem_IsValidVirtualPath( "textures/walls/stone.dds" ), "valid virtual path" ) ) {
        return 1;
    }
    struct invalid_virtual_path_case_t {
        const char *path;
        const char *message;
    };
    const invalid_virtual_path_case_t invalidVirtualPaths[] = {
        { "/absolute/path.cfg", "invalid absolute virtual path" },
        { "C:/absolute/path.cfg", "invalid drive-letter virtual path" },
        { "profiles/../secret.txt", "invalid parent traversal virtual path" }
    };
    for ( const invalid_virtual_path_case_t &invalidPath : invalidVirtualPaths ) {
        if ( !Check( !fs::CypherFileSystem_IsValidVirtualPath( invalidPath.path ), invalidPath.message ) ) {
            return 1;
        }
        if ( !CheckError( fs::CypherFileSystem_NormalizeVirtualPath( invalidPath.path, szPathBuffer, sizeof( szPathBuffer ) ), fs::fs_error_t::ERR_INVALID_PATH, invalidPath.message ) ) {
            return 1;
        }
    }
    if ( !CheckError( fs::CypherFileSystem_PathJoin( "profiles", "Player1\\CONFIG.CFG", szPathBuffer, sizeof( szPathBuffer ) ), fs::fs_error_t::OK, "path join" ) ) {
        return 1;
    }
    if ( !Check( std::strcmp( szPathBuffer, "profiles/player1/config.cfg" ) == 0, "path join value" ) ) {
        return 1;
    }
    if ( !Check( std::strcmp( fs::CypherFileSystem_PathBasename( "profiles/player1/config.cfg" ), "config.cfg" ) == 0, "path basename" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_PathDirname( "Profiles\\Player1\\CONFIG.CFG", szPathBuffer, sizeof( szPathBuffer ) ), fs::fs_error_t::OK, "path dirname" ) ) {
        return 1;
    }
    if ( !Check( std::strcmp( szPathBuffer, "profiles/player1" ) == 0, "path dirname value" ) ) {
        return 1;
    }
    if ( !Check( std::strcmp( fs::CypherFileSystem_PathExtension( "profiles/player1/config.cfg" ), ".cfg" ) == 0, "path extension" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_PathWithoutExtension( "Profiles\\Player1\\CONFIG.CFG", szPathBuffer, sizeof( szPathBuffer ) ), fs::fs_error_t::OK, "path without extension" ) ) {
        return 1;
    }
    if ( !Check( std::strcmp( szPathBuffer, "profiles/player1/config" ) == 0, "path without extension value" ) ) {
        return 1;
    }
    if ( !Check( fs::CypherFileSystem_PathHasExtension( "profiles/player1/config.cfg", "CFG" ), "path has extension" ) ) {
        return 1;
    }

    TraceStep( "basic write and read mount" );

    if ( !CheckError( fs::CypherFileSystem_CreateDirectory( "profiles/player1" ), fs::fs_error_t::OK, "create directory" ) ) {
        return 1;
    }

    fs::file_t file{};
    if ( !CheckError( fs::CypherFileSystem_Open( "profiles/player1/config.cfg", fs::open_mode_t::WRITE_TEXT, file ), fs::fs_error_t::OK, "open write" ) ) {
        return 1;
    }

    const char text[] = "name player1\n";
    cypher::engine::common::u64 nBytesWritten = 0u;
    if ( !CheckError( fs::CypherFileSystem_Write( file, text, std::strlen( text ), nBytesWritten ), fs::fs_error_t::OK, "write file" ) ) {
        return 1;
    }
    if ( !Check( nBytesWritten == std::strlen( text ), "write count" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_Close( file ), fs::fs_error_t::OK, "close file" ) ) {
        return 1;
    }

    fs::mount_handle_t readMount = fs::CYPHER_FILESYSTEM_INVALID_MOUNT;
    fs::mount_handle_t hWritableMount = 123u;
    if ( !CheckError( fs::CypherFileSystem_MountDirectoryWithHandle( "writable", szWritePath.c_str(), fs::CYPHER_FILESYSTEM_MOUNT_WRITABLE, 0u, hWritableMount ), fs::fs_error_t::ERR_NOT_IMPLEMENTED, "writable directory mount not implemented" ) ) {
        return 1;
    }
    if ( !Check( hWritableMount == fs::CYPHER_FILESYSTEM_INVALID_MOUNT, "writable directory mount leaves handle invalid" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_MountDirectory( "mixed", szWritePath.c_str(), fs::CYPHER_FILESYSTEM_MOUNT_READ_ONLY | fs::CYPHER_FILESYSTEM_MOUNT_WRITABLE, 0u ), fs::fs_error_t::ERR_NOT_IMPLEMENTED, "mixed writable directory mount not implemented" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_MountDirectoryWithHandle( "", szWritePath.c_str(), fs::CYPHER_FILESYSTEM_MOUNT_READ_ONLY, 0u, readMount ), fs::fs_error_t::OK, "mount write path for read view" ) ) {
        return 1;
    }
    if ( !Check( readMount != fs::CYPHER_FILESYSTEM_INVALID_MOUNT, "mount handle assigned" ) ) {
        return 1;
    }
    fs::mount_info_t mountInfo{};
    if ( !CheckError( fs::CypherFileSystem_GetMountInfoByHandle( readMount, mountInfo ), fs::fs_error_t::OK, "get mount info by handle" ) ) {
        return 1;
    }
    if ( !Check( mountInfo.handle == readMount && mountInfo.priority == 0u, "mount info values" ) ) {
        return 1;
    }

    TraceStep( "directory overlay mounts" );

    const std::filesystem::path szBaseContentPath = szRootPath / "BaseContent";
    const std::filesystem::path szModContentPath = szRootPath / "ModContent";
    if ( !Check( WritePhysicalTextFile( szBaseContentPath / "shared.cfg", "base\n" ), "write base overlay file" ) ) {
        return 1;
    }
    if ( !Check( WritePhysicalTextFile( szBaseContentPath / "base_only.cfg", "base\n" ), "write base-only overlay file" ) ) {
        return 1;
    }
    if ( !Check( WritePhysicalTextFile( szModContentPath / "shared.cfg", "mod\n" ), "write mod overlay file" ) ) {
        return 1;
    }
    if ( !Check( WritePhysicalTextFile( szModContentPath / "mod_only.cfg", "mod\n" ), "write mod-only overlay file" ) ) {
        return 1;
    }

    fs::mount_handle_t pBaseMount = fs::CYPHER_FILESYSTEM_INVALID_MOUNT;
    if ( !CheckError( fs::CypherFileSystem_MountDirectoryWithHandle( "game", szBaseContentPath.string().c_str(), fs::CYPHER_FILESYSTEM_MOUNT_READ_ONLY, 1u, pBaseMount ), fs::fs_error_t::OK, "mount base overlay" ) ) {
        return 1;
    }
    fs::mount_handle_t modMount = fs::CYPHER_FILESYSTEM_INVALID_MOUNT;
    if ( !CheckError( fs::CypherFileSystem_MountDirectoryWithHandle( "game", szModContentPath.string().c_str(), fs::CYPHER_FILESYSTEM_MOUNT_READ_ONLY, 2u, modMount ), fs::fs_error_t::OK, "mount mod overlay" ) ) {
        return 1;
    }

    if ( !CheckError( fs::CypherFileSystem_ResolvePath( "game/shared.cfg", szPathBuffer, sizeof( szPathBuffer ) ), fs::fs_error_t::OK, "resolve overlay priority" ) ) {
        return 1;
    }
    if ( !Check( std::filesystem::equivalent( std::filesystem::path( szPathBuffer ), szModContentPath / "shared.cfg", ec ), "overlay priority chooses highest mount" ) ) {
        return 1;
    }
    if ( !Check( szPathBuffer[0] != '\0', "resolve loose overlay returns physical path" ) ) {
        return 1;
    }

    fs::directory_entry_t pOverlayEntries[8]{};
    cypher::engine::common::u32 nOverlayEntryCount = 0u;
    if ( !CheckError( fs::CypherFileSystem_ListDirectory( "game", pOverlayEntries, 8u, nOverlayEntryCount ), fs::fs_error_t::OK, "list overlay directory" ) ) {
        return 1;
    }
    if ( !Check( nOverlayEntryCount == 3u, "overlay list suppresses duplicate names" ) ) {
        return 1;
    }
    if ( !Check( HasDirectoryEntry( pOverlayEntries, nOverlayEntryCount, "game/shared.cfg" ), "overlay list has shared file" ) ) {
        return 1;
    }
    if ( !Check( HasDirectoryEntry( pOverlayEntries, nOverlayEntryCount, "game/base_only.cfg" ), "overlay list has base-only file" ) ) {
        return 1;
    }
    if ( !Check( HasDirectoryEntry( pOverlayEntries, nOverlayEntryCount, "game/mod_only.cfg" ), "overlay list has mod-only file" ) ) {
        return 1;
    }

    if ( !CheckError( fs::CypherFileSystem_Unmount( modMount ), fs::fs_error_t::OK, "unmount overlay by handle" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_GetMountInfoByHandle( modMount, mountInfo ), fs::fs_error_t::ERR_MOUNT_NOT_FOUND, "unmounted handle not found" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_ResolvePath( "game/shared.cfg", szPathBuffer, sizeof( szPathBuffer ) ), fs::fs_error_t::OK, "resolve overlay after unmount" ) ) {
        return 1;
    }
    if ( !Check( std::filesystem::equivalent( std::filesystem::path( szPathBuffer ), szBaseContentPath / "shared.cfg", ec ), "overlay falls back after unmount" ) ) {
        return 1;
    }

    TraceStep( "package overlay" );

    const std::filesystem::path szPackageSourcePath = szRootPath / "PackageSource";
    const std::filesystem::path szPackageSharedPath = szPackageSourcePath / "Shared.cfg";
    const std::filesystem::path szPackageOnlyPath = szPackageSourcePath / "PackageOnly.cfg";
    const std::filesystem::path szPackageScriptPath = szPackageSourcePath / "Init.cfg";
    const std::filesystem::path szPackageArchivePath = szRootPath / "game.cypak";
    const std::string szPackageSharedPathString = szPackageSharedPath.string();
    const std::string szPackageOnlyPathString = szPackageOnlyPath.string();
    const std::string szPackageScriptPathString = szPackageScriptPath.string();
    const std::string szPackageArchivePathString = szPackageArchivePath.string();
    const char szPackageSharedText[] = "package\n";
    const char szPackageOnlyText[] = "package_only\n";
    const char szPackageScriptText[] = "exec autoexec\n";

    if ( !Check( WritePhysicalTextFile( szPackageSharedPath, szPackageSharedText ), "write package shared source" ) ) {
        return 1;
    }
    if ( !Check( WritePhysicalTextFile( szPackageOnlyPath, szPackageOnlyText ), "write package-only source" ) ) {
        return 1;
    }
    if ( !Check( WritePhysicalTextFile( szPackageScriptPath, szPackageScriptText ), "write package script source" ) ) {
        return 1;
    }

    const pak::pak_source_file_t packageFiles[] = {
        { "shared.cfg", szPackageSharedPathString.c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE },
        { "package_only.cfg", szPackageOnlyPathString.c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE },
        { "scripts/init.cfg", szPackageScriptPathString.c_str(), pak::pak_compression_t::NONE, pak::CYPHER_PAK_ENTRY_NONE }
    };
    pak::pak_writer_config_t packageConfig{};
    packageConfig.szArchivePath = szPackageArchivePathString.c_str();
    if ( pak::Pak_CreateArchive( packageConfig, packageFiles, 3u ) != pak::pak_error_t::OK ) {
        std::fprintf( stderr, "filesystem smoke failed: create package archive\n" );
        return 1;
    }

    if ( !CheckError( fs::CypherFileSystem_MountPackage( "game", szPackageArchivePathString.c_str(), fs::CYPHER_FILESYSTEM_MOUNT_READ_ONLY, 3u ), fs::fs_error_t::OK, "mount package overlay" ) ) {
        return 1;
    }
    if ( !Check( fs::CypherFileSystem_PackageIsMounted( szPackageArchivePathString.c_str() ), "package is mounted" ) ) {
        return 1;
    }
    fs::package_info_t packageInfo{};
    if ( !CheckError( fs::CypherFileSystem_GetPackageInfo( szPackageArchivePathString.c_str(), packageInfo ), fs::fs_error_t::OK, "get package info" ) ) {
        return 1;
    }
    if ( !Check( packageInfo.mounted && packageInfo.nFileCount == 3u && packageInfo.priority == 3u, "package info values" ) ) {
        return 1;
    }

    if ( !CheckError( fs::CypherFileSystem_ResolvePath( "game/shared.cfg", szPathBuffer, sizeof( szPathBuffer ) ), fs::fs_error_t::ERR_UNSUPPORTED_BACKEND, "resolve package overlay unsupported" ) ) {
        return 1;
    }
    if ( !Check( szPathBuffer[0] == '\0', "resolve package overlay has no physical path" ) ) {
        return 1;
    }

    char pPackageReadBuffer[64]{};
    cypher::engine::common::u64 nPackageBytesRead = 0u;
    if ( !CheckError( fs::CypherFileSystem_ReadEntireFile( "game/shared.cfg", pPackageReadBuffer, sizeof( pPackageReadBuffer ), nPackageBytesRead ), fs::fs_error_t::OK, "read package overlay file" ) ) {
        return 1;
    }
    if ( !Check( nPackageBytesRead == std::strlen( szPackageSharedText ) && std::memcmp( pPackageReadBuffer, szPackageSharedText, std::strlen( szPackageSharedText ) ) == 0, "package overlay wins read" ) ) {
        return 1;
    }

    fs::file_t packageFile{};
    if ( !CheckError( fs::CypherFileSystem_Open( "game/shared.cfg", fs::open_mode_t::READ_BINARY, packageFile ), fs::fs_error_t::OK, "open package file" ) ) {
        return 1;
    }
    if ( !Check( packageFile.backend == fs::file_backend_t::PACKAGE_FILE && packageFile.readable && !packageFile.writable && packageFile.size == std::strlen( szPackageSharedText ), "package file handle values" ) ) {
        return 1;
    }
    cypher::engine::common::u64 nPackagePosition = 1u;
    if ( !CheckError( fs::CypherFileSystem_Tell( packageFile, nPackagePosition ), fs::fs_error_t::OK, "tell package file start" ) ) {
        return 1;
    }
    if ( !Check( nPackagePosition == 0u, "package file starts at zero" ) ) {
        return 1;
    }
    char pPackageHandleBuffer[16]{};
    if ( !CheckError( fs::CypherFileSystem_Read( packageFile, pPackageHandleBuffer, 4u, nPackageBytesRead ), fs::fs_error_t::OK, "read package file prefix" ) ) {
        return 1;
    }
    if ( !Check( nPackageBytesRead == 4u && std::memcmp( pPackageHandleBuffer, "pack", 4u ) == 0, "package file prefix bytes" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_Tell( packageFile, nPackagePosition ), fs::fs_error_t::OK, "tell package file after read" ) ) {
        return 1;
    }
    if ( !Check( nPackagePosition == 4u, "package file cursor advances" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_Seek( packageFile, 5, fs::seek_origin_t::CYPHER_FILESYSTEM_SEEK_START ), fs::fs_error_t::OK, "seek package file near end" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_Read( packageFile, pPackageHandleBuffer, sizeof( pPackageHandleBuffer ), nPackageBytesRead ), fs::fs_error_t::OK, "read package file short at eof" ) ) {
        return 1;
    }
    if ( !Check( nPackageBytesRead == std::strlen( szPackageSharedText ) - 5u && std::memcmp( pPackageHandleBuffer, szPackageSharedText + 5u, static_cast<std::size_t>( nPackageBytesRead ) ) == 0, "package file short eof read" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_Tell( packageFile, nPackagePosition ), fs::fs_error_t::OK, "tell package file eof" ) ) {
        return 1;
    }
    if ( !Check( nPackagePosition == std::strlen( szPackageSharedText ), "package file cursor reaches eof" ) ) {
        return 1;
    }
    pPackageHandleBuffer[0] = 'x';
    if ( !CheckError( fs::CypherFileSystem_Read( packageFile, pPackageHandleBuffer, sizeof( pPackageHandleBuffer ), nPackageBytesRead ), fs::fs_error_t::OK, "read package file at eof" ) ) {
        return 1;
    }
    if ( !Check( nPackageBytesRead == 0u && pPackageHandleBuffer[0] == 'x', "package file eof read returns zero bytes" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_Read( packageFile, nullptr, 0u, nPackageBytesRead ), fs::fs_error_t::OK, "zero read package file" ) ) {
        return 1;
    }
    if ( !Check( nPackageBytesRead == 0u, "package file zero read count" ) ) {
        return 1;
    }
    cypher::engine::common::u64 nPackageBytesWritten = 1u;
    if ( !CheckError( fs::CypherFileSystem_Write( packageFile, "x", 1u, nPackageBytesWritten ), fs::fs_error_t::ERR_PERMISSION_DENIED, "write package file denied" ) ) {
        return 1;
    }
    if ( !Check( nPackageBytesWritten == 0u, "package file denied write count" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_Flush( packageFile ), fs::fs_error_t::ERR_PERMISSION_DENIED, "flush package file denied" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_Close( packageFile ), fs::fs_error_t::OK, "close package file" ) ) {
        return 1;
    }

    fs::file_info_t packageFileInfo{};
    if ( !CheckError( fs::CypherFileSystem_GetFileInfo( "game/shared.cfg", packageFileInfo ), fs::fs_error_t::OK, "package file info" ) ) {
        return 1;
    }
    if ( !Check( packageFileInfo.exists && packageFileInfo.bIsPackageFile && packageFileInfo.backend == fs::file_backend_t::PACKAGE_FILE, "package file info values" ) ) {
        return 1;
    }
    if ( !Check( fs::CypherFileSystem_DirectoryExists( "game/scripts" ), "package virtual directory exists" ) ) {
        return 1;
    }

    if ( !CheckError( fs::CypherFileSystem_ListDirectory( "game", pOverlayEntries, 8u, nOverlayEntryCount ), fs::fs_error_t::OK, "list package overlay directory" ) ) {
        return 1;
    }
    if ( !Check( nOverlayEntryCount == 4u, "package overlay list values" ) ) {
        return 1;
    }
    if ( !Check( HasDirectoryEntry( pOverlayEntries, nOverlayEntryCount, "game/package_only.cfg" ), "package list has package-only file" ) ) {
        return 1;
    }
    if ( !Check( HasDirectoryEntry( pOverlayEntries, nOverlayEntryCount, "game/scripts" ), "package list has scripts directory" ) ) {
        return 1;
    }

    fs::directory_entry_t pPackageFindEntries[8]{};
    cypher::engine::common::u32 nPackageFindCount = 0u;
    if ( !CheckError( fs::CypherFileSystem_FindFiles( "game", "*.cfg", fs::CYPHER_FILESYSTEM_FIND_RECURSIVE | fs::CYPHER_FILESYSTEM_FIND_FILES | fs::CYPHER_FILESYSTEM_FIND_SORT_BY_NAME, pPackageFindEntries, 8u, nPackageFindCount ), fs::fs_error_t::OK, "find files through package overlay" ) ) {
        return 1;
    }
    if ( !Check( nPackageFindCount == 4u, "package recursive find count" ) ) {
        return 1;
    }

    if ( !CheckError( fs::CypherFileSystem_CopyFile( "game/package_only.cfg", "profiles/player1/package-copy.cfg" ), fs::fs_error_t::OK, "copy package file to write path" ) ) {
        return 1;
    }
    if ( !Check( fs::CypherFileSystem_FileExists( "profiles/player1/package-copy.cfg" ), "copied package file exists" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_DeleteFile( "profiles/player1/package-copy.cfg" ), fs::fs_error_t::OK, "delete copied package file" ) ) {
        return 1;
    }

    if ( !CheckError( fs::CypherFileSystem_UnmountPackage( szPackageArchivePathString.c_str() ), fs::fs_error_t::OK, "unmount package" ) ) {
        return 1;
    }
    if ( !Check( !fs::CypherFileSystem_PackageIsMounted( szPackageArchivePathString.c_str() ), "package is unmounted" ) ) {
        return 1;
    }
    pPackageReadBuffer[0] = '\0';
    if ( !CheckError( fs::CypherFileSystem_ReadEntireFile( "game/shared.cfg", pPackageReadBuffer, sizeof( pPackageReadBuffer ), nPackageBytesRead ), fs::fs_error_t::OK, "read loose fallback after package unmount" ) ) {
        return 1;
    }
    if ( !Check( nPackageBytesRead == 5u && std::memcmp( pPackageReadBuffer, "base\n", 5u ) == 0, "loose fallback after package unmount" ) ) {
        return 1;
    }

    TraceStep( "loose file discovery" );

    if ( !Check( fs::CypherFileSystem_Exists( "profiles/player1/config.cfg" ), "file exists through mount" ) ) {
        return 1;
    }
    if ( !Check( fs::CypherFileSystem_FileExists( "profiles/player1/config.cfg" ), "file exists as file" ) ) {
        return 1;
    }
    if ( !Check( fs::CypherFileSystem_DirectoryExists( "profiles/player1" ), "directory exists through mount" ) ) {
        return 1;
    }

    fs::resolve_trace_t trace{};
    if ( !CheckError( fs::CypherFileSystem_TraceResolve( "profiles/player1/config.cfg", trace ), fs::fs_error_t::OK, "trace resolve" ) ) {
        return 1;
    }
    if ( !Check( TraceResolvedThroughMount( trace, readMount ), "trace resolve values" ) ) {
        return 1;
    }

    fs::file_info_t info{};
    if ( !CheckError( fs::CypherFileSystem_GetFileInfo( "profiles/player1/config.cfg", info ), fs::fs_error_t::OK, "get file info" ) ) {
        return 1;
    }
    if ( !Check( info.exists && !info.bIsDirectory && info.nFileSize == std::strlen( text ), "file info values" ) ) {
        return 1;
    }

    fs::directory_entry_t entries[8]{};
    cypher::engine::common::u32 nEntryCount = 0u;
    if ( !CheckError( fs::CypherFileSystem_ListDirectory( "profiles/player1", entries, 8u, nEntryCount ), fs::fs_error_t::OK, "list directory" ) ) {
        return 1;
    }
    if ( !Check( nEntryCount == 1u && std::strcmp( entries[0].name, "config.cfg" ) == 0, "list directory values" ) ) {
        return 1;
    }

    if ( !CheckError( fs::CypherFileSystem_FindFiles( "profiles", "*.cfg", fs::CYPHER_FILESYSTEM_FIND_RECURSIVE | fs::CYPHER_FILESYSTEM_FIND_FILES | fs::CYPHER_FILESYSTEM_FIND_SORT_BY_NAME, entries, 8u, nEntryCount ), fs::fs_error_t::OK, "find files" ) ) {
        return 1;
    }
    if ( !Check( nEntryCount == 1u && std::strcmp( entries[0].szVirtualPath, "profiles/player1/config.cfg" ) == 0, "find files values" ) ) {
        return 1;
    }

    if ( !CheckError( fs::CypherFileSystem_CreateDirectory( "bulk" ), fs::fs_error_t::OK, "create bulk directory" ) ) {
        return 1;
    }
    for ( cypher::engine::common::u32 i = 0u; i < 270u; ++i ) {
        char szBulkFile[64]{};
        std::snprintf( szBulkFile, sizeof( szBulkFile ), "bulk/file%03u.tmp", i );
        if ( !CheckError( fs::CypherFileSystem_WriteEntireFile( szBulkFile, text, std::strlen( text ) ), fs::fs_error_t::OK, "write bulk file" ) ) {
            return 1;
        }
    }

    fs::directory_entry_t pBulkEntries[300]{};
    cypher::engine::common::u32 nBulkEntryCount = 0u;
    if ( !CheckError( fs::CypherFileSystem_FindFiles( "bulk", "*.tmp", fs::CYPHER_FILESYSTEM_FIND_FILES, pBulkEntries, 300u, nBulkEntryCount ), fs::fs_error_t::OK, "find bulk files" ) ) {
        return 1;
    }
    if ( !Check( nBulkEntryCount == 270u, "find files sees directories larger than scratch buffer" ) ) {
        return 1;
    }

    TraceStep( "loose file operations" );

    const char szExtraText[] = "rate 1\n";
    if ( !CheckError( fs::CypherFileSystem_AppendEntireFile( "profiles/player1/config.cfg", szExtraText, std::strlen( szExtraText ) ), fs::fs_error_t::OK, "append entire file" ) ) {
        return 1;
    }

    char pReadBuffer[64]{};
    cypher::engine::common::u64 nBytesRead = 0u;
    if ( !CheckError( fs::CypherFileSystem_ReadEntireFile( "profiles/player1/config.cfg", pReadBuffer, sizeof( pReadBuffer ), nBytesRead ), fs::fs_error_t::OK, "read entire file" ) ) {
        return 1;
    }
    if ( !Check( nBytesRead == std::strlen( text ) + std::strlen( szExtraText ), "read entire byte count" ) ) {
        return 1;
    }

    if ( !CheckError( fs::CypherFileSystem_CopyFile( "profiles/player1/config.cfg", "profiles/player1/config-copy.cfg" ), fs::fs_error_t::OK, "copy file" ) ) {
        return 1;
    }
    if ( !Check( fs::CypherFileSystem_FileExists( "profiles/player1/config-copy.cfg" ), "copied file exists" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_DeleteFile( "profiles/player1/config-copy.cfg" ), fs::fs_error_t::OK, "delete copied file" ) ) {
        return 1;
    }

    if ( !CheckError( fs::CypherFileSystem_CreateDirectory( "profiles/player2/nested" ), fs::fs_error_t::OK, "create recursive directory" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_WriteEntireFile( "profiles/player2/nested/temp.cfg", text, std::strlen( text ) ), fs::fs_error_t::OK, "write entire file" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_RemoveDirectoryTree( "profiles/player2" ), fs::fs_error_t::OK, "remove directory tree" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_RemoveDirectoryTree( "bulk" ), fs::fs_error_t::OK, "remove bulk directory" ) ) {
        return 1;
    }

    fs::stats_t stats{};
    if ( !CheckError( fs::CypherFileSystem_GetStats( stats ), fs::fs_error_t::OK, "get stats" ) ) {
        return 1;
    }
    if ( !Check( stats.nOpenCount > 0u && stats.nCloseCount > 0u && stats.nBytesWritten > 0u, "stats values" ) ) {
        return 1;
    }

    TraceStep( "async read" );

    std::memset( pReadBuffer, 0, sizeof( pReadBuffer ) );

    fs::async_request_t request = fs::CYPHER_FILESYSTEM_INVALID_ASYNC_REQUEST;
    if ( !CheckError( fs::CypherFileSystem_ReadAsync( "profiles/player1/config.cfg", pReadBuffer, sizeof( pReadBuffer ), request ), fs::fs_error_t::OK, "read async" ) ) {
        return 1;
    }
    if ( !Check( request != fs::CYPHER_FILESYSTEM_INVALID_ASYNC_REQUEST, "read async handle assigned" ) ) {
        return 1;
    }
    fs::async_result_t asyncResult{};
    if ( !CheckError( fs::CypherFileSystem_WaitAsync( request, asyncResult ), fs::fs_error_t::OK, "wait read async" ) ) {
        return 1;
    }
    if ( !Check( asyncResult.status == fs::async_status_t::COMPLETE && asyncResult.error == fs::fs_error_t::OK, "read async completed" ) ) {
        return 1;
    }
    if ( !Check( asyncResult.nBytesTransferred == nBytesRead && std::memcmp( pReadBuffer, text, std::strlen( text ) ) == 0, "read async bytes" ) ) {
        return 1;
    }

    TraceStep( "async write" );

    const char szAsyncText[] = "async write\n";
    fs::async_request_t writeRequest = fs::CYPHER_FILESYSTEM_INVALID_ASYNC_REQUEST;
    if ( !CheckError( fs::CypherFileSystem_WriteAsync( "profiles/player1/async.cfg", szAsyncText, std::strlen( szAsyncText ), writeRequest ), fs::fs_error_t::OK, "write async" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_WaitAsync( writeRequest, asyncResult ), fs::fs_error_t::OK, "wait write async" ) ) {
        return 1;
    }
    if ( !Check( asyncResult.status == fs::async_status_t::COMPLETE && asyncResult.error == fs::fs_error_t::OK && asyncResult.nBytesTransferred == std::strlen( szAsyncText ), "write async completed" ) ) {
        return 1;
    }
    std::memset( pReadBuffer, 0, sizeof( pReadBuffer ) );
    if ( !CheckError( fs::CypherFileSystem_ReadEntireFile( "profiles/player1/async.cfg", pReadBuffer, sizeof( pReadBuffer ), nBytesRead ), fs::fs_error_t::OK, "read async written file" ) ) {
        return 1;
    }
    if ( !Check( nBytesRead == std::strlen( szAsyncText ) && std::memcmp( pReadBuffer, szAsyncText, std::strlen( szAsyncText ) ) == 0, "async written file contents" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_DeleteFile( "profiles/player1/async.cfg" ), fs::fs_error_t::OK, "delete async written file" ) ) {
        return 1;
    }

    TraceStep( "watch setup" );

    fs::watch_handle_t watch = fs::CYPHER_FILESYSTEM_INVALID_WATCH;
    if ( !CheckError( fs::CypherFileSystem_WatchPath( "profiles/player1", fs::CYPHER_FILESYSTEM_WATCH_DIRECTORY, watch ), fs::fs_error_t::OK, "watch directory" ) ) {
        return 1;
    }
    if ( !Check( watch != fs::CYPHER_FILESYSTEM_INVALID_WATCH, "watch handle assigned" ) ) {
        return 1;
    }

    fs::watch_event_t events[8]{};
    cypher::engine::common::u32 nWatchEventCount = 0u;
    TraceStep( "watch poll unchanged" );
    if ( !CheckError( fs::CypherFileSystem_PollChanges( events, 8u, nWatchEventCount ), fs::fs_error_t::OK, "poll unchanged watch" ) ) {
        return 1;
    }
    TraceStep( "watch write file" );
    if ( !CheckError( fs::CypherFileSystem_WriteEntireFile( "profiles/player1/watched.cfg", text, std::strlen( text ) ), fs::fs_error_t::OK, "write watched file" ) ) {
        return 1;
    }
    TraceStep( "watch poll created" );
    if ( !CheckError( fs::CypherFileSystem_PollChanges( events, 8u, nWatchEventCount ), fs::fs_error_t::OK, "poll created watch file" ) ) {
        return 1;
    }
    if ( !Check( HasWatchEvent( events, nWatchEventCount, fs::watch_event_type_t::CREATED, "profiles/player1/watched.cfg" ), "watch reports created file" ) ) {
        return 1;
    }
    TraceStep( "watch delete file" );
    if ( !CheckError( fs::CypherFileSystem_DeleteFile( "profiles/player1/watched.cfg" ), fs::fs_error_t::OK, "delete watched file" ) ) {
        return 1;
    }
    TraceStep( "watch poll deleted" );
    if ( !CheckError( fs::CypherFileSystem_PollChanges( events, 8u, nWatchEventCount ), fs::fs_error_t::OK, "poll deleted watch file" ) ) {
        return 1;
    }
    if ( !Check( HasWatchEvent( events, nWatchEventCount, fs::watch_event_type_t::DELETED, "profiles/player1/watched.cfg" ), "watch reports deleted file" ) ) {
        return 1;
    }
    TraceStep( "watch unwatch" );
    if ( !CheckError( fs::CypherFileSystem_UnwatchPath( watch ), fs::fs_error_t::OK, "unwatch directory" ) ) {
        return 1;
    }

    TraceStep( "cleanup" );

    if ( !CheckError( fs::CypherFileSystem_MountPackage( "", "pak0.pak", fs::CYPHER_FILESYSTEM_MOUNT_READ_ONLY | fs::CYPHER_FILESYSTEM_MOUNT_OPTIONAL, 0u ), fs::fs_error_t::OK, "optional missing package mount" ) ) {
        return 1;
    }

    if ( !CheckError( fs::CypherFileSystem_Rename( "profiles/player1/config.cfg", "profiles/player1/config2.cfg" ), fs::fs_error_t::OK, "rename file" ) ) {
        return 1;
    }
    if ( !Check( fs::CypherFileSystem_Exists( "profiles/player1/config2.cfg" ), "renamed file exists through mount" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_DeleteFile( "profiles/player1/config2.cfg" ), fs::fs_error_t::OK, "delete file" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_RemoveDirectory( "profiles/player1" ), fs::fs_error_t::OK, "remove player directory" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_RemoveDirectory( "profiles" ), fs::fs_error_t::OK, "remove profiles directory" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_Unmount( pBaseMount ), fs::fs_error_t::OK, "unmount base overlay" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_Unmount( readMount ), fs::fs_error_t::OK, "unmount by handle" ) ) {
        return 1;
    }
    if ( !CheckError( fs::CypherFileSystem_Shutdown(), fs::fs_error_t::OK, "shutdown" ) ) {
        return 1;
    }

    std::filesystem::remove_all( szRootPath, ec );
    return 0;
}
