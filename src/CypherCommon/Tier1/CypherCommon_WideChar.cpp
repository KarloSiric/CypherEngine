//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_WideChar.cpp
//  Purpose: Implements platform-wide-character text helpers.
//  Details: These helpers support text conversion and tool integration without
//           expanding the dependency-light Tier0 public surface.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_WideChar.h"

namespace cypher::common
{

usize WChar_Length( const wchar_engine_t *pString )
{
    if ( pString == nullptr ) {
        return 0u;
    }

    usize cchLength = 0u;
    while ( pString[cchLength] != L'\0' ) {
        ++cchLength;
    }

    return cchLength;
}

i32 WChar_Compare( const wchar_engine_t *pStringA, const wchar_engine_t *pStringB )
{
    const wchar_engine_t *pA = pStringA != nullptr ? pStringA : L"";
    const wchar_engine_t *pB = pStringB != nullptr ? pStringB : L"";

    while ( *pA != L'\0' && *pA == *pB ) {
        ++pA;
        ++pB;
    }

    if ( *pA < *pB ) {
        return -1;
    }
    if ( *pA > *pB ) {
        return 1;
    }
    return 0;
}

usize WChar_Copy( wchar_engine_t *pDest, const wchar_engine_t *pSrc, usize cchDest )
{
    if ( pDest == nullptr || cchDest == 0u ) {
        return 0u;
    }

    const wchar_engine_t *pRead = pSrc != nullptr ? pSrc : L"";

    usize i = 0u;
    for ( ; i + 1u < cchDest && pRead[i] != L'\0'; ++i ) {
        pDest[i] = pRead[i];
    }
    pDest[i] = L'\0';

    return WChar_Length( pRead );
}

} // namespace cypher::common
