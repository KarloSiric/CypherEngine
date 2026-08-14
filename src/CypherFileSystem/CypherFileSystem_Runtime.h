//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherFileSystem/CypherFileSystem_Runtime.h
//  Purpose: Declares the CypherFileSystem FileSystem Runtime module.
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

#ifndef CYPHER_ENGINE_FILESYSTEM_RUNTIME_H
#define CYPHER_ENGINE_FILESYSTEM_RUNTIME_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherFileSystem.h"
#include "CypherSystem_Platform.h"

#include <future>
#include <mutex>

namespace cypher::engine::fs {

/*
================
Watch filesystem runtime structs

Used for building up the watch structs and entires for communicating with the OS watching.
================
*/
struct watch_snapshot_entry_t {
	bool exists{ false };
	bool bIsDirectory{ false };
	common::u64 size{ 0u };
	std::time_t nModifiedTime{};
	char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
	char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
};

struct watch_t {
	watch_handle_t handle{ CYPHER_FILESYSTEM_INVALID_WATCH };
	common::u32 flags{ CYPHER_FILESYSTEM_WATCH_NONE };

	char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
	char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};

	watch_snapshot_entry_t snapshot[CYPHER_FILESYSTEM_MAX_WATCH_SNAPSHOT_ENTRIES]{};
	common::u32 nSnapshotCount{ 0u };

	void *pNativeHandle{ nullptr };
};

struct async_worker_result_t {
	fs_error_t error{ fs_error_t::ERR_INVALID_HANDLE };
	common::u64 nBytesTransferred{ 0u };
};

struct async_request_state_t {
	async_request_t handle{ CYPHER_FILESYSTEM_INVALID_ASYNC_REQUEST };
	async_status_t status{ async_status_t::INVALID };
	bool used{ false };
	bool cancelled{ false };
	bool bResultCached{ false };
	async_result_t result{};
	std::shared_future<async_worker_result_t> future{};
};

/*
================
Filesystem Runtime

Shared state and helpers used only by filesystem implementation files.
================
*/
struct runtime_state_t {
	bool initialized{ false };
	mount_t mounts[CYPHER_FILESYSTEM_MAX_MOUNTS]{};
	common::u32 nMountCount{ 0u };
	mount_handle_t nNextMountHandle{ 1u };
	char szWritePath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
	stats_t stats{};

	async_request_state_t pAsyncRequests[CYPHER_FILESYSTEM_MAX_ASYNC_REQUESTS]{};
	async_request_t nextAsyncRequest{ 1u };

	/*
	 * File watching elements.
	 */
	watch_t watches[CYPHER_FILESYSTEM_MAX_WATCHES]{};
	common::u32 nWatchCount{ 0u };
	watch_handle_t nNextWatchHandle{ 1u };
	watch_t watchScratch{};

	watch_event_t pWatchEvents[CYPHER_FILESYSTEM_MAX_WATCH_EVENTS]{};
	common::u32 nWatchEventReadIndex{ 0u };
	common::u32 nWatchEventWriteIndex{ 0u };
	common::u32 nWatchEventCount{ 0u };
};

struct resolved_file_t {
	file_backend_t backend{ file_backend_t::INVALID };
	mount_handle_t mount{ CYPHER_FILESYSTEM_INVALID_MOUNT };
	common::u32 nMountIndex{ 0u };
	common::u32 nPackageFileIndex{ 0u };
	void *pPackageReader{ nullptr };
	common::u64 nFileSize{ 0u };
	bool bIsDirectory{ false };
	char szNormalizedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
	char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
	char szPackagePath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};
};

runtime_state_t &CypherFileSystem_RuntimeState();

std::recursive_mutex &CypherFileSystem_RuntimeMutex();

mount_handle_t CypherFileSystem_AllocateMountHandle( runtime_state_t &state );

fs_error_t CypherFileSystem_InsertMountByPriority(
	runtime_state_t &state,
	const mount_t &mount );

void CypherFileSystem_RemoveMountAtIndex(
	runtime_state_t &state,
	common::u32 index );

fs_error_t CypherFileSystem_ResolveReadableFile(
	const char *szVirtualPath,
	resolved_file_t &fileOut );

bool CypherFileSystem_HasWritePath();

fs_error_t CypherFileSystem_BuildWritePath(
	const char *szVirtualPath,
	char *szOutPath,
	common::u32 nOutPathSize );

void CypherFileSystem_ShutdownAsyncRequests();

} // namespace cypher::engine::fs

#endif // CYPHER_ENGINE_FILESYSTEM_RUNTIME_H
