//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringSplit.cpp
//  Purpose: Implements CypherCommon Tier1 StringSplit support.
//  Details: Splits bounded string views without allocation and supports array
//           output, count-only queries, and cancellable visitor callbacks.
//
//  History:
//  - Created by Karlo Siric on 2026-08-04
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringSplit.h"

#include "CypherCommon_Assert.h"

namespace cypher::common
{

string_split_result_t StringSplit_ByChar(
    string_view_t source,
    char chDelimiter,
    flags32_t flags,
    string_view_t *pTokens,
    usize cTokenCapacity ) noexcept
{
    string_split_result_t result{};
    const bool_t bValidSource = StringView_IsValid( source );
    const bool_t bValidFlags = ( flags & ~STRING_SPLIT_VALID_FLAGS ) == 0u;
    const bool_t bValidOutput = pTokens != nullptr || cTokenCapacity == 0u;

    CY_ASSERT_MSG( bValidSource, "StringSplit_ByChar requires a valid source view." );
    CY_ASSERT_MSG( bValidFlags, "StringSplit_ByChar received unsupported flags." );
    CY_ASSERT_MSG( bValidOutput, "StringSplit_ByChar requires output storage for non-zero capacity." );

    if ( !bValidSource || !bValidFlags || !bValidOutput ) {
        return result;
    }

    const bool_t bTrimWhitespace = ( flags & STRING_SPLIT_FLAG_TRIM_WHITESPACE ) != 0u;
    const bool_t bSkipEmpty = ( flags & STRING_SPLIT_FLAG_SKIP_EMPTY ) != 0u;

    usize iTokenStart = 0u;
    // Include the synthetic end boundary so the final field is emitted.
    for ( usize iBoundary = 0u; iBoundary <= source.cchLength; ++iBoundary ) {
        const bool_t bReachedEnd = ( iBoundary == source.cchLength );
        const bool_t bFoundDelimiter = !bReachedEnd && source.pData[iBoundary] == chDelimiter;

        // keep scanning until we find the actual delimiter.
        if ( !bReachedEnd && !bFoundDelimiter ) {
            continue;
        }
        
        string_view_t token{};
        token.pData = source.pData != nullptr ? source.pData + iTokenStart : nullptr;
        // we calculate the length from the start to the boundary we found where the delimiter is.
        // we also exclude the delimiter and include the token itself.
        token.cchLength = iBoundary - iTokenStart;

        if ( bTrimWhitespace ) {
            token = StringView_Trim( token );
        }
        const bool_t bDiscardToken = bSkipEmpty && StringView_IsEmpty( token );
        if ( !bDiscardToken ) {
            ++result.cTokensRequired;
            if ( result.cTokensWritten < cTokenCapacity ) {
                pTokens[result.cTokensWritten] = token;
                ++result.cTokensWritten;
            }
        }

        if ( bReachedEnd ) {
            break;
        }
        iTokenStart = iBoundary + 1u;
    }

    return result;
}

string_split_result_t StringSplit_BySet(
    string_view_t source,
    const character_set_t *pDelimiters,
    flags32_t flags,
    string_view_t *pTokens,
    usize cTokenCapacity ) noexcept
{
    string_split_result_t result{};

    const bool_t bValidSource = StringView_IsValid( source );
    const bool_t bValidFlags = ( flags & ~STRING_SPLIT_VALID_FLAGS ) == 0u;
    const bool_t bValidDelimiters = pDelimiters != nullptr;
    const bool_t bValidOutput = pTokens != nullptr || cTokenCapacity == 0u;

    CY_ASSERT_MSG( bValidSource, "StringSplit_BySet requires a valid source." );
    CY_ASSERT_MSG( bValidFlags, "StringSplit_BySet received unsupported flags." );
    CY_ASSERT_MSG( bValidDelimiters, "StringSplit_BySet requires a non-null delimiter set." );
    CY_ASSERT_MSG( bValidOutput, "StringSplit_BySet requires output storage for non-zero capacity." );

    if ( !bValidSource || !bValidFlags || !bValidDelimiters || !bValidOutput ) {
        return result;
    }
    const bool_t bTrimWhitespace =
        ( flags & STRING_SPLIT_FLAG_TRIM_WHITESPACE ) != 0u;

    const bool_t bSkipEmpty =
        ( flags & STRING_SPLIT_FLAG_SKIP_EMPTY ) != 0u;

    usize iTokenStart = 0u;

    for ( usize iBoundary = 0u;
          iBoundary <= source.cchLength;
          ++iBoundary ) {
        const bool_t bReachedEnd =
            iBoundary == source.cchLength;

        const bool_t bFoundDelimiter =
            !bReachedEnd &&
            CharacterSet_Contains(
                pDelimiters,
                source.pData[iBoundary] );

        if ( !bReachedEnd && !bFoundDelimiter ) {
            continue;
        }

        string_view_t token{};
        token.pData = source.pData != nullptr
            ? source.pData + iTokenStart
            : nullptr;
        token.cchLength = iBoundary - iTokenStart;

        if ( bTrimWhitespace ) {
            token = StringView_Trim( token );
        }

        const bool_t bDiscardToken =
            bSkipEmpty && StringView_IsEmpty( token );

        if ( !bDiscardToken ) {
            ++result.cTokensRequired;

            if ( result.cTokensWritten < cTokenCapacity ) {
                pTokens[result.cTokensWritten] = token;
                ++result.cTokensWritten;
            }
        }

        if ( bReachedEnd ) {
            break;
        }

        iTokenStart = iBoundary + 1u;
    }

    return result;
}

string_split_result_t StringSplit_ByString(
    string_view_t source,
    string_view_t delimiter,
    flags32_t flags,
    string_view_t *pTokens,
    usize cTokenCapacity ) noexcept
{
    string_split_result_t result{};

    const bool_t bValidSource = StringView_IsValid( source );
    const bool_t bValidDelimiter =
        StringView_IsValid( delimiter ) &&
        !StringView_IsEmpty( delimiter );
    const bool_t bValidFlags = ( flags & ~STRING_SPLIT_VALID_FLAGS ) == 0u;
    const bool_t bValidOutput = pTokens != nullptr || cTokenCapacity == 0u;

    CY_ASSERT_MSG( bValidSource, "StringSplit_ByString requires a valid source view." );
    CY_ASSERT_MSG( bValidDelimiter, "StringSplit_ByString requires a valid non-empty delimiter." );
    CY_ASSERT_MSG( bValidFlags, "StringSplit_ByString received unsupported flags." );
    CY_ASSERT_MSG( bValidOutput, "StringSplit_ByString requires output storage for non-zero capacity." );

    if ( !bValidSource || !bValidDelimiter || !bValidFlags || !bValidOutput ) {
        return result;
    }

    const bool_t bTrimWhitespace =
        ( flags & STRING_SPLIT_FLAG_TRIM_WHITESPACE ) != 0u;
    const bool_t bSkipEmpty =
        ( flags & STRING_SPLIT_FLAG_SKIP_EMPTY ) != 0u;

    usize iTokenStart = 0u;

    for ( ;; ) {
        const usize iDelimiter = StringView_Find( source, delimiter, iTokenStart );
        const bool_t bFoundDelimiter = iDelimiter != CY_STRING_VIEW_NPOS;
        const usize iTokenEnd = bFoundDelimiter ? iDelimiter : source.cchLength;

        string_view_t token{};
        token.pData = source.pData != nullptr
            ? source.pData + iTokenStart
            : nullptr;
        token.cchLength = iTokenEnd - iTokenStart;

        if ( bTrimWhitespace ) {
            token = StringView_Trim( token );
        }

        const bool_t bDiscardToken =
            bSkipEmpty && StringView_IsEmpty( token );

        if ( !bDiscardToken ) {
            ++result.cTokensRequired;

            if ( result.cTokensWritten < cTokenCapacity ) {
                pTokens[result.cTokensWritten] = token;
                ++result.cTokensWritten;
            }
        }

        if ( !bFoundDelimiter ) {
            break;
        }

        iTokenStart = iDelimiter + delimiter.cchLength;
    }

    return result;
}

string_split_visit_result_t StringSplit_VisitByChar(
    string_view_t source,
    char chDelimiter,
    flags32_t flags,
    string_split_callback_t pCallback,
    void *pUserData ) noexcept
{
    string_split_visit_result_t result{};

    const bool_t bValidSource = StringView_IsValid( source );
    const bool_t bValidFlags = ( flags & ~STRING_SPLIT_VALID_FLAGS ) == 0u;
    const bool_t bValidCallback = pCallback != nullptr;

    CY_ASSERT_MSG( bValidSource, "StringSplit_VisitByChar requires a valid source view." );
    CY_ASSERT_MSG( bValidFlags, "StringSplit_VisitByChar received unsupported flags." );
    CY_ASSERT_MSG( bValidCallback, "StringSplit_VisitByChar requires a callback." );

    if ( !bValidSource || !bValidFlags || !bValidCallback ) {
        return result;
    }

    const bool_t bTrimWhitespace =
        ( flags & STRING_SPLIT_FLAG_TRIM_WHITESPACE ) != 0u;
    const bool_t bSkipEmpty =
        ( flags & STRING_SPLIT_FLAG_SKIP_EMPTY ) != 0u;

    usize iTokenStart = 0u;

    for ( usize iBoundary = 0u;
          iBoundary <= source.cchLength;
          ++iBoundary ) {
        const bool_t bReachedEnd = iBoundary == source.cchLength;
        const bool_t bFoundDelimiter =
            !bReachedEnd && source.pData[iBoundary] == chDelimiter;

        if ( !bReachedEnd && !bFoundDelimiter ) {
            continue;
        }

        string_view_t token{};
        token.pData = source.pData != nullptr
            ? source.pData + iTokenStart
            : nullptr;
        token.cchLength = iBoundary - iTokenStart;

        if ( bTrimWhitespace ) {
            token = StringView_Trim( token );
        }

        const bool_t bDiscardToken =
            bSkipEmpty && StringView_IsEmpty( token );

        if ( !bDiscardToken ) {
            const usize iToken = result.cTokensVisited;
            ++result.cTokensVisited;

            if ( !pCallback( token, iToken, pUserData ) ) {
                return result;
            }
        }

        if ( bReachedEnd ) {
            break;
        }

        iTokenStart = iBoundary + 1u;
    }

    result.bCompleted = CY_TRUE;
    return result;
}

string_split_visit_result_t StringSplit_VisitBySet(
    string_view_t source,
    const character_set_t *pDelimiters,
    flags32_t flags,
    string_split_callback_t pCallback,
    void *pUserData ) noexcept
{
    string_split_visit_result_t result{};

    const bool_t bValidSource = StringView_IsValid( source );
    const bool_t bValidDelimiters = pDelimiters != nullptr;
    const bool_t bValidFlags = ( flags & ~STRING_SPLIT_VALID_FLAGS ) == 0u;
    const bool_t bValidCallback = pCallback != nullptr;

    CY_ASSERT_MSG( bValidSource, "StringSplit_VisitBySet requires a valid source view." );
    CY_ASSERT_MSG( bValidDelimiters, "StringSplit_VisitBySet requires a non-null delimiter set." );
    CY_ASSERT_MSG( bValidFlags, "StringSplit_VisitBySet received unsupported flags." );
    CY_ASSERT_MSG( bValidCallback, "StringSplit_VisitBySet requires a callback." );

    if ( !bValidSource || !bValidDelimiters || !bValidFlags || !bValidCallback ) {
        return result;
    }

    const bool_t bTrimWhitespace =
        ( flags & STRING_SPLIT_FLAG_TRIM_WHITESPACE ) != 0u;
    const bool_t bSkipEmpty =
        ( flags & STRING_SPLIT_FLAG_SKIP_EMPTY ) != 0u;

    usize iTokenStart = 0u;

    for ( usize iBoundary = 0u;
          iBoundary <= source.cchLength;
          ++iBoundary ) {
        const bool_t bReachedEnd = iBoundary == source.cchLength;
        const bool_t bFoundDelimiter =
            !bReachedEnd &&
            CharacterSet_Contains( pDelimiters, source.pData[iBoundary] );

        if ( !bReachedEnd && !bFoundDelimiter ) {
            continue;
        }

        string_view_t token{};
        token.pData = source.pData != nullptr
            ? source.pData + iTokenStart
            : nullptr;
        token.cchLength = iBoundary - iTokenStart;

        if ( bTrimWhitespace ) {
            token = StringView_Trim( token );
        }

        const bool_t bDiscardToken =
            bSkipEmpty && StringView_IsEmpty( token );

        if ( !bDiscardToken ) {
            const usize iToken = result.cTokensVisited;
            ++result.cTokensVisited;

            if ( !pCallback( token, iToken, pUserData ) ) {
                return result;
            }
        }

        if ( bReachedEnd ) {
            break;
        }

        iTokenStart = iBoundary + 1u;
    }

    result.bCompleted = CY_TRUE;
    return result;
}

string_split_visit_result_t StringSplit_VisitByString(
    string_view_t source,
    string_view_t delimiter,
    flags32_t flags,
    string_split_callback_t pCallback,
    void *pUserData ) noexcept
{
    string_split_visit_result_t result{};

    const bool_t bValidSource = StringView_IsValid( source );
    const bool_t bValidDelimiter =
        StringView_IsValid( delimiter ) &&
        !StringView_IsEmpty( delimiter );
    const bool_t bValidFlags = ( flags & ~STRING_SPLIT_VALID_FLAGS ) == 0u;
    const bool_t bValidCallback = pCallback != nullptr;

    CY_ASSERT_MSG( bValidSource, "StringSplit_VisitByString requires a valid source view." );
    CY_ASSERT_MSG( bValidDelimiter, "StringSplit_VisitByString requires a valid non-empty delimiter." );
    CY_ASSERT_MSG( bValidFlags, "StringSplit_VisitByString received unsupported flags." );
    CY_ASSERT_MSG( bValidCallback, "StringSplit_VisitByString requires a callback." );

    if ( !bValidSource || !bValidDelimiter || !bValidFlags || !bValidCallback ) {
        return result;
    }

    const bool_t bTrimWhitespace =
        ( flags & STRING_SPLIT_FLAG_TRIM_WHITESPACE ) != 0u;
    const bool_t bSkipEmpty =
        ( flags & STRING_SPLIT_FLAG_SKIP_EMPTY ) != 0u;

    usize iTokenStart = 0u;

    for ( ;; ) {
        const usize iDelimiter = StringView_Find( source, delimiter, iTokenStart );
        const bool_t bFoundDelimiter = iDelimiter != CY_STRING_VIEW_NPOS;
        const usize iTokenEnd = bFoundDelimiter ? iDelimiter : source.cchLength;

        string_view_t token{};
        token.pData = source.pData != nullptr
            ? source.pData + iTokenStart
            : nullptr;
        token.cchLength = iTokenEnd - iTokenStart;

        if ( bTrimWhitespace ) {
            token = StringView_Trim( token );
        }

        const bool_t bDiscardToken =
            bSkipEmpty && StringView_IsEmpty( token );

        if ( !bDiscardToken ) {
            const usize iToken = result.cTokensVisited;
            ++result.cTokensVisited;

            if ( !pCallback( token, iToken, pUserData ) ) {
                return result;
            }
        }

        if ( !bFoundDelimiter ) {
            break;
        }

        iTokenStart = iDelimiter + delimiter.cchLength;
    }

    result.bCompleted = CY_TRUE;
    return result;
}

} // namespace cypher::common
