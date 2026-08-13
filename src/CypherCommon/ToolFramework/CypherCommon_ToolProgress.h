//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/ToolFramework/CypherCommon_ToolProgress.h
//  Purpose: Declares presentation-neutral hierarchical progress events.
//  Details: Producers emit structured state; terminal, Qt, JSON, and test hosts
//           independently decide how that state is displayed or retained.
//
//  History:
//  - Created by Karlo Siric on 2026-08-12
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TOOLFRAMEWORK_TOOLPROGRESS_H
#define CYPHER_COMMON_TOOLFRAMEWORK_TOOLPROGRESS_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_ToolStatus.h"
#include "CypherCommon_ToolTypes.h"
#include "CypherCommon_StringView.h"
#include "CypherCommon_Timer.h"

namespace cypher::common
{

enum class tool_progress_state_t : u8 {
    BEGIN = 0u,
    UPDATE,
    COMPLETE,
    FAILED,
    CANCELLED
};

enum class tool_progress_unit_t : u8 {
    NONE = 0u,
    ITEMS,
    BYTES,
    STEPS
};

enum tool_progress_flags_t : flags32_t {
    TOOL_PROGRESS_FLAG_NONE = 0u,
    TOOL_PROGRESS_FLAG_INDETERMINATE = CYPHER_BIT32( 0 )
};

struct tool_progress_t {
    tool_operation_id_t operationId{ CY_TOOL_INVALID_OPERATION_ID };
    tool_operation_id_t parentOperationId{ CY_TOOL_INVALID_OPERATION_ID };
    tool_sequence_t sequence{ CY_TOOL_INVALID_SEQUENCE };
    tool_progress_state_t state{ tool_progress_state_t::BEGIN };
    tool_progress_unit_t unit{ tool_progress_unit_t::NONE };
    tool_status_t status{ tool_status_t::OK };
    u64 nCompleted{ 0u };
    u64 nTotal{ 0u };
    timer_tick_t timestamp{ 0u };
    string_view_t title{};
    string_view_t detail{};
    flags32_t flags{ TOOL_PROGRESS_FLAG_NONE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
tool_status_t ToolProgress_Validate( const tool_progress_t &progress ) noexcept;

// Returns a [0,1] fraction, or -1 when the operation is indeterminate.
CYPHER_NODISCARD CYPHER_COMMON_API
f64 ToolProgress_Fraction( const tool_progress_t &progress ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolProgress_StateName( tool_progress_state_t state ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *ToolProgress_UnitName( tool_progress_unit_t unit ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TOOLFRAMEWORK_TOOLPROGRESS_H
