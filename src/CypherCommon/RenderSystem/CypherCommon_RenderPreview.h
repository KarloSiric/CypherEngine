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

inline constexpr u32 CY_RENDER_PREVIEW_DEFAULT_MAX_DIMENSION = 4096u; // Conservative editor-preview limit.
inline constexpr u32 CY_RENDER_PREVIEW_MAX_MIP_LEVEL = 31u;           // Largest level representable by u32 dimensions.

enum render_preview_capability_flags_t : flags32_t {
    RENDER_PREVIEW_CAPABILITY_NONE     = 0u,                // Service cannot render any preview target.
    RENDER_PREVIEW_CAPABILITY_TEXTURE  = CYPHER_BIT32( 0 ), // Direct texture inspection is available.
    RENDER_PREVIEW_CAPABILITY_MATERIAL = CYPHER_BIT32( 1 )  // Lit material geometry is available.
};

inline constexpr flags32_t CY_RENDER_PREVIEW_CAPABILITY_MASK =
    RENDER_PREVIEW_CAPABILITY_TEXTURE |
    RENDER_PREVIEW_CAPABILITY_MATERIAL;

enum render_preview_flags_t : flags32_t {
    RENDER_PREVIEW_FLAG_NONE         = 0u,                // No optional preview overlays.
    RENDER_PREVIEW_FLAG_CHECKERBOARD = CYPHER_BIT32( 0 ), // Composite alpha over a checker pattern.
    RENDER_PREVIEW_FLAG_GRID         = CYPHER_BIT32( 1 ), // Draw the backend's reference grid.
    RENDER_PREVIEW_FLAG_LIGHTING     = CYPHER_BIT32( 2 )  // Enable scene lighting where applicable.
};

inline constexpr flags32_t CY_RENDER_PREVIEW_FLAG_MASK =
    RENDER_PREVIEW_FLAG_CHECKERBOARD |
    RENDER_PREVIEW_FLAG_GRID |
    RENDER_PREVIEW_FLAG_LIGHTING;

enum class render_preview_target_t : u8 {
    TEXTURE = 0u, // Display one texture subresource directly.
    MATERIAL      // Shade a standard preview mesh with a material.
};

enum class render_preview_source_t : u8 {
    RESOURCE_HANDLE = 0u, // Resolve a retained runtime resource.
    COOKED_RESOURCE       // Consume a borrowed, already validated cooked block.
};

// Version 1 deliberately guarantees one directly displayable output format.
// Additional transfer functions or HDR surfaces require a contract revision.
enum class render_preview_output_format_t : u8 {
    RGBA8_SRGB = 0u // Four tightly packed display-referred bytes per pixel.
};

enum class render_preview_texture_channels_t : u8 {
    RGBA = 0u, // Display all stored channels.
    RGB,       // Force opaque alpha while preserving color.
    RED,       // Replicate red into the displayed RGB channels.
    GREEN,     // Replicate green into the displayed RGB channels.
    BLUE,      // Replicate blue into the displayed RGB channels.
    ALPHA,     // Display alpha as grayscale.
    LUMINANCE, // Display computed luminance as grayscale.
    NORMAL     // Decode normal-map components for visual inspection.
};

enum class render_preview_geometry_t : u8 {
    PLANE = 0u, // Flat UV preview for decals and surfaces.
    CUBE,       // Hard-edged preview for directional response.
    SPHERE      // Smooth preview for general material response.
};

enum class render_preview_status_t : u8 {
    OK = 0u,            // Preview pixels and metadata were produced.
    INVALID_ARGUMENT,   // A direct API argument was malformed.
    INVALID_SERVICE,    // Callback table or service limits are invalid.
    INVALID_REQUEST,    // Request fields violate the shared contract.
    UNSUPPORTED_TARGET, // Backend did not advertise the requested target.
    OUTPUT_TOO_SMALL,   // Caller storage cannot hold the complete frame.
    RESOURCE_UNAVAILABLE, // Requested retained resource cannot be resolved.
    CANCELLED,            // Host cancelled work before publication.
    BACKEND_ERROR,        // Renderer failed while producing the frame.
    CONTRACT_VIOLATION    // Callback returned a status it is not allowed to return.
};

struct render_preview_limits_t {
    u32 nMaximumWidth{ CY_RENDER_PREVIEW_DEFAULT_MAX_DIMENSION };  // Maximum accepted output width.
    u32 nMaximumHeight{ CY_RENDER_PREVIEW_DEFAULT_MAX_DIMENSION }; // Maximum accepted output height.
};

struct render_preview_texture_options_t {
    u32 nMipLevel{ 0u }; // Texture mip to inspect; zero is the authored base.
    render_preview_texture_channels_t channels{
        render_preview_texture_channels_t::RGBA
    };                    // Channel visualization mode.
    f32 exposure{ 0.0f }; // Base-two exposure adjustment applied by the backend.
};

struct render_preview_material_options_t {
    render_preview_geometry_t geometry{ render_preview_geometry_t::SPHERE }; // Mesh used for shading.
    f32 yawRadians{ 0.0f };          // Horizontal object rotation.
    f32 pitchRadians{ 0.0f };        // Vertical object rotation.
    f32 cameraDistance{ 3.0f };      // Positive distance from preview origin.
    f32 verticalFovRadians{ 0.78539816339f }; // Camera vertical field of view.
};

struct render_preview_request_t {
    u64 nRequestId{ 0u };        // Non-zero host correlation identifier.
    u64 nDocumentRevision{ 0u }; // Revision used to reject stale editor frames.
    render_preview_target_t target{ render_preview_target_t::TEXTURE }; // Content to render.
    render_preview_source_t source{
        render_preview_source_t::RESOURCE_HANDLE
    }; // Selects exactly one member of the source union below.
    render_preview_output_format_t outputFormat{
        render_preview_output_format_t::RGBA8_SRGB
    }; // Required CPU output representation.
    resource_handle_t resource{}; // Retained handle used by RESOURCE_HANDLE requests.
    // Enables previews of compiled but unsaved Picasso documents. The block is
    // borrowed only for the synchronous RenderPreview_Render call and must be a
    // complete validated cooked resource understood by the selected backend.
    binary_block_t cookedResource{}; // Borrowed bytes used by COOKED_RESOURCE requests.
    u32 nWidth{ 0u };                // Requested output width in pixels.
    u32 nHeight{ 0u };               // Requested output height in pixels.
    flags32_t flags{ RENDER_PREVIEW_FLAG_NONE }; // Optional backend-neutral display flags.
    colorf_t background{ 0.08f, 0.08f, 0.08f, 1.0f }; // Linear clear/composite color.
    render_preview_texture_options_t texture{};   // Used only for texture targets.
    render_preview_material_options_t material{}; // Used only for material targets.
};

struct render_preview_result_t {
    render_preview_status_t status{ render_preview_status_t::OK }; // Final facade/backend status.
    u64 nRequestId{ 0u };        // Echoed only after successful completion.
    u64 nDocumentRevision{ 0u }; // Echoed only after successful completion.
    render_preview_output_format_t outputFormat{
        render_preview_output_format_t::RGBA8_SRGB
    }; // Format of the completed output bytes.
    u32 nWidth{ 0u };       // Completed frame width.
    u32 nHeight{ 0u };      // Completed frame height.
    u32 cbRowPitch{ 0u };   // Tightly packed byte stride between rows.
    usize cbWritten{ 0u };  // Total initialized bytes in caller storage.
};

// A resource handle remains retained, or a cooked block remains alive, for the
// entire callback. Implementations write exactly the required bytes on success.
using render_preview_fn_t = render_preview_status_t ( * )(
    void *pUserData,
    const render_preview_request_t &request,
    byte_span_t output ) noexcept;

struct render_preview_service_t {
    render_preview_fn_t pfnRender{ nullptr }; // Backend implementation entry point.
    void *pUserData{ nullptr };               // Opaque backend state passed unchanged.
    flags32_t capabilities{ RENDER_PREVIEW_CAPABILITY_NONE }; // Supported target bitset.
    render_preview_limits_t limits{};         // Backend output-allocation limits.
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
