//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ArrayView.h
//  Purpose: Declares CypherCommon Tier1 ArrayView support.
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

#ifndef CYPHER_COMMON_TIER1_ARRAYVIEW_H
#define CYPHER_COMMON_TIER1_ARRAYVIEW_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Array View

Read-only array view declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

template <typename type_t>
struct array_view_t {
    const type_t *pData;
    usize count;
};

template <typename type_t>
array_view_t<type_t> ArrayView_FromPointerCount( const type_t *pData, usize count );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_ARRAYVIEW_H
