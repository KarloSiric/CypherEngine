//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_SequenceNumber.h
//  Purpose: Declares wrap-aware packet sequence comparison and acknowledgement state.
//  Details: Comparisons are defined only within half the sequence space, matching the
//           normal bounded-window assumption of real-time UDP protocols.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_SEQUENCENUMBER_H
#define CYPHER_COMMON_TIER1_SEQUENCENUMBER_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

struct sequence_ack32_t {
    u32 nLatest{ 0u };
    u32 ackBits{ 0u };
    bool_t bInitialized{ CY_FALSE };
};

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Sequence16_IsNewer( u16 left, u16 right ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t Sequence32_IsNewer( u32 left, u32 right ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
i32 Sequence16_Distance( u16 from, u16 to ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
i64 Sequence32_Distance( u32 from, u32 to ) noexcept;

CYPHER_COMMON_API void SequenceAck32_Reset( sequence_ack32_t *pState ) noexcept;

// Returns true only when sequence was not already represented by the window.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t SequenceAck32_Record(
    sequence_ack32_t *pState,
    u32 nSequence ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t SequenceAck32_Contains(
    const sequence_ack32_t *pState,
    u32 nSequence ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_SEQUENCENUMBER_H
