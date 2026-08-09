//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Span.h
//  Purpose: Declares non-owning contiguous typed ranges.
//  Details: Spans carry pointer and element count without allocation or ownership.
//           They are the canonical Tier1 array-view contract.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SPAN_H
#define CYPHER_COMMON_TIER1_SPAN_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename type_t>
struct span_t {
    static_assert( is_object_v<type_t>, "span_t requires an object type." );

    type_t *pData{ nullptr };
    usize nCount{ 0u };
};

using byte_span_t = span_t<byte>;
using const_byte_span_t = span_t<const byte>;

template <typename type_t>
CYPHER_NODISCARD span_t<type_t> Span_Make(
    type_t *pData,
    usize nCount ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD span_t<type_t> Span_FromArray(
    type_t ( &values )[nExtent] ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Span_IsValid( span_t<type_t> span ) noexcept;

template <typename type_t>
CYPHER_NODISCARD bool_t Span_IsEmpty( span_t<type_t> span ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize Span_Count( span_t<type_t> span ) noexcept;

template <typename type_t>
CYPHER_NODISCARD usize Span_SizeBytes( span_t<type_t> span ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Span_Data( span_t<type_t> span ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Span_Begin( span_t<type_t> span ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Span_End( span_t<type_t> span ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Span_At(
    span_t<type_t> span,
    usize iIndex ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Span_Front( span_t<type_t> span ) noexcept;

template <typename type_t>
CYPHER_NODISCARD type_t *Span_Back( span_t<type_t> span ) noexcept;

template <typename type_t>
CYPHER_NODISCARD span_t<type_t> Span_Subspan(
    span_t<type_t> span,
    usize iFirst,
    usize nCount ) noexcept;

template <typename type_t>
CYPHER_NODISCARD span_t<type_t> Span_Prefix(
    span_t<type_t> span,
    usize nCount ) noexcept;

template <typename type_t>
CYPHER_NODISCARD span_t<type_t> Span_Suffix(
    span_t<type_t> span,
    usize nCount ) noexcept;

template <typename type_t>
CYPHER_NODISCARD const_byte_span_t Span_AsBytes(
    span_t<type_t> span ) noexcept;

template <typename type_t, enable_if_t<!std::is_const_v<type_t>, i32> = 0>
CYPHER_NODISCARD byte_span_t Span_AsWritableBytes(
    span_t<type_t> span ) noexcept;

} // namespace cypher::common

#ifndef CYPHER_COMMON_TIER1_SPAN_INL
    #include "CypherCommon_Span.inl"
#endif

#endif // CYPHER_COMMON_TIER1_SPAN_H
