//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Validator.cpp
//  Purpose: Implements CypherCommon Tier0 validation callbacks.
//  Details: Validators provide a tiny reporting hook for tests and debug builds
//           without forcing a dependency on full logging.
//
//  History:
//  - Created by Karlo Siric on 2026-07-07
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_Validator.h"

#include <cstdio>
#include <mutex>

namespace cypher::common
{
namespace
{

// Validator callback replacement is rare. Snapshot the callback/context pair under
// one mutex and use a thread-local depth guard for callbacks that validate again.
std::mutex g_validatorMutex;
validator_callback_t g_validatorCallback = nullptr;
void *g_validatorUserData = nullptr;
thread_local u32 g_validatorCallbackDepth = 0u;

} // namespace

void Cy_ValidatorSetCallback( validator_callback_t pCallback, void *pUserData ) noexcept
{
    std::lock_guard<std::mutex> lock( g_validatorMutex );
    g_validatorCallback = pCallback;
    g_validatorUserData = pUserData;
}

void Cy_ValidatorGetCallback(
    validator_callback_t *pCallbackOut,
    void **ppUserDataOut ) noexcept
{
    std::lock_guard<std::mutex> lock( g_validatorMutex );
    if ( pCallbackOut != nullptr ) {
        *pCallbackOut = g_validatorCallback;
    }
    if ( ppUserDataOut != nullptr ) {
        *ppUserDataOut = g_validatorUserData;
    }
}

void Cy_ValidatorReport( validator_severity_t severity, const char *pMessage ) noexcept
{
    Cy_ValidatorReportAt( severity, CY_ERROR_OK, pMessage, {} );
}

void Cy_ValidatorReportAt(
    validator_severity_t severity,
    error_code_t errorCode,
    const char *pMessage,
    source_location_t location ) noexcept
{
    validation_record_t record{};
    record.severity = severity;
    record.errorCode = errorCode;
    record.location = location;
    record.pMessage = pMessage != nullptr ? pMessage : "";

    validator_callback_t pCallback = nullptr;
    void *pUserData = nullptr;
    {
        // Never call external code while holding the process-wide callback lock.
        std::lock_guard<std::mutex> lock( g_validatorMutex );
        pCallback = g_validatorCallback;
        pUserData = g_validatorUserData;
    }

    if ( pCallback != nullptr && g_validatorCallbackDepth == 0u ) {
        ++g_validatorCallbackDepth;
        pCallback( record, pUserData );
        --g_validatorCallbackDepth;
        return;
    }

    // Missing or recursive callbacks fall back to stderr so validation failures
    // remain visible during early startup and callback failure.
    if ( errorCode != CY_ERROR_OK ) {
        std::fprintf(
            stderr,
            "[Validator:%s][%s:%u] %s\n",
            Cy_ValidatorSeverityName( severity ),
            Cy_ErrorDomainName( Cy_ErrorDomain( errorCode ) ),
            static_cast<unsigned int>( Cy_ErrorLocalCode( errorCode ) ),
            record.pMessage );
    } else {
        std::fprintf(
            stderr,
            "[Validator:%s] %s\n",
            Cy_ValidatorSeverityName( severity ),
            record.pMessage );
    }
    std::fflush( stderr );
}

const char *Cy_ValidatorSeverityName( validator_severity_t severity ) noexcept
{
    switch ( severity ) {
        case validator_severity_t::Info: return "Info";
        case validator_severity_t::Warning: return "Warning";
        case validator_severity_t::Error: return "Error";
        case validator_severity_t::Fatal: return "Fatal";
        case validator_severity_t::Count:
            break;
    }

    return "Unknown";
}

} // namespace cypher::common
