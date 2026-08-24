//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_PathMatch.cpp
//  Purpose: Implements allocation-free path filtering.
//  Details: Path policy is translated into StringMatch flags, keeping include and
//           exclude resolution deterministic for VFS and editor asset queries.
//
//  History:
//  - Created by Karlo Siric on 2026-08-10
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Path Match Implementation Notes

This dependency-light Tier1 utility keeps ownership, capacity, and failure behavior explicit so
higher engine systems can use it without hidden allocation or platform state.
================
*/

#include "CypherCommon_PathMatch.h"

#include "CypherCommon_StringPath.h"

namespace cypher::common
{

namespace
{

bool_t PathMatchFlagsAreValid( flags32_t flags ) noexcept
{
    return ( flags & ~PATH_MATCH_VALID_FLAGS ) == 0u;
}

flags32_t ToStringMatchFlags( flags32_t flags ) noexcept
{
    flags32_t matchFlags = STRING_MATCH_FLAG_ALLOW_CHARACTER_CLASS;
    if ( ( flags & PATH_MATCH_FLAG_CASE_INSENSITIVE_ASCII ) != 0u ) {
        matchFlags |= STRING_MATCH_FLAG_CASE_INSENSITIVE_ASCII;
    }
    if ( ( flags & PATH_MATCH_FLAG_NORMALIZE_SEPARATORS ) != 0u ) {
        matchFlags |= STRING_MATCH_FLAG_PATH_SEPARATORS_EQUAL;
    }
    if ( ( flags & PATH_MATCH_FLAG_STAR_CROSSES_SEPARATOR ) != 0u ) {
        matchFlags |= STRING_MATCH_FLAG_STAR_MATCHES_SEPARATOR;
    }
    return matchFlags;
}

} // namespace

bool_t PathMatch_Wildcard(
    string_view_t path,
    string_view_t pattern,
    flags32_t flags ) noexcept
{
    if ( !StringView_IsValid( path ) || !StringView_IsValid( pattern ) ||
         !PathMatchFlagsAreValid( flags ) ) {
        return CY_FALSE;
    }

    const string_view_t candidate =
        ( flags & PATH_MATCH_FLAG_BASENAME_ONLY ) != 0u
            ? StringPath_FileName( path )
            : path;
    return StringMatch_Wildcard(
        candidate,
        pattern,
        ToStringMatchFlags( flags ) );
}

bool_t PathMatch_Filter(
    string_view_t path,
    const path_filter_t &filter ) noexcept
{
    if ( !StringView_IsValid( path ) || !PathMatchFlagsAreValid( filter.flags ) ||
         ( filter.pIncludes == nullptr && filter.nIncludeCount != 0u ) ||
         ( filter.pExcludes == nullptr && filter.nExcludeCount != 0u ) ) {
        return CY_FALSE;
    }

    // An empty include list means "include all"; exclusions always win afterward.
    bool_t bIncluded = filter.nIncludeCount == 0u;
    for ( usize iPattern = 0u; iPattern < filter.nIncludeCount; ++iPattern ) {
        if ( PathMatch_Wildcard( path, filter.pIncludes[iPattern], filter.flags ) ) {
            bIncluded = CY_TRUE;
            break;
        }
    }
    if ( !bIncluded ) {
        return CY_FALSE;
    }

    for ( usize iPattern = 0u; iPattern < filter.nExcludeCount; ++iPattern ) {
        if ( PathMatch_Wildcard( path, filter.pExcludes[iPattern], filter.flags ) ) {
            return CY_FALSE;
        }
    }
    return CY_TRUE;
}

} // namespace cypher::common
