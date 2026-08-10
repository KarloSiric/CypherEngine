//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HeapSort.inl
//  Purpose: Implements typed heap primitives.
//  Details: less_t produces a max heap. Heap_Push assumes only the final element is
//           new; Heap_Pop moves the maximum element to the final slot.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_HEAPSORT_INL
#define CYPHER_COMMON_TIER1_HEAPSORT_INL

#ifndef CYPHER_COMMON_TIER1_HEAPSORT_H
    #include "CypherCommon_HeapSort.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <typename type_t, typename compare_t>
void Heap_Make( span_t<type_t> values, compare_t compare ) noexcept
{
    const bool_t bValid = Span_IsValid( values );
    CY_ASSERT_MSG( bValid, "Heap_Make requires a valid span." );
    if ( !bValid || values.nCount < 2u ) {
        return;
    }
    for ( usize iRoot = values.nCount / 2u; iRoot > 0u; --iRoot ) {
        detail::Sort_SiftDown(
            values.pData,
            iRoot - 1u,
            values.nCount,
            compare );
    }
}

template <typename type_t, typename compare_t>
void Heap_Push( span_t<type_t> values, compare_t compare ) noexcept
{
    const bool_t bValid = Span_IsValid( values );
    CY_ASSERT_MSG( bValid, "Heap_Push requires a valid span." );
    if ( !bValid || values.nCount < 2u ) {
        return;
    }

    usize iChild = values.nCount - 1u;
    while ( iChild > 0u ) {
        const usize iParent = ( iChild - 1u ) / 2u;
        if ( !compare( values.pData[iParent], values.pData[iChild] ) ) {
            break;
        }
        detail::Sort_Swap( values.pData[iParent], values.pData[iChild] );
        iChild = iParent;
    }
}

template <typename type_t, typename compare_t>
void Heap_Pop( span_t<type_t> values, compare_t compare ) noexcept
{
    const bool_t bValid = Span_IsValid( values );
    CY_ASSERT_MSG( bValid, "Heap_Pop requires a valid span." );
    if ( !bValid || values.nCount < 2u ) {
        return;
    }
    detail::Sort_Swap( values.pData[0], values.pData[values.nCount - 1u] );
    detail::Sort_SiftDown( values.pData, 0u, values.nCount - 1u, compare );
}

template <typename type_t, typename compare_t>
void HeapSort_Sort( span_t<type_t> values, compare_t compare ) noexcept
{
    const bool_t bValid = Span_IsValid( values );
    CY_ASSERT_MSG( bValid, "HeapSort_Sort requires a valid span." );
    if ( !bValid || values.nCount < 2u ) {
        return;
    }
    Heap_Make( values, compare );
    for ( usize nHeap = values.nCount; nHeap > 1u; --nHeap ) {
        Heap_Pop( Span_Make( values.pData, nHeap ), compare );
    }
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HEAPSORT_INL
