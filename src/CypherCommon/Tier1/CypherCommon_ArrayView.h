//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ArrayView.h
//  Purpose: Declares the read-only contiguous array-view compatibility API.
//  Details: Array views alias const spans so Tier1 has one pointer/count invariant
//           rather than two independent non-owning range implementations.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_ARRAYVIEW_H
#define CYPHER_COMMON_TIER1_ARRAYVIEW_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Span.h"

namespace cypher::common
{

template <typename type_t>
using array_view_t = span_t<const type_t>;

template <typename type_t>
CYPHER_NODISCARD array_view_t<type_t> ArrayView_Make(
    const type_t *pData,
    usize nCount ) noexcept;

template <typename type_t, usize nExtent>
CYPHER_NODISCARD array_view_t<type_t> ArrayView_FromArray(
    const type_t ( &values )[nExtent] ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_ARRAYVIEW_H
