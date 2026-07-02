//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Unicode.h
//  Purpose: Declares CypherCommon Tier1 Unicode support.
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

#ifndef CYPHER_COMMON_TIER1_UNICODE_H
#define CYPHER_COMMON_TIER1_UNICODE_H
#pragma once

/*
================
CypherCommon Unicode

UTF validation and conversion helpers for tools, editor, localization and UI.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

using uchar16 = u16;
using uchar32 = u32;

/*
================
UTF-8 Queries
================
*/
// Returns true when pString is valid UTF-8.
bool_t Cy_UTF8Validate( const char *pString );

// Counts Unicode code points in a UTF-8 string.
usize Cy_UTF8Length( const char *pString );

// Returns the byte count needed to store cchCodePoints UTF-8 code points.
usize Cy_UTF8TruncateBytes( const char *pString, usize cchCodePoints );

/*
================
UTF-8 Repair / Copy
================
*/
// Repairs invalid UTF-8 into pDest.
usize Cy_UTF8Repair( const char *pString, char *pDest, usize cchDest );

// Copies pString into pDest without splitting a UTF-8 code point.
usize Cy_UTF8Truncate( const char *pString, char *pDest, usize cchDest, usize cchCodePoints );

/*
================
Code Point Conversion
================
*/
// Encodes one Unicode code point as UTF-8.
usize Cy_UnicodeToUTF8( uchar32 uCodePoint, char *pDest, usize cchDest );

// Decodes one UTF-8 code point.
usize Cy_UTF8ToUnicode( const char *pString, uchar32 *pOutCodePoint );

/*
================
UTF Encoding Conversion
================
*/
// Converts UTF-8 text to UTF-16.
usize Cy_UTF8ToUTF16( const char *pString, uchar16 *pDest, usize cchDest );

// Converts UTF-16 text to UTF-8.
usize Cy_UTF16ToUTF8( const uchar16 *pString, char *pDest, usize cchDest );

// Converts UTF-8 text to UTF-32.
usize Cy_UTF8ToUTF32( const char *pString, uchar32 *pDest, usize cchDest );

// Converts UTF-32 text to UTF-8.
usize Cy_UTF32ToUTF8( const uchar32 *pString, char *pDest, usize cchDest );

// Converts UTF-16 text to UTF-32.
usize Cy_UTF16ToUTF32( const uchar16 *pString, uchar32 *pDest, usize cchDest );

// Converts UTF-32 text to UTF-16.
usize Cy_UTF32ToUTF16( const uchar32 *pString, uchar16 *pDest, usize cchDest );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_UNICODE_H
