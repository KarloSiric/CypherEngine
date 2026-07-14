//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Array.h
//  Purpose: Declares CypherCommon Tier1 Array support.
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

#ifndef CYPHER_COMMON_TIER1_ARRAY_H
#define CYPHER_COMMON_TIER1_ARRAY_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Array

Owning dynamic array declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename type_t>
struct array_t;

template <typename type_t>
bool_t Array_Init( array_t<type_t> *pArray, usize capacity );

template <typename type_t>
void Array_Shutdown( array_t<type_t> *pArray );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_ARRAY_H
