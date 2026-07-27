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
    { static_cast<u16>( common_error_t::ERR_INTERNAL_ERROR ), "ERR_INTERNAL_ERROR", "An internal error occurred." },
    { static_cast<u16>( common_error_t::ERR_BUFFER_TOO_SMALL ), "ERR_BUFFER_TOO_SMALL", "The supplied buffer is too small." },
    { static_cast<u16>( common_error_t::ERR_ALREADY_EXISTS ), "ERR_ALREADY_EXISTS", "The requested object already exists." },
    { static_cast<u16>( common_error_t::ERR_BUSY ), "ERR_BUSY", "The requested resource is busy." },
    { static_cast<u16>( common_error_t::ERR_CANCELLED ), "ERR_CANCELLED", "The operation was cancelled." },
    { static_cast<u16>( common_error_t::ERR_OVERFLOW ), "ERR_OVERFLOW", "The operation exceeded its representable range." },
    { static_cast<u16>( common_error_t::ERR_PERMISSION_DENIED ), "ERR_PERMISSION_DENIED", "Permission was denied." },
    { static_cast<u16>( common_error_t::ERR_END_OF_STREAM ), "ERR_END_OF_STREAM", "The end of the input stream was reached." },
    { static_cast<u16>( common_error_t::ERR_CORRUPT_DATA ), "ERR_CORRUPT_DATA", "The input data is corrupt." },
    { static_cast<u16>( common_error_t::ERR_VERSION_MISMATCH ), "ERR_VERSION_MISMATCH", "The data or interface version is incompatible." }
};

static const error_table_t g_commonErrorTable = {
    error_domain_t::COMMON,
    g_commonErrorDescs,
    CYPHER_ARRAY_COUNT( g_commonErrorDescs )
};

static_assert(
    CYPHER_ARRAY_COUNT( g_commonErrorDescs ) == static_cast<usize>( common_error_t::COUNT ),
    "Every Common error must have a description." );

} // namespace

const char *Cy_CommonErrorName( common_error_t error ) noexcept
{
    const error_code_t packedError = Cy_ErrorMake( error );
    return Cy_ErrorFindName( g_commonErrorTable, packedError );
}

const char *Cy_CommonErrorDescription( common_error_t error ) noexcept
{
    const error_code_t packedError = Cy_ErrorMake( error );
    return Cy_ErrorFindDescription( g_commonErrorTable, packedError );
}

const char *Cy_ErrorName( common_error_t error ) noexcept
{
    return Cy_CommonErrorName( error );
}

const char *Cy_ErrorDescription( common_error_t error ) noexcept
{
    return Cy_CommonErrorDescription( error );
}

const char *Cy_ErrorDomainName( error_domain_t domain ) noexcept
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
        case error_domain_t::COUNT:
        case error_domain_t::INVALID:
            break;
    }

    return "Unknown";
}

const error_description_t *Cy_ErrorFindDesc( const error_table_t &table, error_code_t errorCode ) noexcept
{
    if ( table.pErrors == nullptr || table.errorCount == 0u ) {
        return nullptr;
    }

    if ( Cy_ErrorDomain( errorCode ) != table.domain ) {
        return nullptr;
    }

    const u16 localCode = Cy_ErrorLocalCode( errorCode );
    for ( usize i = 0u; i < table.errorCount; ++i ) {
        const error_description_t &desc = table.pErrors[i];
        if ( desc.localCode == localCode ) {
            return &desc;
        }
    }

    return nullptr;
}

const char *Cy_ErrorFindName( const error_table_t &table, error_code_t errorCode ) noexcept
{
    const error_description_t *pDesc = Cy_ErrorFindDesc( table, errorCode );
    if ( pDesc == nullptr || pDesc->pName == nullptr ) {
        return "ERR_UNKNOWN";
    }

    return pDesc->pName;
}

const char *Cy_ErrorFindDescription( const error_table_t &table, error_code_t errorCode ) noexcept
{
    const error_description_t *pDesc = Cy_ErrorFindDesc( table, errorCode );
    if ( pDesc == nullptr || pDesc->pDescription == nullptr ) {
        return "Unknown error.";
    }

    return pDesc->pDescription;
}

const error_table_t *Cy_CommonErrorTable() noexcept
{
    return &g_commonErrorTable;
}

} // namespace cypher::common
