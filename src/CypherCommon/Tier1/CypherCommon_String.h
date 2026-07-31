//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_String.h
//  Purpose: Declares CypherCommon Tier1 String support.
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

#ifndef CYPHER_COMMON_TIER1_STRING_H
#define CYPHER_COMMON_TIER1_STRING_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon String

Source-style C string utilities owned by Cypher.

Rules:
- No heap allocation.
- No locale-dependent behavior.
- Null input strings are treated as empty strings.
- Copy/append helpers always null-terminate when cchDest > 0.
- Return counts describe the required full output length unless stated otherwise.
- No unsafe write APIs without destination capacity.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

/*
================
Length / State
================
*/
// Returns the number of characters before the null terminator.
CYPHER_NODISCARD CYPHER_COMMON_API
usize Cy_strlen( const char *pString ) noexcept;

// Returns the number of characters before the null terminator, capped at cchMax.
CYPHER_NODISCARD CYPHER_COMMON_API
usize Cy_strnlen( const char *pString, usize cchMax ) noexcept;

// Returns true when pString is null or points to an empty string.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Cy_strisempty( const char *pString ) noexcept;

// Returns true when pString is null, empty, or ASCII whitespace only.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Cy_strisblank( const char *pString ) noexcept;

/*
================
Compare
================
*/
// Compares two strings using byte-wise ASCII ordering.
CYPHER_NODISCARD CYPHER_COMMON_API
i32 Cy_strcmp( const char *pStringA, const char *pStringB ) noexcept;

// Compares up to cchMax characters using byte-wise ASCII ordering.
CYPHER_NODISCARD CYPHER_COMMON_API
i32 Cy_strncmp( const char *pStringA, const char *pStringB, usize cchMax ) noexcept;

// Compares two strings using ASCII case-insensitive ordering.
CYPHER_NODISCARD CYPHER_COMMON_API
i32 Cy_stricmp( const char *pStringA, const char *pStringB ) noexcept;

// Compares up to cchMax characters using ASCII case-insensitive ordering.
CYPHER_NODISCARD CYPHER_COMMON_API
i32 Cy_strnicmp( const char *pStringA, const char *pStringB, usize cchMax ) noexcept;

// Returns true when both strings are byte-wise equal.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Cy_strequal( const char *pStringA, const char *pStringB ) noexcept;

// Returns true when both strings are equal ignoring ASCII case.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Cy_striequal( const char *pStringA, const char *pStringB ) noexcept;

// Returns true when both strings are equal for at most cchMax characters.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Cy_strnequal( const char *pStringA, const char *pStringB, usize cchMax ) noexcept;

// Returns true when both strings are equal for at most cchMax characters ignoring ASCII case.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Cy_strniequal( const char *pStringA, const char *pStringB, usize cchMax ) noexcept;

/*
================
Copy / Append
================
*/

// Copies pSrc into pDest and always terminates when cchDest > 0.
// Returns the full source length required, excluding the null terminator.
CYPHER_NODISCARD_MSG( "Check the required length to detect truncation." )
CYPHER_COMMON_API usize Cy_strncpy(
    char *pDest,
    const char *pSrc,
    usize cchDest ) noexcept;

// Copies at most cchMax source characters and always terminates when cchDest > 0.
CYPHER_NODISCARD_MSG( "Check the required length to detect truncation." )
CYPHER_COMMON_API usize Cy_strncpy_max(
    char *pDest,
    const char *pSrc,
    usize cchDest,
    usize cchMax ) noexcept;

// Appends pSrc to pDest and always terminates when cchDest > 0.
// Returns the full final length required, excluding the null terminator.
CYPHER_NODISCARD_MSG( "Check the required length to detect truncation." )
CYPHER_COMMON_API usize Cy_strncat(
    char *pDest,
    const char *pSrc,
    usize cchDest ) noexcept;

// Appends at most cchMax source characters and always terminates when cchDest > 0.
CYPHER_NODISCARD_MSG( "Check the required length to detect truncation." )
CYPHER_COMMON_API usize Cy_strncat_max(
    char *pDest,
    const char *pSrc,
    usize cchDest,
    usize cchMax ) noexcept;

/*
================
Search
================
*/
// Finds the first occurrence of chFind.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *Cy_strchr( const char *pString, char chFind ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API
char *Cy_strchr( char *pString, char chFind ) noexcept;

// Finds the last occurrence of chFind.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *Cy_strrchr( const char *pString, char chFind ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API
char *Cy_strrchr( char *pString, char chFind ) noexcept;

// Finds the first occurrence of chFind within cchMax characters.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *Cy_strnchr( const char *pString, char chFind, usize cchMax ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API
char *Cy_strnchr( char *pString, char chFind, usize cchMax ) noexcept;

// Finds the first occurrence of pSearch.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *Cy_strstr( const char *pString, const char *pSearch ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API
char *Cy_strstr( char *pString, const char *pSearch ) noexcept;

// Finds the first occurrence of pSearch ignoring ASCII case.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *Cy_stristr( const char *pString, const char *pSearch ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API
char *Cy_stristr( char *pString, const char *pSearch ) noexcept;

// Finds the first occurrence of pSearch within cchMax characters.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *Cy_strnstr(
    const char *pString,
    const char *pSearch,
    usize cchMax ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API
char *Cy_strnstr( char *pString, const char *pSearch, usize cchMax ) noexcept;

// Finds the first occurrence of pSearch within cchMax characters ignoring ASCII case.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *Cy_strnistr(
    const char *pString,
    const char *pSearch,
    usize cchMax ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API
char *Cy_strnistr( char *pString, const char *pSearch, usize cchMax ) noexcept;

/*
================
Prefix / Suffix
================
*/
// Returns true when pString begins with pPrefix.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Cy_strstarts( const char *pString, const char *pPrefix ) noexcept;

// Returns true when pString begins with pPrefix ignoring ASCII case.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Cy_stristarts( const char *pString, const char *pPrefix ) noexcept;

// Returns true when pString ends with pSuffix.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Cy_strends( const char *pString, const char *pSuffix ) noexcept;

// Returns true when pString ends with pSuffix ignoring ASCII case.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Cy_striends( const char *pString, const char *pSuffix ) noexcept;

/*
================
Case
================
*/
// Converts pString to lowercase ASCII in place.
CYPHER_COMMON_API char *Cy_strlower( char *pString ) noexcept;

// Converts pString to uppercase ASCII in place.
CYPHER_COMMON_API char *Cy_strupper( char *pString ) noexcept;

// Converts at most cchMax characters to lowercase ASCII in place.
CYPHER_COMMON_API char *Cy_strnlower( char *pString, usize cchMax ) noexcept;

// Converts at most cchMax characters to uppercase ASCII in place.
CYPHER_COMMON_API char *Cy_strnupper( char *pString, usize cchMax ) noexcept;

// Returns true when all ASCII alphabetic characters are lowercase.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Cy_strislower( const char *pString ) noexcept;

// Returns true when all ASCII alphabetic characters are uppercase.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Cy_strisupper( const char *pString ) noexcept;

/*
================
Whitespace / Trim
================
*/
// Returns the first non-whitespace character in pString.
CYPHER_NODISCARD CYPHER_COMMON_API
const char *Cy_strskipwhite( const char *pString ) noexcept;
CYPHER_NODISCARD CYPHER_COMMON_API
char *Cy_strskipwhite( char *pString ) noexcept;

// Removes leading ASCII whitespace in place.
CYPHER_COMMON_API void Cy_strtrimleft( char *pString ) noexcept;

// Removes trailing ASCII whitespace in place.
CYPHER_COMMON_API void Cy_strtrimright( char *pString ) noexcept;

// Removes leading and trailing ASCII whitespace in place.
CYPHER_COMMON_API void Cy_strtrim( char *pString ) noexcept;

// Removes one matching pair of surrounding quotes in place.
CYPHER_COMMON_API void Cy_strstripquotes( char *pString ) noexcept;

/*
================
Slice / Replace
================
*/
// Copies the leftmost cchCount characters into pDest.
CYPHER_NODISCARD_MSG( "Check the required length to detect truncation." )
CYPHER_COMMON_API usize Cy_strleft(
    const char *pString,
    char *pDest,
    usize cchDest,
    usize cchCount ) noexcept;

// Copies the rightmost cchCount characters into pDest.
CYPHER_NODISCARD_MSG( "Check the required length to detect truncation." )
CYPHER_COMMON_API usize Cy_strright(
    const char *pString,
    char *pDest,
    usize cchDest,
    usize cchCount ) noexcept;

// Copies cchCount characters starting at iStart into pDest.
CYPHER_NODISCARD_MSG( "Check the required length to detect truncation." )
CYPHER_COMMON_API usize Cy_strslice(
    const char *pString,
    char *pDest,
    usize cchDest,
    usize iStart,
    usize cchCount ) noexcept;

// Replaces occurrences of pSearch with pReplace into pDest.
CYPHER_NODISCARD_MSG( "Check the required length to detect truncation." )
CYPHER_COMMON_API usize Cy_strsubst(
    const char *pString,
    const char *pSearch,
    const char *pReplace,
    char *pDest,
    usize cchDest ) noexcept;

// Counts occurrences of chFind.
CYPHER_NODISCARD CYPHER_COMMON_API
usize Cy_strcountchar( const char *pString, char chFind ) noexcept;

// Counts occurrences of pSearch.
CYPHER_NODISCARD CYPHER_COMMON_API
usize Cy_strcountstring( const char *pString, const char *pSearch ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRING_H
