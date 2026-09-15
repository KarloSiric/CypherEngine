//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Shader.h
//  Purpose: Reserves the backend-neutral runtime shader contract.
//  Details: The contract will create live renderer programs from validated
//           cooked shader views. Native OpenGL shader and program names remain
//           private to the OpenGL backend.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_SHADER_H
#define CYPHER_ENGINE_RENDER_SHADER_H
#pragma once

/*
===============================================================================

    Runtime shader contract design checklist

Before declarations are added, define ownership of cooked bytes, supported
stage sets, backend compatibility checks, driver compilation diagnostics,
program reflection, debug names, handle lifetime, and hot-reload replacement.

The first implementation accepts the existing validated CYSH graphics program
containing one vertex stage and one fragment stage. It does not accept authored
CYKV or raw files directly; resource loading and cooking stay outside Renderer.

===============================================================================
*/

#endif // CYPHER_ENGINE_RENDER_SHADER_H
