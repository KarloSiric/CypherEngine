//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/FileSystem/CypherCommon_VfsDirectory.h
//  Purpose: Declares the read-only loose-directory VFS provider.
//  Details: Offline tools, tests, and development hosts use this adapter to
//           expose a native source directory through the provider-neutral VFS
//           contract. Runtime package and mount providers remain separate.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_FILESYSTEM_VFS_DIRECTORY_H
#define CYPHER_COMMON_FILESYSTEM_VFS_DIRECTORY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Vfs.h"

namespace cypher::common
{

// Owns the canonical native root after successful initialization. Enumeration
// produces canonical virtual paths in bytewise ascending order so offline builds
// discover the same input sequence on repeated runs.
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

#endif // CYPHER_COMMON_FILESYSTEM_VFS_DIRECTORY_H
