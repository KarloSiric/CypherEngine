//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Command.h
//  Purpose: Reserves backend-neutral frame-command recording contracts.
//  Details: Frontend commands preserve deterministic ordering between scene
//           submission and backend execution without exposing graphics API
//           command buffers to Host or gameplay code.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_COMMAND_H
#define CYPHER_ENGINE_RENDER_COMMAND_H
#pragma once

/*
===============================================================================

    Render command contract design checklist

Define command ownership, frame-local allocation, alignment, bounded capacity,
command tags, payload validation, submission order, failure behavior, and the
point at which backend execution consumes recorded commands.

===============================================================================
*/

#endif // CYPHER_ENGINE_RENDER_COMMAND_H
