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
usize Cy_strlen( const char *pString );

// Returns the number of characters before the null terminator, capped at cchMax.
usize Cy_strnlen( const char *pString, usize cchMax );

// Returns true when pString is null or points to an empty string.
bool_t Cy_strisempty( const char *pString );

// Returns true when pString is null, empty, or ASCII whitespace only.
bool_t Cy_strisblank( const char *pString );

/*
================
Compare
================
*/
// Compares two strings using byte-wise ASCII ordering.
i32 Cy_strcmp( const char *pStringA, const char *pStringB );

// Compares up to cchMax characters using byte-wise ASCII ordering.
i32 Cy_strncmp( const char *pStringA, const char *pStringB, usize cchMax );

// Compares two strings using ASCII case-insensitive ordering.
i32 Cy_stricmp( const char *pStringA, const char *pStringB );

// Compares up to cchMax characters using ASCII case-insensitive ordering.
i32 Cy_strnicmp( const char *pStringA, const char *pStringB, usize cchMax );

// Returns true when both strings are byte-wise equal.
bool_t Cy_strequal( const char *pStringA, const char *pStringB );

// Returns true when both strings are equal ignoring ASCII case.
bool_t Cy_striequal( const char *pStringA, const char *pStringB );

// Returns true when both strings are equal for at most cchMax characters.
bool_t Cy_strnequal( const char *pStringA, const char *pStringB, usize cchMax );

// Returns true when both strings are equal for at most cchMax characters ignoring ASCII case.
bool_t Cy_strniequal( const char *pStringA, const char *pStringB, usize cchMax );

/*
================
Copy / Append
================
*/

// Copies pSrc into pDest and always terminates when cchDest > 0.
// Returns the full source length required, excluding the null terminator.
usize Cy_strncpy( char *pDest, const char *pSrc, usize cchDest );

// Copies at most cchMax source characters and always terminates when cchDest > 0.
usize Cy_strncpy_max( char *pDest, const char *pSrc, usize cchDest, usize cchMax );

// Appends pSrc to pDest and always terminates when cchDest > 0.
// Returns the full final length required, excluding the null terminator.
usize Cy_strncat( char *pDest, const char *pSrc, usize cchDest );

// Appends at most cchMax source characters and always terminates when cchDest > 0.
usize Cy_strncat_max( char *pDest, const char *pSrc, usize cchDest, usize cchMax );

/*
================
Search
================
*/
// Finds the first occurrence of chFind.
const char *Cy_strchr( const char *pString, char chFind );
char *Cy_strchr( char *pString, char chFind );

// Finds the last occurrence of chFind.
const char *Cy_strrchr( const char *pString, char chFind );
char *Cy_strrchr( char *pString, char chFind );

// Finds the first occurrence of chFind within cchMax characters.
const char *Cy_strnchr( const char *pString, char chFind, usize cchMax );
char *Cy_strnchr( char *pString, char chFind, usize cchMax );

// Finds the first occurrence of pSearch.
const char *Cy_strstr( const char *pString, const char *pSearch );
char *Cy_strstr( char *pString, const char *pSearch );

// Finds the first occurrence of pSearch ignoring ASCII case.
const char *Cy_stristr( const char *pString, const char *pSearch );
char *Cy_stristr( char *pString, const char *pSearch );

// Finds the first occurrence of pSearch within cchMax characters.
const char *Cy_strnstr( const char *pString, const char *pSearch, usize cchMax );
char *Cy_strnstr( char *pString, const char *pSearch, usize cchMax );

// Finds the first occurrence of pSearch within cchMax characters ignoring ASCII case.
const char *Cy_strnistr( const char *pString, const char *pSearch, usize cchMax );
char *Cy_strnistr( char *pString, const char *pSearch, usize cchMax );

/*
================
Prefix / Suffix
================
*/
// Returns true when pString begins with pPrefix.
bool_t Cy_strstarts( const char *pString, const char *pPrefix );

// Returns true when pString begins with pPrefix ignoring ASCII case.
bool_t Cy_stristarts( const char *pString, const char *pPrefix );

// Returns true when pString ends with pSuffix.
bool_t Cy_strends( const char *pString, const char *pSuffix );

// Returns true when pString ends with pSuffix ignoring ASCII case.
bool_t Cy_striends( const char *pString, const char *pSuffix );

/*
================
Case
================
*/
// Converts pString to lowercase ASCII in place.
char *Cy_strlower( char *pString );

// Converts pString to uppercase ASCII in place.
char *Cy_strupper( char *pString );

// Converts at most cchMax characters to lowercase ASCII in place.
char *Cy_strnlower( char *pString, usize cchMax );

// Converts at most cchMax characters to uppercase ASCII in place.
char *Cy_strnupper( char *pString, usize cchMax );

// Returns true when all ASCII alphabetic characters are lowercase.
bool_t Cy_strislower( const char *pString );

// Returns true when all ASCII alphabetic characters are uppercase.
bool_t Cy_strisupper( const char *pString );

/*
================
Whitespace / Trim
================
*/
// Returns the first non-whitespace character in pString.
const char *Cy_strskipwhite( const char *pString );
char *Cy_strskipwhite( char *pString );

// Removes leading ASCII whitespace in place.
void Cy_strtrimleft( char *pString );

// Removes trailing ASCII whitespace in place.
void Cy_strtrimright( char *pString );

// Removes leading and trailing ASCII whitespace in place.
void Cy_strtrim( char *pString );

// Removes one matching pair of surrounding quotes in place.
void Cy_strstripquotes( char *pString );

/*
================
Slice / Replace
================
*/
// Copies the leftmost cchCount characters into pDest.
usize Cy_strleft( const char *pString, char *pDest, usize cchDest, usize cchCount );

// Copies the rightmost cchCount characters into pDest.
usize Cy_strright( const char *pString, char *pDest, usize cchDest, usize cchCount );

// Copies cchCount characters starting at iStart into pDest.
usize Cy_strslice( const char *pString, char *pDest, usize cchDest, usize iStart, usize cchCount );

// Replaces occurrences of pSearch with pReplace into pDest.
usize Cy_strsubst( const char *pString, const char *pSearch, const char *pReplace, char *pDest, usize cchDest );

// Counts occurrences of chFind.
usize Cy_strcountchar( const char *pString, char chFind );

// Counts occurrences of pSearch.
usize Cy_strcountstring( const char *pString, const char *pSearch );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRING_H
