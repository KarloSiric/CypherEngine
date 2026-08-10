//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ExpressionEvaluator.h
//  Purpose: Declares a bounded numeric expression evaluator.
//  Details: Evaluation supports arithmetic, comparison, Boolean operators, variables,
//           and injected functions without allocation or executable code generation.
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

#include "CypherCommon_Lexer.h"

namespace cypher::common
{

constexpr usize CY_EXPRESSION_MAX_FUNCTION_ARGUMENTS = 32u;

enum class expression_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    SYNTAX_ERROR,
    UNKNOWN_IDENTIFIER,
    UNKNOWN_FUNCTION,
    INVALID_ARGUMENT_COUNT,
    DIVIDE_BY_ZERO,
    NUMERIC_OVERFLOW,
    DEPTH_LIMIT
};

using expression_resolve_variable_fn_t = bool_t ( * )(
    string_view_t name,
    f64 *pValueOut,
    void *pUserData ) noexcept;

using expression_call_function_fn_t = bool_t ( * )(
    string_view_t name,
    const f64 *pArguments,
    usize nArgumentCount,
    f64 *pValueOut,
    void *pUserData ) noexcept;

struct expression_context_t {
    expression_resolve_variable_fn_t pfnResolveVariable{ nullptr };
    expression_call_function_fn_t pfnCallFunction{ nullptr };
    void *pUserData{ nullptr };
    u32 nMaxDepth{ 64u };
    usize nMaxFunctionArguments{ 16u };
};

struct expression_result_t {
    expression_status_t status{ expression_status_t::OK };
    f64 value{ 0.0 };
    text_location_t errorLocation{};
};

CYPHER_NODISCARD CYPHER_COMMON_API
expression_result_t ExpressionEvaluator_Evaluate(
    string_view_t expression,
    const expression_context_t &context = {} ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_EXPRESSIONEVALUATOR_H
