//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_IdAllocator.cpp
//  Purpose: Implements monotonic persistent geometry identity allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_IdAllocator.h"

namespace cypher::editor::geometry
{

geometry_status_t GeometrySourceIdAllocator_Reset(
    geometry_source_id_allocator_t *pAllocator,
    geometry_source_id_t first ) noexcept
{
    if ( pAllocator == nullptr || !GeometrySourceId_IsValid( first ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    pAllocator->next = first;
    return geometry_status_t::OK;
}

geometry_source_id_result_t GeometrySourceIdAllocator_Allocate(
    geometry_source_id_allocator_t *pAllocator ) noexcept
{
    if ( pAllocator == nullptr ) {
        return { {}, geometry_status_t::INVALID_ARGUMENT };
    }
    if ( !GeometrySourceId_IsValid( pAllocator->next ) ) {
        return { {}, geometry_status_t::INSUFFICIENT_CAPACITY };
    }

    const geometry_source_id_t allocated = pAllocator->next;
    pAllocator->next = allocated.value == common::CY_U64_MAX
        ? GEOMETRY_SOURCE_ID_INVALID
        : geometry_source_id_t{ allocated.value + 1u };
    return { allocated, geometry_status_t::OK };
}

geometry_status_t GeometrySourceIdAllocator_AdvancePast(
    geometry_source_id_allocator_t *pAllocator,
    geometry_source_id_t observed ) noexcept
{
    if ( pAllocator == nullptr || !GeometrySourceId_IsValid( observed ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }

    // An exhausted allocator cannot be made less exhausted by observing input.
    if ( !GeometrySourceId_IsValid( pAllocator->next ) ||
         observed.value < pAllocator->next.value ) {
        return geometry_status_t::OK;
    }

    pAllocator->next = observed.value == common::CY_U64_MAX
        ? GEOMETRY_SOURCE_ID_INVALID
        : geometry_source_id_t{ observed.value + 1u };
    return geometry_status_t::OK;
}

bool GeometrySourceIdAllocator_IsExhausted(
    const geometry_source_id_allocator_t *pAllocator ) noexcept
{
    return pAllocator != nullptr &&
           !GeometrySourceId_IsValid( pAllocator->next );
}

} // namespace cypher::editor::geometry
