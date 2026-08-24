//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherResource/CypherResource_Types.h
//  Purpose: Declares the public data contracts used by CypherResource.
//  Details: These types describe manager configuration, loader callbacks,
//           resource state, diagnostics, and the opaque manager facade without
//           exposing the runtime table implementation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RESOURCE_TYPES_H
#define CYPHER_ENGINE_RESOURCE_TYPES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_ResourceHandle.h"
#include "CypherCommon_ResourceId.h"

namespace cypher::engine::resource
{

inline constexpr common::u32 CYPHER_RESOURCE_DEFAULT_CAPACITY = 1024u;       // Initial number of simultaneously live resource records.
inline constexpr common::u32 CYPHER_RESOURCE_DEFAULT_TYPE_CAPACITY = 64u;   // Initial number of registered loader types.
inline constexpr common::u32 CYPHER_RESOURCE_MAX_CAPACITY =
    common::CY_RESOURCE_SLOT_MAX + 1u;
inline constexpr common::usize CYPHER_RESOURCE_PATH_BUFFER_SIZE = 260u;
inline constexpr common::usize CYPHER_RESOURCE_PATH_MAX_LENGTH =
    CYPHER_RESOURCE_PATH_BUFFER_SIZE - 1u;

enum class resource_error_t : common::u8 {
    OK = 0,                    // Operation completed successfully.
    INVALID_ARGUMENT,         // A pointer, identifier, path, or configuration is invalid.
    NOT_INITIALIZED,          // Manager storage has not been created.
    ALREADY_INITIALIZED,      // Initialization was requested for a live manager.
    ALLOCATION_FAILED,        // Manager or payload storage could not be allocated.
    CAPACITY_EXCEEDED,        // No free resource record remains.
    TYPE_CAPACITY_EXCEEDED,   // No loader-registration slot remains.
    TYPE_ALREADY_REGISTERED,  // A loader already owns this persistent type ID.
    TYPE_NOT_REGISTERED,      // No loader exists for the requested type ID.
    TYPE_IN_USE,              // Live resources prevent removal of their loader.
    PATH_TOO_LONG,            // Normalized virtual path exceeds fixed record storage.
    ID_COLLISION,             // One stable ID resolved to a different path or type.
    LOAD_FAILED,              // Registered loader rejected or could not create the payload.
    INVALID_HANDLE,           // Slot, generation, or runtime type does not name a live record.
    RESOURCE_BUSY,            // Resource is already in a lifecycle transition.
    DEPENDENCY_CYCLE,         // Recursive loading returned to a resource already loading.
    REFERENCE_OVERFLOW,       // Reference counter cannot be incremented safely.
    REENTRANT_LIFECYCLE,      // Loader callback attempted a forbidden manager lifecycle operation.
    INTERNAL_ERROR            // Manager invariant failed without a more specific public code.
};

enum class resource_state_t : common::u8 {
    EMPTY = 0,                // Record belongs to the free list and has no payload.
    LOADING,                  // Loader callback is currently creating the payload.
    READY,                    // Payload is valid and may be borrowed by callers.
    FAILED,                   // Most recent load attempt failed before publication.
    UNLOADING                 // Unload callback is currently destroying the payload.
};

struct resource_manager_t;

// Creates one backend-specific payload for a normalized virtual path.
using resource_load_fn_t = common::bool_t ( * )(
    void *pUserData,
    common::resource_id_t id,
    common::resource_type_id_t type,
    common::string_view_t normalizedVirtualPath,
    void **ppResourceOut ) noexcept;

// Destroys one payload previously returned by the matching load callback.
using resource_unload_fn_t = void ( * )(
    void *pUserData,
    void *pResource ) noexcept;

struct resource_loader_t {
    common::resource_type_id_t type{};                // Persistent type identity stored in authored/cooked data.
    resource_load_fn_t pfnLoad{ nullptr };            // Creates one runtime payload for this type.
    resource_unload_fn_t pfnUnload{ nullptr };        // Destroys payloads created by pfnLoad.
    void *pUserData{ nullptr };                       // Borrowed backend context passed to both callbacks.
};

struct resource_manager_config_t {
    common::u32 cResourceCapacity{ CYPHER_RESOURCE_DEFAULT_CAPACITY };       // Maximum simultaneously live records.
    common::u32 cTypeCapacity{ CYPHER_RESOURCE_DEFAULT_TYPE_CAPACITY };      // Maximum loader registrations.
    const common::allocator_t *pAllocator{ nullptr };                        // Borrowed allocator; null selects system allocation.
};

struct resource_info_t {
    common::resource_id_t id{};                 // Stable identity derived from normalized path and persistent type.
    common::resource_handle_t handle{};         // Transient generation-checked runtime handle.
    common::resource_type_id_t type{};          // Persistent loader/resource type identity.
    resource_state_t state{ resource_state_t::EMPTY }; // Current lifecycle state.
    common::u32 cReferences{ 0u };              // Number of retained runtime references.
    char szVirtualPath[CYPHER_RESOURCE_PATH_BUFFER_SIZE]{}; // Canonical VFS path used for loading and diagnostics.
};

struct resource_manager_stats_t {
    common::u32 cResourceCapacity{ 0u };         // Configured record capacity.
    common::u32 cTypeCapacity{ 0u };             // Configured loader capacity.
    common::u32 cRegisteredTypes{ 0u };          // Loader registrations currently live.
    common::u32 cLiveResources{ 0u };            // Records currently outside the free list.
    common::u32 cPeakLiveResources{ 0u };        // High-water mark since initialization.
    common::u64 cLoadAttempts{ 0u };             // Calls that entered a loader callback.
    common::u64 cSuccessfulLoads{ 0u };           // Payloads successfully published.
    common::u64 cFailedLoads{ 0u };               // Loader callbacks that failed to publish a payload.
    common::u64 cCacheHits{ 0u };                 // Acquires satisfied by an existing ready record.
    common::u64 cUnloads{ 0u };                   // Payloads destroyed after final release or shutdown.
};

// Small owning facade; all mutable tables remain private to the implementation.
struct resource_manager_t {
    void *pImplementation{ nullptr };                    // Opaque base of the manager's single backing allocation.
    const common::allocator_t *pAllocator{ nullptr };    // Allocator that must release pImplementation.
    common::usize cbAllocation{ 0u };                    // Exact byte count passed back during deallocation.

    resource_manager_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( resource_manager_t );
};

} // namespace cypher::engine::resource

#endif // CYPHER_ENGINE_RESOURCE_TYPES_H
