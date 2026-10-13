//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: examples/CypherRender/CypherRender_Cube.cpp
//  Purpose: Demonstrates a visible-world-object public-renderer drawing path.
//  Details: Loads a cooked shader and floor texture through the loose-directory
//           VFS, resolves meshes and textures from logical World slots, and
//           uploads aligned constants for visible objects each frame.
//           No graphics-driver or native-window API crosses into this host.
//
//  This file is proprietary and confidential. See LICENSE for details.
//
//////////////////////////////////////////////////////////////////////////

#include "CypherCommon/FileSystem/CypherCommon_VfsDirectory.h"
#include "CypherCommon/Mathlib/CypherMath_Affine3.h"
#include "CypherCommon/Mathlib/CypherMath_Bounds.h"
#include "CypherCommon/Mathlib/CypherMath_Frustum.h"
#include "CypherCommon/Mathlib/CypherMath_Matrix4.h"
#include "CypherCommon/Mathlib/CypherMath_Quaternion.h"
#include "CypherRender_CubeCamera.h"
#include "CypherRender/CypherRender_Buffer.h"
#include "CypherRender/CypherRender_Draw.h"
#include "CypherRender/CypherRender_Pipeline.h"
#include "CypherRender/CypherRender_Public.h"
#include "CypherRender/CypherRender_Sampler.h"
#include "CypherRender/CypherRender_Shader.h"
#include "CypherRender/CypherRender_Target.h"
#include "CypherRender/CypherRender_Texture.h"
#include "CypherRender/Runtime/CypherRender_MeshBridge.h"
#include "CypherRender/Runtime/CypherRender_DrawQueue.h"
#include "CypherRender/Runtime/CypherRender_UniformBatch.h"
#include "CypherSystem/Platform/CypherSystem_Public.h"
#include "CypherSystem/Platform/CypherSystem_Window.h"
#include "CypherWorld/CypherWorld_Public.h"
#include "CypherWorld/Submission/CypherWorld_Submission.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

#ifndef CYPHER_RENDER_CUBE_SHADER_PATH
    #define CYPHER_RENDER_CUBE_SHADER_PATH "cooked/render_smoke/cube.cyshader_c"
#endif
#ifndef CYPHER_RENDER_CUBE_TEXTURE_PATH
    #define CYPHER_RENDER_CUBE_TEXTURE_PATH "cooked/textures/blockout/floor_checker.cytex_c"
#endif
#ifndef CYPHER_RENDER_CUBE_SHADOW_SHADER_PATH
    #define CYPHER_RENDER_CUBE_SHADOW_SHADER_PATH "cooked/render_smoke/shadow.cyshader_c"
#endif
#ifndef CYPHER_RENDER_CUBE_TONEMAP_SHADER_PATH
    #define CYPHER_RENDER_CUBE_TONEMAP_SHADER_PATH "cooked/render_smoke/tonemap.cyshader_c"
#endif

namespace common = ::cypher::common;
namespace math = ::cypher::math;
namespace render = ::cypher::engine::render;
namespace sys = ::cypher::engine::sys;
namespace world = ::cypher::engine::world;
namespace camera = ::cypher::samples::render;

namespace
{

struct cube_vertex_t {
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert( sizeof( cube_vertex_t ) == 32u );
static_assert( offsetof( cube_vertex_t, normal ) == 12u );
static_assert( offsetof( cube_vertex_t, uv ) == 24u );

// Four vertices per face preserve hard normals and a complete UV square.
// Every face's two triangles are counterclockwise when viewed from outside.
constexpr cube_vertex_t CUBE_VERTICES[] = {
    { { 1, -1, -1 }, { 1, 0, 0 }, { 0, 0 } },
    { { 1, 1, -1 }, { 1, 0, 0 }, { 1, 0 } },
    { { 1, 1, 1 }, { 1, 0, 0 }, { 1, 1 } },
    { { 1, -1, 1 }, { 1, 0, 0 }, { 0, 1 } },
    { { -1, 1, -1 }, { -1, 0, 0 }, { 0, 0 } },
    { { -1, -1, -1 }, { -1, 0, 0 }, { 1, 0 } },
    { { -1, -1, 1 }, { -1, 0, 0 }, { 1, 1 } },
    { { -1, 1, 1 }, { -1, 0, 0 }, { 0, 1 } },
    { { 1, 1, -1 }, { 0, 1, 0 }, { 0, 0 } },
    { { -1, 1, -1 }, { 0, 1, 0 }, { 1, 0 } },
    { { -1, 1, 1 }, { 0, 1, 0 }, { 1, 1 } },
    { { 1, 1, 1 }, { 0, 1, 0 }, { 0, 1 } },
    { { -1, -1, -1 }, { 0, -1, 0 }, { 0, 0 } },
    { { 1, -1, -1 }, { 0, -1, 0 }, { 1, 0 } },
    { { 1, -1, 1 }, { 0, -1, 0 }, { 1, 1 } },
    { { -1, -1, 1 }, { 0, -1, 0 }, { 0, 1 } },
    { { -1, -1, 1 }, { 0, 0, 1 }, { 0, 0 } },
    { { 1, -1, 1 }, { 0, 0, 1 }, { 1, 0 } },
    { { 1, 1, 1 }, { 0, 0, 1 }, { 1, 1 } },
    { { -1, 1, 1 }, { 0, 0, 1 }, { 0, 1 } },
    { { -1, 1, -1 }, { 0, 0, -1 }, { 0, 0 } },
    { { 1, 1, -1 }, { 0, 0, -1 }, { 1, 0 } },
    { { 1, -1, -1 }, { 0, 0, -1 }, { 1, 1 } },
    { { -1, -1, -1 }, { 0, 0, -1 }, { 0, 1 } }
};

constexpr common::u16 CUBE_INDICES[] = {
    0, 1, 2, 0, 2, 3,       4, 5, 6, 4, 6, 7,
    8, 9, 10, 8, 10, 11,    12, 13, 14, 12, 14, 15,
    16, 17, 18, 16, 18, 19, 20, 21, 22, 20, 22, 23
};

// XY is the ground plane in the engine's Z-up world. Counterclockwise winding
// from above gives these two triangles their +Z normal for back-face culling.
constexpr cube_vertex_t FLOOR_VERTICES[] = {
    { { -6.0f, -5.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
    { { 6.0f, -5.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 2.0f, 0.0f } },
    { { 6.0f, 5.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 2.0f, 2.0f } },
    { { -6.0f, 5.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 2.0f } }
};
constexpr common::u16 FLOOR_INDICES[] = { 0u, 1u, 2u, 0u, 2u, 3u };

// One oversized triangle covers the presentation viewport without a diagonal
// seam. Its vertex shader derives UVs from these clip-space positions.
struct fullscreen_vertex_t {
    float position[2];
};
constexpr fullscreen_vertex_t FULLSCREEN_VERTICES[] = {
    { { -1.0f, -1.0f } }, { { 3.0f, -1.0f } }, { { -1.0f, 3.0f } }
};
constexpr common::u16 FULLSCREEN_INDICES[] = { 0u, 1u, 2u };

// mat4_t is column-major. The two structures match separate GLSL std140 blocks.
struct alignas( 16 ) cube_view_constants_t {
    math::mat4_t view;
    math::mat4_t projection;
    alignas( 16 ) float ambientLight[4];
    alignas( 16 ) float lightDirection[4];
    alignas( 16 ) float lightColor[4];
    math::mat4_t lightViewProjection;
};
static_assert( sizeof( cube_view_constants_t ) == 240u );
static_assert( offsetof( cube_view_constants_t, projection ) == 64u );
static_assert( offsetof( cube_view_constants_t, ambientLight ) == 128u );
static_assert( offsetof( cube_view_constants_t, lightDirection ) == 144u );
static_assert( offsetof( cube_view_constants_t, lightColor ) == 160u );
static_assert( offsetof( cube_view_constants_t, lightViewProjection ) == 176u );

struct alignas( 16 ) shadow_light_constants_t {
    math::mat4_t lightViewProjection;
};
static_assert( sizeof( shadow_light_constants_t ) == 64u );

struct alignas( 16 ) shadow_object_constants_t {
    math::mat4_t model;
};
static_assert( sizeof( shadow_object_constants_t ) == 64u );

struct alignas( 16 ) cube_object_constants_t {
    math::mat4_t model;
    alignas( 16 ) float tint[4];
};
static_assert( sizeof( cube_object_constants_t ) == 80u );
static_assert( offsetof( cube_object_constants_t, tint ) == 64u );

struct cube_options_t {
    const char *shaderPath{ CYPHER_RENDER_CUBE_SHADER_PATH };
    const char *shadowShaderPath{ CYPHER_RENDER_CUBE_SHADOW_SHADER_PATH };
    const char *toneMapShaderPath{ CYPHER_RENDER_CUBE_TONEMAP_SHADER_PATH };
    const char *texturePath{ CYPHER_RENDER_CUBE_TEXTURE_PATH };
    common::u32 frameLimit{ 0u }; // Zero is an interactive run.
    bool hidden{ false };
    bool help{ false };
};

struct cube_instance_t {
    math::vec3_t position;
};

constexpr cube_instance_t CUBE_INSTANCES[] = {
    { { -2.4f, -1.6f, 0.0f } },
    { { 0.0f, 0.0f, 0.0f } },
    { { 2.4f, 1.6f, 0.0f } },
    // Deliberately behind the camera. The World query rejects this object.
    { { 12.0f, -18.0f, 9.6f } }
};
constexpr common::u32 CUBE_INSTANCE_COUNT =
    static_cast<common::u32>( sizeof( CUBE_INSTANCES ) / sizeof( CUBE_INSTANCES[0] ) );
constexpr common::u32 FLOOR_OBJECT_INDEX = CUBE_INSTANCE_COUNT;
// Two overlapping, camera-visible translucent panels reuse the floor quad.
// Their object slots remain separate from opaque casters and are sorted per view.
constexpr math::vec3_t PANEL_POSITIONS[] = {
    { -0.15f, -0.40f, 1.50f }, { 0.20f, -0.15f, 1.78f }
};
constexpr math::vec3_t PANEL_SCALE{ 0.16f, 0.16f, 1.0f };
constexpr common::u32 PANEL_OBJECT_BEGIN = FLOOR_OBJECT_INDEX + 1u;
constexpr common::u32 PANEL_COUNT =
    static_cast<common::u32>( sizeof( PANEL_POSITIONS ) / sizeof( PANEL_POSITIONS[0] ) );
constexpr common::u32 WORLD_OBJECT_COUNT = PANEL_OBJECT_BEGIN + PANEL_COUNT;
constexpr common::u32 MESH_SLOT_CUBE = 0u;
constexpr common::u32 MESH_SLOT_FLOOR = 1u;
constexpr common::u32 MESH_SLOT_COUNT = 2u;
constexpr common::u32 TEXTURE_SLOT_WHITE = 0u;
constexpr common::u32 TEXTURE_SLOT_FLOOR = 1u;
constexpr common::u32 TEXTURE_SLOT_COUNT = 2u;

struct sample_material_t {
    float tint[4];
    common::u32 textureSlot;
};
// Logical material slots contain appearance and another logical texture slot,
// never a GPU resource name or a renderer handle.
constexpr sample_material_t MATERIALS[WORLD_OBJECT_COUNT] = {
    { { 0.75f, 0.28f, 0.18f, 1.0f }, TEXTURE_SLOT_WHITE },
    { { 0.10f, 0.55f, 0.76f, 1.0f }, TEXTURE_SLOT_WHITE },
    { { 0.36f, 0.72f, 0.32f, 1.0f }, TEXTURE_SLOT_WHITE },
    { { 0.72f, 0.28f, 0.68f, 1.0f }, TEXTURE_SLOT_WHITE },
    { { 0.90f, 0.90f, 0.90f, 1.0f }, TEXTURE_SLOT_FLOOR },
    { { 0.18f, 0.70f, 0.86f, 0.42f }, TEXTURE_SLOT_WHITE },
    { { 0.95f, 0.42f, 0.32f, 0.38f }, TEXTURE_SLOT_WHITE }
};
constexpr common::u32 MATERIAL_COUNT =
    static_cast<common::u32>( sizeof( MATERIALS ) / sizeof( MATERIALS[0] ) );

struct cube_state_t {
    sys::window_t window{};
    bool cameraCursorHidden{ false };
    render::render_shader_handle_t shader{};
    render::render_shader_handle_t shadowShader{};
    render::render_shader_handle_t toneMapShader{};
    render::render_pipeline_handle_t pipeline{};
    render::render_pipeline_handle_t transparentPipeline{};
    render::render_pipeline_handle_t shadowPipeline{};
    render::render_pipeline_handle_t toneMapPipeline{};
    render::render_texture_handle_t shadowDepth{};
    render::render_target_handle_t shadowTarget{};
    render::render_texture_handle_t sceneColor{};
    render::render_target_handle_t sceneTarget{};
    render::render_sampler_handle_t shadowSampler{};
    // The host owns this bounded slot table; World stores only slot numbers.
    render::render_static_mesh_bundle_t meshes[MESH_SLOT_COUNT]{};
    render::render_static_mesh_bundle_t fullscreenMesh{};
    render::render_texture_handle_t textures[TEXTURE_SLOT_COUNT]{};
    render::render_buffer_handle_t viewConstants{};
    render::render_buffer_handle_t lightConstants{};
    render::render_uniform_batch_t objectConstants{};
    render::render_draw_queue_t *pDrawQueue{ nullptr };
    render::render_draw_queue_t *pTransparentQueue{ nullptr };
    render::render_draw_queue_t *pShadowQueue{ nullptr };
    world::world_t *pWorld{ nullptr };
    world::world_submission_t *pSubmission{ nullptr };
    world::world_object_id_t objectIds[WORLD_OBJECT_COUNT]{};
};

constexpr math::vec3_t CUBE_CAMERA_POSITION{ 4.0f, -6.0f, 3.2f };
constexpr math::aabb_t CUBE_LOCAL_BOUNDS{
    { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f } };
constexpr math::vec3_t FLOOR_POSITION{ 0.0f, 0.0f, -1.8f };
constexpr math::vec3_t SHADOW_LIGHT_POSITION{ 12.0f, -21.0f, 30.0f };
constexpr common::u32 SHADOW_MAP_SIZE = 1024u;
constexpr math::aabb_t FLOOR_LOCAL_BOUNDS{
    { -6.0f, -5.0f, 0.0f }, { 6.0f, 5.0f, 0.0f } };

struct cube_input_state_t {
    bool held[static_cast<common::u32>( sys::sys_key_t::COUNT )]{};
    float lookDeltaX{ 0.0f };
    float lookDeltaY{ 0.0f };
};

bool KeyHeld( const cube_input_state_t &input, const sys::sys_key_t key ) noexcept
{
    return input.held[static_cast<common::u32>( key )];
}

void ClearCameraInput( cube_input_state_t &input ) noexcept
{
    input = {};
}

bool CheckWorld( world::world_error_t result, const char *operation ) noexcept
{
    if ( result == world::world_error_t::OK ) return true;
    std::fprintf( stderr, "%s: World error %u\n", operation,
        static_cast<unsigned>( result ) );
    return false;
}

bool CheckSubmission(
    world::world_submission_error_t result, const char *operation ) noexcept
{
    if ( result == world::world_submission_error_t::OK ) return true;
    std::fprintf( stderr, "%s: World submission error %u\n", operation,
        static_cast<unsigned>( result ) );
    return false;
}

bool CheckRender( render::render_error_t result, const char *operation ) noexcept
{
    if ( result == render::render_error_t::OK ) return true;
    std::fprintf( stderr, "%s: %s (%s)\n", operation,
        render::R_ErrorName( result ), render::R_ErrorDescription( result ) );
    return false;
}

bool CheckSystem( sys::sys_error_t result, const char *operation ) noexcept
{
    if ( result == sys::sys_error_t::OK ) return true;
    std::fprintf( stderr, "%s: %s\n", operation, sys::Sys_ErrorName( result ) );
    return false;
}

bool SetCameraCapture( cube_state_t &state, const bool captured ) noexcept
{
    if ( captured ) {
        if ( state.window.relativeMouseEnabled && state.cameraCursorHidden ) return true;
        if ( !CheckSystem( sys::Sys_SetRelativeMouseMode( state.window, true ),
                 "Capture camera mouse" ) ) return false;
        if ( !CheckSystem( sys::Sys_SetCursorVisible( false ),
                 "Hide camera cursor" ) ) {
            (void)sys::Sys_SetRelativeMouseMode( state.window, false );
            return false;
        }
        state.cameraCursorHidden = true;
        return true;
    }

    bool success = true;
    if ( state.window.relativeMouseEnabled ) {
        success = CheckSystem( sys::Sys_SetRelativeMouseMode( state.window, false ),
            "Release camera mouse" ) && success;
    }
    if ( state.cameraCursorHidden ) {
        if ( CheckSystem( sys::Sys_SetCursorVisible( true ),
                 "Restore camera cursor" ) ) {
            state.cameraCursorHidden = false;
        } else {
            success = false;
        }
    }
    return success;
}

bool ParseOptions( int argc, char **argv, cube_options_t &options ) noexcept
{
    for ( int i = 1; i < argc; ++i ) {
        if ( std::strcmp( argv[i], "--hidden" ) == 0 ) options.hidden = true;
        else if ( std::strcmp( argv[i], "--help" ) == 0 ) options.help = true;
        else if ( std::strcmp( argv[i], "--shader" ) == 0 && i + 1 < argc ) {
            options.shaderPath = argv[++i];
        } else if ( std::strcmp( argv[i], "--shadow-shader" ) == 0 && i + 1 < argc ) {
            options.shadowShaderPath = argv[++i];
        } else if ( std::strcmp( argv[i], "--tonemap-shader" ) == 0 && i + 1 < argc ) {
            options.toneMapShaderPath = argv[++i];
        } else if ( std::strcmp( argv[i], "--texture" ) == 0 && i + 1 < argc ) {
            options.texturePath = argv[++i];
        } else if ( std::strcmp( argv[i], "--frames" ) == 0 && i + 1 < argc ) {
            const char *first = argv[++i];
            const char *last = first + std::strlen( first );
            const auto parsed = std::from_chars( first, last, options.frameLimit );
            if ( parsed.ec != std::errc{} || parsed.ptr != last || options.frameLimit == 0u ) {
                std::fprintf( stderr, "--frames requires a positive 32-bit integer.\n" );
                return false;
            }
        } else {
            std::fprintf( stderr, "Unknown option or missing value: %s\n", argv[i] );
            return false;
        }
    }
    // Hidden runs should always terminate without needing keyboard input.
    if ( options.hidden && options.frameLimit == 0u ) options.frameLimit = 120u;
    return true;
}

bool ReadCookedFile( const char *nativePath, common::usize maxBytes,
    common::blob_t &bytes )
{
    const std::filesystem::path path = std::filesystem::absolute( nativePath );
    const std::string root = path.parent_path().string();
    const std::string filename = path.filename().string();
    common::vfs_directory_t directory{};
    const common::vfs_status_t mountResult = common::VfsDirectory_Init(
        &directory, { root.data(), root.size() } );
    if ( mountResult != common::vfs_status_t::OK ) {
        std::fprintf( stderr, "Cooked asset directory: %s (%s)\n", root.c_str(),
            common::Vfs_StatusName( mountResult ) );
        return false;
    }

    if ( !common::Blob_Init( &bytes, common::Allocator_GetSystem() ) ) {
        common::VfsDirectory_Shutdown( &directory );
        std::fprintf( stderr, "Could not initialize cooked asset storage.\n" );
        return false;
    }
    const common::vfs_t vfs = common::VfsDirectory_Make( &directory );
    const common::vfs_status_t readResult = common::Vfs_ReadAll(
        &vfs, { filename.data(), filename.size() }, maxBytes, &bytes );
    common::VfsDirectory_Shutdown( &directory );
    if ( readResult != common::vfs_status_t::OK ) {
        std::fprintf( stderr, "Read cooked asset %s: %s\n", nativePath,
            common::Vfs_StatusName( readResult ) );
        common::Blob_Shutdown( &bytes );
        return false;
    }
    return true;
}

bool LoadShader( const char *nativePath, render::render_shader_handle_t &shader )
{
    common::blob_t bytes{};
    constexpr common::usize MAX_SHADER_FILE_BYTES = 40u * 1024u * 1024u;
    if ( !ReadCookedFile( nativePath, MAX_SHADER_FILE_BYTES, bytes ) ) return false;

    common::cooked_shader_view_t view{};
    const common::cooked_shader_result_t cookedResult =
        common::CookedShader_Read( common::Blob_Block( &bytes ), &view );
    if ( !common::CookedShader_Succeeded( cookedResult ) ) {
        std::fprintf( stderr, "Invalid cooked shader %s: %s\n", nativePath,
            common::CookedShader_StatusName( cookedResult.status ) );
        common::Blob_Shutdown( &bytes );
        return false;
    }

    const render::render_shader_desc_t description{ &view, "First-draw cube shader" };
    // Creation consumes the view synchronously. Release its backing bytes afterward.
    const bool created = CheckRender(
        render::R_CreateShader( description, &shader ), "Create shader" );
    common::Blob_Shutdown( &bytes );
    return created;
}

bool LoadTexture( const char *nativePath, render::render_texture_handle_t &texture )
{
    common::blob_t bytes{};
    constexpr common::usize MAX_TEXTURE_FILE_BYTES = 40u * 1024u * 1024u;
    if ( !ReadCookedFile( nativePath, MAX_TEXTURE_FILE_BYTES, bytes ) ) return false;

    common::cooked_texture_view_t view{};
    const common::cooked_texture_result_t cookedResult =
        common::CookedTexture_Read( common::Blob_Block( &bytes ), &view );
    if ( !common::CookedTexture_Succeeded( cookedResult ) ) {
        std::fprintf( stderr, "Invalid cooked texture %s: %s\n", nativePath,
            common::CookedTexture_StatusName( cookedResult.status ) );
        common::Blob_Shutdown( &bytes );
        return false;
    }

    // The renderer consumes every mip synchronously; no borrowed VFS bytes
    // survive beyond this call, including when creation fails.
    const bool created = CheckRender(
        render::R_CreateTexture2DFromCooked( view, &texture ),
        "Create floor texture" );
    common::Blob_Shutdown( &bytes );
    return created;
}

// Resource creation and destruction happen between frames. On failure the
// still-owned texture remains in state for Shutdown to release. Destroy the
// target before its attachment so renderer retention is never violated.
bool RecreateSceneTarget( cube_state_t &state,
    const render::render_extent_t extent ) noexcept
{
    if ( extent.width == 0u || extent.height == 0u ) return false;
    if ( state.sceneTarget.value != 0u ) {
        if ( !CheckRender( render::R_DestroyRenderTarget( state.sceneTarget ),
                 "Destroy old HDR scene target" ) ) return false;
        state.sceneTarget = {};
    }
    if ( state.sceneColor.value != 0u ) {
        if ( !CheckRender( render::R_DestroyTexture( state.sceneColor ),
                 "Destroy old HDR scene color" ) ) return false;
        state.sceneColor = {};
    }

    render::render_texture_desc_t color{};
    color.width = extent.width;
    color.height = extent.height;
    color.sRGB = false;
    color.generateMips = false;
    color.repeat = false;
    color.renderTarget = true;
    color.format = render::render_texture_format_t::RGBA16F;
    color.debugName = "Graybox linear HDR scene color";
    return CheckRender( render::R_CreateTexture2D( color, {},
               &state.sceneColor ), "Create HDR scene color" ) &&
        CheckRender( render::R_CreateRenderTarget( { state.sceneColor, {} },
            &state.sceneTarget ), "Create HDR scene target" );
}

bool CreateScene( cube_state_t &state, const cube_options_t &options )
{
    if ( !LoadShader( options.shaderPath, state.shader ) ||
         !LoadShader( options.shadowShaderPath, state.shadowShader ) ||
         !LoadShader( options.toneMapShaderPath, state.toneMapShader ) ) return false;

    constexpr common::u8 WHITE_PIXEL[4] = { 255u, 255u, 255u, 255u };
    render::render_texture_desc_t white{};
    white.width = 1u;
    white.height = 1u;
    white.generateMips = false;
    white.debugName = "Graybox white texture";
    if ( !CheckRender( render::R_CreateTexture2D( white,
            { WHITE_PIXEL, sizeof( WHITE_PIXEL ) },
            &state.textures[TEXTURE_SLOT_WHITE] ), "Create white texture" ) ||
         !LoadTexture( options.texturePath,
             state.textures[TEXTURE_SLOT_FLOOR] ) ) return false;

    render::render_texture_desc_t shadowDepth{};
    shadowDepth.width = SHADOW_MAP_SIZE;
    shadowDepth.height = SHADOW_MAP_SIZE;
    shadowDepth.sRGB = false;
    shadowDepth.generateMips = false;
    shadowDepth.repeat = false;
    shadowDepth.renderTarget = true;
    shadowDepth.format = render::render_texture_format_t::D24;
    shadowDepth.debugName = "Graybox directional shadow depth";
    if ( !CheckRender( render::R_CreateTexture2D( shadowDepth, {},
             &state.shadowDepth ), "Create shadow depth texture" ) ||
         !CheckRender( render::R_CreateRenderTarget( { {}, state.shadowDepth },
             &state.shadowTarget ), "Create shadow target" ) ) return false;
    render::render_sampler_desc_t shadowSampler{};
    shadowSampler.minFilter = render::render_sampler_filter_t::NEAREST;
    shadowSampler.magFilter = render::render_sampler_filter_t::NEAREST;
    shadowSampler.addressU = render::render_sampler_address_t::CLAMP_TO_EDGE;
    shadowSampler.addressV = render::render_sampler_address_t::CLAMP_TO_EDGE;
    shadowSampler.compare = render::render_sampler_compare_t::LESS_EQUAL;
    shadowSampler.debugName = "Graybox shadow comparison";
    if ( !CheckRender( render::R_CreateSampler( shadowSampler,
             &state.shadowSampler ), "Create shadow sampler" ) ) return false;
    render::render_static_mesh_source_t source{};
    source.vertexLayout.bindingCount = 1u;
    source.vertexLayout.bindings[0].stride = sizeof( cube_vertex_t );
    source.vertexLayout.attributeCount = 3u;
    source.vertexLayout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    source.vertexLayout.attributes[1] = { render::render_format_t::RGB32_FLOAT, 12u, 1u, 0u };
    source.vertexLayout.attributes[2] = { render::render_format_t::RG32_FLOAT, 24u, 2u, 0u };
    source.vertexBytes = CUBE_VERTICES;
    source.vertexByteSize = sizeof( CUBE_VERTICES );
    source.vertexCount = static_cast<common::u32>( sizeof( CUBE_VERTICES ) / sizeof( CUBE_VERTICES[0] ) );
    source.indexBytes = CUBE_INDICES;
    source.indexByteSize = sizeof( CUBE_INDICES );
    source.indexCount = static_cast<common::u32>( sizeof( CUBE_INDICES ) / sizeof( CUBE_INDICES[0] ) );
    source.indexType = render::render_index_type_t::UINT16;
    source.debugName = "Shared cube mesh";
    auto meshResult = render::R_CreateStaticMeshBundle(
        source, &state.meshes[MESH_SLOT_CUBE] );
    if ( meshResult.status != render::render_static_mesh_bridge_status_t::OK ) {
        std::fprintf( stderr, "Create cube mesh: %s (%u)\n",
            render::R_ErrorName( meshResult.renderError ),
            static_cast<unsigned>( meshResult.status ) );
        return false;
    }

    source.vertexBytes = FLOOR_VERTICES;
    source.vertexByteSize = sizeof( FLOOR_VERTICES );
    source.vertexCount = static_cast<common::u32>(
        sizeof( FLOOR_VERTICES ) / sizeof( FLOOR_VERTICES[0] ) );
    source.indexBytes = FLOOR_INDICES;
    source.indexByteSize = sizeof( FLOOR_INDICES );
    source.indexCount = static_cast<common::u32>(
        sizeof( FLOOR_INDICES ) / sizeof( FLOOR_INDICES[0] ) );
    source.debugName = "Graybox floor mesh";
    meshResult = render::R_CreateStaticMeshBundle(
        source, &state.meshes[MESH_SLOT_FLOOR] );
    if ( meshResult.status != render::render_static_mesh_bridge_status_t::OK ) {
        std::fprintf( stderr, "Create floor mesh: %s (%u)\n",
            render::R_ErrorName( meshResult.renderError ),
            static_cast<unsigned>( meshResult.status ) );
        return false;
    }

    source = {};
    source.vertexLayout.bindingCount = 1u;
    source.vertexLayout.bindings[0].stride = sizeof( fullscreen_vertex_t );
    source.vertexLayout.attributeCount = 1u;
    source.vertexLayout.attributes[0] = {
        render::render_format_t::RG32_FLOAT, 0u, 0u, 0u };
    source.vertexBytes = FULLSCREEN_VERTICES;
    source.vertexByteSize = sizeof( FULLSCREEN_VERTICES );
    source.vertexCount = 3u;
    source.indexBytes = FULLSCREEN_INDICES;
    source.indexByteSize = sizeof( FULLSCREEN_INDICES );
    source.indexCount = 3u;
    source.indexType = render::render_index_type_t::UINT16;
    source.debugName = "Indexed fullscreen triangle";
    meshResult = render::R_CreateStaticMeshBundle( source, &state.fullscreenMesh );
    if ( meshResult.status != render::render_static_mesh_bridge_status_t::OK ) {
        std::fprintf( stderr, "Create fullscreen triangle: %s (%u)\n",
            render::R_ErrorName( meshResult.renderError ),
            static_cast<unsigned>( meshResult.status ) );
        return false;
    }

    render::render_pipeline_desc_t pipeline{};
    pipeline.shader = state.shader;
    pipeline.vertexLayout = state.meshes[MESH_SLOT_CUBE].vertexLayout;
    pipeline.depthTest = true;
    pipeline.depthWrite = true;
    pipeline.cullBackFaces = true;
    pipeline.frontCounterClockwise = true;
    pipeline.alphaBlend = false;
    pipeline.uniformBlockCount = 2u;
    pipeline.uniformBlocks[0] = { "ViewConstants", sizeof( cube_view_constants_t ) };
    pipeline.uniformBlocks[1] = { "ObjectConstants", sizeof( cube_object_constants_t ) };
    pipeline.sampledTextureCount = 2u;
    pipeline.sampledTextures[0] = { "albedoTexture",
        render::render_sampled_texture_kind_t::SCALAR_2D };
    pipeline.sampledTextures[1] = { "shadowMap",
        render::render_sampled_texture_kind_t::DEPTH_COMPARE_2D };
    pipeline.debugName = "Depth-tested graybox pipeline";
    if ( !CheckRender( render::R_CreateGraphicsPipeline( pipeline, &state.pipeline ),
            "Create cube pipeline" ) ) return false;

    // Transparent surfaces test against the opaque scene depth but leave that
    // depth intact so later translucent surfaces can blend over one another.
    pipeline.depthWrite = false;
    pipeline.alphaBlend = true;
    pipeline.debugName = "Depth-tested translucent graybox pipeline";
    if ( !CheckRender( render::R_CreateGraphicsPipeline( pipeline,
             &state.transparentPipeline ), "Create translucent pipeline" ) ) return false;

    pipeline.shader = state.shadowShader;
    pipeline.depthWrite = true;
    pipeline.alphaBlend = false;
    pipeline.colorWrite = false;
    // Bias depth written by filled shadow casters. This avoids relying only
    // on the receiver's fixed comparison bias in the fragment shader.
    pipeline.depthBiasEnable = true;
    pipeline.depthBiasSlopeFactor = 1.25f;
    pipeline.depthBiasConstantUnits = 2.0f;
    pipeline.uniformBlocks[0] = { "LightConstants", sizeof( shadow_light_constants_t ) };
    pipeline.uniformBlocks[1] = { "ObjectConstants", sizeof( shadow_object_constants_t ) };
    pipeline.sampledTextureCount = 0u;
    pipeline.sampledTextures[0] = {};
    pipeline.sampledTextures[1] = {};
    pipeline.debugName = "Graybox directional shadow depth pipeline";
    if ( !CheckRender( render::R_CreateGraphicsPipeline( pipeline,
             &state.shadowPipeline ), "Create shadow pipeline" ) ) return false;

    pipeline = {};
    pipeline.shader = state.toneMapShader;
    pipeline.vertexLayout = state.fullscreenMesh.vertexLayout;
    pipeline.depthTest = false;
    pipeline.depthWrite = false;
    pipeline.cullBackFaces = false;
    pipeline.sampledTextureCount = 1u;
    pipeline.sampledTextures[0] = { "sceneColor",
        render::render_sampled_texture_kind_t::SCALAR_2D };
    pipeline.debugName = "Linear HDR tone-map presentation pipeline";
    if ( !CheckRender( render::R_CreateGraphicsPipeline( pipeline,
             &state.toneMapPipeline ), "Create tone-map pipeline" ) ) return false;

    render::render_buffer_desc_t viewBuffer{};
    viewBuffer.byteSize = sizeof( cube_view_constants_t );
    viewBuffer.usage = render::R_BUFFER_USAGE_UNIFORM;
    viewBuffer.updatePolicy = render::render_buffer_update_t::STREAM;
    viewBuffer.memory = render::render_buffer_memory_t::UPLOAD;
    viewBuffer.debugName = "Cube view constants";
    if ( !CheckRender( render::R_CreateBuffer( viewBuffer, nullptr,
            &state.viewConstants ), "Create view constants" ) ) return false;

    viewBuffer.byteSize = sizeof( shadow_light_constants_t );
    viewBuffer.debugName = "Graybox light constants";
    if ( !CheckRender( render::R_CreateBuffer( viewBuffer, nullptr,
             &state.lightConstants ), "Create light constants" ) ) return false;

    const render::render_info_t *info = render::R_GetInfo();
    if ( info == nullptr || info->limits.uniformBufferOffsetAlignment == 0u ) return false;
    const common::u64 alignment = info->limits.uniformBufferOffsetAlignment;
    const common::u64 recordStride =
        ( sizeof( cube_object_constants_t ) + alignment - 1u ) / alignment * alignment;
    const common::u64 capacity =
        ( WORLD_OBJECT_COUNT * 2u - 1u ) * recordStride + sizeof( cube_object_constants_t );
    if ( !CheckRender( render::R_CreateUniformBatch( capacity, nullptr,
            &state.objectConstants ), "Create object constants batch" ) ) return false;
    return CheckRender( render::R_InitDrawQueue( &state.pDrawQueue,
               common::Allocator_GetSystem(), WORLD_OBJECT_COUNT ),
               "Create graybox draw queue" ) &&
        CheckRender( render::R_InitDrawQueue( &state.pTransparentQueue,
            common::Allocator_GetSystem(), PANEL_COUNT ),
            "Create translucent draw queue" ) &&
        CheckRender( render::R_InitDrawQueue( &state.pShadowQueue,
            common::Allocator_GetSystem(), WORLD_OBJECT_COUNT ),
            "Create shadow draw queue" );
}

bool CreateWorld( cube_state_t &state ) noexcept
{
    if ( !CheckWorld( world::World_Init( &state.pWorld,
             common::Allocator_GetSystem(), WORLD_OBJECT_COUNT ),
             "Create cube world" ) ) return false;
    if ( !CheckSubmission( world::WorldSubmission_Init( &state.pSubmission,
             common::Allocator_GetSystem(), WORLD_OBJECT_COUNT ),
             "Create cube view storage" ) ) return false;

    for ( common::u32 i = 0u; i < CUBE_INSTANCE_COUNT; ++i ) {
        world::world_object_t object{};
        object.localToWorld = math::Affine3_FromTranslation(
            CUBE_INSTANCES[i].position );
        object.worldBounds = math::Aabb_TransformAffine(
            CUBE_LOCAL_BOUNDS, object.localToWorld );
        object.layerMask = 1u;
        object.meshSlot = MESH_SLOT_CUBE;
        object.materialSlot = i;
        if ( !CheckWorld( world::World_AddObject( state.pWorld,
                 &object, &state.objectIds[i] ), "Add cube world object" ) ) {
            return false;
        }
    }
    world::world_object_t floor{};
    floor.localToWorld = math::Affine3_FromTranslation( FLOOR_POSITION );
    floor.worldBounds = math::Aabb_TransformAffine(
        FLOOR_LOCAL_BOUNDS, floor.localToWorld );
    floor.layerMask = 1u;
    floor.meshSlot = MESH_SLOT_FLOOR;
    floor.materialSlot = FLOOR_OBJECT_INDEX;
    if ( !CheckWorld( world::World_AddObject( state.pWorld,
             &floor, &state.objectIds[FLOOR_OBJECT_INDEX] ),
             "Add floor world object" ) ) return false;
    for ( common::u32 i = 0u; i < PANEL_COUNT; ++i ) {
        world::world_object_t panel{};
        panel.localToWorld = math::Affine3_Multiply(
            math::Affine3_FromTranslation( PANEL_POSITIONS[i] ),
            math::Affine3_FromScale( PANEL_SCALE ) );
        panel.worldBounds = math::Aabb_TransformAffine(
            FLOOR_LOCAL_BOUNDS, panel.localToWorld );
        panel.layerMask = 1u;
        panel.meshSlot = MESH_SLOT_FLOOR;
        panel.materialSlot = PANEL_OBJECT_BEGIN + i;
        if ( !CheckWorld( world::World_AddObject( state.pWorld,
                 &panel, &state.objectIds[PANEL_OBJECT_BEGIN + i] ),
                 "Add translucent world panel" ) ) return false;
    }
    return true;
}

bool PollSampleInput(
    cube_state_t &state, cube_input_state_t &input,
    const bool interactive ) noexcept
{
    // Native events are pumped before we can enable relative mode. Motion from
    // the capture-start pump therefore belongs to the old pointer mode.
    const bool capturedAtPumpStart =
        state.window.relativeMouseEnabled && state.cameraCursorHidden;
    bool motionEligible = capturedAtPumpStart;
    bool wantsCapture = capturedAtPumpStart;
    bool focused = state.window.focused;
    input.lookDeltaX = 0.0f;
    input.lookDeltaY = 0.0f;
    const common::u64 droppedBefore = sys::Sys_DroppedEventCount();
    sys::Sys_PollWindowEvents( state.window );
    const bool overflowed = sys::Sys_DroppedEventCount() != droppedBefore;

    sys::sys_event_t event{};
    while ( sys::Sys_PollEvent( event ) ) {
        if ( event.type == sys::sys_event_type_t::QUIT_REQUESTED ) {
            sys::Sys_RequestQuit();
            continue;
        }
        if ( event.type == sys::sys_event_type_t::WINDOW_FOCUS_LOST &&
             event.payload.window.windowId == state.window.id ) {
            focused = false;
            wantsCapture = false;
            motionEligible = false;
            ClearCameraInput( input );
            continue;
        }
        if ( event.type == sys::sys_event_type_t::WINDOW_FOCUS_GAINED &&
             event.payload.window.windowId == state.window.id ) {
            focused = true;
            continue;
        }
        if ( event.type == sys::sys_event_type_t::WINDOW_MINIMIZED &&
             event.payload.window.windowId == state.window.id ) {
            wantsCapture = false;
            motionEligible = false;
            ClearCameraInput( input );
            continue;
        }
        if ( event.type == sys::sys_event_type_t::KEY &&
             event.payload.key.windowId == state.window.id ) {
            const auto key = event.payload.key.key;
            const auto action = event.payload.key.action;
            if ( key == sys::sys_key_t::ESCAPE &&
                 action == sys::sys_input_action_t::PRESSED ) {
                sys::Sys_RequestQuit();
            } else if ( interactive && focused ) {
                if ( action == sys::sys_input_action_t::PRESSED ) {
                    input.held[static_cast<common::u32>( key )] = true;
                } else if ( action == sys::sys_input_action_t::RELEASED ) {
                    input.held[static_cast<common::u32>( key )] = false;
                }
                // Key repeats do not change a held-key transition.
            }
            continue;
        }
        if ( !interactive ) continue;
        if ( event.type == sys::sys_event_type_t::MOUSE_BUTTON &&
             event.payload.mouseButton.windowId == state.window.id &&
             event.payload.mouseButton.button == sys::sys_mouse_button_t::RIGHT ) {
            if ( event.payload.mouseButton.action == sys::sys_input_action_t::RELEASED ) {
                wantsCapture = false;
                motionEligible = false;
                input.lookDeltaX = 0.0f;
                input.lookDeltaY = 0.0f;
            } else if ( focused ) {
                wantsCapture = true;
            }
            continue;
        }
        if ( event.type == sys::sys_event_type_t::MOUSE_MOTION &&
             event.payload.mouseMotion.windowId == state.window.id &&
             focused && motionEligible && wantsCapture ) {
            input.lookDeltaX += event.payload.mouseMotion.deltaX;
            input.lookDeltaY += event.payload.mouseMotion.deltaY;
        }
    }

    // The queue drops its oldest event on overflow, which may be a key-up or
    // mouse release. Clear every held control instead of guessing its state.
    if ( overflowed || !state.window.focused || state.window.minimized ||
         sys::Sys_IsQuitRequested() || sys::Sys_WindowShouldClose( state.window ) ) {
        wantsCapture = false;
        ClearCameraInput( input );
    }
    return !interactive || SetCameraCapture( state, wantsCapture );
}

bool RunFrames( cube_state_t &state, const cube_options_t &options ) noexcept
{
    const double started = sys::Sys_TimeNowSeconds();
    double previous = started;
    const double timeout = std::max( 30.0, 10.0 + options.frameLimit / 15.0 );
    float rotationTime = 0.0f;
    common::u32 renderedFrames = 0u;
    common::u64 visibleDraws = 0u;
    common::u64 transparentDraws = 0u;
    common::u64 shadowDraws = 0u;
    common::u64 toneMapDraws = 0u;
    render::render_extent_t extent = render::R_GetInfo()->drawableExtent;
    const bool interactive = options.frameLimit == 0u && !options.hidden;
    cube_input_state_t input{};
    camera::cube_camera_t flyCamera{};
    if ( interactive && !camera::CubeCamera_FromLookAt(
             CUBE_CAMERA_POSITION, math::CY_VEC3_ZERO, 5.0f,
             &flyCamera ) ) {
        std::fprintf( stderr, "Could not initialize the sample fly camera.\n" );
        return false;
    }

    // One fixed orthographic light keeps this a small pass example. The light
    // looks toward the scene from the same direction used by the diffuse term.
    math::mat4_t lightView{};
    math::mat4_t lightProjection{};
    if ( !math::Mat4_TryLookAtRH( SHADOW_LIGHT_POSITION, math::CY_VEC3_ZERO,
             math::CY_VEC3_UP, 0.0001f, &lightView ) ||
         !math::Mat4_TryOrthographicRH( -24.0f, 24.0f, -24.0f, 24.0f,
             1.0f, 80.0f, math::clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
             &lightProjection ) ) {
        std::fprintf( stderr, "Could not construct the graybox light matrices.\n" );
        return false;
    }
    const math::mat4_t lightViewProjection =
        math::Mat4_Multiply( lightProjection, lightView );
    const shadow_light_constants_t lightConstants{ lightViewProjection };
    if ( !CheckRender( render::R_UpdateBuffer( state.lightConstants, 0u,
             { &lightConstants, sizeof( lightConstants ) } ),
             "Upload light constants" ) ) return false;

    while ( !sys::Sys_IsQuitRequested() && !sys::Sys_WindowShouldClose( state.window ) ) {
        if ( !PollSampleInput( state, input, interactive ) ) return false;
        if ( sys::Sys_IsQuitRequested() || sys::Sys_WindowShouldClose( state.window ) ) break;

        const double now = sys::Sys_TimeNowSeconds();
        const float delta = static_cast<float>( std::clamp( now - previous, 0.0, 0.1 ) );
        previous = now;
        if ( options.frameLimit != 0u && now - started > timeout ) {
            std::fprintf( stderr, "Timed out before completing %u frames.\n", options.frameLimit );
            return false;
        }
        if ( state.window.minimized || state.window.width == 0u || state.window.height == 0u ) {
            sys::Sys_SleepMilliseconds( 16u );
            continue;
        }
        if ( extent.width != state.window.width || extent.height != state.window.height ) {
            extent = { state.window.width, state.window.height };
            if ( !CheckRender( render::R_Resize( extent ), "Resize drawable" ) ||
                 !RecreateSceneTarget( state, extent ) ) return false;
        }

        // Finite runs have deterministic animation for smoke checks. Interactive
        // runs advance with elapsed time, capped after debugger/focus pauses.
        rotationTime += options.frameLimit == 0u ? delta : 1.0f / 60.0f;
        const math::quat_t yaw = math::Quat_FromUnitAxisAngle(
            math::CY_VEC3_UP, math::Angle_FromRadians( rotationTime * 0.55f ) );
        const math::quat_t tilt = math::Quat_FromUnitAxisAngle(
            math::CY_VEC3_FORWARD, math::Angle_FromRadians( 0.18f + rotationTime * 0.21f ) );
        math::mat4_t view{};
        math::mat4_t projection{};
        if ( interactive && state.window.relativeMouseEnabled && state.window.focused ) {
            camera::cube_camera_input_t cameraInput{};
            cameraInput.forward = static_cast<float>( KeyHeld( input, sys::sys_key_t::W ) ) -
                static_cast<float>( KeyHeld( input, sys::sys_key_t::S ) );
            cameraInput.right = static_cast<float>( KeyHeld( input, sys::sys_key_t::D ) ) -
                static_cast<float>( KeyHeld( input, sys::sys_key_t::A ) );
            cameraInput.up = static_cast<float>( KeyHeld( input, sys::sys_key_t::SPACE ) ) -
                static_cast<float>( KeyHeld( input, sys::sys_key_t::C ) );
            cameraInput.lookDeltaX = input.lookDeltaX;
            cameraInput.lookDeltaY = input.lookDeltaY;
            cameraInput.fast = KeyHeld( input, sys::sys_key_t::LEFT_SHIFT ) ||
                KeyHeld( input, sys::sys_key_t::RIGHT_SHIFT );
            if ( !camera::CubeCamera_Advance( &flyCamera, cameraInput, delta ) ) {
                std::fprintf( stderr, "Could not advance the sample fly camera.\n" );
                return false;
            }
        }
        const math::vec3_t cameraPosition = interactive
            ? flyCamera.position : CUBE_CAMERA_POSITION;
        if ( !( interactive
                ? camera::CubeCamera_TryView( flyCamera, &view )
                : math::Mat4_TryLookAtRH( CUBE_CAMERA_POSITION, math::CY_VEC3_ZERO,
                    math::CY_VEC3_UP, 0.0001f, &view ) ) ||
             !math::Mat4_TryPerspectiveRH( math::Angle_FromDegrees( 45.0f ),
                 static_cast<float>( extent.width ) / static_cast<float>( extent.height ),
                 0.1f, 100.0f, math::clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
                 &projection ) ) {
            std::fprintf( stderr, "Could not construct cube camera matrices.\n" );
            return false;
        }
        math::frustum_t frustum{};
        if ( !math::Frustum_TryFromViewProjection(
                 math::Mat4_Multiply( projection, view ),
                 math::clip_depth_range_t::NEGATIVE_ONE_TO_ONE,
                 0.000001f, &frustum ) ) {
            std::fprintf( stderr, "Could not construct cube view frustum.\n" );
            return false;
        }

        const math::quat_t rotation = math::Quat_Multiply( yaw, tilt );
        for ( common::u32 i = 0u; i < CUBE_INSTANCE_COUNT; ++i ) {
            world::world_object_t object{};
            object.localToWorld = math::Affine3_FromTRS(
                CUBE_INSTANCES[i].position, rotation, math::Vec3_Splat( 1.0f ) );
            object.worldBounds = math::Aabb_TransformAffine(
                CUBE_LOCAL_BOUNDS, object.localToWorld );
            object.layerMask = 1u;
            object.meshSlot = MESH_SLOT_CUBE;
            object.materialSlot = i;
            if ( !CheckWorld( world::World_UpdateObject( state.pWorld,
                     state.objectIds[i], &object ), "Update cube world object" ) ) {
                return false;
            }
        }

        world::world_view_query_t query{};
        query.frustum = frustum;
        query.cameraPosition = cameraPosition;
        query.maxDistance = 100.0f;
        query.layerMask = 1u;
        query.frustumTolerance = 0.00001f;
        world::world_submission_token_t submissionToken{};
        if ( !CheckSubmission( world::WorldSubmission_Build(
                 state.pSubmission, state.pWorld, &query, &submissionToken ),
                 "Build cube view submission" ) ) return false;
        common::u32 visibleCount = 0u;
        if ( !CheckSubmission( world::WorldSubmission_Count(
                 state.pSubmission, submissionToken, &visibleCount ),
                 "Count visible cube objects" ) ) return false;
        if ( visibleCount > WORLD_OBJECT_COUNT ||
             !render::R_IsGraphicsPipelineValid( state.pipeline ) ||
             !render::R_IsGraphicsPipelineValid( state.transparentPipeline ) ||
             !render::R_IsGraphicsPipelineValid( state.shadowPipeline ) ||
             !render::R_IsGraphicsPipelineValid( state.toneMapPipeline ) ||
             !render::R_IsBufferValid( state.viewConstants ) ||
             !render::R_IsBufferValid( state.lightConstants ) ||
             !render::R_IsRenderTargetValid( state.shadowTarget ) ||
             !render::R_IsRenderTargetValid( state.sceneTarget ) ||
             !render::R_IsTextureValid( state.sceneColor ) ||
             !render::R_IsVertexInputValid( state.fullscreenMesh.vertexInput ) ||
             !render::R_IsTextureValid( state.shadowDepth ) ||
             !render::R_IsSamplerValid( state.shadowSampler ) ) {
            std::fprintf( stderr, "Graybox view capacity or shared render resource is invalid.\n" );
            return false;
        }

        if ( renderedFrames != 0u ) {
            if ( !CheckRender( render::R_ResetUniformBatch( &state.objectConstants ),
                     "Reset object constants batch" ) ||
                 !CheckRender( render::R_ResetDrawQueue( state.pDrawQueue ),
                     "Reset draw queue" ) ||
                 !CheckRender( render::R_ResetDrawQueue( state.pTransparentQueue ),
                     "Reset translucent draw queue" ) ||
                 !CheckRender( render::R_ResetDrawQueue( state.pShadowQueue ),
                     "Reset shadow draw queue" ) ) return false;
        }
        const cube_view_constants_t viewConstants{
            view, projection,
            { 0.30f, 0.30f, 0.30f, 0.0f },
            { 0.40f, -0.70f, 1.00f, 0.0f },
            // Bright directional values deliberately exceed linear 1.0 on
            // illuminated faces, making the HDR intermediate meaningful.
            { 1.75f, 1.75f, 1.75f, 0.0f },
            lightViewProjection
        };
        const render::render_buffer_data_t viewData{ &viewConstants,
            sizeof( viewConstants ) };
        if ( !CheckRender( render::R_UpdateBuffer( state.viewConstants, 0u,
                viewData ), "Upload view constants" ) ) return false;

        render::render_uniform_record_t mainRecords[WORLD_OBJECT_COUNT]{};
        render::render_uniform_record_t shadowRecords[WORLD_OBJECT_COUNT]{};
        render::render_draw_indexed_desc_t mainDraws[WORLD_OBJECT_COUNT]{};
        render::render_draw_indexed_desc_t shadowDrawsByObject[WORLD_OBJECT_COUNT]{};
        float cameraSpaceZ[WORLD_OBJECT_COUNT]{};
        bool transparentByObject[WORLD_OBJECT_COUNT]{};
        // Query every live object, including the opaque cube behind the camera.
        // The camera submission below still selects scene-pass draws; only
        // opaque geometry is staged for the separate light pass.
        for ( common::u32 i = 0u; i < WORLD_OBJECT_COUNT; ++i ) {
            world::world_object_t object{};
            if ( !CheckWorld( world::World_GetObject( state.pWorld,
                     state.objectIds[i], &object ),
                     "Read shadow-casting world object" ) ) return false;
            if ( object.meshSlot >= MESH_SLOT_COUNT ||
                 object.materialSlot >= MATERIAL_COUNT ) {
                std::fprintf( stderr, "Graybox world returned an unresolved asset slot.\n" );
                return false;
            }
            const bool transparent = MATERIALS[object.materialSlot].tint[3] < 1.0f;
            transparentByObject[i] = transparent;
            cameraSpaceZ[i] = math::Mat4_TransformPointAffine(
                view, math::Aabb_Center( object.worldBounds ) ).z;
            mainDraws[i].pipeline = transparent
                ? state.transparentPipeline : state.pipeline;
            mainDraws[i].uniformBuffers[0] = state.viewConstants;
            const auto textureSlot = MATERIALS[object.materialSlot].textureSlot;
            if ( textureSlot >= TEXTURE_SLOT_COUNT ||
                 !render::R_IsTextureValid( state.textures[textureSlot] ) ) {
                std::fprintf( stderr, "Graybox material has an invalid texture slot.\n" );
                return false;
            }
            mainDraws[i].sampledTextures[0] = state.textures[textureSlot];
            mainDraws[i].sampledTextures[1] = state.shadowDepth;
            mainDraws[i].samplers[1] = state.shadowSampler;
            if ( !CheckRender( render::R_ApplyStaticMeshBundleToDraw(
                     state.meshes[object.meshSlot], &mainDraws[i] ),
                     "Resolve main-pass mesh" ) ) return false;
            if ( !transparent ) {
                shadowDrawsByObject[i].pipeline = state.shadowPipeline;
                shadowDrawsByObject[i].uniformBuffers[0] = state.lightConstants;
                if ( !CheckRender( render::R_ApplyStaticMeshBundleToDraw(
                         state.meshes[object.meshSlot], &shadowDrawsByObject[i] ),
                         "Resolve shadow-pass mesh" ) ) return false;
            }

            cube_object_constants_t constants{};
            constants.model = math::Affine3_ToMat4( object.localToWorld );
            std::memcpy( constants.tint, MATERIALS[object.materialSlot].tint,
                sizeof( constants.tint ) );
            if ( !transparent ) {
                const shadow_object_constants_t shadowConstants{ constants.model };
                if ( !CheckRender( render::R_AppendUniformRecord( &state.objectConstants,
                         &shadowConstants, sizeof( shadowConstants ), &shadowRecords[i] ),
                         "Stage shadow object constants" ) ) return false;
            }
            if ( !CheckRender( render::R_AppendUniformRecord( &state.objectConstants,
                     &constants, sizeof( constants ), &mainRecords[i] ),
                     "Stage main object constants" ) ) return false;
        }
        if ( !CheckRender( render::R_CommitUniformBatch( &state.objectConstants ),
                 "Upload graybox object constants" ) ) return false;
        for ( common::u32 i = 0u; i < WORLD_OBJECT_COUNT; ++i ) {
            if ( !CheckRender( render::R_ApplyUniformRecordToDraw(
                     state.objectConstants, mainRecords[i], 1u, &mainDraws[i] ),
                     "Resolve main object constants" ) ) return false;
            if ( transparentByObject[i] ) continue;
            if ( !CheckRender( render::R_ApplyUniformRecordToDraw(
                     state.objectConstants, shadowRecords[i], 1u,
                     &shadowDrawsByObject[i] ),
                     "Resolve shadow object constants" ) ||
                 !CheckRender( render::R_AppendDrawQueue( state.pShadowQueue,
                     shadowDrawsByObject[i] ), "Queue shadow caster" ) ) return false;
        }
        common::u32 transparentIndices[PANEL_COUNT]{};
        common::u32 transparentCount = 0u;
        for ( common::u32 i = 0u; i < visibleCount; ++i ) {
            world::world_object_snapshot_t visible{};
            if ( !CheckSubmission( world::WorldSubmission_Get(
                     state.pSubmission, submissionToken, i, &visible ),
                     "Read visible cube object" ) ) return false;
            common::u32 objectIndex = WORLD_OBJECT_COUNT;
            for ( common::u32 j = 0u; j < WORLD_OBJECT_COUNT; ++j ) {
                const world::world_object_id_t &candidate = state.objectIds[j];
                if ( visible.id.slot.value == candidate.slot.value &&
                     visible.id.worldSerial == candidate.worldSerial &&
                     visible.id.incarnation == candidate.incarnation ) {
                    objectIndex = j;
                    break;
                }
            }
            if ( objectIndex == WORLD_OBJECT_COUNT ) {
                std::fprintf( stderr, "Visible World object has no staged draw.\n" );
                return false;
            }
            if ( transparentByObject[objectIndex] ) {
                if ( transparentCount == PANEL_COUNT ) {
                    std::fprintf( stderr, "Translucent view capacity exceeded.\n" );
                    return false;
                }
                // RH camera space looks along -Z. More negative centers draw
                // first; the slot index breaks equal-depth ties predictably.
                common::u32 at = transparentCount;
                while ( at > 0u &&
                    ( cameraSpaceZ[objectIndex] < cameraSpaceZ[transparentIndices[at - 1u]] ||
                      ( cameraSpaceZ[objectIndex] == cameraSpaceZ[transparentIndices[at - 1u]] &&
                        objectIndex < transparentIndices[at - 1u] ) ) ) {
                    transparentIndices[at] = transparentIndices[at - 1u];
                    --at;
                }
                transparentIndices[at] = objectIndex;
                ++transparentCount;
                continue;
            }
            if ( !CheckRender( render::R_AppendDrawQueue(
                     state.pDrawQueue, mainDraws[objectIndex] ),
                     "Queue graybox draw" ) ) return false;
        }
        for ( common::u32 i = 0u; i < transparentCount; ++i ) {
            if ( !CheckRender( render::R_AppendDrawQueue( state.pTransparentQueue,
                     mainDraws[transparentIndices[i]] ),
                     "Queue sorted translucent panel" ) ) return false;
        }
        world::WorldSubmission_End( state.pSubmission );

        render::render_frame_info_t frame{};
        frame.frameIndex = renderedFrames;
        frame.deltaSeconds = delta;
        frame.drawableExtent = extent;
        frame.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
        frame.clearColor = { 0.035f, 0.045f, 0.065f, 1.0f };
        if ( !CheckRender( render::R_BeginFrame( frame ), "Begin frame" ) ) return false;
        render::render_target_pass_desc_t shadowPass{};
        shadowPass.target = state.shadowTarget;
        shadowPass.clearFlags = render::R_CLEAR_DEPTH;
        bool passOK = CheckRender( render::R_BeginTargetPass( shadowPass ),
            "Begin shadow depth pass" );
        common::u32 completedShadowDraws = 0u;
        if ( passOK ) {
            const bool shadowsOK = CheckRender( render::R_FlushDrawQueue(
                state.pShadowQueue, &completedShadowDraws ),
                "Draw shadow casters" );
            shadowDraws += completedShadowDraws;
            const bool endPassOK = CheckRender( render::R_EndTargetPass(),
                "End shadow depth pass" );
            passOK = shadowsOK && endPassOK;
        }
        common::u32 completedDraws = 0u;
        if ( passOK ) {
            render::render_target_pass_desc_t scenePass{};
            scenePass.target = state.sceneTarget;
            scenePass.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
            scenePass.clearColor = frame.clearColor;
            passOK = CheckRender( render::R_BeginTargetPass( scenePass ),
                "Begin HDR scene pass" );
            if ( passOK ) {
                const bool opaqueOK = CheckRender( render::R_FlushDrawQueue(
                    state.pDrawQueue, &completedDraws ),
                    "Draw opaque graybox objects into HDR scene" );
                visibleDraws += completedDraws;
                common::u32 completedTransparentDraws = 0u;
                const bool transparentOK = opaqueOK && CheckRender(
                    render::R_FlushDrawQueue( state.pTransparentQueue,
                        &completedTransparentDraws ),
                    "Blend sorted translucent panels into HDR scene" );
                transparentDraws += completedTransparentDraws;
                visibleDraws += completedTransparentDraws;
                const bool endPassOK = CheckRender( render::R_EndTargetPass(),
                    "End HDR scene pass" );
                passOK = opaqueOK && transparentOK && endPassOK;
            }
        }
        if ( passOK ) {
            render::render_draw_indexed_desc_t toneMapDraw{};
            toneMapDraw.pipeline = state.toneMapPipeline;
            toneMapDraw.sampledTextures[0] = state.sceneColor;
            passOK = CheckRender( render::R_ApplyStaticMeshBundleToDraw(
                state.fullscreenMesh, &toneMapDraw ),
                "Resolve fullscreen triangle" ) &&
                CheckRender( render::R_DrawIndexed( toneMapDraw ),
                "Tone-map HDR scene to presentation" );
            if ( passOK ) ++toneMapDraws;
        }
        const bool endOK = CheckRender( render::R_EndFrame(), "End frame" );
        if ( !passOK || !endOK ) return false;
        ++renderedFrames;
        if ( options.frameLimit != 0u && renderedFrames >= options.frameLimit ) break;
    }
    std::printf( "Rendered %u frames, %llu visible-object draws (%llu translucent), %llu shadow-caster draws, and %llu tone-map draws.\n",
        renderedFrames, static_cast<unsigned long long>( visibleDraws ),
        static_cast<unsigned long long>( transparentDraws ),
        static_cast<unsigned long long>( shadowDraws ),
        static_cast<unsigned long long>( toneMapDraws ) );
    return options.frameLimit == 0u || renderedFrames == options.frameLimit;
}

bool Shutdown( cube_state_t &state ) noexcept
{
    bool success = true;
    if ( state.window.valid ) {
        success = SetCameraCapture( state, false ) && success;
    }
    render::R_ShutdownDrawQueue( &state.pShadowQueue );
    render::R_ShutdownDrawQueue( &state.pTransparentQueue );
    render::R_ShutdownDrawQueue( &state.pDrawQueue );
    world::WorldSubmission_Shutdown( &state.pSubmission );
    world::World_Shutdown( &state.pWorld );
    if ( render::R_IsInitialized() ) {
        if ( render::R_IsFrameActive() ) {
            success = CheckRender( render::R_EndFrame(), "Finish active frame" ) && success;
        }
        success = CheckRender( render::R_WaitIdle(), "Wait for renderer" ) && success;
        // Release references before owners. Shutdown also catches any object
        // left live after an earlier failure, while the context still exists.
        if ( state.pipeline.value != 0u ) {
            success = CheckRender( render::R_DestroyGraphicsPipeline( state.pipeline ),
                "Destroy pipeline" ) && success;
        }
        if ( state.transparentPipeline.value != 0u ) {
            success = CheckRender( render::R_DestroyGraphicsPipeline(
                state.transparentPipeline ), "Destroy translucent pipeline" ) && success;
        }
        if ( state.shadowPipeline.value != 0u ) {
            success = CheckRender( render::R_DestroyGraphicsPipeline( state.shadowPipeline ),
                "Destroy shadow pipeline" ) && success;
        }
        if ( state.toneMapPipeline.value != 0u ) {
            success = CheckRender( render::R_DestroyGraphicsPipeline( state.toneMapPipeline ),
                "Destroy tone-map pipeline" ) && success;
        }
        if ( state.sceneTarget.value != 0u ) {
            success = CheckRender( render::R_DestroyRenderTarget( state.sceneTarget ),
                "Destroy HDR scene target" ) && success;
        }
        if ( state.shadowTarget.value != 0u ) {
            success = CheckRender( render::R_DestroyRenderTarget( state.shadowTarget ),
                "Destroy shadow target" ) && success;
        }
        if ( state.shadowSampler.value != 0u ) {
            success = CheckRender( render::R_DestroySampler( state.shadowSampler ),
                "Destroy shadow sampler" ) && success;
        }
        if ( state.shadowDepth.value != 0u ) {
            success = CheckRender( render::R_DestroyTexture( state.shadowDepth ),
                "Destroy shadow depth texture" ) && success;
        }
        if ( state.sceneColor.value != 0u ) {
            success = CheckRender( render::R_DestroyTexture( state.sceneColor ),
                "Destroy HDR scene color" ) && success;
        }
        for ( common::u32 i = TEXTURE_SLOT_COUNT; i > 0u; --i ) {
            auto &texture = state.textures[i - 1u];
            if ( texture.value != 0u ) {
                success = CheckRender( render::R_DestroyTexture( texture ),
                    "Destroy graybox texture" ) && success;
            }
        }
        success = CheckRender( render::R_DestroyUniformBatch( &state.objectConstants ),
            "Destroy object constants batch" ) && success;
        if ( state.viewConstants.value != 0u ) {
            success = CheckRender( render::R_DestroyBuffer( state.viewConstants ),
                "Destroy view constants" ) && success;
        }
        if ( state.lightConstants.value != 0u ) {
            success = CheckRender( render::R_DestroyBuffer( state.lightConstants ),
                "Destroy light constants" ) && success;
        }
        success = CheckRender( render::R_DestroyStaticMeshBundle(
            &state.meshes[MESH_SLOT_FLOOR] ), "Destroy floor mesh" ) && success;
        success = CheckRender( render::R_DestroyStaticMeshBundle(
            &state.meshes[MESH_SLOT_CUBE] ), "Destroy cube mesh" ) && success;
        success = CheckRender( render::R_DestroyStaticMeshBundle(
            &state.fullscreenMesh ), "Destroy fullscreen triangle" ) && success;
        if ( state.shader.value != 0u ) {
            success = CheckRender( render::R_DestroyShader( state.shader ), "Destroy shader" ) && success;
        }
        if ( state.shadowShader.value != 0u ) {
            success = CheckRender( render::R_DestroyShader( state.shadowShader ),
                "Destroy shadow shader" ) && success;
        }
        if ( state.toneMapShader.value != 0u ) {
            success = CheckRender( render::R_DestroyShader( state.toneMapShader ),
                "Destroy tone-map shader" ) && success;
        }
        success = CheckRender( render::R_Shutdown(), "Shut down renderer" ) && success;
    }
    if ( state.window.valid ) {
        success = CheckSystem( sys::Sys_DestroyWindow( state.window ), "Destroy window" ) && success;
    }
    if ( sys::Sys_IsInitialized() ) {
        success = CheckSystem( sys::Sys_Shutdown(), "Shut down System" ) && success;
    }
    return success;
}

bool Run( int argc, char **argv, const cube_options_t &options, cube_state_t &state )
{
    const sys::init_info_t init{ argc, argv, "CypherRenderCube", "CypherEngine" };
    if ( !CheckSystem( sys::Sys_Init( init ), "Initialize System" ) ) return false;
    render::render_config_t config = render::R_DefaultConfig();
    config.backend = render::render_backend_t::OPENGL;
    config.presentMode = options.hidden ? render::render_present_mode_t::IMMEDIATE
                                       : render::render_present_mode_t::FIFO;
    config.sRGBFramebuffer = false;
    config.requireAcceleration = false;
    config.requiredCapabilities |= render::R_CAPABILITY_FLOAT_RENDER_TARGETS;
    config.optionalCapabilities = render::R_CAPABILITY_NONE;
    sys::window_desc_t window{};
    window.title = "Cypher Renderer | Graybox World View | Hold RMB to fly | Esc to exit";
    window.width = 1100u;
    window.height = 760u;
    if ( options.hidden ) window.flags |= sys::SYS_WINDOW_HIDDEN;
    if ( !CheckRender( render::R_ConfigureWindow( config, window ), "Configure window" ) ||
         !CheckSystem( sys::Sys_CreateWindow( window, state.window ), "Create window" ) ||
         !CheckRender( render::R_Init( state.window, config ), "Initialize renderer" ) ||
         !CreateScene( state, options ) ||
         !RecreateSceneTarget( state, render::R_GetInfo()->drawableExtent ) ||
         !CreateWorld( state ) ) return false;
    std::printf( "Cooked shader: %s\nCooked shadow shader: %s\nCooked tone-map shader: %s\nCooked floor texture: %s\nCube and floor meshes, %u world objects; fixed shadow depth, opaque then sorted translucent HDR scene, and tone-map presentation passes per frame.\n",
        options.shaderPath, options.shadowShaderPath, options.toneMapShaderPath,
        options.texturePath,
        WORLD_OBJECT_COUNT );
    return RunFrames( state, options );
}

} // namespace

int main( int argc, char **argv )
{
    cube_options_t options{};
    if ( !ParseOptions( argc, argv, options ) ) return 2;
    if ( options.help ) {
        std::puts( "Usage: cypher_render_cube [--shader PATH] [--shadow-shader PATH] [--tonemap-shader PATH] [--texture PATH] [--frames N] [--hidden]\n"
                   "Hold right mouse to fly: mouse look, WASD move, Space/C up/down, Shift faster.\n"
                   "Release right mouse to free the cursor. Escape or close exits.\n"
                   "Resize changes the projection and recreates the HDR scene target.\n"
                   "--shader reads an existing .cyshader_c; cooking is an offline step.\n"
                   "--shadow-shader reads an existing .cyshader_c depth shader.\n"
                   "--tonemap-shader reads an existing .cyshader_c presentation shader.\n"
                   "--texture reads an existing .cytex_c floor texture.\n"
                   "--frames keeps the fixed camera and deterministic animation, then exits.\n"
                   "--hidden defaults to 120 frames when --frames is omitted." );
        return 0;
    }
    cube_state_t state{};
    bool success = false;
    try {
        success = Run( argc, argv, options, state );
    } catch ( const std::exception &error ) {
        std::fprintf( stderr, "Cube demo: %s\n", error.what() );
    }
    const bool shutdownOK = Shutdown( state );
    return success && shutdownOK ? 0 : 1;
}
