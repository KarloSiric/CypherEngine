//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Error.cpp
//  Purpose: Implements CypherCommon Tier0 packed error helpers.
//  Details: Errors keep subsystem-local codes while still giving logs and
//           diagnostics one compact encoded form.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Error.h"

namespace cypher::common
{

namespace
{

static const error_description_t g_commonErrorDescs[] = {
    { static_cast<u16>( common_error_t::OK ), "OK", "Operation completed successfully." },
    { static_cast<u16>( common_error_t::ERR_FAILED ), "ERR_FAILED", "Operation failed." },
    { static_cast<u16>( common_error_t::ERR_INVALID_ARGUMENT ), "ERR_INVALID_ARGUMENT", "An invalid argument was supplied." },
    { static_cast<u16>( common_error_t::ERR_INVALID_STATE ), "ERR_INVALID_STATE", "The object is in an invalid state for this operation." },
    { static_cast<u16>( common_error_t::ERR_INVALID_OPERATION ), "ERR_INVALID_OPERATION", "The requested operation is invalid." },
    { static_cast<u16>( common_error_t::ERR_NOT_INIT ), "ERR_NOT_INIT", "The subsystem has not been initialized." },
    { static_cast<u16>( common_error_t::ERR_IS_INIT ), "ERR_IS_INIT", "The subsystem is already initialized." },
    { static_cast<u16>( common_error_t::ERR_OUT_OF_MEMORY ), "ERR_OUT_OF_MEMORY", "Memory allocation failed." },
    { static_cast<u16>( common_error_t::ERR_NOT_FOUND ), "ERR_NOT_FOUND", "The requested object was not found." },
    { static_cast<u16>( common_error_t::ERR_UNSUPPORTED ), "ERR_UNSUPPORTED", "The requested feature is unsupported." },
    { static_cast<u16>( common_error_t::ERR_TIMEOUT ), "ERR_TIMEOUT", "The operation timed out." },
    { static_cast<u16>( common_error_t::ERR_IO_ERROR ), "ERR_IO_ERROR", "An input/output operation failed." },
    { static_cast<u16>( common_error_t::ERR_PARSE_FAILED ), "ERR_PARSE_FAILED", "Parsing failed." },
    { static_cast<u16>( common_error_t::ERR_INTERNAL_ERROR ), "ERR_INTERNAL_ERROR", "An internal error occurred." }
};

static const error_table_t g_commonErrorTable = {
    error_domain_t::COMMON,
    g_commonErrorDescs,
    static_cast<u32>( sizeof( g_commonErrorDescs ) / sizeof( g_commonErrorDescs[0] ) )
};

} // namespace

error_code_t Cy_ErrorMake( error_domain_t domain, u16 localErrorCode )
{
    return ( static_cast<error_code_t>( domain ) << 16u ) | static_cast<error_code_t>( localErrorCode );
}

error_domain_t Cy_ErrorDomain( error_code_t errorCode )
{
    return static_cast<error_domain_t>( static_cast<u16>( errorCode >> 16u ) );
}

u16 Cy_ErrorLocalCode( error_code_t errorCode )
{
    return static_cast<u16>( errorCode & 0xFFFFu );
}

bool_t Cy_ErrorSucceeded( error_code_t errorCode )
{
    return Cy_ErrorLocalCode( errorCode ) == 0u;
}

bool_t Cy_ErrorFailed( error_code_t errorCode )
{
    return Cy_ErrorLocalCode( errorCode ) != 0u;
}

bool_t Cy_ErrorSucceeded( common_error_t error )
{
    return error == common_error_t::OK;
}

bool_t Cy_ErrorFailed( common_error_t error )
{
    return error != common_error_t::OK;
}

const char *Cy_CommonErrorName( common_error_t error )
{
    const error_code_t packedError = Cy_ErrorMake( error_domain_t::COMMON, static_cast<u16>( error ) );
    return Cy_ErrorFindName( g_commonErrorTable, packedError );
}

const char *Cy_CommonErrorDescription( common_error_t error )
{
    const error_code_t packedError = Cy_ErrorMake( error_domain_t::COMMON, static_cast<u16>( error ) );
    return Cy_ErrorFindDescription( g_commonErrorTable, packedError );
}

const char *Cy_ErrorName( common_error_t error )
{
    return Cy_CommonErrorName( error );
}

const char *Cy_ErrorDescription( common_error_t error )
{
    return Cy_CommonErrorDescription( error );
}

const char *Cy_ErrorDomainName( error_domain_t domain )
{
    switch ( domain ) {
        case error_domain_t::COMMON: return "Common";
        case error_domain_t::MEMORY: return "Memory";
        case error_domain_t::LOG: return "Log";
        case error_domain_t::SYSTEM: return "System";
        case error_domain_t::HOST: return "Host";
        case error_domain_t::FILESYSTEM: return "FileSystem";
        case error_domain_t::PAK: return "Pak";
        case error_domain_t::COMMAND: return "Command";
        case error_domain_t::CVAR: return "CVar";
        case error_domain_t::CONFIG: return "Config";
        case error_domain_t::RENDER: return "Render";
        case error_domain_t::AUDIO: return "Audio";
        case error_domain_t::INPUT: return "Input";
        case error_domain_t::NETWORK: return "Network";
        case error_domain_t::PHYSICS: return "Physics";
        case error_domain_t::GAME: return "Game";
        case error_domain_t::EDITOR: return "Editor";
        case error_domain_t::TOOLS: return "Tools";
        case error_domain_t::ASSET: return "Asset";
        case error_domain_t::RESOURCE: return "Resource";
        case error_domain_t::WORLD: return "World";
        case error_domain_t::ENTITY: return "Entity";
        case error_domain_t::ANIMATION: return "Animation";
        case error_domain_t::AI: return "AI";
        case error_domain_t::SCRIPT: return "Script";
        case error_domain_t::MATERIAL: return "Material";
        case error_domain_t::TEXTURE: return "Texture";
        case error_domain_t::GUI: return "Gui";
        case error_domain_t::JOB: return "Job";
        case error_domain_t::SERIALIZATION: return "Serialization";
        case error_domain_t::REFLECTION: return "Reflection";
    }

    return "Unknown";
}

const error_description_t *Cy_ErrorFindDesc( const error_table_t &table, error_code_t errorCode )
{
    if ( table.pErrors == nullptr || table.errorCount == 0u ) {
        return nullptr;
    }

    if ( Cy_ErrorDomain( errorCode ) != table.domain ) {
        return nullptr;
    }

    const u16 localCode = Cy_ErrorLocalCode( errorCode );
    for ( u32 i = 0u; i < table.errorCount; ++i ) {
        const error_description_t &desc = table.pErrors[i];
        if ( desc.localCode == localCode ) {
            return &desc;
        }
    }

    return nullptr;
}

const char *Cy_ErrorFindName( const error_table_t &table, error_code_t errorCode )
{
    const error_description_t *pDesc = Cy_ErrorFindDesc( table, errorCode );
    if ( pDesc == nullptr || pDesc->pName == nullptr ) {
        return "ERR_UNKNOWN";
    }

    return pDesc->pName;
}

const char *Cy_ErrorFindDescription( const error_table_t &table, error_code_t errorCode )
{
    const error_description_t *pDesc = Cy_ErrorFindDesc( table, errorCode );
    if ( pDesc == nullptr || pDesc->pDescription == nullptr ) {
        return "Unknown error.";
    }

    return pDesc->pDescription;
}

const error_table_t *Cy_CommonErrorTable()
{
    return &g_commonErrorTable;
}

} // namespace cypher::common
