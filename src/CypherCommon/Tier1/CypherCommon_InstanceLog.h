//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_InstanceLog.h
//  Purpose: Declares bounded per-instance diagnostic history.
//  Details: InstanceLog supplements the process logger for editor inspectors and asset
//           jobs by retaining a capped copy of recent records per object or operation.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_INSTANCELOG_H
#define CYPHER_COMMON_TIER1_INSTANCELOG_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_StringView.h"

namespace cypher::common
{

struct instance_log_record_t {
    u64 nTimestampTicks{ 0u };               // Monotonic timestamp captured on insertion.
    log_level_t level{ log_level_t::Info };  // Severity assigned by the producer.
    string_view_t category{};                // Borrowed view into log-owned text storage.
    string_view_t message{};                 // Borrowed view into log-owned text storage.
};

struct instance_log_desc_t {
    const allocator_t *pAllocator{ nullptr }; // Owns records and copied text.
    usize nMaxRecords{ 256u };                // Oldest records are evicted at this count.
    usize cbMaxText{ 256u * CY_KIB };         // Total retained category/message bytes.
};

// cbMaxText counts category and message payload bytes, excluding terminators.
// The log is instance-owned and not internally synchronized.

struct instance_log_t;

CYPHER_NODISCARD CYPHER_COMMON_API
instance_log_t *InstanceLog_Create(
    const instance_log_desc_t &desc ) noexcept;

CYPHER_COMMON_API void InstanceLog_Destroy( instance_log_t *pLog ) noexcept;
CYPHER_COMMON_API void InstanceLog_Clear( instance_log_t *pLog ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t InstanceLog_Add(
    instance_log_t *pLog,
    log_level_t level,
    string_view_t category,
    string_view_t message ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize InstanceLog_Count( const instance_log_t *pLog ) noexcept;

// Returned string views remain valid only until the next mutation of the log.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t InstanceLog_Record(
    const instance_log_t *pLog,
    usize iRecord,
    instance_log_record_t *pRecordOut ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_INSTANCELOG_H
