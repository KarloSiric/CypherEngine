//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_FixedString.h
//  Purpose: Declares inline fixed-capacity null-terminated strings.
//  Details: Fixed strings never allocate. Their capacity excludes the terminator,
//           and mutating operations report the full required character count.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_FIXEDSTRING_H
#define CYPHER_COMMON_TIER1_FIXEDSTRING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_StringView.h"

namespace cypher::common
{

template <usize cchCapacity>
struct fixed_string_t {
    char data[cchCapacity + 1u]{};
    usize cchLength{ 0u };
};

template <usize cchCapacity>
CYPHER_NODISCARD bool_t FixedString_IsValid(
    const fixed_string_t<cchCapacity> &string ) noexcept;

template <usize cchCapacity>
CYPHER_NODISCARD constexpr usize FixedString_Capacity(
    const fixed_string_t<cchCapacity> & ) noexcept
{
    return cchCapacity;
}

template <usize cchCapacity>
CYPHER_NODISCARD usize FixedString_Length(
    const fixed_string_t<cchCapacity> &string ) noexcept;

template <usize cchCapacity>
CYPHER_NODISCARD bool_t FixedString_IsEmpty(
    const fixed_string_t<cchCapacity> &string ) noexcept;

template <usize cchCapacity>
CYPHER_NODISCARD const char *FixedString_CStr(
    const fixed_string_t<cchCapacity> &string ) noexcept;

template <usize cchCapacity>
CYPHER_NODISCARD char *FixedString_Data(
    fixed_string_t<cchCapacity> *pString ) noexcept;

template <usize cchCapacity>
CYPHER_NODISCARD string_view_t FixedString_View(
    const fixed_string_t<cchCapacity> &string ) noexcept;

template <usize cchCapacity>
void FixedString_Clear( fixed_string_t<cchCapacity> *pString ) noexcept;

template <usize cchCapacity>
CYPHER_NODISCARD_MSG( "Compare the required length with FixedString_Capacity." )
usize FixedString_Assign(
    fixed_string_t<cchCapacity> *pString,
    string_view_t source ) noexcept;

template <usize cchCapacity>
CYPHER_NODISCARD_MSG( "Compare the required length with FixedString_Capacity." )
usize FixedString_Append(
    fixed_string_t<cchCapacity> *pString,
    string_view_t source ) noexcept;

template <usize cchCapacity>
CYPHER_NODISCARD bool_t FixedString_AppendChar(
    fixed_string_t<cchCapacity> *pString,
    char ch ) noexcept;

template <usize cchCapacity>
CYPHER_NODISCARD i32 FixedString_Compare(
    const fixed_string_t<cchCapacity> &string,
    string_view_t other ) noexcept;

template <usize cchCapacity>
CYPHER_NODISCARD bool_t FixedString_Equals(
    const fixed_string_t<cchCapacity> &string,
    string_view_t other ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FIXEDSTRING_H
