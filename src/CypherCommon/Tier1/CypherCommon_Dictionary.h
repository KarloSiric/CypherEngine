//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Dictionary.h
//  Purpose: Declares CypherCommon Tier1 Dictionary support.
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

#ifndef CYPHER_COMMON_TIER1_DICTIONARY_H
#define CYPHER_COMMON_TIER1_DICTIONARY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Dictionary

String-keyed dictionary declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename value_t>
struct dictionary_t;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_DICTIONARY_H
