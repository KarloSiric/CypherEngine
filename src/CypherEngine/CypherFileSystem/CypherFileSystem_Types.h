//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherFileSystem/CypherFileSystem_Types.h
//  Purpose: Declares the CypherFileSystem FileSystem Types module.
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

#ifndef CYPHER_ENGINE_FILESYSTEM_TYPES_H
#define CYPHER_ENGINE_FILESYSTEM_TYPES_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon.h"
#include "CypherFileSystem_Error.h"

#include <ctime>       // std::time_t file timestamps.

namespace cypher::engine::fs
{

constexpr common::u32 CYPHER_FILESYSTEM_MAX_MOUNTS                    = 32u;
constexpr common::u32 CYPHER_FILESYSTEM_MAX_PATH_LENGTH               = 260u;
constexpr common::u32 CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH       = 64u;
constexpr common::u32 CYPHER_FILESYSTEM_MAX_EXTENSION_LENGTH          = 32u;
constexpr common::u32 CYPHER_FILESYSTEM_MAX_PATTERN_LENGTH            = 128u;
constexpr common::u32 CYPHER_FILESYSTEM_MAX_ASYNC_REQUESTS            = 32u;
constexpr common::u32 CYPHER_FILESYSTEM_MAX_WATCHES                   = 32u;
constexpr common::u32 CYPHER_FILESYSTEM_MAX_WATCH_SNAPSHOT_ENTRIES    = 512u;
constexpr common::u32 CYPHER_FILESYSTEM_MAX_WATCH_EVENTS              = 256u;
constexpr common::u32 CYPHER_FILESYSTEM_INVALID_MOUNT                 = 0u;
constexpr common::u32 CYPHER_FILESYSTEM_INVALID_ASYNC_REQUEST         = 0u;
constexpr common::u32 CYPHER_FILESYSTEM_INVALID_WATCH                 = 0u;

/*
================
Filesystem Types

Virtual mounts map engine paths onto physical storage backends.
================
*/
enum class mount_type_t : common::u8 {
    CYPHER_FILESYSTEM_DIRECTORY    = 0,
    CYPHER_FILESYSTEM_PACKAGE
};

enum mount_flags_t : common::u32 {
    CYPHER_FILESYSTEM_MOUNT_NONE       = 0u,
    CYPHER_FILESYSTEM_MOUNT_READ_ONLY  = 1u << 0u,
    CYPHER_FILESYSTEM_MOUNT_WRITABLE   = 1u << 1u,
    CYPHER_FILESYSTEM_MOUNT_OPTIONAL   = 1u << 2u
};

enum class open_mode_t : common::u8 {
    READ_TEXT = 0,
    READ_BINARY,
    WRITE_TEXT,
    WRITE_BINARY,
    APPEND_TEXT,
    APPEND_BINARY
};

enum class file_backend_t : common::u8 {
    INVALID = 0,
    OS_FILE,
    PACKAGE_FILE,
    MEMORY_BUFFER,
    MAPPED_FILE
};

enum class seek_origin_t : common::u8 {
    CYPHER_FILESYSTEM_SEEK_START = 0,
    CYPHER_FILESYSTEM_SEEK_CURRENT,
    CYPHER_FILESYSTEM_SEEK_END
};

enum class directory_entry_type_t : common::u8 {
    UNKNOWN = 0,
    FILE,
    DIRECTORY
};

enum find_flags_t : common::u32 {
    CYPHER_FILESYSTEM_FIND_NONE              = 0u,
    CYPHER_FILESYSTEM_FIND_RECURSIVE         = 1u << 0u,
    CYPHER_FILESYSTEM_FIND_FILES             = 1u << 1u,
    CYPHER_FILESYSTEM_FIND_DIRECTORIES       = 1u << 2u,
    CYPHER_FILESYSTEM_FIND_INCLUDE_HIDDEN    = 1u << 3u,
    CYPHER_FILESYSTEM_FIND_SORT_BY_NAME      = 1u << 4u
};

enum watch_flags_t : common::u32 {
    CYPHER_FILESYSTEM_WATCH_NONE             = 0u,
    CYPHER_FILESYSTEM_WATCH_FILE             = 1u << 0u,
    CYPHER_FILESYSTEM_WATCH_DIRECTORY        = 1u << 1u,
    CYPHER_FILESYSTEM_WATCH_RECURSIVE        = 1u << 2u
};

enum class async_status_t : common::u8 {
    INVALID = 0,
    PENDING,
    RUNNING,
    COMPLETE,
    CANCELLED,
    FAILED
};

enum class watch_event_type_t : common::u8 {
    UNKNOWN = 0,
    CREATED,
    MODIFIED,
    DELETED,
    RENAMED
};

using async_request_t = common::u32;
using watch_handle_t = common::u32;
using mount_handle_t = common::u32;

/*
================
Filesystem Records
================
*/
struct mount_t {
    mount_handle_t handle{ CYPHER_FILESYSTEM_INVALID_MOUNT };
    mount_type_t type{ mount_type_t::CYPHER_FILESYSTEM_DIRECTORY };
    char szVirtualRoot[CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH]{};
    char szPhysicalRoot[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    void *pPackageReader{ nullptr };
    common::u32 flags{ CYPHER_FILESYSTEM_MOUNT_READ_ONLY };
    common::u32 priority{ 0u };
};

struct file_t {
    file_backend_t backend{ file_backend_t::INVALID };
    void *pNativeHandle{ nullptr };
    common::u64 size{ 0u };
    common::u64 cursor{ 0u };
    bool readable{ false };
    bool writable{ false };
};

struct file_info_t {
    bool exists{ false };
    bool bIsDirectory{ false };
    bool bIsPackageFile{ false };
    file_backend_t backend{ file_backend_t::INVALID };
    common::u64 nFileSize{ 0u };
    std::time_t nModifiedTime{};
    char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szResolvedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szPackagePath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
};

struct directory_entry_t {
    char name[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    directory_entry_type_t type{ directory_entry_type_t::UNKNOWN };
    common::u64 size{ 0u };
    std::time_t nModifiedTime{};
};

struct mount_info_t {
    mount_handle_t handle{ CYPHER_FILESYSTEM_INVALID_MOUNT };
    mount_type_t type{ mount_type_t::CYPHER_FILESYSTEM_DIRECTORY };
    char szVirtualRoot[CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH]{};
    char szPhysicalRoot[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    common::u32 flags{ CYPHER_FILESYSTEM_MOUNT_NONE };
    common::u32 priority{ 0u };
};

struct package_info_t {
    char szVirtualRoot[CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH]{};
    char szPackagePath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    common::u32 nFileCount{ 0u };
    common::u32 priority{ 0u };
    bool mounted{ false };
};

struct async_result_t {
    async_status_t status{ async_status_t::INVALID };
    fs_error_t error{ fs_error_t::ERR_INVALID_HANDLE };
    common::u64 nBytesTransferred{ 0u };
};

struct watch_event_t {
    watch_event_type_t type{ watch_event_type_t::UNKNOWN };
    char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szOldVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
};

struct stats_t {
    common::u64 nOpenCount{ 0u };
    common::u64 nCloseCount{ 0u };
    common::u64 nReadCount{ 0u };
    common::u64 nWriteCount{ 0u };
    common::u64 nBytesRead{ 0u };
    common::u64 nBytesWritten{ 0u };
    common::u64 nFailedLookupCount{ 0u };
};

struct resolve_trace_entry_t {
    mount_handle_t mount{ CYPHER_FILESYSTEM_INVALID_MOUNT };
    mount_type_t type{ mount_type_t::CYPHER_FILESYSTEM_DIRECTORY };
    bool bRootMatched{ false };
    bool bPathExists{ false };
    char szVirtualRoot[CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH]{};
    char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
};

struct resolve_trace_t {
    fs_error_t result{ fs_error_t::ERR_PATH_NOT_FOUND };
    bool resolved{ false };
    common::u32 nCheckedMountCount{ 0u };
    char szRequestedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szNormalizedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    char szResolvedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    resolve_trace_entry_t entries[CYPHER_FILESYSTEM_MAX_MOUNTS]{};
};

}

#endif // CYPHER_ENGINE_FILESYSTEM_TYPES_H
