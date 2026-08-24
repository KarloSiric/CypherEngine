//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_DataManager.h
//  Purpose: Declares an instance-owned named pointer registry.
//  Details: DataManager is a narrow integration utility, not a service locator. Entries
//           state whether the registry owns destruction of each registered pointer.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_DATAMANAGER_H
#define CYPHER_COMMON_TIER1_DATAMANAGER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

using data_destroy_fn_t = void ( * )(
    void *pData,
    void *pUserData ) noexcept;

struct data_entry_desc_t {
    string_view_t name{};                    // Unique byte-exact registry name.
    void *pData{ nullptr };                  // Opaque object stored by the registry.
    data_destroy_fn_t pfnDestroy{ nullptr }; // Optional ownership-release callback.
    void *pUserData{ nullptr };              // Opaque state passed to pfnDestroy.
};

struct data_manager_t;

// Names are copied and compared byte-for-byte. The registry is not thread-safe.
// Clear and Destroy invoke owned callbacks in reverse registration order.

CYPHER_NODISCARD CYPHER_COMMON_API
data_manager_t *DataManager_Create(
    const allocator_t *pAllocator,
    usize nInitialCapacity = 64u ) noexcept;

CYPHER_COMMON_API void DataManager_Destroy( data_manager_t *pManager ) noexcept;
CYPHER_COMMON_API void DataManager_Clear( data_manager_t *pManager ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t DataManager_Register(
    data_manager_t *pManager,
    const data_entry_desc_t &entry ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
void *DataManager_Find(
    const data_manager_t *pManager,
    string_view_t name ) noexcept;

// Removes an entry and invokes its destroy callback when one was registered.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t DataManager_Remove(
    data_manager_t *pManager,
    string_view_t name ) noexcept;

// Removes an entry without invoking its destroy callback and transfers its pointer.
CYPHER_NODISCARD CYPHER_COMMON_API
void *DataManager_Detach(
    data_manager_t *pManager,
    string_view_t name ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_DATAMANAGER_H
