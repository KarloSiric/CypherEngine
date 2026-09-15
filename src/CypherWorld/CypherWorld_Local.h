//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/CypherWorld_Local.h
//  Purpose: Reserves implementation-only state shared inside CypherWorld.
//  Details: Internal tables and indexes belong here and never cross the public
//           boundary as owning pointers or native renderer objects.
//
//  History:
//  - Created by Karlo Siric on 2026-09-15
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_WORLD_LOCAL_H
#define CYPHER_ENGINE_WORLD_LOCAL_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherWorld_Public.h"

namespace cypher::engine::world
{

/*
===============================================================================

    Private world state staging

The first runtime state will own an object table, generation records, bounds,
layer masks, and scratch storage for one visibility result. Separate indexes
are added only when their measured workloads justify them.

===============================================================================
*/

} // namespace cypher::engine::world

#endif // CYPHER_ENGINE_WORLD_LOCAL_H
