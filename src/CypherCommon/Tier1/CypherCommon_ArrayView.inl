//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ArrayView.inl
//  Purpose: Implements read-only contiguous array-view construction.
//  Details: ArrayView forwards directly to Span so both APIs share the same
//           pointer/count invariant, bounds behavior, and zero-cost representation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Array View Template Definitions

This is a non-owning range. The caller keeps the referenced storage alive, and all slicing or
indexing operations validate the reported extent before pointer arithmetic. Template definitions
remain in this file so each concrete instantiation is compiled at its call site.
================
*/

#ifndef CYPHER_COMMON_TIER1_ARRAYVIEW_INL
#define CYPHER_COMMON_TIER1_ARRAYVIEW_INL

#ifndef CYPHER_COMMON_TIER1_ARRAYVIEW_H
    #include "CypherCommon_ArrayView.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

// ArrayView intentionally forwards to Span instead of duplicating range logic.
// The two names differ by use-site vocabulary, not representation or ownership.

template <typename type_t>
array_view_t<type_t> ArrayView_Make(
    const type_t *pData,
    usize nCount ) noexcept
{
    return Span_Make( pData, nCount );
}

template <typename type_t, usize nExtent>
array_view_t<type_t> ArrayView_FromArray(
    const type_t ( &values )[nExtent] ) noexcept
{
    return Span_FromArray( values );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_ARRAYVIEW_INL
