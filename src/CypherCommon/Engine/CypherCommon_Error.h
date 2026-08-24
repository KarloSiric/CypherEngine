//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Engine/CypherCommon_Error.h
//  Purpose: Declares the CypherCommon Error module.
//  Details: This file holds engine-scoped common metadata or helpers used across
//           runtime subsystems. Keep it narrow so it does not become a dumping
//           ground.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_COMMON_ERROR_H
#define CYPHER_ENGINE_COMMON_ERROR_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon.h"

namespace cypher::engine::common {

using error_t = u32; // Packed 16-bit domain and 16-bit local code.

/*
================
Common Error Encoding

Errors are packed as high 16 bits domain, low 16 bits subsystem-local code.
================
*/
enum class domain_t : u16 {
	COM_DOMAIN_COMMON = 0, // Shared engine/common failures.
	COM_DOMAIN_RENDER,     // Renderer and graphics-backend failures.
	COM_DOMAIN_LOG,        // Structured logging failures.
	COM_DOMAIN_HOST,       // Engine host lifecycle failures.
	COM_DOMAIN_SYS,        // Platform and operating-system failures.
	COM_DOMAIN_AUDIO,      // Audio runtime failures.
	COM_DOMAIN_FS,         // Filesystem and VFS failures.
	COM_DOMAIN_NET,        // Transport and protocol failures.
	COM_DOMAIN_GAME,       // Game-module failures.
	COM_DOMAIN_CMD,        // Console command failures.
	COM_DOMAIN_CVAR,       // Console-variable failures.
	COM_DOMAIN_CFG,        // Configuration processing failures.
    COM_DOMAIN_MEMORY,   // Allocator and memory-budget failures.
    COM_DOMAIN_PAK       // Package archive failures.
};

enum class common_error_t : u8 {
	OK = 0,               // Operation completed.

	ERR_FAILED,          // Unspecified operation failure.
	ERR_INVALID_ARGUMENT,// Caller supplied an invalid argument.
	ERR_INVALID_STATE,   // Object state does not permit the operation.
	ERR_INVALID_OPERATION,// Operation itself is not legal in this context.
	ERR_NOT_INIT,        // Required subsystem is not initialized.
	ERR_IS_INIT,         // Initialization was requested twice.
	ERR_OUT_OF_MEMORY,   // Required memory could not be obtained.
	ERR_NOT_FOUND,       // Requested object or resource does not exist.
	ERR_UNSUPPORTED,     // Feature is not supported by this implementation.
	ERR_IO_ERROR,        // External input/output operation failed.
	ERR_INTERNAL_ERROR   // Internal invariant or dependency failed.
};

/*
================
Common Error Helpers
================
*/
constexpr bool CypherCommon_ErrorOk( const common_error_t code ) {
	return code == common_error_t::OK;
}

constexpr bool CypherCommon_ErrorFailed( const common_error_t code ) {
	return code != common_error_t::OK;
}

constexpr inline const char *CypherCommon_ErrorName( const common_error_t code ) {
	switch ( code ) {
	case common_error_t::OK:
		return "OK";
	case common_error_t::ERR_FAILED:
		return "COM_FAILED";
	case common_error_t::ERR_INVALID_ARGUMENT:
		return "COM_INVALID_ARGUMENT";
	case common_error_t::ERR_INVALID_STATE:
		return "COM_INVALID_STATE";
	case common_error_t::ERR_INVALID_OPERATION:
		return "COM_INVALID_OPERATION";
	case common_error_t::ERR_NOT_INIT:
		return "COM_NOT_INITIALIZED";
	case common_error_t::ERR_IS_INIT:
		return "COM_ALREADY_INITIALIZED";
	case common_error_t::ERR_OUT_OF_MEMORY:
		return "COM_OUT_OF_MEMORY";
	case common_error_t::ERR_NOT_FOUND:
		return "NOT_FOUND";
	case common_error_t::ERR_UNSUPPORTED:
		return "COM_UNSUPPORTED";
	case common_error_t::ERR_IO_ERROR:
		return "IO_ERROR";
	case common_error_t::ERR_INTERNAL_ERROR:
		return "COM_INTERNAL_ERROR";
	default:
		return "COM_UNKNOWN_ERROR";
	}
}

constexpr inline const char *CypherCommon_DomainName( const domain_t domain ) {
	switch ( domain ) {
	case domain_t::COM_DOMAIN_HOST:
		return "HOST";
	case domain_t::COM_DOMAIN_GAME:
		return "GAME";
	case domain_t::COM_DOMAIN_SYS:
		return "SYS";
	case domain_t::COM_DOMAIN_AUDIO:
		return "AUDIO";
	case domain_t::COM_DOMAIN_COMMON:
		return "COMMON";
	case domain_t::COM_DOMAIN_LOG:
		return "LOG";
	case domain_t::COM_DOMAIN_RENDER:
		return "RENDER";
	case domain_t::COM_DOMAIN_FS:
		return "FS";
	case domain_t::COM_DOMAIN_NET:
		return "NET";
	case domain_t::COM_DOMAIN_CMD:
		return "CMD";
    case domain_t::COM_DOMAIN_CVAR:
        return "CVAR";
    case domain_t::COM_DOMAIN_CFG:
        return "CFG";
    case domain_t::COM_DOMAIN_MEMORY:
        return "MEMORY";
    case domain_t::COM_DOMAIN_PAK:
        return "PAK";
	default:
		return "UNKNOWN";
	}
}

constexpr inline error_t CypherCommon_ErrorMake( const domain_t domain, const u16 localErrorCode ) {
	return ( static_cast<error_t>( domain ) << 16u ) | static_cast<error_t>( localErrorCode );
}

constexpr inline domain_t CypherCommon_ErrorDomain( const error_t error ) {
	return static_cast<domain_t>( ( error >> 16u ) & 0xFFFFu );
}

constexpr inline u16 CypherCommon_ErrorCode( const error_t error ) {
	return static_cast<u16>( error & 0xFFFFu );
}

}

#endif // CYPHER_ENGINE_COMMON_ERROR_H
