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
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Error

Common error packing and domain declarations. Subsystems still own their local
error enums, but this gives logs and diagnostics one shared encoded form.
================
*/

#include "CypherCommon_Annotations.h"
#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Defines.h"

namespace cypher::common
{

using error_code_t = u32;

enum class error_domain_t : u16 {
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
    COUNT,

    INVALID = CY_U16_MAX,

    // Compatibility names retained while existing subsystems migrate.
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
    COM_DOMAIN_TOOLS = TOOLS,
    COM_DOMAIN_ASSET = ASSET,
    COM_DOMAIN_RESOURCE = RESOURCE,
    COM_DOMAIN_WORLD = WORLD,
    COM_DOMAIN_ENTITY = ENTITY,
    COM_DOMAIN_ANIMATION = ANIMATION,
    COM_DOMAIN_AI = AI,
    COM_DOMAIN_SCRIPT = SCRIPT,
    COM_DOMAIN_MATERIAL = MATERIAL,
    COM_DOMAIN_TEXTURE = TEXTURE,
    COM_DOMAIN_GUI = GUI,
    COM_DOMAIN_JOB = JOB,
    COM_DOMAIN_SERIALIZATION = SERIALIZATION,
    COM_DOMAIN_REFLECTION = REFLECTION
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
    ERR_INTERNAL_ERROR,
    ERR_BUFFER_TOO_SMALL,
    ERR_ALREADY_EXISTS,
    ERR_BUSY,
    ERR_CANCELLED,
    ERR_OVERFLOW,
    ERR_PERMISSION_DENIED,
    ERR_END_OF_STREAM,
    ERR_CORRUPT_DATA,
    ERR_VERSION_MISMATCH,
    COUNT
};

struct error_description_t {
    u16 localCode;
    const char *pName;
    const char *pDescription;
};

struct error_table_t {
    error_domain_t domain;
    const error_description_t *pErrors;
    usize errorCount;
};

constexpr u32 CY_ERROR_DOMAIN_SHIFT = 16u;
constexpr error_code_t CY_ERROR_LOCAL_MASK = 0x0000FFFFu;
constexpr error_code_t CY_ERROR_DOMAIN_MASK = 0xFFFF0000u;
constexpr error_code_t CY_ERROR_OK = 0u;

// Packs one subsystem domain and one subsystem-local code into a stable value.
[[nodiscard]] constexpr error_code_t Cy_ErrorMake( error_domain_t domain, u16 localErrorCode ) noexcept
{
    return ( static_cast<error_code_t>( domain ) << CY_ERROR_DOMAIN_SHIFT ) |
           static_cast<error_code_t>( localErrorCode );
}

// Packs a Common-domain error.
[[nodiscard]] constexpr error_code_t Cy_ErrorMake( common_error_t error ) noexcept
{
    return Cy_ErrorMake( error_domain_t::COMMON, static_cast<u16>( error ) );
}

// Extracts the subsystem domain from a packed error.
[[nodiscard]] constexpr error_domain_t Cy_ErrorDomain( error_code_t errorCode ) noexcept
{
    return static_cast<error_domain_t>(
        static_cast<u16>( ( errorCode & CY_ERROR_DOMAIN_MASK ) >> CY_ERROR_DOMAIN_SHIFT ) );
}

// Extracts the subsystem-local code from a packed error.
[[nodiscard]] constexpr u16 Cy_ErrorLocalCode( error_code_t errorCode ) noexcept
{
    return static_cast<u16>( errorCode & CY_ERROR_LOCAL_MASK );
}

// A zero local code means success in every registered subsystem domain.
[[nodiscard]] constexpr bool_t Cy_ErrorSucceeded( error_code_t errorCode ) noexcept
{
    return Cy_ErrorLocalCode( errorCode ) == 0u;
}

[[nodiscard]] constexpr bool_t Cy_ErrorFailed( error_code_t errorCode ) noexcept
{
    return !Cy_ErrorSucceeded( errorCode );
}

[[nodiscard]] constexpr bool_t Cy_ErrorSucceeded( common_error_t error ) noexcept
{
    return error == common_error_t::OK;
}

[[nodiscard]] constexpr bool_t Cy_ErrorFailed( common_error_t error ) noexcept
{
    return !Cy_ErrorSucceeded( error );
}

[[nodiscard]] CYPHER_COMMON_API CY_RETURNS_NONNULL const char *Cy_CommonErrorName( common_error_t error ) noexcept;

[[nodiscard]] CYPHER_COMMON_API CY_RETURNS_NONNULL const char *Cy_CommonErrorDescription( common_error_t error ) noexcept;

// Compatibility aliases for the Common-domain lookup API.
[[nodiscard]] CYPHER_COMMON_API CY_RETURNS_NONNULL const char *Cy_ErrorName( common_error_t error ) noexcept;
[[nodiscard]] CYPHER_COMMON_API CY_RETURNS_NONNULL const char *Cy_ErrorDescription( common_error_t error ) noexcept;

[[nodiscard]] CYPHER_COMMON_API CY_RETURNS_NONNULL const char *Cy_ErrorDomainName( error_domain_t domain ) noexcept;

[[nodiscard]] CYPHER_COMMON_API const error_description_t *Cy_ErrorFindDesc(
    const error_table_t &table,
    error_code_t errorCode ) noexcept;

[[nodiscard]] CYPHER_COMMON_API CY_RETURNS_NONNULL const char *Cy_ErrorFindName(
    const error_table_t &table,
    error_code_t errorCode ) noexcept;

[[nodiscard]] CYPHER_COMMON_API CY_RETURNS_NONNULL const char *Cy_ErrorFindDescription(
    const error_table_t &table,
    error_code_t errorCode ) noexcept;

[[nodiscard]] CYPHER_COMMON_API CY_RETURNS_NONNULL const error_table_t *Cy_CommonErrorTable() noexcept;

} // namespace cypher::common

#define CY_ERROR( domain, code )                                                     \
    ::cypher::common::Cy_ErrorMake(                                                  \
        ::cypher::common::error_domain_t::domain,                                    \
        static_cast<::cypher::common::u16>( code ) )

#define CY_ERROR_DOMAIN( errorCode ) \
    ::cypher::common::Cy_ErrorDomain( errorCode )

#define CY_ERROR_CODE( errorCode ) \
    ::cypher::common::Cy_ErrorLocalCode( errorCode )

#define CY_ERROR_SUCCEEDED( errorCode ) \
    ::cypher::common::Cy_ErrorSucceeded( errorCode )

#define CY_ERROR_FAILED( errorCode ) \
    ::cypher::common::Cy_ErrorFailed( errorCode )

#define CY_ERROR_COMMON( code ) \
    ::cypher::common::Cy_ErrorMake( ::cypher::common::common_error_t::code )

#endif // CYPHER_COMMON_TIER0_ERROR_H
