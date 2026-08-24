//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Validator.h
//  Purpose: Reports recoverable data-validation issues through a shared callback.
//  Details: Validators continue after reporting; programmer invariants belong to
//           Assert and unrecoverable process failures belong to Crash.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER0_VALIDATOR_H
#define CYPHER_COMMON_TIER0_VALIDATOR_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

/*
================
CypherCommon Validator

Recoverable data and tool validation reporting. Validators collect issues and
continue; programmer invariants belong to Assert and fatal failures to Crash.
================
*/

#include "CypherCommon_API.h"
#include "CypherCommon_BaseTypes.h"
#include "CypherCommon_Error.h"
#include "CypherCommon_SourceLocation.h"

namespace cypher::common
{

enum class validator_severity_t : u32 {
    Info = 0u,
    Warning,
    Error,
    Fatal,
    Count // Sentinel; never submitted in a valid record.
};

struct validation_record_t {
    validator_severity_t severity{ validator_severity_t::Info }; // Presentation and build-failure policy.
    error_code_t errorCode{ CY_ERROR_OK };                       // Optional packed machine-readable cause.
    source_location_t location{};                               // Reporter call site, not authored-file position.
    const char *pMessage{ "" };                                 // Borrowed during the callback only.
};

using validator_callback_t = void ( * )(
    const validation_record_t &record,
    void *pUserData ) noexcept; // Opaque value installed with the process-wide callback.

// Callbacks may execute concurrently. Replacement does not drain calls already
// in flight, so callback code and user data must outlive active producers.
CYPHER_COMMON_API void Cy_ValidatorSetCallback(
    validator_callback_t pCallback,
    void *pUserData ) noexcept;

CYPHER_COMMON_API void Cy_ValidatorGetCallback(
    validator_callback_t *pCallbackOut,
    void **ppUserDataOut ) noexcept;

CYPHER_COMMON_API void Cy_ValidatorReport(
    validator_severity_t severity,
    const char *pMessage ) noexcept;

CYPHER_COMMON_API void Cy_ValidatorReportAt(
    validator_severity_t severity,
    error_code_t errorCode,
    const char *pMessage,
    source_location_t location ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL const char *Cy_ValidatorSeverityName(
    validator_severity_t severity ) noexcept;

} // namespace cypher::common

#define CY_VALIDATE_REPORT( severity, errorCode, message )                                \
    ::cypher::common::Cy_ValidatorReportAt(                                               \
        ::cypher::common::validator_severity_t::severity,                                 \
        errorCode,                                                                        \
        message,                                                                          \
        CY_SOURCE_LOCATION )

#endif // CYPHER_COMMON_TIER0_VALIDATOR_H
