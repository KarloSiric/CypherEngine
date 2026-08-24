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
	bool exists{ false };                                    // Path existed when this snapshot was captured.
	bool bIsDirectory{ false };                               // Entry represents a directory rather than a file.
	common::u64 size{ 0u };                                   // File size used for polling-based change detection.
	std::time_t nModifiedTime{};                              // Modification time used for polling-based comparison.
	char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};  // Engine-visible path reported in watch events.
	char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Host path queried by the watcher.
};

struct watch_t {
	watch_handle_t handle{ CYPHER_FILESYSTEM_INVALID_WATCH }; // Process-local identity returned to the caller.
	common::u32 flags{ CYPHER_FILESYSTEM_WATCH_NONE };        // watch_flags_t target and recursion policy.

	char szVirtualPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};  // Normalized watch root in the virtual namespace.
	char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Resolved host watch root.

	watch_snapshot_entry_t snapshot[CYPHER_FILESYSTEM_MAX_WATCH_SNAPSHOT_ENTRIES]{}; // Previous polling state.
	common::u32 nSnapshotCount{ 0u };                         // Valid prefix length in snapshot.

	void *pNativeHandle{ nullptr };                           // Reserved platform-native watcher state.
};

struct async_worker_result_t {
	fs_error_t error{ fs_error_t::ERR_INVALID_HANDLE };       // Worker completion status.
	common::u64 nBytesTransferred{ 0u };                      // Bytes completed before success or failure.
};

struct async_request_state_t {
	async_request_t handle{ CYPHER_FILESYSTEM_INVALID_ASYNC_REQUEST }; // Process-local request identity.
	async_status_t status{ async_status_t::INVALID };         // Public lifecycle state.
	bool used{ false };                                       // Slot belongs to a live request.
	bool cancelled{ false };                                  // Cooperative cancellation was requested.
	bool bResultCached{ false };                              // future result has been copied into result.
	async_result_t result{};                                  // Stable result available after future consumption.
	std::shared_future<async_worker_result_t> future{};        // Worker completion channel.
};

/*
================
Filesystem Runtime

Shared state and helpers used only by filesystem implementation files.
================
*/
struct runtime_state_t {
	bool initialized{ false };                                // Global filesystem services are ready for use.
	mount_t mounts[CYPHER_FILESYSTEM_MAX_MOUNTS]{};           // Priority-ordered live mount prefix.
	common::u32 nMountCount{ 0u };                            // Valid prefix length in mounts.
	mount_handle_t nNextMountHandle{ 1u };                    // Monotonic handle source; zero remains invalid.
	char szWritePath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};    // Dedicated physical root for all mutations.
	stats_t stats{};                                          // Process-lifetime I/O counters.

	async_request_state_t pAsyncRequests[CYPHER_FILESYSTEM_MAX_ASYNC_REQUESTS]{}; // Fixed request slots.
	async_request_t nextAsyncRequest{ 1u };                   // Monotonic request handle source.

	/*
	 * File watching elements.
	 */
	watch_t watches[CYPHER_FILESYSTEM_MAX_WATCHES]{};         // Dense active watch prefix.
	common::u32 nWatchCount{ 0u };                            // Valid prefix length in watches.
	watch_handle_t nNextWatchHandle{ 1u };                    // Monotonic watch identity source.
	watch_t watchScratch{};                                   // Reusable snapshot workspace; protected by runtime mutex.

	watch_event_t pWatchEvents[CYPHER_FILESYSTEM_MAX_WATCH_EVENTS]{}; // Bounded event ring.
	common::u32 nWatchEventReadIndex{ 0u };                   // Oldest unread event slot.
	common::u32 nWatchEventWriteIndex{ 0u };                  // Slot filled by the next emitted event.
	common::u32 nWatchEventCount{ 0u };                       // Unread event count, capped at ring capacity.
};

struct resolved_file_t {
	file_backend_t backend{ file_backend_t::INVALID };        // Backend that won mount resolution.
	mount_handle_t mount{ CYPHER_FILESYSTEM_INVALID_MOUNT };  // Winning mount identity.
	common::u32 nMountIndex{ 0u };                            // Runtime mount-table index valid under the mutex.
	common::u32 nPackageFileIndex{ 0u };                      // Entry index when backend is PACKAGE_FILE.
	void *pPackageReader{ nullptr };                          // Borrowed reader owned by the mount.
	common::u64 nFileSize{ 0u };                              // Logical resolved file size in bytes.
	bool bIsDirectory{ false };                               // Resolved object is a directory.
	char szNormalizedPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Canonical requested virtual path.
	char szPhysicalPath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{}; // Resolved host file path for OS_FILE.
	char szPackagePath[CYPHER_FILESYSTEM_MAX_PATH_LENGTH]{};  // Physical archive path for PACKAGE_FILE.
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
