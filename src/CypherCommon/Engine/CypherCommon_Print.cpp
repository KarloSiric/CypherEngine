//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Engine/CypherCommon_Print.cpp
//  Purpose: Implements the CypherCommon Print module.
//  Details: This file holds engine-scoped common metadata or helpers used across
//           runtime subsystems. Keep it narrow so it does not become a dumping
//           ground.
//
//  History:
//  - Created by Karlo Siric on 2026-04-21
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Print.h"
#include "CypherLog.h"

#include <cstdarg>     // va_list handling.
#include <cstdio>      // stdio output and formatting.
#include <ctime>       // std::time for common error log records.

namespace cypher::engine::common
{

namespace {

/*
================
CypherCommon_LogChannelForDomain
================
*/
log::channel_t CypherCommon_LogChannelForDomain( const domain_t domain )
{
    switch ( domain ) {
        case domain_t::COM_DOMAIN_RENDER: return log::channel_t::RENDER;
        case domain_t::COM_DOMAIN_LOG:    return log::channel_t::CORE;
        case domain_t::COM_DOMAIN_HOST:   return log::channel_t::HOST;
        case domain_t::COM_DOMAIN_SYS:    return log::channel_t::SYSTEM;
        case domain_t::COM_DOMAIN_AUDIO:  return log::channel_t::AUDIO;
        case domain_t::COM_DOMAIN_FS:     return log::channel_t::FS;
        case domain_t::COM_DOMAIN_NET:    return log::channel_t::NET;
        case domain_t::COM_DOMAIN_GAME:   return log::channel_t::GAME;
        case domain_t::COM_DOMAIN_CMD:    return log::channel_t::CMD;
        case domain_t::COM_DOMAIN_CVAR:   return log::channel_t::CVAR;
        case domain_t::COM_DOMAIN_CFG:    return log::channel_t::CFG;
        case domain_t::COM_DOMAIN_MEMORY: return log::channel_t::MEMORY;
        case domain_t::COM_DOMAIN_COMMON: return log::channel_t::CORE;
        default:                          return log::channel_t::CORE;
    }
}

}       // namespace

/*
================
CypherCommon_Printf
================
*/
void CypherCommon_Printf( const char *message, ... ) {
    va_list args;
    va_start( args, message );
    CypherCommon_VPrintf( message, args );
    va_end( args );
}

/*
================
CypherCommon_VPrintf

Formats normal engine text and writes it to stdout.
================
*/
void CypherCommon_VPrintf( const char *message, va_list args ) {
    char msgBuf[COM_MSG_MAX]{};
    const char *bSafeMessage = message ? message : "<null message>";
    std::vsnprintf( msgBuf, sizeof( msgBuf ), bSafeMessage, args );

    // Later this should route through registered print sinks.
    std::fputs( msgBuf, stdout );
    std::fflush( stdout );
}

/*
================
CypherCommon_DPrintf

Developer-print hook; later this should respect the developer cvar.
================
*/
void CypherCommon_DPrintf( const char *message, ... ) {
    // Developer-only printing will later be gated by the developer cvar.

    va_list args;
    va_start( args, message );
    CypherCommon_VPrintf( message, args );
    va_end( args );
}

/*
================
CypherCommon_Errorf
================
*/
void CypherCommon_Errorf( const error_t error, const char *message, ... ) {
    va_list args;
    va_start( args, message );
    CypherCommon_VErrorf( error, message, args );
    va_end( args );
}

/*
================
CypherCommon_VErrorf

Formats a domain-coded engine error and writes it to stderr.
================
*/
void CypherCommon_VErrorf( const error_t error, const char *message, va_list args ) {
    char msgBuf[COM_MSG_MAX]{};
    char msgFinal[COM_MSG_MAX + 256]{};
    const char *bSafeFormat = message ? message : "<null error message>";
    std::vsnprintf( msgBuf, sizeof( msgBuf ), bSafeFormat, args );
    const domain_t domain = CypherCommon_ErrorDomain( error );

    if ( log::Log_IsInitialized() ) {
        log::record_t record{};
        record.level = log::level_t::ERR;
        record.channel = CypherCommon_LogChannelForDomain( domain );
        record.nSinkMask = log::Log_DefaultSinkMaskForLevel( record.level );
        record.file = "";
        record.function = "";
        record.line = 0;
        record.timestamp = std::time( nullptr );

        std::snprintf(
            record.message,
            sizeof( record.message ),
            "[0x%08X] %s",
            static_cast<unsigned int>( error ),
            msgBuf
        );

        log::Log_Emit( record );
        return;
    }

    std::snprintf(msgFinal,
                  sizeof( msgFinal ),
                  "[ERROR][%s][0x%08X] %s\n",
                  CypherCommon_DomainName( domain ),
                  static_cast<unsigned int>( error ),
                  msgBuf
                  );

    std::fputs( msgFinal, stderr );
    std::fflush( stderr );
}

}       // namespace cypher::engine::common
