//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ExpressionEvaluator.h
//  Purpose: Declares CypherCommon Tier1 ExpressionEvaluator support.
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

#ifndef CYPHER_COMMON_TIER1_EXPRESSIONEVALUATOR_H
#define CYPHER_COMMON_TIER1_EXPRESSIONEVALUATOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Expression Evaluator

Small expression evaluator declarations.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct expression_result_t {
    f64 value;
    bool_t valid;
};

expression_result_t ExpressionEvaluator_Evaluate( const char *pExpression );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_EXPRESSIONEVALUATOR_H
