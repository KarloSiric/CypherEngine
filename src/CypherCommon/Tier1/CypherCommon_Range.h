//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Range.h
//  Purpose: Declares index, byte, and inclusive value-range contracts.
//  Details: Range helpers centralize overflow-aware offset/count operations used by
//           containers, streams, files, packets, and serialized formats.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_RANGE_H
#define CYPHER_COMMON_TIER1_RANGE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct index_range_t {
    usize iFirst{ 0u };
    usize nCount{ 0u };
};

struct byte_range_t {
    usize iOffset{ 0u };
    usize cbSize{ 0u };
};

template <typename type_t>
struct value_range_t {
    type_t minValue{};
    type_t maxValue{};
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t IndexRange_IsValid( index_range_t range ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize IndexRange_End( index_range_t range ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t IndexRange_Contains( index_range_t range, usize iIndex ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t IndexRange_ContainsRange(
    index_range_t outer,
    index_range_t inner ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
index_range_t IndexRange_Intersection(
    index_range_t rangeA,
    index_range_t rangeB ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteRange_IsValid( byte_range_t range ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize ByteRange_End( byte_range_t range ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteRange_ContainsOffset(
    byte_range_t range,
    usize iOffset ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t ByteRange_ContainsRange(
    byte_range_t outer,
    byte_range_t inner ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
byte_range_t ByteRange_Intersection(
    byte_range_t rangeA,
    byte_range_t rangeB ) noexcept;

template <typename type_t>
CYPHER_NODISCARD constexpr bool_t ValueRange_IsOrdered(
    const value_range_t<type_t> &range ) noexcept;

template <typename type_t>
CYPHER_NODISCARD constexpr bool_t ValueRange_Contains(
    const value_range_t<type_t> &range,
    const type_t &value ) noexcept;

template <typename type_t>
CYPHER_NODISCARD constexpr type_t ValueRange_Clamp(
    const value_range_t<type_t> &range,
    const type_t &value ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_RANGE_INL
    #include "CypherCommon_Range.inl"
#endif

#endif // CYPHER_COMMON_TIER1_RANGE_H
