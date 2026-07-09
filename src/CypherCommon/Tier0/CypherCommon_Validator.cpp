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

std::mutex g_validatorMutex;
validator_callback_t g_validatorCallback = nullptr;

} // namespace

void Validator_SetCallback( validator_callback_t callback )
{
    std::lock_guard<std::mutex> lock( g_validatorMutex );
    g_validatorCallback = callback;
}

void Validator_Report( validator_severity_t severity, const char *pMessage )
{
    std::lock_guard<std::mutex> lock( g_validatorMutex );
    if ( g_validatorCallback != nullptr ) {
        g_validatorCallback( severity, pMessage );
        return;
    }

    std::fprintf( stderr, "[Validator:%u] %s\n", static_cast<u32>( severity ), pMessage != nullptr ? pMessage : "" );
}

} // namespace cypher::common
