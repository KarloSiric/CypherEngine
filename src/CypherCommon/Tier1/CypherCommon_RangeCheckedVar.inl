//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_RangeCheckedVar.inl
//  Purpose: Implements values constrained to caller-defined inclusive ranges.
//  Details: Rejecting assignment is transactional, while clamped assignment stores
//           the nearest endpoint. Invalid range contracts never alter the value.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_RANGECHECKEDVAR_INL
#define CYPHER_COMMON_TIER1_RANGECHECKEDVAR_INL

#ifndef CYPHER_COMMON_TIER1_RANGECHECKEDVAR_H
    #include "CypherCommon_RangeCheckedVar.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <typename type_t>
bool_t RangeCheckedVar_Init(
    range_checked_var_t<type_t> *pVariable,
    type_t value,
    value_range_t<type_t> range ) noexcept
{
    const bool_t bValidDestination = pVariable != nullptr;
    const bool_t bOrderedRange = ValueRange_IsOrdered( range );
    CY_ASSERT_MSG(
        bValidDestination,
        "RangeCheckedVar_Init requires destination storage." );
    CY_ASSERT_MSG(
        bOrderedRange,
        "RangeCheckedVar_Init requires ordered range endpoints." );
    if ( !bValidDestination || !bOrderedRange ||
         !ValueRange_Contains( range, value ) ) {
        return CY_FALSE;
    }

    pVariable->value = value;
    pVariable->range = range;
    return CY_TRUE;
}

template <typename type_t>
bool_t RangeCheckedVar_TrySet(
    range_checked_var_t<type_t> *pVariable,
    type_t value ) noexcept
{
    const bool_t bValidVariable =
        pVariable != nullptr && ValueRange_IsOrdered( pVariable->range );
    CY_ASSERT_MSG(
        bValidVariable,
        "RangeCheckedVar_TrySet requires an initialized variable." );
    if ( !bValidVariable || !ValueRange_Contains( pVariable->range, value ) ) {
        return CY_FALSE;
    }

    pVariable->value = value;
    return CY_TRUE;
}

template <typename type_t>
void RangeCheckedVar_SetClamped(
    range_checked_var_t<type_t> *pVariable,
    type_t value ) noexcept
{
    const bool_t bValidVariable =
        pVariable != nullptr && ValueRange_IsOrdered( pVariable->range );
    CY_ASSERT_MSG(
        bValidVariable,
        "RangeCheckedVar_SetClamped requires an initialized variable." );
    if ( !bValidVariable ) {
        return;
    }

    pVariable->value = ValueRange_Clamp( pVariable->range, value );
}

template <typename type_t>
type_t RangeCheckedVar_Get(
    const range_checked_var_t<type_t> &variable ) noexcept
{
    const bool_t bOrderedRange = ValueRange_IsOrdered( variable.range );
    CY_ASSERT_MSG(
        bOrderedRange,
        "RangeCheckedVar_Get requires an initialized variable." );
    return variable.value;
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RANGECHECKEDVAR_INL
