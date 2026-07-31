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
#include "CypherCommon_String.h"
#include "CypherCommon_Assert.h"

namespace cypher::common
{

string_view_t StringView_FromCString( const char *pString ) noexcept
{
    if ( pString == nullptr ) {
        return ( string_view_t{} );
    }

    string_view_t view{};
    view.pData = pString;
    view.cchLength = Cy_strlen( pString );

    return ( view );
}

string_view_t StringView_FromRange( const char *pData, usize cchLength ) noexcept
{
    string_view_t view{};
    if ( pData == nullptr ) {
        // A null pointer can represent only an empty range.
        CY_ASSERT_MSG( cchLength == 0u, "StringView_FromRange requires a non-null data for a non-empty range." );
        return ( view );
    }

    view.pData = pData;
    view.cchLength = cchLength;
    return ( view );
}

bool_t StringView_IsEmpty( string_view_t view ) noexcept
{
    return ( view.cchLength == 0u );
}

bool_t StringView_IsValid( string_view_t view ) noexcept
{
    if ( view.pData == nullptr && view.cchLength != 0u ) {
        return ( CY_FALSE );
    }

    return ( CY_TRUE );
}

usize StringView_Length( string_view_t view ) noexcept
{
    return ( view.cchLength );
}

const char *StringView_Begin( string_view_t view ) noexcept
{
    if ( view.pData == nullptr ) {
        CY_ASSERT_MSG( view.cchLength == 0u, "StringView_Begin requires a non-null data for a non-empty range." );
        return ( nullptr );
    }
    
    // we return where things begin.   
    return ( view.pData );
}

const char *StringView_End( string_view_t view ) noexcept
{
    if ( view.pData == nullptr ) {
        CY_ASSERT_MSG( view.cchLength == 0u, "StringView_End requires a non-null data for a non-empty range." );
        return ( nullptr );
    }
    
    // we return where things end.
    return ( view.pData + view.cchLength );
}

char StringView_At( string_view_t view, usize iIndex ) noexcept
{
    const bool_t bValid = StringView_IsValid( view );
    CY_ASSERT_MSG( bValid, "StringView_At requires a valid string view." );
    if ( !bValid ) {
        return ( '\0' );
    }
    
    const bool_t bInBounds = ( iIndex < view.cchLength );
    CY_ASSERT_MSG( bInBounds, "StringView_At index is outside the view range." );
    if ( !bInBounds ) {
        return ( '\0' );
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



} // namespace cypher::common
