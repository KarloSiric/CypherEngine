//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Error.h
//  Purpose: Declares CypherCommon Tier0 Error support.
//  Details: Tier0 is dependency-light runtime infrastructure shared by the engine,
//           tools, tests, and future editor code. Keep this layer portable,
//           predictable, and careful about allocation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_ERROR_H
#define CYPHER_COMMON_TIER0_ERROR_H
#pragma once

/*
================
CypherCommon Error

Common error packing and domain declarations. Subsystems still own their local
error enums, but this gives logs and diagnostics one shared encoded form.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using error_t = u32;

enum class domain_t : u16 {
    COMMON = 0u,
    MEMORY,
    LOG,
    SYSTEM,
    HOST,
    FILESYSTEM,
    PAK,
    COMMAND,
    CVAR,
    CONFIG,
    RENDER,
    AUDIO,
    INPUT,
    NETWORK,
    PHYSICS,
    GAME,
    EDITOR,
    TOOLS,
    ASSET,
    RESOURCE,
    WORLD,
    ENTITY,
    ANIMATION,
    AI,
    SCRIPT,
    MATERIAL,
    TEXTURE,
    GUI,
    JOB,
    SERIALIZATION,
    REFLECTION,

    COM_DOMAIN_COMMON = COMMON,
    COM_DOMAIN_MEMORY = MEMORY,
    COM_DOMAIN_LOG = LOG,
    COM_DOMAIN_SYSTEM = SYSTEM,
    COM_DOMAIN_HOST = HOST,
    COM_DOMAIN_FILESYSTEM = FILESYSTEM,
    COM_DOMAIN_PAK = PAK,
    COM_DOMAIN_COMMAND = COMMAND,
    COM_DOMAIN_CVAR = CVAR,
    COM_DOMAIN_CONFIG = CONFIG,
    COM_DOMAIN_RENDER = RENDER,
    COM_DOMAIN_AUDIO = AUDIO,
    COM_DOMAIN_INPUT = INPUT,
    COM_DOMAIN_NETWORK = NETWORK,
    COM_DOMAIN_PHYSICS = PHYSICS,
    COM_DOMAIN_GAME = GAME,
    COM_DOMAIN_EDITOR = EDITOR,
    COM_DOMAIN_TOOLS = TOOLS
};

enum class common_error_t : u16 {
    OK = 0u,
    ERR_FAILED,
    ERR_INVALID_ARGUMENT,
    ERR_INVALID_STATE,
    ERR_INVALID_OPERATION,
    ERR_NOT_INIT,
    ERR_IS_INIT,
    ERR_OUT_OF_MEMORY,
    ERR_NOT_FOUND,
    ERR_UNSUPPORTED,
    ERR_TIMEOUT,
    ERR_IO_ERROR,
    ERR_PARSE_FAILED,
    ERR_INTERNAL_ERROR
};

struct error_desc_t {
    u16 localCode;
    const char *pName;
    const char *pDescription;
};

struct error_table_t {
    domain_t domain;
    const error_desc_t *pErrors;
    u32 errorCount;
};

error_t Cy_ErrorMake( domain_t domain, u16 localErrorCode );

domain_t Cy_ErrorDomain( error_t error );

u16 Cy_ErrorLocalCode( error_t error );

bool_t Cy_ErrorSucceeded( error_t error );

bool_t Cy_ErrorFailed( error_t error );

bool_t Cy_ErrorSucceeded( common_error_t error );

bool_t Cy_ErrorFailed( common_error_t error );

const char *Cy_CommonErrorName( common_error_t error );

const char *Cy_CommonErrorDescription( common_error_t error );

const char *Cy_ErrorName( common_error_t error );

const char *Cy_ErrorDescription( common_error_t error );

const char *Cy_ErrorDomainName( domain_t domain );

const error_desc_t *Cy_ErrorFindDesc(
    const error_table_t &table,
    error_t error );

const char *Cy_ErrorFindName(
    const error_table_t &table,
    error_t error );

const char *Cy_ErrorFindDescription(
    const error_table_t &table,
    error_t error );

const error_table_t *Cy_CommonErrorTable();

} // namespace cypher::common

#define CY_ERROR( domain, code ) \
    CY_ERROR_##domain( code )

#define CY_ERROR_DOMAIN( error ) \
    cypher::common::Cy_ErrorDomain( error )

#define CY_ERROR_CODE( error ) \
    cypher::common::Cy_ErrorLocalCode( error )

#define CY_ERROR_SUCCEEDED( error ) \
    cypher::common::Cy_ErrorSucceeded( error )

#define CY_ERROR_FAILED( error ) \
    cypher::common::Cy_ErrorFailed( error )

#define CY_ERROR_COMMON( code ) \
    cypher::common::Cy_ErrorMake( \
        cypher::common::domain_t::COMMON, \
        static_cast<cypher::common::u16>( cypher::common::common_error_t::code ) )

#endif // CYPHER_COMMON_TIER0_ERROR_H
