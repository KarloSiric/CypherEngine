//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherGeometry_Scratch.inl
//  Purpose: Implements typed geometry-scratch allocation helpers.
//
//  History:
//  - Created by Karlo Siric on 2026-09-18
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_EDITOR_GEOMETRY_SCRATCH_INL
#define CYPHER_EDITOR_GEOMETRY_SCRATCH_INL

#include <type_traits>

namespace cypher::editor::geometry
{

template <typename type_t>
geometry_status_t GeometryScratch_AllocateArrayStorage(
    geometry_scratch_t *pScratch,
    usize nCount,
    type_t **ppStorage,
    usize nAlignment ) noexcept
{
    static_assert( !std::is_void_v<type_t>,
                   "Geometry scratch cannot allocate an array of void." );

    if ( ppStorage == nullptr ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    *ppStorage = nullptr;

    if ( pScratch == nullptr || nCount == 0u ||
         nAlignment < alignof( type_t ) ||
         !common::Cy_AlignIsPowerOfTwo( nAlignment ) ) {
        return geometry_status_t::INVALID_ARGUMENT;
    }
    if ( !pScratch->bInitialized ) {
        return geometry_status_t::NOT_INITIALIZED;
    }
    if ( !GeometryScratch_IsValid( pScratch ) ) {
        return geometry_status_t::CORRUPT_STATE;
    }

    usize cbSize = 0u;
    if ( !common::Cy_TryArrayByteCount<type_t>( nCount, cbSize ) ) {
        return geometry_status_t::LIMIT_EXCEEDED;
    }

    void *pStorage = nullptr;
    const geometry_status_t status = GeometryScratch_Allocate(
        pScratch,
        cbSize,
        nAlignment,
        &pStorage );
    if ( status != geometry_status_t::OK ) {
        return status;
    }

    *ppStorage = static_cast<type_t *>( pStorage );
    return geometry_status_t::OK;
}

} // namespace cypher::editor::geometry

#endif // CYPHER_EDITOR_GEOMETRY_SCRATCH_INL
