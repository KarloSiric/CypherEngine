//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/CypherCommon_RenderPreview.cpp
//  Purpose: Implements the backend-neutral render preview service facade.
//  Details: The facade validates bounded image output, display options, retained
//           handles, and backend results before exposing a completed preview frame.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon_RenderPreview.h"

#include <cmath>
#include <limits>

namespace cypher::common
{
namespace
{

bool_t IsFiniteColor( colorf_t color ) noexcept
{
    return std::isfinite( color.r ) &&
           std::isfinite( color.g ) &&
           std::isfinite( color.b ) &&
           std::isfinite( color.a );
}

bool_t IsUnitColor( colorf_t color ) noexcept
{
    return IsFiniteColor( color ) &&
           color.r >= 0.0f && color.r <= 1.0f &&
           color.g >= 0.0f && color.g <= 1.0f &&
           color.b >= 0.0f && color.b <= 1.0f &&
           color.a >= 0.0f && color.a <= 1.0f;
}

bool_t IsTextureChannelModeValid(
    render_preview_texture_channels_t channels ) noexcept
{
    switch ( channels ) {
        case render_preview_texture_channels_t::RGBA:
        case render_preview_texture_channels_t::RGB:
        case render_preview_texture_channels_t::RED:
        case render_preview_texture_channels_t::GREEN:
        case render_preview_texture_channels_t::BLUE:
        case render_preview_texture_channels_t::ALPHA:
        case render_preview_texture_channels_t::LUMINANCE:
        case render_preview_texture_channels_t::NORMAL:
            return CY_TRUE;
    }
    return CY_FALSE;
}

bool_t IsGeometryValid( render_preview_geometry_t geometry ) noexcept
{
    return geometry == render_preview_geometry_t::PLANE ||
           geometry == render_preview_geometry_t::CUBE ||
           geometry == render_preview_geometry_t::SPHERE;
}

bool_t IsBackendFailureStatusValid(
    render_preview_status_t status ) noexcept
{
    return status == render_preview_status_t::RESOURCE_UNAVAILABLE ||
           status == render_preview_status_t::CANCELLED ||
           status == render_preview_status_t::BACKEND_ERROR;
}

} // namespace

usize RenderPreview_RequiredOutputSize(
    u32 nWidth,
    u32 nHeight,
    render_preview_output_format_t format ) noexcept
{
    if ( nWidth == 0u || nHeight == 0u ||
         format != render_preview_output_format_t::RGBA8_SRGB ) {
        return 0u;
    }

    constexpr usize cbPixel = 4u;
    if ( static_cast<usize>( nWidth ) >
         std::numeric_limits<usize>::max() / static_cast<usize>( nHeight ) ) {
        return 0u;
    }
    const usize nPixels =
        static_cast<usize>( nWidth ) * static_cast<usize>( nHeight );
    return nPixels <= std::numeric_limits<usize>::max() / cbPixel
        ? nPixels * cbPixel
        : 0u;
}

bool_t RenderPreview_IsServiceValid(
    const render_preview_service_t *pService ) noexcept
{
    return pService != nullptr &&
           pService->pfnRender != nullptr &&
           pService->capabilities != RENDER_PREVIEW_CAPABILITY_NONE &&
           ( pService->capabilities & ~CY_RENDER_PREVIEW_CAPABILITY_MASK ) == 0u &&
           pService->limits.nMaximumWidth != 0u &&
           pService->limits.nMaximumHeight != 0u;
}

render_preview_status_t RenderPreview_ValidateRequest(
    const render_preview_service_t *pService,
    const render_preview_request_t &request ) noexcept
{
    if ( !RenderPreview_IsServiceValid( pService ) ) {
        return render_preview_status_t::INVALID_SERVICE;
    }
    const bool_t bResourceSource =
        request.source == render_preview_source_t::RESOURCE_HANDLE &&
        ResourceHandle_IsValid( request.resource ) &&
        BinaryBlock_IsEmpty( request.cookedResource );
    const bool_t bCookedSource =
        request.source == render_preview_source_t::COOKED_RESOURCE &&
        !ResourceHandle_IsValid( request.resource ) &&
        BinaryBlock_IsValid( request.cookedResource ) &&
        !BinaryBlock_IsEmpty( request.cookedResource );

    if ( request.nRequestId == 0u ||
         ( !bResourceSource && !bCookedSource ) ||
         request.nWidth == 0u ||
         request.nHeight == 0u ||
         request.nWidth > CY_U32_MAX / 4u ||
         request.nWidth > pService->limits.nMaximumWidth ||
         request.nHeight > pService->limits.nMaximumHeight ||
         request.outputFormat != render_preview_output_format_t::RGBA8_SRGB ||
         ( request.flags & ~CY_RENDER_PREVIEW_FLAG_MASK ) != 0u ||
         !IsUnitColor( request.background ) ||
         RenderPreview_RequiredOutputSize(
             request.nWidth,
             request.nHeight,
             request.outputFormat ) == 0u ) {
        return render_preview_status_t::INVALID_REQUEST;
    }

    if ( request.target == render_preview_target_t::TEXTURE ) {
        if ( ( pService->capabilities &
               RENDER_PREVIEW_CAPABILITY_TEXTURE ) == 0u ) {
            return render_preview_status_t::UNSUPPORTED_TARGET;
        }
        if ( request.texture.nMipLevel > CY_RENDER_PREVIEW_MAX_MIP_LEVEL ||
             !IsTextureChannelModeValid( request.texture.channels ) ||
             !std::isfinite( request.texture.exposure ) ) {
            return render_preview_status_t::INVALID_REQUEST;
        }
        return render_preview_status_t::OK;
    }

    if ( request.target == render_preview_target_t::MATERIAL ) {
        if ( ( pService->capabilities &
               RENDER_PREVIEW_CAPABILITY_MATERIAL ) == 0u ) {
            return render_preview_status_t::UNSUPPORTED_TARGET;
        }
        if ( !IsGeometryValid( request.material.geometry ) ||
             !std::isfinite( request.material.yawRadians ) ||
             !std::isfinite( request.material.pitchRadians ) ||
             !std::isfinite( request.material.cameraDistance ) ||
             !std::isfinite( request.material.verticalFovRadians ) ||
             request.material.cameraDistance <= 0.0f ||
             request.material.verticalFovRadians <= 0.0f ||
             request.material.verticalFovRadians >= 3.14159265359f ) {
            return render_preview_status_t::INVALID_REQUEST;
        }
        return render_preview_status_t::OK;
    }

    return render_preview_status_t::INVALID_REQUEST;
}

render_preview_result_t RenderPreview_Render(
    const render_preview_service_t *pService,
    const render_preview_request_t &request,
    byte_span_t output ) noexcept
{
    render_preview_result_t result{};
    result.status = RenderPreview_ValidateRequest( pService, request );
    if ( result.status != render_preview_status_t::OK ) {
        return result;
    }

    const usize cbRequired = RenderPreview_RequiredOutputSize(
        request.nWidth,
        request.nHeight,
        request.outputFormat );
    if ( !Span_IsValid( output ) || output.nCount < cbRequired ) {
        result.status = render_preview_status_t::OUTPUT_TOO_SMALL;
        return result;
    }

    const render_preview_status_t backendStatus = pService->pfnRender(
        pService->pUserData,
        request,
        { output.pData, cbRequired } );
    if ( backendStatus != render_preview_status_t::OK ) {
        result.status = IsBackendFailureStatusValid( backendStatus )
            ? backendStatus
            : render_preview_status_t::CONTRACT_VIOLATION;
        return result;
    }

    result.status = render_preview_status_t::OK;
    result.nRequestId = request.nRequestId;
    result.nDocumentRevision = request.nDocumentRevision;
    result.outputFormat = request.outputFormat;
    result.nWidth = request.nWidth;
    result.nHeight = request.nHeight;
    result.cbRowPitch = request.nWidth * 4u;
    result.cbWritten = cbRequired;
    return result;
}

const char *RenderPreview_StatusName(
    render_preview_status_t status ) noexcept
{
    switch ( status ) {
        case render_preview_status_t::OK: return "OK";
        case render_preview_status_t::INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case render_preview_status_t::INVALID_SERVICE:
            return "INVALID_SERVICE";
        case render_preview_status_t::INVALID_REQUEST:
            return "INVALID_REQUEST";
        case render_preview_status_t::UNSUPPORTED_TARGET:
            return "UNSUPPORTED_TARGET";
        case render_preview_status_t::OUTPUT_TOO_SMALL:
            return "OUTPUT_TOO_SMALL";
        case render_preview_status_t::RESOURCE_UNAVAILABLE:
            return "RESOURCE_UNAVAILABLE";
        case render_preview_status_t::CANCELLED: return "CANCELLED";
        case render_preview_status_t::BACKEND_ERROR: return "BACKEND_ERROR";
        case render_preview_status_t::CONTRACT_VIOLATION:
            return "CONTRACT_VIOLATION";
    }
    return "UNKNOWN_RENDER_PREVIEW_STATUS";
}

} // namespace cypher::common
