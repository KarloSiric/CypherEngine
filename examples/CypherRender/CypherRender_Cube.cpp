//////////////////////////////////////////////////////////////////////////
//
//  CypherEngine Source Code
//  Copyright (c) 2026 Karlo Siric. All rights reserved.
//
//  File: examples/CypherRender/CypherRender_Cube.cpp
//  Purpose: Demonstrates a visible-world-object public-renderer drawing path.
//  Details: Loads a cooked shader through the loose-directory VFS, shares one
//           indexed mesh, queries a small in-memory world, and uploads aligned
//           per-view and per-object constants for visible objects each frame.
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
#include "CypherRender/CypherRender_Draw.h"
#include "CypherRender/CypherRender_Pipeline.h"
#include "CypherRender/CypherRender_Public.h"
#include "CypherRender/CypherRender_Shader.h"
#include "CypherRender/Runtime/CypherRender_MeshBridge.h"
#include "CypherRender/Runtime/CypherRender_DrawQueue.h"
#include "CypherRender/Runtime/CypherRender_UniformBatch.h"
#include "CypherSystem/Platform/CypherSystem_Public.h"
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

namespace common = ::cypher::common;
namespace math = ::cypher::math;
namespace render = ::cypher::engine::render;
namespace sys = ::cypher::engine::sys;
namespace world = ::cypher::engine::world;

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

// mat4_t is column-major. The two structures match separate GLSL std140 blocks.
struct alignas( 16 ) cube_view_constants_t {
    math::mat4_t view;
    math::mat4_t projection;
};
static_assert( sizeof( cube_view_constants_t ) == 128u );
static_assert( offsetof( cube_view_constants_t, projection ) == 64u );

struct alignas( 16 ) cube_object_constants_t {
    math::mat4_t model;
    alignas( 16 ) float tint[4];
};
static_assert( sizeof( cube_object_constants_t ) == 80u );
static_assert( offsetof( cube_object_constants_t, tint ) == 64u );

struct cube_options_t {
    const char *shaderPath{ CYPHER_RENDER_CUBE_SHADER_PATH };
    common::u32 frameLimit{ 0u }; // Zero is an interactive run.
    bool hidden{ false };
    bool help{ false };
};

struct cube_instance_t {
    math::vec3_t position;
    float tint[4];
};

constexpr cube_instance_t CUBE_INSTANCES[] = {
    { { -2.4f, -1.6f, 0.0f }, { 0.75f, 0.28f, 0.18f, 1.0f } },
    { { 0.0f, 0.0f, 0.0f }, { 0.10f, 0.55f, 0.76f, 1.0f } },
    { { 2.4f, 1.6f, 0.0f }, { 0.36f, 0.72f, 0.32f, 1.0f } },
    // Deliberately behind the camera. The World query rejects this object.
    { { 12.0f, -18.0f, 9.6f }, { 0.72f, 0.28f, 0.68f, 1.0f } }
};
constexpr common::u32 CUBE_INSTANCE_COUNT =
    static_cast<common::u32>( sizeof( CUBE_INSTANCES ) / sizeof( CUBE_INSTANCES[0] ) );

struct cube_state_t {
    sys::window_t window{};
    render::render_shader_handle_t shader{};
    render::render_pipeline_handle_t pipeline{};
    render::render_static_mesh_bundle_t mesh{};
    render::render_buffer_handle_t viewConstants{};
    render::render_uniform_batch_t objectConstants{};
    render::render_draw_queue_t *pDrawQueue{ nullptr };
    world::world_t *pWorld{ nullptr };
    world::world_submission_t *pSubmission{ nullptr };
    world::world_object_id_t objectIds[CUBE_INSTANCE_COUNT]{};
};

constexpr math::vec3_t CUBE_CAMERA_POSITION{ 4.0f, -6.0f, 3.2f };
constexpr math::aabb_t CUBE_LOCAL_BOUNDS{
    { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f } };

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

bool ParseOptions( int argc, char **argv, cube_options_t &options ) noexcept
{
    for ( int i = 1; i < argc; ++i ) {
        if ( std::strcmp( argv[i], "--hidden" ) == 0 ) options.hidden = true;
        else if ( std::strcmp( argv[i], "--help" ) == 0 ) options.help = true;
        else if ( std::strcmp( argv[i], "--shader" ) == 0 && i + 1 < argc ) {
            options.shaderPath = argv[++i];
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

bool LoadShader( const char *nativePath, render::render_shader_handle_t &shader )
{
    const std::filesystem::path path = std::filesystem::absolute( nativePath );
    const std::string root = path.parent_path().string();
    const std::string filename = path.filename().string();
    common::vfs_directory_t directory{};
    const common::vfs_status_t mountResult = common::VfsDirectory_Init(
        &directory, { root.data(), root.size() } );
    if ( mountResult != common::vfs_status_t::OK ) {
        std::fprintf( stderr, "Shader directory: %s (%s)\n", root.c_str(),
            common::Vfs_StatusName( mountResult ) );
        return false;
    }

    common::blob_t bytes{};
    if ( !common::Blob_Init( &bytes, common::Allocator_GetSystem() ) ) {
        common::VfsDirectory_Shutdown( &directory );
        std::fprintf( stderr, "Could not initialize cooked shader storage.\n" );
        return false;
    }
    const common::vfs_t vfs = common::VfsDirectory_Make( &directory );
    constexpr common::usize MAX_SHADER_FILE_BYTES = 40u * 1024u * 1024u;
    const common::vfs_status_t readResult = common::Vfs_ReadAll(
        &vfs, { filename.data(), filename.size() }, MAX_SHADER_FILE_BYTES, &bytes );
    common::VfsDirectory_Shutdown( &directory );
    if ( readResult != common::vfs_status_t::OK ) {
        std::fprintf( stderr, "Read cooked shader %s: %s\n", nativePath,
            common::Vfs_StatusName( readResult ) );
        common::Blob_Shutdown( &bytes );
        return false;
    }

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

bool CreateCube( cube_state_t &state, const char *shaderPath )
{
    if ( !LoadShader( shaderPath, state.shader ) ) return false;
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
    const auto meshResult = render::R_CreateStaticMeshBundle( source, &state.mesh );
    if ( meshResult.status != render::render_static_mesh_bridge_status_t::OK ) {
        std::fprintf( stderr, "Create cube mesh: %s (%u)\n",
            render::R_ErrorName( meshResult.renderError ),
            static_cast<unsigned>( meshResult.status ) );
        return false;
    }

    render::render_pipeline_desc_t pipeline{};
    pipeline.shader = state.shader;
    pipeline.vertexLayout = state.mesh.vertexLayout;
    pipeline.depthTest = true;
    pipeline.depthWrite = true;
    pipeline.cullBackFaces = true;
    pipeline.frontCounterClockwise = true;
    pipeline.alphaBlend = false;
    pipeline.uniformBlockCount = 2u;
    pipeline.uniformBlocks[0] = { "ViewConstants", sizeof( cube_view_constants_t ) };
    pipeline.uniformBlocks[1] = { "ObjectConstants", sizeof( cube_object_constants_t ) };
    pipeline.debugName = "Depth-tested cube pipeline";
    if ( !CheckRender( render::R_CreateGraphicsPipeline( pipeline, &state.pipeline ),
            "Create cube pipeline" ) ) return false;

    render::render_buffer_desc_t viewBuffer{};
    viewBuffer.byteSize = sizeof( cube_view_constants_t );
    viewBuffer.usage = render::R_BUFFER_USAGE_UNIFORM;
    viewBuffer.updatePolicy = render::render_buffer_update_t::STREAM;
    viewBuffer.memory = render::render_buffer_memory_t::UPLOAD;
    viewBuffer.debugName = "Cube view constants";
    if ( !CheckRender( render::R_CreateBuffer( viewBuffer, nullptr,
            &state.viewConstants ), "Create view constants" ) ) return false;

    const render::render_info_t *info = render::R_GetInfo();
    if ( info == nullptr || info->limits.uniformBufferOffsetAlignment == 0u ) return false;
    const common::u64 alignment = info->limits.uniformBufferOffsetAlignment;
    const common::u64 recordStride =
        ( sizeof( cube_object_constants_t ) + alignment - 1u ) / alignment * alignment;
    const common::u64 capacity =
        ( CUBE_INSTANCE_COUNT - 1u ) * recordStride + sizeof( cube_object_constants_t );
    if ( !CheckRender( render::R_CreateUniformBatch( capacity, nullptr,
            &state.objectConstants ), "Create object constants batch" ) ) return false;
    return CheckRender( render::R_InitDrawQueue( &state.pDrawQueue,
        common::Allocator_GetSystem(), CUBE_INSTANCE_COUNT ),
        "Create cube draw queue" );
}

bool CreateWorld( cube_state_t &state ) noexcept
{
    if ( !CheckWorld( world::World_Init( &state.pWorld,
             common::Allocator_GetSystem(), CUBE_INSTANCE_COUNT ),
             "Create cube world" ) ) return false;
    if ( !CheckSubmission( world::WorldSubmission_Init( &state.pSubmission,
             common::Allocator_GetSystem(), CUBE_INSTANCE_COUNT ),
             "Create cube view storage" ) ) return false;

    for ( common::u32 i = 0u; i < CUBE_INSTANCE_COUNT; ++i ) {
        world::world_object_t object{};
        object.localToWorld = math::Affine3_FromTranslation(
            CUBE_INSTANCES[i].position );
        object.worldBounds = math::Aabb_TransformAffine(
            CUBE_LOCAL_BOUNDS, object.localToWorld );
        object.layerMask = 1u;
        object.meshSlot = 0u; // The host resolves this logical slot to state.mesh.
        object.materialSlot = i; // The host resolves this slot to a sample tint.
        if ( !CheckWorld( world::World_AddObject( state.pWorld,
                 &object, &state.objectIds[i] ), "Add cube world object" ) ) {
            return false;
        }
    }
    return true;
}

bool RunFrames( cube_state_t &state, const cube_options_t &options ) noexcept
{
    const double started = sys::Sys_TimeNowSeconds();
    double previous = started;
    const double timeout = std::max( 30.0, 10.0 + options.frameLimit / 15.0 );
    float rotationTime = 0.0f;
    common::u32 renderedFrames = 0u;
    common::u64 visibleDraws = 0u;
    render::render_extent_t extent = render::R_GetInfo()->drawableExtent;

    while ( !sys::Sys_IsQuitRequested() && !sys::Sys_WindowShouldClose( state.window ) ) {
        sys::Sys_PollWindowEvents( state.window );
        sys::sys_event_t event{};
        while ( sys::Sys_PollEvent( event ) ) {
            if ( event.type == sys::sys_event_type_t::QUIT_REQUESTED ||
                 ( event.type == sys::sys_event_type_t::KEY &&
                   event.payload.key.key == sys::sys_key_t::ESCAPE &&
                   event.payload.key.action == sys::sys_input_action_t::PRESSED ) ) {
                sys::Sys_RequestQuit();
            }
        }
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
            if ( !CheckRender( render::R_Resize( extent ), "Resize drawable" ) ) return false;
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
        if ( !math::Mat4_TryLookAtRH( CUBE_CAMERA_POSITION, math::CY_VEC3_ZERO,
                 math::CY_VEC3_UP, 0.0001f, &view ) ||
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
            object.meshSlot = 0u;
            object.materialSlot = i;
            if ( !CheckWorld( world::World_UpdateObject( state.pWorld,
                     state.objectIds[i], &object ), "Update cube world object" ) ) {
                return false;
            }
        }

        world::world_view_query_t query{};
        query.frustum = frustum;
        query.cameraPosition = CUBE_CAMERA_POSITION;
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

        if ( renderedFrames != 0u ) {
            if ( !CheckRender( render::R_ResetUniformBatch( &state.objectConstants ),
                     "Reset object constants batch" ) ||
                 !CheckRender( render::R_ResetDrawQueue( state.pDrawQueue ),
                     "Reset draw queue" ) ) return false;
        }
        const cube_view_constants_t viewConstants{ view, projection };
        const render::render_buffer_data_t viewData{ &viewConstants,
            sizeof( viewConstants ) };
        if ( !CheckRender( render::R_UpdateBuffer( state.viewConstants, 0u,
                viewData ), "Upload view constants" ) ) return false;

        render::render_uniform_record_t records[CUBE_INSTANCE_COUNT]{};
        for ( common::u32 i = 0u; i < visibleCount; ++i ) {
            world::world_object_snapshot_t visible{};
            if ( !CheckSubmission( world::WorldSubmission_Get(
                     state.pSubmission, submissionToken, i, &visible ),
                     "Read visible cube object" ) ) return false;
            const world::world_object_t &object = visible.object;
            if ( object.meshSlot != 0u || object.materialSlot >= CUBE_INSTANCE_COUNT ) {
                std::fprintf( stderr, "Cube world returned an unresolved asset slot.\n" );
                return false;
            }
            cube_object_constants_t constants{};
            constants.model = math::Affine3_ToMat4( object.localToWorld );
            std::memcpy( constants.tint, CUBE_INSTANCES[object.materialSlot].tint,
                sizeof( constants.tint ) );
            if ( !CheckRender( render::R_AppendUniformRecord( &state.objectConstants,
                     &constants, sizeof( constants ), &records[i] ),
                     "Stage object constants" ) ) return false;
        }
        if ( visibleCount != 0u && !CheckRender(
                 render::R_CommitUniformBatch( &state.objectConstants ),
                 "Upload visible object constants" ) ) return false;

        render::render_draw_indexed_desc_t draw{};
        draw.pipeline = state.pipeline;
        draw.uniformBuffers[0] = state.viewConstants;
        if ( visibleCount != 0u && !CheckRender(
            render::R_ApplyStaticMeshBundleToDraw( state.mesh, &draw ),
            "Bind cube mesh" ) ) return false;
        for ( common::u32 i = 0u; i < visibleCount; ++i ) {
            if ( !CheckRender( render::R_ApplyUniformRecordToDraw(
                     state.objectConstants, records[i], 1u, &draw ),
                     "Bind object constants" ) ||
                 !CheckRender( render::R_AppendDrawQueue( state.pDrawQueue, draw ),
                     "Queue cube draw" ) ) return false;
        }

        render::render_frame_info_t frame{};
        frame.frameIndex = renderedFrames;
        frame.deltaSeconds = delta;
        frame.drawableExtent = extent;
        frame.clearFlags = render::R_CLEAR_COLOR | render::R_CLEAR_DEPTH;
        frame.clearColor = { 0.035f, 0.045f, 0.065f, 1.0f };
        if ( !CheckRender( render::R_BeginFrame( frame ), "Begin frame" ) ) return false;
        common::u32 completedDraws = 0u;
        const bool drawOK = CheckRender( render::R_FlushDrawQueue(
            state.pDrawQueue, &completedDraws ), "Draw queued cubes" );
        visibleDraws += completedDraws;
        const bool endOK = CheckRender( render::R_EndFrame(), "End frame" );
        world::WorldSubmission_End( state.pSubmission );
        if ( !drawOK || !endOK ) return false;
        ++renderedFrames;
        if ( options.frameLimit != 0u && renderedFrames >= options.frameLimit ) break;
    }
    std::printf( "Rendered %u frames and %llu visible-object draws.\n",
        renderedFrames, static_cast<unsigned long long>( visibleDraws ) );
    return options.frameLimit == 0u || renderedFrames == options.frameLimit;
}

bool Shutdown( cube_state_t &state ) noexcept
{
    bool success = true;
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
        success = CheckRender( render::R_DestroyUniformBatch( &state.objectConstants ),
            "Destroy object constants batch" ) && success;
        if ( state.viewConstants.value != 0u ) {
            success = CheckRender( render::R_DestroyBuffer( state.viewConstants ),
                "Destroy view constants" ) && success;
        }
        success = CheckRender( render::R_DestroyStaticMeshBundle( &state.mesh ),
            "Destroy cube mesh" ) && success;
        if ( state.shader.value != 0u ) {
            success = CheckRender( render::R_DestroyShader( state.shader ), "Destroy shader" ) && success;
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
    config.optionalCapabilities = render::R_CAPABILITY_NONE;
    sys::window_desc_t window{};
    window.title = "Cypher Renderer | Visible World View | Esc to exit";
    window.width = 1100u;
    window.height = 760u;
    if ( options.hidden ) window.flags |= sys::SYS_WINDOW_HIDDEN;
    if ( !CheckRender( render::R_ConfigureWindow( config, window ), "Configure window" ) ||
         !CheckSystem( sys::Sys_CreateWindow( window, state.window ), "Create window" ) ||
         !CheckRender( render::R_Init( state.window, config ), "Initialize renderer" ) ||
         !CreateCube( state, options.shaderPath ) ||
         !CreateWorld( state ) ) return false;
    std::printf( "Cooked shader: %s\nOne 24-vertex mesh, %u world objects; one view upload and one visible-object batch per frame.\n",
        options.shaderPath, CUBE_INSTANCE_COUNT );
    return RunFrames( state, options );
}

} // namespace

int main( int argc, char **argv )
{
    cube_options_t options{};
    if ( !ParseOptions( argc, argv, options ) ) return 2;
    if ( options.help ) {
        std::puts( "Usage: cypher_render_cube [--shader PATH] [--frames N] [--hidden]\n"
                   "Escape or close the window to quit. Resize changes the projection.\n"
                   "--shader reads an existing .cyshader_c; cooking is an offline step.\n"
                   "--frames makes animation deterministic and exits after N frames.\n"
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
