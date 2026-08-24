//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_FileIo.h
//  Purpose: Declares minimal native-file stream helpers for bootstrap code and tools.
//  Details: Paths are native OS paths, never VFS paths. Runtime asset access should use
//           CypherFileSystem so mounts, packages, policy, and diagnostics are preserved.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_FILEIO_H
#define CYPHER_COMMON_TIER1_FILEIO_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Blob.h"
#include "CypherCommon_Stream.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

enum file_open_flags_t : flags32_t {
    FILE_OPEN_FLAG_NONE      = 0u,                // Invalid without READ or WRITE.
    FILE_OPEN_FLAG_READ      = CYPHER_BIT32( 0 ), // Permit reads.
    FILE_OPEN_FLAG_WRITE     = CYPHER_BIT32( 1 ), // Permit writes.
    FILE_OPEN_FLAG_APPEND    = CYPHER_BIT32( 2 ), // Force writes to current end.
    FILE_OPEN_FLAG_CREATE    = CYPHER_BIT32( 3 ), // Create when missing.
    FILE_OPEN_FLAG_TRUNCATE  = CYPHER_BIT32( 4 ), // Clear existing content on open.
    FILE_OPEN_FLAG_EXCLUSIVE = CYPHER_BIT32( 5 )  // Fail CREATE when path exists.
};

/*
================
Native File I/O

These functions are a bootstrap and tool boundary for native operating-system
paths. They deliberately do not apply VFS mounts, package lookup, or virtual-path
policy.

Flag rules:
- READ and/or WRITE must be present.
- APPEND, CREATE, and TRUNCATE require WRITE.
- EXCLUSIVE requires CREATE and means create-only-if-missing.
- APPEND and TRUNCATE are mutually exclusive.
- CREATE without TRUNCATE preserves an existing file.

The stream returned by FileIo_AsStream borrows its native_file_t and becomes
invalid immediately after FileIo_CloseNative.
================
*/

struct native_file_t;

CYPHER_NODISCARD CYPHER_COMMON_API
native_file_t *FileIo_OpenNative(
    string_view_t nativePath,
    flags32_t flags,
    const allocator_t *pAllocator ) noexcept;

CYPHER_COMMON_API void FileIo_CloseNative( native_file_t *pFile ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
stream_t FileIo_AsStream( native_file_t *pFile ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FileIo_ReadAllNative(
    string_view_t nativePath,
    blob_t *pDest ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FileIo_WriteAllNative(
    string_view_t nativePath,
    binary_block_t source ) noexcept;

// Creates every missing component in a native directory path. Existing
// directories are accepted; an existing non-directory component is rejected.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FileIo_CreateDirectoriesNative( string_view_t nativePath ) noexcept;

// Removes one native file. Directories are intentionally outside this API.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FileIo_RemoveNative( string_view_t nativePath ) noexcept;

// Atomically publishes sourcePath at destinationPath when both paths reside on
// the same filesystem. An existing destination file is replaced.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FileIo_ReplaceNative(
    string_view_t sourcePath,
    string_view_t destinationPath ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FileIo_NativeExists( string_view_t nativePath ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FILEIO_H
