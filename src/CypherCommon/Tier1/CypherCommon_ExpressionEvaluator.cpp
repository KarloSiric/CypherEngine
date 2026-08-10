//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_ExpressionEvaluator.cpp
//  Purpose: Implements bounded numeric expression evaluation.
//  Details: A recursive-descent parser consumes the shared lexer. Fixed argument
//           storage and a depth budget keep evaluation allocation-free and bounded.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_ExpressionEvaluator.h"

#include "CypherCommon_StringParse.h"

#include <cmath>

namespace cypher::common
{

namespace
{

constexpr string_view_t EXPRESSION_PUNCTUATIONS[]{
    { "&&", 2u }, { "||", 2u }, { "==", 2u }, { "!=", 2u },
    { "<=", 2u }, { ">=", 2u },
    { "+", 1u }, { "-", 1u }, { "*", 1u }, { "/", 1u },
    { "%", 1u }, { "!", 1u }, { "<", 1u }, { ">", 1u },
    { "(", 1u }, { ")", 1u }, { ",", 1u }
};

struct expression_parser_t {
    lexer_t lexer{};
    token_t token{};
    expression_context_t context{};
    expression_status_t status{ expression_status_t::OK };
    text_location_t errorLocation{};
};

void SetError(
    expression_parser_t &parser,
    expression_status_t status,
    text_location_t location ) noexcept
{
    if ( parser.status == expression_status_t::OK ) {
        parser.status = status;
        parser.errorLocation = location;
    }
}

bool_t Advance( expression_parser_t &parser ) noexcept
{
    if ( parser.status != expression_status_t::OK ) {
        return CY_FALSE;
    }
    const lexer_status_t status = Lexer_Read( &parser.lexer, &parser.token );
    if ( status == lexer_status_t::OK ||
         status == lexer_status_t::END_OF_INPUT ) {
        return CY_TRUE;
    }
    SetError(
        parser,
        expression_status_t::SYNTAX_ERROR,
        parser.lexer.errorLocation );
    return CY_FALSE;
}

bool_t TokenIs( const expression_parser_t &parser, const char *pText ) noexcept
{
    return parser.token.kind == token_kind_t::PUNCTUATION &&
           StringView_Equals(
               parser.token.lexeme,
               StringView_FromCString( pText ) );
}

bool_t Consume(
    expression_parser_t &parser,
    const char *pText ) noexcept
{
    if ( !TokenIs( parser, pText ) ) {
        return CY_FALSE;
    }
    return Advance( parser );
}

bool_t CheckFinite(
    expression_parser_t &parser,
    f64 value,
    text_location_t location ) noexcept
{
    if ( std::isfinite( value ) ) {
        return CY_TRUE;
    }
    SetError( parser, expression_status_t::NUMERIC_OVERFLOW, location );
    return CY_FALSE;
}

bool_t ParseOr(
    expression_parser_t &parser,
    u32 nDepth,
    bool_t bEvaluate,
    f64 &valueOut ) noexcept;

bool_t ParseNumber(
    expression_parser_t &parser,
    f64 &valueOut ) noexcept
{
    const token_t number = parser.token;
    if ( number.kind == token_kind_t::FLOAT ) {
        f64 value = 0.0;
        const string_parse_result_t result = StringParse_F64(
            number.lexeme,
            STRING_PARSE_FLAG_ALLOW_PLUS_SIGN |
                STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR,
            &value );
        if ( !StringParse_Succeeded( result ) ) {
            SetError(
                parser,
                result.status == string_parse_status_t::NUMERIC_OVERFLOW ||
                        result.status == string_parse_status_t::NUMERIC_UNDERFLOW ||
                        result.status == string_parse_status_t::NON_FINITE_VALUE
                    ? expression_status_t::NUMERIC_OVERFLOW
                    : expression_status_t::SYNTAX_ERROR,
                number.range.begin );
            return CY_FALSE;
        }
        valueOut = value;
    } else {
        const string_parse_options_t options{
            0u,
            STRING_PARSE_FLAG_ALLOW_PLUS_SIGN |
                STRING_PARSE_FLAG_ALLOW_BASE_PREFIX |
                STRING_PARSE_FLAG_ALLOW_DIGIT_SEPARATOR
        };
        if ( number.lexeme.cchLength != 0u &&
             number.lexeme.pData[0] == '-' ) {
            i64 value = 0;
            const string_parse_result_t result = StringParse_I64(
                number.lexeme,
                options,
                &value );
            if ( !StringParse_Succeeded( result ) ) {
                SetError( parser, expression_status_t::NUMERIC_OVERFLOW, number.range.begin );
                return CY_FALSE;
            }
            valueOut = static_cast<f64>( value );
        } else {
            u64 value = 0u;
            const string_parse_result_t result = StringParse_U64(
                number.lexeme,
                options,
                &value );
            if ( !StringParse_Succeeded( result ) ) {
                SetError( parser, expression_status_t::NUMERIC_OVERFLOW, number.range.begin );
                return CY_FALSE;
            }
            valueOut = static_cast<f64>( value );
        }
    }
    return Advance( parser );
}

bool_t ParsePrimary(
    expression_parser_t &parser,
    u32 nDepth,
    bool_t bEvaluate,
    f64 &valueOut ) noexcept
{
    if ( nDepth > parser.context.nMaxDepth ) {
        SetError( parser, expression_status_t::DEPTH_LIMIT, parser.token.range.begin );
        return CY_FALSE;
    }
    if ( parser.token.kind == token_kind_t::INTEGER ||
         parser.token.kind == token_kind_t::FLOAT ) {
        return ParseNumber( parser, valueOut );
    }
    if ( Consume( parser, "(" ) ) {
        if ( !ParseOr( parser, nDepth + 1u, bEvaluate, valueOut ) ) {
            return CY_FALSE;
        }
        if ( !TokenIs( parser, ")" ) ) {
            SetError( parser, expression_status_t::SYNTAX_ERROR, parser.token.range.begin );
            return CY_FALSE;
        }
        return Advance( parser );
    }
    if ( parser.token.kind != token_kind_t::IDENTIFIER ) {
        SetError( parser, expression_status_t::SYNTAX_ERROR, parser.token.range.begin );
        return CY_FALSE;
    }

    const token_t identifier = parser.token;
    if ( !Advance( parser ) ) {
        return CY_FALSE;
    }
    if ( TokenIs( parser, "(" ) ) {
        if ( !Advance( parser ) ) {
            return CY_FALSE;
        }
        f64 arguments[CY_EXPRESSION_MAX_FUNCTION_ARGUMENTS]{};
        usize nArgumentCount = 0u;
        if ( !TokenIs( parser, ")" ) ) {
            for ( ;; ) {
                if ( nArgumentCount >= parser.context.nMaxFunctionArguments ) {
                    SetError(
                        parser,
                        expression_status_t::INVALID_ARGUMENT_COUNT,
                        parser.token.range.begin );
                    return CY_FALSE;
                }
                if ( !ParseOr(
                         parser,
                         nDepth + 1u,
                         bEvaluate,
                         arguments[nArgumentCount] ) ) {
                    return CY_FALSE;
                }
                ++nArgumentCount;
                if ( !TokenIs( parser, "," ) ) {
                    break;
                }
                if ( !Advance( parser ) ) {
                    return CY_FALSE;
                }
            }
        }
        if ( !TokenIs( parser, ")" ) ) {
            SetError( parser, expression_status_t::SYNTAX_ERROR, parser.token.range.begin );
            return CY_FALSE;
        }
        if ( !Advance( parser ) ) {
            return CY_FALSE;
        }
        if ( !bEvaluate ) {
            valueOut = 0.0;
            return CY_TRUE;
        }
        if ( parser.context.pfnCallFunction == nullptr ) {
            SetError( parser, expression_status_t::UNKNOWN_FUNCTION, identifier.range.begin );
            return CY_FALSE;
        }
        if ( !parser.context.pfnCallFunction(
                 identifier.lexeme,
                 arguments,
                 nArgumentCount,
                 &valueOut,
                 parser.context.pUserData ) ) {
            SetError(
                parser,
                expression_status_t::INVALID_ARGUMENT_COUNT,
                identifier.range.begin );
            return CY_FALSE;
        }
        return CheckFinite( parser, valueOut, identifier.range.begin );
    }

    if ( StringView_Equals(
             identifier.lexeme,
             StringView_FromCString( "true" ) ) ) {
        valueOut = 1.0;
        return CY_TRUE;
    }
    if ( StringView_Equals(
             identifier.lexeme,
             StringView_FromCString( "false" ) ) ) {
        valueOut = 0.0;
        return CY_TRUE;
    }
    if ( !bEvaluate ) {
        valueOut = 0.0;
        return CY_TRUE;
    }
    if ( parser.context.pfnResolveVariable == nullptr ||
         !parser.context.pfnResolveVariable(
             identifier.lexeme,
             &valueOut,
             parser.context.pUserData ) ) {
        SetError(
            parser,
            expression_status_t::UNKNOWN_IDENTIFIER,
            identifier.range.begin );
        return CY_FALSE;
    }
    return CheckFinite( parser, valueOut, identifier.range.begin );
}

bool_t ParseUnary(
    expression_parser_t &parser,
    u32 nDepth,
    bool_t bEvaluate,
    f64 &valueOut ) noexcept
{
    if ( nDepth > parser.context.nMaxDepth ) {
        SetError( parser, expression_status_t::DEPTH_LIMIT, parser.token.range.begin );
        return CY_FALSE;
    }
    if ( TokenIs( parser, "+" ) ||
         TokenIs( parser, "-" ) ||
         TokenIs( parser, "!" ) ) {
        const token_t operation = parser.token;
        if ( !Advance( parser ) ||
             !ParseUnary( parser, nDepth + 1u, bEvaluate, valueOut ) ) {
            return CY_FALSE;
        }
        if ( !bEvaluate ) {
            valueOut = 0.0;
            return CY_TRUE;
        }
        if ( operation.lexeme.pData[0] == '-' ) {
            valueOut = -valueOut;
            return CheckFinite( parser, valueOut, operation.range.begin );
        }
        if ( operation.lexeme.pData[0] == '!' ) {
            valueOut = valueOut == 0.0 ? 1.0 : 0.0;
        }
        return CY_TRUE;
    }
    return ParsePrimary( parser, nDepth, bEvaluate, valueOut );
}

bool_t ParseMultiply(
    expression_parser_t &parser,
    u32 nDepth,
    bool_t bEvaluate,
    f64 &valueOut ) noexcept
{
    if ( !ParseUnary( parser, nDepth, bEvaluate, valueOut ) ) {
        return CY_FALSE;
    }
    while ( TokenIs( parser, "*" ) ||
            TokenIs( parser, "/" ) ||
            TokenIs( parser, "%" ) ) {
        const token_t operation = parser.token;
        if ( !Advance( parser ) ) {
            return CY_FALSE;
        }
        f64 right = 0.0;
        if ( !ParseUnary( parser, nDepth, bEvaluate, right ) ) {
            return CY_FALSE;
        }
        if ( !bEvaluate ) {
            continue;
        }
        if ( ( operation.lexeme.pData[0] == '/' ||
               operation.lexeme.pData[0] == '%' ) && right == 0.0 ) {
            SetError(
                parser,
                expression_status_t::DIVIDE_BY_ZERO,
                operation.range.begin );
            return CY_FALSE;
        }
        if ( operation.lexeme.pData[0] == '*' ) {
            valueOut *= right;
        } else if ( operation.lexeme.pData[0] == '/' ) {
            valueOut /= right;
        } else {
            valueOut = std::fmod( valueOut, right );
        }
        if ( !CheckFinite( parser, valueOut, operation.range.begin ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t ParseAdd(
    expression_parser_t &parser,
    u32 nDepth,
    bool_t bEvaluate,
    f64 &valueOut ) noexcept
{
    if ( !ParseMultiply( parser, nDepth, bEvaluate, valueOut ) ) {
        return CY_FALSE;
    }
    while ( TokenIs( parser, "+" ) || TokenIs( parser, "-" ) ) {
        const token_t operation = parser.token;
        if ( !Advance( parser ) ) {
            return CY_FALSE;
        }
        f64 right = 0.0;
        if ( !ParseMultiply( parser, nDepth, bEvaluate, right ) ) {
            return CY_FALSE;
        }
        if ( bEvaluate ) {
            valueOut = operation.lexeme.pData[0] == '+'
                ? valueOut + right
                : valueOut - right;
            if ( !CheckFinite( parser, valueOut, operation.range.begin ) ) {
                return CY_FALSE;
            }
        }
    }
    return CY_TRUE;
}

bool_t ParseComparison(
    expression_parser_t &parser,
    u32 nDepth,
    bool_t bEvaluate,
    f64 &valueOut ) noexcept
{
    if ( !ParseAdd( parser, nDepth, bEvaluate, valueOut ) ) {
        return CY_FALSE;
    }
    while ( TokenIs( parser, "<" ) || TokenIs( parser, "<=" ) ||
            TokenIs( parser, ">" ) || TokenIs( parser, ">=" ) ) {
        const token_t operation = parser.token;
        if ( !Advance( parser ) ) {
            return CY_FALSE;
        }
        f64 right = 0.0;
        if ( !ParseAdd( parser, nDepth, bEvaluate, right ) ) {
            return CY_FALSE;
        }
        if ( bEvaluate ) {
            if ( StringView_Equals( operation.lexeme, { "<", 1u } ) ) {
                valueOut = valueOut < right ? 1.0 : 0.0;
            } else if ( StringView_Equals( operation.lexeme, { "<=", 2u } ) ) {
                valueOut = valueOut <= right ? 1.0 : 0.0;
            } else if ( StringView_Equals( operation.lexeme, { ">", 1u } ) ) {
                valueOut = valueOut > right ? 1.0 : 0.0;
            } else {
                valueOut = valueOut >= right ? 1.0 : 0.0;
            }
        }
    }
    return CY_TRUE;
}

bool_t ParseEquality(
    expression_parser_t &parser,
    u32 nDepth,
    bool_t bEvaluate,
    f64 &valueOut ) noexcept
{
    if ( !ParseComparison( parser, nDepth, bEvaluate, valueOut ) ) {
        return CY_FALSE;
    }
    while ( TokenIs( parser, "==" ) || TokenIs( parser, "!=" ) ) {
        const bool_t bEqualOperation = TokenIs( parser, "==" );
        if ( !Advance( parser ) ) {
            return CY_FALSE;
        }
        f64 right = 0.0;
        if ( !ParseComparison( parser, nDepth, bEvaluate, right ) ) {
            return CY_FALSE;
        }
        if ( bEvaluate ) {
            const bool_t bEqual = valueOut == right;
            valueOut = bEqual == bEqualOperation ? 1.0 : 0.0;
        }
    }
    return CY_TRUE;
}

bool_t ParseAnd(
    expression_parser_t &parser,
    u32 nDepth,
    bool_t bEvaluate,
    f64 &valueOut ) noexcept
{
    if ( !ParseEquality( parser, nDepth, bEvaluate, valueOut ) ) {
        return CY_FALSE;
    }
    while ( TokenIs( parser, "&&" ) ) {
        const bool_t bLeftTrue = valueOut != 0.0;
        if ( !Advance( parser ) ) {
            return CY_FALSE;
        }
        f64 right = 0.0;
        if ( !ParseEquality(
                 parser,
                 nDepth,
                 bEvaluate && bLeftTrue,
                 right ) ) {
            return CY_FALSE;
        }
        if ( bEvaluate ) {
            valueOut = bLeftTrue && right != 0.0 ? 1.0 : 0.0;
        }
    }
    return CY_TRUE;
}

bool_t ParseOr(
    expression_parser_t &parser,
    u32 nDepth,
    bool_t bEvaluate,
    f64 &valueOut ) noexcept
{
    if ( !ParseAnd( parser, nDepth, bEvaluate, valueOut ) ) {
        return CY_FALSE;
    }
    while ( TokenIs( parser, "||" ) ) {
        const bool_t bLeftTrue = valueOut != 0.0;
        if ( !Advance( parser ) ) {
            return CY_FALSE;
        }
        f64 right = 0.0;
        if ( !ParseAnd(
                 parser,
                 nDepth,
                 bEvaluate && !bLeftTrue,
                 right ) ) {
            return CY_FALSE;
        }
        if ( bEvaluate ) {
            valueOut = bLeftTrue || right != 0.0 ? 1.0 : 0.0;
        }
    }
    return CY_TRUE;
}

} // namespace

expression_result_t ExpressionEvaluator_Evaluate(
    string_view_t expression,
    const expression_context_t &context ) noexcept
{
    expression_result_t result{};
    if ( !StringView_IsValid( expression ) ||
         expression.cchLength == 0u ||
         context.nMaxDepth == 0u ||
         context.nMaxFunctionArguments > CY_EXPRESSION_MAX_FUNCTION_ARGUMENTS ) {
        result.status = expression_status_t::INVALID_ARGUMENT;
        return result;
    }

    lexer_rules_t rules = Lexer_DefaultRules();
    rules.pPunctuations = EXPRESSION_PUNCTUATIONS;
    rules.nPunctuationCount =
        sizeof( EXPRESSION_PUNCTUATIONS ) / sizeof( EXPRESSION_PUNCTUATIONS[0] );

    expression_parser_t parser{};
    parser.context = context;
    if ( !Lexer_Init( &parser.lexer, expression, rules ) || !Advance( parser ) ) {
        result.status = expression_status_t::SYNTAX_ERROR;
        result.errorLocation = parser.lexer.errorLocation;
        return result;
    }

    f64 value = 0.0;
    if ( ParseOr( parser, 1u, CY_TRUE, value ) &&
         parser.token.kind != token_kind_t::END_OF_INPUT ) {
        SetError( parser, expression_status_t::SYNTAX_ERROR, parser.token.range.begin );
    }
    result.status = parser.status;
    result.errorLocation = parser.errorLocation;
    result.value = parser.status == expression_status_t::OK ? value : 0.0;
    return result;
}

} // namespace cypher::common
