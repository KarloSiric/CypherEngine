//////////////////////////////////////////////////////////////////////////
// CypherEngine Source Code — Copyright (c) 2026 Karlo Siric.
// Purpose: Runs the public World camera/shadow adapters in a small lit scene.
// No native graphics API, map parser, or gameplay system belongs in this host.
// This file is proprietary and confidential. See LICENSE for details.
//////////////////////////////////////////////////////////////////////////

#include "CypherRender_CubeCamera.h"
#include "CypherCommon/FileSystem/CypherCommon_VfsDirectory.h"
#include "CypherCommon/Mathlib/CypherMath_Quaternion.h"
#include "CypherRender/CypherRender_Public.h"
#include "CypherRender/CypherRender_Buffer.h"
#include "CypherRender/CypherRender_Pipeline.h"
#include "CypherRender/CypherRender_Sampler.h"
#include "CypherRender/CypherRender_Shader.h"
#include "CypherRender/CypherRender_Target.h"
#include "CypherRender/CypherRender_Texture.h"
#include "CypherRender/Runtime/CypherRender_WorldDraw.h"
#include "CypherRender/Runtime/CypherRender_SceneView.h"
#include "CypherSystem/Platform/CypherSystem_Public.h"
#include "CypherSystem/Platform/CypherSystem_Window.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

#ifndef CYPHER_RENDER_WORLD_SHADER_PATH
#define CYPHER_RENDER_WORLD_SHADER_PATH "cooked/render_smoke/world_lighting.cyshader_c"
#endif
#ifndef CYPHER_RENDER_WORLD_ADDITIVE_SHADER_PATH
#define CYPHER_RENDER_WORLD_ADDITIVE_SHADER_PATH "cooked/render_smoke/world_lighting_additive.cyshader_c"
#endif
#ifndef CYPHER_RENDER_WORLD_CUTOUT_SHADER_PATH
#define CYPHER_RENDER_WORLD_CUTOUT_SHADER_PATH "cooked/render_smoke/world_lighting_cutout.cyshader_c"
#endif
#ifndef CYPHER_RENDER_WORLD_CUTOUT_ADDITIVE_SHADER_PATH
#define CYPHER_RENDER_WORLD_CUTOUT_ADDITIVE_SHADER_PATH "cooked/render_smoke/world_lighting_cutout_additive.cyshader_c"
#endif
#ifndef CYPHER_RENDER_WORLD_SHADOW_SHADER_PATH
#define CYPHER_RENDER_WORLD_SHADOW_SHADER_PATH "cooked/render_smoke/world_shadow.cyshader_c"
#endif
#ifndef CYPHER_RENDER_WORLD_CUTOUT_SHADOW_SHADER_PATH
#define CYPHER_RENDER_WORLD_CUTOUT_SHADOW_SHADER_PATH "cooked/render_smoke/world_shadow_cutout.cyshader_c"
#endif
#ifndef CYPHER_RENDER_WORLD_TONEMAP_SHADER_PATH
#define CYPHER_RENDER_WORLD_TONEMAP_SHADER_PATH "cooked/render_smoke/tonemap.cyshader_c"
#endif

namespace common = cypher::common;
namespace math = cypher::math;
namespace render = cypher::engine::render;
namespace world = cypher::engine::world;
namespace sys = cypher::engine::sys;
namespace camera = cypher::samples::render;

namespace
{
constexpr common::u32 OPAQUE_MATERIAL_COUNT = 3u;
constexpr common::u32 MATERIAL_COUNT = OPAQUE_MATERIAL_COUNT + 2u; // Optional BLEND and ADDITIVE.
constexpr common::u32 MAX_OPAQUE_OBJECT_COUNT = 13u; // Floor plus six material faces per box.
constexpr common::u32 MAX_OBJECT_COUNT = MAX_OPAQUE_OBJECT_COUNT + 2u;
constexpr common::u32 MESH_COUNT = 7u; // Whole cube, then its six face ranges.
constexpr common::u32 SHADOW_SIZE = 1024u;
constexpr math::vec3_t LIGHT_POSITION{ 6.0f, -8.0f, 12.0f };
constexpr math::aabb_t LOCAL_BOUNDS{ { -1, -1, -1 }, { 1, 1, 1 } };
struct vertex_t { float position[3]; float normal[3]; float uv[2]; };
static_assert( sizeof( vertex_t ) == 32u );

struct options_t {
    common::u32 frames{ 0u };
    bool hidden{ false }, help{ false }, resizeTest{ false }, noFog{ false },
        materialRanges{ false }, transparency{ false }, cutout{ false };
};
struct scene_target_t {
    render::render_texture_handle_t color{};
    render::render_target_handle_t target{};
    render::render_extent_t extent{};
};
struct state_t {
    sys::window_t window{};
    bool cursorHidden{ false };
    // Default camera/shadow/presentation/additive, then opt-in cutout camera/additive/shadow.
    render::render_shader_handle_t shaders[7]{};
    render::render_pipeline_handle_t cameraPipelines[2]{}; // CCW, mirrored CW
    render::render_pipeline_handle_t blendPipelines[2]{}, additivePipelines[2]{};
    render::render_pipeline_handle_t shadowPipelines[2]{};
    render::render_pipeline_handle_t maskShadowPipelines[2]{};
    render::render_pipeline_handle_t presentationPipeline{};
    render::render_static_mesh_bundle_t cube{}, fullscreen{};
    render::render_buffer_handle_t view{}, light{}, tints[MATERIAL_COUNT]{};
    render::render_texture_handle_t white{}, checker{}, shadowDepth{};
    render::render_target_handle_t shadowTarget{};
    render::render_sampler_handle_t shadowSampler{};
    render::render_uniform_batch_t cameraModels{}, shadowModels{};
    // These are borrowed views of the shared resources above, not owners.
    render::render_material_bundle_t materials[MATERIAL_COUNT]{};
    render::render_static_mesh_bundle_t meshes[MESH_COUNT]{};
    render::render_world_mesh_range_t meshRanges[MESH_COUNT]{};
    scene_target_t scene{}, pending{};
    world::world_t *pWorld{ nullptr };
    world::world_submission_t *cameraSubmission{ nullptr }, *lightSubmission{ nullptr };
    world::world_object_id_t objects[MAX_OBJECT_COUNT]{};
    common::u32 objectCount{ 0u };
};

bool Check( render::render_error_t error, const char *operation ) noexcept
{
    if ( error == render::render_error_t::OK ) return true;
    std::fprintf( stderr, "%s: %s\n", operation, render::R_ErrorName( error ) );
    return false;
}
bool Check( sys::sys_error_t error, const char *operation ) noexcept
{
    if ( error == sys::sys_error_t::OK ) return true;
    std::fprintf( stderr, "%s: %s\n", operation, sys::Sys_ErrorName( error ) );
    return false;
}
bool Check( world::world_error_t error, const char *operation ) noexcept
{
    if ( error == world::world_error_t::OK ) return true;
    std::fprintf( stderr, "%s: World error %u\n", operation, static_cast<unsigned>( error ) );
    return false;
}
bool Check( world::world_submission_error_t error, const char *operation ) noexcept
{
    if ( error == world::world_submission_error_t::OK ) return true;
    std::fprintf( stderr, "%s: submission error %u\n", operation, static_cast<unsigned>( error ) );
    return false;
}

template<typename Handle, typename Destroy>
bool Release( Handle &handle, Destroy destroy, const char *operation ) noexcept
{
    if ( handle.value == 0u ) return true;
    if ( !Check( destroy( handle ), operation ) ) return false;
    handle = Handle{}; // Retain ownership if destruction failed.
    return true;
}

bool DestroySceneTarget( scene_target_t &scene ) noexcept
{
    if ( !Release( scene.target, render::R_DestroyRenderTarget, "Destroy HDR target" ) ) return false;
    return Release( scene.color, render::R_DestroyTexture, "Destroy HDR color" );
}

bool ResizeScene( state_t &state, render::render_extent_t extent ) noexcept
{
    if ( !DestroySceneTarget( state.pending ) ) return false;
    render::render_texture_desc_t color{};
    color.width = extent.width;
    color.height = extent.height;
    color.format = render::render_texture_format_t::RGBA16F;
    color.renderTarget = true;
    color.sRGB = false;
    color.repeat = false;
    color.generateMips = false;
    color.debugName = "World HDR scene";
    // Publish only after both allocations succeed. The old scene remains
    // owned on allocation failure; pending owns any incomplete replacement.
    if ( !Check( render::R_CreateTexture2D( color, {}, &state.pending.color ), "Create HDR color" ) ||
         !Check( render::R_CreateRenderTarget( { state.pending.color, {} },
             &state.pending.target ), "Create HDR target" ) ) return false;
    state.pending.extent = extent;
    std::swap( state.scene, state.pending );
    return DestroySceneTarget( state.pending );
}

bool LoadShader( const char *nativePath, render::render_shader_handle_t &shader )
{
    const std::filesystem::path path = std::filesystem::absolute( nativePath );
    const std::string root = path.parent_path().string();
    const std::string name = path.filename().string();
    common::vfs_directory_t directory{};
    common::blob_t bytes{};
    if ( common::VfsDirectory_Init( &directory, { root.data(), root.size() } ) != common::vfs_status_t::OK ) {
        std::fprintf( stderr, "Cannot mount shader directory: %s\n", root.c_str() );
        return false;
    }
    bool success = common::Blob_Init( &bytes, common::Allocator_GetSystem() );
    if ( success ) {
        const auto vfs = common::VfsDirectory_Make( &directory );
        success = common::Vfs_ReadAll( &vfs, { name.data(), name.size() },
            40u * 1024u * 1024u, &bytes ) == common::vfs_status_t::OK;
    }
    common::VfsDirectory_Shutdown( &directory );
    if ( success ) {
        common::cooked_shader_view_t cooked{};
        success = common::CookedShader_Succeeded( common::CookedShader_Read(
            common::Blob_Block( &bytes ), &cooked ) ) &&
            Check( render::R_CreateShader( { &cooked, nativePath }, &shader ), "Create cooked shader" );
    }
    common::Blob_Shutdown( &bytes );
    if ( !success ) std::fprintf( stderr, "Failed to load cooked shader: %s\n", nativePath );
    return success;
}

bool CreateMesh( const render::render_static_mesh_source_t &source,
    render::render_static_mesh_bundle_t &output ) noexcept
{
    const auto result = render::R_CreateStaticMeshBundle( source, &output );
    if ( result.status == render::render_static_mesh_bridge_status_t::OK ) return true;
    std::fprintf( stderr, "Create mesh: bridge status %u, %s\n",
        static_cast<unsigned>( result.status ), render::R_ErrorName( result.renderError ) );
    return false;
}

bool CreateMeshes( state_t &state ) noexcept
{
    // Each face has four vertices so normals remain flat at the cube edges.
    vertex_t vertices[24]{};
    common::u16 indices[36]{};
    constexpr float CORNERS[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for ( common::u32 face = 0u; face < 6u; ++face ) {
        const common::u32 axis = face / 2u;
        const common::u32 u = ( axis + 1u ) % 3u, v = ( axis + 2u ) % 3u;
        const float sign = face % 2u == 0u ? 1.0f : -1.0f;
        for ( common::u32 corner = 0u; corner < 4u; ++corner ) {
            auto &vertex = vertices[face * 4u + corner];
            vertex.position[axis] = sign;
            vertex.position[u] = CORNERS[corner][0] * sign;
            vertex.position[v] = CORNERS[corner][1];
            vertex.normal[axis] = sign;
            vertex.uv[0] = ( CORNERS[corner][0] + 1.0f ) * 0.5f;
            vertex.uv[1] = ( CORNERS[corner][1] + 1.0f ) * 0.5f;
        }
        constexpr common::u16 FACE_INDICES[6] = { 0, 1, 2, 0, 2, 3 };
        for ( common::u32 i = 0u; i < 6u; ++i ) {
            indices[face * 6u + i] = static_cast<common::u16>( face * 4u + FACE_INDICES[i] );
        }
    }
    render::render_static_mesh_source_t source{};
    source.vertexLayout.bindingCount = 1u;
    source.vertexLayout.bindings[0].stride = sizeof( vertex_t );
    source.vertexLayout.attributeCount = 3u;
    source.vertexLayout.attributes[0] = { render::render_format_t::RGB32_FLOAT, 0u, 0u, 0u };
    source.vertexLayout.attributes[1] = { render::render_format_t::RGB32_FLOAT, 12u, 1u, 0u };
    source.vertexLayout.attributes[2] = { render::render_format_t::RG32_FLOAT, 24u, 2u, 0u };
    source.vertexBytes = vertices;
    source.vertexByteSize = sizeof( vertices );
    source.vertexCount = 24u;
    source.indexBytes = indices;
    source.indexByteSize = sizeof( indices );
    source.indexCount = 36u;
    source.indexType = render::render_index_type_t::UINT16;
    source.debugName = "World shared box";
    if ( !CreateMesh( source, state.cube ) ) return false;
    for ( common::u32 i = 0u; i < MESH_COUNT; ++i ) {
        state.meshes[i] = state.cube; // Borrowed aliases; only state.cube owns handles.
        state.meshRanges[i] = i == 0u
            ? render::render_world_mesh_range_t{ 0u,36u }
            : render::render_world_mesh_range_t{ ( i - 1u ) * 6u,6u };
    }
    constexpr float SCREEN[3][2] = { { -1, -1 }, { 3, -1 }, { -1, 3 } };
    constexpr common::u16 SCREEN_INDICES[3] = { 0, 1, 2 };
    source = render::render_static_mesh_source_t{};
    source.vertexLayout.bindingCount = 1u;
    source.vertexLayout.bindings[0].stride = sizeof( SCREEN[0] );
    source.vertexLayout.attributeCount = 1u;
    source.vertexLayout.attributes[0] = { render::render_format_t::RG32_FLOAT, 0u, 0u, 0u };
    source.vertexBytes = SCREEN;
    source.vertexByteSize = sizeof( SCREEN );
    source.vertexCount = 3u;
    source.indexBytes = SCREEN_INDICES;
    source.indexByteSize = sizeof( SCREEN_INDICES );
    source.indexCount = 3u;
    source.indexType = render::render_index_type_t::UINT16;
    return CreateMesh( source, state.fullscreen );
}

bool CreateUniform( common::u64 bytes, const void *initial,
    render::render_buffer_handle_t &buffer ) noexcept
{
    render::render_buffer_desc_t description{};
    description.byteSize = bytes;
    description.usage = render::R_BUFFER_USAGE_UNIFORM;
    description.updatePolicy = render::render_buffer_update_t::STREAM;
    description.memory = render::render_buffer_memory_t::UPLOAD;
    const render::render_buffer_data_t data{ initial, bytes };
    return Check( render::R_CreateBuffer( description, initial ? &data : nullptr,
        &buffer ), "Create uniform buffer" );
}

bool CreateResources( state_t &state, bool transparency, bool cutout )
{
    if ( !( cutout
             ? LoadShader( CYPHER_RENDER_WORLD_CUTOUT_SHADER_PATH, state.shaders[4] )
             : LoadShader( CYPHER_RENDER_WORLD_SHADER_PATH, state.shaders[0] ) ) ||
         !LoadShader( CYPHER_RENDER_WORLD_SHADOW_SHADER_PATH, state.shaders[1] ) ||
         !LoadShader( CYPHER_RENDER_WORLD_TONEMAP_SHADER_PATH, state.shaders[2] ) ||
         ( transparency && !( cutout
             ? LoadShader( CYPHER_RENDER_WORLD_CUTOUT_ADDITIVE_SHADER_PATH, state.shaders[5] )
             : LoadShader( CYPHER_RENDER_WORLD_ADDITIVE_SHADER_PATH, state.shaders[3] ) ) ) ||
         ( cutout && !LoadShader( CYPHER_RENDER_WORLD_CUTOUT_SHADOW_SHADER_PATH, state.shaders[6] ) ) ||
         !CreateMeshes( state ) ) return false;
    render::render_pipeline_desc_t pipeline{};
    pipeline.shader = state.shaders[cutout ? 4u : 0u];
    pipeline.vertexLayout = state.cube.vertexLayout;
    pipeline.depthTest = true;
    pipeline.depthWrite = true;
    pipeline.cullBackFaces = true;
    pipeline.uniformBlockCount = 3u;
    pipeline.uniformBlocks[0] = { "View", sizeof( render::render_world_fogged_view_lighting_record_t ) };
    pipeline.uniformBlocks[1] = { "Model", cutout ? 128u : 112u };
    pipeline.uniformBlocks[2] = { "Material", 16u };
    pipeline.sampledTextureCount = 2u;
    pipeline.sampledTextures[0] = { "albedoMap", render::render_sampled_texture_kind_t::SCALAR_2D };
    pipeline.sampledTextures[1] = { "shadowMap", render::render_sampled_texture_kind_t::DEPTH_COMPARE_2D };
    for ( common::u32 winding = 0u; winding < 2u; ++winding ) {
        pipeline.frontCounterClockwise = winding == 0u;
        if ( !Check( render::R_CreateGraphicsPipeline( pipeline,
                 &state.cameraPipelines[winding] ), "Create World camera pipeline" ) ) return false;
    }
    if ( transparency ) {
        auto translucent = pipeline;
        translucent.depthWrite = false;
        translucent.blendMode = render::render_blend_mode_t::STRAIGHT_ALPHA;
        for ( common::u32 winding = 0u; winding < 2u; ++winding ) {
            translucent.frontCounterClockwise = winding == 0u;
            if ( !Check( render::R_CreateGraphicsPipeline( translucent,
                     &state.blendPipelines[winding] ), "Create World alpha pipeline" ) ) return false;
        }
        translucent.shader = state.shaders[cutout ? 5u : 3u];
        translucent.blendMode = render::render_blend_mode_t::ADDITIVE;
        for ( common::u32 winding = 0u; winding < 2u; ++winding ) {
            translucent.frontCounterClockwise = winding == 0u;
            if ( !Check( render::R_CreateGraphicsPipeline( translucent,
                     &state.additivePipelines[winding] ), "Create World additive pipeline" ) ) return false;
        }
    }
    pipeline.shader = state.shaders[1];
    pipeline.colorWrite = false;
    pipeline.depthBiasEnable = true;
    pipeline.depthBiasSlopeFactor = 1.25f;
    pipeline.depthBiasConstantUnits = 2.0f;
    pipeline.uniformBlockCount = 2u;
    pipeline.uniformBlocks[0] = { "Light", 64u };
    pipeline.uniformBlocks[1] = { "Model", 64u };
    pipeline.uniformBlocks[2] = {};
    pipeline.sampledTextureCount = 0u;
    for ( auto &binding : pipeline.sampledTextures ) binding = {};
    for ( common::u32 winding = 0u; winding < 2u; ++winding ) {
        pipeline.frontCounterClockwise = winding == 0u;
        if ( !Check( render::R_CreateGraphicsPipeline( pipeline,
                 &state.shadowPipelines[winding] ), "Create World shadow pipeline" ) ) return false;
    }
    if ( cutout ) {
        auto maskedShadow = pipeline;
        maskedShadow.shader = state.shaders[6];
        maskedShadow.uniformBlocks[1] = { "Model", 80u };
        maskedShadow.sampledTextureCount = 1u;
        maskedShadow.sampledTextures[0] = {
            "albedoMap", render::render_sampled_texture_kind_t::SCALAR_2D
        };
        for ( common::u32 winding = 0u; winding < 2u; ++winding ) {
            maskedShadow.frontCounterClockwise = winding == 0u;
            if ( !Check( render::R_CreateGraphicsPipeline( maskedShadow,
                     &state.maskShadowPipelines[winding] ), "Create World MASK shadow pipeline" ) ) return false;
        }
    }
    pipeline = render::render_pipeline_desc_t{};
    pipeline.shader = state.shaders[2];
    pipeline.vertexLayout = state.fullscreen.vertexLayout;
    pipeline.depthTest = false;
    pipeline.depthWrite = false;
    pipeline.cullBackFaces = false;
    pipeline.sampledTextureCount = 1u;
    pipeline.sampledTextures[0] = { "sceneColor", render::render_sampled_texture_kind_t::SCALAR_2D };
    if ( !Check( render::R_CreateGraphicsPipeline( pipeline,
             &state.presentationPipeline ), "Create tone-map pipeline" ) ) return false;
    render::render_texture_desc_t texture{};
    texture.width = texture.height = 1u;
    texture.generateMips = false;
    constexpr common::u8 WHITE[4] = { 255, 255, 255, 255 };
    if ( !Check( render::R_CreateTexture2D( texture, { WHITE, sizeof( WHITE ) },
             &state.white ), "Create white albedo" ) ) return false;
    if ( cutout ) {
        // Four-by-four opaque/empty tiles on each cube face. The same raw
        // alpha texture is borrowed by camera and shadow MASK draws.
        common::u8 texels[8u * 8u * 4u]{};
        for ( common::u32 y = 0u; y < 8u; ++y ) {
            for ( common::u32 x = 0u; x < 8u; ++x ) {
                const common::u32 offset = ( y * 8u + x ) * 4u;
                texels[offset] = 226u;
                texels[offset + 1u] = 211u;
                texels[offset + 2u] = 157u;
                texels[offset + 3u] = ( ( x / 2u + y / 2u ) & 1u ) == 0u ? 255u : 0u;
            }
        }
        texture.width = texture.height = 8u;
        if ( !Check( render::R_CreateTexture2D( texture,
                 { texels, sizeof( texels ) }, &state.checker ),
                 "Create MASK checker albedo" ) ) return false;
    }
    texture.width = texture.height = SHADOW_SIZE;
    texture.format = render::render_texture_format_t::D24;
    texture.sRGB = false;
    texture.renderTarget = true;
    texture.repeat = false;
    if ( !Check( render::R_CreateTexture2D( texture, {}, &state.shadowDepth ), "Create shadow map" ) ||
         !Check( render::R_CreateRenderTarget( { {}, state.shadowDepth },
             &state.shadowTarget ), "Create depth target" ) ) return false;
    render::render_sampler_desc_t sampler{};
    sampler.minFilter = sampler.magFilter = render::render_sampler_filter_t::LINEAR;
    sampler.addressU = sampler.addressV = render::render_sampler_address_t::CLAMP_TO_EDGE;
    sampler.compare = render::render_sampler_compare_t::LESS_EQUAL;
    if ( !Check( render::R_CreateSampler( sampler, &state.shadowSampler ), "Create comparison sampler" ) ||
         !CreateUniform( sizeof( render::render_world_fogged_view_lighting_record_t ), nullptr, state.view ) ||
         !CreateUniform( 64u, nullptr, state.light ) ) return false;
    constexpr float TINTS[MATERIAL_COUNT][4] = {
        { 0.38f, 0.42f, 0.46f, 1.0f }, { 0.65f, 0.22f, 0.08f, 1.0f },
        { 0.10f, 0.28f, 0.55f, 1.0f }, { 0.09f, 0.65f, 0.75f, 0.42f },
        { 0.9f, 0.56f, 0.12f, 0.78f }
    };
    for ( common::u32 i = 0u; i < ( transparency ? MATERIAL_COUNT : OPAQUE_MATERIAL_COUNT ); ++i ) {
        if ( !CreateUniform( 16u, TINTS[i], state.tints[i] ) ) return false;
        auto &material = state.materials[i];
        const bool additive = i == OPAQUE_MATERIAL_COUNT + 1u;
        const auto *pipelines = additive ? state.additivePipelines
            : ( i == OPAQUE_MATERIAL_COUNT ? state.blendPipelines : state.cameraPipelines );
        material.shader = additive ? state.shaders[cutout ? 5u : 3u]
            : state.shaders[cutout ? 4u : 0u];
        material.pipeline = pipelines[0];
        material.mirroredPipeline = pipelines[1];
        material.textureCount = 1u;
        material.textures[0] = cutout && ( i == 1u || i == 2u )
            ? state.checker : state.white;
        material.passShadowSlot = 1u;
        material.materialUniformBuffer = state.tints[i];
        material.materialUniformSlot = 2u;
        material.alphaMode = cutout && ( i == 1u || i == 2u )
            ? common::render_material_alpha_mode_t::MASK
            : additive ? common::render_material_alpha_mode_t::ADDITIVE
            : ( i == OPAQUE_MATERIAL_COUNT ? common::render_material_alpha_mode_t::BLEND
                : common::render_material_alpha_mode_t::OPAQUE );
        if ( material.alphaMode == common::render_material_alpha_mode_t::MASK ) {
            material.alphaCutoff = 0.5f;
        }
        material.materialFlags = i < OPAQUE_MATERIAL_COUNT
            ? common::COOKED_MATERIAL_FLAG_CASTS_SHADOWS : 0u;
    }
    const auto *info = render::R_GetInfo();
    if ( !info || info->limits.uniformBufferOffsetAlignment == 0u ) return false;
    const common::u64 alignment = info->limits.uniformBufferOffsetAlignment;
    // All camera records opt into Model128 when MASK exists; mixed shadow
    // submissions retain Model64 for opaque casters and Model80 for MASK.
    const auto capacity = [alignment]( common::u64 bytes ) {
        return ( MAX_OBJECT_COUNT - 1u ) * ( ( bytes + alignment - 1u ) / alignment * alignment ) + bytes;
    };
    return Check( render::R_CreateUniformBatch( capacity( cutout ? 128u : 112u ), nullptr,
               &state.cameraModels ), "Create camera model storage" ) &&
        Check( render::R_CreateUniformBatch( capacity( cutout ? 80u : 64u ), nullptr,
            &state.shadowModels ), "Create shadow model storage" );
}

world::world_object_t Object( common::u32 index, float seconds, bool materialRanges ) noexcept
{
    world::world_object_t object{};
    const common::u32 opaqueCount = materialRanges ? MAX_OPAQUE_OBJECT_COUNT : 3u;
    if ( index >= opaqueCount ) {
        // Two floating thin boxes make blending and additive fog visible over
        // the opaque scene. They use the full cube range even in face mode.
        const bool additive = index == opaqueCount + 1u;
        object.localToWorld = math::Affine3_FromTRS(
            additive ? math::vec3_t{ 0.65f, -2.4f, 1.7f }
                     : math::vec3_t{ -0.8f, -2.3f, 1.55f },
            math::CY_QUAT_IDENTITY,
            additive ? math::vec3_t{ -0.52f, 0.12f, 0.52f }
                     : math::vec3_t{ 0.9f, 0.12f, 0.8f } );
        object.worldBounds = math::Aabb_TransformAffine( LOCAL_BOUNDS, object.localToWorld );
        object.meshSlot = 0u;
        object.materialSlot = additive ? OPAQUE_MATERIAL_COUNT + 1u : OPAQUE_MATERIAL_COUNT;
        object.layerMask = 1u;
        return object;
    }
    const common::u32 body = materialRanges && index > 0u ? 1u + ( index - 1u ) / 6u : index;
    const auto rotation = math::Quat_FromUnitAxisAngle( math::CY_VEC3_UP,
        math::Angle_FromRadians( seconds * 0.45f ) );
    if ( body == 0u ) {
        object.localToWorld = math::Affine3_FromTRS(
            { 0, 0, -0.2f }, math::CY_QUAT_IDENTITY, { 7, 6, 0.2f } );
    } else {
        // One mirrored, nonuniform box exercises both normal packing and CW selection.
        object.localToWorld = math::Affine3_FromTRS(
            body == 1u ? math::vec3_t{ -2, 0, 1.2f } : math::vec3_t{ 2, 1, 1 },
            rotation, body == 1u ? math::vec3_t{ 0.8f, 1, 1.2f } : math::vec3_t{ -1, 0.7f, 1 } );
    }
    object.worldBounds = math::Aabb_TransformAffine( LOCAL_BOUNDS, object.localToWorld );
    object.meshSlot = 0u;
    object.materialSlot = body;
    if ( materialRanges && index > 0u ) {
        const common::u32 face = ( index - 1u ) % 6u;
        object.meshSlot = face + 1u;
        object.materialSlot = ( body + face ) % OPAQUE_MATERIAL_COUNT;
        // Each face keeps the box's conservative full bounds and transform.
    }
    object.layerMask = 1u;
    return object;
}

bool CreateWorld( state_t &state, bool materialRanges, bool transparency ) noexcept
{
    const auto *allocator = common::Allocator_GetSystem();
    state.objectCount = ( materialRanges ? MAX_OPAQUE_OBJECT_COUNT : 3u ) +
        ( transparency ? 2u : 0u );
    if ( !Check( world::World_Init( &state.pWorld, allocator, MAX_OBJECT_COUNT ), "Create World" ) ||
         !Check( world::WorldSubmission_Init( &state.cameraSubmission, allocator, MAX_OBJECT_COUNT ), "Create camera submission" ) ||
         !Check( world::WorldSubmission_Init( &state.lightSubmission, allocator, MAX_OBJECT_COUNT ), "Create light submission" ) ) return false;
    for ( common::u32 i = 0u; i < state.objectCount; ++i ) {
        const auto object = Object( i, 0.0f, materialRanges );
        if ( !Check( world::World_AddObject( state.pWorld, &object,
                 &state.objects[i] ), "Add World object" ) ) return false;
    }
    return true;
}

bool Capture( state_t &state, bool enabled ) noexcept
{
    if ( !Check( sys::Sys_SetRelativeMouseMode( state.window, enabled ), "Set camera capture" ) ) return false;
    if ( !Check( sys::Sys_SetCursorVisible( !enabled ), "Set camera cursor" ) ) return false;
    state.cursorHidden = enabled;
    return true;
}
struct input_t {
    bool held[static_cast<common::u32>( sys::sys_key_t::COUNT )]{};
    float dx{ 0 }, dy{ 0 };
};

bool PollInput( state_t &state, input_t &input, bool interactive ) noexcept
{
    input.dx = input.dy = 0;
    bool wantsCapture = state.window.relativeMouseEnabled && state.cursorHidden;
    bool eligible = wantsCapture;
    bool focused = state.window.focused;
    const auto dropped = sys::Sys_DroppedEventCount();
    sys::Sys_PollWindowEvents( state.window );
    sys::sys_event_t event{};
    while ( sys::Sys_PollEvent( event ) ) {
        if ( event.type == sys::sys_event_type_t::QUIT_REQUESTED ) sys::Sys_RequestQuit();
        else if ( event.type == sys::sys_event_type_t::WINDOW_FOCUS_LOST && event.payload.window.windowId == state.window.id ) {
            focused = wantsCapture = eligible = false;
            input = input_t{};
        } else if ( event.type == sys::sys_event_type_t::WINDOW_FOCUS_GAINED && event.payload.window.windowId == state.window.id ) focused = true;
        else if ( event.type == sys::sys_event_type_t::WINDOW_MINIMIZED && event.payload.window.windowId == state.window.id ) {
            wantsCapture = eligible = false;
            input = input_t{};
        } else if ( event.type == sys::sys_event_type_t::KEY && event.payload.key.windowId == state.window.id ) {
            const auto &key = event.payload.key;
            if ( key.key == sys::sys_key_t::ESCAPE && key.action == sys::sys_input_action_t::PRESSED ) sys::Sys_RequestQuit();
            else if ( interactive && focused ) {
                const auto index = static_cast<common::u32>( key.key );
                if ( index < static_cast<common::u32>( sys::sys_key_t::COUNT ) ) {
                    if ( key.action == sys::sys_input_action_t::PRESSED ) input.held[index] = true;
                    else if ( key.action == sys::sys_input_action_t::RELEASED ) input.held[index] = false;
                }
            }
        } else if ( interactive && event.type == sys::sys_event_type_t::MOUSE_BUTTON &&
                    event.payload.mouseButton.windowId == state.window.id && event.payload.mouseButton.button == sys::sys_mouse_button_t::RIGHT ) {
            if ( event.payload.mouseButton.action == sys::sys_input_action_t::RELEASED ) {
                wantsCapture = eligible = false;
                input.dx = input.dy = 0;
            } else if ( focused ) wantsCapture = true;
        } else if ( interactive && focused && eligible && wantsCapture &&
                    event.type == sys::sys_event_type_t::MOUSE_MOTION && event.payload.mouseMotion.windowId == state.window.id ) {
            input.dx += event.payload.mouseMotion.deltaX;
            input.dy += event.payload.mouseMotion.deltaY;
        }
    }
    if ( dropped != sys::Sys_DroppedEventCount() || !state.window.focused || state.window.minimized ||
         sys::Sys_IsQuitRequested() || sys::Sys_WindowShouldClose( state.window ) ) {
        wantsCapture = false;
        input = input_t{};
    }
    return !interactive || ( wantsCapture == state.window.relativeMouseEnabled &&
        wantsCapture == state.cursorHidden ) || Capture( state, wantsCapture );
}

bool Query( world::world_submission_t *submission, const state_t &state,
    const math::mat4_t &clipFromWorld, math::vec3_t position,
    world::world_submission_token_t &token ) noexcept
{
    world::world_view_query_t query{};
    query.cameraPosition = position;
    query.layerMask = 1u;
    if ( !math::Frustum_TryFromViewProjection( clipFromWorld,
             math::clip_depth_range_t::NEGATIVE_ONE_TO_ONE, 0.000001f, &query.frustum ) ) return false;
    world::WorldSubmission_End( submission );
    return Check( world::WorldSubmission_Build( submission, state.pWorld,
        &query, &token ), "Build World view" );
}

bool RunFrames( state_t &state, const options_t &options ) noexcept
{
    math::mat4_t lightView{}, lightProjection{};
    if ( !math::Mat4_TryLookAtRH( LIGHT_POSITION, {}, math::CY_VEC3_UP, 0.0001f, &lightView ) ||
         !math::Mat4_TryOrthographicRH( -12, 12, -12, 12, 1, 40,
             math::clip_depth_range_t::NEGATIVE_ONE_TO_ONE, &lightProjection ) ) return false;
    const auto lightClip = math::Mat4_Multiply( lightProjection, lightView );
    if ( !Check( render::R_UpdateBuffer( state.light, 0u, { &lightClip, sizeof( lightClip ) } ), "Upload light view" ) ) return false;
    camera::cube_camera_t viewpoint{};
    if ( !camera::CubeCamera_FromLookAt( { 9, -13, 9 }, { 0, 0, 0.5f }, 5, &viewpoint ) ) return false;
    input_t input{};
    const bool interactive = options.frames == 0u && !options.hidden;
    const double started = sys::Sys_TimeNowSeconds();
    double previous = started;
    float elapsed = 0;
    common::u32 frames = 0u;
    common::u32 resizeCount = 0u;
    common::u64 opaqueDraws = 0u, blendDraws = 0u,
        additiveDraws = 0u, shadowDraws = 0u;
    while ( !sys::Sys_IsQuitRequested() && !sys::Sys_WindowShouldClose( state.window ) ) {
        if ( options.resizeTest && ( frames == options.frames / 3u || frames == options.frames - options.frames / 3u ) ) {
            const bool smaller = frames == options.frames / 3u;
            if ( !Check( sys::Sys_SetWindowSize( state.window, smaller ? 800u : 1100u,
                     smaller ? 600u : 760u ), "Request smoke resize" ) ) return false;
        }
        if ( !PollInput( state, input, interactive ) ) return false;
        if ( sys::Sys_IsQuitRequested() || sys::Sys_WindowShouldClose( state.window ) ) break;
        const double now = sys::Sys_TimeNowSeconds();
        const float dt = static_cast<float>( std::clamp( now - previous, 0.0, 0.1 ) );
        previous = now;
        if ( options.frames && now - started > std::max( 30.0, 10.0 + options.frames / 15.0 ) ) {
            std::fputs( "World example timed out.\n", stderr ); return false;
        }
        if ( state.window.minimized || !state.window.width || !state.window.height ) {
            sys::Sys_SleepMilliseconds( 16u ); continue;
        }
        const render::render_extent_t extent{ state.window.width, state.window.height };
        if ( extent.width != state.scene.extent.width || extent.height != state.scene.extent.height ) {
            if ( !Check( render::R_Resize( extent ), "Resize renderer" ) || !ResizeScene( state, extent ) ) return false;
            ++resizeCount;
        }
        const auto held = [&input]( sys::sys_key_t key ) { return input.held[static_cast<common::u32>( key )] ? 1.0f : 0.0f; };
        if ( interactive && state.window.relativeMouseEnabled ) {
            const camera::cube_camera_input_t movement{
                held( sys::sys_key_t::W ) - held( sys::sys_key_t::S ),
                held( sys::sys_key_t::D ) - held( sys::sys_key_t::A ),
                held( sys::sys_key_t::SPACE ) - held( sys::sys_key_t::C ), input.dx, input.dy,
                held( sys::sys_key_t::LEFT_SHIFT ) != 0 || held( sys::sys_key_t::RIGHT_SHIFT ) != 0
            };
            if ( !camera::CubeCamera_Advance( &viewpoint, movement, dt ) ) return false;
        }
        elapsed += interactive ? dt : 1.0f / 60.0f;
        for ( common::u32 i = 1u; i < state.objectCount; ++i ) {
            const auto object = Object( i, elapsed, options.materialRanges );
            if ( !Check( world::World_UpdateObject( state.pWorld, state.objects[i], &object ), "Move World object" ) ) return false;
        }
        render::render_world_view_lighting_desc_t lighting{};
        if ( !camera::CubeCamera_TryView( viewpoint, &lighting.viewFromWorld ) ||
             !math::Mat4_TryPerspectiveRH( math::Angle_FromDegrees( 45 ),
                 static_cast<float>( extent.width ) / extent.height, 0.1f, 100,
                 math::clip_depth_range_t::NEGATIVE_ONE_TO_ONE, &lighting.clipFromView ) ) return false;
        lighting.ambientLight = { 0.15f, 0.17f, 0.21f };
        lighting.directionToLight = LIGHT_POSITION;
        lighting.directionalRadiance = { 2.2f, 2.05f, 1.8f };
        lighting.pointLightCount = 1u;
        lighting.pointLights[0] = { { 0, -2, 2 }, 6, { 0.15f, 0.4f, 1 }, 2 };
        const render::render_world_depth_fog_desc_t fog{
            { 0.11f, 0.14f, 0.18f }, 8.0f, 32.0f, !options.noFog
        };
        render::render_world_fogged_view_lighting_record_t viewRecord{};
        if ( !Check( render::R_PackWorldFoggedViewLighting( lighting, lightClip,
                 fog, &viewRecord ), "Pack World lighting and depth fog" ) ||
             !Check( render::R_UpdateBuffer( state.view, 0u, { &viewRecord, sizeof( viewRecord ) } ), "Upload camera lighting" ) ||
             !Check( render::R_ResetUniformBatch( &state.cameraModels ), "Reset camera models" ) ||
             !Check( render::R_ResetUniformBatch( &state.shadowModels ), "Reset light models" ) ) return false;
        world::world_submission_token_t cameraToken{}, lightToken{};
        if ( !Query( state.cameraSubmission, state, math::Mat4_Multiply(
                 lighting.clipFromView, lighting.viewFromWorld ), viewpoint.position, cameraToken ) ||
             !Query( state.lightSubmission, state, lightClip, LIGHT_POSITION, lightToken ) ) return false;
        render::render_world_camera_draw_t cameraOutput[MAX_OBJECT_COUNT]{};
        render::render_world_shadow_draw_t lightOutput[MAX_OBJECT_COUNT]{};
        render::render_scene_item_t sceneScratch[MAX_OBJECT_COUNT]{};
        common::u32 indexScratch[MAX_OBJECT_COUNT]{};
        render::render_world_camera_prepare_desc_t cameraPrepare{};
        cameraPrepare.submission = state.cameraSubmission;
        cameraPrepare.token = cameraToken;
        cameraPrepare.meshes = state.meshes;
        cameraPrepare.meshCount = options.materialRanges ? MESH_COUNT : 1u;
        cameraPrepare.meshRanges = options.materialRanges ? state.meshRanges : nullptr;
        cameraPrepare.meshRangeCount = options.materialRanges ? MESH_COUNT : 0u;
        cameraPrepare.materials = state.materials;
        cameraPrepare.materialCount = options.transparency ? MATERIAL_COUNT : OPAQUE_MATERIAL_COUNT;
        cameraPrepare.modelUniforms = &state.cameraModels;
        cameraPrepare.modelUniformSlot = 1u;
        cameraPrepare.modelLayout = options.cutout
            ? render::render_world_model_layout_t::MODEL_NORMAL_CUTOUT
            : render::render_world_model_layout_t::MODEL_NORMAL;
        cameraPrepare.viewFromWorld = lighting.viewFromWorld;
        cameraPrepare.sceneItemsScratch = sceneScratch;
        cameraPrepare.sceneItemCapacity = MAX_OBJECT_COUNT;
        cameraPrepare.passIndicesScratch = indexScratch;
        cameraPrepare.passIndexCapacity = MAX_OBJECT_COUNT;
        cameraPrepare.drawTemplate.uniformBuffers[0] = state.view;
        cameraPrepare.drawTemplate.uniformRanges[0] = { 0u, sizeof( viewRecord ) };
        cameraPrepare.drawTemplate.sampledTextures[1] = state.shadowDepth;
        cameraPrepare.drawTemplate.samplers[1] = state.shadowSampler;
        render::render_world_camera_pass_counts_t counts{};
        if ( !Check( render::R_PrepareWorldCameraDraws( cameraPrepare,
                 cameraOutput, MAX_OBJECT_COUNT, &counts ), "Prepare World camera" ) ) return false;
        render::render_world_shadow_prepare_desc_t shadowPrepare{};
        shadowPrepare.submission = state.lightSubmission;
        shadowPrepare.token = lightToken;
        shadowPrepare.meshes = state.meshes;
        shadowPrepare.meshCount = cameraPrepare.meshCount;
        shadowPrepare.meshRanges = cameraPrepare.meshRanges;
        shadowPrepare.meshRangeCount = cameraPrepare.meshRangeCount;
        shadowPrepare.materials = state.materials;
        shadowPrepare.materialCount = cameraPrepare.materialCount;
        shadowPrepare.modelUniforms = &state.shadowModels;
        shadowPrepare.modelUniformSlot = 1u;
        shadowPrepare.shadowPipeline = state.shadowPipelines[0];
        shadowPrepare.mirroredShadowPipeline = state.shadowPipelines[1];
        shadowPrepare.maskShadowPipeline = state.maskShadowPipelines[0];
        shadowPrepare.mirroredMaskShadowPipeline = state.maskShadowPipelines[1];
        shadowPrepare.drawTemplate.uniformBuffers[0] = state.light;
        shadowPrepare.drawTemplate.uniformRanges[0] = { 0u, sizeof( lightClip ) };
        common::u32 casterCount = 0u;
        if ( !Check( render::R_PrepareWorldShadowDraws( shadowPrepare,
                 lightOutput, MAX_OBJECT_COUNT, &casterCount ), "Prepare World shadows" ) ) return false;
        // Submissions are value snapshots. Draws now borrow only renderer resources.
        world::WorldSubmission_End( state.cameraSubmission );
        world::WorldSubmission_End( state.lightSubmission );
        render::render_draw_indexed_desc_t present{};
        present.pipeline = state.presentationPipeline;
        present.sampledTextures[0] = state.scene.color;
        if ( !Check( render::R_ApplyStaticMeshBundleToDraw( state.fullscreen,
                 &present ), "Prepare presentation" ) ) return false;
        // Extract plain draw values so pass execution stays independent of
        // World IDs/queries. This bounded copy needs no per-frame allocation.
        render::render_draw_indexed_desc_t cameraDrawRecords[MAX_OBJECT_COUNT]{};
        render::render_draw_indexed_desc_t shadowDrawRecords[MAX_OBJECT_COUNT]{};
        const common::u32 cameraCount = counts.opaqueCount + counts.blendCount + counts.additiveCount;
        for ( common::u32 i = 0u; i < cameraCount; ++i ) cameraDrawRecords[i] = cameraOutput[i].draw;
        for ( common::u32 i = 0u; i < casterCount; ++i ) shadowDrawRecords[i] = lightOutput[i].draw;
        render::render_scene_view_desc_t sceneView{};
        sceneView.shadowPass.target = state.shadowTarget;
        sceneView.shadowDraws = { shadowDrawRecords, casterCount };
        sceneView.cameraPass.target = state.scene.target;
        sceneView.cameraPass.clearColor = fog.enabled
            ? render::render_color_t{ fog.color.x, fog.color.y, fog.color.z, 1.0f }
            : render::render_color_t{ 0.025f, 0.035f, 0.05f, 1.0f };
        sceneView.opaqueDraws = { cameraDrawRecords, counts.opaqueCount };
        sceneView.blendDraws = { cameraDrawRecords + counts.opaqueCount, counts.blendCount };
        sceneView.additiveDraws = { cameraDrawRecords + counts.opaqueCount + counts.blendCount, counts.additiveCount };
        sceneView.outputDraw = present;
        render::render_frame_info_t frame{};
        frame.frameIndex = frames;
        frame.deltaSeconds = dt;
        frame.drawableExtent = extent;
        if ( !Check( render::R_BeginFrame( frame ), "Begin frame" ) ) return false;
        const auto result = render::R_ExecuteSceneView( sceneView );
        const bool drawn = Check( result.error, "Execute World scene view" );
        if ( result.cleanupError != render::render_error_t::OK ) {
            (void)Check( result.cleanupError, "Close failed World scene pass" );
        }
        const bool ended = Check( render::R_EndFrame(), "End frame" );
        if ( !drawn || !ended ) return false;
        opaqueDraws += result.opaqueDraws;
        blendDraws += result.blendDraws;
        additiveDraws += result.additiveDraws;
        shadowDraws += result.shadowDraws;
        if ( options.frames ) {
            const common::u32 expectedOpaque = options.materialRanges ? MAX_OPAQUE_OBJECT_COUNT : 3u;
            const common::u32 expectedTranslucent = options.transparency ? 1u : 0u;
            if ( cameraCount != state.objectCount || counts.opaqueCount != expectedOpaque ||
                 counts.blendCount != expectedTranslucent || counts.additiveCount != expectedTranslucent ||
                 casterCount != expectedOpaque || result.opaqueDraws != expectedOpaque ||
                 result.blendDraws != expectedTranslucent ||
                 result.additiveDraws != expectedTranslucent || result.shadowDraws != expectedOpaque ) {
                std::fputs( "Fixed view did not draw the expected opaque, blend, additive and shadow passes.\n", stderr );
                return false;
            }
        }
        ++frames;
        if ( options.frames && frames >= options.frames ) break;
    }
    std::printf( "World lighting (%s, %u records): %u frames, %llu opaque + %llu blend + %llu additive camera draws, %llu shadow draws, %u tone-map draws, %u resizes.\n",
        options.materialRanges ? "material ranges" : "whole meshes", state.objectCount,
        frames, static_cast<unsigned long long>( opaqueDraws ),
        static_cast<unsigned long long>( blendDraws ),
        static_cast<unsigned long long>( additiveDraws ),
        static_cast<unsigned long long>( shadowDraws ), frames, resizeCount );
    return ( options.frames == 0u || frames == options.frames ) &&
        ( !options.resizeTest || resizeCount >= 2u );
}

bool Shutdown( state_t &state ) noexcept
{
    bool success = true;
    if ( state.window.valid && ( state.window.relativeMouseEnabled || state.cursorHidden ) ) success = Capture( state, false );
    world::WorldSubmission_Shutdown( &state.lightSubmission );
    world::WorldSubmission_Shutdown( &state.cameraSubmission );
    world::World_Shutdown( &state.pWorld );
    if ( render::R_IsInitialized() ) {
        if ( render::R_IsFrameActive() ) success = Check( render::R_EndFrame(), "Finish frame" ) && success;
        success = Check( render::R_WaitIdle(), "Wait for renderer" ) && success;
        for ( auto &pipeline : state.cameraPipelines ) success = Release( pipeline, render::R_DestroyGraphicsPipeline, "Destroy camera pipeline" ) && success;
        for ( auto &pipeline : state.blendPipelines ) success = Release( pipeline, render::R_DestroyGraphicsPipeline, "Destroy alpha pipeline" ) && success;
        for ( auto &pipeline : state.additivePipelines ) success = Release( pipeline, render::R_DestroyGraphicsPipeline, "Destroy additive pipeline" ) && success;
        for ( auto &pipeline : state.shadowPipelines ) success = Release( pipeline, render::R_DestroyGraphicsPipeline, "Destroy shadow pipeline" ) && success;
        for ( auto &pipeline : state.maskShadowPipelines ) success = Release( pipeline, render::R_DestroyGraphicsPipeline, "Destroy MASK shadow pipeline" ) && success;
        success = Release( state.presentationPipeline, render::R_DestroyGraphicsPipeline, "Destroy presentation pipeline" ) && success;
        success = DestroySceneTarget( state.pending ) && success;
        success = DestroySceneTarget( state.scene ) && success;
        success = Release( state.shadowTarget, render::R_DestroyRenderTarget, "Destroy shadow target" ) && success;
        success = Release( state.shadowDepth, render::R_DestroyTexture, "Destroy shadow depth" ) && success;
        success = Release( state.shadowSampler, render::R_DestroySampler, "Destroy comparison sampler" ) && success;
        success = Release( state.white, render::R_DestroyTexture, "Destroy albedo" ) && success;
        success = Release( state.checker, render::R_DestroyTexture, "Destroy MASK albedo" ) && success;
        for ( auto &buffer : state.tints ) success = Release( buffer, render::R_DestroyBuffer, "Destroy material buffer" ) && success;
        success = Release( state.view, render::R_DestroyBuffer, "Destroy View buffer" ) && success;
        success = Release( state.light, render::R_DestroyBuffer, "Destroy Light buffer" ) && success;
        success = Check( render::R_DestroyUniformBatch( &state.cameraModels ), "Destroy camera batch" ) && success;
        success = Check( render::R_DestroyUniformBatch( &state.shadowModels ), "Destroy shadow batch" ) && success;
        success = Check( render::R_DestroyStaticMeshBundle( &state.cube ), "Destroy shared box" ) && success;
        success = Check( render::R_DestroyStaticMeshBundle( &state.fullscreen ), "Destroy fullscreen mesh" ) && success;
        for ( auto &shader : state.shaders ) success = Release( shader, render::R_DestroyShader, "Destroy shader" ) && success;
        success = Check( render::R_Shutdown(), "Shut down renderer" ) && success;
    }
    // A failed buffer destroy keeps CPU staging alive. Once shutdown has
    // retired the renderer registry, the helper can release that storage.
    // Preserve the original failure instead of turning cleanup into success.
    if ( !render::R_IsInitialized() ) {
        if ( state.cameraModels.staging ) success = Check( render::R_DestroyUniformBatch(
            &state.cameraModels ), "Release retained camera staging" ) && success;
        if ( state.shadowModels.staging ) success = Check( render::R_DestroyUniformBatch(
            &state.shadowModels ), "Release retained shadow staging" ) && success;
    }
    if ( state.window.valid ) success = Check( sys::Sys_DestroyWindow( state.window ), "Destroy window" ) && success;
    if ( sys::Sys_IsInitialized() ) success = Check( sys::Sys_Shutdown(), "Shut down System" ) && success;
    return success;
}

bool Run( int argc, char **argv, const options_t &options, state_t &state )
{
    if ( !Check( sys::Sys_Init( { argc, argv, "CypherWorldLighting", "CypherEngine" } ), "Initialize System" ) ) return false;
    auto config = render::R_DefaultConfig();
    config.backend = render::render_backend_t::OPENGL;
    config.presentMode = options.hidden ? render::render_present_mode_t::IMMEDIATE : render::render_present_mode_t::FIFO;
    config.sRGBFramebuffer = false; // Tone-map shader performs the sole output transfer.
    config.requireAcceleration = false;
    config.requiredCapabilities |= render::R_CAPABILITY_FLOAT_RENDER_TARGETS;
    config.optionalCapabilities = render::R_CAPABILITY_NONE;
    sys::window_desc_t window{};
    window.title = "Cypher World Lighting | Hold RMB + WASD to fly | Esc exits";
    window.width = 1100u;
    window.height = 760u;
    if ( options.hidden ) window.flags |= sys::SYS_WINDOW_HIDDEN;
    if ( !Check( render::R_ConfigureWindow( config, window ), "Configure window" ) ||
         !Check( sys::Sys_CreateWindow( window, state.window ), "Create window" ) ||
         !Check( render::R_Init( state.window, config ), "Initialize renderer" ) ||
         !CreateResources( state, options.transparency, options.cutout ) ||
         !CreateWorld( state, options.materialRanges, options.transparency ) ||
         !ResizeScene( state, render::R_GetInfo()->drawableExtent ) ) return false;
    return RunFrames( state, options );
}
} // namespace

int main( int argc, char **argv )
{
    options_t options{};
    for ( int i = 1; i < argc; ++i ) {
        if ( std::strcmp( argv[i], "--hidden" ) == 0 ) options.hidden = true;
        else if ( std::strcmp( argv[i], "--no-fog" ) == 0 ) options.noFog = true;
        else if ( std::strcmp( argv[i], "--material-ranges" ) == 0 ) options.materialRanges = true;
        else if ( std::strcmp( argv[i], "--transparency" ) == 0 ) options.transparency = true;
        else if ( std::strcmp( argv[i], "--cutout" ) == 0 ) options.cutout = true;
        else if ( std::strcmp( argv[i], "--resize-test" ) == 0 ) options.resizeTest = true;
        else if ( std::strcmp( argv[i], "--help" ) == 0 ) options.help = true;
        else if ( std::strcmp( argv[i], "--frames" ) == 0 && i + 1 < argc ) {
            const char *first = argv[++i], *last = first + std::strlen( first );
            const auto parsed = std::from_chars( first, last, options.frames );
            if ( parsed.ec != std::errc{} || parsed.ptr != last || options.frames == 0u ) {
                std::fputs( "--frames requires a positive 32-bit integer.\n", stderr ); return 2;
            }
        } else { std::fprintf( stderr, "Unknown option or missing value: %s\n", argv[i] ); return 2; }
    }
    if ( options.help ) {
        std::puts( "Usage: cypher_render_world_lighting [--hidden] [--frames N] [--resize-test] [--no-fog] [--material-ranges] [--transparency] [--cutout]\n"
            "Hold RMB: mouse look, WASD, Space/C up/down, Shift faster. Esc exits.\n"
            "--frames uses a fixed camera and deterministic animation.\n"
            "--hidden defaults to 120 frames; a working graphics session is required.\n"
            "--resize-test requests two native window resizes during a finite run.\n"
            "--no-fog disables linear camera-depth fog for comparison.\n"
            "--material-ranges gives each box face its own material draw using the shared cube mesh.\n"
            "--transparency adds one alpha-blended box and one additive box, neither casting shadows.\n"
            "--cutout gives the two boxes a perforated albedo and matching alpha-tested shadows.\n"
            "The host owns a procedural scene, light descriptions and pass order.\n"
            "This is a World-adapter example, not a map loader or game runtime." );
        return 0;
    }
    if ( ( options.hidden || options.resizeTest ) && !options.frames ) options.frames = 120u;
    if ( options.resizeTest && options.frames < 6u ) {
        std::fputs( "--resize-test requires at least 6 frames.\n", stderr ); return 2;
    }
    state_t state{};
    bool success = false;
    try { success = Run( argc, argv, options, state ); }
    catch ( const std::exception &error ) { std::fprintf( stderr, "World lighting: %s\n", error.what() ); }
    const bool cleaned = Shutdown( state );
    return success && cleaned ? 0 : 1;
}
