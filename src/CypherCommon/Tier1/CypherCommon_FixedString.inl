//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_FixedString.inl
//  Purpose: Implements inline fixed-capacity null-terminated strings.
//  Details: Operations preserve the length and terminator invariant, tolerate
//           overlapping source views, and report required lengths on truncation.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_FIXEDSTRING_INL
#define CYPHER_COMMON_TIER1_FIXEDSTRING_INL

#ifndef CYPHER_COMMON_TIER1_FIXEDSTRING_H
    #include "CypherCommon_FixedString.h"
#endif

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::common
{

template <usize cchCapacity>
bool_t FixedString_IsValid(
    const fixed_string_t<cchCapacity> &string ) noexcept
{
    return string.cchLength <= cchCapacity &&
           string.data[string.cchLength] == '\0';
}

template <usize cchCapacity>
usize FixedString_Length(
    const fixed_string_t<cchCapacity> &string ) noexcept
{
    const bool_t bValid = FixedString_IsValid( string );
    CY_ASSERT_MSG( bValid, "FixedString_Length requires a valid string." );
    return bValid ? string.cchLength : 0u;
}

template <usize cchCapacity>
bool_t FixedString_IsEmpty(
    const fixed_string_t<cchCapacity> &string ) noexcept
{
    return FixedString_Length( string ) == 0u;
}

template <usize cchCapacity>
const char *FixedString_CStr(
    const fixed_string_t<cchCapacity> &string ) noexcept
{
    const bool_t bValid = FixedString_IsValid( string );
    CY_ASSERT_MSG( bValid, "FixedString_CStr requires a valid string." );
    return bValid ? string.data : "";
}

template <usize cchCapacity>
char *FixedString_Data(
    fixed_string_t<cchCapacity> *pString ) noexcept
{
    const bool_t bValidString = pString != nullptr;
    CY_ASSERT_MSG( bValidString, "FixedString_Data requires a string object." );
    return bValidString ? pString->data : nullptr;
}

template <usize cchCapacity>
string_view_t FixedString_View(
    const fixed_string_t<cchCapacity> &string ) noexcept
{
    const bool_t bValid = FixedString_IsValid( string );
    CY_ASSERT_MSG( bValid, "FixedString_View requires a valid string." );
    return bValid
        ? string_view_t{ string.data, string.cchLength }
        : string_view_t{};
}

template <usize cchCapacity>
void FixedString_Clear( fixed_string_t<cchCapacity> *pString ) noexcept
{
    const bool_t bValidString = pString != nullptr;
    CY_ASSERT_MSG( bValidString, "FixedString_Clear requires a string object." );
    if ( !bValidString ) {
        return;
    }

    pString->data[0] = '\0';
    pString->cchLength = 0u;
}

template <usize cchCapacity>
usize FixedString_Assign(
    fixed_string_t<cchCapacity> *pString,
    string_view_t source ) noexcept
{
    const bool_t bValidString = pString != nullptr;
    const bool_t bValidSource = StringView_IsValid( source );
    CY_ASSERT_MSG( bValidString, "FixedString_Assign requires a string object." );
    CY_ASSERT_MSG( bValidSource, "FixedString_Assign requires a valid source view." );

    if ( !bValidString || !bValidSource ) {
        return CY_USIZE_MAX;
    }

    const usize cchRequired = StringView_CopyToCString(
        source,
        pString->data,
        cchCapacity + 1u );
    pString->cchLength = cchRequired < cchCapacity
        ? cchRequired
        : cchCapacity;
    return cchRequired;
}

template <usize cchCapacity>
usize FixedString_Append(
    fixed_string_t<cchCapacity> *pString,
    string_view_t source ) noexcept
{
    const bool_t bValidString = pString != nullptr;
    const bool_t bValidSource = StringView_IsValid( source );
    CY_ASSERT_MSG( bValidString, "FixedString_Append requires a string object." );
    CY_ASSERT_MSG( bValidSource, "FixedString_Append requires a valid source view." );

    if ( !bValidString || !bValidSource ) {
        return CY_USIZE_MAX;
    }

    const bool_t bValidState = FixedString_IsValid( *pString );
    CY_ASSERT_MSG( bValidState, "FixedString_Append requires a valid destination." );
    if ( !bValidState ) {
        return CY_USIZE_MAX;
    }

    if ( source.cchLength > CY_USIZE_MAX - pString->cchLength ) {
        CY_ASSERT_MSG( false, "FixedString_Append required length overflowed." );
        return CY_USIZE_MAX;
    }

    const usize cchRequired = pString->cchLength + source.cchLength;
    const usize cchAvailable = cchCapacity - pString->cchLength;
    const usize cchCopy = source.cchLength < cchAvailable
        ? source.cchLength
        : cchAvailable;

    if ( cchCopy > 0u ) {
        Cy_MemMove(
            pString->data + pString->cchLength,
            source.pData,
            cchCopy );
        pString->cchLength += cchCopy;
    }
    pString->data[pString->cchLength] = '\0';
    return cchRequired;
}

template <usize cchCapacity>
bool_t FixedString_AppendChar(
    fixed_string_t<cchCapacity> *pString,
    char ch ) noexcept
{
    const bool_t bValidString = pString != nullptr;
    CY_ASSERT_MSG( bValidString, "FixedString_AppendChar requires a string object." );
    if ( !bValidString ) {
        return CY_FALSE;
    }

    const bool_t bValidState = FixedString_IsValid( *pString );
    CY_ASSERT_MSG( bValidState, "FixedString_AppendChar requires a valid destination." );
    if ( !bValidState || pString->cchLength == cchCapacity ) {
        return CY_FALSE;
    }

    pString->data[pString->cchLength] = ch;
    ++pString->cchLength;
    pString->data[pString->cchLength] = '\0';
    return CY_TRUE;
}

template <usize cchCapacity>
i32 FixedString_Compare(
    const fixed_string_t<cchCapacity> &string,
    string_view_t other ) noexcept
{
    return StringView_Compare( FixedString_View( string ), other );
}

template <usize cchCapacity>
bool_t FixedString_Equals(
    const fixed_string_t<cchCapacity> &string,
    string_view_t other ) noexcept
{
    return StringView_Equals( FixedString_View( string ), other );
}

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FIXEDSTRING_INL
