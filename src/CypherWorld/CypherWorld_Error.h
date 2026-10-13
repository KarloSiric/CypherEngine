//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherWorld/CypherWorld_Error.h
//  Purpose: Reports bounded world object-table failures.
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

enum class world_error_t {
    OK = 0,
    INVALID_ARGUMENT,
    OUT_OF_MEMORY,
    CAPACITY_EXCEEDED,
    STALE_OBJECT_ID
};

} // namespace cypher::engine::world

#endif // CYPHER_ENGINE_WORLD_ERROR_H
