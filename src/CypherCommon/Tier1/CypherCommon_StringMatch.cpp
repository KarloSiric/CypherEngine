//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringMatch.cpp
//  Purpose: Implements bounded literal and wildcard string matching.
//  Details: Matching is allocation-free, byte-oriented, and iterative. ASCII case
//           and path-separator policies remain explicit at every public boundary.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringMatch.h"

#include "CypherCommon_Char.h"

namespace cypher::common
{

namespace
{

struct character_class_result_t {
    bool_t bValid{ CY_FALSE };
    bool_t bMatched{ CY_FALSE };
    usize iNextPattern{ 0u };
};

bool_t MatchFlagsAreValid( flags32_t flags ) noexcept
{
    return ( flags & ~STRING_MATCH_VALID_FLAGS ) == 0u;
}

bool_t MatchViewsAreValid( string_view_t viewA, string_view_t viewB ) noexcept
{
    return StringView_IsValid( viewA ) && StringView_IsValid( viewB );
}

bool_t IsPathSeparator( char ch ) noexcept
{
    return ch == '/' || ch == '\\';
}

char FoldMatchByte( char ch, flags32_t flags ) noexcept
{
    if ( ( flags & STRING_MATCH_FLAG_PATH_SEPARATORS_EQUAL ) != 0u &&
         IsPathSeparator( ch ) ) {
        return '/';
    }
    if ( ( flags & STRING_MATCH_FLAG_CASE_INSENSITIVE_ASCII ) != 0u ) {
        return Char_ToLowerAscii( ch );
    }
    return ch;
}

bool_t MatchByteEquals( char left, char right, flags32_t flags ) noexcept
{
    return FoldMatchByte( left, flags ) == FoldMatchByte( right, flags );
}

bool_t WildcardCanConsume( char ch, flags32_t flags ) noexcept
{
    return ( flags & STRING_MATCH_FLAG_STAR_MATCHES_SEPARATOR ) != 0u ||
           !IsPathSeparator( ch );
}

character_class_result_t MatchCharacterClass(
    string_view_t pattern,
    usize iOpenBracket,
    char chText,
    flags32_t flags ) noexcept
{
    character_class_result_t result{};
    usize iCursor = iOpenBracket + 1u;
    if ( iCursor >= pattern.cchLength ) {
        return result;
    }

    bool_t bNegated = CY_FALSE;
    if ( pattern.pData[iCursor] == '!' || pattern.pData[iCursor] == '^' ) {
        bNegated = CY_TRUE;
        ++iCursor;
    }
    if ( iCursor >= pattern.cchLength || pattern.pData[iCursor] == ']' ) {
        return result;
    }

    const char chFoldedText = FoldMatchByte( chText, flags );
    bool_t bMatched = CY_FALSE;
    bool_t bHasMember = CY_FALSE;

    while ( iCursor < pattern.cchLength && pattern.pData[iCursor] != ']' ) {
        const char chRangeFirst = FoldMatchByte( pattern.pData[iCursor], flags );
        bHasMember = CY_TRUE;

        if ( iCursor + 2u < pattern.cchLength &&
             pattern.pData[iCursor + 1u] == '-' &&
             pattern.pData[iCursor + 2u] != ']' ) {
            const char chRangeLast = FoldMatchByte( pattern.pData[iCursor + 2u], flags );
            const u8 nFirst = static_cast<u8>( chRangeFirst );
            const u8 nLast = static_cast<u8>( chRangeLast );
            const u8 nText = static_cast<u8>( chFoldedText );
            if ( nFirst > nLast ) {
                return result;
            }
            if ( nText >= nFirst && nText <= nLast ) {
                bMatched = CY_TRUE;
            }
            iCursor += 3u;
            continue;
        }

        if ( chFoldedText == chRangeFirst ) {
            bMatched = CY_TRUE;
        }
        ++iCursor;
    }

    if ( !bHasMember || iCursor >= pattern.cchLength ||
         pattern.pData[iCursor] != ']' ) {
        return result;
    }

    result.bValid = CY_TRUE;
    result.bMatched = bNegated ? !bMatched : bMatched;
    result.iNextPattern = iCursor + 1u;
    return result;
}

} // namespace

bool_t StringMatch_Equals(
    string_view_t text,
    string_view_t expected,
    flags32_t flags ) noexcept
{
    if ( !MatchViewsAreValid( text, expected ) || !MatchFlagsAreValid( flags ) ||
         text.cchLength != expected.cchLength ) {
        return CY_FALSE;
    }

    for ( usize iByte = 0u; iByte < text.cchLength; ++iByte ) {
        if ( !MatchByteEquals( text.pData[iByte], expected.pData[iByte], flags ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t StringMatch_StartsWith(
    string_view_t text,
    string_view_t prefix,
    flags32_t flags ) noexcept
{
    if ( !MatchViewsAreValid( text, prefix ) || !MatchFlagsAreValid( flags ) ||
         prefix.cchLength > text.cchLength ) {
        return CY_FALSE;
    }

    for ( usize iByte = 0u; iByte < prefix.cchLength; ++iByte ) {
        if ( !MatchByteEquals( text.pData[iByte], prefix.pData[iByte], flags ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t StringMatch_EndsWith(
    string_view_t text,
    string_view_t suffix,
    flags32_t flags ) noexcept
{
    if ( !MatchViewsAreValid( text, suffix ) || !MatchFlagsAreValid( flags ) ||
         suffix.cchLength > text.cchLength ) {
        return CY_FALSE;
    }

    const usize iStart = text.cchLength - suffix.cchLength;
    for ( usize iByte = 0u; iByte < suffix.cchLength; ++iByte ) {
        if ( !MatchByteEquals( text.pData[iStart + iByte], suffix.pData[iByte], flags ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

bool_t StringMatch_Contains(
    string_view_t text,
    string_view_t search,
    flags32_t flags ) noexcept
{
    if ( !MatchViewsAreValid( text, search ) || !MatchFlagsAreValid( flags ) ||
         search.cchLength > text.cchLength ) {
        return CY_FALSE;
    }
    if ( search.cchLength == 0u ) {
        return CY_TRUE;
    }

    const usize iLastStart = text.cchLength - search.cchLength;
    for ( usize iStart = 0u; iStart <= iLastStart; ++iStart ) {
        usize iSearch = 0u;
        while ( iSearch < search.cchLength &&
                MatchByteEquals( text.pData[iStart + iSearch], search.pData[iSearch], flags ) ) {
            ++iSearch;
        }
        if ( iSearch == search.cchLength ) {
            return CY_TRUE;
        }
    }
    return CY_FALSE;
}

bool_t StringMatch_Wildcard(
    string_view_t text,
    string_view_t pattern,
    flags32_t flags ) noexcept
{
    if ( !MatchViewsAreValid( text, pattern ) || !MatchFlagsAreValid( flags ) ) {
        return CY_FALSE;
    }

    usize iText = 0u;
    usize iPattern = 0u;
    usize iStarPattern = CY_STRING_VIEW_NPOS;
    usize iStarText = CY_STRING_VIEW_NPOS;

    while ( iText < text.cchLength ) {
        if ( iPattern < pattern.cchLength && pattern.pData[iPattern] == '*' ) {
            while ( iPattern < pattern.cchLength && pattern.pData[iPattern] == '*' ) {
                ++iPattern;
            }
            iStarPattern = iPattern;
            iStarText = iText;
            continue;
        }

        bool_t bMatchedUnit = CY_FALSE;
        usize iNextPattern = iPattern;
        if ( iPattern < pattern.cchLength ) {
            const char chPattern = pattern.pData[iPattern];
            if ( chPattern == '?' ) {
                bMatchedUnit = WildcardCanConsume( text.pData[iText], flags );
                iNextPattern = iPattern + 1u;
            } else if ( chPattern == '[' &&
                        ( flags & STRING_MATCH_FLAG_ALLOW_CHARACTER_CLASS ) != 0u ) {
                if ( WildcardCanConsume( text.pData[iText], flags ) ) {
                    const character_class_result_t classResult = MatchCharacterClass(
                        pattern,
                        iPattern,
                        text.pData[iText],
                        flags );
                    if ( !classResult.bValid ) {
                        return CY_FALSE;
                    }
                    bMatchedUnit = classResult.bMatched;
                    iNextPattern = classResult.iNextPattern;
                }
            } else {
                bMatchedUnit = MatchByteEquals( text.pData[iText], chPattern, flags );
                iNextPattern = iPattern + 1u;
            }
        }

        if ( bMatchedUnit ) {
            ++iText;
            iPattern = iNextPattern;
            continue;
        }

        if ( iStarPattern == CY_STRING_VIEW_NPOS ||
             iStarText >= text.cchLength ||
             !WildcardCanConsume( text.pData[iStarText], flags ) ) {
            return CY_FALSE;
        }

        ++iStarText;
        iText = iStarText;
        iPattern = iStarPattern;
    }

    while ( iPattern < pattern.cchLength && pattern.pData[iPattern] == '*' ) {
        ++iPattern;
    }
    return iPattern == pattern.cchLength;
}

} // namespace cypher::common
