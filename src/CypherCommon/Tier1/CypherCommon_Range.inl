//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Range.inl
//  Purpose: Implements inclusive generic value ranges.
//  Details: Value helpers validate endpoint ordering and provide predictable
//           containment and clamping without allocation or hidden state.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_RANGE_INL
#define CYPHER_COMMON_TIER1_RANGE_INL
#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <typename type_t>
constexpr bool_t ValueRange_IsOrdered(
    const value_range_t<type_t> &range ) noexcept
{
    return range.minValue <= range.maxValue;
}

template <typename type_t>
constexpr bool_t ValueRange_Contains(
    const value_range_t<type_t> &range,
    const type_t &value ) noexcept
{
    const bool_t bOrdered = ValueRange_IsOrdered( range );
    CY_ASSERT_MSG( bOrdered, "ValueRange_Contains requires ordered endpoints." );
    if ( !bOrdered ) {
        return CY_FALSE;
    }

    return value >= range.minValue &&
           value <= range.maxValue;
}

template <typename type_t>
constexpr type_t ValueRange_Clamp(
    const value_range_t<type_t> &range,
    const type_t &value ) noexcept
{
    const bool_t bOrdered = ValueRange_IsOrdered( range );
    CY_ASSERT_MSG( bOrdered, "ValueRange_Clamp requires ordered endpoints." );
    if ( !bOrdered ) {
        return value;
    }

    if ( value < range.minValue ) {
        return range.minValue;
    }
    if ( value > range.maxValue ) {
        return range.maxValue;
    }
    return value;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RANGE_INL
