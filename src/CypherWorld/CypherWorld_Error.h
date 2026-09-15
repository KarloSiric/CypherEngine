//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/CypherWorld_Error.h
//  Purpose: Reserves stable failure reporting for CypherWorld operations.
//  Details: Error values will be introduced with the operations that can
//           produce them rather than publishing speculative failure states.
//
//  History:
//  - Created by Karlo Siric on 2026-09-15
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_WORLD_ERROR_H
#define CYPHER_ENGINE_WORLD_ERROR_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::engine::world
{

/*
===============================================================================

    World errors

The eventual error set must distinguish invalid arguments, invalid or stale
handles, malformed cooked maps, capacity exhaustion, unavailable dependencies,
streaming failures, and invalid lifecycle transitions. Diagnostic text remains
separate from stable machine-readable values.

===============================================================================
*/

} // namespace cypher::engine::world

#endif // CYPHER_ENGINE_WORLD_ERROR_H
