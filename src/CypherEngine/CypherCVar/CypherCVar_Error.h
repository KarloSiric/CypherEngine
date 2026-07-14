//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherCVar/CypherCVar_Error.h
//  Purpose: Declares the CypherCVar CVar Error module.
//  Details: This file participates in console variable storage and runtime tuning.
//           Keep value ownership, defaults, and validation explicit so tools can
//           inspect and edit them safely.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_CVAR_ERROR_H
#define CYPHER_ENGINE_CVAR_ERROR_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon.h"
#include "CypherCommon_Error.h"

namespace cypher::engine::cvar {

/*
================
Cvar Error Codes
================
*/
enum class cvar_error_t : common::u8 {
	OK = 0,

	ERR_NOT_INIT,
	ERR_IS_INIT,
	ERR_INVALID_CVAR,
	ERR_INVALID_DEFAULT_VALUE,
	ERR_INVALID_FLAG,
	ERR_CVAR_ALREADY_EXISTS,
	ERR_CVAR_NOT_FOUND,
	ERR_REGISTRY_FULL,
	ERR_READONLY,
	ERR_CHEAT_PROTECTED
};

/*
================
Cvar Error Helpers
================
*/
constexpr inline const char *CypherCVar_ErrorName( const cvar_error_t error ) {
	switch ( error ) {
	case cvar_error_t::OK:
		return "OK";
	case cvar_error_t::ERR_NOT_INIT:
		return "ERR_NOT_INIT";
	case cvar_error_t::ERR_IS_INIT:
		return "ERR_IS_INIT";
	case cvar_error_t::ERR_INVALID_CVAR:
		return "ERR_INVALID_CVAR";
	case cvar_error_t::ERR_CVAR_ALREADY_EXISTS:
		return "ERR_CVAR_ALREADY_EXISTS";
	case cvar_error_t::ERR_CVAR_NOT_FOUND:
		return "ERR_CVAR_NOT_FOUND";
	case cvar_error_t::ERR_REGISTRY_FULL:
		return "ERR_REGISTRY_FULL";
	case cvar_error_t::ERR_INVALID_DEFAULT_VALUE:
		return "ERR_INVALID_DEFAULT_VALUE";
	case cvar_error_t::ERR_READONLY:
		return "ERR_READONLY";
	case cvar_error_t::ERR_CHEAT_PROTECTED:
		return "ERR_CHEAT_PROTECTED";
	case cvar_error_t::ERR_INVALID_FLAG:
		return "ERR_INVALID_FLAG";
	default:
		return "ERR_UNKNOWN";
	}
}

constexpr inline const char *CypherCVar_ErrorDesc( const cvar_error_t error ) {
	switch ( error ) {
	case cvar_error_t::OK:
		return "success";
	case cvar_error_t::ERR_NOT_INIT:
		return "cvar system is not initialized";
	case cvar_error_t::ERR_IS_INIT:
		return "cvar system is already initialized";
	case cvar_error_t::ERR_INVALID_CVAR:
		return "invalid cvar name or value was provided";
	case cvar_error_t::ERR_CVAR_ALREADY_EXISTS:
		return "cvar is already registered";
	case cvar_error_t::ERR_CVAR_NOT_FOUND:
		return "cvar was not found";
	case cvar_error_t::ERR_REGISTRY_FULL:
		return "cvar registry is full";
	case cvar_error_t::ERR_INVALID_DEFAULT_VALUE:
		return "invalid default cvar value";
	case cvar_error_t::ERR_READONLY:
		return "cvar is read-only";
	case cvar_error_t::ERR_CHEAT_PROTECTED:
		return "cvar is cheat-protected";
	case cvar_error_t::ERR_INVALID_FLAG:
		return "invalid cvar flags were provided";
	default:
		return "unknown cvar error";
	}
}

constexpr inline common::error_t CypherCVar_ErrorCode( cvar_error_t error ) {
	return common::CypherCommon_ErrorMake( common::domain_t::COM_DOMAIN_CVAR, static_cast<common::u16>( error ) );
}

}

#endif // CYPHER_ENGINE_CVAR_ERROR_H
