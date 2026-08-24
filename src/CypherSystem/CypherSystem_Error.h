//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherSystem/CypherSystem_Error.h
//  Purpose: Declares the CypherSystem System Error module.
//  Details: This file owns platform-facing system, window, and graphics context
//           boundaries. Keep OS-specific code isolated enough that higher-level
//           runtime code remains portable.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_SYSTEM_ERROR_H
#define CYPHER_ENGINE_SYSTEM_ERROR_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon_Error.h"

namespace cypher::engine::sys {

/*
================
System Error Codes
================
*/
enum class sys_error_t : common::u8 {
		OK = 0,                        // Operation completed successfully.

		ERR_NOT_INIT,                  // Platform services are not initialized.
		ERR_IS_INIT,                   // Initialization was requested for live state.

		ERR_INVALID_ARGUMENT,          // Caller supplied an invalid pointer, size, or combination.
		ERR_INVALID_PATH,              // Path is empty, malformed, or outside policy.

		ERR_UNSUPPORTED_PLATFORM,      // Build or runtime platform has no implementation.
		ERR_UNSUPPORTED_COMPILER,      // Compiler toolchain has no supported configuration.

		ERR_PATH_QUERY_FAILED,         // Operating system could not provide a required path.
		ERR_PATH_TOO_LONG,             // Platform path exceeds fixed runtime storage.
		ERR_DIRECTORY_CREATE_FAILED,   // Required writable directory could not be created.

		ERR_TIME_UNAVAILABLE,          // Monotonic platform clock could not be queried.
		ERR_LOCALTIME_FAILED,          // Calendar-time conversion failed.

		ERR_INTERNAL_ERROR             // Platform invariant failed without a specific code.
};

/*
================
System Error Helpers
================
*/
constexpr inline const char *CypherSystem_ErrorName( const sys_error_t error ) {
    switch ( error ) {
    case sys_error_t::OK:
        return "OK";
    case sys_error_t::ERR_NOT_INIT:
        return "ERR_NOT_INIT";
    case sys_error_t::ERR_IS_INIT:
        return "ERR_IS_INIT";
    case sys_error_t::ERR_INVALID_ARGUMENT:
        return "ERR_INVALID_ARGUMENT";
    case sys_error_t::ERR_INVALID_PATH:
        return "ERR_INVALID_PATH";
    case sys_error_t::ERR_UNSUPPORTED_PLATFORM:
        return "ERR_UNSUPPORTED_PLATFORM";
    case sys_error_t::ERR_UNSUPPORTED_COMPILER:
        return "ERR_UNSUPPORTED_COMPILER";
    case sys_error_t::ERR_PATH_QUERY_FAILED:
        return "ERR_PATH_QUERY_FAILED";
    case sys_error_t::ERR_PATH_TOO_LONG:
        return "ERR_PATH_TOO_LONG";
    case sys_error_t::ERR_DIRECTORY_CREATE_FAILED:
        return "ERR_DIRECTORY_CREATE_FAILED";
    case sys_error_t::ERR_TIME_UNAVAILABLE:
        return "ERR_TIME_UNAVAILABLE";
    case sys_error_t::ERR_LOCALTIME_FAILED:
        return "ERR_LOCALTIME_FAILED";
    case sys_error_t::ERR_INTERNAL_ERROR:
        return "ERR_INTERNAL_ERROR";
    default:
        return "ERR_UNKNOWN";
    }
}

constexpr inline const char *CypherSystem_ErrorDesc( const sys_error_t error ) {
    switch ( error ) {
    case sys_error_t::OK:
        return "operation completed successfully";
    case sys_error_t::ERR_NOT_INIT:
        return "sys subsystem is not initialized";
    case sys_error_t::ERR_IS_INIT:
        return "sys subsystem is already initialized";
    case sys_error_t::ERR_INVALID_ARGUMENT:
        return "invalid sys argument";
    case sys_error_t::ERR_INVALID_PATH:
        return "invalid sys path";
    case sys_error_t::ERR_UNSUPPORTED_PLATFORM:
        return "unsupported platform";
    case sys_error_t::ERR_UNSUPPORTED_COMPILER:
        return "unsupported compiler";
    case sys_error_t::ERR_PATH_QUERY_FAILED:
        return "failed to query platform path";
    case sys_error_t::ERR_PATH_TOO_LONG:
        return "platform path is too long";
    case sys_error_t::ERR_DIRECTORY_CREATE_FAILED:
        return "failed to create platform directory";
    case sys_error_t::ERR_TIME_UNAVAILABLE:
        return "platform time source is unavailable";
    case sys_error_t::ERR_LOCALTIME_FAILED:
        return "failed to convert platform local time";
    case sys_error_t::ERR_INTERNAL_ERROR:
        return "internal sys error";
    default:
        return "unknown sys error";
    }
}

constexpr inline common::error_t CypherSystem_ErrorCode( sys_error_t error ) {
	return common::CypherCommon_ErrorMake( common::domain_t::COM_DOMAIN_SYS, static_cast<common::u16>( error ) );
}

}       // namespace cypher::engine::sys

#endif // CYPHER_ENGINE_SYSTEM_ERROR_H
