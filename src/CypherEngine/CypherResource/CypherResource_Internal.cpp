//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherResource/CypherResource_Internal.cpp
//  Purpose: Implements private resource tables, lookup, and handle resolution.
//  Details: A fixed-capacity open-addressed index provides allocation-free lookup
//           after initialization. Free and live lists make slot reuse and reverse
//           load-order shutdown deterministic.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherResource_Internal.h"

namespace cypher::engine::resource::detail
{

namespace
{

template <typename type_t>
common::bool_t AppendArrayLayout(
    common::usize nCount,
    common::usize &iCursor,
    common::usize &iOffsetOut ) noexcept
{
    common::usize cbArray = 0u;
    if ( !common::Cy_TryArrayByteCount<type_t>( nCount, cbArray ) ) {
        return common::CY_FALSE;
    }

    common::usize iAligned = 0u;
    if ( !common::Cy_AlignUpChecked(
            iCursor,
            alignof( type_t ),
            iAligned ) ||
         cbArray > common::CY_USIZE_MAX - iAligned ) {
        return common::CY_FALSE;
    }

    iOffsetOut = iAligned;
    iCursor = iAligned + cbArray;
    return common::CY_TRUE;
}

common::u32 LookupCapacityFor( common::u32 cResources ) noexcept
{
    const common::u32 cRequired = cResources * 2u;
    common::u32 cCapacity = 1u;
    while ( cCapacity < cRequired ) {
        cCapacity <<= 1u;
    }
    return cCapacity;
}

common::resource_generation_t NextGeneration(
    common::resource_generation_t nGeneration ) noexcept
{
    return nGeneration >= common::CY_RESOURCE_GENERATION_MAX
        ? common::CY_RESOURCE_GENERATION_FIRST
        : nGeneration + 1u;
}

} // namespace

common::bool_t CalculateLayout(
    const resource_manager_config_t &config,
    resource_manager_layout_t &layoutOut ) noexcept
{
    layoutOut = {};
    layoutOut.cLookupCapacity = LookupCapacityFor(
        config.cResourceCapacity );

    common::usize iCursor = sizeof( resource_manager_impl_t );
    if ( !AppendArrayLayout<resource_record_t>(
            config.cResourceCapacity,
            iCursor,
            layoutOut.iRecordsOffset ) ||
         !AppendArrayLayout<resource_type_entry_t>(
            config.cTypeCapacity,
            iCursor,
            layoutOut.iTypesOffset ) ||
         !AppendArrayLayout<common::u32>(
            layoutOut.cLookupCapacity,
            iCursor,
            layoutOut.iLookupOffset ) ) {
        layoutOut = {};
        return common::CY_FALSE;
    }

    layoutOut.cbTotal = iCursor;
    return common::CY_TRUE;
}

common::bool_t InsertLookup(
    resource_manager_impl_t &impl,
    common::u32 iRecord ) noexcept
{
    const common::resource_id_t id = impl.pRecords[iRecord].id;
    common::u32 iLookup = HashLookupIndex( id, impl.cLookupCapacity );
    const common::u32 nMask = impl.cLookupCapacity - 1u;

    for ( common::u32 iProbe = 0u;
          iProbe < impl.cLookupCapacity;
          ++iProbe ) {
        const common::u32 nEntry = impl.pLookup[iLookup];
        if ( nEntry == CYPHER_RESOURCE_LOOKUP_EMPTY ) {
            impl.pLookup[iLookup] = iRecord + 1u;
            return common::CY_TRUE;
        }

        iLookup = ( iLookup + 1u ) & nMask;
    }

    return common::CY_FALSE;
}

common::bool_t RemoveLookup(
    resource_manager_impl_t &impl,
    common::u32 iRecord ) noexcept
{
    const common::resource_id_t id = impl.pRecords[iRecord].id;
    common::u32 iLookup = HashLookupIndex( id, impl.cLookupCapacity );
    const common::u32 nMask = impl.cLookupCapacity - 1u;

    for ( common::u32 iProbe = 0u;
          iProbe < impl.cLookupCapacity;
          ++iProbe ) {
        const common::u32 nEntry = impl.pLookup[iLookup];
        if ( nEntry == CYPHER_RESOURCE_LOOKUP_EMPTY ) {
            return common::CY_FALSE;
        }

        if ( nEntry - 1u == iRecord ) {
            // Clearing a slot in an open-addressed table would make later
            // entries in the same probe cluster unreachable. Remove and
            // reinsert that cluster so unloaded resources leave no tombstones
            // and long-running editor sessions retain bounded miss costs.
            impl.pLookup[iLookup] = CYPHER_RESOURCE_LOOKUP_EMPTY;
            common::u32 iCluster = ( iLookup + 1u ) & nMask;
            while ( impl.pLookup[iCluster] != CYPHER_RESOURCE_LOOKUP_EMPTY ) {
                const common::u32 iClusterRecord =
                    impl.pLookup[iCluster] - 1u;
                impl.pLookup[iCluster] = CYPHER_RESOURCE_LOOKUP_EMPTY;
                if ( !InsertLookup( impl, iClusterRecord ) ) {
                    return common::CY_FALSE;
                }
                iCluster = ( iCluster + 1u ) & nMask;
            }
            return common::CY_TRUE;
        }
        iLookup = ( iLookup + 1u ) & nMask;
    }

    return common::CY_FALSE;
}

common::u32 AllocateRecord( resource_manager_impl_t &impl ) noexcept
{
    if ( impl.iFreeHead == CYPHER_RESOURCE_INVALID_INDEX ) {
        return CYPHER_RESOURCE_INVALID_INDEX;
    }

    const common::u32 iRecord = impl.iFreeHead;
    resource_record_t &record = impl.pRecords[iRecord];
    impl.iFreeHead = record.iNextFree;
    record.iNextFree = CYPHER_RESOURCE_INVALID_INDEX;
    return iRecord;
}

void RecycleRecord(
    resource_manager_impl_t &impl,
    common::u32 iRecord ) noexcept
{
    resource_record_t &record = impl.pRecords[iRecord];
    const common::resource_generation_t nNextGeneration =
        NextGeneration( record.nGeneration );
    record = {};
    record.nGeneration = nNextGeneration;
    record.iNextFree = impl.iFreeHead;
    record.iPreviousLive = CYPHER_RESOURCE_INVALID_INDEX;
    record.iNextLive = CYPHER_RESOURCE_INVALID_INDEX;
    impl.iFreeHead = iRecord;
}

void AppendLiveRecord(
    resource_manager_impl_t &impl,
    common::u32 iRecord ) noexcept
{
    resource_record_t &record = impl.pRecords[iRecord];
    record.iPreviousLive = impl.iLiveTail;
    record.iNextLive = CYPHER_RESOURCE_INVALID_INDEX;

    if ( impl.iLiveTail != CYPHER_RESOURCE_INVALID_INDEX ) {
        impl.pRecords[impl.iLiveTail].iNextLive = iRecord;
    } else {
        impl.iLiveHead = iRecord;
    }
    impl.iLiveTail = iRecord;
}

void RemoveLiveRecord(
    resource_manager_impl_t &impl,
    common::u32 iRecord ) noexcept
{
    resource_record_t &record = impl.pRecords[iRecord];
    if ( record.iPreviousLive != CYPHER_RESOURCE_INVALID_INDEX ) {
        impl.pRecords[record.iPreviousLive].iNextLive = record.iNextLive;
    } else {
        impl.iLiveHead = record.iNextLive;
    }

    if ( record.iNextLive != CYPHER_RESOURCE_INVALID_INDEX ) {
        impl.pRecords[record.iNextLive].iPreviousLive = record.iPreviousLive;
    } else {
        impl.iLiveTail = record.iPreviousLive;
    }

    record.iPreviousLive = CYPHER_RESOURCE_INVALID_INDEX;
    record.iNextLive = CYPHER_RESOURCE_INVALID_INDEX;
}

resource_error_t UnloadRecord(
    resource_manager_impl_t &impl,
    common::u32 iRecord ) noexcept
{
    resource_record_t &record = impl.pRecords[iRecord];
    resource_type_entry_t *pType = FindType( impl, record.type );
    if ( pType == nullptr || pType->loader.pfnUnload == nullptr ) {
        return resource_error_t::INTERNAL_ERROR;
    }

    record.state = resource_state_t::UNLOADING;
    RemoveLiveRecord( impl, iRecord );
    --impl.stats.cLiveResources;

    ++impl.cCallbackDepth;
    pType->loader.pfnUnload(
        pType->loader.pUserData,
        record.pResource );
    --impl.cCallbackDepth;

    if ( !RemoveLookup( impl, iRecord ) ) {
        return resource_error_t::INTERNAL_ERROR;
    }

    ++impl.stats.cUnloads;
    RecycleRecord( impl, iRecord );
    return resource_error_t::OK;
}

} // namespace cypher::engine::resource::detail
