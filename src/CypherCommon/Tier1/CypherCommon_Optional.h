//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Optional.h
//  Purpose: Declares inline optional-value storage.
//  Details: Optional owns at most one in-place value and performs no allocation.
//           Value construction and destruction are explicit through its free-function API.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_OPTIONAL_H
#define CYPHER_COMMON_TIER1_OPTIONAL_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename type_t>
struct optional_t {
    static_assert( is_object_v<type_t>, "optional_t requires an object type." );
    static_assert( !is_array_v<type_t>, "optional_t does not store array types." );

    optional_t() noexcept = default;
    ~optional_t() noexcept;
    optional_t( const optional_t & ) = delete;
    optional_t &operator=( const optional_t & ) = delete;
    optional_t( optional_t &&other ) noexcept;
    optional_t &operator=( optional_t &&other ) noexcept;

    alignas( type_t ) byte storage[sizeof( type_t )]{};
    bool_t bHasValue{ CY_FALSE };
};

template <typename type_t>
CYPHER_NODISCARD bool_t Optional_HasValue(
    const optional_t<type_t> &optional ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Optional_Get(
    optional_t<type_t> *pOptional ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const type_t *Optional_Get(
    const optional_t<type_t> *pOptional ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Optional_Emplace(
    optional_t<type_t> *pOptional,
    const type_t &value ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Optional_EmplaceMove(
    optional_t<type_t> *pOptional,
    type_t &&value ) noexcept;

// Replaces the current value by constructing type_t from forwarded arguments.
template <typename type_t, typename... args_t>
CYPHER_NODISCARD type_t *Optional_EmplaceArgs(
    optional_t<type_t> *pOptional,
    args_t &&...args ) noexcept;

template <typename type_t>
void Optional_Reset( optional_t<type_t> *pOptional ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Optional_Take(
    optional_t<type_t> *pOptional,
    type_t *pValueOut ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_OPTIONAL_INL
    #include "CypherCommon_Optional.inl"
#endif

#endif // CYPHER_COMMON_TIER1_OPTIONAL_H
