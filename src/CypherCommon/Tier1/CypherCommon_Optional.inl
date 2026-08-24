//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Optional.inl
//  Purpose: Implements inline optional-value storage.
//  Details: Object lifetime is managed explicitly with placement construction,
//           destruction, move transfer, and allocation-free value access.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_OPTIONAL_INL
#define CYPHER_COMMON_TIER1_OPTIONAL_INL

#ifndef CYPHER_COMMON_TIER1_OPTIONAL_H
    #include "CypherCommon_Optional.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include <new>

namespace cypher::common
{

namespace detail
{

template <typename type_t>
CYPHER_NODISCARD type_t *Optional_Storage(
    optional_t<type_t> *pOptional ) noexcept
{
    // The byte buffer contains a type_t only while bHasValue is true; launder
    // obtains the pointer for the current placement-constructed lifetime.
    return std::launder(
        reinterpret_cast<type_t *>( pOptional->storage ) );
}

template <typename type_t>
CYPHER_NODISCARD const type_t *Optional_Storage(
    const optional_t<type_t> *pOptional ) noexcept
{
    return std::launder(
        reinterpret_cast<const type_t *>( pOptional->storage ) );
}

} // namespace detail

template <typename type_t>
bool_t Optional_HasValue(
    const optional_t<type_t> &optional ) noexcept
{
    return optional.bHasValue;
}

template <typename type_t>
type_t *Optional_Get(
    optional_t<type_t> *pOptional ) noexcept
{
    const bool_t bValidOptional = pOptional != nullptr;
    CY_ASSERT_MSG( bValidOptional, "Optional_Get requires an optional object." );
    if ( !bValidOptional || !pOptional->bHasValue ) {
        return nullptr;
    }

    return detail::Optional_Storage( pOptional );
}

template <typename type_t>
const type_t *Optional_Get(
    const optional_t<type_t> *pOptional ) noexcept
{
    const bool_t bValidOptional = pOptional != nullptr;
    CY_ASSERT_MSG( bValidOptional, "Optional_Get requires an optional object." );
    if ( !bValidOptional || !pOptional->bHasValue ) {
        return nullptr;
    }

    return detail::Optional_Storage( pOptional );
}

template <typename type_t, typename... args_t>
type_t *Optional_EmplaceArgs(
    optional_t<type_t> *pOptional,
    args_t &&...args ) noexcept
{
    static_assert(
        std::is_nothrow_constructible_v<type_t, args_t &&...>,
        "Optional_EmplaceArgs requires nothrow value construction." );

    const bool_t bValidOptional = pOptional != nullptr;
    CY_ASSERT_MSG(
        bValidOptional,
        "Optional_EmplaceArgs requires an optional object." );
    if ( !bValidOptional ) {
        return nullptr;
    }

    // End any previous value lifetime before beginning the replacement lifetime.
    Optional_Reset( pOptional );
    type_t *pValue = ::new ( static_cast<void *>( pOptional->storage ) )
        type_t( static_cast<args_t &&>( args )... );
    pOptional->bHasValue = CY_TRUE;
    return pValue;
}

template <typename type_t>
type_t *Optional_Emplace(
    optional_t<type_t> *pOptional,
    const type_t &value ) noexcept
{
    return Optional_EmplaceArgs( pOptional, value );
}

template <typename type_t>
type_t *Optional_EmplaceMove(
    optional_t<type_t> *pOptional,
    type_t &&value ) noexcept
{
    return Optional_EmplaceArgs(
        pOptional,
        static_cast<type_t &&>( value ) );
}

template <typename type_t>
void Optional_Reset( optional_t<type_t> *pOptional ) noexcept
{
    static_assert(
        std::is_nothrow_destructible_v<type_t>,
        "Optional_Reset requires nothrow value destruction." );

    const bool_t bValidOptional = pOptional != nullptr;
    CY_ASSERT_MSG( bValidOptional, "Optional_Reset requires an optional object." );
    if ( !bValidOptional || !pOptional->bHasValue ) {
        return;
    }

    detail::Optional_Storage( pOptional )->~type_t();
    pOptional->bHasValue = CY_FALSE;
}

template <typename type_t>
bool_t Optional_Take(
    optional_t<type_t> *pOptional,
    type_t *pValueOut ) noexcept
{
    static_assert(
        std::is_nothrow_move_assignable_v<type_t>,
        "Optional_Take requires nothrow move assignment." );

    const bool_t bValidArguments =
        pOptional != nullptr &&
        pValueOut != nullptr;
    CY_ASSERT_MSG(
        bValidArguments,
        "Optional_Take requires optional and output objects." );
    if ( !bValidArguments || !pOptional->bHasValue ) {
        return CY_FALSE;
    }

    // Transfer the value, then make the optional empty by destroying the moved-from object.
    *pValueOut = static_cast<type_t &&>( *detail::Optional_Storage( pOptional ) );
    Optional_Reset( pOptional );
    return CY_TRUE;
}

template <typename type_t>
optional_t<type_t>::~optional_t() noexcept
{
    Optional_Reset( this );
}

template <typename type_t>
optional_t<type_t>::optional_t( optional_t &&other ) noexcept
{
    static_assert(
        std::is_nothrow_move_constructible_v<type_t>,
        "optional_t move construction requires a nothrow movable value." );

    if ( other.bHasValue ) {
        ::new ( static_cast<void *>( storage ) )
            type_t( static_cast<type_t &&>( *detail::Optional_Storage( &other ) ) );
        bHasValue = CY_TRUE;
        Optional_Reset( &other );
    }
}

template <typename type_t>
optional_t<type_t> &optional_t<type_t>::operator=(
    optional_t &&other ) noexcept
{
    static_assert(
        std::is_nothrow_move_constructible_v<type_t>,
        "optional_t move assignment requires nothrow move construction." );
    static_assert(
        std::is_nothrow_move_assignable_v<type_t>,
        "optional_t move assignment requires nothrow move assignment." );

    if ( this == &other ) {
        return *this;
    }

    // Two engaged optionals can use move assignment. Every other state change
    // requires ending or beginning an object lifetime in raw storage.
    if ( bHasValue && other.bHasValue ) {
        *detail::Optional_Storage( this ) =
            static_cast<type_t &&>( *detail::Optional_Storage( &other ) );
        Optional_Reset( &other );
        return *this;
    }

    Optional_Reset( this );
    if ( other.bHasValue ) {
        ::new ( static_cast<void *>( storage ) )
            type_t( static_cast<type_t &&>( *detail::Optional_Storage( &other ) ) );
        bHasValue = CY_TRUE;
        Optional_Reset( &other );
    }
    return *this;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_OPTIONAL_INL
