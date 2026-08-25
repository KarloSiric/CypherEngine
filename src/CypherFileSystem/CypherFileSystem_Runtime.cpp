//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherFileSystem/CypherFileSystem_Runtime.cpp
//  Purpose: Implements the CypherFileSystem FileSystem Runtime module.
//  Details: This file participates in the virtual filesystem layer that maps engine
//           paths to mounted physical or package-backed data. Keep path validation
//           strict because every asset pipeline will depend on it.
//
//  History:
//  - Created by Karlo Siric on 2026-06-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Runtime Implementation Notes

The filesystem runtime coordinates initialization and shutdown of mounts and open-file services.
Shutdown refuses to hide outstanding ownership or background work.
================
*/

#include "CypherFileSystem_Runtime.h"

namespace cypher::engine::fs
{

namespace {

runtime_state_t s_FsRuntimeState{};             // Process-wide filesystem state; Host owns its lifetime.
std::recursive_mutex s_FsRuntimeMutex{};        // Serializes mounts, handles, watches, async slots, and counters.

}       // namespace

runtime_state_t &FS_RuntimeState()
{
    return s_FsRuntimeState;
}

std::recursive_mutex &FS_RuntimeMutex()
{
    return s_FsRuntimeMutex;
}

bool FS_HasWritePath()
{
    std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
    const runtime_state_t &state = FS_RuntimeState();
    return state.initialized && state.szWritePath[0] != '\0';
}

fs_error_t FS_BuildWritePath( const char *szVirtualPath, char *szOutPath, common::u32 nOutPathSize )
{
    std::lock_guard<std::recursive_mutex> lock( FS_RuntimeMutex() );
    runtime_state_t &state = FS_RuntimeState();

    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( state.szWritePath[0] == '\0' ) {
        return fs_error_t::ERR_WRITE_PATH_NOT_SET;
    }
    if ( szOutPath == nullptr || nOutPathSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }

    // Normalize before joining so parent traversal cannot escape the configured write
    // root and two spellings of one virtual path map to the same physical destination.
    char szNormalizedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    const fs_error_t normalizeResult = FS_NormalizeVirtualPath( szVirtualPath, szNormalizedPath, sizeof( szNormalizedPath ) );
    if ( normalizeResult != fs_error_t::OK ) {
        szOutPath[0] = '\0';
        return normalizeResult;
    }

    return FS_BuildPhysicalPath( state.szWritePath, szNormalizedPath, szOutPath, nOutPathSize );
}

}       // namespace cypher::engine::fs
