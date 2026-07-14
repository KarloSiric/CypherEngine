//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherLog/CypherLog_Error.h
//  Purpose: Declares the CypherLog Log Error module.
//  Details: This file participates in engine logging and formatted diagnostic output.
//           Keep it usable from early startup and failure paths without introducing
//           fragile dependencies.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_LOG_ERROR_H
#define CYPHER_ENGINE_LOG_ERROR_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Error.h"

namespace cypher::engine::log
{

/*
================
Log Error Codes
================
*/
enum class log_error_t : common::u8 {
      OK = 0,

      ERR_NOT_INIT,
      ERR_IS_INIT,
      ERR_INVALID_CONFIG,
      ERR_FILE_OPEN_FAILED,
      ERR_FILE_WRITE_FAILED,
      ERR_FORMAT_FAILED,
      ERR_INVALID_CHANNEL,
      ERR_INVALID_LEVEL
};

/*
================
Log Error Helpers
================
*/
constexpr inline const char *CypherLog_ErrorName( const log_error_t error ) {
    switch ( error ) {
    case log_error_t::OK:
        return "OK";
    case log_error_t::ERR_NOT_INIT:
        return "ERR_NOT_INIT";
    case log_error_t::ERR_IS_INIT:
        return "ERR_IS_INIT";
    case log_error_t::ERR_INVALID_CONFIG:
        return "ERR_INVALID_CONFIG";
    case log_error_t::ERR_FILE_OPEN_FAILED:
        return "ERR_FILE_OPEN_FAILED";
    case log_error_t::ERR_FILE_WRITE_FAILED:
        return "ERR_FILE_WRITE_FAILED";
    case log_error_t::ERR_FORMAT_FAILED:
        return "ERR_FORMAT_FAILED";
    case log_error_t::ERR_INVALID_CHANNEL:
        return "ERR_INVALID_CHANNEL";
    case log_error_t::ERR_INVALID_LEVEL:
        return "ERR_INVALID_LEVEL";
    default:
        return "ERR_UNKNOWN";
    }
}

constexpr inline const char *CypherLog_ErrorDesc( const log_error_t error ) {
    switch ( error ) {
    case log_error_t::OK:
        return "operation completed successfully";
    case log_error_t::ERR_NOT_INIT:
        return "log subsystem is not initialized";
    case log_error_t::ERR_IS_INIT:
        return "log subsystem is already initialized";
    case log_error_t::ERR_INVALID_CONFIG:
        return "invalid log configuration";
    case log_error_t::ERR_FILE_OPEN_FAILED:
        return "log subsystem file open failure";
    case log_error_t::ERR_FILE_WRITE_FAILED:
        return "log subsystem file write failure";
    case log_error_t::ERR_FORMAT_FAILED:
        return "log subsystem formatting failure";
    case log_error_t::ERR_INVALID_CHANNEL:
        return "log subsystem invalid channel";
    case log_error_t::ERR_INVALID_LEVEL:
        return "log subsystem invalid level";
    default:
        return "unknown log error";
    }
}

constexpr inline common::error_t CypherLog_ErrorCode( log_error_t error ) {
    return common::CypherCommon_ErrorMake( common::domain_t::COM_DOMAIN_LOG, static_cast<common::u16>( error ) );
}

}

#endif // CYPHER_ENGINE_LOG_ERROR_H
