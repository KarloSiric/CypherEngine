//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: src/CypherRender/CypherRender_Types.h
//  Purpose: Defines renderer-wide value types and stable capability bits.
//  Details: This is the sole owner of renderer contract types. Backend-specific
//           OpenGL, software, and Vulkan state must remain outside this file.
//
//  History:
//  - Created by Karlo Siric on 2026-09-08
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#ifndef CYPHER_ENGINE_RENDER_TYPES_H
#define CYPHER_ENGINE_RENDER_TYPES_H

#ifndef PRAGMA_ONCE
    #pragma once
#endif

#include "CypherCommon/Tier0/CypherCommon_Defines.h"
#include "CypherCommon/Tier0/CypherCommon_Handle.h"

namespace cypher::engine::render
{

/*
===============================================================================

    Renderer selection and operating policy

These enums describe backend-independent policy chosen by Host. They are kept
small because they cross the renderer frontend/backend boundary frequently and
may later be recorded in diagnostics or frame captures.

===============================================================================
*/
enum class render_backend_t : ::cypher::common::u8 {
    AUTO = 0u, // Resolve the configured platform default during initialization.
    SOFTWARE,  // CPU rasterization backend used for learning and diagnostics.
    OPENGL,    // Initial hardware rasterization backend.
    VULKAN,    // Future explicit backend and hardware ray-tracing path.
    COUNT      // Number of backend values; never a valid selection.
};

enum class render_mode_t : ::cypher::common::u8 {
    RASTER = 0u,       // Conventional rasterized rendering.
    HYBRID_RAY_TRACED, // Rasterization with selected traced effects.
    PATH_TRACED,       // Complete progressive path-traced rendering.
    COUNT              // Number of rendering modes; never selected.
};

enum class render_present_mode_t : ::cypher::common::u8 {
    IMMEDIATE = 0u, // Present without waiting for vertical synchronization.
    FIFO,           // Ordered vertical synchronization; the portable default.
    FIFO_RELAXED,   // Adaptive synchronization when the backend supports it.
    MAILBOX,        // Low-latency queued presentation when available.
    COUNT           // Number of presentation modes; never selected.
};

enum class render_validation_t : ::cypher::common::u8 {
    DISABLED = 0u, // Perform only checks required to keep execution valid.
    ERRORS,        // Report invalid operations and backend failures.
    VERBOSE,       // Enable debug output, object labels, markers, and extra checks.
    COUNT          // Number of validation policies; never selected.
};

/*
================
Renderer Object Types

Resource handles and renderer handles are deliberately different. A resource
handle identifies cooked CPU-side asset data. A renderer handle identifies a
live renderer-owned object, frequently backed by GPU storage.
================
*/
enum class render_object_type_t : ::cypher::common::u16 {
    INVALID = 0u, // Zero is reserved so a zero-initialized handle stays invalid.
    BUFFER,       // Vertex, index, uniform, storage, or transfer buffer.
    TEXTURE,      // Sampled image or renderable image storage.
    SAMPLER,      // Filtering, addressing, and comparison state.
    SHADER,       // Backend shader module or linked shader stage data.
    PIPELINE,     // Complete executable graphics or compute state.
    BINDING_SET,  // Resource bindings consumed by a pipeline.
    RENDER_TARGET,// Color and depth attachments used by a render pass.
    QUERY,        // Timestamp, occlusion, or pipeline-statistics query.
    FENCE,        // CPU/GPU synchronization object.
    MESH,         // Renderer-owned geometry buffers and draw metadata.
    MATERIAL,     // Pipeline and binding combination used for a surface.
    COUNT         // Number of renderer object types; never stored in a handle.
};

using render_handle_t = ::cypher::common::handle64_t;
inline constexpr render_handle_t R_INVALID_HANDLE{}; // Packed zero sentinel.

/*
================
Renderer Capability Flags

Required capabilities make initialization fail when absent. Optional
capabilities are enabled when available and reported through render_info_t.
Bit positions are part of the renderer contract and must never be reordered.
================
*/
using render_capability_flags_t = ::cypher::common::flags64_t;

enum render_capability_flag_t : render_capability_flags_t {
    R_CAPABILITY_NONE                     = 0u,
    R_CAPABILITY_RASTERIZATION            = CYPHER_BIT64( 0 ),
    R_CAPABILITY_INSTANCING               = CYPHER_BIT64( 1 ),
    R_CAPABILITY_DRAW_INDIRECT            = CYPHER_BIT64( 2 ),
    R_CAPABILITY_MULTI_DRAW_INDIRECT      = CYPHER_BIT64( 3 ),
    R_CAPABILITY_COMPUTE                  = CYPHER_BIT64( 4 ),
    R_CAPABILITY_GEOMETRY_SHADER          = CYPHER_BIT64( 5 ),
    R_CAPABILITY_TESSELLATION_SHADER      = CYPHER_BIT64( 6 ),
    R_CAPABILITY_SHADER_STORAGE           = CYPHER_BIT64( 7 ),
    R_CAPABILITY_TEXTURE_ARRAYS           = CYPHER_BIT64( 8 ),
    R_CAPABILITY_CUBE_MAP_ARRAYS          = CYPHER_BIT64( 9 ),
    R_CAPABILITY_ANISOTROPIC_FILTERING    = CYPHER_BIT64( 10 ),
    R_CAPABILITY_TEXTURE_COMPRESSION_BC   = CYPHER_BIT64( 11 ),
    R_CAPABILITY_TEXTURE_COMPRESSION_ETC  = CYPHER_BIT64( 12 ),
    R_CAPABILITY_TEXTURE_COMPRESSION_ASTC = CYPHER_BIT64( 13 ),
    R_CAPABILITY_FLOAT_RENDER_TARGETS     = CYPHER_BIT64( 14 ),
    R_CAPABILITY_SRGB_FRAMEBUFFER         = CYPHER_BIT64( 15 ),
    R_CAPABILITY_MULTISAMPLING            = CYPHER_BIT64( 16 ),
    R_CAPABILITY_DEBUG_OUTPUT             = CYPHER_BIT64( 17 ),
    R_CAPABILITY_DEBUG_MARKERS            = CYPHER_BIT64( 18 ),
    R_CAPABILITY_GPU_TIMESTAMPS           = CYPHER_BIT64( 19 ),
    R_CAPABILITY_HDR_OUTPUT               = CYPHER_BIT64( 20 ),
    R_CAPABILITY_BINDLESS_RESOURCES       = CYPHER_BIT64( 21 ),
    R_CAPABILITY_SPARSE_RESOURCES         = CYPHER_BIT64( 22 ),
    R_CAPABILITY_VARIABLE_RATE_SHADING    = CYPHER_BIT64( 23 ),
    R_CAPABILITY_MESH_SHADERS             = CYPHER_BIT64( 24 ),
    R_CAPABILITY_ACCELERATION_STRUCTURES  = CYPHER_BIT64( 25 ),
    R_CAPABILITY_RAY_QUERY                = CYPHER_BIT64( 26 ),
    R_CAPABILITY_RAY_TRACING_PIPELINE     = CYPHER_BIT64( 27 )
};

inline constexpr ::cypher::common::u32 R_CAPABILITY_BIT_COUNT = 28u;
inline constexpr render_capability_flags_t R_CAPABILITY_KNOWN_MASK =
    CYPHER_BIT64( R_CAPABILITY_BIT_COUNT ) - 1u;

/*
================
Renderer Lifecycle Records

Host owns render_config_t and supplies one render_frame_info_t per frame. The
renderer owns render_info_t and exposes it as read-only state until shutdown.
================
*/
struct render_extent_t {
    ::cypher::common::u32 width{ 0u };  // Renderable width in physical pixels.
    ::cypher::common::u32 height{ 0u }; // Renderable height in physical pixels.
};

struct render_color_t {
    ::cypher::common::f32 red{ 0.0f };   // Linear red component.
    ::cypher::common::f32 green{ 0.0f }; // Linear green component.
    ::cypher::common::f32 blue{ 0.0f };  // Linear blue component.
    ::cypher::common::f32 alpha{ 1.0f }; // Linear alpha component.
};

using render_clear_flags_t = ::cypher::common::flags32_t;

enum render_clear_flag_t : render_clear_flags_t {
    R_CLEAR_NONE    = 0u,
    R_CLEAR_COLOR   = CYPHER_BIT32( 0 ), // Clear the active color attachment.
    R_CLEAR_DEPTH   = CYPHER_BIT32( 1 ), // Clear the active depth attachment.
    R_CLEAR_STENCIL = CYPHER_BIT32( 2 )  // Clear the active stencil attachment.
};

inline constexpr render_clear_flags_t R_CLEAR_FLAG_MASK =
    R_CLEAR_COLOR | R_CLEAR_DEPTH | R_CLEAR_STENCIL;

struct render_limits_t {
    ::cypher::common::u32 maxTexture2DSize{ 0u };          // Maximum width or height of a 2D texture.
    ::cypher::common::u32 maxTexture3DSize{ 0u };          // Maximum width, height, or depth of a 3D texture.
    ::cypher::common::u32 maxCubeMapSize{ 0u };            // Maximum width or height of one cube-map face.
    ::cypher::common::u32 maxArrayTextureLayers{ 0u };     // Maximum layers in an array texture.
    ::cypher::common::u32 maxCombinedTextureUnits{ 0u };   // Texture units visible to all shader stages.
    ::cypher::common::u32 maxVertexAttributes{ 0u };       // Vertex attributes accepted by one pipeline.
    ::cypher::common::u32 maxUniformBufferBindings{ 0u };  // Simultaneous uniform-buffer binding points.
    ::cypher::common::u32 maxColorAttachments{ 0u };       // Color attachments accepted by one framebuffer.
    ::cypher::common::u32 maxDrawBuffers{ 0u };            // Color outputs writable by one draw.
    ::cypher::common::u32 maxSamples{ 0u };                // Maximum supported raster sample count.
    ::cypher::common::u32 maxViewportWidth{ 0u };          // Maximum physical viewport width.
    ::cypher::common::u32 maxViewportHeight{ 0u };         // Maximum physical viewport height.
    ::cypher::common::u64 maxUniformBlockBytes{ 0u };      // Maximum storage in one uniform block.
};

struct render_config_t {
    render_backend_t backend{ render_backend_t::AUTO }; // Backend requested by Host or configuration.
    render_mode_t mode{ render_mode_t::RASTER };        // Rendering technique requested at startup.
    render_present_mode_t presentMode{ render_present_mode_t::FIFO }; // Presentation and latency policy.
    render_validation_t validation{ render_validation_t::ERRORS };    // Runtime diagnostic strictness.

    render_capability_flags_t requiredCapabilities{
        R_CAPABILITY_RASTERIZATION }; // Missing any bit makes initialization fail.
    render_capability_flags_t optionalCapabilities{
        R_CAPABILITY_SRGB_FRAMEBUFFER |
        R_CAPABILITY_DEBUG_OUTPUT |
        R_CAPABILITY_DEBUG_MARKERS |
        R_CAPABILITY_GPU_TIMESTAMPS }; // Enabled and reported when supported.

    ::cypher::common::u8 sampleCount{ 0u }; // Zero disables MSAA; otherwise requests a supported power of two.
    bool allowBackendFallback{ true };      // AUTO may try another built backend when its first choice fails.
    bool sRGBFramebuffer{ true };           // Request correct linear-to-sRGB conversion on presentation.
    bool requireAcceleration{ true };       // Reject a software driver for a hardware backend.
};

struct render_info_t {
    render_backend_t backend{ render_backend_t::AUTO }; // Backend that successfully initialized.
    render_mode_t mode{ render_mode_t::RASTER };        // Rendering mode actually selected.
    render_present_mode_t presentMode{ render_present_mode_t::FIFO }; // Presentation mode actually selected.
    render_capability_flags_t capabilities{ R_CAPABILITY_NONE };      // Complete supported feature set.
    render_extent_t drawableExtent{};                                 // Current drawable size in physical pixels.
    render_limits_t limits{};                                         // Numeric limits reported by the active device.

    ::cypher::common::u16 apiMajorVersion{ 0u }; // Native graphics API major version.
    ::cypher::common::u16 apiMinorVersion{ 0u }; // Native graphics API minor version.
    ::cypher::common::u8 sampleCount{ 0u };      // Actual default-framebuffer sample count.
    bool accelerated{ false };                  // Backend reports hardware acceleration.

    const char *apiName{ nullptr };                // Borrowed API name; valid until R_Shutdown.
    const char *deviceName{ nullptr };             // Borrowed adapter/device name; valid until R_Shutdown.
    const char *vendorName{ nullptr };             // Borrowed device vendor; valid until R_Shutdown.
    const char *driverVersion{ nullptr };          // Borrowed driver version; valid until R_Shutdown.
    const char *shadingLanguageVersion{ nullptr }; // Borrowed shader-language version; valid until R_Shutdown.
};

struct render_frame_info_t {
    ::cypher::common::u64 frameIndex{ 0u }; // Monotonic Host frame number used by diagnostics and captures.
    ::cypher::common::f32 deltaSeconds{ 0.0f }; // Simulation time represented by this frame.
    render_extent_t drawableExtent{};           // Current drawable size after platform DPI scaling.
    render_clear_flags_t clearFlags{ R_CLEAR_COLOR | R_CLEAR_DEPTH }; // Attachments reset before frame commands.
    render_color_t clearColor{ 0.02f, 0.025f, 0.035f, 1.0f };         // Linear clear color for an empty frame.
    ::cypher::common::f32 clearDepth{ 1.0f };                         // Far depth value for a conventional Z buffer.
    ::cypher::common::u8 clearStencil{ 0u };                          // Stencil value written when requested.
};

static_assert( sizeof( render_backend_t ) == sizeof( ::cypher::common::u8 ) );
static_assert( sizeof( render_object_type_t ) == sizeof( ::cypher::common::u16 ) );
static_assert( R_CAPABILITY_BIT_COUNT < 64u );

} // namespace cypher::engine::render

#endif // CYPHER_ENGINE_RENDER_TYPES_H
