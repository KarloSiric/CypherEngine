//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherResource/CypherResource_Access.cpp
//  Purpose: Implements resource acquisition, references, and payload access.
//  Details: Acquisition performs identity lookup before invoking a registered
//           loader. Failed loads roll back transactionally, while generation-safe
//           handles reject access after a slot has been recycled.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherResource_Internal.h"

namespace cypher::engine::resource
{

resource_error_t CypherResource_Acquire(
    resource_manager_t *pManager,
    common::resource_type_id_t type,
    common::string_view_t normalizedVirtualPath,
    common::resource_handle_t *pHandleOut ) noexcept
{
    using namespace detail;

    if ( pHandleOut != nullptr ) {
        *pHandleOut = common::CY_RESOURCE_HANDLE_INVALID;
    }
    if ( pHandleOut == nullptr ||
         type == 0u ||
         !common::StringView_IsValid( normalizedVirtualPath ) ||
         common::StringView_IsEmpty( normalizedVirtualPath ) ||
         VirtualPathHasEmbeddedNull( normalizedVirtualPath ) ) {
        return resource_error_t::INVALID_ARGUMENT;
    }
    if ( normalizedVirtualPath.cchLength > CYPHER_RESOURCE_PATH_MAX_LENGTH ) {
        return resource_error_t::PATH_TOO_LONG;
    }

    resource_manager_impl_t *pImpl = nullptr;
    const resource_error_t managerResult = RequireManager( pManager, pImpl );
    if ( managerResult != resource_error_t::OK ) {
        return managerResult;
    }
    if ( pImpl->bShuttingDown ) {
        return resource_error_t::RESOURCE_BUSY;
    }

    resource_type_entry_t *pType = FindType( *pImpl, type );
    if ( pType == nullptr ) {
        return resource_error_t::TYPE_NOT_REGISTERED;
    }

    const common::resource_id_t id = common::ResourceId_FromPath(
        normalizedVirtualPath,
        type );
    if ( !common::ResourceId_IsValid( id ) ) {
        return resource_error_t::INVALID_ARGUMENT;
    }

    const common::u32 iExisting = LookupRecord( *pImpl, id );
    if ( iExisting != CYPHER_RESOURCE_INVALID_INDEX ) {
        resource_record_t &record = pImpl->pRecords[iExisting];
        if ( record.type != type ||
             !RecordPathEquals( record, normalizedVirtualPath ) ) {
            return resource_error_t::ID_COLLISION;
        }
        if ( record.state == resource_state_t::LOADING ) {
            return resource_error_t::DEPENDENCY_CYCLE;
        }
        if ( record.state != resource_state_t::READY ) {
            return resource_error_t::RESOURCE_BUSY;
        }
        if ( record.cReferences == common::CY_U32_MAX ) {
            return resource_error_t::REFERENCE_OVERFLOW;
        }

        ++record.cReferences;
        ++pImpl->stats.cCacheHits;
        *pHandleOut = common::ResourceHandle_Make(
            iExisting,
            record.nGeneration,
            record.iTypeSlot );
        return resource_error_t::OK;
    }

    const common::u32 iRecord = AllocateRecord( *pImpl );
    if ( iRecord == CYPHER_RESOURCE_INVALID_INDEX ) {
        return resource_error_t::CAPACITY_EXCEEDED;
    }

    resource_record_t &record = pImpl->pRecords[iRecord];
    record.id = id;
    record.type = type;
    record.iTypeSlot = pType->iTypeSlot;
    record.state = resource_state_t::LOADING;
    record.cchVirtualPath = normalizedVirtualPath.cchLength;
    const common::usize cchCopied = common::StringView_CopyToCString(
        normalizedVirtualPath,
        record.szVirtualPath,
        CYPHER_RESOURCE_PATH_BUFFER_SIZE );
    if ( cchCopied != normalizedVirtualPath.cchLength ) {
        RecycleRecord( *pImpl, iRecord );
        return resource_error_t::INTERNAL_ERROR;
    }

    // Publish the loading record before the callback so recursive acquisition
    // reports a dependency cycle instead of loading the same identity twice.
    if ( !InsertLookup( *pImpl, iRecord ) ) {
        RecycleRecord( *pImpl, iRecord );
        return resource_error_t::INTERNAL_ERROR;
    }

    ++pImpl->stats.cLoadAttempts;
    void *pResource = nullptr;
    ++pImpl->cCallbackDepth;
    const common::bool_t bLoaded = pType->loader.pfnLoad(
        pType->loader.pUserData,
        id,
        type,
        normalizedVirtualPath,
        &pResource );
    --pImpl->cCallbackDepth;

    if ( !bLoaded || pResource == nullptr ) {
        record.state = resource_state_t::FAILED;
        record.pResource = pResource;
        ++pImpl->stats.cFailedLoads;
        if ( pResource != nullptr ) {
            ++pImpl->cCallbackDepth;
            pType->loader.pfnUnload(
                pType->loader.pUserData,
                pResource );
            --pImpl->cCallbackDepth;
        }
        if ( !RemoveLookup( *pImpl, iRecord ) ) {
            return resource_error_t::INTERNAL_ERROR;
        }
        RecycleRecord( *pImpl, iRecord );
        return resource_error_t::LOAD_FAILED;
    }

    record.pResource = pResource;
    record.cReferences = 1u;
    record.state = resource_state_t::READY;
    AppendLiveRecord( *pImpl, iRecord );
    ++pImpl->stats.cLiveResources;
    ++pImpl->stats.cSuccessfulLoads;
    if ( pImpl->stats.cLiveResources > pImpl->stats.cPeakLiveResources ) {
        pImpl->stats.cPeakLiveResources = pImpl->stats.cLiveResources;
    }

    *pHandleOut = common::ResourceHandle_Make(
        iRecord,
        record.nGeneration,
        record.iTypeSlot );
    return resource_error_t::OK;
}

resource_error_t CypherResource_Retain(
    resource_manager_t *pManager,
    common::resource_handle_t handle ) noexcept
{
    using namespace detail;

    resource_manager_impl_t *pImpl = nullptr;
    const resource_error_t managerResult = RequireManager( pManager, pImpl );
    if ( managerResult != resource_error_t::OK ) {
        return managerResult;
    }
    if ( pImpl->bShuttingDown ) {
        return resource_error_t::RESOURCE_BUSY;
    }

    common::u32 iRecord = CYPHER_RESOURCE_INVALID_INDEX;
    const resource_error_t handleResult = ResolveHandle(
        *pImpl,
        handle,
        iRecord );
    if ( handleResult != resource_error_t::OK ) {
        return handleResult;
    }

    resource_record_t &record = pImpl->pRecords[iRecord];
    if ( record.state != resource_state_t::READY ) {
        return resource_error_t::RESOURCE_BUSY;
    }
    if ( record.cReferences == common::CY_U32_MAX ) {
        return resource_error_t::REFERENCE_OVERFLOW;
    }

    ++record.cReferences;
    return resource_error_t::OK;
}

resource_error_t CypherResource_Release(
    resource_manager_t *pManager,
    common::resource_handle_t handle ) noexcept
{
    using namespace detail;

    resource_manager_impl_t *pImpl = nullptr;
    const resource_error_t managerResult = RequireManager( pManager, pImpl );
    if ( managerResult != resource_error_t::OK ) {
        return managerResult;
    }

    common::u32 iRecord = CYPHER_RESOURCE_INVALID_INDEX;
    const resource_error_t handleResult = ResolveHandle(
        *pImpl,
        handle,
        iRecord );
    if ( handleResult != resource_error_t::OK ) {
        return handleResult;
    }

    resource_record_t &record = pImpl->pRecords[iRecord];
    if ( record.state != resource_state_t::READY ||
         record.cReferences == 0u ) {
        return resource_error_t::RESOURCE_BUSY;
    }

    --record.cReferences;
    return record.cReferences == 0u
        ? UnloadRecord( *pImpl, iRecord )
        : resource_error_t::OK;
}

resource_error_t CypherResource_Get(
    const resource_manager_t *pManager,
    common::resource_handle_t handle,
    void **ppResourceOut ) noexcept
{
    using namespace detail;

    if ( ppResourceOut != nullptr ) {
        *ppResourceOut = nullptr;
    }
    if ( ppResourceOut == nullptr ) {
        return resource_error_t::INVALID_ARGUMENT;
    }

    const resource_manager_impl_t *pImpl = nullptr;
    const resource_error_t managerResult = RequireManager( pManager, pImpl );
    if ( managerResult != resource_error_t::OK ) {
        return managerResult;
    }

    common::u32 iRecord = CYPHER_RESOURCE_INVALID_INDEX;
    const resource_error_t handleResult = ResolveHandle(
        *pImpl,
        handle,
        iRecord );
    if ( handleResult != resource_error_t::OK ) {
        return handleResult;
    }

    const resource_record_t &record = pImpl->pRecords[iRecord];
    if ( record.state != resource_state_t::READY ||
         record.pResource == nullptr ) {
        return resource_error_t::RESOURCE_BUSY;
    }

    *ppResourceOut = record.pResource;
    return resource_error_t::OK;
}

common::bool_t CypherResource_IsAlive(
    const resource_manager_t *pManager,
    common::resource_handle_t handle ) noexcept
{
    using namespace detail;

    const resource_manager_impl_t *pImpl = ManagerImpl( pManager );
    if ( pImpl == nullptr ) {
        return common::CY_FALSE;
    }

    common::u32 iRecord = CYPHER_RESOURCE_INVALID_INDEX;
    return ResolveHandle( *pImpl, handle, iRecord ) == resource_error_t::OK &&
           pImpl->pRecords[iRecord].state == resource_state_t::READY;
}

resource_error_t CypherResource_GetInfo(
    const resource_manager_t *pManager,
    common::resource_handle_t handle,
    resource_info_t *pInfoOut ) noexcept
{
    using namespace detail;

    if ( pInfoOut != nullptr ) {
        *pInfoOut = {};
    }
    if ( pInfoOut == nullptr ) {
        return resource_error_t::INVALID_ARGUMENT;
    }

    const resource_manager_impl_t *pImpl = nullptr;
    const resource_error_t managerResult = RequireManager( pManager, pImpl );
    if ( managerResult != resource_error_t::OK ) {
        return managerResult;
    }

    common::u32 iRecord = CYPHER_RESOURCE_INVALID_INDEX;
    const resource_error_t handleResult = ResolveHandle(
        *pImpl,
        handle,
        iRecord );
    if ( handleResult != resource_error_t::OK ) {
        return handleResult;
    }

    const resource_record_t &record = pImpl->pRecords[iRecord];
    pInfoOut->id = record.id;
    pInfoOut->handle = handle;
    pInfoOut->type = record.type;
    pInfoOut->state = record.state;
    pInfoOut->cReferences = record.cReferences;
    const common::usize cchCopied = common::StringView_CopyToCString(
        { record.szVirtualPath, record.cchVirtualPath },
        pInfoOut->szVirtualPath,
        CYPHER_RESOURCE_PATH_BUFFER_SIZE );
    if ( cchCopied != record.cchVirtualPath ) {
        *pInfoOut = {};
        return resource_error_t::INTERNAL_ERROR;
    }
    return resource_error_t::OK;
}

resource_manager_stats_t CypherResource_GetStats(
    const resource_manager_t *pManager ) noexcept
{
    const detail::resource_manager_impl_t *pImpl = detail::ManagerImpl( pManager );
    return pImpl != nullptr
        ? pImpl->stats
        : resource_manager_stats_t{};
}

} // namespace cypher::engine::resource
