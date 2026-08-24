//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolTypes.h
//  Purpose: Declares identifiers shared by Cypher tool-framework contracts.
//  Details: These scalar IDs cross CLI, GUI, test, and service hosts without
//           exposing application-specific objects or presentation types.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

/*
================
Tool Types Contract

These are stable tool-neutral contracts shared by CLI applications, future GUI hosts, tests, and
compiler modules. They must not depend on Qt or terminal state.
================
*/

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLTYPES_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLTYPES_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_BaseTypes.h"

namespace cypher::common
{

using tool_operation_id_t = u64;      // Correlates records from one logical operation.
using tool_sequence_t = u64;          // Orders records emitted by a producer.
using tool_diagnostic_code_t = u32;   // Stable machine-readable diagnostic identifier.

inline constexpr tool_operation_id_t CY_TOOL_INVALID_OPERATION_ID = 0u; // Reserved invalid operation.
inline constexpr tool_sequence_t CY_TOOL_INVALID_SEQUENCE = 0u;         // Reserved invalid sequence.
inline constexpr tool_diagnostic_code_t CY_TOOL_DIAGNOSTIC_NONE = 0u;   // Reserved no-diagnostic code.

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLTYPES_H
