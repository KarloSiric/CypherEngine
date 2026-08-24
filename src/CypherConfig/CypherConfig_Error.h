//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherConfig/CypherConfig_Error.h
//  Purpose: Declares the CypherConfig Config Error module.
//  Details: This file participates in configuration loading and runtime settings.
//           Keep file format handling strict and predictable so startup failures are
//           easy to diagnose.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_CONFIG_ERROR_H
#define CYPHER_ENGINE_CONFIG_ERROR_H


#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "Engine/CypherCommon.h"
#include "Engine/CypherCommon_Error.h"

namespace cypher::engine::cfg {

/*
================
Config Error Codes
================
*/
enum class cfg_error_t : common::u8 {
		OK = 0,                  // Operation completed successfully.
		ERR_NOT_INIT,            // Config service is not initialized.
		ERR_IS_INIT,             // Initialization was requested for live state.
		ERR_INVALID_PATH,        // Config virtual path is empty or malformed.
		ERR_INVALID_LINE,        // One line exceeds syntax or storage policy.
		ERR_FILE_OPEN_FAILED,    // Config source could not be opened.
		ERR_PARSE_FAILED,        // One or more lines could not be tokenized.
		ERR_COMMAND_FAILED,      // Parsed command or CVar assignment failed to execute.
		ERR_IO_ERROR             // Read failed after the file was opened.
};

/*
================
Config Error Helpers
================
*/
constexpr inline const char *CypherConfig_ErrorName( const cfg_error_t error ) {
	switch ( error ) {
	case cfg_error_t::OK:
		return "OK";
	case cfg_error_t::ERR_NOT_INIT:
		return "ERR_NOT_INIT";
	case cfg_error_t::ERR_IS_INIT:
		return "ERR_IS_INIT";
	case cfg_error_t::ERR_INVALID_PATH:
		return "ERR_INVALID_PATH";
	case cfg_error_t::ERR_INVALID_LINE:
		return "ERR_INVALID_LINE";
	case cfg_error_t::ERR_FILE_OPEN_FAILED:
		return "ERR_FILE_OPEN_FAILED";
	case cfg_error_t::ERR_PARSE_FAILED:
		return "ERR_PARSE_FAILED";
	case cfg_error_t::ERR_COMMAND_FAILED:
		return "ERR_COMMAND_FAILED";
	case cfg_error_t::ERR_IO_ERROR:
		return "ERR_IO_ERROR";
	default:
		return "ERR_UNKNOWN";
	}
}

constexpr inline const char *CypherConfig_ErrorDesc( const cfg_error_t error ) {
	switch ( error ) {
	case cfg_error_t::OK:
		return "operation completed successfully";
	case cfg_error_t::ERR_NOT_INIT:
		return "cfg subsystem is not initialized";
	case cfg_error_t::ERR_IS_INIT:
		return "cfg subsystem is already initialized";
	case cfg_error_t::ERR_INVALID_PATH:
		return "invalid cfg path";
	case cfg_error_t::ERR_INVALID_LINE:
		return "invalid cfg line";
	case cfg_error_t::ERR_FILE_OPEN_FAILED:
		return "unable to open cfg file";
	case cfg_error_t::ERR_PARSE_FAILED:
		return "one or more cfg lines failed to parse";
	case cfg_error_t::ERR_COMMAND_FAILED:
		return "command execution failed";
	case cfg_error_t::ERR_IO_ERROR:
		return "cfg IO error";
	default:
		return "unknown cfg error";
	}
}

constexpr inline common::error_t CypherConfig_ErrorCode( const cfg_error_t error ) {
	return common::CypherCommon_ErrorMake( common::domain_t::COM_DOMAIN_CFG, static_cast<common::u16>( error ) );
}

}

#endif // CYPHER_ENGINE_CONFIG_ERROR_H
