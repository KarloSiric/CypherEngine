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

inline constexpr usize CY_VFS_MAX_VIRTUAL_PATH = 4095u;

enum class vfs_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_PATH,
    NOT_FOUND,
    NOT_A_FILE,
    NOT_A_DIRECTORY,
    SIZE_LIMIT,
    OUT_OF_MEMORY,
    IO_ERROR,
    UNSUPPORTED,
    CANCELLED
};

enum class vfs_entry_type_t : u8 {
    UNKNOWN = 0u,
    FILE,
    DIRECTORY
};

enum vfs_capability_flags_t : flags32_t {
    VFS_CAPABILITY_NONE             = 0u,
    VFS_CAPABILITY_READ_ALL         = CYPHER_BIT32( 0 ),
    VFS_CAPABILITY_STAT             = CYPHER_BIT32( 1 ),
    VFS_CAPABILITY_ENUMERATE        = CYPHER_BIT32( 2 ),
    VFS_CAPABILITY_DIAGNOSTIC_PATH  = CYPHER_BIT32( 3 )
};

inline constexpr flags32_t CY_VFS_CAPABILITY_MASK =
    VFS_CAPABILITY_READ_ALL |
    VFS_CAPABILITY_STAT |
    VFS_CAPABILITY_ENUMERATE |
    VFS_CAPABILITY_DIAGNOSTIC_PATH;

struct vfs_file_info_t {
    vfs_entry_type_t type{ vfs_entry_type_t::UNKNOWN };
    u64 cbSize{ 0u };
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
    vfs_read_all_fn_t pfnReadAll{ nullptr };
    vfs_stat_fn_t pfnStat{ nullptr };
    vfs_enumerate_fn_t pfnEnumerate{ nullptr };
    vfs_resolve_diagnostic_path_fn_t pfnResolveDiagnosticPath{ nullptr };
};

struct vfs_t {
    const vfs_ops_t *pOps{ nullptr };
    void *pUserData{ nullptr };
    flags32_t capabilities{ VFS_CAPABILITY_NONE };
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

// Enumeration order is provider-defined by the contract. The directory provider
// below guarantees bytewise ascending virtual paths for reproducible tools.
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

// Read-only loose-directory provider used by authoring tools and development
// builds. The canonical native root is owned by the provider after Init.
struct vfs_directory_t {
    text_buffer_t nativeRoot{};
};

CYPHER_NODISCARD CYPHER_COMMON_API
vfs_status_t VfsDirectory_Init(
    vfs_directory_t *pDirectory,
    string_view_t nativeRoot ) noexcept;

CYPHER_COMMON_API void VfsDirectory_Shutdown(
    vfs_directory_t *pDirectory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
vfs_t VfsDirectory_Make( vfs_directory_t *pDirectory ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_FILESYSTEM_VFS_H
