//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ContainerOps.inl
//  Purpose: Implements shared typed-container lifetime operations.
//  Details: Centralizes construction, destruction, copying, and relocation so
//           owning Tier1 containers enforce one no-exception object contract.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Container Ops Template Definitions

Algorithms operate only on the supplied range and callback contracts. Comparators must define a
consistent ordering; the implementation performs no hidden allocation unless explicitly
documented. Template definitions remain in this file so each concrete instantiation is compiled
at its call site.
================
*/

#ifndef CYPHER_COMMON_TIER1_CONTAINEROPS_INL
#define CYPHER_COMMON_TIER1_CONTAINEROPS_INL
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Assert.h"
#include "CypherCommon_MemoryOps.h"

#include <new>
#include <type_traits>

namespace cypher::common
{

template <typename type_t>
void Container_DefaultConstructRange(
    type_t *pDestination,
    usize nCount ) noexcept
{
    static_assert(
        std::is_nothrow_default_constructible_v<type_t>,
        "Container default construction must not throw." );

    for ( usize iIndex = 0u; iIndex < nCount; ++iIndex ) {
        ::new ( static_cast<void *>( pDestination + iIndex ) ) type_t{};
    }
}

template <typename type_t>
void Container_DestroyRange(
    type_t *pData,
    usize nCount ) noexcept
{
    static_assert(
        std::is_nothrow_destructible_v<type_t>,
        "Container element destruction must not throw." );

    // Trivial elements require no lifetime work. Non-trivial elements are
    // destroyed in reverse construction order, matching normal array teardown.
    if constexpr ( !std::is_trivially_destructible_v<type_t> ) {
        while ( nCount > 0u ) {
            --nCount;
            pData[nCount].~type_t();
        }
    }
}

template <typename type_t>
void Container_CopyConstructRange(
    type_t *pDestination,
    const type_t *pSource,
    usize nCount ) noexcept
{
    static_assert(
        std::is_nothrow_copy_constructible_v<type_t>,
        "Container copy construction must not throw." );

    // Byte-copying is legal only for trivially copyable types. Every other type
    // must begin a distinct object lifetime through its copy constructor.
    if constexpr ( std::is_trivially_copyable_v<type_t> ) {
        usize cbSize = 0u;
        const bool_t bValidByteCount =
            Cy_TryArrayByteCount<type_t>( nCount, cbSize );
        CY_ASSERT_MSG( bValidByteCount, "Container copy byte count overflowed." );
        if ( bValidByteCount && cbSize > 0u ) {
            Cy_MemCopy( pDestination, pSource, cbSize );
        }
    } else {
        for ( usize iIndex = 0u; iIndex < nCount; ++iIndex ) {
            ::new ( static_cast<void *>( pDestination + iIndex ) )
                type_t( pSource[iIndex] );
        }
    }
}

template <typename type_t>
void Container_RelocateConstructRange(
    type_t *pDestination,
    type_t *pSource,
    usize nCount ) noexcept
{
    static_assert(
        std::is_nothrow_move_constructible_v<type_t> ||
        std::is_nothrow_copy_constructible_v<type_t>,
        "Container relocation requires nothrow move or copy construction." );

    // Relocation constructs destination objects but deliberately leaves source
    // destruction to the owning container after the full transfer succeeds.
    if constexpr ( std::is_trivially_copyable_v<type_t> ) {
        usize cbSize = 0u;
        const bool_t bValidByteCount =
            Cy_TryArrayByteCount<type_t>( nCount, cbSize );
        CY_ASSERT_MSG( bValidByteCount, "Container relocation byte count overflowed." );
        if ( bValidByteCount && cbSize > 0u ) {
            Cy_MemCopy( pDestination, pSource, cbSize );
        }
    } else if constexpr ( std::is_nothrow_move_constructible_v<type_t> ) {
        // Forward assignment is used when shifting elements toward lower addresses.
        for ( usize iIndex = 0u; iIndex < nCount; ++iIndex ) {
            ::new ( static_cast<void *>( pDestination + iIndex ) )
                type_t( static_cast<type_t &&>( pSource[iIndex] ) );
        }
    } else {
        Container_CopyConstructRange( pDestination, pSource, nCount );
    }
}

template <typename type_t>
void Container_MoveAssignRangeForward(
    type_t *pDestination,
    type_t *pSource,
    usize nCount ) noexcept
{
    if constexpr ( std::is_trivially_copyable_v<type_t> ) {
        usize cbSize = 0u;
        const bool_t bValidByteCount =
            Cy_TryArrayByteCount<type_t>( nCount, cbSize );
        CY_ASSERT_MSG( bValidByteCount, "Container move byte count overflowed." );
        if ( bValidByteCount && cbSize > 0u ) {
            Cy_MemMove( pDestination, pSource, cbSize );
        }
    } else {
        static_assert(
            std::is_nothrow_move_assignable_v<type_t>,
            "Container shifting requires nothrow move assignment." );
        for ( usize iIndex = 0u; iIndex < nCount; ++iIndex ) {
            pDestination[iIndex] = static_cast<type_t &&>( pSource[iIndex] );
        }
    }
}

template <typename type_t>
void Container_MoveAssignRangeBackward(
    type_t *pDestination,
    type_t *pSource,
    usize nCount ) noexcept
{
    if constexpr ( std::is_trivially_copyable_v<type_t> ) {
        usize cbSize = 0u;
        const bool_t bValidByteCount =
            Cy_TryArrayByteCount<type_t>( nCount, cbSize );
        CY_ASSERT_MSG( bValidByteCount, "Container move byte count overflowed." );
        if ( bValidByteCount && cbSize > 0u ) {
            Cy_MemMove( pDestination, pSource, cbSize );
        }
    } else {
        static_assert(
            std::is_nothrow_move_assignable_v<type_t>,
            "Container shifting requires nothrow move assignment." );
        // Walk backward when shifting toward higher addresses so overlapping
        // source elements are consumed before their slots are overwritten.
        while ( nCount > 0u ) {
            --nCount;
            pDestination[nCount] =
                static_cast<type_t &&>( pSource[nCount] );
        }
    }
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CONTAINEROPS_INL
