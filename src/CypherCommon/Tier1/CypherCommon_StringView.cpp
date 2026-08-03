//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_StringView.cpp
//  Purpose: Implements CypherCommon Tier1 StringView support.
//  Details: String views provide allocation-free, non-owning ranges over character
//           storage. Construction preserves source addresses and never assumes
//           range-created views are null-terminated.
//
//  History:
//  - Created by Karlo Siric on 2026-07-29
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_StringView.h"
#include "CypherCommon_Char.h"
#include "CypherCommon_String.h"
#include "CypherCommon_Assert.h"
#include "CypherCommon_MemoryOps.h"

namespace cypher::common
{

string_view_t StringView_FromCString( const char *pString ) noexcept
{
    if ( pString == nullptr ) {
        return string_view_t{};
    }

    string_view_t view{};
    view.pData = pString;
    view.cchLength = Cy_strlen( pString );

    return view;
}

string_view_t StringView_FromRange( const char *pData, usize cchLength ) noexcept
{
    string_view_t view{};
    if ( pData == nullptr ) {
        // A null pointer can represent only an empty range.
        CY_ASSERT_MSG( cchLength == 0u, "StringView_FromRange requires a non-null data for a non-empty range." );
        return view;
    }

    view.pData = pData;
    view.cchLength = cchLength;
    return view;
}

bool_t StringView_IsEmpty( string_view_t view ) noexcept
{
    return ( view.cchLength == 0u );
}

bool_t StringView_IsValid( string_view_t view ) noexcept
{
    if ( view.pData == nullptr && view.cchLength != 0u ) {
        return CY_FALSE;
    }

    return CY_TRUE;
}

usize StringView_Length( string_view_t view ) noexcept
{
    return ( view.cchLength );
}

const char *StringView_Begin( string_view_t view ) noexcept
{
    if ( view.pData == nullptr ) {
        CY_ASSERT_MSG( view.cchLength == 0u, "StringView_Begin requires a non-null data for a non-empty range." );
        return nullptr;
    }
    
    // we return where things begin.   
    return view.pData;
}

const char *StringView_End( string_view_t view ) noexcept
{
    if ( view.pData == nullptr ) {
        CY_ASSERT_MSG( view.cchLength == 0u, "StringView_End requires a non-null data for a non-empty range." );
        return nullptr;
    }
    
    // we return where things end.
    return ( view.pData + view.cchLength );
}

char StringView_At( string_view_t view, usize iIndex ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG( bValid, "StringView_At requires a valid string view." );
    if ( !bValid ) {
        return '\0';
    }
    
    const bool_t bInBounds = ( iIndex < view.cchLength );
    CY_ASSERT_MSG( bInBounds, "StringView_At index is outside the view range." );
    if ( !bInBounds ) {
        return '\0';
    } 
    
    return ( view.pData[iIndex] );
}

char StringView_Front( string_view_t view ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG( bValid, "StringView_Front requires a valid string view." );

    if ( !bValid ) {
        return '\0';
    }

    const bool_t bNotEmpty = ( view.cchLength > 0u );
    CY_ASSERT_MSG( bNotEmpty, "StringView_Front requires a non-empty string view." );

    if ( !bNotEmpty ) {
        return '\0';
    }

    return view.pData[0];
}

char StringView_Back( string_view_t view ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG(
        bValid,
        "StringView_Back requires a valid string view." );

    if ( !bValid ) {
        return '\0';
    }

    const bool_t bNotEmpty = ( view.cchLength > 0u );
    CY_ASSERT_MSG(
        bNotEmpty,
        "StringView_Back requires a non-empty string view." );

    if ( !bNotEmpty ) {
        return '\0';
    }

    return view.pData[view.cchLength - 1u];
}

i32 StringView_Compare( string_view_t viewA, string_view_t viewB ) noexcept
{
    const bool_t bValidA = StringView_IsValid( viewA );
    const bool_t bValidB = StringView_IsValid( viewB );

    CY_ASSERT_MSG(
        bValidA,
        "StringView_Compare requires a valid first string view." );
    CY_ASSERT_MSG(
        bValidB,
        "StringView_Compare requires a valid second string view." );

    // Preserve deterministic and memory-safe Release behavior.
    if ( !bValidA ) {
        viewA = {};
    }
    if ( !bValidB ) {
        viewB = {};
    }

    // Identical ranges are immediately equal.
    if ( viewA.pData == viewB.pData &&
         viewA.cchLength == viewB.cchLength ) {
        return 0;
    }

    const usize cchCompare =
        viewA.cchLength < viewB.cchLength
            ? viewA.cchLength
            : viewB.cchLength;

    const i32 nResult =
        Cy_MemCompare( viewA.pData, viewB.pData, cchCompare );

    if ( nResult != 0 ) {
        return nResult;
    }

    // The common prefix matched, so the shorter view sorts first.
    if ( viewA.cchLength < viewB.cchLength ) {
        return -1;
    }
    if ( viewA.cchLength > viewB.cchLength ) {
        return 1;
    }

    return 0;
}

i32 StringView_CompareInsensitiveAscii(
    string_view_t viewA,
    string_view_t viewB ) noexcept
{
    const bool_t bValidA = StringView_IsValid( viewA );
    const bool_t bValidB = StringView_IsValid( viewB );

    CY_ASSERT_MSG(
        bValidA,
        "StringView_CompareInsensitiveAscii requires a valid first view." );
    CY_ASSERT_MSG(
        bValidB,
        "StringView_CompareInsensitiveAscii requires a valid second view." );

    if ( !bValidA ) {
        viewA = {};
    }
    if ( !bValidB ) {
        viewB = {};
    }

    if ( viewA.pData == viewB.pData &&
         viewA.cchLength == viewB.cchLength ) {
        return 0;
    }

    const usize cchCompare =
        viewA.cchLength < viewB.cchLength
            ? viewA.cchLength
            : viewB.cchLength;

    for ( usize iIndex = 0u; iIndex < cchCompare; ++iIndex ) {
        const u8 chA = static_cast<u8>(
            Char_ToLowerAscii( viewA.pData[iIndex] ) );
        const u8 chB = static_cast<u8>(
            Char_ToLowerAscii( viewB.pData[iIndex] ) );

        if ( chA != chB ) {
            return static_cast<i32>( chA ) -
                   static_cast<i32>( chB );
        }
    }

    if ( viewA.cchLength < viewB.cchLength ) {
        return -1;
    }
    if ( viewA.cchLength > viewB.cchLength ) {
        return 1;
    }

    return 0;
}

bool_t StringView_Equals(
    string_view_t viewA,
    string_view_t viewB ) noexcept
{
    const bool_t bValidA = StringView_IsValid( viewA );
    const bool_t bValidB = StringView_IsValid( viewB );

    CY_ASSERT_MSG(
        bValidA,
        "StringView_Equals requires a valid first view." );
    CY_ASSERT_MSG(
        bValidB,
        "StringView_Equals requires a valid second view." );

    if ( !bValidA ) {
        viewA = {};
    }
    if ( !bValidB ) {
        viewB = {};
    }

    // Equal strings must have equal lengths.
    if ( viewA.cchLength != viewB.cchLength ) {
        return CY_FALSE;
    }

    // Every zero-length view represents the same empty string.
    if ( viewA.cchLength == 0u ) {
        return CY_TRUE;
    }

    // Equal pointers and equal lengths represent the same range.
    if ( viewA.pData == viewB.pData ) {
        return CY_TRUE;
    }

    return Cy_MemEqual(
        viewA.pData,
        viewB.pData,
        viewA.cchLength );
}

bool_t StringView_EqualsInsensitiveAscii(
    string_view_t viewA,
    string_view_t viewB ) noexcept
{
    const bool_t bValidA = StringView_IsValid( viewA );
    const bool_t bValidB = StringView_IsValid( viewB );

    CY_ASSERT_MSG(
        bValidA,
        "StringView_EqualsInsensitiveAscii requires a valid first view." );
    CY_ASSERT_MSG(
        bValidB,
        "StringView_EqualsInsensitiveAscii requires a valid second view." );

    if ( !bValidA ) {
        viewA = {};
    }
    if ( !bValidB ) {
        viewB = {};
    }

    if ( viewA.cchLength != viewB.cchLength ) {
        return CY_FALSE;
    }

    if ( viewA.cchLength == 0u ) {
        return CY_TRUE;
    }

    if ( viewA.pData == viewB.pData ) {
        return CY_TRUE;
    }

    for ( usize iIndex = 0u;
          iIndex < viewA.cchLength;
          ++iIndex ) {
        const char chA =
            Char_ToLowerAscii( viewA.pData[iIndex] );
        const char chB =
            Char_ToLowerAscii( viewB.pData[iIndex] );

        if ( chA != chB ) {
            return CY_FALSE;
        }
    }

    return CY_TRUE;
}

bool_t StringView_StartsWith( string_view_t view, string_view_t prefix ) noexcept
{
    const bool_t bValidView = StringView_IsValid( view );
    const bool_t bValidPrefix = StringView_IsValid( prefix );

    CY_ASSERT_MSG(
        bValidView,
        "StringView_StartsWith requires a valid source view." );
    CY_ASSERT_MSG(
        bValidPrefix,
        "StringView_StartsWith requires a valid prefix view." );

    if ( !bValidView ) {
        view = {};
    }
    if ( !bValidPrefix ) {
        prefix = {};
    }

    if ( prefix.cchLength > view.cchLength ) {
        return CY_FALSE;
    }
    if ( prefix.cchLength == 0u ) {
        return CY_TRUE;
    }

    if ( view.pData == prefix.pData ) {
        return CY_TRUE;
    }

    return Cy_MemEqual( view.pData, prefix.pData, prefix.cchLength );
}

bool_t StringView_StartsWithInsensitiveAscii(
    string_view_t view,
    string_view_t prefix ) noexcept
{
    const bool_t bValidView = StringView_IsValid( view );
    const bool_t bValidPrefix = StringView_IsValid( prefix );

    CY_ASSERT_MSG(
        bValidView,
        "StringView_StartsWithInsensitiveAscii requires a valid source view." );
    CY_ASSERT_MSG(
        bValidPrefix,
        "StringView_StartsWithInsensitiveAscii requires a valid prefix view." );

    if ( !bValidView ) {
        view = {};
    }
    if ( !bValidPrefix ) {
        prefix = {};
    }
    if ( prefix.cchLength > view.cchLength ) {
        return CY_FALSE;
    }
    if ( prefix.cchLength == 0u ) {
        return CY_TRUE;
    }

    if ( view.pData == prefix.pData ) {
        return CY_TRUE;
    }

    for ( usize iIndex = 0u;
          iIndex < prefix.cchLength;
          ++iIndex ) {
        const char chView =
            Char_ToLowerAscii( view.pData[iIndex] );
        const char chPrefix =
            Char_ToLowerAscii( prefix.pData[iIndex] );

        if ( chView != chPrefix ) {
            return CY_FALSE;
        }
    }

    return CY_TRUE;
}

bool_t StringView_EndsWith( string_view_t view, string_view_t suffix ) noexcept
{
    const bool_t bValidView = StringView_IsValid( view );
    const bool_t bValidSuffix = StringView_IsValid( suffix );

    CY_ASSERT_MSG(
        bValidView,
        "StringView_EndsWith requires a valid source view." );
    CY_ASSERT_MSG(
        bValidSuffix,
        "StringView_EndsWith requires a valid suffix view." );

    if ( !bValidView ) {
        view = {};
    }
    if ( !bValidSuffix ) {
        suffix = {};
    }

    if ( suffix.cchLength > view.cchLength ) {
        return CY_FALSE;
    }
    if ( suffix.cchLength == 0u ) {
        return CY_TRUE;
    }

    const usize iSuffix = view.cchLength - suffix.cchLength;
    const char *pViewSuffix = view.pData + iSuffix;
    if ( pViewSuffix == suffix.pData ) {
        return CY_TRUE;
    }

    return Cy_MemEqual(
        pViewSuffix,
        suffix.pData,
        suffix.cchLength );
}

bool_t StringView_EndsWithInsensitiveAscii(
    string_view_t view,
    string_view_t suffix ) noexcept
{
    const bool_t bValidView = StringView_IsValid( view );
    const bool_t bValidSuffix = StringView_IsValid( suffix );

    CY_ASSERT_MSG(
        bValidView,
        "StringView_EndsWithInsensitiveAscii requires a valid source view." );
    CY_ASSERT_MSG(
        bValidSuffix,
        "StringView_EndsWithInsensitiveAscii requires a valid suffix view." );

    if ( !bValidView ) {
        view = {};
    }
    if ( !bValidSuffix ) {
        suffix = {};
    }

    if ( suffix.cchLength > view.cchLength ) {
        return CY_FALSE;
    }
    if ( suffix.cchLength == 0u ) {
        return CY_TRUE;
    }

    const usize iSuffix = view.cchLength - suffix.cchLength;
    if ( view.pData + iSuffix == suffix.pData ) {
        return CY_TRUE;
    }

    for ( usize iIndex = 0u; iIndex < suffix.cchLength; ++iIndex ) {
        const char chView =
            Char_ToLowerAscii( view.pData[iSuffix + iIndex] );
        const char chSuffix =
            Char_ToLowerAscii( suffix.pData[iIndex] );

        if ( chView != chSuffix ) {
            return CY_FALSE;
        }
    }

    return CY_TRUE;
}

string_view_t StringView_Subview(
    string_view_t view,
    usize iStart,
    usize cchLength ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG(
        bValid,
        "StringView_Subview requires a valid source view." );

    if ( !bValid ) {
        return {};
    }

    const bool_t bStartInRange = ( iStart <= view.cchLength );
    CY_ASSERT_MSG(
        bStartInRange,
        "StringView_Subview start index is outside the source range." );

    if ( !bStartInRange ) {
        iStart = view.cchLength;
    }

    if ( view.pData == nullptr ) {
        return {};
    }

    const usize cchAvailable = view.cchLength - iStart;
    const usize cchSubview =
        cchLength < cchAvailable ? cchLength : cchAvailable;

    return string_view_t{ view.pData + iStart, cchSubview };
}

string_view_t StringView_Prefix(
    string_view_t view,
    usize cchLength ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG(
        bValid,
        "StringView_Prefix requires a valid source view." );

    if ( !bValid ) {
        return {};
    }

    const usize cchPrefix =
        cchLength < view.cchLength ? cchLength : view.cchLength;
    return string_view_t{ view.pData, cchPrefix };
}

string_view_t StringView_Suffix(
    string_view_t view,
    usize cchLength ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG(
        bValid,
        "StringView_Suffix requires a valid source view." );

    if ( !bValid ) {
        return {};
    }
    if ( view.pData == nullptr ) {
        return {};
    }

    const usize cchSuffix =
        cchLength < view.cchLength ? cchLength : view.cchLength;
    const usize iSuffix = view.cchLength - cchSuffix;
    return string_view_t{ view.pData + iSuffix, cchSuffix };
}

string_view_t StringView_RemovePrefix(
    string_view_t view,
    usize cchLength ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG(
        bValid,
        "StringView_RemovePrefix requires a valid source view." );

    if ( !bValid ) {
        return {};
    }
    if ( view.pData == nullptr ) {
        return {};
    }

    const usize cchRemove =
        cchLength < view.cchLength ? cchLength : view.cchLength;
    return string_view_t{
        view.pData + cchRemove,
        view.cchLength - cchRemove
    };
}

string_view_t StringView_RemoveSuffix(
    string_view_t view,
    usize cchLength ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG(
        bValid,
        "StringView_RemoveSuffix requires a valid source view." );

    if ( !bValid ) {
        return {};
    }

    const usize cchRemove =
        cchLength < view.cchLength ? cchLength : view.cchLength;
    return string_view_t{
        view.pData,
        view.cchLength - cchRemove
    };
}

usize StringView_FindChar(
    string_view_t view,
    char chFind,
    usize iStart ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG(
        bValid,
        "StringView_FindChar requires a valid source view." );

    if ( !bValid || iStart >= view.cchLength ) {
        return CY_STRING_VIEW_NPOS;
    }

    for ( usize iIndex = iStart; iIndex < view.cchLength; ++iIndex ) {
        if ( view.pData[iIndex] == chFind ) {
            return iIndex;
        }
    }

    return CY_STRING_VIEW_NPOS;
}

usize StringView_FindLastChar( string_view_t view, char chFind ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG(
        bValid,
        "StringView_FindLastChar requires a valid source view." );

    if ( !bValid ) {
        return CY_STRING_VIEW_NPOS;
    }

    for ( usize iRemaining = view.cchLength;
          iRemaining > 0u;
          --iRemaining ) {
        const usize iIndex = iRemaining - 1u;
        if ( view.pData[iIndex] == chFind ) {
            return iIndex;
        }
    }

    return CY_STRING_VIEW_NPOS;
}

usize StringView_Find(
    string_view_t view,
    string_view_t search,
    usize iStart ) noexcept
{
    const bool_t bValidView = StringView_IsValid( view );
    const bool_t bValidSearch = StringView_IsValid( search );

    CY_ASSERT_MSG(
        bValidView,
        "StringView_Find requires a valid source view." );
    CY_ASSERT_MSG(
        bValidSearch,
        "StringView_Find requires a valid search view." );

    if ( !bValidView ) {
        view = {};
    }
    if ( !bValidSearch ) {
        search = {};
    }

    if ( iStart > view.cchLength ) {
        return CY_STRING_VIEW_NPOS;
    }
    if ( search.cchLength == 0u ) {
        return iStart;
    }
    if ( search.cchLength > view.cchLength - iStart ) {
        return CY_STRING_VIEW_NPOS;
    }

    const usize iLastStart = view.cchLength - search.cchLength;
    for ( usize iIndex = iStart; iIndex <= iLastStart; ++iIndex ) {
        if ( view.pData[iIndex] == search.pData[0] &&
             Cy_MemEqual(
                 view.pData + iIndex,
                 search.pData,
                 search.cchLength ) ) {
            return iIndex;
        }
    }

    return CY_STRING_VIEW_NPOS;
}

usize StringView_FindInsensitiveAscii(
    string_view_t view,
    string_view_t search,
    usize iStart ) noexcept
{
    const bool_t bValidView = StringView_IsValid( view );
    const bool_t bValidSearch = StringView_IsValid( search );

    CY_ASSERT_MSG(
        bValidView,
        "StringView_FindInsensitiveAscii requires a valid source view." );
    CY_ASSERT_MSG(
        bValidSearch,
        "StringView_FindInsensitiveAscii requires a valid search view." );

    if ( !bValidView ) {
        view = {};
    }
    if ( !bValidSearch ) {
        search = {};
    }

    if ( iStart > view.cchLength ) {
        return CY_STRING_VIEW_NPOS;
    }
    if ( search.cchLength == 0u ) {
        return iStart;
    }
    if ( search.cchLength > view.cchLength - iStart ) {
        return CY_STRING_VIEW_NPOS;
    }

    const usize iLastStart = view.cchLength - search.cchLength;
    const char chSearchFirst = Char_ToLowerAscii( search.pData[0] );

    for ( usize iIndex = iStart; iIndex <= iLastStart; ++iIndex ) {
        if ( Char_ToLowerAscii( view.pData[iIndex] ) != chSearchFirst ) {
            continue;
        }

        bool_t bMatch = CY_TRUE;
        for ( usize iSearch = 1u;
              iSearch < search.cchLength;
              ++iSearch ) {
            const char chView =
                Char_ToLowerAscii( view.pData[iIndex + iSearch] );
            const char chSearch =
                Char_ToLowerAscii( search.pData[iSearch] );
            if ( chView != chSearch ) {
                bMatch = CY_FALSE;
                break;
            }
        }

        if ( bMatch ) {
            return iIndex;
        }
    }

    return CY_STRING_VIEW_NPOS;
}

bool_t StringView_Contains(
    string_view_t view,
    string_view_t search ) noexcept
{
    return StringView_Find( view, search ) != CY_STRING_VIEW_NPOS;
}

bool_t StringView_ContainsInsensitiveAscii(
    string_view_t view,
    string_view_t search ) noexcept
{
    return StringView_FindInsensitiveAscii( view, search ) !=
           CY_STRING_VIEW_NPOS;
}

string_view_t StringView_TrimLeft( string_view_t view ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG(
        bValid,
        "StringView_TrimLeft requires a valid source view." );

    if ( !bValid || view.pData == nullptr ) {
        return {};
    }

    usize iStart = 0u;
    while ( iStart < view.cchLength &&
            Char_IsWhitespaceAscii( view.pData[iStart] ) ) {
        ++iStart;
    }

    return string_view_t{
        view.pData + iStart,
        view.cchLength - iStart
    };
}

string_view_t StringView_TrimRight( string_view_t view ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG(
        bValid,
        "StringView_TrimRight requires a valid source view." );

    if ( !bValid ) {
        return {};
    }

    usize iEnd = view.cchLength;
    while ( iEnd > 0u &&
            Char_IsWhitespaceAscii( view.pData[iEnd - 1u] ) ) {
        --iEnd;
    }

    return string_view_t{ view.pData, iEnd };
}

string_view_t StringView_Trim( string_view_t view ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG(
        bValid,
        "StringView_Trim requires a valid source view." );

    if ( !bValid || view.pData == nullptr ) {
        return {};
    }

    usize iStart = 0u;
    while ( iStart < view.cchLength &&
            Char_IsWhitespaceAscii( view.pData[iStart] ) ) {
        ++iStart;
    }

    usize iEnd = view.cchLength;
    while ( iEnd > iStart &&
            Char_IsWhitespaceAscii( view.pData[iEnd - 1u] ) ) {
        --iEnd;
    }

    return string_view_t{ view.pData + iStart, iEnd - iStart };
}

usize StringView_CopyToCString(
    string_view_t view,
    char *pDest,
    usize cchDest ) noexcept
{
    const bool_t bValidView = StringView_IsValid( view );
    CY_ASSERT_MSG(
        bValidView,
        "StringView_CopyToCString requires a valid source view." );

    if ( !bValidView ) {
        view = {};
    }

    const bool_t bValidDest = ( pDest != nullptr || cchDest == 0u );
    CY_ASSERT_MSG(
        bValidDest,
        "StringView_CopyToCString requires a destination when capacity is nonzero." );

    if ( !bValidDest || cchDest == 0u ) {
        return view.cchLength;
    }

    const usize cchWritable = cchDest - 1u;
    const usize cchCopy =
        view.cchLength < cchWritable ? view.cchLength : cchWritable;

    if ( cchCopy > 0u ) {
        Cy_MemMove( pDest, view.pData, cchCopy );
    }
    pDest[cchCopy] = '\0';

    return view.cchLength;
}

} // namespace cypher::common
