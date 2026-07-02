//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_RangeCheckedVar.h
//  Purpose: Declares CypherCommon Tier1 RangeCheckedVar support.
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

#ifndef CYPHER_COMMON_TIER1_RANGECHECKEDVAR_H
#define CYPHER_COMMON_TIER1_RANGECHECKEDVAR_H
#pragma once

/*
================
CypherCommon Range Checked Var

Range-checked variable declarations.
================
*/

namespace cypher::common
{

template <typename type_t>
struct range_checked_var_t;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_RANGECHECKEDVAR_H
