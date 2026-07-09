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

static const error_desc_t g_commonErrorDescs[] = {
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
    domain_t::COMMON,
    g_commonErrorDescs,
    static_cast<u32>( sizeof( g_commonErrorDescs ) / sizeof( g_commonErrorDescs[0] ) )
};

} // namespace

error_t Cy_ErrorMake( domain_t domain, u16 localErrorCode )
{
    return ( static_cast<error_t>( domain ) << 16u ) | static_cast<error_t>( localErrorCode );
}

domain_t Cy_ErrorDomain( error_t error )
{
    return static_cast<domain_t>( static_cast<u16>( error >> 16u ) );
}

u16 Cy_ErrorLocalCode( error_t error )
{
    return static_cast<u16>( error & 0xFFFFu );
}

bool_t Cy_ErrorSucceeded( error_t error )
{
    return Cy_ErrorLocalCode( error ) == 0u;
}

bool_t Cy_ErrorFailed( error_t error )
{
    return Cy_ErrorLocalCode( error ) != 0u;
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
    const error_t packedError = Cy_ErrorMake( domain_t::COMMON, static_cast<u16>( error ) );
    return Cy_ErrorFindName( g_commonErrorTable, packedError );
}

const char *Cy_CommonErrorDescription( common_error_t error )
{
    const error_t packedError = Cy_ErrorMake( domain_t::COMMON, static_cast<u16>( error ) );
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

const char *Cy_ErrorDomainName( domain_t domain )
{
    switch ( domain ) {
        case domain_t::COMMON: return "Common";
        case domain_t::MEMORY: return "Memory";
        case domain_t::LOG: return "Log";
        case domain_t::SYSTEM: return "System";
        case domain_t::HOST: return "Host";
        case domain_t::FILESYSTEM: return "FileSystem";
        case domain_t::PAK: return "Pak";
        case domain_t::COMMAND: return "Command";
        case domain_t::CVAR: return "CVar";
        case domain_t::CONFIG: return "Config";
        case domain_t::RENDER: return "Render";
        case domain_t::AUDIO: return "Audio";
        case domain_t::INPUT: return "Input";
        case domain_t::NETWORK: return "Network";
        case domain_t::PHYSICS: return "Physics";
        case domain_t::GAME: return "Game";
        case domain_t::EDITOR: return "Editor";
        case domain_t::TOOLS: return "Tools";
        case domain_t::ASSET: return "Asset";
        case domain_t::RESOURCE: return "Resource";
        case domain_t::WORLD: return "World";
        case domain_t::ENTITY: return "Entity";
        case domain_t::ANIMATION: return "Animation";
        case domain_t::AI: return "AI";
        case domain_t::SCRIPT: return "Script";
        case domain_t::MATERIAL: return "Material";
        case domain_t::TEXTURE: return "Texture";
        case domain_t::GUI: return "Gui";
        case domain_t::JOB: return "Job";
        case domain_t::SERIALIZATION: return "Serialization";
        case domain_t::REFLECTION: return "Reflection";
    }

    return "Unknown";
}

const error_desc_t *Cy_ErrorFindDesc( const error_table_t &table, error_t error )
{
    if ( table.pErrors == nullptr || table.errorCount == 0u ) {
        return nullptr;
    }

    if ( Cy_ErrorDomain( error ) != table.domain ) {
        return nullptr;
    }

    const u16 localCode = Cy_ErrorLocalCode( error );
    for ( u32 i = 0u; i < table.errorCount; ++i ) {
        const error_desc_t &desc = table.pErrors[i];
        if ( desc.localCode == localCode ) {
            return &desc;
        }
    }

    return nullptr;
}

const char *Cy_ErrorFindName( const error_table_t &table, error_t error )
{
    const error_desc_t *pDesc = Cy_ErrorFindDesc( table, error );
    if ( pDesc == nullptr || pDesc->pName == nullptr ) {
        return "ERR_UNKNOWN";
    }

    return pDesc->pName;
}

const char *Cy_ErrorFindDescription( const error_table_t &table, error_t error )
{
    const error_desc_t *pDesc = Cy_ErrorFindDesc( table, error );
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
