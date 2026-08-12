//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherResource/CypherResource_Internal.h
//  Purpose: Declares private tables and shared operations for CypherResource.
//  Details: This header is internal to the runtime library. It keeps public
//           resource contracts opaque while allowing lifecycle, registry, and
//           access code to share one validated table implementation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RESOURCE_INTERNAL_H
#define CYPHER_ENGINE_RESOURCE_INTERNAL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherResource.h"

namespace cypher::engine::resource::detail
{

inline constexpr common::u32 CYPHER_RESOURCE_INVALID_INDEX =
    common::CY_U32_MAX;
inline constexpr common::u32 CYPHER_RESOURCE_LOOKUP_EMPTY = 0u;

struct resource_type_entry_t {
    resource_loader_t loader{};
    common::resource_type_slot_t iTypeSlot{
        common::CY_RESOURCE_TYPE_SLOT_INVALID };
};

struct resource_record_t {
    common::resource_id_t id{};
    common::resource_type_id_t type{};
    common::resource_generation_t nGeneration{
        common::CY_RESOURCE_GENERATION_FIRST };
    common::resource_type_slot_t iTypeSlot{
        common::CY_RESOURCE_TYPE_SLOT_INVALID };
    resource_state_t state{ resource_state_t::EMPTY };
    common::u32 cReferences{ 0u };
    void *pResource{ nullptr };
    common::u32 iNextFree{ CYPHER_RESOURCE_INVALID_INDEX };
    common::u32 iPreviousLive{ CYPHER_RESOURCE_INVALID_INDEX };
    common::u32 iNextLive{ CYPHER_RESOURCE_INVALID_INDEX };
    common::usize cchVirtualPath{ 0u };
    char szVirtualPath[CYPHER_RESOURCE_PATH_BUFFER_SIZE]{};
};

struct resource_manager_impl_t {
    resource_record_t *pRecords{ nullptr };
    resource_type_entry_t *pTypes{ nullptr };
    common::u32 *pLookup{ nullptr };
    common::u32 cResourceCapacity{ 0u };
    common::u32 cTypeCapacity{ 0u };
    common::u32 cLookupCapacity{ 0u };
    common::u32 cRegisteredTypes{ 0u };
    common::resource_type_slot_t iNextTypeSlot{
        common::CY_RESOURCE_TYPE_SLOT_INVALID + 1u };
    common::u32 iFreeHead{ CYPHER_RESOURCE_INVALID_INDEX };
    common::u32 iLiveHead{ CYPHER_RESOURCE_INVALID_INDEX };
    common::u32 iLiveTail{ CYPHER_RESOURCE_INVALID_INDEX };
    common::u32 cCallbackDepth{ 0u };
    common::bool_t bShuttingDown{ common::CY_FALSE };
    resource_manager_stats_t stats{};
};

struct resource_manager_layout_t {
    common::usize cbTotal{ 0u };
    common::usize iRecordsOffset{ 0u };
    common::usize iTypesOffset{ 0u };
    common::usize iLookupOffset{ 0u };
    common::u32 cLookupCapacity{ 0u };
};

CYPHER_NODISCARD common::bool_t CalculateLayout(
    const resource_manager_config_t &config,
    resource_manager_layout_t &layoutOut ) noexcept;

inline resource_manager_impl_t *ManagerImpl(
    resource_manager_t *pManager ) noexcept
{
    return pManager != nullptr
        ? static_cast<resource_manager_impl_t *>( pManager->pImplementation )
        : nullptr;
}

inline const resource_manager_impl_t *ManagerImpl(
    const resource_manager_t *pManager ) noexcept
{
    return pManager != nullptr
        ? static_cast<const resource_manager_impl_t *>( pManager->pImplementation )
        : nullptr;
}

inline resource_error_t RequireManager(
    resource_manager_t *pManager,
    resource_manager_impl_t *&pImplOut ) noexcept
{
    pImplOut = ManagerImpl( pManager );
    return pImplOut != nullptr
        ? resource_error_t::OK
        : resource_error_t::NOT_INITIALIZED;
}

inline resource_error_t RequireManager(
    const resource_manager_t *pManager,
    const resource_manager_impl_t *&pImplOut ) noexcept
{
    pImplOut = ManagerImpl( pManager );
    return pImplOut != nullptr
        ? resource_error_t::OK
        : resource_error_t::NOT_INITIALIZED;
}

inline common::u32 HashLookupIndex(
    common::resource_id_t id,
    common::u32 cLookupCapacity ) noexcept
{
    common::u64 nValue = id.value;
    nValue ^= nValue >> 33u;
    nValue *= 0xff51afd7ed558ccdull;
    nValue ^= nValue >> 33u;
    nValue *= 0xc4ceb9fe1a85ec53ull;
    nValue ^= nValue >> 33u;
    return static_cast<common::u32>( nValue ) & ( cLookupCapacity - 1u );
}

inline common::u32 LookupRecord(
    const resource_manager_impl_t &impl,
    common::resource_id_t id ) noexcept
{
    common::u32 iLookup = HashLookupIndex( id, impl.cLookupCapacity );
    const common::u32 nMask = impl.cLookupCapacity - 1u;

    for ( common::u32 iProbe = 0u;
          iProbe < impl.cLookupCapacity;
          ++iProbe ) {
        const common::u32 nEntry = impl.pLookup[iLookup];
        if ( nEntry == CYPHER_RESOURCE_LOOKUP_EMPTY ) {
            return CYPHER_RESOURCE_INVALID_INDEX;
        }

        const common::u32 iRecord = nEntry - 1u;
        if ( common::ResourceId_Equals( impl.pRecords[iRecord].id, id ) ) {
            return iRecord;
        }

        iLookup = ( iLookup + 1u ) & nMask;
    }

    return CYPHER_RESOURCE_INVALID_INDEX;
}
CYPHER_NODISCARD common::bool_t InsertLookup(
    resource_manager_impl_t &impl,
    common::u32 iRecord ) noexcept;
CYPHER_NODISCARD common::bool_t RemoveLookup(
    resource_manager_impl_t &impl,
    common::u32 iRecord ) noexcept;

inline resource_type_entry_t *FindType(
    resource_manager_impl_t &impl,
    common::resource_type_id_t type ) noexcept
{
    for ( common::u32 iType = 0u;
          iType < impl.cRegisteredTypes;
          ++iType ) {
        if ( impl.pTypes[iType].loader.type == type ) {
            return &impl.pTypes[iType];
        }
    }
    return nullptr;
}

inline const resource_type_entry_t *FindType(
    const resource_manager_impl_t &impl,
    common::resource_type_id_t type ) noexcept
{
    for ( common::u32 iType = 0u;
          iType < impl.cRegisteredTypes;
          ++iType ) {
        if ( impl.pTypes[iType].loader.type == type ) {
            return &impl.pTypes[iType];
        }
    }
    return nullptr;
}

CYPHER_NODISCARD common::u32 AllocateRecord(
    resource_manager_impl_t &impl ) noexcept;
void RecycleRecord(
    resource_manager_impl_t &impl,
    common::u32 iRecord ) noexcept;
void AppendLiveRecord(
    resource_manager_impl_t &impl,
    common::u32 iRecord ) noexcept;
void RemoveLiveRecord(
    resource_manager_impl_t &impl,
    common::u32 iRecord ) noexcept;

inline resource_error_t ResolveHandle(
    const resource_manager_impl_t &impl,
    common::resource_handle_t handle,
    common::u32 &iRecordOut ) noexcept
{
    iRecordOut = CYPHER_RESOURCE_INVALID_INDEX;
    if ( !common::ResourceHandle_IsValid( handle ) ) {
        return resource_error_t::INVALID_HANDLE;
    }

    const common::resource_handle_parts_t parts =
        common::ResourceHandle_Unpack( handle );
    if ( parts.iSlot >= impl.cResourceCapacity ) {
        return resource_error_t::INVALID_HANDLE;
    }

    const resource_record_t &record = impl.pRecords[parts.iSlot];
    if ( record.state == resource_state_t::EMPTY ||
         record.nGeneration != parts.nGeneration ||
         record.iTypeSlot != parts.iTypeSlot ) {
        return resource_error_t::INVALID_HANDLE;
    }

    iRecordOut = parts.iSlot;
    return resource_error_t::OK;
}

inline common::bool_t VirtualPathHasEmbeddedNull(
    common::string_view_t path ) noexcept
{
    for ( common::usize iByte = 0u; iByte < path.cchLength; ++iByte ) {
        if ( path.pData[iByte] == '\0' ) {
            return common::CY_TRUE;
        }
    }
    return common::CY_FALSE;
}

inline common::bool_t RecordPathEquals(
    const resource_record_t &record,
    common::string_view_t path ) noexcept
{
    return common::StringView_Equals(
        { record.szVirtualPath, record.cchVirtualPath },
        path );
}
CYPHER_NODISCARD resource_error_t UnloadRecord(
    resource_manager_impl_t &impl,
    common::u32 iRecord ) noexcept;

} // namespace cypher::engine::resource::detail

#endif // CYPHER_ENGINE_RESOURCE_INTERNAL_H
