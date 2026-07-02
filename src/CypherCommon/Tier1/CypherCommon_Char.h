//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Char.h
//  Purpose: Declares CypherCommon Tier1 Char support.
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

#ifndef CYPHER_COMMON_TIER1_CHAR_H
#define CYPHER_COMMON_TIER1_CHAR_H
#pragma once

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

bool_t Char_IsAscii( char c );

bool_t Char_IsControlAscii( char c );

bool_t Char_IsPrintableAscii( char c );

bool_t Char_IsUpperAscii( char c );

bool_t Char_IsLowerAscii( char c );

bool_t Char_IsAlphaAscii( char c );

bool_t Char_IsDigitAscii( char c );

bool_t Char_IsIdentifierStart( char c );

bool_t Char_IsIdentifierBody( char c );

bool_t Char_IsPathNameChar( char c );

bool_t Char_IsHexDigitAscii( char c );

bool_t Char_IsAlphaNumericAscii( char c );

bool_t Char_IsWhitespaceAscii( char c );

bool_t Char_IsNewLineAscii( char c );

bool_t Char_IsSlash( char c );

bool_t Char_IsPathSeparator( char c );

bool_t Char_IsDriveSeparator( char c );

char Char_ToLowerAscii( char c );

char Char_ToUpperAscii( char c );

u8 Char_HexValueAscii( char c );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_CHAR_H
