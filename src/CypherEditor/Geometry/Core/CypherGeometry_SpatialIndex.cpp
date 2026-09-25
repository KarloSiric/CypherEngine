//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_SpatialIndex.cpp
//  Purpose: Implements the flat AABB broad-phase spatial index.
//  Details: All operations are linear scans. The index is small enough at
//           editor brush counts that cache-friendly flat iteration beats
//           tree traversal. Insert and remove use swap-erase to avoid
//           shifting the tail of the array.
//
//  History:
//  - Created by Karlo Siric on 2026-09-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_SpatialIndex.h"

namespace cypher::editor::geometry
{

namespace
{

// Linear scan for a brush by source ID. Returns the index or nCount
// if not found.
common::usize FindEntryIndex(
    const common::vector_t<spatial_index_entry_t> &entries,
    geometry_source_id_t brushId ) noexcept
{
    for ( common::usize i = 0u; i < entries.nCount; ++i ) {
        if ( entries.pData[i].brushId.value == brushId.value ) {
            return i;
        }
    }
    return entries.nCount;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

geometry_status_t GeometrySpatialIndex_Init(
    geometry_spatial_index_t *pIndex,
    const common::allocator_t *pAllocator ) noexcept
{
    if ( pIndex == nullptr || pAllocator == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pIndex->entries.pAllocator != nullptr ) {
        return geometry_status_t::ALREADY_INITIALIZED;
    }

    if ( !common::Vector_Init( &pIndex->entries, pAllocator, 0u ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

void GeometrySpatialIndex_Shutdown(
    geometry_spatial_index_t *pIndex ) noexcept
{
    if ( pIndex == nullptr ) {
        return;
    }
    common::Vector_Shutdown( &pIndex->entries );
}

// ---------------------------------------------------------------------------
// Mutation
// ---------------------------------------------------------------------------

geometry_status_t GeometrySpatialIndex_TryInsert(
    geometry_spatial_index_t *pIndex,
    geometry_source_id_t brushId,
    math::aabbd_t bounds ) noexcept
{
    if ( pIndex == nullptr || pIndex->entries.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( brushId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Aabbd_IsValid( bounds ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    if ( FindEntryIndex( pIndex->entries, brushId ) <
         pIndex->entries.nCount ) {
        return geometry_status_t::IDENTITY_CONFLICT;
    }

    spatial_index_entry_t entry{};
    entry.brushId = brushId;
    entry.bounds = bounds;

    if ( !common::Vector_PushBack( &pIndex->entries, entry ) ) {
        return geometry_status_t::ALLOCATION_FAILED;
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometrySpatialIndex_TryRemove(
    geometry_spatial_index_t *pIndex,
    geometry_source_id_t brushId ) noexcept
{
    if ( pIndex == nullptr || pIndex->entries.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( brushId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    const common::usize iIndex =
        FindEntryIndex( pIndex->entries, brushId );
    if ( iIndex >= pIndex->entries.nCount ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    common::Vector_EraseSwap( &pIndex->entries, iIndex );
    return geometry_status_t::OK;
}

geometry_status_t GeometrySpatialIndex_TryRefit(
    geometry_spatial_index_t *pIndex,
    geometry_source_id_t brushId,
    math::aabbd_t newBounds ) noexcept
{
    if ( pIndex == nullptr || pIndex->entries.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( brushId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !math::Aabbd_IsValid( newBounds ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::usize iIndex =
        FindEntryIndex( pIndex->entries, brushId );
    if ( iIndex >= pIndex->entries.nCount ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    pIndex->entries.pData[iIndex].bounds = newBounds;
    return geometry_status_t::OK;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

common::usize GeometrySpatialIndex_Count(
    const geometry_spatial_index_t *pIndex ) noexcept
{
    if ( pIndex == nullptr || pIndex->entries.pAllocator == nullptr ) {
        return 0u;
    }
    return common::Vector_Count( &pIndex->entries );
}

geometry_status_t GeometrySpatialIndex_QueryOverlap(
    const geometry_spatial_index_t *pIndex,
    math::aabbd_t queryBounds,
    geometry_source_id_t *pResultsOut,
    common::usize nCapacity,
    common::usize *pCountOut ) noexcept
{
    if ( pCountOut != nullptr ) {
        *pCountOut = 0u;
    }
    if ( pIndex == nullptr || pResultsOut == nullptr ||
         pCountOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pIndex->entries.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Aabbd_IsValid( queryBounds ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    common::usize cHits = 0u;
    for ( common::usize i = 0u; i < pIndex->entries.nCount; ++i ) {
        if ( math::Aabbd_Overlaps(
                 pIndex->entries.pData[i].bounds, queryBounds ) ) {
            if ( cHits < nCapacity ) {
                pResultsOut[cHits] = pIndex->entries.pData[i].brushId;
            }
            ++cHits;
        }
    }

    *pCountOut = cHits;
    if ( cHits > nCapacity ) {
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometrySpatialIndex_QueryPoint(
    const geometry_spatial_index_t *pIndex,
    math::vec3d_t queryPoint,
    geometry_source_id_t *pResultsOut,
    common::usize nCapacity,
    common::usize *pCountOut ) noexcept
{
    if ( pCountOut != nullptr ) {
        *pCountOut = 0u;
    }
    if ( pIndex == nullptr || pResultsOut == nullptr ||
         pCountOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pIndex->entries.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !math::Vec3d_IsFinite( queryPoint ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    common::usize cHits = 0u;
    for ( common::usize i = 0u; i < pIndex->entries.nCount; ++i ) {
        if ( math::Aabbd_ContainsPoint(
                 pIndex->entries.pData[i].bounds, queryPoint ) ) {
            if ( cHits < nCapacity ) {
                pResultsOut[cHits] = pIndex->entries.pData[i].brushId;
            }
            ++cHits;
        }
    }

    *pCountOut = cHits;
    if ( cHits > nCapacity ) {
        return geometry_status_t::INSUFFICIENT_CAPACITY;
    }
    return geometry_status_t::OK;
}

geometry_status_t GeometrySpatialIndex_TryGetBounds(
    const geometry_spatial_index_t *pIndex,
    geometry_source_id_t brushId,
    math::aabbd_t *pBoundsOut ) noexcept
{
    if ( pBoundsOut != nullptr ) {
        *pBoundsOut = math::CY_AABBD_EMPTY;
    }
    if ( pIndex == nullptr || pBoundsOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( pIndex->entries.pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometrySourceId_IsValid( brushId ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    const common::usize iIndex =
        FindEntryIndex( pIndex->entries, brushId );
    if ( iIndex >= pIndex->entries.nCount ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    *pBoundsOut = pIndex->entries.pData[iIndex].bounds;
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry
