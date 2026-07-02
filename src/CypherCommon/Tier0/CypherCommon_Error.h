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
    COM_DOMAIN_COMMON = 0u,
    COM_DOMAIN_MEMORY,
    COM_DOMAIN_LOG,
    COM_DOMAIN_SYSTEM,
    COM_DOMAIN_HOST,
    COM_DOMAIN_FILESYSTEM,
    COM_DOMAIN_PAK,
    COM_DOMAIN_COMMAND,
    COM_DOMAIN_CVAR,
    COM_DOMAIN_CONFIG,
    COM_DOMAIN_RENDER,
    COM_DOMAIN_AUDIO,
    COM_DOMAIN_INPUT,
    COM_DOMAIN_NETWORK,
    COM_DOMAIN_PHYSICS,
    COM_DOMAIN_GAME,
    COM_DOMAIN_EDITOR,
    COM_DOMAIN_TOOLS
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

error_t Cy_ErrorMake( domain_t domain, u16 localErrorCode );
domain_t Cy_ErrorDomain( error_t error );
u16 Cy_ErrorLocalCode( error_t error );
bool_t Cy_ErrorSucceeded( common_error_t error );
bool_t Cy_ErrorFailed( common_error_t error );
const char *Cy_ErrorName( common_error_t error );
const char *Cy_ErrorDescription( common_error_t error );
const char *Cy_ErrorDomainName( domain_t domain );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_ERROR_H
