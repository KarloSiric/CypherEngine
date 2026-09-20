//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Attributes_BrushSideStore.cpp
//  Purpose: Implements bounded, failure-atomic brush-side attribute storage.
//  Details: Failure atomicity rests on vector_t reserving new storage before
//           relocating into it, so a failed allocation leaves the original
//           buffer intact. Every mutator here reserves first and only then
//           commits, which keeps that guarantee end to end.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_Attributes_BrushSideStore.h"

namespace cypher::editor::geometry
{

namespace
{

using cypher::common::bool_t;

bool_t IsInitialized(
    const geometry_brush_side_attribute_store_t *pStore ) noexcept
{
    // An allocator binding is what Vector_Init establishes, so its presence is
    // the honest test for "this store has been initialized" -- a zeroed store
    // has none, and nothing else about vector_t distinguishes the two states.
    return pStore != nullptr && pStore->records.pAllocator != nullptr;
}

} // namespace

geometry_status_t BrushSideAttributeStore_Init(
    geometry_brush_side_attribute_store_t *pStore,
    const allocator_t *pAllocator ) noexcept
{
    if ( pStore == nullptr || pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pStore->records.pAllocator != nullptr ) {
        // Re-initializing would leak the existing allocation and silently
        // detach any indices the caller still holds.
        return geometry_status_t::ALREADY_INITIALIZED;
    }
    if ( !common::Vector_Init( &pStore->records, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void BrushSideAttributeStore_Shutdown(
    geometry_brush_side_attribute_store_t *pStore ) noexcept
{
    if ( pStore == nullptr || pStore->records.pAllocator == nullptr ) {
        return;
    }
    common::Vector_Shutdown( &pStore->records );
}

usize BrushSideAttributeStore_Count(
    const geometry_brush_side_attribute_store_t *pStore ) noexcept
{
    if ( !IsInitialized( pStore ) ) {
        return 0u;
    }
    return common::Vector_Count( &pStore->records );
}

geometry_status_t BrushSideAttributeStore_TryReserve(
    geometry_brush_side_attribute_store_t *pStore,
    const geometry_limit_policy_t &limits,
    usize nCapacity ) noexcept
{
    if ( !IsInitialized( pStore ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    // Checked before allocating, so an absurd request fails immediately rather
    // than after the allocator has spent time trying to satisfy it.
    if ( static_cast<common::u64>( nCapacity ) > limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !common::Vector_Reserve( &pStore->records, nCapacity ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

geometry_status_t BrushSideAttributeStore_TryAppend(
    geometry_brush_side_attribute_store_t *pStore,
    const geometry_policy_t &policy,
    const geometry_brush_side_attributes_t &attributes,
    usize *pIndexOut ) noexcept
{
    if ( !IsInitialized( pStore ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    // Validate before touching storage. Appending first and rolling back on a
    // failed check would work, but it would also mean the store briefly holds a
    // record it would never accept, which any concurrent read or diagnostic
    // dump would observe.
    const geometry_status_t validation =
        BrushSideAttributes_Validate( policy.numerical, attributes );
    if ( validation != geometry_status_t::OK ) {
        return validation;
    }

    const usize nCount = common::Vector_Count( &pStore->records );
    if ( static_cast<common::u64>( nCount ) >= policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }
    if ( !common::Vector_PushBack( &pStore->records, attributes ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    if ( pIndexOut != nullptr ) {
        *pIndexOut = nCount;
    }
    return geometry_status_t::OK;
}

geometry_status_t BrushSideAttributeStore_TryGet(
    const geometry_brush_side_attribute_store_t *pStore,
    usize iIndex,
    geometry_brush_side_attributes_t *pAttributesOut ) noexcept
{
    if ( pAttributesOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pAttributesOut = {};
    if ( !IsInitialized( pStore ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iIndex >= common::Vector_Count( &pStore->records ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pAttributesOut = pStore->records.pData[iIndex];
    return geometry_status_t::OK;
}

geometry_status_t BrushSideAttributeStore_TrySet(
    geometry_brush_side_attribute_store_t *pStore,
    const geometry_numerical_policy_t &policy,
    usize iIndex,
    const geometry_brush_side_attributes_t &attributes ) noexcept
{
    if ( !IsInitialized( pStore ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iIndex >= common::Vector_Count( &pStore->records ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // Validated before the assignment, so a rejected replacement leaves the
    // existing record exactly as it was.
    const geometry_status_t validation =
        BrushSideAttributes_Validate( policy, attributes );
    if ( validation != geometry_status_t::OK ) {
        return validation;
    }

    pStore->records.pData[iIndex] = attributes;
    return geometry_status_t::OK;
}

geometry_status_t BrushSideAttributeStore_TryCopyFrom(
    geometry_brush_side_attribute_store_t *pDestination,
    const geometry_brush_side_attribute_store_t *pSource,
    const geometry_limit_policy_t &limits ) noexcept
{
    if ( !IsInitialized( pDestination ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !IsInitialized( pSource ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pDestination == pSource ) {
        // Self-copy is already the identity, and doing it the long way would
        // clear the destination out from under the source read.
        return geometry_status_t::OK;
    }

    const usize nSourceCount = common::Vector_Count( &pSource->records );
    if ( static_cast<common::u64>( nSourceCount ) > limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    // Reserve the whole result before clearing anything. Clearing first would
    // be simpler but would destroy the destination's contents before learning
    // whether the copy can actually complete, which is precisely the
    // non-atomic behavior a rolled-back transaction cannot tolerate.
    if ( !common::Vector_Reserve( &pDestination->records, nSourceCount ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    common::Vector_Clear( &pDestination->records );
    for ( usize i = 0u; i < nSourceCount; ++i ) {
        // Cannot fail: capacity was secured above and Vector_Clear does not
        // release it, so this loop only constructs into reserved slots.
        ( void )common::Vector_PushBack(
            &pDestination->records, pSource->records.pData[i] );
    }
    return geometry_status_t::OK;
}

geometry_status_t BrushSideAttributeStore_Validate(
    const geometry_brush_side_attribute_store_t *pStore,
    const geometry_policy_t &policy ) noexcept
{
    if ( !IsInitialized( pStore ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    const usize nCount = common::Vector_Count( &pStore->records );
    if ( static_cast<common::u64>( nCount ) > policy.limits.cBrushSidesPerBrushMax ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    // Reports the first failure rather than a count, and in index order, so the
    // same malformed store always produces the same diagnosis.
    for ( usize i = 0u; i < nCount; ++i ) {
        const geometry_status_t status = BrushSideAttributes_Validate(
            policy.numerical, pStore->records.pData[i] );
        if ( status != geometry_status_t::OK ) {
            return status;
        }
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
