//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Interface.h
//  Purpose: Declares instance-owned versioned interface factories.
//  Details: InterfaceRegistry supports module boundaries without C++ virtual ABI
//           coupling. Factories return opaque API tables whose versions are validated.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_INTERFACE_H
#define CYPHER_COMMON_TIER1_INTERFACE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

struct interface_id_t {
    string_view_t name{};
    u32 nMajorVersion{ 0u };
    u32 nMinorVersion{ 0u };
};

using interface_create_fn_t = void *( * )(
    const interface_id_t &requested,
    void *pUserData ) noexcept;

using interface_release_fn_t = void ( * )(
    void *pInterface,
    void *pUserData ) noexcept;

struct interface_factory_desc_t {
    // Registry copies the name. Callbacks and pUserData remain borrowed.
    interface_id_t provided{};
    interface_create_fn_t pfnCreate{ nullptr };
    interface_release_fn_t pfnRelease{ nullptr };
    void *pUserData{ nullptr };
};

struct interface_registry_t;

// A registry owns copied names but not callback state. One factory may be registered
// per name and major version; its provided minor version must satisfy requested minor.
// Live interfaces must be released before their factory or registry is removed.

CYPHER_NODISCARD CYPHER_COMMON_API
interface_registry_t *InterfaceRegistry_Create(
    const allocator_t *pAllocator,
    usize nInitialFactories = 32u ) noexcept;

CYPHER_COMMON_API void InterfaceRegistry_Destroy(
    interface_registry_t *pRegistry ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t InterfaceRegistry_Register(
    interface_registry_t *pRegistry,
    const interface_factory_desc_t &factory ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t InterfaceRegistry_Unregister(
    interface_registry_t *pRegistry,
    string_view_t name,
    u32 nMajorVersion ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
void *InterfaceRegistry_CreateInterface(
    const interface_registry_t *pRegistry,
    const interface_id_t &requested,
    interface_release_fn_t *ppfnReleaseOut = nullptr,
    void **ppReleaseUserDataOut = nullptr ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_INTERFACE_H
