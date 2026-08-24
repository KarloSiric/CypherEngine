//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Mesh.h
//  Purpose: Declares the CypherRender Render Mesh module.
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

#ifndef CYPHER_ENGINE_RENDER_MESH_H
#define CYPHER_ENGINE_RENDER_MESH_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherMath_Bounds.h"
#include "CypherRender_Error.h"

namespace cypher::engine::render
{

/*
================
Renderer Mesh Types

CPU-facing mesh description plus transitional OpenGL handles owned by the
renderer. The public backend identities will be replaced by opaque render
handles when the frontend/backend boundary is consolidated.
================
*/
struct vertex_t {
    ::cypher::math::vec3_t position{};                      // Object-space vertex position.
    ::cypher::math::vec3_t color{};                         // Linear RGB vertex color.
};

struct mesh_t {
    common::u32 nVertexCount{ 0u };                         // Number of uploaded vertex_t records.
    common::u32 nIndexCount{ 0u };                          // Number of uploaded 32-bit triangle indices.

    common::u32 nGlVao{ 0u };                               // OpenGL vertex-array object owned by this mesh.
    common::u32 nGlVbo{ 0u };                               // OpenGL vertex-buffer object owned by this mesh.
    common::u32 nGlEbo{ 0u };                               // OpenGL index-buffer object owned by this mesh.

    ::cypher::math::aabb_t bounds{ ::cypher::math::CY_AABB_EMPTY }; // Object-space bounds used for culling.

    bool loaded{ false };                                   // True only while all backend objects are valid.
};

/*
================
CypherRender_MeshCreate

Uploads vertex/index data into the active renderer backend.
================
*/
render_error_t CypherRender_MeshCreate( const vertex_t *vertices,
                             const common::u32 nVertexCount,
                             const common::u32 *indices,
                             const common::u32 nIndexCount,
                             mesh_t &meshOut );

void CypherRender_MeshDestroy( mesh_t &mesh );

render_error_t CypherRender_MeshDraw( const mesh_t &mesh );

}       // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_MESH_H
