//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/CypherWorld_Types.h
//  Purpose: Reserves backend-neutral value types for the world runtime.
//  Details: Types will describe world identity, object bounds, view queries,
//           visibility results, and submissions without exposing containers.
//
//  History:
//  - Created by Karlo Siric on 2026-09-15
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_WORLD_TYPES_H
#define CYPHER_ENGINE_WORLD_TYPES_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

namespace cypher::engine::world
{

/*
===============================================================================

    World value-type rules

Public records remain plain data. They may carry IDs, handles, transforms,
bounds, layer masks, resource handles, and renderer handles, but they may not
expose an STL container, a native graphics object, an editor object, or a
pointer into a movable internal table.

The first concrete records will be designed alongside the Gate 1 object table
instead of being guessed in advance.

===============================================================================
*/

} // namespace cypher::engine::world

#endif // CYPHER_ENGINE_WORLD_TYPES_H
