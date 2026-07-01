/*======================================================================
   File: CypherCommon_String.cpp
   Project: CYPHER
   Author: ksiric <email@example.com>
   Created: 2026-06-22 18:00:51
   Last Modified by: ksiric
   Last Modified: 2026-06-30 12:44:32
   ---------------------------------------------------------------------
   Description:

   ---------------------------------------------------------------------
   License:
   Company:
   Version: 0.1.0
 ======================================================================
                                                                       */

#include "CypherCommon_String.h"
#include "CypherCommon_Char.h"

namespace
{

CYPHER_FORCE_INLINE cypher::common::i32 CyCompareBytes( cypher::common::u8 chA, cypher::common::u8 chB )
{
    return static_cast<cypher::common::i32>( chA ) - static_cast<cypher::common::i32>( chB );
}

} // namespace

namespace cypher::common
{

usize Cy_strlen( const char *pString )
{
    if ( pString == nullptr ) {
        return 0u;
    }

    usize cchCount = 0u;
    const char *pCursor = pString;
    while ( *pCursor != '\0' ) {
        ++pCursor;
        ++cchCount;
    }

    return cchCount;
}

usize Cy_strnlen( const char *pString, usize cchMax )
{
    if ( pString == nullptr ) {
        return 0u;
    }

    usize cchCount = 0u;
    const char *pCursor = pString;
    while ( *pCursor != '\0' && cchCount < cchMax ) {
        ++pCursor;
        ++cchCount;
    }

    return cchCount;
}

bool_t Cy_strisempty( const char *pString )
{
    if ( pString == nullptr ) {
        return true;
    }

    return pString[0] == '\0';
}

bool_t Cy_strisblank( const char *pString )
{
    if ( pString == nullptr ) {
        return true;
    }

    const char *pCursor = pString;
    while ( *pCursor != '\0' ) {
        if ( !Char_IsWhitespaceAscii( *pCursor ) ) {
            return false;
        }
        ++pCursor;
    }

    return true;
}

i32 Cy_strcmp( const char *pStringA, const char *pStringB )
{
    if ( pStringA == pStringB ) {
        return 0;
    }

    const char *pA = pStringA != nullptr ? pStringA : "";
    const char *pB = pStringB != nullptr ? pStringB : "";

    while ( true ) {
        u8 chA = static_cast<u8>( pA[0] );
        u8 chB = static_cast<u8>( pB[0] );
        if ( chA != chB || chA == 0u ) {
            return CyCompareBytes( chA, chB );
        }

        chA = static_cast<u8>( pA[1] );
        chB = static_cast<u8>( pB[1] );
        if ( chA != chB || chA == 0u ) {
            return CyCompareBytes( chA, chB );
        }

        chA = static_cast<u8>( pA[2] );
        chB = static_cast<u8>( pB[2] );
        if ( chA != chB || chA == 0u ) {
            return CyCompareBytes( chA, chB );
        }

        chA = static_cast<u8>( pA[3] );
        chB = static_cast<u8>( pB[3] );
        if ( chA != chB || chA == 0u ) {
            return CyCompareBytes( chA, chB );
        }

        pA += 4u;
        pB += 4u;
    }
}

i32 Cy_strncmp( const char *pStringA, const char *pStringB, usize cchMax )
{
    if ( cchMax == 0u || pStringA == pStringB ) {
        return 0;
    }

    const char *pA = pStringA != nullptr ? pStringA : "";
    const char *pB = pStringB != nullptr ? pStringB : "";

    usize cchRemaining = cchMax;
    while ( cchRemaining >= 4u ) {
        u8 chA = static_cast<u8>( pA[0] );
        u8 chB = static_cast<u8>( pB[0] );
        if ( chA != chB || chA == 0u ) {
            return CyCompareBytes( chA, chB );
        }

        chA = static_cast<u8>( pA[1] );
        chB = static_cast<u8>( pB[1] );
        if ( chA != chB || chA == 0u ) {
            return CyCompareBytes( chA, chB );
        }

        chA = static_cast<u8>( pA[2] );
        chB = static_cast<u8>( pB[2] );
        if ( chA != chB || chA == 0u ) {
            return CyCompareBytes( chA, chB );
        }

        chA = static_cast<u8>( pA[3] );
        chB = static_cast<u8>( pB[3] );
        if ( chA != chB || chA == 0u ) {
            return CyCompareBytes( chA, chB );
        }

        pA += 4u;
        pB += 4u;
        cchRemaining -= 4u;
    }

    while ( cchRemaining > 0u ) {
        const u8 chA = static_cast<u8>( *pA );
        const u8 chB = static_cast<u8>( *pB );
        if ( chA != chB || chA == 0u ) {
            return CyCompareBytes( chA, chB );
        }

        ++pA;
        ++pB;
        --cchRemaining;
    }

    return 0;
}

i32 Cy_stricmp( const char *pStringA, const char *pStringB )
{
    const char *pA = pStringA != nullptr ? pStringA : "";
    const char *pB = pStringB != nullptr ? pStringB : "";
    while ( true ) {
        const u8 chA = static_cast<u8>( Char_ToLowerAscii( *pA ) );
        const u8 chB = static_cast<u8>( Char_ToLowerAscii( *pB ) );
        if ( chA != chB ) {
            return static_cast<i32>( chA ) - static_cast<i32>( chB );
        }
        if ( chA == 0u ) {
            return 0;
        }
        ++pA;
        ++pB;
    }
    return ( 0 );
}

i32 Cy_strnicmp( const char *pStringA, const char *pStringB, usize cchMax )
{
    const char *pA = pStringA != nullptr ? pStringA : "";
    const char *pB = pStringB != nullptr ? pStringB : "";
    if ( cchMax == 0u ) {
        return 0;
    }
    usize cchCount = 0u;
    while ( cchCount < cchMax ) {
        const u8 chA = static_cast<u8>( Char_ToLowerAscii( *pA ) );
        const u8 chB = static_cast<u8>( Char_ToLowerAscii( *pB ) );
        if ( chA != chB ) {
            return static_cast<i32>( chA ) - static_cast<i32>( chB );
        }
        if ( chA == 0u ) {
            return 0;
        }
        ++pA;
        ++pB;
        ++cchCount;
    }
    return ( 0 );
}

bool_t Cy_strequal( const char *pStringA, const char *pStringB )
{
    return Cy_strcmp( pStringA, pStringB ) == 0;
}

bool_t Cy_striequal( const char *pStringA, const char *pStringB )
{
    return Cy_stricmp( pStringA, pStringB ) == 0;
}

bool_t Cy_strnequal( const char *pStringA, const char *pStringB, usize cchMax )
{
    return Cy_strncmp( pStringA, pStringB, cchMax ) == 0;
}

bool_t Cy_strniequal( const char *pStringA, const char *pStringB, usize cchMax )
{
    return Cy_strnicmp( pStringA, pStringB, cchMax ) == 0;
}

usize Cy_strncpy( char *pDest, const char *pSrc, usize cchDest )
{
    const char *pRead = pSrc != nullptr ? pSrc : "";
    usize cchSource = 0u;

    if ( pDest != nullptr && cchDest > 0u ) {
        usize cchWrite = 0u;
        while ( pRead[cchSource] != '\0' ) {
            if ( cchWrite + 1u < cchDest ) {
                pDest[cchWrite] = pRead[cchSource];
                ++cchWrite;
            }
            ++cchSource;
        }
        pDest[cchWrite] = '\0';
        return cchSource;
    }

    while ( pRead[cchSource] != '\0' ) {
        ++cchSource;
    }

    return cchSource;
}

usize Cy_strncpy_max( char *pDest, const char *pSrc, usize cchDest, usize cchMax )
{
    const char *pRead = pSrc != nullptr ? pSrc : "";
    usize cchSource = 0u;
    usize cchWrite = 0u;

    if ( pDest != nullptr && cchDest > 0u ) {
        while ( cchSource < cchMax && pRead[cchSource] != '\0' ) {
            if ( cchWrite + 1u < cchDest ) {
                pDest[cchWrite] = pRead[cchSource];
                ++cchWrite;
            }
            ++cchSource;
        }
        pDest[cchWrite] = '\0';
        return cchSource;
    }

    while ( cchSource < cchMax && pRead[cchSource] != '\0' ) {
        ++cchSource;
    }

    return cchSource;
}

usize Cy_strncat( char *pDest, const char *pSrc, usize cchDest )
{
    const char *pRead = pSrc != nullptr ? pSrc : "";
    usize cchDestLen = 0u;
    usize cchSource = 0u;

    if ( pDest != nullptr && cchDest > 0u ) {
        while ( cchDestLen < cchDest && pDest[cchDestLen] != '\0' ) {
            ++cchDestLen;
        }

        usize cchWrite = cchDestLen;
        while ( pRead[cchSource] != '\0' ) {
            if ( cchWrite + 1u < cchDest ) {
                pDest[cchWrite] = pRead[cchSource];
                ++cchWrite;
            }
            ++cchSource;
        }
        if ( cchDestLen < cchDest ) {
            pDest[cchWrite] = '\0';
        } else {
            pDest[cchDest - 1u] = '\0';
        }

        return cchDestLen + cchSource;
    }

    while ( pRead[cchSource] != '\0' ) {
        ++cchSource;
    }

    return cchSource;
}

usize Cy_strncat_max( char *pDest, const char *pSrc, usize cchDest, usize cchMax )
{
    const char *pRead = pSrc != nullptr ? pSrc : "";
    usize cchDestLen = 0u;
    usize cchSource = 0u;

    if ( pDest != nullptr && cchDest > 0u ) {
        while ( cchDestLen < cchDest && pDest[cchDestLen] != '\0' ) {
            ++cchDestLen;
        }

        usize cchWrite = cchDestLen;
        while ( cchSource < cchMax && pRead[cchSource] != '\0' ) {
            if ( cchWrite + 1u < cchDest ) {
                pDest[cchWrite] = pRead[cchSource];
                ++cchWrite;
            }
            ++cchSource;
        }

        if ( cchDestLen < cchDest ) {
            pDest[cchWrite] = '\0';
        } else {
            pDest[cchDest - 1u] = '\0';
        }

        return cchDestLen + cchSource;
    }

    while ( cchSource < cchMax && pRead[cchSource] != '\0' ) {
        ++cchSource;
    }

    return cchSource;
}

const char *Cy_strchr( const char *pString, char chFind )
{
    if ( pString == nullptr ) {
        return nullptr;
    }

    const char *pCursor = pString;
    while ( true ) {
        if ( *pCursor == chFind ) {
            return pCursor;
        }
        if ( *pCursor == '\0' ) {
            return nullptr;
        }
        ++pCursor;
    }
}

char *Cy_strchr( char *pString, char chFind )
{
    const char *pResult = Cy_strchr( static_cast<const char *>( pString ), chFind );
    return const_cast<char *>( pResult );
}

const char *Cy_strrchr( const char *pString, char chFind )
{
    if ( pString == nullptr ) {
        return nullptr;
    }

    const char *pLast = nullptr;
    const char *pCursor = pString;
    while ( true ) {
        if ( *pCursor == chFind ) {
            pLast = pCursor;
        }
        if ( *pCursor == '\0' ) {
            break;
        }
        ++pCursor;
    }

    return pLast;
}

char *Cy_strrchr( char *pString, char chFind )
{
    const char *pResult = Cy_strrchr( static_cast<const char *>( pString ), chFind );
    return const_cast<char *>( pResult );
}

const char *Cy_strnchr( const char *pString, char chFind, usize cchMax )
{
    if ( pString == nullptr ) {
        return nullptr;
    }

    const char *pCursor = pString;
    usize cchCount = 0u;
    while ( cchCount < cchMax ) {
        if ( *pCursor == chFind ) {
            return pCursor;
        }
        if ( *pCursor == '\0' ) {
            return nullptr;
        }
        ++pCursor;
        ++cchCount;
    }

    return nullptr;
}

char *Cy_strnchr( char *pString, char chFind, usize cchMax )
{
    const char *pResult = Cy_strnchr( static_cast<const char *>( pString ), chFind, cchMax );
    return const_cast<char *>( pResult );
}

const char *Cy_strstr( const char *pString, const char *pSearch )
{
    if ( pString == nullptr ) {
        return nullptr;
    }

    const char *pNeedle = pSearch != nullptr ? pSearch : "";
    const usize cchNeedle = Cy_strlen( pNeedle );
    if ( cchNeedle == 0u ) {
        return pString;
    }

    const char *pCursor = pString;
    while ( *pCursor != '\0' ) {
        if ( *pCursor == pNeedle[0] && Cy_strncmp( pCursor, pNeedle, cchNeedle ) == 0 ) {
            return pCursor;
        }
        ++pCursor;
    }

    return nullptr;
}

char *Cy_strstr( char *pString, const char *pSearch )
{
    const char *pResult = Cy_strstr( static_cast<const char *>( pString ), pSearch );
    return const_cast<char *>( pResult );
}

const char *Cy_stristr( const char *pString, const char *pSearch )
{
    if ( pString == nullptr ) {
        return nullptr;
    }

    const char *pNeedle = pSearch != nullptr ? pSearch : "";
    const usize cchNeedle = Cy_strlen( pNeedle );
    if ( cchNeedle == 0u ) {
        return pString;
    }

    const char chNeedle = Char_ToLowerAscii( pNeedle[0] );
    const char *pCursor = pString;
    while ( *pCursor != '\0' ) {
        if ( Char_ToLowerAscii( *pCursor ) == chNeedle && Cy_strnicmp( pCursor, pNeedle, cchNeedle ) == 0 ) {
            return pCursor;
        }
        ++pCursor;
    }

    return nullptr;
}

char *Cy_stristr( char *pString, const char *pSearch )
{
    const char *pResult = Cy_stristr( static_cast<const char *>( pString ), pSearch );
    return const_cast<char *>( pResult );
}

const char *Cy_strnstr( const char *pString, const char *pSearch, usize cchMax )
{
    if ( pString == nullptr ) {
        return nullptr;
    }

    const char *pNeedle = pSearch != nullptr ? pSearch : "";
    const usize cchNeedle = Cy_strlen( pNeedle );
    if ( cchNeedle == 0u ) {
        return pString;
    }
    if ( cchNeedle > cchMax ) {
        return nullptr;
    }

    usize i = 0u;
    while ( i + cchNeedle <= cchMax && pString[i] != '\0' ) {
        if ( pString[i] == pNeedle[0] && Cy_strncmp( pString + i, pNeedle, cchNeedle ) == 0 ) {
            return pString + i;
        }
        ++i;
    }

    return nullptr;
}

char *Cy_strnstr( char *pString, const char *pSearch, usize cchMax )
{
    const char *pResult = Cy_strnstr( static_cast<const char *>( pString ), pSearch, cchMax );
    return const_cast<char *>( pResult );
}

const char *Cy_strnistr( const char *pString, const char *pSearch, usize cchMax )
{
    if ( pString == nullptr ) {
        return nullptr;
    }

    const char *pNeedle = pSearch != nullptr ? pSearch : "";
    const usize cchNeedle = Cy_strlen( pNeedle );
    if ( cchNeedle == 0u ) {
        return pString;
    }
    if ( cchNeedle > cchMax ) {
        return nullptr;
    }

    const char chNeedle = Char_ToLowerAscii( pNeedle[0] );
    usize i = 0u;
    while ( i + cchNeedle <= cchMax && pString[i] != '\0' ) {
        if ( Char_ToLowerAscii( pString[i] ) == chNeedle && Cy_strnicmp( pString + i, pNeedle, cchNeedle ) == 0 ) {
            return pString + i;
        }
        ++i;
    }

    return nullptr;
}

char *Cy_strnistr( char *pString, const char *pSearch, usize cchMax )
{
    const char *pResult = Cy_strnistr( static_cast<const char *>( pString ), pSearch, cchMax );
    return const_cast<char *>( pResult );
}

bool_t Cy_strstarts( const char *pString, const char *pPrefix )
{
    const char *pRead = pString != nullptr ? pString : "";
    const char *pNeedle = pPrefix != nullptr ? pPrefix : "";
    const usize cchPrefix = Cy_strlen( pNeedle );
    return Cy_strncmp( pRead, pNeedle, cchPrefix ) == 0;
}

bool_t Cy_stristarts( const char *pString, const char *pPrefix )
{
    const char *pRead = pString != nullptr ? pString : "";
    const char *pNeedle = pPrefix != nullptr ? pPrefix : "";
    const usize cchPrefix = Cy_strlen( pNeedle );
    return Cy_strnicmp( pRead, pNeedle, cchPrefix ) == 0;
}

bool_t Cy_strends( const char *pString, const char *pSuffix )
{
    const char *pRead = pString != nullptr ? pString : "";
    const char *pNeedle = pSuffix != nullptr ? pSuffix : "";
    const usize cchString = Cy_strlen( pRead );
    const usize cchSuffix = Cy_strlen( pNeedle );
    if ( cchSuffix > cchString ) {
        return false;
    }

    return Cy_strcmp( pRead + ( cchString - cchSuffix ), pNeedle ) == 0;
}

bool_t Cy_striends( const char *pString, const char *pSuffix )
{
    const char *pRead = pString != nullptr ? pString : "";
    const char *pNeedle = pSuffix != nullptr ? pSuffix : "";
    const usize cchString = Cy_strlen( pRead );
    const usize cchSuffix = Cy_strlen( pNeedle );
    if ( cchSuffix > cchString ) {
        return false;
    }

    return Cy_stricmp( pRead + ( cchString - cchSuffix ), pNeedle ) == 0;
}

char *Cy_strlower( char *pString )
{
    return Cy_strnlower( pString, Cy_strlen( pString ) );
}

char *Cy_strupper( char *pString )
{
    return Cy_strnupper( pString, Cy_strlen( pString ) );
}

char *Cy_strnlower( char *pString, usize cchMax )
{
    if ( pString == nullptr ) {
        return nullptr;
    }

    usize cchCount = 0u;
    while ( cchCount < cchMax && pString[cchCount] != '\0' ) {
        pString[cchCount] = Char_ToLowerAscii( pString[cchCount] );
        ++cchCount;
    }

    return pString;
}

char *Cy_strnupper( char *pString, usize cchMax )
{
    if ( pString == nullptr ) {
        return nullptr;
    }

    usize cchCount = 0u;
    while ( cchCount < cchMax && pString[cchCount] != '\0' ) {
        pString[cchCount] = Char_ToUpperAscii( pString[cchCount] );
        ++cchCount;
    }

    return pString;
}

bool_t Cy_strislower( const char *pString )
{
    const char *pRead = pString != nullptr ? pString : "";
    while ( *pRead != '\0' ) {
        if ( Char_IsUpperAscii( *pRead ) ) {
            return false;
        }
        ++pRead;
    }

    return true;
}

bool_t Cy_strisupper( const char *pString )
{
    const char *pRead = pString != nullptr ? pString : "";
    while ( *pRead != '\0' ) {
        if ( Char_IsLowerAscii( *pRead ) ) {
            return false;
        }
        ++pRead;
    }

    return true;
}

const char *Cy_strskipwhite( const char *pString )
{
    if ( pString == nullptr ) {
        return nullptr;
    }

    const char *pCursor = pString;
    while ( Char_IsWhitespaceAscii( *pCursor ) ) {
        ++pCursor;
    }

    return pCursor;
}

char *Cy_strskipwhite( char *pString )
{
    const char *pResult = Cy_strskipwhite( static_cast<const char *>( pString ) );
    return const_cast<char *>( pResult );
}

void Cy_strtrimleft( char *pString )
{
    if ( pString == nullptr ) {
        return;
    }

    const char *pRead = Cy_strskipwhite( pString );
    char *pWrite = pString;
    while ( *pRead != '\0' ) {
        *pWrite = *pRead;
        ++pWrite;
        ++pRead;
    }
    *pWrite = '\0';
}

void Cy_strtrimright( char *pString )
{
    if ( pString == nullptr ) {
        return;
    }

    usize cchLen = Cy_strlen( pString );
    while ( cchLen > 0u && Char_IsWhitespaceAscii( pString[cchLen - 1u] ) ) {
        --cchLen;
    }
    pString[cchLen] = '\0';
}

void Cy_strtrim( char *pString )
{
    Cy_strtrimright( pString );
    Cy_strtrimleft( pString );
}

void Cy_strstripquotes( char *pString )
{
    if ( pString == nullptr ) {
        return;
    }

    const usize cchLen = Cy_strlen( pString );
    if ( cchLen < 2u ) {
        return;
    }

    const char chFirst = pString[0];
    const char chLast = pString[cchLen - 1u];
    if ( ( chFirst != '"' || chLast != '"' ) && ( chFirst != '\'' || chLast != '\'' ) ) {
        return;
    }

    for ( usize i = 1u; i + 1u < cchLen; ++i ) {
        pString[i - 1u] = pString[i];
    }
    pString[cchLen - 2u] = '\0';
}

usize Cy_strleft( const char *pString, char *pDest, usize cchDest, usize cchCount )
{
    return Cy_strncpy_max( pDest, pString, cchDest, cchCount );
}

usize Cy_strright( const char *pString, char *pDest, usize cchDest, usize cchCount )
{
    const char *pRead = pString != nullptr ? pString : "";
    const usize cchLen = Cy_strlen( pRead );
    const usize iStart = cchLen > cchCount ? cchLen - cchCount : 0u;
    return Cy_strncpy( pDest, pRead + iStart, cchDest );
}

usize Cy_strslice( const char *pString, char *pDest, usize cchDest, usize iStart, usize cchCount )
{
    const char *pRead = pString != nullptr ? pString : "";
    const usize cchLen = Cy_strlen( pRead );
    if ( iStart >= cchLen ) {
        if ( pDest != nullptr && cchDest > 0u ) {
            pDest[0] = '\0';
        }
        return 0u;
    }

    const usize cchAvailable = cchLen - iStart;
    const usize cchSlice = cchAvailable < cchCount ? cchAvailable : cchCount;
    return Cy_strncpy_max( pDest, pRead + iStart, cchDest, cchSlice );
}

usize Cy_strsubst( const char *pString, const char *pSearch, const char *pReplace, char *pDest, usize cchDest )
{
    const char *pRead = pString != nullptr ? pString : "";
    const char *pNeedle = pSearch != nullptr ? pSearch : "";
    const char *pReplacement = pReplace != nullptr ? pReplace : "";
    const usize cchNeedle = Cy_strlen( pNeedle );

    if ( cchNeedle == 0u ) {
        return Cy_strncpy( pDest, pRead, cchDest );
    }

    const usize cchReplacement = Cy_strlen( pReplacement );
    usize cchRequired = 0u;
    usize cchWrite = 0u;

    while ( *pRead != '\0' ) {
        if ( Cy_strncmp( pRead, pNeedle, cchNeedle ) == 0 ) {
            for ( usize i = 0u; i < cchReplacement; ++i ) {
                if ( pDest != nullptr && cchDest > 0u && cchWrite + 1u < cchDest ) {
                    pDest[cchWrite] = pReplacement[i];
                    ++cchWrite;
                }
                ++cchRequired;
            }
            pRead += cchNeedle;
        } else {
            if ( pDest != nullptr && cchDest > 0u && cchWrite + 1u < cchDest ) {
                pDest[cchWrite] = *pRead;
                ++cchWrite;
            }
            ++cchRequired;
            ++pRead;
        }
    }

    if ( pDest != nullptr && cchDest > 0u ) {
        pDest[cchWrite] = '\0';
    }

    return cchRequired;
}

usize Cy_strcountchar( const char *pString, char chFind )
{
    const char *pRead = pString != nullptr ? pString : "";
    usize cchCount = 0u;
    while ( *pRead != '\0' ) {
        if ( *pRead == chFind ) {
            ++cchCount;
        }
        ++pRead;
    }

    return cchCount;
}

usize Cy_strcountstring( const char *pString, const char *pSearch )
{
    const char *pRead = pString != nullptr ? pString : "";
    const char *pNeedle = pSearch != nullptr ? pSearch : "";
    const usize cchNeedle = Cy_strlen( pNeedle );
    if ( cchNeedle == 0u ) {
        return 0u;
    }

    usize cchCount = 0u;
    while ( *pRead != '\0' ) {
        if ( Cy_strncmp( pRead, pNeedle, cchNeedle ) == 0 ) {
            ++cchCount;
            pRead += cchNeedle;
        } else {
            ++pRead;
        }
    }

    return cchCount;
}

} // namespace cypher::common
