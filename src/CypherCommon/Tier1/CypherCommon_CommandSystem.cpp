//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_CommandSystem.cpp
//  Purpose: Implements the instance-owned command and ConVar registry.
//  Details: Registry metadata and string values are allocator-owned. Stable name
//           storage backs hash keys, while generational tables reject stale handles
//           and transactional updates preserve live state after allocation failure.
//
//  History:
//  - Created by Karlo Siric on 2026-08-09
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_CommandSystem.h"

#include "CypherCommon_HandleTable.h"
#include "CypherCommon_Hash.h"
#include "CypherCommon_HashMap.h"
#include "CypherCommon_TextBuffer.h"

#include <new>

namespace cypher::common
{

namespace command_system_detail
{

struct name_hasher_t {
    bool_t bCaseInsensitiveAscii{ CY_TRUE };

    hash64_t operator()( string_view_t name ) const noexcept
    {
        if ( !StringView_IsValid( name ) ) {
            return 0u;
        }
        return bCaseInsensitiveAscii
            ? Hash64_StringInsensitiveAscii( name )
            : Hash64_String( name );
    }
};

struct name_equal_t {
    bool_t bCaseInsensitiveAscii{ CY_TRUE };

    bool_t operator()( string_view_t left, string_view_t right ) const noexcept
    {
        if ( !StringView_IsValid( left ) || !StringView_IsValid( right ) ) {
            return CY_FALSE;
        }
        return bCaseInsensitiveAscii
            ? StringView_EqualsInsensitiveAscii( left, right )
            : StringView_Equals( left, right );
    }
};

struct command_record_t {
    command_record_t() noexcept = default;
    CYPHER_NO_COPY( command_record_t );
    command_record_t &operator=( command_record_t && ) = delete;

    command_record_t( command_record_t &&source ) noexcept
        : desc( source.desc )
    {
        TextBuffer_Move( &name, &source.name );
        TextBuffer_Move( &help, &source.help );
        TextBuffer_Move( &usage, &source.usage );
        RebindViews();
        source.desc = {};
    }

    void RebindViews() noexcept
    {
        desc.name = TextBuffer_View( &name );
        desc.help = TextBuffer_View( &help );
        desc.usage = TextBuffer_View( &usage );
    }

    text_buffer_t name{};
    text_buffer_t help{};
    text_buffer_t usage{};
    concommand_desc_t desc{};
};

struct convar_record_t {
    convar_record_t() noexcept = default;
    CYPHER_NO_COPY( convar_record_t );
    convar_record_t &operator=( convar_record_t && ) = delete;

    convar_record_t( convar_record_t &&source ) noexcept
        : desc( source.desc ),
          currentValue( source.currentValue ),
          minimum( source.minimum ),
          maximum( source.maximum ),
          bHasMinimum( source.bHasMinimum ),
          bHasMaximum( source.bHasMaximum ),
          bInCallback( source.bInCallback )
    {
        TextBuffer_Move( &name, &source.name );
        TextBuffer_Move( &help, &source.help );
        TextBuffer_Move( &defaultText, &source.defaultText );
        TextBuffer_Move( &minimumText, &source.minimumText );
        TextBuffer_Move( &maximumText, &source.maximumText );
        TextBuffer_Move( &currentText, &source.currentText );
        RebindViews();
        source.desc = {};
        source.currentValue = {};
        source.minimum = {};
        source.maximum = {};
        source.bHasMinimum = CY_FALSE;
        source.bHasMaximum = CY_FALSE;
        source.bInCallback = CY_FALSE;
    }

    void RebindViews() noexcept
    {
        desc.name = TextBuffer_View( &name );
        desc.help = TextBuffer_View( &help );
        desc.defaultValue = TextBuffer_View( &defaultText );
        desc.minValue = TextBuffer_View( &minimumText );
        desc.maxValue = TextBuffer_View( &maximumText );
        if ( desc.type == convar_type_t::STRING ) {
            currentValue.value = Variant_FromString(
                TextBuffer_View( &currentText ) );
        }
    }

    text_buffer_t name{};
    text_buffer_t help{};
    text_buffer_t defaultText{};
    text_buffer_t minimumText{};
    text_buffer_t maximumText{};
    text_buffer_t currentText{};
    convar_desc_t desc{};
    convar_value_t currentValue{};
    convar_value_t minimum{};
    convar_value_t maximum{};
    bool_t bHasMinimum{ CY_FALSE };
    bool_t bHasMaximum{ CY_FALSE };
    bool_t bInCallback{ CY_FALSE };
};

using command_name_map_t = hash_map_t<
    string_view_t,
    command_handle_t,
    name_hasher_t,
    name_equal_t>;

using convar_name_map_t = hash_map_t<
    string_view_t,
    convar_handle_t,
    name_hasher_t,
    name_equal_t>;

} // namespace command_system_detail

struct command_system_t {
    const allocator_t *pAllocator{ nullptr };
    command_output_fn_t pfnOutput{ nullptr };
    void *pOutputUserData{ nullptr };
    bool_t bCaseInsensitiveAscii{ CY_TRUE };
    bool_t bInitialized{ CY_FALSE };
    usize nExecutionDepth{ 0u };
    usize nCallbackDepth{ 0u };

    // Name maps are declared after record tables so they are destroyed first.
    handle_table_t<command_system_detail::command_record_t> commands{};
    handle_table_t<command_system_detail::convar_record_t> convars{};
    command_system_detail::command_name_map_t commandNames{};
    command_system_detail::convar_name_map_t convarNames{};
};

namespace
{

using command_system_detail::command_record_t;
using command_system_detail::convar_record_t;
using command_system_detail::name_equal_t;
using command_system_detail::name_hasher_t;

CYPHER_NODISCARD bool_t IsReady( const command_system_t *pSystem ) noexcept
{
    return pSystem != nullptr && pSystem->bInitialized;
}

CYPHER_NODISCARD bool_t IsValidCommandSource(
    command_source_t source ) noexcept
{
    return static_cast<u8>( source ) <=
           static_cast<u8>( command_source_t::TOOL );
}

CYPHER_NODISCARD bool_t IsRemoteSource(
    command_source_t source ) noexcept
{
    return source == command_source_t::REMOTE_CLIENT;
}

CYPHER_NODISCARD bool_t HasEmbeddedNull( string_view_t text ) noexcept
{
    return StringView_IsValid( text ) &&
           StringView_FindChar( text, '\0' ) != CY_STRING_VIEW_NPOS;
}

CYPHER_NODISCARD bool_t InitOwnedText(
    text_buffer_t *pBuffer,
    const allocator_t *pAllocator,
    string_view_t text ) noexcept
{
    return TextBuffer_Init( pBuffer, pAllocator ) &&
           TextBuffer_Assign( pBuffer, text );
}

CYPHER_NODISCARD bool_t InitCommandRecord(
    command_record_t *pRecord,
    const allocator_t *pAllocator,
    const concommand_desc_t &desc ) noexcept
{
    if ( pRecord == nullptr || !Allocator_IsValid( pAllocator ) ||
         !ConCommand_ValidateDesc( desc ) ) {
        return CY_FALSE;
    }

    if ( !InitOwnedText( &pRecord->name, pAllocator, desc.name ) ||
         !InitOwnedText( &pRecord->help, pAllocator, desc.help ) ||
         !InitOwnedText( &pRecord->usage, pAllocator, desc.usage ) ) {
        return CY_FALSE;
    }

    pRecord->desc = desc;
    pRecord->RebindViews();
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t InitConVarRecord(
    convar_record_t *pRecord,
    const allocator_t *pAllocator,
    const convar_desc_t &desc ) noexcept
{
    if ( pRecord == nullptr || !Allocator_IsValid( pAllocator ) ||
         !ConVar_ValidateDesc( desc ) ) {
        return CY_FALSE;
    }

    if ( !InitOwnedText( &pRecord->name, pAllocator, desc.name ) ||
         !InitOwnedText( &pRecord->help, pAllocator, desc.help ) ||
         !InitOwnedText( &pRecord->defaultText, pAllocator, desc.defaultValue ) ||
         !InitOwnedText( &pRecord->minimumText, pAllocator, desc.minValue ) ||
         !InitOwnedText( &pRecord->maximumText, pAllocator, desc.maxValue ) ||
         !TextBuffer_Init( &pRecord->currentText, pAllocator ) ) {
        return CY_FALSE;
    }

    convar_value_t defaultValue{};
    if ( !ConVar_ParseSucceeded(
             ConVar_ParseValue( desc.type, desc.defaultValue, &defaultValue ) ) ) {
        return CY_FALSE;
    }

    pRecord->bHasMinimum = !StringView_IsEmpty( desc.minValue );
    pRecord->bHasMaximum = !StringView_IsEmpty( desc.maxValue );
    if ( pRecord->bHasMinimum &&
         !ConVar_ParseSucceeded( ConVar_ParseValue(
             desc.type,
             desc.minValue,
             &pRecord->minimum ) ) ) {
        return CY_FALSE;
    }
    if ( pRecord->bHasMaximum &&
         !ConVar_ParseSucceeded( ConVar_ParseValue(
             desc.type,
             desc.maxValue,
             &pRecord->maximum ) ) ) {
        return CY_FALSE;
    }

    if ( desc.type == convar_type_t::STRING &&
         !TextBuffer_Assign( &pRecord->currentText, desc.defaultValue ) ) {
        return CY_FALSE;
    }

    pRecord->desc = desc;
    pRecord->currentValue = defaultValue;
    pRecord->RebindViews();
    return CY_TRUE;
}

CYPHER_NODISCARD bool_t NameStartsWith(
    const command_system_t &system,
    string_view_t name,
    string_view_t prefix ) noexcept
{
    return system.bCaseInsensitiveAscii
        ? StringView_StartsWithInsensitiveAscii( name, prefix )
        : StringView_StartsWith( name, prefix );
}

CYPHER_NODISCARD i32 CompareNames(
    const command_system_t &system,
    string_view_t left,
    string_view_t right ) noexcept
{
    return system.bCaseInsensitiveAscii
        ? StringView_CompareInsensitiveAscii( left, right )
        : StringView_Compare( left, right );
}

CYPHER_NODISCARD bool_t NameExists(
    const command_system_t &system,
    string_view_t name ) noexcept
{
    return HashMap_Contains( &system.commandNames, name ) ||
           HashMap_Contains( &system.convarNames, name );
}

CYPHER_NODISCARD bool_t IsCommandAllowed(
    const concommand_desc_t &desc,
    const command_context_t &context ) noexcept
{
    if ( ( desc.flags & CONCOMMAND_FLAG_CHEAT ) != 0u &&
         !context.bCheatsAllowed ) {
        return CY_FALSE;
    }
    if ( ( desc.flags & CONCOMMAND_FLAG_DEVELOPMENT ) != 0u &&
         !context.bDevelopmentAllowed ) {
        return CY_FALSE;
    }
    if ( ( desc.flags & CONCOMMAND_FLAG_SERVER_ONLY ) != 0u &&
         !context.bServerContext ) {
        return CY_FALSE;
    }
    if ( ( desc.flags & CONCOMMAND_FLAG_CLIENT_ONLY ) != 0u &&
         !context.bClientContext ) {
        return CY_FALSE;
    }
    if ( IsRemoteSource( context.source ) &&
         ( desc.flags & CONCOMMAND_FLAG_REMOTE_ALLOWED ) == 0u ) {
        return CY_FALSE;
    }
    return CY_TRUE;
}

CYPHER_NODISCARD convar_system_error_t ConVarWritePermission(
    const convar_desc_t &desc,
    const command_context_t &context ) noexcept
{
    if ( ( desc.flags & CONVAR_FLAG_READ_ONLY ) != 0u ) {
        return convar_system_error_t::READ_ONLY;
    }
    if ( ( desc.flags & CONVAR_FLAG_CHEAT ) != 0u &&
         !context.bCheatsAllowed ) {
        return convar_system_error_t::PERMISSION_DENIED;
    }
    if ( ( desc.flags & CONVAR_FLAG_DEVELOPMENT ) != 0u &&
         !context.bDevelopmentAllowed ) {
        return convar_system_error_t::PERMISSION_DENIED;
    }
    if ( IsRemoteSource( context.source ) &&
         ( desc.flags & CONVAR_FLAG_REMOTE_WRITE_ALLOWED ) == 0u ) {
        return convar_system_error_t::PERMISSION_DENIED;
    }
    return convar_system_error_t::OK;
}

CYPHER_NODISCARD i32 CompareConVarValues(
    convar_type_t type,
    const convar_value_t &left,
    const convar_value_t &right ) noexcept
{
    switch ( type ) {
        case convar_type_t::I64:
            if ( left.value.data.iValue < right.value.data.iValue ) return -1;
            if ( left.value.data.iValue > right.value.data.iValue ) return 1;
            return 0;
        case convar_type_t::U64:
            if ( left.value.data.uValue < right.value.data.uValue ) return -1;
            if ( left.value.data.uValue > right.value.data.uValue ) return 1;
            return 0;
        case convar_type_t::F64:
            if ( left.value.data.flValue < right.value.data.flValue ) return -1;
            if ( left.value.data.flValue > right.value.data.flValue ) return 1;
            return 0;
        case convar_type_t::BOOL:
        case convar_type_t::STRING:
            return 0;
    }
    return 0;
}

CYPHER_NODISCARD convar_system_error_t ValidateConVarBounds(
    const convar_record_t &record,
    const convar_value_t &value ) noexcept
{
    if ( record.bHasMinimum &&
         CompareConVarValues( record.desc.type, value, record.minimum ) < 0 ) {
        return convar_system_error_t::BELOW_MINIMUM;
    }
    if ( record.bHasMaximum &&
         CompareConVarValues( record.desc.type, value, record.maximum ) > 0 ) {
        return convar_system_error_t::ABOVE_MAXIMUM;
    }
    return convar_system_error_t::OK;
}

void InvokeConVarCallback(
    command_system_t &system,
    convar_record_t &record,
    const convar_value_t &oldValue ) noexcept
{
    if ( record.desc.pfnChanged == nullptr ) {
        return;
    }

    record.bInCallback = CY_TRUE;
    ++system.nCallbackDepth;
    record.desc.pfnChanged(
        record.desc.name,
        oldValue,
        record.currentValue,
        record.desc.pUserData );
    --system.nCallbackDepth;
    record.bInCallback = CY_FALSE;
}

CYPHER_NODISCARD error_code_t SetConVarRecord(
    command_system_t &system,
    convar_record_t &record,
    string_view_t text,
    const command_context_t &context ) noexcept
{
    if ( record.bInCallback ) {
        return CommandSystem_MakeError( convar_system_error_t::BUSY );
    }

    const convar_system_error_t permission =
        ConVarWritePermission( record.desc, context );
    if ( permission != convar_system_error_t::OK ) {
        return CommandSystem_MakeError( permission );
    }

    convar_value_t parsed{};
    const convar_parse_result_t parseResult = ConVar_ParseValue(
        record.desc.type,
        text,
        &parsed );
    if ( !ConVar_ParseSucceeded( parseResult ) ) {
        return CommandSystem_MakeError( convar_system_error_t::INVALID_VALUE );
    }

    const convar_system_error_t bounds = ValidateConVarBounds( record, parsed );
    if ( bounds != convar_system_error_t::OK ) {
        return CommandSystem_MakeError( bounds );
    }

    if ( Variant_Equals( record.currentValue.value, parsed.value ) ) {
        return CY_ERROR_OK;
    }

    if ( record.desc.type != convar_type_t::STRING ) {
        const convar_value_t oldValue = record.currentValue;
        record.currentValue = parsed;
        InvokeConVarCallback( system, record, oldValue );
        return CY_ERROR_OK;
    }

    text_buffer_t replacement{};
    if ( !TextBuffer_Init( &replacement, system.pAllocator ) ||
         !TextBuffer_Assign( &replacement, text ) ) {
        return CommandSystem_MakeError( convar_system_error_t::OUT_OF_MEMORY );
    }

    text_buffer_t previous{};
    TextBuffer_Move( &previous, &record.currentText );
    TextBuffer_Move( &record.currentText, &replacement );
    const convar_value_t oldValue{
        Variant_FromString( TextBuffer_View( &previous ) )
    };
    record.currentValue.value = Variant_FromString(
        TextBuffer_View( &record.currentText ) );
    InvokeConVarCallback( system, record, oldValue );
    return CY_ERROR_OK;
}

CYPHER_NODISCARD string_view_t ConVarValueView(
    const convar_value_t &value,
    char *pScratch,
    usize cchScratch ) noexcept
{
    if ( value.value.type == variant_type_t::STRING_VIEW ) {
        return {
            value.value.data.stringValue.pData,
            value.value.data.stringValue.cchLength
        };
    }

    const usize cchRequired = ConVar_FormatValue(
        value,
        pScratch,
        cchScratch );
    if ( cchRequired >= cchScratch ) {
        return {};
    }
    return StringView_FromRange( pScratch, cchRequired );
}

CYPHER_NODISCARD error_code_t OutputConVar(
    command_system_t &system,
    const convar_record_t &record ) noexcept
{
    if ( system.pfnOutput == nullptr ) {
        return CY_ERROR_OK;
    }

    char scratch[128]{};
    const string_view_t value = ConVarValueView(
        record.currentValue,
        scratch,
        sizeof( scratch ) );
    if ( !StringView_IsValid( value ) ) {
        return CommandSystem_MakeError( convar_system_error_t::INVALID_VALUE );
    }

    constexpr usize cchSeparator = 3u;
    if ( record.desc.name.cchLength > CY_USIZE_MAX - cchSeparator ||
         value.cchLength >
             CY_USIZE_MAX - record.desc.name.cchLength - cchSeparator ) {
        return CommandSystem_MakeError( convar_system_error_t::OUT_OF_MEMORY );
    }
    const usize cchRequired =
        record.desc.name.cchLength + cchSeparator + value.cchLength;

    text_buffer_t output{};
    if ( !TextBuffer_Init( &output, system.pAllocator, cchRequired ) ||
         !TextBuffer_Append( &output, record.desc.name ) ||
         !TextBuffer_Append( &output, StringView_FromCString( " = " ) ) ||
         !TextBuffer_Append( &output, value ) ) {
        return CommandSystem_MakeError( convar_system_error_t::OUT_OF_MEMORY );
    }

    ++system.nCallbackDepth;
    system.pfnOutput( TextBuffer_View( &output ), system.pOutputUserData );
    --system.nCallbackDepth;
    return CY_ERROR_OK;
}

struct execution_scope_t {
    explicit execution_scope_t( command_system_t &systemIn ) noexcept
        : system( systemIn )
    {
        ++system.nExecutionDepth;
    }

    ~execution_scope_t() noexcept
    {
        --system.nExecutionDepth;
    }

    command_system_t &system;
};

void SortSuggestions(
    const command_system_t &system,
    string_view_t *pSuggestions,
    usize nCount ) noexcept
{
    for ( usize iSuggestion = 1u; iSuggestion < nCount; ++iSuggestion ) {
        const string_view_t current = pSuggestions[iSuggestion];
        usize iInsert = iSuggestion;
        while ( iInsert > 0u &&
                CompareNames(
                    system,
                    current,
                    pSuggestions[iInsert - 1u] ) < 0 ) {
            pSuggestions[iInsert] = pSuggestions[iInsert - 1u];
            --iInsert;
        }
        pSuggestions[iInsert] = current;
    }
}

} // namespace

const char *CommandSystem_ErrorName( command_system_error_t error ) noexcept
{
    switch ( error ) {
        case command_system_error_t::OK:                return "OK";
        case command_system_error_t::INVALID_ARGUMENT:  return "INVALID_ARGUMENT";
        case command_system_error_t::OUT_OF_MEMORY:     return "OUT_OF_MEMORY";
        case command_system_error_t::PARSE_FAILED:      return "PARSE_FAILED";
        case command_system_error_t::NOT_FOUND:         return "NOT_FOUND";
        case command_system_error_t::ALREADY_EXISTS:    return "ALREADY_EXISTS";
        case command_system_error_t::PERMISSION_DENIED: return "PERMISSION_DENIED";
        case command_system_error_t::BUSY:              return "BUSY";
        case command_system_error_t::RECURSION_LIMIT:   return "RECURSION_LIMIT";
    }
    return "UNKNOWN_COMMAND_SYSTEM_ERROR";
}

const char *CommandSystem_ErrorName( convar_system_error_t error ) noexcept
{
    switch ( error ) {
        case convar_system_error_t::OK:                return "OK";
        case convar_system_error_t::INVALID_ARGUMENT:  return "INVALID_ARGUMENT";
        case convar_system_error_t::OUT_OF_MEMORY:     return "OUT_OF_MEMORY";
        case convar_system_error_t::NOT_FOUND:         return "NOT_FOUND";
        case convar_system_error_t::ALREADY_EXISTS:    return "ALREADY_EXISTS";
        case convar_system_error_t::READ_ONLY:         return "READ_ONLY";
        case convar_system_error_t::PERMISSION_DENIED: return "PERMISSION_DENIED";
        case convar_system_error_t::INVALID_VALUE:     return "INVALID_VALUE";
        case convar_system_error_t::BELOW_MINIMUM:     return "BELOW_MINIMUM";
        case convar_system_error_t::ABOVE_MAXIMUM:     return "ABOVE_MAXIMUM";
        case convar_system_error_t::BUSY:              return "BUSY";
    }
    return "UNKNOWN_CONVAR_SYSTEM_ERROR";
}

command_system_t *CommandSystem_Create(
    const command_system_desc_t &desc ) noexcept
{
    const allocator_t *pAllocator = desc.pAllocator != nullptr
        ? desc.pAllocator
        : Allocator_GetSystem();
    if ( !Allocator_IsValid( pAllocator ) ||
         desc.nInitialCommands > CY_HANDLE_TABLE_MAX_CAPACITY ||
         desc.nInitialConVars > CY_HANDLE_TABLE_MAX_CAPACITY ) {
        return nullptr;
    }

    void *pStorage = Allocator_Allocate(
        pAllocator,
        sizeof( command_system_t ),
        alignof( command_system_t ) );
    if ( pStorage == nullptr ) {
        return nullptr;
    }

    command_system_t *pSystem =
        ::new ( pStorage ) command_system_t;
    pSystem->pAllocator = pAllocator;
    pSystem->pfnOutput = desc.pfnOutput;
    pSystem->pOutputUserData = desc.pOutputUserData;
    pSystem->bCaseInsensitiveAscii = desc.bCaseInsensitiveAscii;

    const name_hasher_t hasher{ desc.bCaseInsensitiveAscii };
    const name_equal_t equalKey{ desc.bCaseInsensitiveAscii };
    const bool_t bInitialized =
        HandleTable_Init(
            &pSystem->commands,
            pAllocator,
            desc.nInitialCommands ) &&
        HandleTable_Init(
            &pSystem->convars,
            pAllocator,
            desc.nInitialConVars ) &&
        HashMap_Init(
            &pSystem->commandNames,
            pAllocator,
            desc.nInitialCommands,
            hasher,
            equalKey ) &&
        HashMap_Init(
            &pSystem->convarNames,
            pAllocator,
            desc.nInitialConVars,
            hasher,
            equalKey );
    if ( !bInitialized ) {
        pSystem->~command_system_t();
        Allocator_Free(
            pAllocator,
            pStorage,
            sizeof( command_system_t ),
            alignof( command_system_t ) );
        return nullptr;
    }

    pSystem->bInitialized = CY_TRUE;
    return pSystem;
}

void CommandSystem_Destroy( command_system_t *pSystem ) noexcept
{
    if ( pSystem == nullptr ) {
        return;
    }
    const bool_t bCanDestroy =
        IsReady( pSystem ) &&
        pSystem->nCallbackDepth == 0u &&
        pSystem->nExecutionDepth == 0u;
    CY_ASSERT_MSG(
        bCanDestroy,
        "CommandSystem_Destroy cannot run during execution or a callback." );
    if ( !bCanDestroy ) {
        return;
    }

    const allocator_t *pAllocator = pSystem->pAllocator;
    pSystem->bInitialized = CY_FALSE;
    pSystem->~command_system_t();
    Allocator_Free(
        pAllocator,
        pSystem,
        sizeof( command_system_t ),
        alignof( command_system_t ) );
}

bool_t CommandSystem_IsValid( const command_system_t *pSystem ) noexcept
{
    if ( !IsReady( pSystem ) || !Allocator_IsValid( pSystem->pAllocator ) ||
         !HandleTable_IsValid( &pSystem->commands ) ||
         !HandleTable_IsValid( &pSystem->convars ) ||
         !HashMap_IsValid( &pSystem->commandNames ) ||
         !HashMap_IsValid( &pSystem->convarNames ) ||
         HandleTable_Count( &pSystem->commands ) !=
             HashMap_Count( &pSystem->commandNames ) ||
         HandleTable_Count( &pSystem->convars ) !=
             HashMap_Count( &pSystem->convarNames ) ||
         pSystem->nExecutionDepth > CY_COMMAND_MAX_EXECUTION_DEPTH ) {
        return CY_FALSE;
    }

    bool_t bValid = CY_TRUE;
    const usize nCommandsVisited = HandleTable_ForEach(
        &pSystem->commands,
        [&]( command_handle_t handle, const command_record_t &record ) noexcept -> bool_t {
            const command_handle_t *pMapped =
                HashMap_Find( &pSystem->commandNames, record.desc.name );
            bValid = ConCommand_ValidateDesc( record.desc ) &&
                     pMapped != nullptr && pMapped->value == handle.value &&
                     !HashMap_Contains( &pSystem->convarNames, record.desc.name );
            return bValid;
        } );
    (void)nCommandsVisited;
    if ( !bValid ) {
        return CY_FALSE;
    }

    const usize nConVarsVisited = HandleTable_ForEach(
        &pSystem->convars,
        [&]( convar_handle_t handle, const convar_record_t &record ) noexcept -> bool_t {
            const convar_handle_t *pMapped =
                HashMap_Find( &pSystem->convarNames, record.desc.name );
            bValid = ConVar_ValidateDesc( record.desc ) &&
                     ConVar_ValueMatchesType(
                         record.desc.type,
                         record.currentValue ) &&
                     pMapped != nullptr && pMapped->value == handle.value &&
                     !HashMap_Contains( &pSystem->commandNames, record.desc.name );
            if ( bValid && record.desc.type == convar_type_t::STRING ) {
                string_view_t current{};
                bValid = Variant_GetString( record.currentValue.value, &current ) &&
                         StringView_Equals(
                             current,
                             TextBuffer_View( &record.currentText ) );
            }
            return bValid;
        } );
    (void)nConVarsVisited;
    return bValid;
}

usize CommandSystem_CommandCount( const command_system_t *pSystem ) noexcept
{
    return IsReady( pSystem )
        ? HandleTable_Count( &pSystem->commands )
        : 0u;
}

usize CommandSystem_ConVarCount( const command_system_t *pSystem ) noexcept
{
    return IsReady( pSystem )
        ? HandleTable_Count( &pSystem->convars )
        : 0u;
}

command_register_result_t CommandSystem_RegisterCommand(
    command_system_t *pSystem,
    const concommand_desc_t &desc ) noexcept
{
    if ( !IsReady( pSystem ) || !ConCommand_ValidateDesc( desc ) ) {
        return {
            CommandSystem_MakeError( command_system_error_t::INVALID_ARGUMENT ),
            CY_COMMAND_HANDLE_INVALID
        };
    }
    if ( pSystem->nCallbackDepth > 0u ) {
        return {
            CommandSystem_MakeError( command_system_error_t::BUSY ),
            CY_COMMAND_HANDLE_INVALID
        };
    }
    if ( NameExists( *pSystem, desc.name ) ) {
        return {
            CommandSystem_MakeError( command_system_error_t::ALREADY_EXISTS ),
            CY_COMMAND_HANDLE_INVALID
        };
    }

    command_record_t record{};
    if ( !InitCommandRecord( &record, pSystem->pAllocator, desc ) ) {
        return {
            CommandSystem_MakeError( command_system_error_t::OUT_OF_MEMORY ),
            CY_COMMAND_HANDLE_INVALID
        };
    }

    const command_handle_t handle = HandleTable_InsertMove(
        &pSystem->commands,
        static_cast<command_record_t &&>( record ) );
    if ( !Cy_Handle32IsValid( handle ) ) {
        return {
            CommandSystem_MakeError( command_system_error_t::OUT_OF_MEMORY ),
            CY_COMMAND_HANDLE_INVALID
        };
    }

    const command_record_t *pStored = HandleTable_Get(
        &pSystem->commands,
        handle );
    const auto inserted = HashMap_Insert(
        &pSystem->commandNames,
        pStored->desc.name,
        handle );
    if ( !inserted.bInserted ) {
        const bool_t bRemoved = HandleTable_Remove( &pSystem->commands, handle );
        CY_ASSERT_MSG( bRemoved, "Command registration rollback failed." );
        return {
            CommandSystem_MakeError(
                inserted.pValue != nullptr
                    ? command_system_error_t::ALREADY_EXISTS
                    : command_system_error_t::OUT_OF_MEMORY ),
            CY_COMMAND_HANDLE_INVALID
        };
    }
    return { CY_ERROR_OK, handle };
}

convar_register_result_t CommandSystem_RegisterConVar(
    command_system_t *pSystem,
    const convar_desc_t &desc ) noexcept
{
    if ( !IsReady( pSystem ) || !ConVar_ValidateDesc( desc ) ) {
        return {
            CommandSystem_MakeError( convar_system_error_t::INVALID_ARGUMENT ),
            CY_CONVAR_HANDLE_INVALID
        };
    }
    if ( pSystem->nCallbackDepth > 0u ) {
        return {
            CommandSystem_MakeError( convar_system_error_t::BUSY ),
            CY_CONVAR_HANDLE_INVALID
        };
    }
    if ( NameExists( *pSystem, desc.name ) ) {
        return {
            CommandSystem_MakeError( convar_system_error_t::ALREADY_EXISTS ),
            CY_CONVAR_HANDLE_INVALID
        };
    }

    convar_record_t record{};
    if ( !InitConVarRecord( &record, pSystem->pAllocator, desc ) ) {
        return {
            CommandSystem_MakeError( convar_system_error_t::OUT_OF_MEMORY ),
            CY_CONVAR_HANDLE_INVALID
        };
    }

    const convar_handle_t handle = HandleTable_InsertMove(
        &pSystem->convars,
        static_cast<convar_record_t &&>( record ) );
    if ( !Cy_Handle32IsValid( handle ) ) {
        return {
            CommandSystem_MakeError( convar_system_error_t::OUT_OF_MEMORY ),
            CY_CONVAR_HANDLE_INVALID
        };
    }

    const convar_record_t *pStored = HandleTable_Get(
        &pSystem->convars,
        handle );
    const auto inserted = HashMap_Insert(
        &pSystem->convarNames,
        pStored->desc.name,
        handle );
    if ( !inserted.bInserted ) {
        const bool_t bRemoved = HandleTable_Remove( &pSystem->convars, handle );
        CY_ASSERT_MSG( bRemoved, "ConVar registration rollback failed." );
        return {
            CommandSystem_MakeError(
                inserted.pValue != nullptr
                    ? convar_system_error_t::ALREADY_EXISTS
                    : convar_system_error_t::OUT_OF_MEMORY ),
            CY_CONVAR_HANDLE_INVALID
        };
    }
    return { CY_ERROR_OK, handle };
}

error_code_t CommandSystem_UnregisterCommand(
    command_system_t *pSystem,
    command_handle_t handle ) noexcept
{
    if ( !IsReady( pSystem ) || !Cy_Handle32IsValid( handle ) ) {
        return CommandSystem_MakeError( command_system_error_t::INVALID_ARGUMENT );
    }
    if ( pSystem->nCallbackDepth > 0u ) {
        return CommandSystem_MakeError( command_system_error_t::BUSY );
    }

    const command_record_t *pRecord = HandleTable_Get(
        &pSystem->commands,
        handle );
    if ( pRecord == nullptr ) {
        return CommandSystem_MakeError( command_system_error_t::NOT_FOUND );
    }

    const bool_t bErased = HashMap_Erase(
        &pSystem->commandNames,
        pRecord->desc.name );
    const bool_t bRemoved = HandleTable_Remove( &pSystem->commands, handle );
    CY_ASSERT_MSG( bErased && bRemoved, "Command unregistration lost registry state." );
    return bErased && bRemoved
        ? CY_ERROR_OK
        : CommandSystem_MakeError( command_system_error_t::NOT_FOUND );
}

error_code_t CommandSystem_UnregisterConVar(
    command_system_t *pSystem,
    convar_handle_t handle ) noexcept
{
    if ( !IsReady( pSystem ) || !Cy_Handle32IsValid( handle ) ) {
        return CommandSystem_MakeError( convar_system_error_t::INVALID_ARGUMENT );
    }
    if ( pSystem->nCallbackDepth > 0u ) {
        return CommandSystem_MakeError( convar_system_error_t::BUSY );
    }

    const convar_record_t *pRecord = HandleTable_Get(
        &pSystem->convars,
        handle );
    if ( pRecord == nullptr ) {
        return CommandSystem_MakeError( convar_system_error_t::NOT_FOUND );
    }

    const bool_t bErased = HashMap_Erase(
        &pSystem->convarNames,
        pRecord->desc.name );
    const bool_t bRemoved = HandleTable_Remove( &pSystem->convars, handle );
    CY_ASSERT_MSG( bErased && bRemoved, "ConVar unregistration lost registry state." );
    return bErased && bRemoved
        ? CY_ERROR_OK
        : CommandSystem_MakeError( convar_system_error_t::NOT_FOUND );
}

command_handle_t CommandSystem_FindCommand(
    const command_system_t *pSystem,
    string_view_t name ) noexcept
{
    if ( !IsReady( pSystem ) || !StringView_IsValid( name ) ||
         StringView_IsEmpty( name ) || HasEmbeddedNull( name ) ) {
        return CY_COMMAND_HANDLE_INVALID;
    }
    const command_handle_t *pHandle = HashMap_Find(
        &pSystem->commandNames,
        name );
    return pHandle != nullptr ? *pHandle : CY_COMMAND_HANDLE_INVALID;
}

convar_handle_t CommandSystem_FindConVar(
    const command_system_t *pSystem,
    string_view_t name ) noexcept
{
    if ( !IsReady( pSystem ) || !StringView_IsValid( name ) ||
         StringView_IsEmpty( name ) || HasEmbeddedNull( name ) ) {
        return CY_CONVAR_HANDLE_INVALID;
    }
    const convar_handle_t *pHandle = HashMap_Find(
        &pSystem->convarNames,
        name );
    return pHandle != nullptr ? *pHandle : CY_CONVAR_HANDLE_INVALID;
}

bool_t CommandSystem_GetCommandDesc(
    const command_system_t *pSystem,
    command_handle_t handle,
    concommand_desc_t *pDescOut ) noexcept
{
    if ( !IsReady( pSystem ) || pDescOut == nullptr ) {
        return CY_FALSE;
    }
    const command_record_t *pRecord = HandleTable_Get(
        &pSystem->commands,
        handle );
    if ( pRecord == nullptr ) {
        return CY_FALSE;
    }
    *pDescOut = pRecord->desc;
    return CY_TRUE;
}

bool_t CommandSystem_GetConVarDesc(
    const command_system_t *pSystem,
    convar_handle_t handle,
    convar_desc_t *pDescOut ) noexcept
{
    if ( !IsReady( pSystem ) || pDescOut == nullptr ) {
        return CY_FALSE;
    }
    const convar_record_t *pRecord = HandleTable_Get(
        &pSystem->convars,
        handle );
    if ( pRecord == nullptr ) {
        return CY_FALSE;
    }
    *pDescOut = pRecord->desc;
    return CY_TRUE;
}

error_code_t CommandSystem_ExecuteLine(
    command_system_t *pSystem,
    string_view_t commandLine,
    const command_context_t &context ) noexcept
{
    if ( !IsReady( pSystem ) || !IsValidCommandSource( context.source ) ) {
        return CommandSystem_MakeError( command_system_error_t::INVALID_ARGUMENT );
    }
    if ( pSystem->nExecutionDepth >= CY_COMMAND_MAX_EXECUTION_DEPTH ) {
        return CommandSystem_MakeError( command_system_error_t::RECURSION_LIMIT );
    }

    command_args_t args{};
    const command_parse_result_t parseResult = ConCommand_ParseArgs(
        commandLine,
        &args );
    if ( !ConCommand_ParseSucceeded( parseResult ) ) {
        return CommandSystem_MakeError( command_system_error_t::PARSE_FAILED );
    }

    execution_scope_t executionScope{ *pSystem };
    const string_view_t name = args.arguments[0];
    const command_handle_t commandHandle = CommandSystem_FindCommand(
        pSystem,
        name );
    if ( Cy_Handle32IsValid( commandHandle ) ) {
        command_record_t *pRecord = HandleTable_Get(
            &pSystem->commands,
            commandHandle );
        if ( pRecord == nullptr ) {
            return CommandSystem_MakeError( command_system_error_t::NOT_FOUND );
        }
        if ( !IsCommandAllowed( pRecord->desc, context ) ) {
            return CommandSystem_MakeError(
                command_system_error_t::PERMISSION_DENIED );
        }

        const concommand_callback_t pfnExecute = pRecord->desc.pfnExecute;
        void *pCommandUserData = pRecord->desc.pUserData;
        ++pSystem->nCallbackDepth;
        const error_code_t error = pfnExecute(
            context,
            args,
            pCommandUserData );
        --pSystem->nCallbackDepth;
        return error;
    }

    const convar_handle_t convarHandle = CommandSystem_FindConVar(
        pSystem,
        name );
    if ( !Cy_Handle32IsValid( convarHandle ) ) {
        return CommandSystem_MakeError( command_system_error_t::NOT_FOUND );
    }

    if ( args.nArgumentCount == 1u ) {
        const convar_record_t *pRecord = HandleTable_Get(
            &pSystem->convars,
            convarHandle );
        return pRecord != nullptr
            ? OutputConVar( *pSystem, *pRecord )
            : CommandSystem_MakeError( convar_system_error_t::NOT_FOUND );
    }
    if ( args.nArgumentCount != 2u ) {
        return CommandSystem_MakeError( convar_system_error_t::INVALID_ARGUMENT );
    }
    return CommandSystem_SetConVar(
        pSystem,
        convarHandle,
        args.arguments[1],
        context );
}

bool_t CommandSystem_GetConVar(
    const command_system_t *pSystem,
    convar_handle_t handle,
    convar_value_t *pValueOut ) noexcept
{
    if ( !IsReady( pSystem ) || pValueOut == nullptr ) {
        return CY_FALSE;
    }
    const convar_record_t *pRecord = HandleTable_Get(
        &pSystem->convars,
        handle );
    if ( pRecord == nullptr ) {
        return CY_FALSE;
    }
    *pValueOut = pRecord->currentValue;
    return CY_TRUE;
}

error_code_t CommandSystem_SetConVar(
    command_system_t *pSystem,
    convar_handle_t handle,
    string_view_t value,
    const command_context_t &context ) noexcept
{
    if ( !IsReady( pSystem ) || !Cy_Handle32IsValid( handle ) ||
         !StringView_IsValid( value ) || HasEmbeddedNull( value ) ||
         !IsValidCommandSource( context.source ) ) {
        return CommandSystem_MakeError( convar_system_error_t::INVALID_ARGUMENT );
    }

    convar_record_t *pRecord = HandleTable_Get(
        &pSystem->convars,
        handle );
    if ( pRecord == nullptr ) {
        return CommandSystem_MakeError( convar_system_error_t::NOT_FOUND );
    }
    return SetConVarRecord( *pSystem, *pRecord, value, context );
}

error_code_t CommandSystem_ResetConVar(
    command_system_t *pSystem,
    convar_handle_t handle,
    const command_context_t &context ) noexcept
{
    if ( !IsReady( pSystem ) ) {
        return CommandSystem_MakeError( convar_system_error_t::INVALID_ARGUMENT );
    }
    const convar_record_t *pRecord = HandleTable_Get(
        &pSystem->convars,
        handle );
    if ( pRecord == nullptr ) {
        return CommandSystem_MakeError( convar_system_error_t::NOT_FOUND );
    }
    const string_view_t defaultValue = pRecord->desc.defaultValue;
    return CommandSystem_SetConVar( pSystem, handle, defaultValue, context );
}

usize CommandSystem_ForEachCommand(
    command_system_t *pSystem,
    command_visit_fn_t pfnVisitor,
    void *pUserData ) noexcept
{
    if ( !IsReady( pSystem ) || pfnVisitor == nullptr ) {
        return 0u;
    }

    ++pSystem->nCallbackDepth;
    const usize nVisited = HandleTable_ForEach(
        &pSystem->commands,
        [&]( command_handle_t handle, command_record_t &record ) noexcept -> bool_t {
            return pfnVisitor( handle, record.desc, pUserData );
        } );
    --pSystem->nCallbackDepth;
    return nVisited;
}

usize CommandSystem_ForEachConVar(
    command_system_t *pSystem,
    convar_visit_fn_t pfnVisitor,
    void *pUserData ) noexcept
{
    if ( !IsReady( pSystem ) || pfnVisitor == nullptr ) {
        return 0u;
    }

    ++pSystem->nCallbackDepth;
    const usize nVisited = HandleTable_ForEach(
        &pSystem->convars,
        [&]( convar_handle_t handle, convar_record_t &record ) noexcept -> bool_t {
            return pfnVisitor(
                handle,
                record.desc,
                record.currentValue,
                pUserData );
        } );
    --pSystem->nCallbackDepth;
    return nVisited;
}

usize CommandSystem_Complete(
    command_system_t *pSystem,
    string_view_t partial,
    string_view_t *pSuggestions,
    usize nSuggestionCapacity ) noexcept
{
    if ( !IsReady( pSystem ) || !StringView_IsValid( partial ) ||
         HasEmbeddedNull( partial ) ||
         ( pSuggestions == nullptr && nSuggestionCapacity > 0u ) ) {
        return 0u;
    }

    usize iSeparator = CY_STRING_VIEW_NPOS;
    for ( usize iByte = 0u; iByte < partial.cchLength; ++iByte ) {
        if ( partial.pData[iByte] == ' ' || partial.pData[iByte] == '\t' ) {
            iSeparator = iByte;
            break;
        }
    }

    if ( iSeparator != CY_STRING_VIEW_NPOS ) {
        const string_view_t commandName = StringView_Prefix(
            partial,
            iSeparator );
        const command_handle_t handle = CommandSystem_FindCommand(
            pSystem,
            commandName );
        command_record_t *pRecord = HandleTable_Get(
            &pSystem->commands,
            handle );
        if ( pRecord == nullptr || pRecord->desc.pfnComplete == nullptr ) {
            return 0u;
        }

        const string_view_t argumentPartial = StringView_TrimLeft(
            StringView_RemovePrefix( partial, iSeparator + 1u ) );
        ++pSystem->nCallbackDepth;
        const usize nRequired = pRecord->desc.pfnComplete(
            argumentPartial,
            pSuggestions,
            nSuggestionCapacity,
            pRecord->desc.pUserData );
        --pSystem->nCallbackDepth;
        return nRequired;
    }

    usize nRequired = 0u;
    usize nWritten = 0u;
    const usize nCommandsVisited = HandleTable_ForEach(
        &pSystem->commands,
        [&]( command_handle_t, const command_record_t &record ) noexcept -> bool_t {
            if ( ( record.desc.flags & CONCOMMAND_FLAG_HIDDEN ) == 0u &&
                 NameStartsWith( *pSystem, record.desc.name, partial ) ) {
                ++nRequired;
                if ( nWritten < nSuggestionCapacity ) {
                    pSuggestions[nWritten++] = record.desc.name;
                }
            }
            return CY_TRUE;
        } );
    const usize nConVarsVisited = HandleTable_ForEach(
        &pSystem->convars,
        [&]( convar_handle_t, const convar_record_t &record ) noexcept -> bool_t {
            if ( ( record.desc.flags & CONVAR_FLAG_HIDDEN ) == 0u &&
                 NameStartsWith( *pSystem, record.desc.name, partial ) ) {
                ++nRequired;
                if ( nWritten < nSuggestionCapacity ) {
                    pSuggestions[nWritten++] = record.desc.name;
                }
            }
            return CY_TRUE;
        } );
    (void)nCommandsVisited;
    (void)nConVarsVisited;
    SortSuggestions( *pSystem, pSuggestions, nWritten );
    return nRequired;
}

} // namespace cypher::common
