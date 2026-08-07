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
    FILE_OPEN_FLAG_NONE       = 0u,
    FILE_OPEN_FLAG_READ       = CYPHER_BIT32( 0 ),
    FILE_OPEN_FLAG_WRITE      = CYPHER_BIT32( 1 ),
    FILE_OPEN_FLAG_APPEND     = CYPHER_BIT32( 2 ),
    FILE_OPEN_FLAG_CREATE     = CYPHER_BIT32( 3 ),
    FILE_OPEN_FLAG_TRUNCATE   = CYPHER_BIT32( 4 ),
    FILE_OPEN_FLAG_EXCLUSIVE  = CYPHER_BIT32( 5 )
};

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

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t FileIo_NativeExists( string_view_t nativePath ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FILEIO_H
