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
#pragma once

/*
================
CypherCommon String View

Non-owning string slice declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct string_view_t {
    const char *pData;
    usize cchLength;
};

string_view_t StringView_FromCString( const char *pString );
string_view_t StringView_FromRange( const char *pData, usize cchLength );
bool_t StringView_IsEmpty( string_view_t view );
i32 StringView_Compare( string_view_t a, string_view_t b );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_STRINGVIEW_H
