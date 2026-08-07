/*======================================================================
   File: CypherCommon_Lexer.cpp
   Project: CYPHER
   Author: ksiric <email@example.com>
   Created: 2026-08-07 00:20:43
   Last Modified by: ksiric
   Last Modified: 2026-08-07 23:09:51
   ---------------------------------------------------------------------
   Description:
       
   ---------------------------------------------------------------------
   License: 
   Company: 
   Version: 0.1.0
 ======================================================================
                                                                       */

#include "CypherCommon_Lexer.h"

#include "CypherCommon_Assert.h"
#include "CypherCommon_Char.h"

namespace cypher::common
{

namespace 
{

static bool_t Lexer_HasValidByteAt( const lexer_t &lexer, usize iByteIdx ) noexcept
{
    return ( lexer.source.pData != nullptr && iByteIdx < lexer.source.cchLength );
}

static char Lexer_ByteAt( const lexer_t &lexer, usize iByteIdx ) noexcept
{
    const bool_t bHasByte = Lexer_HasValidByteAt( lexer, iByteIdx );
    CY_ASSERT_MSG( bHasByte, "Lexer_ByteAt requires an existing source byte." );
    
    if ( !bHasByte ) {
        return '\0';
    }
    
    return lexer.source.pData[iByteIdx];
}

static void Lexer_Advance( lexer_t *pLexer ) noexcept
{
    CY_ASSERT_MSG( pLexer != nullptr, "Lexer_Advance requires a valid lexer." );
    if ( pLexer == nullptr ) {
        return ;
    }
    const usize iCurrentByte = pLexer->cursor.iByte;
    const bool_t bHasCurrentByte = Lexer_HasValidByteAt( *pLexer, iCurrentByte );
    CY_ASSERT_MSG( bHasCurrentByte, "Lexer_Advance cannot advance beyond the source." );
    if ( !bHasCurrentByte ) {
        return ;
    }
    
    const char chCurrent = Lexer_ByteAt( *pLexer, iCurrentByte );
    if ( chCurrent == '\r' ) {
        ++pLexer->cursor.iByte;
        // Treat Windows CRLF as one logical newline.
        if ( Lexer_HasValidByteAt( *pLexer, pLexer->cursor.iByte ) && Lexer_ByteAt( *pLexer, pLexer->cursor.iByte ) == '\n' ) {
            ++pLexer->cursor.iByte;
        }
        ++pLexer->cursor.nLine;
        pLexer->cursor.nColumn = 1u;
        return ;
    }
    
    ++pLexer->cursor.iByte;
    
    if ( chCurrent == '\n' ) {
        ++pLexer->cursor.nLine;
        pLexer->cursor.nColumn = 1u;
    } else {
        ++pLexer->cursor.nColumn;
    }
    
    return ;
}
    
}       // NAMESPACE

}       // namespace cypher::common
