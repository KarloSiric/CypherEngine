//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_FixedString.h
//  Purpose: Declares CypherCommon Tier1 FixedString support.
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

#ifndef CYPHER_COMMON_TIER1_FIXEDSTRING_H
#define CYPHER_COMMON_TIER1_FIXEDSTRING_H
#pragma once

/*
================
CypherCommon Fixed String

Fixed-capacity stack string declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <usize cchCapacity>
struct fixed_string_t;

template <usize cchCapacity>
usize FixedString_Length( const fixed_string_t<cchCapacity> &string );

template <usize cchCapacity>
usize FixedString_Copy( fixed_string_t<cchCapacity> *pString, const char *pSrc );

template <usize cchCapacity>
usize FixedString_Append( fixed_string_t<cchCapacity> *pString, const char *pSrc );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_FIXEDSTRING_H
