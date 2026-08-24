//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/FileSystem/CypherCommon_Vfs.h
//  Purpose: Declares the shared read-only virtual filesystem contract.
//  Details: Compilers, runtime loaders, tests, and editor hosts use this callback
//           boundary without depending on one mount or package implementation.
//           Virtual paths are canonical resource identities, never native paths.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_FILESYSTEM_VFS_H
#define CYPHER_COMMON_FILESYSTEM_VFS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Blob.h"
#include "CypherCommon_TextBuffer.h"

namespace cypher::common
{

inline constexpr usize CY_VFS_MAX_VIRTUAL_PATH = 4095u; // UTF-8 bytes, excluding NUL.

enum class vfs_status_t : u8 {
    OK = 0u,        // Operation completed.
    INVALID_ARGUMENT,// Provider, callback, output, or size argument is invalid.
    INVALID_PATH,  // Virtual path violates canonical policy or escapes its mount.
    NOT_FOUND,     // No provider entry resolves to the requested path.
    NOT_A_FILE,    // Entry exists but cannot be read as a regular file.
    NOT_A_DIRECTORY,// Entry exists but cannot be enumerated as a directory.
    SIZE_LIMIT,    // File, path data, or entry count exceeds a caller/provider bound.
    OUT_OF_MEMORY, // Provider could not allocate transactional output.
    IO_ERROR,      // Native filesystem or package operation failed.
    UNSUPPORTED,   // Provider does not advertise the requested capability.
    CANCELLED      // Visitor stopped enumeration intentionally.
};

enum class vfs_entry_type_t : u8 {
    UNKNOWN = 0u, // Unrecognized or unsupported provider entry.
    FILE,         // Readable regular-file resource.
    DIRECTORY     // Enumerable virtual directory.
};

enum vfs_capability_flags_t : flags32_t {
    VFS_CAPABILITY_NONE             = 0u, // Invalid provider with no operations.
    VFS_CAPABILITY_READ_ALL         = CYPHER_BIT32( 0 ), // Whole-file reads.
    VFS_CAPABILITY_STAT             = CYPHER_BIT32( 1 ), // Entry type and size queries.
    VFS_CAPABILITY_ENUMERATE        = CYPHER_BIT32( 2 ), // Directory traversal.
    VFS_CAPABILITY_DIAGNOSTIC_PATH  = CYPHER_BIT32( 3 )  // Human-only native path.
};

inline constexpr flags32_t CY_VFS_CAPABILITY_MASK =
    VFS_CAPABILITY_READ_ALL |
    VFS_CAPABILITY_STAT |
    VFS_CAPABILITY_ENUMERATE |
    VFS_CAPABILITY_DIAGNOSTIC_PATH;

struct vfs_file_info_t {
    vfs_entry_type_t type{ vfs_entry_type_t::UNKNOWN }; // Provider entry kind.
    u64 cbSize{ 0u }; // File bytes; zero for directories and unknown entries.
};

using vfs_visit_fn_t = bool_t ( * )(
    string_view_t virtualPath,
    const vfs_file_info_t &info,
    void *pUserData ) noexcept;

using vfs_read_all_fn_t = vfs_status_t ( * )(
    void *pUserData,
    string_view_t virtualPath,
    usize cbMaximum,
    blob_t *pDest ) noexcept;

using vfs_stat_fn_t = vfs_status_t ( * )(
    void *pUserData,
    string_view_t virtualPath,
    vfs_file_info_t *pInfoOut ) noexcept;

using vfs_enumerate_fn_t = vfs_status_t ( * )(
    void *pUserData,
    string_view_t virtualRoot,
    bool_t bRecursive,
    vfs_visit_fn_t pVisit,
    void *pVisitUserData ) noexcept;

using vfs_resolve_diagnostic_path_fn_t = vfs_status_t ( * )(
    void *pUserData,
    string_view_t virtualPath,
    text_buffer_t *pNativePathOut ) noexcept;

struct vfs_ops_t {
    vfs_read_all_fn_t pfnReadAll{ nullptr }; // Required by READ_ALL capability.
    vfs_stat_fn_t pfnStat{ nullptr }; // Required by STAT capability.
    vfs_enumerate_fn_t pfnEnumerate{ nullptr }; // Required by ENUMERATE capability.
    vfs_resolve_diagnostic_path_fn_t pfnResolveDiagnosticPath{ nullptr }; // Optional.
};

struct vfs_t {
    const vfs_ops_t *pOps{ nullptr }; // Borrowed process-lifetime callback table.
    void *pUserData{ nullptr };       // Borrowed provider instance passed to callbacks.
    flags32_t capabilities{ VFS_CAPABILITY_NONE }; // vfs_capability_flags_t bits.
};

// Checks the canonical lowercase, forward-slash virtual path policy. Empty paths
// are rejected; Vfs_Enumerate alone accepts an empty root to mean the VFS root.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Vfs_IsCanonicalPath( string_view_t virtualPath ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Vfs_IsValid( const vfs_t *pVfs ) noexcept;

// Reads at most cbMaximum bytes and leaves pDest unchanged on failure.
CYPHER_NODISCARD CYPHER_COMMON_API
vfs_status_t Vfs_ReadAll(
    const vfs_t *pVfs,
    string_view_t virtualPath,
    usize cbMaximum,
    blob_t *pDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
vfs_status_t Vfs_Stat(
    const vfs_t *pVfs,
    string_view_t virtualPath,
    vfs_file_info_t *pInfoOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
vfs_status_t Vfs_Exists(
    const vfs_t *pVfs,
    string_view_t virtualPath,
    bool_t *pExistsOut ) noexcept;

// Enumeration order is provider-defined by the contract. Deterministic providers
// should document the ordering they guarantee to callers.
CYPHER_NODISCARD CYPHER_COMMON_API
vfs_status_t Vfs_Enumerate(
    const vfs_t *pVfs,
    string_view_t virtualRoot,
    bool_t bRecursive,
    vfs_visit_fn_t pVisit,
    void *pVisitUserData ) noexcept;

// Produces an optional native path for human diagnostics only. Callers must not
// reopen this path because package-backed providers may not expose one.
CYPHER_NODISCARD CYPHER_COMMON_API
vfs_status_t Vfs_ResolveDiagnosticPath(
    const vfs_t *pVfs,
    string_view_t virtualPath,
    text_buffer_t *pNativePathOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *Vfs_StatusName( vfs_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_FILESYSTEM_VFS_H
