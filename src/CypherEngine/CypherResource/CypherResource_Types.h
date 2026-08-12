//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherResource/CypherResource_Types.h
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

inline constexpr common::u32 CYPHER_RESOURCE_DEFAULT_CAPACITY = 1024u;
inline constexpr common::u32 CYPHER_RESOURCE_DEFAULT_TYPE_CAPACITY = 64u;
inline constexpr common::u32 CYPHER_RESOURCE_MAX_CAPACITY =
    common::CY_RESOURCE_SLOT_MAX + 1u;
inline constexpr common::usize CYPHER_RESOURCE_PATH_BUFFER_SIZE = 260u;
inline constexpr common::usize CYPHER_RESOURCE_PATH_MAX_LENGTH =
    CYPHER_RESOURCE_PATH_BUFFER_SIZE - 1u;

enum class resource_error_t : common::u8 {
    OK = 0,
    INVALID_ARGUMENT,
    NOT_INITIALIZED,
    ALREADY_INITIALIZED,
    ALLOCATION_FAILED,
    CAPACITY_EXCEEDED,
    TYPE_CAPACITY_EXCEEDED,
    TYPE_ALREADY_REGISTERED,
    TYPE_NOT_REGISTERED,
    TYPE_IN_USE,
    PATH_TOO_LONG,
    ID_COLLISION,
    LOAD_FAILED,
    INVALID_HANDLE,
    RESOURCE_BUSY,
    DEPENDENCY_CYCLE,
    REFERENCE_OVERFLOW,
    REENTRANT_LIFECYCLE,
    INTERNAL_ERROR
};

enum class resource_state_t : common::u8 {
    EMPTY = 0,
    LOADING,
    READY,
    FAILED,
    UNLOADING
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
    common::resource_type_id_t type{};
    resource_load_fn_t pfnLoad{ nullptr };
    resource_unload_fn_t pfnUnload{ nullptr };
    void *pUserData{ nullptr };
};

struct resource_manager_config_t {
    common::u32 cResourceCapacity{ CYPHER_RESOURCE_DEFAULT_CAPACITY };
    common::u32 cTypeCapacity{ CYPHER_RESOURCE_DEFAULT_TYPE_CAPACITY };
    const common::allocator_t *pAllocator{ nullptr };
};

struct resource_info_t {
    common::resource_id_t id{};
    common::resource_handle_t handle{};
    common::resource_type_id_t type{};
    resource_state_t state{ resource_state_t::EMPTY };
    common::u32 cReferences{ 0u };
    char szVirtualPath[CYPHER_RESOURCE_PATH_BUFFER_SIZE]{};
};

struct resource_manager_stats_t {
    common::u32 cResourceCapacity{ 0u };
    common::u32 cTypeCapacity{ 0u };
    common::u32 cRegisteredTypes{ 0u };
    common::u32 cLiveResources{ 0u };
    common::u32 cPeakLiveResources{ 0u };
    common::u64 cLoadAttempts{ 0u };
    common::u64 cSuccessfulLoads{ 0u };
    common::u64 cFailedLoads{ 0u };
    common::u64 cCacheHits{ 0u };
    common::u64 cUnloads{ 0u };
};

// Small owning facade; all mutable tables remain private to the implementation.
struct resource_manager_t {
    void *pImplementation{ nullptr };
    const common::allocator_t *pAllocator{ nullptr };
    common::usize cbAllocation{ 0u };

    resource_manager_t() noexcept = default;
    CYPHER_NO_COPY_MOVE( resource_manager_t );
};

} // namespace cypher::engine::resource

#endif // CYPHER_ENGINE_RESOURCE_TYPES_H
