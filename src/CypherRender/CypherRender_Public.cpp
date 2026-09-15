//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Public.cpp
//  Purpose: Implements the backend-neutral renderer frontend lifecycle.
//  Details: This module owns call ordering and public state. The selected
//           backend owns graphics API objects and native device interaction.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_Public.h"

#include "CypherRender_Local.h"

#include <cmath> // std::isfinite for frame input validation.

namespace cypher::engine::render
{

renderer_state_t tr{};

namespace
{

bool R_IsExtentValid( const render_extent_t &extent, const bool allowZero ) noexcept
{
    const bool bothZero = extent.width == 0u && extent.height == 0u;
    const bool bothNonzero = extent.width != 0u && extent.height != 0u;
    return bothNonzero || ( allowZero && bothZero );
}

bool R_IsFrameInfoValid( const render_frame_info_t &frameInfo ) noexcept
{
    if ( !R_IsExtentValid( frameInfo.drawableExtent, false ) ||
         !std::isfinite( frameInfo.deltaSeconds ) || frameInfo.deltaSeconds < 0.0f ||
         ( frameInfo.clearFlags & ~R_CLEAR_FLAG_MASK ) != 0u ||
         !std::isfinite( frameInfo.clearColor.red ) ||
         !std::isfinite( frameInfo.clearColor.green ) ||
         !std::isfinite( frameInfo.clearColor.blue ) ||
         !std::isfinite( frameInfo.clearColor.alpha ) ||
         !std::isfinite( frameInfo.clearDepth ) ||
         frameInfo.clearDepth < 0.0f || frameInfo.clearDepth > 1.0f ) {
        return false;
    }

    return true;
}

} // namespace

render_config_t R_DefaultConfig() noexcept
{
    return {};
}

render_error_t R_ValidateConfig( const render_config_t &config ) noexcept
{
    if ( config.backend >= render_backend_t::COUNT ||
         config.mode >= render_mode_t::COUNT ||
         config.presentMode >= render_present_mode_t::COUNT ||
         config.validation >= render_validation_t::COUNT ||
         ( config.requiredCapabilities & ~R_CAPABILITY_KNOWN_MASK ) != 0u ||
         ( config.optionalCapabilities & ~R_CAPABILITY_KNOWN_MASK ) != 0u ||
         ( config.requiredCapabilities & config.optionalCapabilities ) != 0u ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    if ( config.sampleCount == 1u || config.sampleCount > 64u ||
         ( config.sampleCount != 0u &&
           ( config.sampleCount & ( config.sampleCount - 1u ) ) != 0u ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    // Ray tracing and path tracing require resource, acceleration-structure,
    // and synchronization contracts that are intentionally not part of this
    // bootstrap milestone. Accepting them here would advertise false support.
    if ( config.mode != render_mode_t::RASTER ) {
        return render_error_t::ERR_UNSUPPORTED;
    }

    return render_error_t::OK;
}

render_error_t R_ConfigureWindow(
    const render_config_t &config,
    ::cypher::engine::sys::window_desc_t &windowDescription ) noexcept
{
    if ( tr.initialized ) {
        return render_error_t::ERR_ALREADY_INITIALIZED;
    }

    const render_error_t configResult = R_ValidateConfig( config );
    if ( configResult != render_error_t::OK ) {
        return configResult;
    }

    const backend_api_t *backend = nullptr;
    const render_error_t selectionResult = R_SelectBackend( config, &backend );
    if ( selectionResult != render_error_t::OK ) {
        return selectionResult;
    }

    return backend->ConfigureWindow(
        config,
        windowDescription,
        backend->state );
}

render_error_t R_Init(
    ::cypher::engine::sys::window_t &window,
    const render_config_t &config ) noexcept
{
    if ( tr.initialized ) {
        return render_error_t::ERR_ALREADY_INITIALIZED;
    }

    const render_error_t configResult = R_ValidateConfig( config );
    if ( configResult != render_error_t::OK ) {
        return configResult;
    }
    if ( !::cypher::engine::sys::Sys_WindowIsValid( window ) ) {
        return render_error_t::ERR_WINDOW_INCOMPATIBLE;
    }

    const backend_api_t *backend = nullptr;
    const render_error_t selectionResult = R_SelectBackend( config, &backend );
    if ( selectionResult != render_error_t::OK ) {
        return selectionResult;
    }

    render_info_t info{};
    const render_error_t initResult = backend->Init(
        window,
        config,
        info,
        backend->state );
    if ( initResult != render_error_t::OK ) {
        return initResult;
    }

    const bool malformedInfo = info.backend != backend->backend ||
        info.apiName == nullptr || info.apiName[0] == '\0' ||
        info.deviceName == nullptr || info.deviceName[0] == '\0' ||
        info.vendorName == nullptr || info.vendorName[0] == '\0' ||
        info.driverVersion == nullptr || info.driverVersion[0] == '\0' ||
        info.shadingLanguageVersion == nullptr || info.shadingLanguageVersion[0] == '\0' ||
        !R_IsExtentValid( info.drawableExtent, false );
    const bool missingCapabilities =
        ( info.capabilities & config.requiredCapabilities ) != config.requiredCapabilities;
    const bool accelerationMissing = config.requireAcceleration && !info.accelerated;
    if ( malformedInfo || missingCapabilities || accelerationMissing ) {
        (void)backend->Shutdown( backend->state );
        if ( malformedInfo ) return render_error_t::ERR_DEVICE_QUERY_FAILED;
        return render_error_t::ERR_CAPABILITY_MISSING;
    }

    const render_error_t bufferInitResult = R_BufferSystemInit();
    if ( bufferInitResult != render_error_t::OK ) {
        (void)backend->Shutdown( backend->state );
        return bufferInitResult;
    }

    const render_error_t vertexInputInitResult = R_VertexInputSystemInit();
    if ( vertexInputInitResult != render_error_t::OK ) {
        (void)R_BufferSystemShutdown();
        (void)backend->Shutdown( backend->state );
        return vertexInputInitResult;
    }

    tr.backend = backend;
    tr.window = &window;
    tr.config = config;
    tr.info = info;
    tr.initialized = true;
    tr.frameActive = false;
    return render_error_t::OK;
}

render_error_t R_Shutdown() noexcept
{
    if ( !tr.initialized || tr.backend == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( tr.frameActive ) {
        return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    }

    const render_error_t idleResult = tr.backend->WaitIdle( tr.backend->state );
    const render_error_t vertexInputResult = R_VertexInputSystemShutdown();
    const render_error_t bufferResult = R_BufferSystemShutdown();
    const render_error_t shutdownResult = tr.backend->Shutdown( tr.backend->state );

    tr = {};
    if ( shutdownResult != render_error_t::OK ) return shutdownResult;
    if ( vertexInputResult != render_error_t::OK ) return vertexInputResult;
    if ( bufferResult != render_error_t::OK ) return bufferResult;
    return idleResult;
}

bool R_IsInitialized() noexcept
{
    return tr.initialized;
}

bool R_IsFrameActive() noexcept
{
    return tr.frameActive;
}

const render_info_t *R_GetInfo() noexcept
{
    return tr.initialized ? &tr.info : nullptr;
}

render_error_t R_BeginFrame( const render_frame_info_t &frameInfo ) noexcept
{
    if ( !tr.initialized || tr.backend == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( tr.frameActive ) {
        return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    }
    if ( !R_IsFrameInfoValid( frameInfo ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    const render_error_t result = tr.backend->BeginFrame(
        frameInfo,
        tr.backend->state );
    if ( result == render_error_t::OK ) {
        tr.frameActive = true;
        tr.info.drawableExtent = frameInfo.drawableExtent;
    }
    return result;
}

render_error_t R_Resize( const render_extent_t &drawableExtent ) noexcept
{
    if ( !tr.initialized || tr.backend == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( tr.frameActive ) {
        return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    }
    if ( !R_IsExtentValid( drawableExtent, true ) ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    const render_error_t result = tr.backend->Resize(
        drawableExtent,
        tr.backend->state );
    if ( result == render_error_t::OK ) {
        tr.info.drawableExtent = drawableExtent;
    }
    return result;
}

render_error_t R_EndFrame() noexcept
{
    if ( !tr.initialized || tr.backend == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( !tr.frameActive ) {
        return render_error_t::ERR_FRAME_NOT_ACTIVE;
    }

    const render_error_t result = tr.backend->EndFrame( tr.backend->state );
    // A failed present still consumes the frame. Retrying EndFrame would submit
    // the same backend state twice and conceal the original failure.
    tr.frameActive = false;
    return result;
}

render_error_t R_SetPresentMode( const render_present_mode_t presentMode ) noexcept
{
    if ( !tr.initialized || tr.backend == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( tr.frameActive ) {
        return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    }
    if ( presentMode >= render_present_mode_t::COUNT ) {
        return render_error_t::ERR_INVALID_ARGUMENT;
    }

    render_present_mode_t actualMode = tr.info.presentMode;
    const render_error_t result = tr.backend->SetPresentMode(
        presentMode,
        actualMode,
        tr.backend->state );
    if ( result == render_error_t::OK ) {
        tr.config.presentMode = presentMode;
        tr.info.presentMode = actualMode;
    }
    return result;
}

render_error_t R_WaitIdle() noexcept
{
    if ( !tr.initialized || tr.backend == nullptr ) {
        return render_error_t::ERR_NOT_INITIALIZED;
    }
    if ( tr.frameActive ) {
        return render_error_t::ERR_FRAME_ALREADY_ACTIVE;
    }

    return tr.backend->WaitIdle( tr.backend->state );
}

} // namespace cypher::engine::render
