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

/*
================
Expression Evaluator Contract

Expression evaluation uses bounded tokens and explicit numeric rules. Invalid syntax, overflow,
and division errors are returned as data rather than escaping through exceptions.
================
*/

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
    OK = 0u,             // Expression evaluated to a finite result.
    INVALID_ARGUMENT,    // Input or context contract is invalid.
    SYNTAX_ERROR,        // Tokens do not form a valid expression.
    UNKNOWN_IDENTIFIER,  // Variable resolver rejected a referenced name.
    UNKNOWN_FUNCTION,    // Function resolver rejected a called name.
    INVALID_ARGUMENT_COUNT, // Call exceeds policy or function arity.
    DIVIDE_BY_ZERO,      // Division or remainder denominator is zero.
    NUMERIC_OVERFLOW,    // Evaluation produced a non-finite scalar.
    DEPTH_LIMIT          // Recursive expression nesting exceeded policy.
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
    expression_resolve_variable_fn_t pfnResolveVariable{ nullptr }; // Optional name resolver.
    expression_call_function_fn_t pfnCallFunction{ nullptr };       // Optional function dispatcher.
    void *pUserData{ nullptr };                         // Opaque resolver/call state.
    u32 nMaxDepth{ 64u };                               // Recursive parse/evaluation limit.
    usize nMaxFunctionArguments{ 16u };                 // Per-call argument ceiling.
};

struct expression_result_t {
    expression_status_t status{ expression_status_t::OK }; // Evaluation result.
    f64 value{ 0.0 };                         // Valid only when status is OK.
    text_location_t errorLocation{};          // Source position associated with failure.
};

CYPHER_NODISCARD CYPHER_COMMON_API
expression_result_t ExpressionEvaluator_Evaluate(
    string_view_t expression,
    const expression_context_t &context = {} ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_EXPRESSIONEVALUATOR_H
