//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: CypherEditor_Commands.cpp
//  Purpose: Implements the editor command registry, console-line execution,
//           and prefix completion.
//  Details: The table stays sorted by ID so lookups are logarithmic and menus,
//           help, and completion list commands in a stable order no matter
//           which module registered first.
//
//  History:
//  - Created by Karlo Siric on 2026-09-26
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherEditor_Commands.h"

#include "CypherCommon/Tier0/CypherCommon_Assert.h"
#include "CypherCommon/Tier2/CypherCommon_DataValidation.h"

#include <cstring>

namespace cypher::editor
{

using namespace cypher::common;

namespace
{

CYPHER_NODISCARD i32 CompareIds( string_view_t a, string_view_t b ) noexcept
{
    const usize cch = a.cchLength < b.cchLength ? a.cchLength : b.cchLength;
    const i32 order = cch != 0u ? std::memcmp( a.pData, b.pData, cch ) : 0;
    if ( order != 0 ) { return order; }
    return a.cchLength < b.cchLength ? -1 : ( a.cchLength > b.cchLength ? 1 : 0 );
}

CYPHER_NODISCARD string_view_t IdOf( const command_desc_t &command ) noexcept
{
    return StringView_FromCString( command.pId );
}

// Index of the first command whose ID is not less than id.
CYPHER_NODISCARD usize LowerBound( const command_registry_t &registry, string_view_t id ) noexcept
{
    usize iLow = 0u;
    usize iHigh = Vector_Count( &registry.commands );
    while ( iLow < iHigh ) {
        const usize iMid = iLow + ( iHigh - iLow ) / 2u;
        if ( CompareIds( IdOf( registry.commands.pData[iMid] ), id ) < 0 ) { iLow = iMid + 1u; } else { iHigh = iMid; }
    }
    return iLow;
}

CYPHER_NODISCARD bool_t StartsWith( string_view_t text, string_view_t prefix ) noexcept
{
    return text.cchLength >= prefix.cchLength &&
           ( prefix.cchLength == 0u || std::memcmp( text.pData, prefix.pData, prefix.cchLength ) == 0 );
}

CYPHER_NODISCARD bool_t IsSpace( char c ) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

} // namespace

bool_t EditorCommand_IsValidId( string_view_t id ) noexcept
{
    if ( id.cchLength == 0u || id.cchLength > EDITOR_COMMAND_ID_MAX_LENGTH ) { return CY_FALSE; }
    usize nParts = 0u;
    usize iStart = 0u;
    for ( usize iChar = 0u; iChar <= id.cchLength; ++iChar ) {
        if ( iChar < id.cchLength && id.pData[iChar] != '.' ) { continue; }
        if ( !DataValidation_Succeeded( DataValidation_CheckStableIdentifier(
                 { id.pData + iStart, iChar - iStart }, EDITOR_COMMAND_ID_MAX_LENGTH ) ) ) {
            return CY_FALSE;
        }
        ++nParts;
        iStart = iChar + 1u;
    }
    return nParts >= 2u;
}

command_registry_status_t EditorCommands_Init( command_registry_t *pRegistry, const allocator_t *pAllocator ) noexcept
{
    if ( pRegistry == nullptr || pAllocator == nullptr ) { return command_registry_status_t::INVALID_ARGUMENT; }
    pRegistry->pfnObserver = nullptr;
    pRegistry->pObserverContext = nullptr;
    return Vector_Init( &pRegistry->commands, pAllocator ) ? command_registry_status_t::OK
                                                            : command_registry_status_t::OUT_OF_MEMORY;
}

void EditorCommands_Shutdown( command_registry_t *pRegistry ) noexcept
{
    if ( pRegistry != nullptr ) { Vector_Shutdown( &pRegistry->commands ); }
}

command_registry_status_t EditorCommands_Register(
    command_registry_t *pRegistry,
    const command_desc_t *pCommands,
    usize nCommands ) noexcept
{
    if ( pRegistry == nullptr || ( pCommands == nullptr && nCommands != 0u ) ) {
        return command_registry_status_t::INVALID_ARGUMENT;
    }
    // Validate everything first so a bad table registers nothing.
    for ( usize iCommand = 0u; iCommand < nCommands; ++iCommand ) {
        const command_desc_t &command = pCommands[iCommand];
        if ( command.pId == nullptr || !EditorCommand_IsValidId( IdOf( command ) ) || command.pfnExecute == nullptr ||
             command.pLabel == nullptr || command.pLabel[0] == '\0' ) {
            return command_registry_status_t::INVALID_ARGUMENT;
        }
        if ( EditorCommands_Find( pRegistry, IdOf( command ) ) != nullptr ) {
            return command_registry_status_t::DUPLICATE_COMMAND;
        }
        for ( usize iEarlier = 0u; iEarlier < iCommand; ++iEarlier ) {
            if ( CompareIds( IdOf( pCommands[iEarlier] ), IdOf( command ) ) == 0 ) {
                return command_registry_status_t::DUPLICATE_COMMAND;
            }
        }
    }
    if ( !Vector_Reserve( &pRegistry->commands, Vector_Count( &pRegistry->commands ) + nCommands ) ) {
        return command_registry_status_t::OUT_OF_MEMORY;
    }
    for ( usize iCommand = 0u; iCommand < nCommands; ++iCommand ) {
        const usize iInsert = LowerBound( *pRegistry, IdOf( pCommands[iCommand] ) );
        // Duplicates were rejected above, so the slot is strictly between neighbours.
        CY_ASSERT( iInsert == Vector_Count( &pRegistry->commands ) ||
                   CompareIds( IdOf( pRegistry->commands.pData[iInsert] ), IdOf( pCommands[iCommand] ) ) > 0 );
        if ( !Vector_Insert( &pRegistry->commands, iInsert, pCommands[iCommand] ) ) {
            return command_registry_status_t::OUT_OF_MEMORY; // Unreachable after the reserve.
        }
    }
    return command_registry_status_t::OK;
}

usize EditorCommands_UnregisterModule( command_registry_t *pRegistry, string_view_t module ) noexcept
{
    if ( pRegistry == nullptr || module.cchLength == 0u ) { return 0u; }
    usize nRemoved = 0u;
    usize iCommand = 0u;
    while ( iCommand < Vector_Count( &pRegistry->commands ) ) {
        const string_view_t id = IdOf( pRegistry->commands.pData[iCommand] );
        // "map" owns "map.tool.clip" but not "mapping.x".
        if ( id.cchLength > module.cchLength && StartsWith( id, module ) && id.pData[module.cchLength] == '.' ) {
            Vector_Erase( &pRegistry->commands, iCommand );
            ++nRemoved;
        } else {
            ++iCommand;
        }
    }
    return nRemoved;
}

void EditorCommands_SetObserver(
    command_registry_t *pRegistry,
    command_observer_fn pfnObserver,
    void *pObserverContext ) noexcept
{
    if ( pRegistry == nullptr ) { return; }
    pRegistry->pfnObserver = pfnObserver;
    pRegistry->pObserverContext = pObserverContext;
}

const command_desc_t *EditorCommands_Find( const command_registry_t *pRegistry, string_view_t id ) noexcept
{
    if ( pRegistry == nullptr ) { return nullptr; }
    const usize iFound = LowerBound( *pRegistry, id );
    return iFound < Vector_Count( &pRegistry->commands ) &&
                   CompareIds( IdOf( pRegistry->commands.pData[iFound] ), id ) == 0
        ? &pRegistry->commands.pData[iFound] : nullptr;
}

usize EditorCommands_Count( const command_registry_t *pRegistry ) noexcept
{
    return pRegistry != nullptr ? Vector_Count( &pRegistry->commands ) : 0u;
}

const command_desc_t *EditorCommands_At( const command_registry_t *pRegistry, usize iCommand ) noexcept
{
    return pRegistry != nullptr && iCommand < Vector_Count( &pRegistry->commands ) ? &pRegistry->commands.pData[iCommand]
                                                                                  : nullptr;
}

u32 EditorCommands_State( const command_registry_t *pRegistry, string_view_t id ) noexcept
{
    const command_desc_t *pCommand = EditorCommands_Find( pRegistry, id );
    if ( pCommand == nullptr ) { return COMMAND_STATE_NONE; }
    return pCommand->pfnState != nullptr ? pCommand->pfnState( pCommand->pContext ) : COMMAND_STATE_ENABLED;
}

command_result_t EditorCommands_Execute(
    const command_registry_t *pRegistry,
    string_view_t id,
    const command_args_t &args ) noexcept
{
    const command_desc_t *pCommand = EditorCommands_Find( pRegistry, id );
    if ( pCommand == nullptr ) { return command_result_t::UNKNOWN_COMMAND; }
    const u32 state = pCommand->pfnState != nullptr ? pCommand->pfnState( pCommand->pContext ) : COMMAND_STATE_ENABLED;
    CY_ASSERT_MSG( pCommand->pfnExecute != nullptr, "Registration guarantees an execute callback" );
    const command_result_t result = ( state & COMMAND_STATE_ENABLED ) != 0u
        ? pCommand->pfnExecute( pCommand->pContext, args )
        : command_result_t::DISABLED;
    if ( pRegistry->pfnObserver != nullptr ) {
        pRegistry->pfnObserver( pRegistry->pObserverContext, *pCommand, args, result );
    }
    return result;
}

command_result_t EditorCommands_ExecuteLine( const command_registry_t *pRegistry, string_view_t line ) noexcept
{
    if ( line.pData == nullptr || line.cchLength > EDITOR_COMMAND_LINE_MAX_LENGTH ) { return command_result_t::BAD_LINE; }
    // Unquoted tokens borrow the line; quoted ones are unescaped into scratch.
    char scratch[EDITOR_COMMAND_LINE_MAX_LENGTH]{};
    usize cchScratch = 0u;
    string_view_t tokens[EDITOR_COMMAND_MAX_ARGS + 1u]{};
    usize nTokens = 0u;
    usize iChar = 0u;
    while ( iChar < line.cchLength ) {
        if ( IsSpace( line.pData[iChar] ) ) { ++iChar; continue; }
        if ( nTokens == EDITOR_COMMAND_MAX_ARGS + 1u ) { return command_result_t::BAD_LINE; }
        if ( line.pData[iChar] != '"' ) {
            const usize iStart = iChar;
            while ( iChar < line.cchLength && !IsSpace( line.pData[iChar] ) ) { ++iChar; }
            tokens[nTokens++] = { line.pData + iStart, iChar - iStart };
            continue;
        }
        const usize iStart = cchScratch;
        bool_t bClosed = CY_FALSE;
        for ( ++iChar; iChar < line.cchLength; ++iChar ) {
            char c = line.pData[iChar];
            if ( c == '"' ) { bClosed = CY_TRUE; ++iChar; break; }
            if ( c == '\\' && iChar + 1u < line.cchLength && ( line.pData[iChar + 1u] == '"' || line.pData[iChar + 1u] == '\\' ) ) {
                c = line.pData[++iChar];
            }
            scratch[cchScratch++] = c;
        }
        if ( !bClosed ) { return command_result_t::BAD_LINE; }
        tokens[nTokens++] = { scratch + iStart, cchScratch - iStart };
    }
    if ( nTokens == 0u ) { return command_result_t::BAD_LINE; }
    const command_args_t args{ nTokens > 1u ? tokens + 1 : nullptr, nTokens - 1u };
    return EditorCommands_Execute( pRegistry, tokens[0], args );
}

usize EditorCommands_Complete(
    const command_registry_t *pRegistry,
    string_view_t prefix,
    const command_desc_t **ppMatches,
    usize nCapacity ) noexcept
{
    if ( pRegistry == nullptr ) { return 0u; }
    usize nMatches = 0u;
    for ( usize iCommand = LowerBound( *pRegistry, prefix ); iCommand < Vector_Count( &pRegistry->commands ); ++iCommand ) {
        if ( !StartsWith( IdOf( pRegistry->commands.pData[iCommand] ), prefix ) ) { break; }
        if ( ppMatches != nullptr && nMatches < nCapacity ) { ppMatches[nMatches] = &pRegistry->commands.pData[iCommand]; }
        ++nMatches;
    }
    return nMatches;
}

const char *EditorCommands_ResultName( command_result_t result ) noexcept
{
    switch ( result ) {
        case command_result_t::OK: return "OK";
        case command_result_t::FAILED: return "FAILED";
        case command_result_t::INVALID_ARGUMENTS: return "INVALID_ARGUMENTS";
        case command_result_t::DISABLED: return "DISABLED";
        case command_result_t::UNKNOWN_COMMAND: return "UNKNOWN_COMMAND";
        case command_result_t::BAD_LINE: return "BAD_LINE";
    }
    return "UNKNOWN";
}

} // namespace cypher::editor
