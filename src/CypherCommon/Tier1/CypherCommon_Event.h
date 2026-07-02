//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/Tier1/CypherCommon_Event.h
//  Purpose: Declares CypherCommon Tier1 Event support.
//  Details: Tier1 builds practical utilities on top of Tier0 for strings, containers,
//           parsing, data flow, and tool-facing helpers. Keep APIs explicit and
//           stable because many systems will depend on them.
//
//  History:
//  - Created by Karlo Siric on 2026-06-22
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_TIER1_EVENT_H
#define CYPHER_COMMON_TIER1_EVENT_H
#pragma once

/*
================
CypherCommon Event

Small event and callback declarations for tools, editor notifications and
runtime message routing.
================
*/

#include "CypherCommon_Tier0.h"

namespace cypher::common
{

using event_id_t = u32;

struct event_payload_t {
    const void *pData;
    usize cbData;
};

using event_callback_t = void ( * )( event_id_t eventId, const event_payload_t &payload, void *pUserData );

struct event_bus_t;

bool_t EventBus_Init( event_bus_t *pBus, u32 maxListeners );
void EventBus_Shutdown( event_bus_t *pBus );
bool_t EventBus_Subscribe( event_bus_t *pBus, event_id_t eventId, event_callback_t pCallback, void *pUserData );
bool_t EventBus_Unsubscribe( event_bus_t *pBus, event_id_t eventId, event_callback_t pCallback, void *pUserData );
bool_t EventBus_Emit( event_bus_t *pBus, event_id_t eventId, const event_payload_t &payload );

} // namespace cypher::common

#endif // CYPHER_COMMON_TIER1_EVENT_H
