//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_FuzzyMatch.cpp
//  Purpose: Implements the editor's fuzzy matcher.
//  Details: A dynamic programme over text positions and query characters
//           finds the best-scoring way to place each query character, so
//           "gs" against "Paging Sets Grid Snap" picks the word starts of
//           "Grid Snap" rather than the first g and s it meets. Texts and
//           queries beyond the programme's fixed tables are scored by a
//           single greedy pass instead: still correct about whether they
//           match, only less exact about the ranking.
//
//  History:
//  - Created by Karlo Siric on 2026-09-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_FuzzyMatch.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"

namespace cypher::editor
{

using namespace cypher::common;

namespace
{

constexpr usize kMaxDpText = 512u;
constexpr usize kMaxDpQuery = 32u;
constexpr i32 kNegative = -1000000;
constexpr i32 kMatch = 1;
constexpr i32 kWordStart = 8;
constexpr i32 kTextStart = 10;
constexpr i32 kConsecutive = 5;
constexpr i32 kLeadingGapMax = 3;

CYPHER_NODISCARD char Lower( char c ) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>( c - 'A' + 'a' ) : c;
}

CYPHER_NODISCARD bool_t IsSeparator( char c ) noexcept
{
    return c == ' ' || c == '_' || c == '.' || c == '/' || c == '\\' || c == '-' || c == ':';
}

// Bonus for matching at text position i: the text's start, a word start
// after a separator, or an upper-case letter after a lower-case one.
CYPHER_NODISCARD i32 PositionBonus( string_view_t text, usize i ) noexcept
{
    if ( i == 0u ) { return kTextStart; }
    const char previous = text.pData[i - 1u];
    const char current = text.pData[i];
    if ( IsSeparator( previous ) ) { return kWordStart; }
    if ( previous >= 'a' && previous <= 'z' && current >= 'A' && current <= 'Z' ) { return kWordStart; }
    return 0;
}

CYPHER_NODISCARD i32 GreedyScore( string_view_t text, string_view_t query, u32 *pMatched ) noexcept
{
    i32 score = 0;
    usize iText = 0u;
    usize iPrevious = CY_INVALID_SIZE;
    for ( usize iQuery = 0u; iQuery < query.cchLength; ++iQuery ) {
        const char wanted = Lower( query.pData[iQuery] );
        while ( iText < text.cchLength && Lower( text.pData[iText] ) != wanted ) { ++iText; }
        if ( iText == text.cchLength ) { return EDITOR_FUZZY_NO_MATCH; }
        score += kMatch + PositionBonus( text, iText );
        if ( iPrevious != CY_INVALID_SIZE && iPrevious + 1u == iText ) { score += kConsecutive; }
        if ( pMatched != nullptr ) { pMatched[iQuery] = static_cast<u32>( iText ); }
        iPrevious = iText;
        ++iText;
    }
    return score < 0 ? 0 : score;
}

} // namespace

i32 EditorFuzzy_Score( string_view_t text, string_view_t query, u32 *pMatched ) noexcept
{
    if ( query.cchLength == 0u ) { return 0; }
    if ( query.cchLength > EDITOR_FUZZY_MAX_QUERY || text.cchLength < query.cchLength || text.pData == nullptr || query.pData == nullptr ) {
        return EDITOR_FUZZY_NO_MATCH;
    }
    if ( text.cchLength > kMaxDpText || query.cchLength > kMaxDpQuery ) { return GreedyScore( text, query, pMatched ); }

    const usize n = text.cchLength;
    const usize m = query.cchLength;
    // Two rows of scores; the parent table lets pMatched be recovered.
    i32 previousRow[kMaxDpText];
    i32 currentRow[kMaxDpText];
    i16 parents[kMaxDpQuery][kMaxDpText];
    for ( usize i = 0u; i < n; ++i ) {
        const bool_t bMatch = Lower( text.pData[i] ) == Lower( query.pData[0] );
        const i32 leadingGap = static_cast<i32>( i ) < kLeadingGapMax ? static_cast<i32>( i ) : kLeadingGapMax;
        previousRow[i] = bMatch ? kMatch + PositionBonus( text, i ) - leadingGap : kNegative;
        parents[0][i] = -1;
    }
    for ( usize j = 1u; j < m; ++j ) {
        const char wanted = Lower( query.pData[j] );
        // Best of score[k][j-1] + k over k < i; a gap of g characters costs g.
        i32 bestShifted = kNegative;
        i16 bestK = -1;
        for ( usize i = 0u; i < n; ++i ) {
            currentRow[i] = kNegative;
            parents[j][i] = -1;
            if ( i > 0u && previousRow[i - 1u] > kNegative ) {
                const i32 shifted = previousRow[i - 1u] + static_cast<i32>( i - 1u );
                if ( shifted > bestShifted ) {
                    bestShifted = shifted;
                    bestK = static_cast<i16>( i - 1u );
                }
            }
            if ( Lower( text.pData[i] ) != wanted || bestK < 0 ) { continue; }
            i32 best = bestShifted - static_cast<i32>( i - 1u ); // Gap from the best earlier match.
            i16 from = bestK;
            if ( previousRow[i - 1u] > kNegative && previousRow[i - 1u] + kConsecutive > best ) {
                best = previousRow[i - 1u] + kConsecutive;
                from = static_cast<i16>( i - 1u );
            }
            currentRow[i] = kMatch + PositionBonus( text, i ) + best;
            parents[j][i] = from;
        }
        for ( usize i = 0u; i < n; ++i ) { previousRow[i] = currentRow[i]; }
    }
    i32 best = kNegative;
    usize iBest = 0u;
    for ( usize i = 0u; i < n; ++i ) {
        if ( previousRow[i] > best ) {
            best = previousRow[i];
            iBest = i;
        }
    }
    if ( best <= kNegative ) { return EDITOR_FUZZY_NO_MATCH; }
    if ( pMatched != nullptr ) {
        usize i = iBest;
        for ( usize j = m; j-- > 0u; ) {
            pMatched[j] = static_cast<u32>( i );
            CY_ASSERT( j == 0u || parents[j][i] >= 0 );
            if ( j != 0u ) { i = static_cast<usize>( parents[j][i] ); }
        }
    }
    // Scores stay non-negative so callers can sort without special cases.
    return best < 0 ? 0 : best;
}

} // namespace cypher::editor
