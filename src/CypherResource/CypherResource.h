//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherResource/CypherResource.h
//  Purpose: Declares the engine runtime resource manager.
//  Details: The manager owns resource identity, reference counts, generation-safe
//           handles, and loader dispatch. Public structures are separated into
//           CypherResource_Types.h so consumers can share the contract cleanly.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RESOURCE_H
#define CYPHER_ENGINE_RESOURCE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherResource_Types.h"

namespace cypher::engine::resource
{

/*
================
CypherResource

Synchronous runtime resource ownership for the first playable engine slice.

Rules:
- Virtual paths must already be normalized by CypherFileSystem.
- One successful acquire owns one reference; every reference must be released.
- A non-null loader output transfers payload ownership to the manager, even when
  the loader reports failure. The matching unload callback then cleans it up.
- Load and unload callbacks may acquire or release dependent resources, but must
  not initialize, shut down, register, or unregister this manager recursively.
- The manager is owner-thread only. External synchronization is required when
  callers use it from more than one thread.
================
*/

// Returns practical capacities and the process-lifetime system allocator.
CYPHER_NODISCARD resource_manager_config_t
CypherResource_DefaultConfig() noexcept;

// Allocates manager tables and establishes the initial free-slot generations.
CYPHER_NODISCARD resource_error_t CypherResource_Init(
    resource_manager_t *pManager,
    const resource_manager_config_t &config ) noexcept;

// Forces every live payload through its unload callback and releases all tables.
CYPHER_NODISCARD resource_error_t CypherResource_Shutdown(
    resource_manager_t *pManager ) noexcept;

CYPHER_NODISCARD common::bool_t CypherResource_IsInitialized(
    const resource_manager_t *pManager ) noexcept;

// Registers one stable resource type and returns its compact runtime type slot.
CYPHER_NODISCARD resource_error_t CypherResource_RegisterType(
    resource_manager_t *pManager,
    const resource_loader_t &loader,
    common::resource_type_slot_t *pTypeSlotOut = nullptr ) noexcept;

// Removes an unused type registration. Runtime type slots are not reused.
CYPHER_NODISCARD resource_error_t CypherResource_UnregisterType(
    resource_manager_t *pManager,
    common::resource_type_id_t type ) noexcept;

// Finds or synchronously loads a resource and acquires one reference.
CYPHER_NODISCARD resource_error_t CypherResource_Acquire(
    resource_manager_t *pManager,
    common::resource_type_id_t type,
    common::string_view_t normalizedVirtualPath,
    common::resource_handle_t *pHandleOut ) noexcept;

// Acquires another reference from an already-live handle.
CYPHER_NODISCARD resource_error_t CypherResource_Retain(
    resource_manager_t *pManager,
    common::resource_handle_t handle ) noexcept;

// Releases one reference and unloads the payload when the count reaches zero.
CYPHER_NODISCARD resource_error_t CypherResource_Release(
    resource_manager_t *pManager,
    common::resource_handle_t handle ) noexcept;

// Returns a borrowed payload pointer valid while at least one reference remains.
CYPHER_NODISCARD resource_error_t CypherResource_Get(
    const resource_manager_t *pManager,
    common::resource_handle_t handle,
    void **ppResourceOut ) noexcept;

CYPHER_NODISCARD common::bool_t CypherResource_IsAlive(
    const resource_manager_t *pManager,
    common::resource_handle_t handle ) noexcept;

// Copies stable diagnostics for one live resource.
CYPHER_NODISCARD resource_error_t CypherResource_GetInfo(
    const resource_manager_t *pManager,
    common::resource_handle_t handle,
    resource_info_t *pInfoOut ) noexcept;

CYPHER_NODISCARD resource_manager_stats_t CypherResource_GetStats(
    const resource_manager_t *pManager ) noexcept;

CYPHER_NODISCARD const char *CypherResource_ErrorName(
    resource_error_t error ) noexcept;

} // namespace cypher::engine::resource

#endif // CYPHER_ENGINE_RESOURCE_H
