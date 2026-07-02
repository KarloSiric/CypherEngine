//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherEngine/CypherRender/CypherRender_Mesh.cpp
//  Purpose: Implements the CypherRender Render Mesh module.
//  Details: This file participates in the renderer bootstrap and draw path. Keep API
//           boundaries clear so the renderer can grow from simple OpenGL startup into
//           a fuller rendering backend.
//
//  History:
//  - Created by Karlo Siric on 2026-05-11
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_Mesh.h"
#include "CypherLog.h"
#include "CypherRender_GL.h"

namespace cypher::engine::render
{

/*
================
CypherRender_MeshCreate

Validates CPU mesh data, calculates simple bounds, then uploads to OpenGL.
================
*/
render_error_t CypherRender_MeshCreate( const vertex_t *vertices,
                             const common::u32 nVertexCount,
                             const common::u32 *indices,
                             const common::u32 nIndexCount,
                             mesh_t &meshOut )
{
    if ( vertices == nullptr || nVertexCount == 0u ) {
        LOG_ERROR( log::channel_t::RENDER, "mesh create failed: invalid vertices pointer/count=%u.", nVertexCount );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    if ( indices == nullptr || nIndexCount == 0u ) {
        LOG_ERROR( log::channel_t::RENDER, "mesh create failed: invalid indices pointer/count=%u.", nIndexCount );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    meshOut = {};

    meshOut.bounds.mins = vertices[0].position;
    meshOut.bounds.maxs = vertices[0].position;

    for ( common::u32 i = 1u; i < nVertexCount; ++i ) {
        const math::vec3_t &position = vertices[i].position;

        if ( position.x < meshOut.bounds.mins.x ) {
            meshOut.bounds.mins.x = position.x;
        }
        if ( position.y < meshOut.bounds.mins.y ) {
            meshOut.bounds.mins.y = position.y;
        }
        if ( position.z < meshOut.bounds.mins.z ) {
            meshOut.bounds.mins.z = position.z;
        }
        if ( position.x > meshOut.bounds.maxs.x ) {
            meshOut.bounds.maxs.x = position.x;
        }
        if ( position.y > meshOut.bounds.maxs.y ) {
            meshOut.bounds.maxs.y = position.y;
        }
        if ( position.z > meshOut.bounds.maxs.z ) {
            meshOut.bounds.maxs.z = position.z;
        }
    }

    // Calling OpenGL API for creating a mesh, creating a distinction between different API's.
    const auto result = CypherRenderGL_MeshCreate(
        vertices,
        nVertexCount,
        indices,
        nIndexCount,
        meshOut );

    if ( result != render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "mesh create failed: GL upload failed: %s.", CypherRender_ErrorDesc( result ) );
        meshOut = {};
    } else {
        LOG_DEBUG( log::channel_t::RENDER, "mesh created: vertices=%u, indices=%u, vao=%u, vbo=%u, ebo=%u.", meshOut.nVertexCount, meshOut.nIndexCount, meshOut.nGlVao, meshOut.nGlVbo, meshOut.nGlEbo );
    }

    return result;
}

void CypherRender_MeshDestroy( mesh_t &mesh )
{
    if ( !mesh.loaded ) {
        return ;
    }

    LOG_DEBUG( log::channel_t::RENDER, "mesh destroyed: vao=%u, vbo=%u, ebo=%u.", mesh.nGlVao, mesh.nGlVbo, mesh.nGlEbo );
    CypherRenderGL_MeshDestroy( mesh );
    mesh = {};

    return ;
}

render_error_t CypherRender_MeshDraw( const mesh_t &mesh )
{
    if ( !mesh.loaded ) {
        LOG_ERROR( log::channel_t::RENDER, "mesh draw failed: mesh is not loaded." );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    return CypherRenderGL_MeshDraw( mesh );
}

}       // namespace cypher::engine::render
