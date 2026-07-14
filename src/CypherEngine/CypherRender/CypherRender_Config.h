//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherRender/CypherRender_Config.h
//  Purpose: Declares the CypherRender Render Config module.
//  Details: This file participates in the renderer bootstrap and draw path. Keep API
//           boundaries clear so the renderer can grow from simple OpenGL startup into
//           a fuller rendering backend.
//
//  History:
//  - Created by Karlo Siric on 2026-06-05
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_CONFIG_H
#define CYPHER_ENGINE_RENDER_CONFIG_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Types.h"
#include "CypherCommon.h"

namespace cypher::engine::render
{

// DEFAULT CONSTANTS FOR THE RENDERER, LATER IN THE CONFIG DEFINED
constexpr common::f32 CYPHER_RENDER_DEFAULT_FOV_Y_RADIANS      = 70.0f * math::MATH_DEG2RAD_F;
constexpr common::f32 CYPHER_RENDER_DEFAULT_NEAR_Z             = 0.1f;
constexpr common::f32 CYPHER_RENDER_DEFAULT_FAR_Z              = 4096.0f;

constexpr common::f32 CYPHER_RENDER_DEFAULT_VIEWPORT_WIDTH     = 1280u;
constexpr common::f32 CYPHER_RENDER_DEFAULT_VIEWPORT_HEIGHT    = 720u;
constexpr common::f32 CYPHER_RENDER_DEFAULT_ASPECT_RATIO       = ( CYPHER_RENDER_DEFAULT_VIEWPORT_WIDTH / CYPHER_RENDER_DEFAULT_VIEWPORT_HEIGHT );

}       // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_CONFIG_H
