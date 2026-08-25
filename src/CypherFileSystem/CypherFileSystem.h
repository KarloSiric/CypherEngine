//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherFileSystem/CypherFileSystem.h
//  Purpose: Declares the CypherFileSystem FileSystem module.
//  Details: This file participates in the virtual filesystem layer that maps engine
//           paths to mounted physical or package-backed data. Keep path validation
//           strict because every asset pipeline will depend on it.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_FILESYSTEM_H
#define CYPHER_ENGINE_FILESYSTEM_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherFileSystem_Error.h"
#include "CypherFileSystem_Types.h"

namespace cypher::engine::fs
{

/*
================
Filesystem API

Mount-based virtual file system used by configs, shaders, assets, packages,
editor browsing, save data and future hot reload.
================
*/

/*
================
Lifecycle
================
*/
fs_error_t FS_Init();

fs_error_t FS_Shutdown();

bool FS_IsInitialized();

/*
================
Mounts

Reads search mounted content by priority. Higher priority wins; equal priority
keeps insertion order stable so overlays remain predictable.
================
*/
fs_error_t FS_MountDirectory(
    const char *pszVirtualRoot,
    const char *pszPhysicalPath,
    common::u32 nFlags,
    common::u32 nPriority );

fs_error_t FS_MountDirectoryWithHandle(
    const char *pszVirtualRoot,
    const char *pszPhysicalPath,
    common::u32 nFlags,
    common::u32 nPriority,
    mount_handle_t &hMountOut );

fs_error_t FS_UnmountDirectory( const char *pszVirtualRoot );

fs_error_t FS_Unmount( mount_handle_t hMount );

fs_error_t FS_MountPackage(
    const char *pszVirtualRoot,
    const char *pszPackagePath,
    common::u32 nFlags,
    common::u32 nPriority );

fs_error_t FS_UnmountPackage( const char *pszPackagePath );

common::u32 FS_MountCount();

fs_error_t FS_GetMountInfo(
    common::u32 iMount,
    mount_info_t &mountInfoOut );

fs_error_t FS_GetMountInfoByHandle(
    mount_handle_t hMount,
    mount_info_t &mountInfoOut );

/*
================
Path Policy

Virtual paths use forward slashes, are relative to virtual roots, reject '..',
reject absolute paths, reject drive letters and normalize asset paths to lower
case internally for cross-platform consistency.
================
*/
fs_error_t FS_NormalizeVirtualPath(
    const char *pszVirtualPath,
    char *pszOutPath,
    common::u32 nOutPathSize );

fs_error_t FS_NormalizeVirtualRoot(
    const char *pszVirtualRoot,
    char *pszOutRoot,
    common::u32 nOutRootSize );

bool FS_IsValidVirtualPath( const char *pszVirtualPath );

bool FS_VirtualPathStartsWithRoot(
    const char *pszVirtualPath,
    const char *pszVirtualRoot,
    const char **ppszRelativePathOut );

fs_error_t FS_BuildPhysicalPath(
    const char *pszPhysicalRoot,
    const char *pszRelativePath,
    char *pszOutPath,
    common::u32 nOutPathSize );

fs_error_t FS_ResolvePath(
    const char *pszVirtualPath,
    char *pszOutResolvedPath,
    common::u32 nOutResolvedPathSize );

fs_error_t FS_TraceResolve(
    const char *pszVirtualPath,
    resolve_trace_t &traceOut );

fs_error_t FS_PathJoin(
    const char *pszLeft,
    const char *pszRight,
    char *pszOutPath,
    common::u32 nOutPathSize );

const char *FS_PathBasename( const char *pszVirtualPath );

fs_error_t FS_PathDirname(
    const char *pszVirtualPath,
    char *pszOutPath,
    common::u32 nOutPathSize );

const char *FS_PathExtension( const char *pszVirtualPath );

fs_error_t FS_PathWithoutExtension(
    const char *pszVirtualPath,
    char *pszOutPath,
    common::u32 nOutPathSize );

bool FS_PathHasExtension(
    const char *pszVirtualPath,
    const char *pszExtension );

/*
================
Write Root

Writes, deletes, renames and generated data go through the write path. They do
not modify arbitrary read mounts.
================
*/
fs_error_t FS_SetWritePath( const char *pszPhysicalPath );

const char *FS_GetWritePath();

/*
================
File I/O
================
*/
fs_error_t FS_Open(
    const char *pszVirtualPath,
    open_mode_t mode,
    file_t &fileOut );

fs_error_t FS_Close( file_t &file );

fs_error_t FS_Read(
    file_t &file,
    void *pBuffer,
    common::u64 nBytesToRead,
    common::u64 &nBytesReadOut );

fs_error_t FS_Write(
    file_t &file,
    const void *pBuffer,
    common::u64 nBytesToWrite,
    common::u64 &nBytesWrittenOut );

fs_error_t FS_Seek(
    file_t &file,
    common::i64 nOffset,
    seek_origin_t origin );

fs_error_t FS_Tell(
    file_t &file,
    common::u64 &nPositionOut );

fs_error_t FS_Flush( file_t &file );

fs_error_t FS_ReadEntireFile(
    const char *pszVirtualPath,
    void *pBuffer,
    common::u64 nBytesToRead,
    common::u64 &nBytesReadOut );

fs_error_t FS_WriteEntireFile(
    const char *pszVirtualPath,
    const void *pBuffer,
    common::u64 nBytesToWrite );

fs_error_t FS_AppendEntireFile(
    const char *pszVirtualPath,
    const void *pBuffer,
    common::u64 nBytesToWrite );

/*
================
Write-Side Management

These APIs operate under the write path. RemoveDirectory is intentionally
non-recursive; destructive recursive removal is a separate explicit API.
================
*/
fs_error_t FS_CreateDirectory( const char *pszVirtualPath );

fs_error_t FS_DeleteFile( const char *pszVirtualPath );

fs_error_t FS_RemoveDirectory( const char *pszVirtualPath );

fs_error_t FS_RemoveDirectoryTree( const char *pszVirtualPath );

fs_error_t FS_Rename(
    const char *pszFromVirtualPath,
    const char *pszToVirtualPath );

fs_error_t FS_CopyFile(
    const char *pszFromVirtualPath,
    const char *pszToVirtualPath );

/*
================
Query And Discovery
================
*/
bool FS_Exists( const char *pszVirtualPath );

bool FS_FileExists( const char *pszVirtualPath );

bool FS_DirectoryExists( const char *pszVirtualPath );

fs_error_t FS_GetFileInfo(
    const char *pszVirtualPath,
    file_info_t &fileInfoOut );

fs_error_t FS_ListDirectory(
    const char *pszVirtualPath,
    directory_entry_t *pEntries,
    common::u32 nMaxEntries,
    common::u32 &nEntryCountOut );

fs_error_t FS_FindFiles(
    const char *pszVirtualRoot,
    const char *pszPattern,
    common::u32 nFlags,
    directory_entry_t *pEntries,
    common::u32 nMaxEntries,
    common::u32 &nEntryCountOut );

/*
================
Packages

Package APIs will back .pak/.zip/custom package mounts later. The public API is
declared now so engine code has a stable target surface.
================
*/
fs_error_t FS_GetPackageInfo(
    const char *pszPackagePath,
    package_info_t &packageInfoOut );

bool FS_PackageIsMounted( const char *pszPackagePath );

/*
================
Async And Streaming

The first implementation can be simple worker-thread file reads. Later this
becomes the streaming layer for textures, audio, levels and packages.
================
*/
fs_error_t FS_ReadAsync(
    const char *pszVirtualPath,
    void *pBuffer,
    common::u64 nBytesToRead,
    async_request_t &hRequestOut );

fs_error_t FS_WriteAsync(
    const char *pszVirtualPath,
    const void *pBuffer,
    common::u64 nBytesToWrite,
    async_request_t &hRequestOut );

fs_error_t FS_PollAsync(
    async_request_t hRequest,
    async_result_t &resultOut );

fs_error_t FS_WaitAsync(
    async_request_t hRequest,
    async_result_t &resultOut );

fs_error_t FS_CancelAsync( async_request_t hRequest );

/*
================
File Watching

Used by editor workflows and hot reload for shaders, configs and assets.
================
*/
fs_error_t FS_WatchPath(
    const char *pszVirtualPath,
    common::u32 nFlags,
    watch_handle_t &hWatchOut );

fs_error_t FS_UnwatchPath( watch_handle_t hWatch );

fs_error_t FS_PollChanges(
    watch_event_t *pEvents,
    common::u32 nMaxEvents,
    common::u32 &nEventCountOut );

/*
================
Diagnostics

Runtime visibility is mandatory for a professional filesystem: mount dumps,
lookup failures, bytes read/written and package/debug inspection.
================
*/
fs_error_t FS_GetStats( stats_t &statsOut );

fs_error_t FS_ResetStats();

fs_error_t FS_DumpMounts();

fs_error_t FS_DumpStats();

}       // namespace cypher::engine::fs

#endif // CYPHER_ENGINE_FILESYSTEM_H
