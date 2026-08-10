//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_RangeCheckedVar.h
//  Purpose: Declares values constrained to an inclusive range.
//  Details: Assignment policy is explicit: reject preserves the old value, while clamp
//           stores the nearest valid endpoint. No hidden wrapping is performed.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_RANGECHECKEDVAR_H
#define CYPHER_COMMON_TIER1_RANGECHECKEDVAR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Range.h"

namespace cypher::common
{

template <typename type_t>
struct range_checked_var_t {
    type_t value{};
    value_range_t<type_t> range{};
};

template <typename type_t>
CYPHER_NODISCARD bool_t RangeCheckedVar_Init(
    range_checked_var_t<type_t> *pVariable,
    type_t value,
    value_range_t<type_t> range ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t RangeCheckedVar_TrySet(
    range_checked_var_t<type_t> *pVariable,
    type_t value ) noexcept;

template <typename type_t>
void RangeCheckedVar_SetClamped(
    range_checked_var_t<type_t> *pVariable,
    type_t value ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t RangeCheckedVar_Get(
    const range_checked_var_t<type_t> &variable ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_RANGECHECKEDVAR_INL
    #include "CypherCommon_RangeCheckedVar.inl"
#endif

#endif // CYPHER_COMMON_TIER1_RANGECHECKEDVAR_H
