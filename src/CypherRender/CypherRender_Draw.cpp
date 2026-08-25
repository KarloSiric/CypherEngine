//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Draw.cpp
//  Purpose: Implements the CypherRender Render Draw module.
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

/*
================
Draw Implementation Notes

Draw commands validate borrowed geometry and material references before submission. Backend
execution owns no caller memory and must not retain transient command pointers past their
documented lifetime.
================
*/

#include "CypherRender_Draw.h"
#include "CypherLog.h"

namespace cypher::engine::render
{

render_error_t R_DrawItem( const draw_item_t &item, const camera_t &camera )
{
    if ( item.mesh == nullptr ) {
        LOG_ERROR( log::channel_t::RENDER, "draw item failed: mesh is null." );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    if ( item.shader == nullptr ) {
        LOG_ERROR( log::channel_t::RENDER, "draw item failed: shader is null." );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    // The Cypher 1 shader contract names these three transforms explicitly.
    render_error_t result = R_ShaderBind( *item.shader );
    if ( result != render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "draw item failed: shader bind failed: %s.", R_ErrorDesc( result ) );
        return result;
    }

    result = R_ShaderSetMat4( *item.shader, "u_model", item.modelMatrix );
    if ( result != render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "draw item failed: setting u_model failed: %s.", R_ErrorDesc( result ) );
        return result;
    }

    result = R_ShaderSetMat4( *item.shader, "u_view", camera.view );
    if ( result != render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "draw item failed: setting u_view failed: %s.", R_ErrorDesc( result ) );
        return result;
    }

    result = R_ShaderSetMat4( *item.shader, "u_projection", camera.projection );
    if ( result != render_error_t::OK ) {
        LOG_ERROR( log::channel_t::RENDER, "draw item failed: setting u_projection failed: %s.", R_ErrorDesc( result ) );
        return result;
    }

    return R_MeshDraw( *item.mesh );
}

void R_DrawListInit( draw_list_t &drawList, draw_item_t *items, common::u32 nItemCapacity )
{
    // Storage is borrowed; the owner must keep it alive until all submitted draws complete.
    drawList.items = items;
    drawList.nItemCount = 0u;
    drawList.nItemCapacity = nItemCapacity;
    LOG_DEBUG( log::channel_t::RENDER, "draw list initialized: capacity=%u.", nItemCapacity );

    return ;
}

void R_DrawListClear( draw_list_t &drawList )
{
    // Clearing is O(1); stale slots are outside the live prefix and are never read.
    drawList.nItemCount = 0u;

    return ;
}

render_error_t R_DrawListSubmit( draw_list_t &drawList, const draw_item_t &item )
{
    if ( drawList.items == nullptr || drawList.nItemCapacity == 0u ) {
        LOG_ERROR( log::channel_t::RENDER, "draw list submit failed: draw list storage is invalid." );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    if ( item.mesh == nullptr || item.shader == nullptr ) {
        LOG_ERROR( log::channel_t::RENDER, "draw list submit failed: invalid draw item mesh=%p shader=%p.", static_cast<void *>( item.mesh ), static_cast<void *>( item.shader ) );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    if ( drawList.nItemCount >= drawList.nItemCapacity ) {
        LOG_ERROR( log::channel_t::RENDER, "draw list submit failed: list full (%u/%u).", drawList.nItemCount, drawList.nItemCapacity );
        return render_error_t::ERR_DRAW_LIST_FULL;
    }

    // Draw items contain borrowed pointers, so submission copies only the lightweight command.
    drawList.items[drawList.nItemCount] = item;
    ++drawList.nItemCount;

    return render_error_t::OK;
}

render_error_t R_DrawListDraw( const draw_list_t &drawList, const camera_t &camera )
{
    if ( drawList.nItemCount == 0u ) {
        return render_error_t::OK;
    }

    if ( drawList.items == nullptr ) {
        LOG_ERROR( log::channel_t::RENDER, "draw list draw failed: draw list storage is invalid." );
        return render_error_t::ERR_INVALID_FUNC_PARAMETER;
    }

    // Preserve submission order until explicit sorting/batching policy is introduced.
    for ( common::u32 i = 0u; i < drawList.nItemCount; ++i ) {
        const render_error_t result = R_DrawItem( drawList.items[i], camera );

        if ( result != render_error_t::OK ) {
            LOG_ERROR( log::channel_t::RENDER, "draw list draw failed: item=%u error=%s.", i, R_ErrorDesc( result ) );
            return result;
        }
    }

    return render_error_t::OK;
}

}       // namespace cypher::engine::render
