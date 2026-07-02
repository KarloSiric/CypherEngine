//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Span.h
//  Purpose: Declares CypherCommon Tier1 Span support.
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

#ifndef CYPHER_COMMON_TIER1_SPAN_H
#define CYPHER_COMMON_TIER1_SPAN_H
#pragma once

/*
================
CypherCommon Span

Non-owning contiguous array view declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename type_t>
struct span_t {
    type_t *pData;
    usize count;
};

template <typename type_t>
span_t<type_t> Span_FromPointerCount( type_t *pData, usize count );

template <typename type_t>
bool_t Span_IsEmpty( span_t<type_t> span );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SPAN_H
