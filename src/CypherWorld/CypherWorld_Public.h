//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/CypherWorld_Public.h
//  Purpose: Reserves the stable engine-facing boundary of CypherWorld.
//  Details: Runtime operations will be added here gate by gate after their
//           ownership, lifetime, failure behavior, and tests are designed.
//
//  History:
//  - Created by Karlo Siric on 2026-09-15
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_WORLD_PUBLIC_H
#define CYPHER_ENGINE_WORLD_PUBLIC_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherWorld_Error.h"
#include "CypherWorld_Types.h"

namespace cypher::engine::world
{

/*
===============================================================================

    Public world contract staging

This header intentionally exports no operations yet. The first contract will
cover one loaded world, explicit object lifetime, bounds updates, a view query,
and an immutable renderer-neutral submission. Terrain, portals, streaming, and
editor bridges are not allowed to enter this API before their implementation
gates are reached.

===============================================================================
*/

} // namespace cypher::engine::world

#endif // CYPHER_ENGINE_WORLD_PUBLIC_H
