//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier0/CypherCommon_Validator.h
//  Purpose: Declares CypherCommon Tier0 Validator support.
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

#ifndef CYPHER_COMMON_TIER0_VALIDATOR_H
#define CYPHER_COMMON_TIER0_VALIDATOR_H
#pragma once

/*
================
CypherCommon Validator

Runtime validation declarations used by debug builds and tests.
================
*/

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

enum class validator_severity_t : u32 {
    Info = 0u,
    Warning,
    Error,
    Fatal
};

using validator_callback_t = void ( * )( validator_severity_t severity, const char *pMessage );

void Validator_SetCallback( validator_callback_t callback );
void Validator_Report( validator_severity_t severity, const char *pMessage );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER0_VALIDATOR_H
