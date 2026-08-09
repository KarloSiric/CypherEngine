//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Span.inl
//  Purpose: Implements non-owning contiguous typed ranges.
//  Details: These inline templates enforce one pointer/count invariant and keep
//           bounds checks available to every Tier1 container and binary utility.
//
//  History:
//  - Created by Karlo Siric on 2026-08-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SPAN_INL
#define CYPHER_COMMON_TIER1_SPAN_INL

#ifndef CYPHER_COMMON_TIER1_SPAN_H
    #include "CypherCommon_Span.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <typename type_t>
span_t<type_t> Span_Make(
    type_t *pData,
    usize nCount ) noexcept
{
    const bool_t bValidRange = pData != nullptr || nCount == 0u;
    CY_ASSERT_MSG(
        bValidRange,
        "Span_Make requires non-null data for a non-empty range." );
    if ( !bValidRange ) {
        return {};
    }

    return { pData, nCount };
}

template <typename type_t, usize nExtent>
span_t<type_t> Span_FromArray(
    type_t ( &values )[nExtent] ) noexcept
{
    return { values, nExtent };
}

template <typename type_t>
bool_t Span_IsValid( span_t<type_t> span ) noexcept
{
    return span.pData != nullptr || span.nCount == 0u;
}

template <typename type_t>
bool_t Span_IsEmpty( span_t<type_t> span ) noexcept
{
    return span.nCount == 0u;
}

template <typename type_t>
usize Span_Count( span_t<type_t> span ) noexcept
{
    return span.nCount;
}

template <typename type_t>
usize Span_SizeBytes( span_t<type_t> span ) noexcept
{
    const bool_t bValidSpan = Span_IsValid( span );
    CY_ASSERT_MSG( bValidSpan, "Span_SizeBytes requires a valid span." );
    if ( !bValidSpan ) {
        return 0u;
    }

    usize cbSize = 0u;
    const bool_t bValidByteCount =
        Cy_TryArrayByteCount<type_t>( span.nCount, cbSize );
    CY_ASSERT_MSG( bValidByteCount, "Span byte count overflowed." );
    return bValidByteCount ? cbSize : 0u;
}

template <typename type_t>
type_t *Span_Data( span_t<type_t> span ) noexcept
{
    const bool_t bValidSpan = Span_IsValid( span );
    CY_ASSERT_MSG( bValidSpan, "Span_Data requires a valid span." );
    return bValidSpan ? span.pData : nullptr;
}

template <typename type_t>
type_t *Span_Begin( span_t<type_t> span ) noexcept
{
    const bool_t bValidSpan = Span_IsValid( span );
    CY_ASSERT_MSG( bValidSpan, "Span_Begin requires a valid span." );
    return bValidSpan ? span.pData : nullptr;
}

template <typename type_t>
type_t *Span_End( span_t<type_t> span ) noexcept
{
    const bool_t bValidSpan = Span_IsValid( span );
    CY_ASSERT_MSG( bValidSpan, "Span_End requires a valid span." );
    if ( !bValidSpan || span.pData == nullptr ) {
        return nullptr;
    }

    return span.pData + span.nCount;
}

template <typename type_t>
type_t *Span_At(
    span_t<type_t> span,
    usize iIndex ) noexcept
{
    const bool_t bValidSpan = Span_IsValid( span );
    CY_ASSERT_MSG( bValidSpan, "Span_At requires a valid span." );
    if ( !bValidSpan ) {
        return nullptr;
    }

    const bool_t bIndexInRange = iIndex < span.nCount;
    CY_ASSERT_MSG( bIndexInRange, "Span_At index is outside the represented range." );
    if ( !bIndexInRange ) {
        return nullptr;
    }

    return span.pData + iIndex;
}

template <typename type_t>
type_t *Span_Front( span_t<type_t> span ) noexcept
{
    return Span_At( span, 0u );
}

template <typename type_t>
type_t *Span_Back( span_t<type_t> span ) noexcept
{
    const bool_t bValidSpan = Span_IsValid( span );
    CY_ASSERT_MSG( bValidSpan, "Span_Back requires a valid span." );
    if ( !bValidSpan ) {
        return nullptr;
    }

    const bool_t bNotEmpty = span.nCount > 0u;
    CY_ASSERT_MSG( bNotEmpty, "Span_Back requires a non-empty span." );
    if ( !bNotEmpty ) {
        return nullptr;
    }

    return span.pData + span.nCount - 1u;
}

template <typename type_t>
span_t<type_t> Span_Subspan(
    span_t<type_t> span,
    usize iFirst,
    usize nCount ) noexcept
{
    const bool_t bValidSpan = Span_IsValid( span );
    CY_ASSERT_MSG( bValidSpan, "Span_Subspan requires a valid source span." );
    if ( !bValidSpan ) {
        return {};
    }

    const bool_t bStartInRange = iFirst <= span.nCount;
    CY_ASSERT_MSG(
        bStartInRange,
        "Span_Subspan start index is outside the source range." );
    if ( !bStartInRange ) {
        iFirst = span.nCount;
    }

    if ( span.pData == nullptr ) {
        return {};
    }

    const usize nAvailable = span.nCount - iFirst;
    const usize nSubspan = nCount < nAvailable ? nCount : nAvailable;
    return { span.pData + iFirst, nSubspan };
}

template <typename type_t>
span_t<type_t> Span_Prefix(
    span_t<type_t> span,
    usize nCount ) noexcept
{
    const bool_t bValidSpan = Span_IsValid( span );
    CY_ASSERT_MSG( bValidSpan, "Span_Prefix requires a valid source span." );
    if ( !bValidSpan ) {
        return {};
    }

    const usize nPrefix = nCount < span.nCount ? nCount : span.nCount;
    return { span.pData, nPrefix };
}

template <typename type_t>
span_t<type_t> Span_Suffix(
    span_t<type_t> span,
    usize nCount ) noexcept
{
    const bool_t bValidSpan = Span_IsValid( span );
    CY_ASSERT_MSG( bValidSpan, "Span_Suffix requires a valid source span." );
    if ( !bValidSpan || span.pData == nullptr ) {
        return {};
    }

    const usize nSuffix = nCount < span.nCount ? nCount : span.nCount;
    return { span.pData + ( span.nCount - nSuffix ), nSuffix };
}

template <typename type_t>
const_byte_span_t Span_AsBytes( span_t<type_t> span ) noexcept
{
    const usize cbSize = Span_SizeBytes( span );
    if ( !Span_IsValid( span ) || ( span.nCount > 0u && cbSize == 0u ) ) {
        return {};
    }

    return {
        reinterpret_cast<const byte *>( span.pData ),
        cbSize
    };
}

template <typename type_t, enable_if_t<!std::is_const_v<type_t>, i32>>
byte_span_t Span_AsWritableBytes( span_t<type_t> span ) noexcept
{
    const usize cbSize = Span_SizeBytes( span );
    if ( !Span_IsValid( span ) || ( span.nCount > 0u && cbSize == 0u ) ) {
        return {};
    }

    return {
        reinterpret_cast<byte *>( span.pData ),
        cbSize
    };
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SPAN_INL
