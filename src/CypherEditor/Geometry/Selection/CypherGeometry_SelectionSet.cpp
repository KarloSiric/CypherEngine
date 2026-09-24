//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SelectionSet.cpp
//  Purpose: Implements typed geometry component selection sets.
//  Details: Membership is tracked in a flat vector with linear duplicate
//           checks. Removal uses swap-erase to avoid shifting. At typical
//           selection sizes (tens to hundreds) this is faster than a hash
//           set due to cache locality and zero hashing overhead.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_SelectionSet.h"

namespace cypher::editor::geometry
{

namespace
{

bool LevelIsValid( geometry_selection_level_t level ) noexcept
{
    return level != geometry_selection_level_t::INVALID &&
           level < geometry_selection_level_t::COUNT;
}

bool SelectionIsInitialized(
    const geometry_selection_set_t *pSet ) noexcept
{
    return pSet != nullptr && pSet->ids.pAllocator != nullptr &&
           common::Vector_IsValid( &pSet->ids ) &&
           LevelIsValid( pSet->level );
}

bool SelectionIsCanonicalEmpty(
    const geometry_selection_set_t &set ) noexcept
{
    return set.ids.pData == nullptr && set.ids.nCount == 0u &&
           set.ids.nCapacity == 0u && set.ids.pAllocator == nullptr &&
           set.level == geometry_selection_level_t::INVALID;
}

common::usize FindIdIndex(
    const common::vector_t<geometry_source_id_t> &ids,
    geometry_source_id_t id ) noexcept
{
    for ( common::usize i = 0u; i < ids.nCount; ++i ) {
        if ( ids.pData[i].value == id.value ) {
            return i;
        }
    }
    return ids.nCount;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

geometry_status_t GeometrySelectionSet_Init(
    geometry_selection_set_t *pSet,
    const common::allocator_t *pAllocator,
    geometry_selection_level_t level ) noexcept
{
    if ( pSet == nullptr || pAllocator == nullptr ||
         !common::Allocator_IsValid( pAllocator ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !LevelIsValid( level ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pSet->ids.pAllocator != nullptr ) {
        return SelectionIsInitialized( pSet )
            ? geometry_status_t::ALREADY_INITIALIZED
            : geometry_status_t::CORRUPT_STATE;
    }
    if ( !SelectionIsCanonicalEmpty( *pSet ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    if ( !common::Vector_Init( &pSet->ids, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }

    pSet->level = level;
    return geometry_status_t::OK;
}

void GeometrySelectionSet_Shutdown(
    geometry_selection_set_t *pSet ) noexcept
{
    if ( pSet == nullptr ) {
        return;
    }
    common::Vector_Shutdown( &pSet->ids );
    pSet->level = geometry_selection_level_t::INVALID;
}

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

geometry_status_t GeometrySelectionSet_TryAdd(
    geometry_selection_set_t *pSet,
    geometry_source_id_t id ) noexcept
{
    if ( !SelectionIsInitialized( pSet ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( id ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    if ( FindIdIndex( pSet->ids, id ) < pSet->ids.nCount ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }

    if ( !common::Vector_PushBack( &pSet->ids, id ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometrySelectionSet_TryRemove(
    geometry_selection_set_t *pSet,
    geometry_source_id_t id ) noexcept
{
    if ( !SelectionIsInitialized( pSet ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( id ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::usize iIndex = FindIdIndex( pSet->ids, id );
    if ( iIndex >= pSet->ids.nCount ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    common::Vector_EraseSwap( &pSet->ids, iIndex );
    return geometry_status_t::OK;
}

geometry_status_t GeometrySelectionSet_Toggle(
    geometry_selection_set_t *pSet,
    geometry_source_id_t id,
    bool *pIsSelectedOut ) noexcept
{
    if ( pIsSelectedOut != nullptr ) {
        *pIsSelectedOut = false;
    }
    if ( !SelectionIsInitialized( pSet ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( id ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::usize iIndex = FindIdIndex( pSet->ids, id );
    if ( iIndex < pSet->ids.nCount ) {
        common::Vector_EraseSwap( &pSet->ids, iIndex );
        if ( pIsSelectedOut != nullptr ) {
            *pIsSelectedOut = false;
        }
    } else {
        if ( !common::Vector_PushBack( &pSet->ids, id ) ) {
            return geometry_status_t::ALLOCATION_FAILED;
        }
        if ( pIsSelectedOut != nullptr ) {
            *pIsSelectedOut = true;
        }
    }
    return geometry_status_t::OK;
}

void GeometrySelectionSet_Clear(
    geometry_selection_set_t *pSet ) noexcept
{
    if ( !SelectionIsInitialized( pSet ) ) {
        return;
    }
    common::Vector_Clear( &pSet->ids );
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

common::usize GeometrySelectionSet_Count(
    const geometry_selection_set_t *pSet ) noexcept
{
    if ( !SelectionIsInitialized( pSet ) ) {
        return 0u;
    }
    return common::Vector_Count( &pSet->ids );
}

bool GeometrySelectionSet_Contains(
    const geometry_selection_set_t *pSet,
    geometry_source_id_t id ) noexcept
{
    if ( !SelectionIsInitialized( pSet ) ||
         !GeometrySourceId_IsValid( id ) ) {
        return false;
    }
    return FindIdIndex( pSet->ids, id ) < pSet->ids.nCount;
}

bool GeometrySelectionSet_IsEmpty(
    const geometry_selection_set_t *pSet ) noexcept
{
    return GeometrySelectionSet_Count( pSet ) == 0u;
}

geometry_selection_level_t GeometrySelectionSet_GetLevel(
    const geometry_selection_set_t *pSet ) noexcept
{
    if ( !SelectionIsInitialized( pSet ) ) {
        return geometry_selection_level_t::INVALID;
    }
    return pSet->level;
}

geometry_status_t GeometrySelectionSet_TryGetAll(
    const geometry_selection_set_t *pSet,
    geometry_source_id_t *pIdsOut,
    common::usize nCapacity,
    common::usize *pCountOut ) noexcept
{
    if ( pCountOut != nullptr ) {
        *pCountOut = 0u;
    }
    if ( pSet == nullptr || pCountOut == nullptr ||
         ( pIdsOut == nullptr && nCapacity > 0u ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !SelectionIsInitialized( pSet ) ) {
        return geometry_status_t::NOT_INITIALIZED;
    }

    const common::usize cTotal = pSet->ids.nCount;
    const common::usize cCopy =
        ( cTotal < nCapacity ) ? cTotal : nCapacity;

    for ( common::usize i = 0u; i < cCopy; ++i ) {
        pIdsOut[i] = pSet->ids.pData[i];
    }

    *pCountOut = cTotal;
    if ( cTotal > nCapacity ) {
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
