//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_CookSourceMap.cpp
//  Purpose: Implements cooked-primitive source lookups.
//
//  History:
//  - Created by Karlo Siric on 2026-09-24
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherGeometry_CookSourceMap.h"

namespace cypher::editor::geometry
{

geometry_status_t CookSourceMap_TryResolve(
    const common::vector_t<geometry_cook_source_t> *pSources,
    common::usize iPrimitive,
    geometry_cook_source_t *pSourceOut ) noexcept
{
    if ( pSourceOut == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pSourceOut = {};
    if ( pSources == nullptr || pSources->pAllocator == nullptr ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( iPrimitive >= common::Vector_Count( pSources ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *pSourceOut = pSources->pData[iPrimitive];
    return geometry_status_t::OK;
}

common::usize CookSourceMap_CountFrom(
    const common::vector_t<geometry_cook_source_t> *pSources,
    geometry_source_id_t rootId,
    geometry_source_id_t componentId ) noexcept
{
    if ( pSources == nullptr || pSources->pAllocator == nullptr ) {
        return 0u;
    }
    common::usize cCount = 0u;
    for ( common::usize i = 0u; i < common::Vector_Count( pSources ); ++i ) {
        const geometry_cook_source_t &source = pSources->pData[i];
        if ( source.rootId.value == rootId.value &&
             ( !GeometrySourceId_IsValid( componentId ) ||
               source.componentId.value == componentId.value ) ) {
            ++cCount;
        }
    }
    return cCount;
}

} // namespace cypher::editor::geometry
