//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Event.h
//  Purpose: Declares a synchronous application event bus.
//  Details: This event bus is unrelated to the Tier0 thread-wait event. Emission is
//           synchronous, callbacks run on the caller thread, and the bus is not thread-safe.
//           Higher priorities run first and equal priorities preserve subscription order.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_EVENT_H
#define CYPHER_COMMON_TIER1_EVENT_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Allocator.h"
#include "CypherCommon_BinaryBlock.h"

namespace cypher::common
{

using event_id_t = u64;
using event_subscription_t = handle32_t;
inline constexpr event_subscription_t CY_EVENT_SUBSCRIPTION_INVALID =
    CY_HANDLE32_INVALID;

struct event_payload_t {
    // Borrowed bytes remain valid only for the synchronous EventBus_Emit call.
    binary_block_t data{};
    u64 nSenderId{ 0u };
};

enum event_subscription_flags_t : flags32_t {
    EVENT_SUBSCRIPTION_FLAG_NONE  = 0u,
    EVENT_SUBSCRIPTION_FLAG_ONCE  = CYPHER_BIT32( 0 )
};

// Callback and user data must remain valid until unsubscribed or the bus is destroyed.
using event_callback_t = void ( * )(
    event_id_t eventId,
    const event_payload_t &payload,
    void *pUserData ) noexcept;

struct event_bus_desc_t {
    const allocator_t *pAllocator{ nullptr };
    usize nInitialSubscriptions{ 128u };
};

struct event_bus_t;

CYPHER_NODISCARD CYPHER_COMMON_API
event_bus_t *EventBus_Create( const event_bus_desc_t &desc ) noexcept;

CYPHER_COMMON_API void EventBus_Destroy( event_bus_t *pBus ) noexcept;

// Clear and unsubscribe are permitted during emission and cancel matching callbacks
// that have not yet run. Destroy and nested emission are forbidden.
CYPHER_COMMON_API void EventBus_Clear( event_bus_t *pBus ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t EventBus_IsValid( const event_bus_t *pBus ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
event_subscription_t EventBus_Subscribe(
    event_bus_t *pBus,
    event_id_t eventId,
    i32 nPriority,
    flags32_t flags,
    event_callback_t pfnCallback,
    void *pUserData ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t EventBus_Unsubscribe(
    event_bus_t *pBus,
    event_subscription_t subscription ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t EventBus_IsSubscribed(
    const event_bus_t *pBus,
    event_subscription_t subscription ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize EventBus_SubscriptionCount( const event_bus_t *pBus ) noexcept;

// Reports allocation or reentrancy failure distinctly from an event with no listeners.
// Subscriptions created by callbacks are deferred until the next emission.
CYPHER_NODISCARD CYPHER_COMMON_API
bool_t EventBus_TryEmit(
    event_bus_t *pBus,
    event_id_t eventId,
    const event_payload_t &payload,
    usize *pInvokedOut ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
usize EventBus_Emit(
    event_bus_t *pBus,
    event_id_t eventId,
    const event_payload_t &payload ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_EVENT_H
