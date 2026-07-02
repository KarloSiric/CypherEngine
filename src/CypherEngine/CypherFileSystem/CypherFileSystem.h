//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherFileSystem/CypherFileSystem.h
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

#pragma once

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
fs_error_t CypherFileSystem_Init();

fs_error_t CypherFileSystem_Shutdown();

bool CypherFileSystem_IsInitialized();

/*
================
Mounts

Reads search mounted content by priority. Higher priority wins; equal priority
keeps insertion order stable so overlays remain predictable.
================
*/
fs_error_t CypherFileSystem_MountDirectory(
    const char *pszVirtualRoot,
    const char *pszPhysicalPath,
    common::u32 nFlags,
    common::u32 nPriority );

fs_error_t CypherFileSystem_MountDirectoryWithHandle(
    const char *pszVirtualRoot,
    const char *pszPhysicalPath,
    common::u32 nFlags,
    common::u32 nPriority,
    mount_handle_t &hMountOut );

fs_error_t CypherFileSystem_UnmountDirectory( const char *pszVirtualRoot );

fs_error_t CypherFileSystem_Unmount( mount_handle_t hMount );

fs_error_t CypherFileSystem_MountPackage(
    const char *pszVirtualRoot,
    const char *pszPackagePath,
    common::u32 nFlags,
    common::u32 nPriority );

fs_error_t CypherFileSystem_UnmountPackage( const char *pszPackagePath );

common::u32 CypherFileSystem_MountCount();

fs_error_t CypherFileSystem_GetMountInfo(
    common::u32 iMount,
    mount_info_t &mountInfoOut );

fs_error_t CypherFileSystem_GetMountInfoByHandle(
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
fs_error_t CypherFileSystem_NormalizeVirtualPath(
    const char *pszVirtualPath,
    char *pszOutPath,
    common::u32 nOutPathSize );

fs_error_t CypherFileSystem_NormalizeVirtualRoot(
    const char *pszVirtualRoot,
    char *pszOutRoot,
    common::u32 nOutRootSize );

bool CypherFileSystem_IsValidVirtualPath( const char *pszVirtualPath );

bool CypherFileSystem_VirtualPathStartsWithRoot(
    const char *pszVirtualPath,
    const char *pszVirtualRoot,
    const char **ppszRelativePathOut );

fs_error_t CypherFileSystem_BuildPhysicalPath(
    const char *pszPhysicalRoot,
    const char *pszRelativePath,
    char *pszOutPath,
    common::u32 nOutPathSize );

fs_error_t CypherFileSystem_ResolvePath(
    const char *pszVirtualPath,
    char *pszOutResolvedPath,
    common::u32 nOutResolvedPathSize );

fs_error_t CypherFileSystem_TraceResolve(
    const char *pszVirtualPath,
    resolve_trace_t &traceOut );

fs_error_t CypherFileSystem_PathJoin(
    const char *pszLeft,
    const char *pszRight,
    char *pszOutPath,
    common::u32 nOutPathSize );

const char *CypherFileSystem_PathBasename( const char *pszVirtualPath );

fs_error_t CypherFileSystem_PathDirname(
    const char *pszVirtualPath,
    char *pszOutPath,
    common::u32 nOutPathSize );

const char *CypherFileSystem_PathExtension( const char *pszVirtualPath );

fs_error_t CypherFileSystem_PathWithoutExtension(
    const char *pszVirtualPath,
    char *pszOutPath,
    common::u32 nOutPathSize );

bool CypherFileSystem_PathHasExtension(
    const char *pszVirtualPath,
    const char *pszExtension );

/*
================
Write Root

Writes, deletes, renames and generated data go through the write path. They do
not modify arbitrary read mounts.
================
*/
fs_error_t CypherFileSystem_SetWritePath( const char *pszPhysicalPath );

const char *CypherFileSystem_GetWritePath();

/*
================
File I/O
================
*/
fs_error_t CypherFileSystem_Open(
    const char *pszVirtualPath,
    open_mode_t mode,
    file_t &fileOut );

fs_error_t CypherFileSystem_Close( file_t &file );

fs_error_t CypherFileSystem_Read(
    file_t &file,
    void *pBuffer,
    common::u64 nBytesToRead,
    common::u64 &nBytesReadOut );

fs_error_t CypherFileSystem_Write(
    file_t &file,
    const void *pBuffer,
    common::u64 nBytesToWrite,
    common::u64 &nBytesWrittenOut );

fs_error_t CypherFileSystem_Seek(
    file_t &file,
    common::i64 nOffset,
    seek_origin_t origin );

fs_error_t CypherFileSystem_Tell(
    file_t &file,
    common::u64 &nPositionOut );

fs_error_t CypherFileSystem_Flush( file_t &file );

fs_error_t CypherFileSystem_ReadEntireFile(
    const char *pszVirtualPath,
    void *pBuffer,
    common::u64 nBytesToRead,
    common::u64 &nBytesReadOut );

fs_error_t CypherFileSystem_WriteEntireFile(
    const char *pszVirtualPath,
    const void *pBuffer,
    common::u64 nBytesToWrite );

fs_error_t CypherFileSystem_AppendEntireFile(
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
fs_error_t CypherFileSystem_CreateDirectory( const char *pszVirtualPath );

fs_error_t CypherFileSystem_DeleteFile( const char *pszVirtualPath );

fs_error_t CypherFileSystem_RemoveDirectory( const char *pszVirtualPath );

fs_error_t CypherFileSystem_RemoveDirectoryTree( const char *pszVirtualPath );

fs_error_t CypherFileSystem_Rename(
    const char *pszFromVirtualPath,
    const char *pszToVirtualPath );

fs_error_t CypherFileSystem_CopyFile(
    const char *pszFromVirtualPath,
    const char *pszToVirtualPath );

/*
================
Query And Discovery
================
*/
bool CypherFileSystem_Exists( const char *pszVirtualPath );

bool CypherFileSystem_FileExists( const char *pszVirtualPath );

bool CypherFileSystem_DirectoryExists( const char *pszVirtualPath );

fs_error_t CypherFileSystem_GetFileInfo(
    const char *pszVirtualPath,
    file_info_t &fileInfoOut );

fs_error_t CypherFileSystem_ListDirectory(
    const char *pszVirtualPath,
    directory_entry_t *pEntries,
    common::u32 nMaxEntries,
    common::u32 &nEntryCountOut );

fs_error_t CypherFileSystem_FindFiles(
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
fs_error_t CypherFileSystem_GetPackageInfo(
    const char *pszPackagePath,
    package_info_t &packageInfoOut );

bool CypherFileSystem_PackageIsMounted( const char *pszPackagePath );

/*
================
Async And Streaming

The first implementation can be simple worker-thread file reads. Later this
becomes the streaming layer for textures, audio, levels and packages.
================
*/
fs_error_t CypherFileSystem_ReadAsync(
    const char *pszVirtualPath,
    void *pBuffer,
    common::u64 nBytesToRead,
    async_request_t &hRequestOut );

fs_error_t CypherFileSystem_WriteAsync(
    const char *pszVirtualPath,
    const void *pBuffer,
    common::u64 nBytesToWrite,
    async_request_t &hRequestOut );

fs_error_t CypherFileSystem_PollAsync(
    async_request_t hRequest,
    async_result_t &resultOut );

fs_error_t CypherFileSystem_WaitAsync(
    async_request_t hRequest,
    async_result_t &resultOut );

fs_error_t CypherFileSystem_CancelAsync( async_request_t hRequest );

/*
================
File Watching

Used by editor workflows and hot reload for shaders, configs and assets.
================
*/
fs_error_t CypherFileSystem_WatchPath(
    const char *pszVirtualPath,
    common::u32 nFlags,
    watch_handle_t &hWatchOut );

fs_error_t CypherFileSystem_UnwatchPath( watch_handle_t hWatch );

fs_error_t CypherFileSystem_PollChanges(
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
fs_error_t CypherFileSystem_GetStats( stats_t &statsOut );

fs_error_t CypherFileSystem_ResetStats();

fs_error_t CypherFileSystem_DumpMounts();

fs_error_t CypherFileSystem_DumpStats();

}       // namespace cypher::engine::fs

#endif // CYPHER_ENGINE_FILESYSTEM_H
