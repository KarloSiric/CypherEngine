//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_HandleTable.h
//  Purpose: Declares CypherCommon Tier1 HandleTable support.
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

#ifndef CYPHER_COMMON_TIER1_HANDLETABLE_H
#define CYPHER_COMMON_TIER1_HANDLETABLE_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Handle Table

Generational handle table declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename type_t>
struct handle_table_t;

template <typename type_t>
handle_t HandleTable_Add( handle_table_t<type_t> *pTable, const type_t &value );

template <typename type_t>
type_t *HandleTable_Get( handle_table_t<type_t> *pTable, handle_t handle );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_HANDLETABLE_H
