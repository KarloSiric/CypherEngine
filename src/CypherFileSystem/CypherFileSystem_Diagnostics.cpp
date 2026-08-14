//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherFileSystem/CypherFileSystem_Diagnostics.cpp
//  Purpose: Implements the CypherFileSystem FileSystem Diagnostics module.
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
#include "CypherLog.h"

namespace cypher::engine::fs
{

fs_error_t CypherFileSystem_GetStats( stats_t &statsOut )
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    const runtime_state_t &state = CypherFileSystem_RuntimeState();
    if ( !state.initialized ) {
        statsOut = {};
        return fs_error_t::ERR_NOT_INIT;
    }

    statsOut = state.stats;
    return fs_error_t::OK;
}

fs_error_t CypherFileSystem_ResetStats()
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    runtime_state_t &state = CypherFileSystem_RuntimeState();
    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }

    state.stats = {};
    return fs_error_t::OK;
}

fs_error_t CypherFileSystem_DumpMounts()
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    const runtime_state_t &state = CypherFileSystem_RuntimeState();
    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }

    LOG_INFO( log::channel_t::FS, "filesystem mounts: count=%u.", state.nMountCount );
    for ( common::u32 i = 0u; i < state.nMountCount; ++i ) {
        const mount_t &mount = state.mounts[i];
        LOG_INFO(
            log::channel_t::FS,
            "filesystem mount[%u]: handle=%u type=%u virtual='%s' physical='%s' flags=0x%x priority=%u.",
            i,
            mount.handle,
            static_cast<common::u32>( mount.type ),
            mount.szVirtualRoot[0] != '\0' ? mount.szVirtualRoot : "<root>",
            mount.szPhysicalRoot,
            mount.flags,
            mount.priority );
    }

    return fs_error_t::OK;
}

fs_error_t CypherFileSystem_DumpStats()
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    const runtime_state_t &state = CypherFileSystem_RuntimeState();
    if ( !state.initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }

    const stats_t &stats = state.stats;
    LOG_INFO(
        log::channel_t::FS,
        "filesystem stats: open=%llu close=%llu read=%llu write=%llu bytes_read=%llu bytes_written=%llu failed_lookup=%llu.",
        static_cast<unsigned long long>( stats.nOpenCount ),
        static_cast<unsigned long long>( stats.nCloseCount ),
        static_cast<unsigned long long>( stats.nReadCount ),
        static_cast<unsigned long long>( stats.nWriteCount ),
        static_cast<unsigned long long>( stats.nBytesRead ),
        static_cast<unsigned long long>( stats.nBytesWritten ),
        static_cast<unsigned long long>( stats.nFailedLookupCount ) );

    return fs_error_t::OK;
}

}       // namespace cypher::engine::fs
