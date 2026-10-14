//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Runs ordered scene passes without owning the host frame or resources.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_SceneView.h"
#include "CypherRender/CypherRender_Public.h"

#include <cmath>
#include <limits>

namespace cypher::engine::render
{
namespace common = ::cypher::common;
namespace
{
bool ValidSpan( const render_scene_draw_span_t &span ) noexcept
{
    const common::u64 bytes = static_cast<common::u64>( span.count ) * sizeof( render_draw_indexed_desc_t );
    return ( span.count == 0u || span.draws != nullptr ) &&
        bytes <= std::numeric_limits<common::usize>::max();
}

bool ValidClear( const render_target_pass_desc_t &pass ) noexcept
{
    return ( pass.clearFlags & ~( R_CLEAR_COLOR | R_CLEAR_DEPTH ) ) == 0u &&
        std::isfinite( pass.clearColor.red ) && std::isfinite( pass.clearColor.green ) &&
        std::isfinite( pass.clearColor.blue ) && std::isfinite( pass.clearColor.alpha ) &&
        std::isfinite( pass.clearDepth ) && pass.clearDepth >= 0.0f && pass.clearDepth <= 1.0f;
}

bool AttachmentsOverlap( const render_target_info_t &a, const render_target_info_t &b ) noexcept
{
    const render_texture_handle_t first[] = { a.color, a.depth };
    const render_texture_handle_t second[] = { b.color, b.depth };
    for ( const auto x : first ) {
        for ( const auto y : second ) {
            if ( x.value != 0u && x.value == y.value ) return true;
        }
    }
    return false;
}

render_error_t Preflight( const render_scene_view_desc_t &description ) noexcept
{
    if ( !R_IsInitialized() ) return render_error_t::ERR_NOT_INITIALIZED;
    if ( !R_IsFrameActive() ) return render_error_t::ERR_FRAME_NOT_ACTIVE;
    if ( R_IsTargetPassActive() ) return render_error_t::ERR_COMMAND_STATE_INVALID;
    if ( !ValidSpan( description.shadowDraws ) || !ValidSpan( description.opaqueDraws ) ||
         !ValidSpan( description.blendDraws ) || !ValidSpan( description.additiveDraws ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }
    const bool shadowEnabled = description.shadowPass.target.value != 0u;
    const bool outputOffscreen = description.outputPass.target.value != 0u;
    if ( ( !shadowEnabled && description.shadowDraws.count != 0u ) ||
         !ValidClear( description.cameraPass ) ||
         ( shadowEnabled && ( !ValidClear( description.shadowPass ) ||
             ( description.shadowPass.clearFlags & R_CLEAR_COLOR ) != 0u ) ) ||
         ( outputOffscreen && !ValidClear( description.outputPass ) ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    render_target_info_t camera{}, shadow{}, output{};
    auto error = R_GetRenderTargetInfo( description.cameraPass.target, &camera );
    if ( error != render_error_t::OK ) return error;
    if ( camera.color.value == 0u ) return render_error_t::ERR_RENDER_TARGET_INVALID;
    if ( shadowEnabled ) {
        error = R_GetRenderTargetInfo( description.shadowPass.target, &shadow );
        if ( error != render_error_t::OK ) return error;
        if ( shadow.depth.value == 0u || shadow.color.value != 0u || AttachmentsOverlap( shadow, camera ) ) {
            return render_error_t::ERR_RENDER_TARGET_INVALID;
        }
    }
    if ( outputOffscreen ) {
        error = R_GetRenderTargetInfo( description.outputPass.target, &output );
        if ( error != render_error_t::OK ) return error;
        if ( output.color.value == 0u || AttachmentsOverlap( output, camera ) ||
             ( shadowEnabled && AttachmentsOverlap( output, shadow ) ) ) {
            return render_error_t::ERR_RENDER_TARGET_INVALID;
        }
        for ( const auto texture : description.outputDraw.sampledTextures ) {
            if ( texture.value != 0u && ( texture.value == output.color.value ||
                 texture.value == output.depth.value ) ) return render_error_t::ERR_RESOURCE_BUSY;
        }
    }
    if ( description.outputDraw.sampledTextures[0].value != camera.color.value ) {
        return render_error_t::ERR_BINDING_MISMATCH;
    }
    return render_error_t::OK;
}

void DrawSpan( const render_scene_draw_span_t &span, common::u32 &completed,
    render_scene_view_result_t &result ) noexcept
{
    for ( common::u32 i = 0u; i < span.count && result.error == render_error_t::OK; ++i ) {
        result.error = R_DrawIndexed( span.draws[i] );
        if ( result.error == render_error_t::OK ) ++completed;
    }
}

void EndPass( render_scene_view_result_t &result ) noexcept
{
    result.cleanupError = R_EndTargetPass();
    if ( result.error == render_error_t::OK ) result.error = result.cleanupError;
}
} // namespace

render_scene_view_result_t R_ExecuteSceneView( const render_scene_view_desc_t &description ) noexcept
{
    render_scene_view_result_t result{};
    result.error = Preflight( description );
    if ( result.error != render_error_t::OK ) return result;

    if ( description.shadowPass.target.value != 0u ) {
        result.stage = render_scene_view_stage_t::SHADOW;
        result.error = R_BeginTargetPass( description.shadowPass );
        if ( result.error != render_error_t::OK ) return result;
        DrawSpan( description.shadowDraws, result.shadowDraws, result );
        EndPass( result );
        if ( result.error != render_error_t::OK ) return result;
    }

    result.stage = render_scene_view_stage_t::CAMERA;
    result.error = R_BeginTargetPass( description.cameraPass );
    if ( result.error != render_error_t::OK ) return result;
    DrawSpan( description.opaqueDraws, result.opaqueDraws, result );
    DrawSpan( description.blendDraws, result.blendDraws, result );
    DrawSpan( description.additiveDraws, result.additiveDraws, result );
    EndPass( result );
    if ( result.error != render_error_t::OK ) return result;

    result.stage = render_scene_view_stage_t::OUTPUT;
    const bool outputOffscreen = description.outputPass.target.value != 0u;
    if ( outputOffscreen ) {
        result.error = R_BeginTargetPass( description.outputPass );
        if ( result.error != render_error_t::OK ) return result;
    }
    result.error = R_DrawIndexed( description.outputDraw );
    result.outputDrawn = result.error == render_error_t::OK;
    if ( outputOffscreen ) EndPass( result );
    if ( result.error == render_error_t::OK ) result.stage = render_scene_view_stage_t::COMPLETE;
    return result;
}

} // namespace cypher::engine::render
