//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringView.h
//  Purpose: Declares CypherCommon Tier1 StringView support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_STRINGVIEW_H
#define CYPHER_COMMON_TIER1_STRINGVIEW_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon String View

Non-owning, length-bounded string slices.

Rules:
- A view never owns, allocates, frees, or modifies its source storage.
- A view is not required to end with a null terminator.
- A null data pointer is valid only when the length is zero.
- Positions use zero-based byte indices; this API does not decode Unicode.
- Search functions return CY_STRING_VIEW_NPOS when no match exists.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct string_view_t {
    const char *pData { nullptr };
    usize cchLength { 0u };
};

constexpr usize CY_STRING_VIEW_NPOS = CY_INVALID_SIZE;

/*
================
Construction / State
================
*/
// Creates a view over a null-terminated string without taking ownership.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringView_FromCString( const char *pString ) noexcept;

// Creates a view over exactly cchLength bytes beginning at pData.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringView_FromRange( const char *pData, usize cchLength ) noexcept;

// Returns true when the view contains no characters.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringView_IsEmpty( string_view_t view ) noexcept;

// Returns true when the view satisfies its pointer-and-length invariant.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringView_IsValid( string_view_t view ) noexcept;

// Returns the number of bytes represented by the view.
CYPHER_NODISCARD CYPHER_COMMON_API
usize StringView_Length( string_view_t view ) noexcept;

/*
================
Access
================
*/
// Returns the first address in the represented range.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *StringView_Begin( string_view_t view ) noexcept;

// Returns the one-past-the-end address of the represented range.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *StringView_End( string_view_t view ) noexcept;

// Returns the character at iIndex; iIndex must be inside the view.
CYPHER_NODISCARD CYPHER_COMMON_API
char StringView_At( string_view_t view, usize iIndex ) noexcept;

// Returns the first character; the view must not be empty.
CYPHER_NODISCARD CYPHER_COMMON_API
char StringView_Front( string_view_t view ) noexcept;

// Returns the last character; the view must not be empty.
CYPHER_NODISCARD CYPHER_COMMON_API
char StringView_Back( string_view_t view ) noexcept;

/*
================
Compare
================
*/
// Compares two views using byte-wise ASCII ordering.
CYPHER_NODISCARD CYPHER_COMMON_API
i32 StringView_Compare( string_view_t viewA, string_view_t viewB ) noexcept;

// Compares two views using ASCII case-insensitive ordering.
CYPHER_NODISCARD CYPHER_COMMON_API
i32 StringView_CompareInsensitiveAscii(
    string_view_t viewA,
    string_view_t viewB ) noexcept;

// Returns true when both views contain the same bytes.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringView_Equals( string_view_t viewA, string_view_t viewB ) noexcept;

// Returns true when both views are equal ignoring ASCII case.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringView_EqualsInsensitiveAscii(
    string_view_t viewA,
    string_view_t viewB ) noexcept;

/*
================
Prefix / Suffix
================
*/
// Returns true when view begins with prefix.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringView_StartsWith( string_view_t view, string_view_t prefix ) noexcept;

// Returns true when view begins with prefix ignoring ASCII case.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringView_StartsWithInsensitiveAscii(
    string_view_t view,
    string_view_t prefix ) noexcept;

// Returns true when view ends with suffix.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringView_EndsWith( string_view_t view, string_view_t suffix ) noexcept;

// Returns true when view ends with suffix ignoring ASCII case.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringView_EndsWithInsensitiveAscii(
    string_view_t view,
    string_view_t suffix ) noexcept;

/*
================
Slice
================
*/
// Returns a subview beginning at iStart and containing at most cchLength bytes.
// iStart must not exceed the source length; CY_STRING_VIEW_NPOS selects the remainder.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringView_Subview(
    string_view_t view,
    usize iStart,
    usize cchLength ) noexcept;

// Returns at most the first cchLength bytes of view.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringView_Prefix( string_view_t view, usize cchLength ) noexcept;

// Returns at most the last cchLength bytes of view.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringView_Suffix( string_view_t view, usize cchLength ) noexcept;

// Advances the beginning of view by at most cchLength bytes.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringView_RemovePrefix(
    string_view_t view,
    usize cchLength ) noexcept;

// Retreats the end of view by at most cchLength bytes.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringView_RemoveSuffix(
    string_view_t view,
    usize cchLength ) noexcept;

/*
================
Search
================
*/
// Finds chFind at or after iStart and returns its byte index.
CYPHER_NODISCARD CYPHER_COMMON_API
usize StringView_FindChar(
    string_view_t view,
    char chFind,
    usize iStart = 0u ) noexcept;

// Finds the final occurrence of chFind and returns its byte index.
CYPHER_NODISCARD CYPHER_COMMON_API
usize StringView_FindLastChar( string_view_t view, char chFind ) noexcept;

// Finds search at or after iStart and returns its starting byte index.
CYPHER_NODISCARD CYPHER_COMMON_API
usize StringView_Find(
    string_view_t view,
    string_view_t search,
    usize iStart = 0u ) noexcept;

// Finds search at or after iStart while ignoring ASCII case.
CYPHER_NODISCARD CYPHER_COMMON_API
usize StringView_FindInsensitiveAscii(
    string_view_t view,
    string_view_t search,
    usize iStart = 0u ) noexcept;

// Returns true when view contains search.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringView_Contains( string_view_t view, string_view_t search ) noexcept;

// Returns true when view contains search ignoring ASCII case.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t StringView_ContainsInsensitiveAscii(
    string_view_t view,
    string_view_t search ) noexcept;

/*
================
Trim
================
*/
// Returns view without leading ASCII whitespace.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringView_TrimLeft( string_view_t view ) noexcept;

// Returns view without trailing ASCII whitespace.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringView_TrimRight( string_view_t view ) noexcept;

// Returns view without leading or trailing ASCII whitespace.
CYPHER_NODISCARD CYPHER_COMMON_API
string_view_t StringView_Trim( string_view_t view ) noexcept;

/*
================
Copy
================
*/
// Copies view into pDest and always terminates when pDest is valid and cchDest > 0.
// A null destination with zero capacity performs a size query without writing.
// Returns the full view length required, excluding the null terminator.
CYPHER_NODISCARD_MSG( "Check the required length to detect truncation." )
CYPHER_COMMON_API usize StringView_CopyToCString(
    string_view_t view,
    char *pDest,
    usize cchDest ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGVIEW_H
