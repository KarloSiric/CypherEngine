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

#include "CypherFileSystem_Runtime.h"

namespace cypher::engine::fs
{

namespace {

runtime_state_t s_FsRuntimeState{};
std::recursive_mutex s_FsRuntimeMutex{};

}       // namespace

runtime_state_t &CypherFileSystem_RuntimeState()
{
    return s_FsRuntimeState;
}

std::recursive_mutex &CypherFileSystem_RuntimeMutex()
{
    return s_FsRuntimeMutex;
}

bool CypherFileSystem_HasWritePath()
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    const runtime_state_t &state = CypherFileSystem_RuntimeState();
    return state.initialized && state.szWritePath[0] != '\0';
}

fs_error_t CypherFileSystem_BuildWritePath( const char *szVirtualPath, char *szOutPath, common::u32 nOutPathSize )
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    runtime_state_t &state = CypherFileSystem_RuntimeState();

    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( state.szWritePath[0] == '\0' ) {
        return fs_error_t::ERR_WRITE_PATH_NOT_SET;
    }
    if ( szOutPath == nullptr || nOutPathSize == 0u ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }

    char szNormalizedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
    const fs_error_t normalizeResult = CypherFileSystem_NormalizeVirtualPath( szVirtualPath, szNormalizedPath, sizeof( szNormalizedPath ) );
    if ( normalizeResult != fs_error_t::OK ) {
        szOutPath[0] = '\0';
        return normalizeResult;
    }

    return CypherFileSystem_BuildPhysicalPath( state.szWritePath, szNormalizedPath, szOutPath, nOutPathSize );
}

}       // namespace cypher::engine::fs
