//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: tests/CypherCommon/Tier1/CypherCommon_Tier1_ExpressionEvaluator_Tests.cpp
//  Purpose: Tests bounded numeric expression evaluation.
//  Details: Covers precedence, unary and Boolean operators, variables, functions,
//           short-circuiting, diagnostics, numeric failures, and depth limits.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ExpressionEvaluator.h"

#include <catch2/catch_test_macros.hpp>

using namespace cypher::common;

namespace
{

struct expression_test_state_t {
    usize nVariableCalls{ 0u };
};

bool_t ResolveVariable(
    string_view_t name,
    f64 *pValueOut,
    void *pUserData ) noexcept
{
    auto *pState = static_cast<expression_test_state_t *>( pUserData );
    ++pState->nVariableCalls;
    if ( StringView_Equals( name, StringView_FromCString( "health" ) ) ) {
        *pValueOut = 75.0;
        return CY_TRUE;
    }
    return CY_FALSE;
}

bool_t CallFunction(
    string_view_t name,
    const f64 *pArguments,
    usize nArgumentCount,
    f64 *pValueOut,
    void * ) noexcept
{
    if ( !StringView_Equals( name, StringView_FromCString( "max" ) ) ||
         nArgumentCount != 2u ) {
        return CY_FALSE;
    }
    *pValueOut = pArguments[0] > pArguments[1]
        ? pArguments[0]
        : pArguments[1];
    return CY_TRUE;
}

} // namespace

TEST_CASE( "ExpressionEvaluator applies arithmetic and Boolean precedence",
           "[CypherCommon][Tier1][ExpressionEvaluator]" )
{
    expression_result_t result = ExpressionEvaluator_Evaluate(
        StringView_FromCString( "1 + 2 * 3 == 7 && !(4 < 2)" ) );
    REQUIRE( result.status == expression_status_t::OK );
    REQUIRE( result.value == 1.0 );

    result = ExpressionEvaluator_Evaluate(
        StringView_FromCString( "(10 - 4) / 3 + 5 % 2" ) );
    REQUIRE( result.status == expression_status_t::OK );
    REQUIRE( result.value == 3.0 );
}

TEST_CASE( "ExpressionEvaluator resolves variables and bounded functions",
           "[CypherCommon][Tier1][ExpressionEvaluator]" )
{
    expression_test_state_t state{};
    expression_context_t context{
        ResolveVariable,
        CallFunction,
        &state,
        32u,
        8u
    };
    const expression_result_t result = ExpressionEvaluator_Evaluate(
        StringView_FromCString( "max(health, 100) - 25" ),
        context );
    REQUIRE( result.status == expression_status_t::OK );
    REQUIRE( result.value == 75.0 );
    REQUIRE( state.nVariableCalls == 1u );
}

TEST_CASE( "ExpressionEvaluator short circuits callbacks and invalid arithmetic",
           "[CypherCommon][Tier1][ExpressionEvaluator]" )
{
    expression_test_state_t state{};
    expression_context_t context{};
    context.pfnResolveVariable = ResolveVariable;
    context.pUserData = &state;

    expression_result_t result = ExpressionEvaluator_Evaluate(
        StringView_FromCString( "true || missing" ),
        context );
    REQUIRE( result.status == expression_status_t::OK );
    REQUIRE( result.value == 1.0 );
    REQUIRE( state.nVariableCalls == 0u );

    result = ExpressionEvaluator_Evaluate(
        StringView_FromCString( "false && (1 / 0)" ),
        context );
    REQUIRE( result.status == expression_status_t::OK );
    REQUIRE( result.value == 0.0 );
}

TEST_CASE( "ExpressionEvaluator reports syntax semantic and depth errors",
           "[CypherCommon][Tier1][ExpressionEvaluator]" )
{
    expression_result_t result = ExpressionEvaluator_Evaluate(
        StringView_FromCString( "1 / 0" ) );
    REQUIRE( result.status == expression_status_t::DIVIDE_BY_ZERO );
    REQUIRE( result.errorLocation.nColumn == 3u );

    result = ExpressionEvaluator_Evaluate(
        StringView_FromCString( "unknown + 1" ) );
    REQUIRE( result.status == expression_status_t::UNKNOWN_IDENTIFIER );

    expression_context_t context{};
    context.nMaxDepth = 2u;
    result = ExpressionEvaluator_Evaluate(
        StringView_FromCString( "(((1)))" ),
        context );
    REQUIRE( result.status == expression_status_t::DEPTH_LIMIT );

    result = ExpressionEvaluator_Evaluate(
        StringView_FromCString( "1 +" ) );
    REQUIRE( result.status == expression_status_t::SYNTAX_ERROR );
}
