//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherFileSystem/CypherFileSystem_Types.h
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

#include "Engine/CypherCommon.h"
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
    CYPHER_FILESYSTEM_DIRECTORY    = 0, // Virtual root resolves into an OS directory.
    CYPHER_FILESYSTEM_PACKAGE          // Virtual root resolves into an open CypherPak archive.
};

enum mount_flags_t : common::u32 {
    CYPHER_FILESYSTEM_MOUNT_NONE       = 0u,       // No optional mount behavior.
    CYPHER_FILESYSTEM_MOUNT_READ_ONLY  = 1u << 0u, // Mount may satisfy reads but never writes.
    CYPHER_FILESYSTEM_MOUNT_WRITABLE   = 1u << 1u, // Directory may be selected as an explicit write root.
    CYPHER_FILESYSTEM_MOUNT_OPTIONAL   = 1u << 2u  // Missing physical backing is not a fatal configuration error.
};

enum class open_mode_t : common::u8 {
    READ_TEXT = 0, // Open existing text content for reads.
    READ_BINARY,   // Open existing content without text translation.
    WRITE_TEXT,    // Create or truncate text output.
    WRITE_BINARY,  // Create or truncate binary output.
    APPEND_TEXT,   // Append text after existing content.
    APPEND_BINARY  // Append bytes after existing content.
};

enum class file_backend_t : common::u8 {
    INVALID = 0,  // Closed or uninitialized file record.
    OS_FILE,      // Native host filesystem handle.
    PACKAGE_FILE, // Logical file backed by a package entry.
    MEMORY_BUFFER,// Borrowed or owned in-memory byte span.
    MAPPED_FILE   // Platform memory-mapped file view.
};

enum class seek_origin_t : common::u8 {
    CYPHER_FILESYSTEM_SEEK_START = 0, // Offset is relative to byte zero.
    CYPHER_FILESYSTEM_SEEK_CURRENT,   // Offset is relative to the current cursor.
    CYPHER_FILESYSTEM_SEEK_END        // Offset is relative to logical file size.
};

enum class directory_entry_type_t : common::u8 {
    UNKNOWN = 0, // Entry type could not be classified.
    FILE,        // Entry names readable file content.
    DIRECTORY    // Entry names a traversable directory.
};

enum find_flags_t : common::u32 {
    CYPHER_FILESYSTEM_FIND_NONE              = 0u,       // Use the direct directory and default entry policy.
    CYPHER_FILESYSTEM_FIND_RECURSIVE         = 1u << 0u, // Descend through matching subdirectories.
    CYPHER_FILESYSTEM_FIND_FILES             = 1u << 1u, // Include regular files in results.
    CYPHER_FILESYSTEM_FIND_DIRECTORIES       = 1u << 2u, // Include directories in results.
    CYPHER_FILESYSTEM_FIND_INCLUDE_HIDDEN    = 1u << 3u, // Do not filter platform-hidden names.
    CYPHER_FILESYSTEM_FIND_SORT_BY_NAME      = 1u << 4u  // Return deterministic lexical path order.
};

enum watch_flags_t : common::u32 {
    CYPHER_FILESYSTEM_WATCH_NONE             = 0u,       // No watch target category selected.
    CYPHER_FILESYSTEM_WATCH_FILE             = 1u << 0u, // Watch one resolved file.
    CYPHER_FILESYSTEM_WATCH_DIRECTORY        = 1u << 1u, // Watch immediate directory entries.
    CYPHER_FILESYSTEM_WATCH_RECURSIVE        = 1u << 2u  // Include descendants of a directory target.
};

enum class async_status_t : common::u8 {
    INVALID = 0, // Handle does not name a live request.
    PENDING,     // Request is queued but work has not begun.
    RUNNING,     // Worker is currently performing I/O.
    COMPLETE,    // Transfer completed successfully.
    CANCELLED,   // Cancellation won before successful publication.
    FAILED       // Worker completed with an fs_error_t failure.
};

enum class watch_event_type_t : common::u8 {
    UNKNOWN = 0, // Event could not be classified reliably.
    CREATED,     // Path appeared since the previous snapshot.
    MODIFIED,    // Existing path metadata changed.
    DELETED,     // Path disappeared since the previous snapshot.
    RENAMED      // Old and new paths were correlated as one rename.
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
    mount_handle_t handle{ CYPHER_FILESYSTEM_INVALID_MOUNT }; // Process-local identity for unmount and diagnostics.
    mount_type_t type{ mount_type_t::CYPHER_FILESYSTEM_DIRECTORY }; // Active backing interpretation.
    char szVirtualRoot[CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH]{}; // Normalized namespace prefix.
    char szPhysicalRoot[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Directory or package path on the host filesystem.
    void *pPackageReader{ nullptr };                        // Owned package reader for PACKAGE mounts.
    common::u32 flags{ CYPHER_FILESYSTEM_MOUNT_READ_ONLY }; // mount_flags_t behavior bits.
    common::u32 priority{ 0u };                             // Higher values shadow lower-priority mounts.
};

struct file_t {
    file_backend_t backend{ file_backend_t::INVALID };      // Selects interpretation of pNativeHandle.
    void *pNativeHandle{ nullptr };                         // Backend-owned open file state.
    common::u64 size{ 0u };                                 // Logical file length in bytes.
    common::u64 cursor{ 0u };                               // Logical byte position for package/memory backends.
    bool readable{ false };                                 // Open mode permits reads.
    bool writable{ false };                                 // Open mode permits writes.
};

struct file_info_t {
    bool exists{ false };                                   // A mounted backend resolved the requested path.
    bool bIsDirectory{ false };                             // Resolved object is a directory rather than a file.
    bool bIsPackageFile{ false };                           // Content is stored inside a package archive.
    file_backend_t backend{ file_backend_t::INVALID };      // Backend that supplied the result.
    common::u64 nFileSize{ 0u };                            // Logical content size in bytes.
    std::time_t nModifiedTime{};                            // Backend modification timestamp when available.
    char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Normalized engine-visible path.
    char szResolvedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Physical file path or package-entry name.
    char szPackagePath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Physical package archive path when applicable.
};

struct directory_entry_t {
    char name[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};         // Basename of the discovered entry.
    char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Full normalized path in the virtual namespace.
    directory_entry_type_t type{ directory_entry_type_t::UNKNOWN }; // File/directory classification.
    common::u64 size{ 0u };                                 // File bytes; zero for directories or unknown size.
    std::time_t nModifiedTime{};                            // Backend modification timestamp when available.
};

struct mount_info_t {
    mount_handle_t handle{ CYPHER_FILESYSTEM_INVALID_MOUNT }; // Public process-local mount identity.
    mount_type_t type{ mount_type_t::CYPHER_FILESYSTEM_DIRECTORY }; // Directory or package backing.
    char szVirtualRoot[CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH]{}; // Normalized namespace prefix.
    char szPhysicalRoot[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Host directory/archive path.
    common::u32 flags{ CYPHER_FILESYSTEM_MOUNT_NONE };      // mount_flags_t behavior bits.
    common::u32 priority{ 0u };                             // Search precedence; higher values win.
};

struct package_info_t {
    char szVirtualRoot[CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH]{}; // Namespace prefix supplied by the mount.
    char szPackagePath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Physical archive path.
    common::u32 nFileCount{ 0u };                           // Files indexed by the open package reader.
    common::u32 priority{ 0u };                             // Search precedence of this package mount.
    bool mounted{ false };                                  // Package is active in the current mount table.
};

struct async_result_t {
    async_status_t status{ async_status_t::INVALID };       // Terminal or current lifecycle state.
    fs_error_t error{ fs_error_t::ERR_INVALID_HANDLE };     // Worker result when COMPLETE/FAILED/CANCELLED.
    common::u64 nBytesTransferred{ 0u };                    // Bytes successfully read or written.
};

struct watch_event_t {
    watch_event_type_t type{ watch_event_type_t::UNKNOWN }; // Change category.
    char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Current path, or deleted path for DELETE.
    char szOldVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Previous path for RENAME only.
};

struct stats_t {
    common::u64 nOpenCount{ 0u };                           // Successful open operations.
    common::u64 nCloseCount{ 0u };                          // Successful close operations.
    common::u64 nReadCount{ 0u };                           // Successful read operations.
    common::u64 nWriteCount{ 0u };                          // Successful write operations.
    common::u64 nBytesRead{ 0u };                           // Bytes returned through read APIs.
    common::u64 nBytesWritten{ 0u };                        // Bytes accepted through write APIs.
    common::u64 nFailedLookupCount{ 0u };                   // Virtual paths unresolved by every mount.
};

struct resolve_trace_entry_t {
    mount_handle_t mount{ CYPHER_FILESYSTEM_INVALID_MOUNT }; // Mount considered at this step.
    mount_type_t type{ mount_type_t::CYPHER_FILESYSTEM_DIRECTORY }; // Mount backend category.
    bool bRootMatched{ false };                             // Requested path belongs to this virtual root.
    bool bPathExists{ false };                              // Backend contained the resolved relative path.
    char szVirtualRoot[CYPHER_FILESYSTEM_MAX_VIRTUAL_ROOT_LENGTH]{}; // Root tested by this step.
    char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Candidate host path when applicable.
};

struct resolve_trace_t {
    fs_error_t result{ fs_error_t::ERR_PATH_NOT_FOUND };    // Final resolution result.
    bool resolved{ false };                                 // A mount supplied a usable path.
    common::u32 nCheckedMountCount{ 0u };                   // Valid prefix length in entries.
    char szRequestedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Original caller spelling.
    char szNormalizedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Canonical virtual path.
    char szResolvedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Winning physical or package path.
    resolve_trace_entry_t entries[CYPHER_FILESYSTEM_MAX_MOUNTS]{}; // Per-mount search decisions.
};

}

#endif // CYPHER_ENGINE_FILESYSTEM_TYPES_H
