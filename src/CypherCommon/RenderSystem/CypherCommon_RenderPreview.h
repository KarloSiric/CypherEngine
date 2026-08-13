//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherCommon/RenderSystem/CypherCommon_RenderPreview.h
//  Purpose: Declares the backend-neutral render preview service contract.
//  Details: Editor cores submit retained resource handles and receive pixels in
//           caller-owned storage. Qt widgets, native GPU handles, and renderer
//           implementation types never cross this shared boundary.
//
//  History:
//  - Created by Karlo Siric on 2026-08-13
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_COMMON_RENDERSYSTEM_RENDERPREVIEW_H
#define CYPHER_COMMON_RENDERSYSTEM_RENDERPREVIEW_H
#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon_Color.h"
#include "CypherCommon_BinaryBlock.h"
#include "CypherCommon_ResourceHandle.h"
#include "CypherCommon_Span.h"

namespace cypher::common
{

inline constexpr u32 CY_RENDER_PREVIEW_DEFAULT_MAX_DIMENSION = 4096u;
inline constexpr u32 CY_RENDER_PREVIEW_MAX_MIP_LEVEL = 31u;

enum render_preview_capability_flags_t : flags32_t {
    RENDER_PREVIEW_CAPABILITY_NONE     = 0u,
    RENDER_PREVIEW_CAPABILITY_TEXTURE  = CYPHER_BIT32( 0 ),
    RENDER_PREVIEW_CAPABILITY_MATERIAL = CYPHER_BIT32( 1 )
};

inline constexpr flags32_t CY_RENDER_PREVIEW_CAPABILITY_MASK =
    RENDER_PREVIEW_CAPABILITY_TEXTURE |
    RENDER_PREVIEW_CAPABILITY_MATERIAL;

enum render_preview_flags_t : flags32_t {
    RENDER_PREVIEW_FLAG_NONE         = 0u,
    RENDER_PREVIEW_FLAG_CHECKERBOARD = CYPHER_BIT32( 0 ),
    RENDER_PREVIEW_FLAG_GRID         = CYPHER_BIT32( 1 ),
    RENDER_PREVIEW_FLAG_LIGHTING     = CYPHER_BIT32( 2 )
};

inline constexpr flags32_t CY_RENDER_PREVIEW_FLAG_MASK =
    RENDER_PREVIEW_FLAG_CHECKERBOARD |
    RENDER_PREVIEW_FLAG_GRID |
    RENDER_PREVIEW_FLAG_LIGHTING;

enum class render_preview_target_t : u8 {
    TEXTURE = 0u,
    MATERIAL
};

enum class render_preview_source_t : u8 {
    RESOURCE_HANDLE = 0u,
    COOKED_RESOURCE
};

// Version 1 deliberately guarantees one directly displayable output format.
// Additional transfer functions or HDR surfaces require a contract revision.
enum class render_preview_output_format_t : u8 {
    RGBA8_SRGB = 0u
};

enum class render_preview_texture_channels_t : u8 {
    RGBA = 0u,
    RGB,
    RED,
    GREEN,
    BLUE,
    ALPHA,
    LUMINANCE,
    NORMAL
};

enum class render_preview_geometry_t : u8 {
    PLANE = 0u,
    CUBE,
    SPHERE
};

enum class render_preview_status_t : u8 {
    OK = 0u,
    INVALID_ARGUMENT,
    INVALID_SERVICE,
    INVALID_REQUEST,
    UNSUPPORTED_TARGET,
    OUTPUT_TOO_SMALL,
    RESOURCE_UNAVAILABLE,
    CANCELLED,
    BACKEND_ERROR,
    CONTRACT_VIOLATION
};

struct render_preview_limits_t {
    u32 nMaximumWidth{ CY_RENDER_PREVIEW_DEFAULT_MAX_DIMENSION };
    u32 nMaximumHeight{ CY_RENDER_PREVIEW_DEFAULT_MAX_DIMENSION };
};

struct render_preview_texture_options_t {
    u32 nMipLevel{ 0u };
    render_preview_texture_channels_t channels{
        render_preview_texture_channels_t::RGBA
    };
    f32 exposure{ 0.0f };
};

struct render_preview_material_options_t {
    render_preview_geometry_t geometry{ render_preview_geometry_t::SPHERE };
    f32 yawRadians{ 0.0f };
    f32 pitchRadians{ 0.0f };
    f32 cameraDistance{ 3.0f };
    f32 verticalFovRadians{ 0.78539816339f };
};

struct render_preview_request_t {
    u64 nRequestId{ 0u };
    u64 nDocumentRevision{ 0u };
    render_preview_target_t target{ render_preview_target_t::TEXTURE };
    render_preview_source_t source{
        render_preview_source_t::RESOURCE_HANDLE
    };
    render_preview_output_format_t outputFormat{
        render_preview_output_format_t::RGBA8_SRGB
    };
    resource_handle_t resource{};
    // Enables previews of compiled but unsaved Picasso documents. The block is
    // borrowed only for the synchronous RenderPreview_Render call and must be a
    // complete validated cooked resource understood by the selected backend.
    binary_block_t cookedResource{};
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    flags32_t flags{ RENDER_PREVIEW_FLAG_NONE };
    colorf_t background{ 0.08f, 0.08f, 0.08f, 1.0f };
    render_preview_texture_options_t texture{};
    render_preview_material_options_t material{};
};

struct render_preview_result_t {
    render_preview_status_t status{ render_preview_status_t::OK };
    u64 nRequestId{ 0u };
    u64 nDocumentRevision{ 0u };
    render_preview_output_format_t outputFormat{
        render_preview_output_format_t::RGBA8_SRGB
    };
    u32 nWidth{ 0u };
    u32 nHeight{ 0u };
    u32 cbRowPitch{ 0u };
    usize cbWritten{ 0u };
};

// A resource handle remains retained, or a cooked block remains alive, for the
// entire callback. Implementations write exactly the required bytes on success.
using render_preview_fn_t = render_preview_status_t ( * )(
    void *pUserData,
    const render_preview_request_t &request,
    byte_span_t output ) noexcept;

struct render_preview_service_t {
    render_preview_fn_t pfnRender{ nullptr };
    void *pUserData{ nullptr };
    flags32_t capabilities{ RENDER_PREVIEW_CAPABILITY_NONE };
    render_preview_limits_t limits{};
};

CYPHER_NODISCARD CYPHER_COMMON_API
usize RenderPreview_RequiredOutputSize(
    u32 nWidth,
    u32 nHeight,
    render_preview_output_format_t format ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
bool_t RenderPreview_IsServiceValid(
    const render_preview_service_t *pService ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API
render_preview_status_t RenderPreview_ValidateRequest(
    const render_preview_service_t *pService,
    const render_preview_request_t &request ) noexcept;

// Validates the service, request, and output transaction before dispatch. Result
// metadata is committed only after a successful backend call.
CYPHER_NODISCARD CYPHER_COMMON_API
render_preview_result_t RenderPreview_Render(
    const render_preview_service_t *pService,
    const render_preview_request_t &request,
    byte_span_t output ) noexcept;

CYPHER_NODISCARD CYPHER_COMMON_API CY_RETURNS_NONNULL
const char *RenderPreview_StatusName(
    render_preview_status_t status ) noexcept;

} // namespace cypher::common

#endif // CYPHER_COMMON_RENDERSYSTEM_RENDERPREVIEW_H
