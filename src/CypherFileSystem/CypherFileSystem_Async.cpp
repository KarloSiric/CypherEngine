//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherFileSystem/CypherFileSystem_Async.cpp
//  Purpose: Implements the CypherFileSystem FileSystem Async module.
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
Asynchronous I/O Notes

This first implementation uses one std::async task per request.  Request handles refer to a
fixed bookkeeping table, while read buffers remain caller-owned and write data is copied into
worker-owned storage.  Cancellation is cooperative at the result boundary: it changes the
reported result but cannot interrupt an operating-system call already in flight.
================
*/

#include "CypherFileSystem_Runtime.h"

#include <chrono>
#include <cstring>
#include <future>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace cypher::engine::fs
{

namespace {

async_request_state_t *FindAsyncRequest( runtime_state_t &state, async_request_t request )
{
    // The table is deliberately bounded; linear lookup keeps handle storage simple at
    // this scale and avoids allocating bookkeeping during an I/O request.
    for ( common::u32 i = 0u; i < CYPHER_FILESYSTEM_MAX_ASYNC_REQUESTS; ++i ) {
        async_request_state_t &slot = state.pAsyncRequests[i];
        if ( slot.used && slot.handle == request ) {
            return &slot;
        }
    }

    return nullptr;
}

async_request_state_t *AllocateAsyncRequest( runtime_state_t &state )
{
    // Prefer a never-used slot, then reclaim a terminal slot whose worker is known to
    // have stopped.  A future that is not ready still owns live worker state.
    for ( common::u32 i = 0u; i < CYPHER_FILESYSTEM_MAX_ASYNC_REQUESTS; ++i ) {
        async_request_state_t &slot = state.pAsyncRequests[i];
        if ( !slot.used ) {
            slot = async_request_state_t{};
            slot.used = true;
            return &slot;
        }
    }

    for ( common::u32 i = 0u; i < CYPHER_FILESYSTEM_MAX_ASYNC_REQUESTS; ++i ) {
        async_request_state_t &slot = state.pAsyncRequests[i];
        if ( slot.status == async_status_t::COMPLETE ||
             slot.status == async_status_t::FAILED ||
             slot.status == async_status_t::CANCELLED ) {
            if ( slot.future.valid() &&
                 slot.future.wait_for( std::chrono::seconds( 0 ) ) != std::future_status::ready ) {
                continue;
            }
            slot = async_request_state_t{};
            slot.used = true;
            return &slot;
        }
    }

    return nullptr;
}

async_request_t AllocateAsyncHandle( runtime_state_t &state )
{
    // Zero remains an invalid sentinel so default-initialized handles fail safely.
    async_request_t handle = state.nextAsyncRequest++;
    if ( handle == CYPHER_FILESYSTEM_INVALID_ASYNC_REQUEST ) {
        handle = state.nextAsyncRequest++;
    }

    return handle;
}

async_worker_result_t ReadAsyncWorker(
    const std::string szVirtualPath,
    void *buffer,
    const common::u64 nBytesToRead )
{
    // buffer is borrowed from the caller for the entire request lifetime.  The caller
    // must not release or mutate it until Poll/Wait reports a terminal status.
    common::u64 nBytesRead = 0u;
    const fs_error_t result = CypherFileSystem_ReadEntireFile(
        szVirtualPath.c_str(),
        buffer,
        nBytesToRead,
        nBytesRead );

    return { result, nBytesRead };
}

async_worker_result_t WriteAsyncWorker(
    const std::string szVirtualPath,
    std::vector<common::u8> data,
    const common::u64 nBytesToWrite )
{
    // Write requests move an owned byte copy into the worker, so caller storage may be
    // reused immediately after CypherFileSystem_WriteAsync returns.
    const void *buffer = data.empty() ? nullptr : data.data();
    const fs_error_t result = CypherFileSystem_WriteEntireFile(
        szVirtualPath.c_str(),
        buffer,
        nBytesToWrite );

    return { result, result == fs_error_t::OK ? nBytesToWrite : 0u };
}

async_result_t BuildPendingAsyncResult( const async_request_state_t &slot )
{
    async_result_t result{};
    result.status = slot.cancelled ? async_status_t::CANCELLED : slot.status;
    result.error = slot.cancelled ? fs_error_t::ERR_CANCELLED : fs_error_t::OK;
    result.nBytesTransferred = 0u;
    return result;
}

async_result_t BuildCompletedAsyncResult( async_request_state_t &slot )
{
    if ( slot.bResultCached ) {
        return slot.result;
    }

    // shared_future::get may rethrow a worker exception.  Exceptions are contained at
    // this boundary and translated into the filesystem's explicit error contract.
    async_worker_result_t workerResult{};
    try {
        workerResult = slot.future.get();
    } catch ( ... ) {
        workerResult.error = fs_error_t::ERR_IO_ERROR;
        workerResult.nBytesTransferred = 0u;
    }

    // Cancellation wins over a worker result, even if the underlying I/O completed.
    // It is a result-state policy, not an attempt to preempt a kernel operation.
    if ( slot.cancelled ) {
        slot.result.status = async_status_t::CANCELLED;
        slot.result.error = fs_error_t::ERR_CANCELLED;
        slot.result.nBytesTransferred = 0u;
    } else {
        slot.result.status = workerResult.error == fs_error_t::OK ? async_status_t::COMPLETE : async_status_t::FAILED;
        slot.result.error = workerResult.error;
        slot.result.nBytesTransferred = workerResult.nBytesTransferred;
    }

    slot.status = slot.result.status;
    slot.bResultCached = true;
    return slot.result;
}

fs_error_t SubmitAsyncRequest(
    async_request_state_t &slot,
    std::shared_future<async_worker_result_t> future,
    async_request_t &requestOut )
{
    runtime_state_t &state = CypherFileSystem_RuntimeState();

    // Publish the handle only after the worker future exists and the slot is complete.
    slot.handle = AllocateAsyncHandle( state );
    slot.status = async_status_t::RUNNING;
    slot.cancelled = false;
    slot.bResultCached = false;
    slot.result = {};
    slot.future = std::move( future );

    requestOut = slot.handle;
    return fs_error_t::OK;
}

}       // namespace

fs_error_t CypherFileSystem_ReadAsync(
    const char *szVirtualPath,
    void *buffer,
    common::u64 nBytesToRead,
    async_request_t &requestOut )
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    requestOut = CYPHER_FILESYSTEM_INVALID_ASYNC_REQUEST;

    if ( !CypherFileSystem_RuntimeState().initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }
    if ( nBytesToRead != 0u && buffer == nullptr ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }

    async_request_state_t *slot = AllocateAsyncRequest( CypherFileSystem_RuntimeState() );
    if ( slot == nullptr ) {
        return fs_error_t::ERR_OUT_OF_MEMORY;
    }

    // std::launch::async requires execution on another thread rather than permitting
    // deferred execution during Poll or Wait.
    try {
        std::shared_future<async_worker_result_t> future =
            std::async(
                std::launch::async,
                ReadAsyncWorker,
                std::string( szVirtualPath ),
                buffer,
                nBytesToRead ).share();

        return SubmitAsyncRequest( *slot, std::move( future ), requestOut );
    } catch ( const std::bad_alloc & ) {
        *slot = async_request_state_t{};
        return fs_error_t::ERR_OUT_OF_MEMORY;
    } catch ( ... ) {
        *slot = async_request_state_t{};
        return fs_error_t::ERR_IO_ERROR;
    }
}

fs_error_t CypherFileSystem_WriteAsync(
    const char *szVirtualPath,
    const void *buffer,
    common::u64 nBytesToWrite,
    async_request_t &requestOut )
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    requestOut = CYPHER_FILESYSTEM_INVALID_ASYNC_REQUEST;

    if ( !CypherFileSystem_RuntimeState().initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( szVirtualPath == nullptr || szVirtualPath[0] == '\0' ) {
        return fs_error_t::ERR_INVALID_PATH;
    }
    if ( nBytesToWrite != 0u && buffer == nullptr ) {
        return fs_error_t::ERR_INVALID_ARGUMENT;
    }

    // Copy write input before launching the worker.  This explicit ownership cost
    // prevents use-after-free when callers submit stack or transient arena storage.
    std::vector<common::u8> pWriteBuffer{};
    if ( nBytesToWrite > 0u ) {
        if ( nBytesToWrite > static_cast<common::u64>( std::numeric_limits<common::usize>::max() ) ) {
            return fs_error_t::ERR_OUT_OF_MEMORY;
        }

        try {
            pWriteBuffer.resize( static_cast<common::usize>( nBytesToWrite ) );
        } catch ( const std::bad_alloc & ) {
            return fs_error_t::ERR_OUT_OF_MEMORY;
        }

        std::memcpy( pWriteBuffer.data(), buffer, static_cast<common::usize>( nBytesToWrite ) );
    }

    async_request_state_t *slot = AllocateAsyncRequest( CypherFileSystem_RuntimeState() );
    if ( slot == nullptr ) {
        return fs_error_t::ERR_OUT_OF_MEMORY;
    }

    try {
        std::shared_future<async_worker_result_t> future =
            std::async(
                std::launch::async,
                WriteAsyncWorker,
                std::string( szVirtualPath ),
                std::move( pWriteBuffer ),
                nBytesToWrite ).share();

        return SubmitAsyncRequest( *slot, std::move( future ), requestOut );
    } catch ( const std::bad_alloc & ) {
        *slot = async_request_state_t{};
        return fs_error_t::ERR_OUT_OF_MEMORY;
    } catch ( ... ) {
        *slot = async_request_state_t{};
        return fs_error_t::ERR_IO_ERROR;
    }
}

fs_error_t CypherFileSystem_PollAsync( async_request_t request, async_result_t &resultOut )
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    resultOut = {};

    if ( !CypherFileSystem_RuntimeState().initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( request == CYPHER_FILESYSTEM_INVALID_ASYNC_REQUEST ) {
        resultOut.error = fs_error_t::ERR_INVALID_HANDLE;
        return fs_error_t::ERR_INVALID_HANDLE;
    }

    async_request_state_t *slot = FindAsyncRequest( CypherFileSystem_RuntimeState(), request );
    if ( slot == nullptr || !slot->future.valid() ) {
        resultOut.error = fs_error_t::ERR_INVALID_HANDLE;
        return fs_error_t::ERR_INVALID_HANDLE;
    }

    if ( slot->bResultCached ) {
        resultOut = slot->result;
        return fs_error_t::OK;
    }

    // Poll never blocks; an unfinished request returns a valid pending result.
    const std::future_status status = slot->future.wait_for( std::chrono::seconds( 0 ) );
    if ( status != std::future_status::ready ) {
        resultOut = BuildPendingAsyncResult( *slot );
        return fs_error_t::OK;
    }

    resultOut = BuildCompletedAsyncResult( *slot );
    return fs_error_t::OK;
}

fs_error_t CypherFileSystem_WaitAsync( async_request_t request, async_result_t &resultOut )
{
    std::shared_future<async_worker_result_t> future{};

    {
        std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
        resultOut = {};

        if ( !CypherFileSystem_RuntimeState().initialized ) {
            return fs_error_t::ERR_NOT_INIT;
        }
        if ( request == CYPHER_FILESYSTEM_INVALID_ASYNC_REQUEST ) {
            resultOut.error = fs_error_t::ERR_INVALID_HANDLE;
            return fs_error_t::ERR_INVALID_HANDLE;
        }

        async_request_state_t *slot = FindAsyncRequest( CypherFileSystem_RuntimeState(), request );
        if ( slot == nullptr || !slot->future.valid() ) {
            resultOut.error = fs_error_t::ERR_INVALID_HANDLE;
            return fs_error_t::ERR_INVALID_HANDLE;
        }
        if ( slot->bResultCached ) {
            resultOut = slot->result;
            return fs_error_t::OK;
        }

        // Copy the shared future, then release the runtime lock before blocking.  The
        // worker enters regular filesystem APIs which need this same recursive mutex.
        future = slot->future;
    }

    future.wait();
    return CypherFileSystem_PollAsync( request, resultOut );
}

fs_error_t CypherFileSystem_CancelAsync( async_request_t request )
{
    std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
    if ( !CypherFileSystem_RuntimeState().initialized ) {
        return fs_error_t::ERR_NOT_INIT;
    }
    if ( request == CYPHER_FILESYSTEM_INVALID_ASYNC_REQUEST ) {
        return fs_error_t::ERR_INVALID_HANDLE;
    }

    async_request_state_t *slot = FindAsyncRequest( CypherFileSystem_RuntimeState(), request );
    if ( slot == nullptr || !slot->future.valid() ) {
        return fs_error_t::ERR_INVALID_HANDLE;
    }
    if ( slot->status == async_status_t::COMPLETE ||
         slot->status == async_status_t::FAILED ||
         slot->status == async_status_t::CANCELLED ) {
        return fs_error_t::ERR_CANCELLED;
    }

    // std::future has no portable preemption operation.  Mark the public result as
    // cancelled and let the worker finish before its slot can be reclaimed.
    slot->cancelled = true;
    slot->status = async_status_t::CANCELLED;
    return fs_error_t::OK;
}

void CypherFileSystem_ShutdownAsyncRequests()
{
    std::shared_future<async_worker_result_t> futures[CYPHER_FILESYSTEM_MAX_ASYNC_REQUESTS]{};
    common::u32 nFutureCount = 0u;

    {
        std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
        runtime_state_t &state = CypherFileSystem_RuntimeState();
        for ( common::u32 i = 0u; i < CYPHER_FILESYSTEM_MAX_ASYNC_REQUESTS; ++i ) {
            async_request_state_t &slot = state.pAsyncRequests[i];
            if ( slot.used && slot.future.valid() ) {
                slot.cancelled = true;
                futures[nFutureCount++] = slot.future;
            }
        }
    }

    // Never wait while holding the filesystem mutex: workers may be blocked trying to
    // enter the synchronous read/write path guarded by that mutex.
    for ( common::u32 i = 0u; i < nFutureCount; ++i ) {
        futures[i].wait();
    }

    // Once every worker has stopped, clearing the slots invalidates all old handles.
    {
        std::lock_guard<std::recursive_mutex> lock( CypherFileSystem_RuntimeMutex() );
        runtime_state_t &state = CypherFileSystem_RuntimeState();
        for ( common::u32 i = 0u; i < CYPHER_FILESYSTEM_MAX_ASYNC_REQUESTS; ++i ) {
            state.pAsyncRequests[i] = async_request_state_t{};
        }
    }
}

}       // namespace cypher::engine::fs
